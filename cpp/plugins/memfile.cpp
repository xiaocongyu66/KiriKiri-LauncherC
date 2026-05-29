#include "ncbind.hpp"
#include "StorageIntf.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#define NCB_MODULE_NAME TJS_W("memfile.dll")
#define BASENAME TJS_W("mem")

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

class MemFileStream : public tTJSBinaryStream {
public:
    MemFileStream(std::vector<tjs_uint8> &data, tjs_uint32 flags) :
        _data(data), _pos(0), _writable(false) {
        switch(flags & TJS_BS_ACCESS_MASK) {
            case TJS_BS_WRITE:
                _data.clear();
                _writable = true;
                break;
            case TJS_BS_APPEND:
                _pos = _data.size();
                _writable = true;
                break;
            case TJS_BS_UPDATE:
                _writable = true;
                break;
            default:
                break;
        }
    }

    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
        tjs_int64 base = 0;
        switch(whence) {
            case TJS_BS_SEEK_CUR:
                base = static_cast<tjs_int64>(_pos);
                break;
            case TJS_BS_SEEK_END:
                base = static_cast<tjs_int64>(_data.size());
                break;
            case TJS_BS_SEEK_SET:
            default:
                break;
        }

        tjs_int64 next = base + offset;
        if(next < 0)
            next = 0;
        _pos = static_cast<size_t>(next);
        return static_cast<tjs_uint64>(_pos);
    }

    tjs_uint Read(void *buffer, tjs_uint read_size) override {
        if(!buffer || read_size == 0 || _pos >= _data.size())
            return 0;
        const size_t readable =
            std::min<size_t>(read_size, _data.size() - _pos);
        std::memcpy(buffer, _data.data() + _pos, readable);
        _pos += readable;
        return static_cast<tjs_uint>(readable);
    }

    tjs_uint Write(const void *buffer, tjs_uint write_size) override {
        if(!_writable || !buffer || write_size == 0)
            return 0;
        const size_t end = _pos + write_size;
        if(end > _data.size())
            _data.resize(end, 0);
        std::memcpy(_data.data() + _pos, buffer, write_size);
        _pos = end;
        return write_size;
    }

    void SetEndOfStorage() override {
        if(_writable)
            _data.resize(_pos);
    }

    tjs_uint64 GetSize() override {
        return static_cast<tjs_uint64>(_data.size());
    }

private:
    std::vector<tjs_uint8> &_data;
    size_t _pos;
    bool _writable;
};

class FileInfo {
public:
    using Directory = std::map<ttstr, std::unique_ptr<FileInfo>>;

    FileInfo(FileInfo *parent, const ttstr &name, bool directory = false) :
        _parent(parent), _name(name), _directory(directory) {}

    bool isDirectory() const { return _directory; }

    tjs_uint64 getSize() const { return _data.size(); }

    tTJSVariant getData() const {
        static const tjs_uint8 empty = 0;
        const tjs_uint8 *ptr = _data.empty() ? &empty : _data.data();
        return tTJSVariant(ptr, static_cast<tjs_uint>(_data.size()));
    }

    tTJSVariant getInfo() const {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return tTJSVariant();

        tTJSVariant name(_name);
        tTJSVariant size(static_cast<tjs_int64>(getSize()));
        tTJSVariant isDirectory(_directory ? 1 : 0);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &name, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("size"), nullptr, &size, dict);
        dict->PropSet(TJS_MEMBERENSURE, TJS_W("isDirectory"), nullptr,
                      &isDirectory, dict);

        tTJSVariant ret(dict, dict);
        dict->Release();
        return ret;
    }

    void getDirectory(iTVPStorageLister *lister) const {
        if(!_directory || !lister)
            return;
        for(const auto &it : _children) {
            if(!it.second->isDirectory())
                lister->Add(it.first);
        }
    }

    void getDirectory(iTJSDispatch2 *array) const {
        if(!_directory || !array)
            return;
        for(const auto &it : _children) {
            tTJSVariant item = it.second->getInfo();
            tTJSVariant *param = &item;
            array->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, &param,
                            array);
        }
    }

    const FileInfo *find(const ttstr &path) const {
        if(!_directory || path.IsEmpty())
            return nullptr;

        ttstr head;
        ttstr tail;
        splitPath(path, head, tail);
        auto it = _children.find(head);
        if(it == _children.end())
            return nullptr;
        if(tail.IsEmpty())
            return it->second.get();
        return it->second->find(tail);
    }

    const FileInfo *findFile(const ttstr &path) const {
        const FileInfo *file = find(path);
        return file && !file->isDirectory() ? file : nullptr;
    }

    const FileInfo *findDirectory(const ttstr &path) const {
        const FileInfo *dir = path.IsEmpty() ? this : find(path);
        return dir && dir->isDirectory() ? dir : nullptr;
    }

    bool mkdir(const ttstr &path) { return ensureDirectory(path) != nullptr; }

    bool removeFile(const ttstr &path) {
        FileInfo *parent = nullptr;
        ttstr leaf;
        FileInfo *entry = findMutableWithParent(path, parent, leaf);
        if(!entry || entry->isDirectory() || !parent)
            return false;
        parent->_children.erase(leaf);
        return true;
    }

    bool removeDirectory(const ttstr &path) {
        FileInfo *parent = nullptr;
        ttstr leaf;
        FileInfo *entry = findMutableWithParent(path, parent, leaf);
        if(!entry || !entry->isDirectory() || !entry->_children.empty() ||
           !parent)
            return false;
        parent->_children.erase(leaf);
        return true;
    }

    tTJSBinaryStream *open(const ttstr &path, tjs_uint32 flags) {
        if(!_directory || path.IsEmpty() || path.GetLastChar() == TJS_W('/'))
            return nullptr;

        const tjs_uint32 access = flags & TJS_BS_ACCESS_MASK;
        ttstr head;
        ttstr tail;
        splitPath(path, head, tail);
        if(!tail.IsEmpty()) {
            FileInfo *dir =
                access == TJS_BS_READ ? findDirectoryMutable(head)
                                      : ensureChildDirectory(head);
            return dir ? dir->open(tail, flags) : nullptr;
        }

        auto it = _children.find(head);
        if(it == _children.end()) {
            if(access == TJS_BS_READ)
                return nullptr;
            auto inserted = _children.emplace(
                head, std::unique_ptr<FileInfo>(new FileInfo(this, head)));
            it = inserted.first;
        }

        FileInfo *file = it->second.get();
        if(file->isDirectory())
            return nullptr;
        return new MemFileStream(file->_data, flags);
    }

private:
    static void splitPath(const ttstr &path, ttstr &head, ttstr &tail) {
        const tjs_char *p = path.c_str();
        const tjs_char *q = TJS_strchr(p, TJS_W('/'));
        if(q) {
            head = ttstr(p, q - p);
            tail = ttstr(q + 1);
        } else {
            head = path;
            tail.Clear();
        }
    }

    FileInfo *findDirectoryMutable(const ttstr &name) {
        auto it = _children.find(name);
        if(it == _children.end() || !it->second->isDirectory())
            return nullptr;
        return it->second.get();
    }

    FileInfo *ensureChildDirectory(const ttstr &name) {
        auto it = _children.find(name);
        if(it != _children.end())
            return it->second->isDirectory() ? it->second.get() : nullptr;
        auto inserted = _children.emplace(
            name, std::unique_ptr<FileInfo>(new FileInfo(this, name, true)));
        return inserted.first->second.get();
    }

    FileInfo *ensureDirectory(const ttstr &path) {
        if(!_directory || path.IsEmpty())
            return nullptr;
        ttstr head;
        ttstr tail;
        splitPath(path, head, tail);
        FileInfo *dir = ensureChildDirectory(head);
        if(!dir)
            return nullptr;
        return tail.IsEmpty() ? dir : dir->ensureDirectory(tail);
    }

    FileInfo *findMutable(const ttstr &path) {
        if(!_directory || path.IsEmpty())
            return nullptr;
        ttstr head;
        ttstr tail;
        splitPath(path, head, tail);
        auto it = _children.find(head);
        if(it == _children.end())
            return nullptr;
        if(tail.IsEmpty())
            return it->second.get();
        return it->second->findMutable(tail);
    }

    FileInfo *findMutableWithParent(const ttstr &path, FileInfo *&parent,
                                    ttstr &leaf) {
        parent = nullptr;
        leaf.Clear();
        if(!_directory || path.IsEmpty())
            return nullptr;
        ttstr head;
        ttstr tail;
        splitPath(path, head, tail);
        if(tail.IsEmpty()) {
            auto it = _children.find(head);
            if(it == _children.end())
                return nullptr;
            parent = this;
            leaf = head;
            return it->second.get();
        }
        auto it = _children.find(head);
        if(it == _children.end() || !it->second->isDirectory())
            return nullptr;
        return it->second->findMutableWithParent(tail, parent, leaf);
    }

    FileInfo *_parent;
    ttstr _name;
    bool _directory;
    std::vector<tjs_uint8> _data;
    Directory _children;
};

class MemStorage : public iTVPStorageMedia {
public:
    MemStorage() : _refCount(1), _root(nullptr, TJS_W("root"), true) {}

    void TJS_INTF_METHOD AddRef() override { ++_refCount; }

    void TJS_INTF_METHOD Release() override {
        if(_refCount == 1)
            delete this;
        else
            --_refCount;
    }

    void TJS_INTF_METHOD GetName(ttstr &name) override { name = BASENAME; }

    void TJS_INTF_METHOD NormalizeDomainName(ttstr &name) override {}

    void TJS_INTF_METHOD NormalizePathName(ttstr &name) override {}

    bool TJS_INTF_METHOD CheckExistentStorage(const ttstr &name) override {
        ttstr filename;
        return getFilename(name, filename) && _root.findFile(filename);
    }

    tTJSBinaryStream *TJS_INTF_METHOD Open(const ttstr &name,
                                           tjs_uint32 flags) override {
        ttstr filename;
        if(!getFilename(name, filename))
            TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);

        tTJSBinaryStream *stream = _root.open(filename, flags);
        if(!stream)
            TVPThrowExceptionMessage(TJS_W("cannot open memfile:%1"), name);
        return stream;
    }

    void TJS_INTF_METHOD GetListAt(const ttstr &name,
                                   iTVPStorageLister *lister) override {
        ttstr filename;
        if(!getFilename(name, filename))
            return;
        const FileInfo *dir = _root.findDirectory(filename);
        if(dir)
            dir->getDirectory(lister);
    }

    void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr &name) override {
        name.Clear();
    }

    bool mkdir(const ttstr &name) { return _root.mkdir(name); }

    bool isExistFile(const ttstr &name) {
        return _root.findFile(name) != nullptr;
    }

    bool isExistDirectory(const ttstr &name) {
        return _root.findDirectory(name) != nullptr;
    }

    bool remove(const ttstr &name) { return _root.removeFile(name); }

    bool rmdir(const ttstr &name) { return _root.removeDirectory(name); }

    tTJSVariant getInfo(const ttstr &name) {
        const FileInfo *file = _root.findFile(name);
        return file ? file->getInfo() : tTJSVariant();
    }

    tTJSVariant getData(const ttstr &name) {
        const FileInfo *file = _root.findFile(name);
        return file ? file->getData() : tTJSVariant();
    }

    tTJSVariant getDirectory(const ttstr &name) {
        const FileInfo *dir = _root.findDirectory(name);
        if(!dir)
            return tTJSVariant();

        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();
        dir->getDirectory(array);
        tTJSVariant ret(array, array);
        array->Release();
        return ret;
    }

private:
    bool getFilename(const ttstr &name, ttstr &filename) {
        const tjs_char *p = name.c_str();
        const tjs_char *q = TJS_strchr(p, TJS_W('/'));
        if(!q || q == p)
            return false;
        ttstr domain(p, q - p);
        if(domain != TJS_W("."))
            TVPThrowExceptionMessage(TJS_W("no such domain:%1"), domain);
        filename = ttstr(q + 1);
        return !filename.IsEmpty();
    }

    tjs_uint _refCount;
    FileInfo _root;
};

MemStorage *gMemStorage = nullptr;

class StoragesMemFile {
public:
    static void initMemoryFile() {
        if(!gMemStorage) {
            gMemStorage = new MemStorage();
            TVPRegisterStorageMedia(gMemStorage);
        }
    }

    static void doneMemoryFile() {
        if(gMemStorage) {
            TVPUnregisterStorageMedia(gMemStorage);
            gMemStorage->Release();
            gMemStorage = nullptr;
        }
    }

    static bool isExistMemoryFile(ttstr filename) {
        return gMemStorage && gMemStorage->isExistFile(filename);
    }

    static bool isExistMemoryDirectory(ttstr dirname) {
        return gMemStorage && gMemStorage->isExistDirectory(dirname);
    }

    static bool deleteMemoryFile(ttstr filename) {
        return gMemStorage && gMemStorage->remove(filename);
    }

    static bool deleteMemoryDirectory(ttstr dirname) {
        return gMemStorage && gMemStorage->rmdir(dirname);
    }

    static tTJSVariant getMemoryFileInfo(ttstr filename) {
        return gMemStorage ? gMemStorage->getInfo(filename) : tTJSVariant();
    }

    static tTJSVariant getMemoryFileData(ttstr filename) {
        return gMemStorage ? gMemStorage->getData(filename) : tTJSVariant();
    }

    static tTJSVariant getMemoryDirectory(ttstr dirname) {
        return gMemStorage ? gMemStorage->getDirectory(dirname) : tTJSVariant();
    }
};

} // namespace

NCB_ATTACH_CLASS(StoragesMemFile, Storages) {
    NCB_METHOD(isExistMemoryFile);
    NCB_METHOD(isExistMemoryDirectory);
    NCB_METHOD(deleteMemoryFile);
    NCB_METHOD(deleteMemoryDirectory);
    NCB_METHOD(getMemoryFileInfo);
    NCB_METHOD(getMemoryFileData);
    NCB_METHOD(getMemoryDirectory);
}

static void PreRegistCallback() { StoragesMemFile::initMemoryFile(); }

static void PostUnregistCallback() { StoragesMemFile::doneMemoryFile(); }

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
