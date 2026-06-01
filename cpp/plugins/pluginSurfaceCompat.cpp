#include "ncbind.hpp"
#include "ClipboardIntf.h"
#include "CharacterSet.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "md5.h"

#include <minizip/zip.h>
#include <sqlite3.h>

#if __has_include(<curl/curl.h>)
#include <curl/curl.h>
#define KRKR2_PLUGIN_HAS_CURL 1
#else
#define KRKR2_PLUGIN_HAS_CURL 0
#endif

#if __has_include("tinyxml2/tinyxml2.h")
#include "tinyxml2/tinyxml2.h"
#define KRKR2_PLUGIN_HAS_TINYXML2 1
#else
#define KRKR2_PLUGIN_HAS_TINYXML2 0
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

tTJSVariant EmptyArrayCompat() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return tTJSVariant();
    tTJSVariant ret(array, array);
    array->Release();
    return ret;
}

tTJSVariant EmptyDictionaryCompat() {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    tTJSVariant ret(dict, dict);
    dict->Release();
    return ret;
}

tTJSVariant EmptyOctetCompat() {
    static const tjs_uint8 empty = 0;
    return tTJSVariant(&empty, 0);
}

tTJSVariant OctetCompat(const std::vector<tjs_uint8> &data) {
    if(data.empty())
        return EmptyOctetCompat();
    return tTJSVariant(data.data(), static_cast<tjs_uint>(data.size()));
}

tjs_error ReturnVoidCompat(tTJSVariant *result) {
    if(result)
        result->Clear();
    return TJS_S_OK;
}

tjs_error ReturnBoolCompat(tTJSVariant *result, bool value) {
    if(result)
        *result = value ? 1 : 0;
    return TJS_S_OK;
}

tjs_error ReturnIntCompat(tTJSVariant *result, tjs_int64 value) {
    if(result)
        *result = static_cast<tTVInteger>(value);
    return TJS_S_OK;
}

std::string VariantToBytes(tTJSVariant *value) {
    if(!value || value->Type() == tvtVoid)
        return {};
    if(value->Type() == tvtOctet) {
        tTJSVariantOctet *octet = value->AsOctetNoAddRef();
        if(!octet)
            return {};
        const char *ptr = reinterpret_cast<const char *>(octet->GetData());
        return std::string(ptr, ptr + octet->GetLength());
    }
    return ttstr(*value).AsNarrowStdString();
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return value;
}

std::vector<tjs_uint8> ReadStorageBytes(const ttstr &storage) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(storage, TJS_BS_READ));
    if(!stream)
        TVPThrowExceptionMessage(
            (storage + TJS_W(" can't open.")).c_str());

    std::vector<tjs_uint8> data;
    tjs_uint64 size = stream->GetSize();
    if(size > 0 && size < 0x7fffffffULL)
        data.reserve(static_cast<size_t>(size));

    std::vector<tjs_uint8> buffer(64 * 1024);
    for(;;) {
        tjs_uint read = stream->Read(buffer.data(), buffer.size());
        if(read == 0)
            break;
        data.insert(data.end(), buffer.begin(), buffer.begin() + read);
    }
    return data;
}

void WriteStorageBytes(const ttstr &storage, const std::vector<tjs_uint8> &data) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(storage, TJS_BS_WRITE));
    if(!stream)
        TVPThrowExceptionMessage(
            (storage + TJS_W(" can't open.")).c_str());
    if(!data.empty())
        stream->WriteBuffer(data.data(), static_cast<tjs_uint>(data.size()));
}

ttstr HexMD5(const std::vector<tjs_uint8> &data) {
    md5_state_t state;
    md5_byte_t digest[16];
    md5_init(&state);
    size_t offset = 0;
    while(offset < data.size()) {
        size_t chunk = std::min<size_t>(data.size() - offset, 0x7fffffff);
        md5_append(&state, data.data() + offset, static_cast<int>(chunk));
        offset += chunk;
    }
    md5_finish(&state, digest);

    static const char hex[] = "0123456789abcdef";
    char text[33];
    for(int i = 0; i < 16; ++i) {
        text[i * 2] = hex[digest[i] >> 4];
        text[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    text[32] = 0;
    return ttstr(text);
}

std::string Base64Encode(const std::string &input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for(size_t i = 0; i < input.size(); i += 3) {
        uint32_t chunk = static_cast<unsigned char>(input[i]) << 16;
        if(i + 1 < input.size())
            chunk |= static_cast<unsigned char>(input[i + 1]) << 8;
        if(i + 2 < input.size())
            chunk |= static_cast<unsigned char>(input[i + 2]);

        output.push_back(table[(chunk >> 18) & 0x3f]);
        output.push_back(table[(chunk >> 12) & 0x3f]);
        output.push_back(i + 1 < input.size() ? table[(chunk >> 6) & 0x3f]
                                              : '=');
        output.push_back(i + 2 < input.size() ? table[chunk & 0x3f] : '=');
    }
    return output;
}

std::string Base64Encode(const std::vector<tjs_uint8> &input) {
    if(input.empty())
        return {};
    return Base64Encode(std::string(reinterpret_cast<const char *>(input.data()),
                                    input.size()));
}

int Base64Value(char c) {
    if(c >= 'A' && c <= 'Z')
        return c - 'A';
    if(c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if(c >= '0' && c <= '9')
        return c - '0' + 52;
    if(c == '+')
        return 62;
    if(c == '/')
        return 63;
    return -1;
}

std::vector<tjs_uint8> Base64Decode(const std::string &input) {
    std::vector<tjs_uint8> output;
    int bits = 0;
    int value = 0;
    for(char c : input) {
        if(c == '=')
            break;
        if(std::isspace(static_cast<unsigned char>(c)))
            continue;
        int decoded = Base64Value(c);
        if(decoded < 0)
            continue;
        value = (value << 6) | decoded;
        bits += 6;
        if(bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<tjs_uint8>((value >> bits) & 0xff));
        }
    }
    return output;
}

bool GetObjectValue(iTJSDispatch2 *object, const tjs_char *name,
                    tTJSVariant *value) {
    if(!object || !name || !value)
        return false;
    value->Clear();
    return TJS_SUCCEEDED(object->PropGet(0, name, nullptr, value, object));
}

tjs_int GetObjectInt(iTJSDispatch2 *object, const tjs_char *name,
                     tjs_int fallback = 0) {
    tTJSVariant value;
    if(GetObjectValue(object, name, &value) && value.Type() != tvtVoid)
        return static_cast<tjs_int>(value.AsInteger());
    return fallback;
}

bool HasObjectValue(iTJSDispatch2 *object, const tjs_char *name) {
    tTJSVariant value;
    return GetObjectValue(object, name, &value) && value.Type() != tvtVoid;
}

void SetObjectValue(iTJSDispatch2 *object, const tjs_char *name,
                    const tTJSVariant &value) {
    if(object)
        object->PropSet(TJS_MEMBERENSURE, name, nullptr,
                        const_cast<tTJSVariant *>(&value), object);
}

void SetObjectInt(iTJSDispatch2 *object, const tjs_char *name,
                  tjs_int value) {
    SetObjectValue(object, name, tTJSVariant(value));
}

void SetObjectInt64(iTJSDispatch2 *object, const tjs_char *name,
                    tjs_uint64 value) {
    SetObjectValue(object, name,
                   tTJSVariant(static_cast<tTVInteger>(value)));
}

void EmptyPluginCompat() {}

ttstr TtstrFromChars(const std::basic_string<tjs_char> &text) {
    std::vector<tjs_char> buffer(text.begin(), text.end());
    buffer.push_back(0);
    return ttstr(buffer.data());
}

tTJSVariant ClassObjectCompat(const tjs_char *name) {
    tTJSVariant result;
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(global) {
        global->PropGet(TJS_IGNOREPROP, name, nullptr, &result, global);
        global->Release();
    }
    return result;
}

bool CallTjsMethodCompat(iTJSDispatch2 *target, const tjs_char *name,
                         std::vector<tTJSVariant> &args) {
    if(!target || !name)
        return false;
    if(target->IsValid(TJS_IGNOREPROP, name, nullptr, target) != TJS_S_TRUE)
        return false;
    std::vector<tTJSVariant *> argv;
    argv.reserve(args.size());
    for(auto &arg : args)
        argv.push_back(&arg);
    return TJS_SUCCEEDED(target->FuncCall(0, name, nullptr, nullptr,
                                          static_cast<tjs_int>(argv.size()),
                                          argv.empty() ? nullptr : argv.data(),
                                          target));
}

ttstr Utf8BytesToTtstrCompat(const std::vector<tjs_uint8> &bytes) {
    if(bytes.empty())
        return ttstr();

    if(bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        std::basic_string<tjs_char> out;
        for(size_t i = 2; i + 1 < bytes.size(); i += 2) {
            tjs_uint16 ch = static_cast<tjs_uint16>(bytes[i]) |
                            (static_cast<tjs_uint16>(bytes[i + 1]) << 8);
            if(ch == 0)
                break;
            out.push_back(static_cast<tjs_char>(ch));
        }
        return TtstrFromChars(out);
    }

    if(bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) {
        std::basic_string<tjs_char> out;
        for(size_t i = 2; i + 1 < bytes.size(); i += 2) {
            tjs_uint16 ch = (static_cast<tjs_uint16>(bytes[i]) << 8) |
                            static_cast<tjs_uint16>(bytes[i + 1]);
            if(ch == 0)
                break;
            out.push_back(static_cast<tjs_char>(ch));
        }
        return TtstrFromChars(out);
    }

    size_t offset = bytes.size() >= 3 && bytes[0] == 0xef &&
                            bytes[1] == 0xbb && bytes[2] == 0xbf
                        ? 3
                        : 0;
    const char *data = reinterpret_cast<const char *>(bytes.data() + offset);
    tjs_uint length = static_cast<tjs_uint>(bytes.size() - offset);
    tjs_int outlen = TVPUtf8ToWideCharString(data, length, nullptr);
    if(outlen >= 0) {
        std::vector<tjs_char> out(static_cast<size_t>(outlen) + 1);
        TVPUtf8ToWideCharString(data, length, out.data());
        out[static_cast<size_t>(outlen)] = 0;
        return ttstr(out.data());
    }

    return ttstr(std::string(data, data + length).c_str());
}

std::string TtstrToUtf8Compat(const ttstr &value) {
    const tjs_char *src = value.c_str();
    tjs_int len = TVPWideCharToUtf8String(src, nullptr);
    if(len >= 0) {
        std::vector<char> out(static_cast<size_t>(len) + 1);
        TVPWideCharToUtf8String(src, out.data());
        out[static_cast<size_t>(len)] = 0;
        return std::string(out.data());
    }
    return value.AsNarrowStdString();
}

ttstr Utf8TextToTtstrCompat(const char *data, size_t length) {
    if(!data || length == 0)
        return ttstr();
    tjs_int outlen = TVPUtf8ToWideCharString(
        data, static_cast<tjs_uint>(length), nullptr);
    if(outlen >= 0) {
        std::vector<tjs_char> out(static_cast<size_t>(outlen) + 1);
        TVPUtf8ToWideCharString(data, static_cast<tjs_uint>(length),
                                out.data());
        out[static_cast<size_t>(outlen)] = 0;
        return ttstr(out.data());
    }
    return ttstr(std::string(data, data + length).c_str());
}

ttstr Utf8TextToTtstrCompat(const char *text) {
    return text ? Utf8TextToTtstrCompat(text, std::strlen(text)) : ttstr();
}

} // namespace

// ---------------------------------------------------------------------------
// minizip.dll
// ---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("minizip.dll")

class ZipCompat {
public:
    ~ZipCompat() { close(); }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          ZipCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        self->close();

        ttstr filename = *param[0];
        tjs_int overwrite =
            numparams > 1 && param[1] ? (tjs_int)param[1]->AsInteger() : 0;
        bool exists = !TVPGetPlacedPath(filename).IsEmpty();
        if(overwrite == 0 && exists) {
            TVPThrowExceptionMessage((filename + TJS_W(" exists.")).c_str());
        }

        ttstr local = TVPNormalizeStorageName(filename);
        TVPGetLocalName(local);
        int append = (overwrite == 2 && exists) ? APPEND_STATUS_ADDINZIP
                                                : APPEND_STATUS_CREATE;
        self->zip_ = zipOpen64(local.AsNarrowStdString().c_str(), append);
        if(!self->zip_) {
            TVPThrowExceptionMessage(
                (filename + TJS_W(" can't open.")).c_str());
        }
        return ReturnBoolCompat(result, true);
    }

    void close() {
        if(zip_) {
            zipClose(zip_, nullptr);
            zip_ = nullptr;
        }
    }

    static tjs_error TJS_INTF_METHOD add(tTJSVariant *result,
                                         tjs_int numparams, tTJSVariant **param,
                                         ZipCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        if(!self->zip_)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));

        ttstr srcname = *param[0];
        ttstr destname = *param[1];
        int level = Z_DEFAULT_COMPRESSION;
        if(numparams > 2 && param[2] && param[2]->Type() == tvtInteger)
            level = static_cast<int>(param[2]->AsInteger());
        if(level < 0 || level > 9)
            level = Z_DEFAULT_COMPRESSION;

        int compressionMethod = 1;
        if(numparams > 4 && param[4] && param[4]->Type() == tvtInteger)
            compressionMethod = static_cast<int>(param[4]->AsInteger());
        int method = (level == 0 || compressionMethod == 0) ? 0 : Z_DEFLATED;

        std::unique_ptr<tTJSBinaryStream> input(
            TVPCreateStream(srcname, TJS_BS_READ));
        if(!input) {
            TVPThrowExceptionMessage(
                (srcname + TJS_W(" can't open.")).c_str());
        }

        zip_fileinfo info = {};
        const bool zip64 = input->GetSize() >= 0xffffffffULL;
        std::string dest = destname.AsNarrowStdString();
        int zerr = zipOpenNewFileInZip64(self->zip_, dest.c_str(), &info,
                                         nullptr, 0, nullptr, 0, nullptr,
                                         method, level, zip64 ? 1 : 0);
        if(zerr != ZIP_OK)
            return ReturnBoolCompat(result, false);

        std::vector<tjs_uint8> buffer(64 * 1024);
        bool ok = true;
        for(;;) {
            tjs_uint read = input->Read(buffer.data(), buffer.size());
            if(read == 0)
                break;
            if(zipWriteInFileInZip(self->zip_, buffer.data(), read) !=
               ZIP_OK) {
                ok = false;
                break;
            }
        }
        if(zipCloseFileInZip(self->zip_) != ZIP_OK)
            ok = false;
        return ReturnBoolCompat(result, ok);
    }

private:
    zipFile zip_ = nullptr;
};

class UnzipCompat {
public:
    ~UnzipCompat() { close(); }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          UnzipCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        self->close();
        self->filename_ = *param[0];
        self->archive_ = TVPOpenArchive(self->filename_, false);
        if(!self->archive_) {
            TVPThrowExceptionMessage(
                (self->filename_ + TJS_W(" can't open.")).c_str());
        }
        return ReturnBoolCompat(result, true);
    }

    void close() {
        if(archive_) {
            archive_->Release();
            archive_ = nullptr;
        }
        filename_.Clear();
    }

    tTJSVariant list() {
        if(!archive_)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));

        iTJSDispatch2 *array = TJSCreateArrayObject();
        if(!array)
            return tTJSVariant();

        const tjs_uint count = archive_->GetCount();
        for(tjs_uint i = 0; i < count; ++i) {
            iTJSDispatch2 *obj = TJSCreateDictionaryObject();
            if(!obj)
                continue;

            ttstr filename = archive_->GetName(i);
            SetObjectValue(obj, TJS_W("filename"), tTJSVariant(filename));

            tjs_uint64 size = 0;
            std::unique_ptr<tTJSBinaryStream> stream(
                archive_->CreateStreamByIndex(i));
            if(stream)
                size = stream->GetSize();

            SetObjectInt64(obj, TJS_W("uncompressed_size"), size);
            SetObjectInt64(obj, TJS_W("compressed_size"), size);
            SetObjectInt(obj, TJS_W("crypted"), 0);
            SetObjectInt(obj, TJS_W("deflated"), 0);
            SetObjectInt(obj, TJS_W("deflateLevel"), 0);
            SetObjectInt(obj, TJS_W("crc"), 0);

            tTJSVariant item(obj, obj);
            obj->Release();
            tTJSVariant *args[] = { &item };
            array->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args,
                            array);
        }

        tTJSVariant ret(array, array);
        array->Release();
        return ret;
    }

    static tjs_error TJS_INTF_METHOD extract(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             UnzipCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        if(!self->archive_)
            TVPThrowExceptionMessage(TJS_W("don't open zipfile"));

        ttstr srcname = *param[0];
        ttstr destname = *param[1];
        tTVPArchive::NormalizeInArchiveStorageName(srcname);
        if(!self->archive_->IsExistent(srcname))
            return ReturnBoolCompat(result, false);

        std::unique_ptr<tTJSBinaryStream> input(
            self->archive_->CreateStream(srcname));
        if(!input)
            return ReturnBoolCompat(result, false);

        std::unique_ptr<tTJSBinaryStream> output(
            TVPCreateStream(destname, TJS_BS_WRITE));
        if(!output) {
            TVPThrowExceptionMessage(
                (destname + TJS_W(" can't open.")).c_str());
        }

        std::vector<tjs_uint8> buffer(64 * 1024);
        for(;;) {
            tjs_uint read = input->Read(buffer.data(), buffer.size());
            if(read == 0)
                break;
            output->WriteBuffer(buffer.data(), read);
        }
        return ReturnBoolCompat(result, true);
    }

private:
    ttstr filename_;
    tTVPArchive *archive_ = nullptr;
};

class ZipStorageMediaCompat : public iTVPStorageMedia {
public:
    ZipStorageMediaCompat() = default;

    void AddRef() override { ++refCount_; }

    void Release() override {
        if(refCount_ == 1)
            delete this;
        else
            --refCount_;
    }

    void GetName(ttstr &name) override { name = TJS_W("zip"); }

    void NormalizeDomainName(ttstr &name) override {
        tjs_char *p = name.Independ();
        while(*p) {
            if(*p >= TJS_W('A') && *p <= TJS_W('Z'))
                *p += TJS_W('a') - TJS_W('A');
            ++p;
        }
    }

    void NormalizePathName(ttstr &name) override {
        bool leadingSlash =
            !name.IsEmpty() && name.c_str()[0] == TJS_W('/');
        ttstr path = leadingSlash ? ttstr(name.c_str() + 1) : name;
        tTVPArchive::NormalizeInArchiveStorageName(path);
        name = TJS_W("/");
        name += path;
    }

    bool CheckExistentStorage(const ttstr &name) override {
        ttstr storage = makeArchiveStorageName(name);
        return !storage.IsEmpty() && TVPIsExistentStorageNoSearch(storage);
    }

    tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override {
        if((flags & TJS_BS_ACCESS_MASK) != TJS_BS_READ)
            TVPThrowExceptionMessage(TJS_W("Cannot write to mounted zip"));
        ttstr storage = makeArchiveStorageName(name);
        if(storage.IsEmpty())
            TVPThrowExceptionMessage(TVPCannotOpenStorage, name);
        return TVPCreateStream(storage, flags);
    }

    void GetListAt(const ttstr &name, iTVPStorageLister *lister) override {
        if(!lister)
            return;
        ttstr domain;
        ttstr path;
        if(!splitName(name, &domain, &path))
            return;
        auto it = mounts_.find(domain);
        if(it == mounts_.end())
            return;

        ttstr prefix = path;
        tTVPArchive::NormalizeInArchiveStorageName(prefix);
        if(!prefix.IsEmpty() && prefix.GetLastChar() != TJS_W('/'))
            prefix += TJS_W("/");

        tTVPArchive *archive = TVPOpenArchive(it->second, false);
        if(!archive)
            return;

        std::set<ttstr> emitted;
        try {
            const tjs_uint count = archive->GetCount();
            for(tjs_uint i = 0; i < count; ++i) {
                ttstr item = archive->GetName(i);
                tTVPArchive::NormalizeInArchiveStorageName(item);
                if(!prefix.IsEmpty() && !item.StartsWith(prefix))
                    continue;
                ttstr rest = prefix.IsEmpty()
                                 ? item
                                 : ttstr(item.c_str() + prefix.GetLen());
                if(rest.IsEmpty() || TJS_strchr(rest.c_str(), TJS_W('/')))
                    continue;
                if(emitted.insert(rest).second)
                    lister->Add(rest);
            }
        } catch(...) {
            archive->Release();
            throw;
        }
        archive->Release();
    }

    void GetLocallyAccessibleName(ttstr &name) override { name.Clear(); }

    bool mount(const ttstr &domainName, const ttstr &zipfile) {
        ttstr domain = domainName;
        NormalizeDomainName(domain);
        if(domain.IsEmpty())
            return false;
        ttstr placed = TVPGetPlacedPath(zipfile);
        if(placed.IsEmpty())
            return false;
        mounts_[domain] = placed;
        return true;
    }

    bool unmount(const ttstr &domainName) {
        ttstr domain = domainName;
        NormalizeDomainName(domain);
        return mounts_.erase(domain) != 0;
    }

private:
    static bool splitName(const ttstr &name, ttstr *domain, ttstr *path) {
        const tjs_char *raw = name.c_str();
        const tjs_char *slash = TJS_strchr(raw, TJS_W('/'));
        if(slash) {
            if(domain)
                *domain = ttstr(raw, static_cast<int>(slash - raw));
            if(path)
                *path = ttstr(slash + 1);
        } else {
            if(domain)
                *domain = name;
            if(path)
                path->Clear();
        }
        return domain && !domain->IsEmpty();
    }

    ttstr makeArchiveStorageName(const ttstr &name) {
        ttstr domain;
        ttstr path;
        if(!splitName(name, &domain, &path))
            return {};
        auto it = mounts_.find(domain);
        if(it == mounts_.end())
            return {};
        tTVPArchive::NormalizeInArchiveStorageName(path);
        if(path.IsEmpty())
            return {};
        ttstr storage = it->second;
        storage += TVPArchiveDelimiter;
        storage += path;
        return storage;
    }

    tjs_uint refCount_ = 1;
    std::map<ttstr, ttstr> mounts_;
};

static ZipStorageMediaCompat *gZipStorageMediaCompat = nullptr;

static void EnsureZipStorageMediaCompat() {
    if(gZipStorageMediaCompat)
        return;
    auto *media = new ZipStorageMediaCompat();
    TVPRegisterStorageMedia(media);
    gZipStorageMediaCompat = media;
    media->Release();
}

static void ReleaseZipStorageMediaCompat() {
    if(!gZipStorageMediaCompat)
        return;
    ZipStorageMediaCompat *media = gZipStorageMediaCompat;
    gZipStorageMediaCompat = nullptr;
    TVPUnregisterStorageMedia(media);
}

class StoragesZipCompat {
public:
    static bool mountZip(ttstr name, ttstr zipfile) {
        EnsureZipStorageMediaCompat();
        return gZipStorageMediaCompat &&
            gZipStorageMediaCompat->mount(name, zipfile);
    }

    static bool unmountZip(ttstr name) {
        return gZipStorageMediaCompat &&
            gZipStorageMediaCompat->unmount(name);
    }
};

static void MinizipPreRegistCallback() { EnsureZipStorageMediaCompat(); }
static void MinizipPostUnregistCallback() { ReleaseZipStorageMediaCompat(); }

NCB_PRE_REGIST_CALLBACK(MinizipPreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(MinizipPostUnregistCallback);

NCB_REGISTER_CLASS_DIFFER(Zip, ZipCompat) {
    NCB_CONSTRUCTOR(());
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    RawCallback(TJS_W("add"), &Class::add, 0);
    Variant(TJS_W("CompressionMethodStore"), (tjs_int)0);
    Variant(TJS_W("CompressionMethodDeflate"), (tjs_int)1);
    Variant(TJS_W("CompressionMethodBzip2"), (tjs_int)2);
    Variant(TJS_W("CompressionMethodLzma"), (tjs_int)3);
}

NCB_REGISTER_CLASS_DIFFER(Unzip, UnzipCompat) {
    NCB_CONSTRUCTOR(());
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    NCB_METHOD(list);
    RawCallback(TJS_W("extract"), &Class::extract, 0);
}

NCB_ATTACH_CLASS(StoragesZipCompat, Storages) {
    NCB_METHOD(mountZip);
    NCB_METHOD(unmountZip);
}

// ---------------------------------------------------------------------------
// httprequest.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httprequest.dll")

namespace {

struct HttpCompatResponse {
    tjs_int status = 0;
    ttstr statusText;
    std::map<ttstr, ttstr> headers;
    std::vector<tjs_uint8> body;
    ttstr contentType;
    ttstr contentEncoding;
};

std::string TrimHttpAscii(std::string value) {
    while(!value.empty() &&
          std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while(!value.empty() &&
          std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

ttstr HttpHeaderValueCompat(const std::map<ttstr, ttstr> &headers,
                            const ttstr &name) {
    for(const auto &entry : headers) {
        if(TJS_stricmp(entry.first.c_str(), name.c_str()) == 0)
            return entry.second;
    }
    return ttstr();
}

void ParseHttpContentType(HttpCompatResponse *response) {
    if(!response)
        return;
    ttstr headerType = HttpHeaderValueCompat(response->headers,
                                             TJS_W("Content-Type"));
    if(!headerType.IsEmpty())
        response->contentType = headerType;
    response->contentEncoding.Clear();
    std::string type = TtstrToUtf8Compat(response->contentType);
    std::string lower = LowerAscii(type);
    size_t charset = lower.find("charset=");
    if(charset != std::string::npos) {
        std::string enc = type.substr(charset + 8);
        size_t semi = enc.find(';');
        if(semi != std::string::npos)
            enc.resize(semi);
        enc = TrimHttpAscii(enc);
        if(!enc.empty() &&
           ((enc.front() == '"' && enc.back() == '"') ||
            (enc.front() == '\'' && enc.back() == '\'')))
            enc = enc.substr(1, enc.size() - 2);
        response->contentEncoding = Utf8TextToTtstrCompat(enc.c_str(),
                                                          enc.size());
    }
}

#if KRKR2_PLUGIN_HAS_CURL
size_t HttpCurlWriteBody(char *ptr, size_t size, size_t nmemb,
                         void *userdata) {
    auto *response = static_cast<HttpCompatResponse *>(userdata);
    size_t bytes = size * nmemb;
    if(response && ptr && bytes)
        response->body.insert(response->body.end(),
                              reinterpret_cast<tjs_uint8 *>(ptr),
                              reinterpret_cast<tjs_uint8 *>(ptr) + bytes);
    return bytes;
}

size_t HttpCurlWriteHeader(char *ptr, size_t size, size_t nmemb,
                           void *userdata) {
    auto *response = static_cast<HttpCompatResponse *>(userdata);
    size_t bytes = size * nmemb;
    if(!response || !ptr || !bytes)
        return bytes;

    std::string line(ptr, ptr + bytes);
    while(!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    if(line.empty())
        return bytes;

    std::string lower = LowerAscii(line);
    if(lower.rfind("http/", 0) == 0) {
        size_t first = line.find(' ');
        if(first != std::string::npos) {
            size_t second = line.find(' ', first + 1);
            if(second != std::string::npos)
                response->statusText = Utf8TextToTtstrCompat(
                    line.c_str() + second + 1, line.size() - second - 1);
        }
        return bytes;
    }

    size_t colon = line.find(':');
    if(colon != std::string::npos) {
        std::string name = TrimHttpAscii(line.substr(0, colon));
        std::string value = TrimHttpAscii(line.substr(colon + 1));
        response->headers[Utf8TextToTtstrCompat(name.c_str(), name.size())] =
            Utf8TextToTtstrCompat(value.c_str(), value.size());
    }
    return bytes;
}
#endif

bool HttpPerformCompat(const ttstr &method, const ttstr &url,
                       const std::map<ttstr, ttstr> &requestHeaders,
                       const std::vector<tjs_uint8> &requestBody,
                       HttpCompatResponse *response, ttstr *error) {
    if(response)
        *response = HttpCompatResponse();
#if KRKR2_PLUGIN_HAS_CURL
    static bool curlInitialized = []() {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if(!curlInitialized) {
        if(error)
            *error = TJS_W("curl initialization failed");
        return false;
    }

    CURL *curl = curl_easy_init();
    if(!curl) {
        if(error)
            *error = TJS_W("curl_easy_init failed");
        return false;
    }

    std::string urlUtf8 = TtstrToUtf8Compat(url);
    std::string methodUtf8 = TtstrToUtf8Compat(method);
    std::string methodLower = LowerAscii(methodUtf8);
    std::string body;
    if(!requestBody.empty())
        body.assign(reinterpret_cast<const char *>(requestBody.data()),
                    requestBody.size());
    curl_slist *headerList = nullptr;
    for(const auto &entry : requestHeaders) {
        std::string header = TtstrToUtf8Compat(entry.first);
        header += ": ";
        header += TtstrToUtf8Compat(entry.second);
        headerList = curl_slist_append(headerList, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, HttpCurlWriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HttpCurlWriteHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    if(headerList)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

    if(methodLower == "post") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
    } else if(methodLower == "head") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if(methodLower != "get" && !methodUtf8.empty()) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, methodUtf8.c_str());
        if(!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(body.size()));
        }
    }

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if(response) {
        response->status = static_cast<tjs_int>(status);
        char *contentType = nullptr;
        if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType) ==
               CURLE_OK &&
           contentType)
            response->contentType = Utf8TextToTtstrCompat(contentType);
        ParseHttpContentType(response);
    }

    if(headerList)
        curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    if(code != CURLE_OK) {
        if(response) {
            response->status = 0;
            response->statusText = Utf8TextToTtstrCompat(curl_easy_strerror(code));
        }
        if(error)
            *error = response ? response->statusText
                              : Utf8TextToTtstrCompat(curl_easy_strerror(code));
        return false;
    }
    if(response && response->statusText.IsEmpty() && response->status > 0)
        response->statusText = TJS_W("OK");
    return true;
#else
    if(error)
        *error = TJS_W("HTTP request is unavailable on this platform");
    return false;
#endif
}

} // namespace

class HttpRequestCompat {
public:
    enum ReadyState {
        UNINITIALIZED = 0,
        OPEN = 1,
        SENT = 2,
        RECEIVING = 3,
        LOADED = 4
    };

    static tjs_error factory(HttpRequestCompat **result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = new HttpRequestCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        self->method_ = ttstr(*param[0]);
        self->url_ = ttstr(*param[1]);
        self->readyState_ = OPEN;
        self->status_ = 0;
        self->statusText_ = TJS_W("");
        return ReturnVoidCompat(result);
    }

    void setRequestHeader(ttstr name, ttstr value) { requestHeaders_[name] = value; }

    static tjs_error TJS_INTF_METHOD send(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        std::vector<tjs_uint8> body;
        if(numparams > 0 && param && param[0])
            body = VariantBytesVector(param[0]);
        self->perform(body);
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD sendSync(tTJSVariant *result, tjs_int n,
                                              tTJSVariant **p,
                                              HttpRequestCompat *self) {
        tjs_error hr = send(nullptr, n, p, self);
        if(TJS_FAILED(hr))
            return hr;
        return ReturnIntCompat(result, self ? self->status_ : 0);
    }

    static tjs_error TJS_INTF_METHOD sendStorage(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **param,
                                                 HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        self->perform(ReadStorageBytes(ttstr(*param[0])));
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD sendStorageSync(tTJSVariant *result,
                                                     tjs_int n,
                                                     tTJSVariant **p,
                                                     HttpRequestCompat *self) {
        tjs_error hr = sendStorage(nullptr, n, p, self);
        if(TJS_FAILED(hr))
            return hr;
        return ReturnIntCompat(result, self ? self->status_ : 0);
    }

    void abort() {
        readyState_ = LOADED;
        status_ = -1;
        statusText_ = TJS_W("cancelled");
    }

    tTJSVariant getAllResponseHeaders() {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        for(const auto &entry : responseHeaders_) {
            tTJSVariant value(entry.second);
            dict->PropSet(TJS_MEMBERENSURE, entry.first.c_str(), nullptr,
                          &value, dict);
        }
        tTJSVariant ret(dict, dict);
        dict->Release();
        return ret;
    }
    ttstr getResponseHeader(ttstr name) {
        return HttpHeaderValueCompat(responseHeaders_, name);
    }

    static tjs_error TJS_INTF_METHOD getResponseText(tTJSVariant *result,
                                                     tjs_int,
                                                     tTJSVariant **,
                                                     HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(result)
            *result = self->responseText_;
        return TJS_S_OK;
    }

    tjs_int getReadyState() const { return readyState_; }
    tTJSVariant getResponse() { return responseText_; }
    tTJSVariant getResponseData() { return OctetCompat(responseData_); }
    tjs_int getStatus() const { return status_; }
    ttstr getStatusText() const { return statusText_; }
    ttstr getContentType() const { return contentType_; }
    ttstr getContentTypeEncoding() const { return contentEncoding_; }
    tjs_int64 getContentLength() const {
        return static_cast<tjs_int64>(responseData_.size());
    }

    static tjs_error TJS_INTF_METHOD encodeBase64(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  HttpRequestCompat *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = ttstr(Base64Encode(VariantToBytes(param[0])).c_str());
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD decodeBase64(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  HttpRequestCompat *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> decoded = Base64Decode(VariantToBytes(param[0]));
        if(result) {
            if(decoded.empty())
                *result = EmptyOctetCompat();
            else
                *result = tTJSVariant(decoded.data(),
                                      static_cast<tjs_uint>(decoded.size()));
        }
        return TJS_S_OK;
    }

private:
    static std::vector<tjs_uint8> VariantBytesVector(tTJSVariant *value) {
        std::string bytes = VariantToBytes(value);
        return std::vector<tjs_uint8>(bytes.begin(), bytes.end());
    }

    void perform(const std::vector<tjs_uint8> &body) {
        readyState_ = SENT;
        HttpCompatResponse response;
        ttstr error;
        bool ok = HttpPerformCompat(method_, url_, requestHeaders_, body,
                                    &response, &error);
        readyState_ = LOADED;
        status_ = response.status;
        statusText_ = ok ? response.statusText : error;
        responseHeaders_ = response.headers;
        responseData_ = response.body;
        responseText_ = Utf8BytesToTtstrCompat(responseData_);
        contentType_ = response.contentType;
        contentEncoding_ = response.contentEncoding;
    }

    ttstr method_;
    ttstr url_;
    std::map<ttstr, ttstr> requestHeaders_;
    std::map<ttstr, ttstr> responseHeaders_;
    std::vector<tjs_uint8> responseData_;
    ttstr responseText_;
    ttstr contentType_;
    ttstr contentEncoding_;
    tjs_int readyState_ = UNINITIALIZED;
    tjs_int status_ = 0;
    ttstr statusText_;
};

NCB_REGISTER_CLASS_DIFFER(HttpRequest, HttpRequestCompat) {
    Factory(&HttpRequestCompat::factory);
    Variant(TJS_W("UNINITIALIZED"), (tjs_int)HttpRequestCompat::UNINITIALIZED);
    Variant(TJS_W("OPEN"), (tjs_int)HttpRequestCompat::OPEN);
    Variant(TJS_W("SENT"), (tjs_int)HttpRequestCompat::SENT);
    Variant(TJS_W("RECEIVING"), (tjs_int)HttpRequestCompat::RECEIVING);
    Variant(TJS_W("LOADED"), (tjs_int)HttpRequestCompat::LOADED);
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(setRequestHeader);
    RawCallback(TJS_W("send"), &Class::send, 0);
    RawCallback(TJS_W("sendSync"), &Class::sendSync, 0);
    RawCallback(TJS_W("sendStorage"), &Class::sendStorage, 0);
    RawCallback(TJS_W("sendStorageSync"), &Class::sendStorageSync, 0);
    NCB_METHOD(abort);
    NCB_METHOD(getAllResponseHeaders);
    NCB_METHOD(getResponseHeader);
    RawCallback(TJS_W("getResponseText"), &Class::getResponseText, 0);
    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(response, getResponse);
    NCB_PROPERTY_RO(responseData, getResponseData);
    NCB_PROPERTY_RO(status, getStatus);
    NCB_PROPERTY_RO(statusText, getStatusText);
    NCB_PROPERTY_RO(contentType, getContentType);
    NCB_PROPERTY_RO(contentTypeEncoding, getContentTypeEncoding);
    NCB_PROPERTY_RO(contentLength, getContentLength);
    RawCallback(TJS_W("encodeBase64"), &Class::encodeBase64, 0);
    RawCallback(TJS_W("decodeBase64"), &Class::decodeBase64, 0);
}

// ---------------------------------------------------------------------------
// httpserv.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("httpserv.dll")

class SimpleHTTPServerCompat {
public:
    static tjs_error factory(SimpleHTTPServerCompat **result,
                             tjs_int numparams, tTJSVariant **param,
                             iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        tjs_int port = numparams > 0 && param && param[0]
                           ? static_cast<tjs_int>(param[0]->AsInteger())
                           : 0;
        tjs_int timeout = numparams > 1 && param && param[1]
                              ? static_cast<tjs_int>(param[1]->AsInteger())
                              : 10;
        tjs_int codepage = numparams > 2 && param && param[2]
                               ? static_cast<tjs_int>(param[2]->AsInteger())
                               : 65001;
        *result = new SimpleHTTPServerCompat(port, timeout, codepage);
        return TJS_S_OK;
    }

    SimpleHTTPServerCompat(tjs_int port, tjs_int timeout, tjs_int codepage) :
        port_(port), timeout_(timeout), codepage_(codepage) {}

    tjs_int start() { return port_; }
    void stop() {}
    tjs_int getPort() const { return port_; }
    tjs_int getTimeout() const { return timeout_; }
    tjs_int getCodepage() const { return codepage_; }
    void setCodepage(tjs_int codepage) { codepage_ = codepage; }

private:
    tjs_int port_;
    tjs_int timeout_;
    tjs_int codepage_;
};

NCB_REGISTER_CLASS_DIFFER(SimpleHTTPServer, SimpleHTTPServerCompat) {
    Factory(&SimpleHTTPServerCompat::factory);
    NCB_METHOD(start);
    NCB_METHOD(stop);
    NCB_PROPERTY_RO(port, getPort);
    NCB_PROPERTY_RO(timeout, getTimeout);
    NCB_PROPERTY(codepage, getCodepage, setCodepage);
    Variant(TJS_W("cpACP"), (tjs_int)0);
    Variant(TJS_W("cpOEM"), (tjs_int)1);
    Variant(TJS_W("cpUTF8"), (tjs_int)65001);
    Variant(TJS_W("cpSJIS"), (tjs_int)932);
    Variant(TJS_W("cpEUC"), (tjs_int)20932);
    Variant(TJS_W("cpJIS"), (tjs_int)50220);
}

// ---------------------------------------------------------------------------
// tftSave.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tftSave.dll")

class TftSaveSystemCompat {
public:
    static tjs_error TJS_INTF_METHOD voidRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnVoidCompat(result);
    }
};

NCB_ATTACH_CLASS(TftSaveSystemCompat, System) {
    RawCallback(TJS_W("savePreRenderedFont"), &TftSaveSystemCompat::voidRaw,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("loadPreRenderedFont"), &TftSaveSystemCompat::voidRaw,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("modifyPreRenderedFont"), &TftSaveSystemCompat::voidRaw,
                TJS_STATICMEMBER);
}

class LayerGlyphCompat {
public:
    explicit LayerGlyphCompat(iTJSDispatch2 *) {}

    void setGlyphInfo(tjs_int) {}
    void drawGlyph(tjs_int) {}
    bool renderGlyph(tjs_int) { return false; }
    tjs_int getGlyphCharset() const { return glyphCharset_; }
    void setGlyphCharset(tjs_int value) { glyphCharset_ = value; }

private:
    tjs_int glyphCharset_ = 1;
};

NCB_GET_INSTANCE_HOOK(LayerGlyphCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerGlyphCompat, Layer) {
    NCB_METHOD(setGlyphInfo);
    NCB_METHOD(drawGlyph);
    NCB_METHOD(renderGlyph);
    NCB_PROPERTY(glyphCharset, getGlyphCharset, setGlyphCharset);
}

// ---------------------------------------------------------------------------
// Bundle/compatibility aliases from the source plugin directory.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerEx.dll")
static void layerExBundleCompat() {
    ncbAutoRegister::LoadModule(TJS_W("layerExDraw.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExImage.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExBTOA.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExRaster.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExAreaAverage.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExLongExposure.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExSave.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExAVI.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));
    ncbAutoRegister::LoadModule(TJS_W("perspective.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExAgg.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExCairo.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExGdiPlus.dll"));
}
NCB_PRE_REGIST_CALLBACK(layerExBundleCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("psdfile.dll")
static void psdfileAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("psd.dll"));
    ncbAutoRegister::LoadModule(TJS_W("psbfile.dll"));
}
NCB_PRE_REGIST_CALLBACK(psdfileAliasCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("steam.dll")
static void steamAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("krkrsteam.dll"));
    ncbAutoRegister::LoadModule(TJS_W("steam_api.dll"));
}
NCB_PRE_REGIST_CALLBACK(steamAliasCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3D.dll")

class DrawDeviceD3DCompat {
public:
    DrawDeviceD3DCompat(tjs_int width, tjs_int height) :
        width_(width), height_(height) {}

    tTJSVariant getDevice() const { return tTJSVariant(); }
    tjs_int getWidth() const { return width_; }
    void setWidth(tjs_int value) { width_ = value; }
    tjs_int getHeight() const { return height_; }
    void setHeight(tjs_int value) { height_ = value; }
    void setSize(tjs_int width, tjs_int height) {
        width_ = width;
        height_ = height;
    }
    tjs_int getDestWidth() const { return width_; }
    tjs_int getDestHeight() const { return height_; }
    bool getDefaultVisible() const { return defaultVisible_; }
    void setDefaultVisible(bool value) { defaultVisible_ = value; }
    bool getVisible(tjs_int id) const {
        auto it = visible_.find(id);
        return it == visible_.end() ? defaultVisible_ : it->second;
    }
    void setVisible(tjs_int id, bool visible) { visible_[id] = visible; }

private:
    tjs_int width_;
    tjs_int height_;
    bool defaultVisible_ = true;
    std::map<tjs_int, bool> visible_;
};

NCB_REGISTER_CLASS_DIFFER(DrawDeviceD3D, DrawDeviceD3DCompat) {
    NCB_CONSTRUCTOR((tjs_int,tjs_int));
    NCB_PROPERTY_RO(interface, getDevice);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
    NCB_METHOD(setSize);
    NCB_PROPERTY_RO(destWidth, getDestWidth);
    NCB_PROPERTY_RO(destHeight, getDestHeight);
    NCB_PROPERTY(defaultVisible, getDefaultVisible, setDefaultVisible);
    NCB_METHOD(getVisible);
    NCB_METHOD(setVisible);
}

static void drawDeviceD3DAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll"));
    ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
}
NCB_PRE_REGIST_CALLBACK(drawDeviceD3DAliasCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3DZ.dll")
static void drawDeviceD3DZAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("DrawDeviceD3D.dll"));
}
NCB_PRE_REGIST_CALLBACK(drawDeviceD3DZAliasCompat);

// ---------------------------------------------------------------------------
// base64.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("base64.dll")

class Base64Compat {
public:
    static tjs_error TJS_INTF_METHOD encode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> bytes = ReadStorageBytes(ttstr(*param[0]));
        if(result)
            *result = ttstr(Base64Encode(bytes).c_str());
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD decode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> decoded = Base64Decode(VariantToBytes(param[0]));
        WriteStorageBytes(ttstr(*param[1]), decoded);
        if(result)
            *result = HexMD5(decoded);
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Base64, Base64Compat) {
    RawCallback(TJS_W("encode"), &Class::encode, 0);
    RawCallback(TJS_W("decode"), &Class::decode, 0);
}

// ---------------------------------------------------------------------------
// encode.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("encode.dll")

class EncodeCompat {
public:
    static tjs_error TJS_INTF_METHOD decode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        std::string encoding = LowerAscii(ttstr(*param[1]).AsNarrowStdString());
        std::string bytes = VariantToBytes(param[0]);
        if(result) {
            // The engine stores TJS strings as Unicode; UTF-8 and the current
            // narrow encoding both land here as UTF-8 on this port.
            if(encoding == "utf-8" || encoding == "utf8" ||
               encoding == "utf-8n" || encoding == "sjis" ||
               encoding == "shift_jis" || encoding == "euc-jp")
                *result = ttstr(bytes);
            else
                *result = ttstr(bytes);
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD encode(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        std::string bytes = ttstr(*param[0]).AsNarrowStdString();
        std::vector<tjs_uint8> out(bytes.begin(), bytes.end());
        if(result)
            *result = OctetCompat(out);
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(Encode, EncodeCompat) {
    RawCallback(TJS_W("decode"), &Class::decode, 0);
    RawCallback(TJS_W("encode"), &Class::encode, 0);
}

// ---------------------------------------------------------------------------
// sqlite3.dll / sqlite3_xp3_vfs.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sqlite3.dll")

namespace {

ttstr SqliteLocalDatabaseName(const ttstr &database) {
    if(database.IsEmpty())
        return database;
    const tjs_char *name = database.c_str();
    if(name[0] == TJS_W(':'))
        return database;

    ttstr normalized = TVPNormalizeStorageName(database);
    ttstr local = TVPGetLocallyAccessibleName(normalized);
    if(local.IsEmpty()) {
        local = normalized;
        TVPGetLocalName(local);
    }
    return local;
}

void SqliteSetError(sqlite3 *db, tjs_int *code, ttstr *message, int fallback) {
    if(code)
        *code = db ? sqlite3_errcode(db) : fallback;
    if(message) {
        if(db && sqlite3_errmsg16(db))
            *message = reinterpret_cast<const tjs_char *>(sqlite3_errmsg16(db));
        else
            *message = TJS_W("database is not open");
    }
}

int SqliteBindParam(sqlite3_stmt *stmt, const tTJSVariant &param, int pos) {
    switch(param.Type()) {
    case tvtInteger:
        return sqlite3_bind_int64(stmt, pos,
                                  static_cast<sqlite3_int64>(param.AsInteger()));
    case tvtReal:
        return sqlite3_bind_double(stmt, pos, param.AsReal());
    case tvtString: {
        tTJSVariantString *str = param.AsStringNoAddRef();
        const void *text = str ? static_cast<const void *>(*str) : nullptr;
        int bytes = str ? str->GetLength() * static_cast<int>(sizeof(tjs_char))
                        : 0;
        return sqlite3_bind_text16(stmt, pos, text, bytes, SQLITE_TRANSIENT);
    }
    case tvtOctet: {
        tTJSVariantOctet *octet = param.AsOctetNoAddRef();
        return sqlite3_bind_blob(stmt, pos,
                                 octet ? octet->GetData() : nullptr,
                                 octet ? static_cast<int>(octet->GetLength()) : 0,
                                 SQLITE_TRANSIENT);
    }
    default:
        return sqlite3_bind_null(stmt, pos);
    }
}

int SqliteBindPos(sqlite3_stmt *stmt, const tTJSVariant &name) {
    if(name.Type() == tvtInteger || name.Type() == tvtReal)
        return static_cast<int>(name.AsInteger()) + 1;
    if(name.Type() != tvtString)
        return 0;

    ttstr key(name);
    std::string utf8 = TtstrToUtf8Compat(key);
    return sqlite3_bind_parameter_index(stmt, utf8.c_str());
}

class SqliteBindCaller : public tTJSDispatch {
public:
    explicit SqliteBindCaller(sqlite3_stmt *stmt) : stmt_(stmt) {}

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char *,
                                       tjs_uint32 *, tTJSVariant *result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *) override {
        if(numparams > 2 && param && param[0] && param[1] && param[2]) {
            tTVInteger flags = param[1]->AsInteger();
            if(!(flags & TJS_HIDDENMEMBER)) {
                errorCode_ = SqliteBindParam(
                    stmt_, *param[2], SqliteBindPos(stmt_, *param[0]));
            }
        }
        if(result)
            *result = errorCode_ == SQLITE_OK;
        return TJS_S_OK;
    }

    int getErrorCode() const { return errorCode_; }

private:
    sqlite3_stmt *stmt_ = nullptr;
    int errorCode_ = SQLITE_OK;
};

int SqliteBindParams(sqlite3_stmt *stmt, const tTJSVariant *params) {
    if(!stmt || !params || params->Type() == tvtVoid)
        return SQLITE_OK;
    if(params->Type() != tvtObject)
        return SqliteBindParam(stmt, *params, 1);

    tTJSVariantClosure closure = params->AsObjectClosureNoAddRef();
    if(closure.IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr, TJS_W("Array"),
                            nullptr) == TJS_S_TRUE) {
        tTJSVariant countVar;
        closure.PropGet(0, TJS_W("count"), nullptr, &countVar, nullptr);
        int count = static_cast<int>(countVar.AsInteger());
        for(int i = 0; i < count; ++i) {
            tTJSVariant value;
            closure.PropGetByNum(0, i, &value, nullptr);
            int ret = SqliteBindParam(stmt, value, i + 1);
            if(ret != SQLITE_OK)
                return ret;
        }
        return SQLITE_OK;
    }

    SqliteBindCaller *caller = new SqliteBindCaller(stmt);
    tTJSVariantClosure enumClosure(caller);
    closure.EnumMembers(TJS_IGNOREPROP, &enumClosure, nullptr);
    int ret = caller->getErrorCode();
    caller->Release();
    return ret;
}

void SqliteColumnToVariant(sqlite3_stmt *stmt, int column,
                           tTJSVariant *result) {
    if(!result)
        return;
    switch(sqlite3_column_type(stmt, column)) {
    case SQLITE_INTEGER:
        *result = static_cast<tTVInteger>(sqlite3_column_int64(stmt, column));
        break;
    case SQLITE_FLOAT:
        *result = sqlite3_column_double(stmt, column);
        break;
    case SQLITE_TEXT:
        *result = reinterpret_cast<const tjs_char *>(
            sqlite3_column_text16(stmt, column));
        break;
    case SQLITE_BLOB:
        *result = tTJSVariant(
            reinterpret_cast<const tjs_uint8 *>(sqlite3_column_blob(stmt, column)),
            static_cast<tjs_uint>(sqlite3_column_bytes(stmt, column)));
        break;
    default:
        result->Clear();
        break;
    }
}

int SqliteColumnIndex(sqlite3_stmt *stmt, const tTJSVariant &column) {
    if(!stmt)
        return -1;
    if(column.Type() == tvtInteger || column.Type() == tvtReal)
        return static_cast<int>(column.AsInteger());
    if(column.Type() != tvtString)
        return -1;

    ttstr wanted(column);
    int count = sqlite3_column_count(stmt);
    for(int i = 0; i < count; ++i) {
        const auto *name = reinterpret_cast<const tjs_char *>(
            sqlite3_column_name16(stmt, i));
        if(name && TJS_stricmp(wanted.c_str(), name) == 0)
            return i;
    }
    return -1;
}

} // namespace

class SqliteCompat {
public:
    static tjs_error factory(SqliteCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        ttstr database;
        if(numparams > 0 && param && param[0])
            database = ttstr(*param[0]);
        bool readonly = numparams > 1 && param && param[1] &&
                        param[1]->AsInteger() != 0;
        *result = new SqliteCompat(database, readonly);
        return TJS_S_OK;
    }

    SqliteCompat(ttstr database, bool readonly) :
        database_(database), readonly_(readonly) {
        ttstr local = SqliteLocalDatabaseName(database_);
        int flags = readonly_ ? SQLITE_OPEN_READONLY
                              : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        std::string path = TtstrToUtf8Compat(local);
        errorCode_ = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
        if(errorCode_ != SQLITE_OK)
            SqliteSetError(db_, &errorCode_, &errorMessage_, errorCode_);
        else
            errorMessage_.Clear();
    }

    ~SqliteCompat() {
        if(db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    static tjs_error TJS_INTF_METHOD exec(tTJSVariant *result, tjs_int numparams,
                                          tTJSVariant **param,
                                          SqliteCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(!self->db_)
            return ReturnBoolCompat(result, false);

        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare16_v2(self->db_, param[0]->GetString(), -1,
                                       &stmt, nullptr);
        if(ret == SQLITE_OK && stmt) {
            ret = SqliteBindParams(stmt,
                                   numparams > 1 && param[1] ? param[1] : nullptr);
            if(ret == SQLITE_OK) {
                if(numparams > 2 && param[2] &&
                   param[2]->Type() == tvtObject) {
                    tTJSVariantClosure callback =
                        param[2]->AsObjectClosureNoAddRef();
                    int argc = sqlite3_column_count(stmt);
                    std::vector<tTJSVariant> args(
                        static_cast<size_t>(std::max(argc, 0)));
                    std::vector<tTJSVariant *> argv(args.size());
                    for(size_t i = 0; i < args.size(); ++i)
                        argv[i] = &args[i];
                    while((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
                        for(int i = 0; i < argc; ++i)
                            SqliteColumnToVariant(stmt, i, &args[i]);
                        callback.FuncCall(0, nullptr, nullptr, nullptr, argc,
                                          argv.empty() ? nullptr : argv.data(),
                                          nullptr);
                    }
                } else {
                    while((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
                    }
                }
            }
        }
        if(stmt)
            sqlite3_finalize(stmt);
        if(ret == SQLITE_DONE || ret == SQLITE_ROW)
            ret = SQLITE_OK;
        self->errorCode_ = ret;
        SqliteSetError(self->db_, &self->errorCode_, &self->errorMessage_, ret);
        return ReturnBoolCompat(result, ret == SQLITE_OK);
    }

    static tjs_error TJS_INTF_METHOD execValue(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               SqliteCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            result->Clear();
        if(!self->db_)
            return TJS_S_OK;

        sqlite3_stmt *stmt = nullptr;
        int ret = sqlite3_prepare16_v2(self->db_, param[0]->GetString(), -1,
                                       &stmt, nullptr);
        if(ret == SQLITE_OK && stmt) {
            ret = SqliteBindParams(stmt,
                                   numparams > 1 && param[1] ? param[1] : nullptr);
            if(ret == SQLITE_OK) {
                int stepRet = sqlite3_step(stmt);
                if(stepRet == SQLITE_ROW && sqlite3_column_count(stmt) > 0)
                    SqliteColumnToVariant(stmt, 0, result);
                ret = (stepRet == SQLITE_ROW || stepRet == SQLITE_DONE)
                          ? SQLITE_OK
                          : stepRet;
            }
        }
        if(stmt)
            sqlite3_finalize(stmt);
        self->errorCode_ = ret;
        SqliteSetError(self->db_, &self->errorCode_, &self->errorMessage_,
                       self->errorCode_);
        return TJS_S_OK;
    }

    bool begin() { return execSimple("BEGIN TRANSACTION;"); }
    bool commit() { return execSimple("COMMIT;"); }
    bool rollback() { return execSimple("ROLLBACK;"); }
    tjs_int64 getLastInsertRowId() const {
        return db_ ? sqlite3_last_insert_rowid(db_) : 0;
    }
    tjs_int getErrorCode() const { return errorCode_; }
    ttstr getErrorMessage() const { return errorMessage_; }
    ttstr getDatabase() const { return database_; }
    bool getReadOnly() const { return readonly_; }
    sqlite3 *getHandle() const { return db_; }

private:
    bool execSimple(const char *sql) {
        if(!db_)
            return false;
        errorCode_ = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
        SqliteSetError(db_, &errorCode_, &errorMessage_, errorCode_);
        return errorCode_ == SQLITE_OK;
    }

    ttstr database_;
    bool readonly_ = false;
    tjs_int errorCode_ = SQLITE_OK;
    ttstr errorMessage_;
    sqlite3 *db_ = nullptr;
};

class SqliteStatementCompat {
public:
    static tjs_error factory(SqliteStatementCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_S_OK;
        if(numparams < 1 || !param || !param[0] ||
           param[0]->Type() != tvtObject)
            return TJS_E_BADPARAMCOUNT;
        auto *sqlite = ncbInstanceAdaptor<SqliteCompat>::GetNativeInstance(
            param[0]->AsObjectNoAddRef());
        if(!sqlite)
            TVPThrowExceptionMessage(TJS_W("use Sqlite class Object"));
        std::unique_ptr<SqliteStatementCompat> self(
            new SqliteStatementCompat(*param[0], sqlite));
        if(numparams > 1 && param && param[1] && param[1]->Type() != tvtVoid)
            self->openSql(ttstr(*param[1]),
                          numparams > 2 && param[2] ? param[2] : nullptr);
        if(objthis) {
            tTJSVariant name(TJS_W("missing"));
            objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
        }
        *result = self.release();
        return TJS_S_OK;
    }

    SqliteStatementCompat(tTJSVariant owner, SqliteCompat *sqlite) :
        owner_(owner), sqlite_(sqlite), db_(sqlite ? sqlite->getHandle() : nullptr) {}

    ~SqliteStatementCompat() { close(); }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        int ret = self->openSql(ttstr(*param[0]),
                                numparams > 1 && param[1] ? param[1] : nullptr);
        return ReturnIntCompat(result, ret);
    }

    void close() {
        if(stmt_) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
        sql_.Clear();
        bindPos_ = 1;
    }
    ttstr getSql() const { return sql_; }
    tjs_int reset() {
        bindPos_ = 1;
        return stmt_ ? sqlite3_reset(stmt_) : SQLITE_MISUSE;
    }
    tjs_int bind(tTJSVariant params = tTJSVariant()) {
        return stmt_ ? SqliteBindParams(stmt_, &params) : SQLITE_MISUSE;
    }

    static tjs_error TJS_INTF_METHOD bindAt(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !self->stmt_ || !param || !param[0])
            return ReturnIntCompat(result, SQLITE_MISUSE);
        int pos = numparams > 1 && param[1]
                      ? SqliteBindPos(self->stmt_, *param[1])
                      : self->bindPos_++;
        int ret = SqliteBindParam(self->stmt_, *param[0], pos);
        return ReturnIntCompat(result, ret);
    }

    tjs_int exec() {
        if(!stmt_)
            return SQLITE_MISUSE;
        int ret = sqlite3_step(stmt_);
        if(ret != SQLITE_ROW)
            reset();
        return ret;
    }
    bool step() {
        if(!stmt_)
            return false;
        int ret = sqlite3_step(stmt_);
        if(ret == SQLITE_ROW)
            return true;
        reset();
        return false;
    }
    tjs_int getCount() const { return stmt_ ? sqlite3_data_count(stmt_) : 0; }
    tjs_int getColumnCount() const {
        return stmt_ ? sqlite3_column_count(stmt_) : 0;
    }
    bool isNull(tTJSVariant column = tTJSVariant()) {
        int index = SqliteColumnIndex(stmt_, column);
        return index < 0 || sqlite3_column_type(stmt_, index) == SQLITE_NULL;
    }
    tjs_int getType(tTJSVariant column = tTJSVariant()) {
        int index = SqliteColumnIndex(stmt_, column);
        return index < 0 ? SQLITE_NULL : sqlite3_column_type(stmt_, index);
    }
    ttstr getName(tTJSVariant column = tTJSVariant()) {
        int index = SqliteColumnIndex(stmt_, column);
        if(index < 0)
            return ttstr();
        return reinterpret_cast<const tjs_char *>(
            sqlite3_column_name16(stmt_, index));
    }

    static tjs_error TJS_INTF_METHOD get(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!result)
            return TJS_S_OK;
        if(!self->stmt_) {
            result->Clear();
            return TJS_S_OK;
        }
        if(numparams == 0) {
            iTJSDispatch2 *array = TJSCreateArrayObject();
            int count = sqlite3_column_count(self->stmt_);
            for(int i = 0; i < count; ++i) {
                tTJSVariant value;
                SqliteColumnToVariant(self->stmt_, i, &value);
                tTJSVariant *args[] = { &value };
                array->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args,
                                array);
            }
            *result = tTJSVariant(array, array);
            array->Release();
            return TJS_S_OK;
        }
        int index = SqliteColumnIndex(self->stmt_, *param[0]);
        if(index < 0 || sqlite3_column_type(self->stmt_, index) == SQLITE_NULL) {
            if(numparams > 1 && param[1] && param[1]->Type() != tvtVoid)
                *result = *param[1];
            else
                result->Clear();
            return TJS_S_OK;
        }
        SqliteColumnToVariant(self->stmt_, index, result);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD missing(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        bool handled = false;
        if(numparams >= 3 && param && param[0] && param[1] && param[2] &&
           !static_cast<bool>(param[0]->AsInteger()) && self->stmt_) {
            int index = SqliteColumnIndex(self->stmt_, *param[1]);
            if(index >= 0) {
                tTJSVariant value;
                SqliteColumnToVariant(self->stmt_, index, &value);
                param[2]->AsObjectClosureNoAddRef().PropSet(
                    0, nullptr, nullptr, &value, nullptr);
                handled = true;
            }
        }
        return ReturnBoolCompat(result, handled);
    }

private:
    int openSql(const ttstr &sql, const tTJSVariant *params) {
        close();
        if(!db_)
            return SQLITE_MISUSE;
        sql_ = sql;
        int ret = sqlite3_prepare16_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
        if(ret == SQLITE_OK && stmt_) {
            reset();
            ret = SqliteBindParams(stmt_, params);
        }
        if(ret != SQLITE_OK)
            close();
        return ret;
    }

    tTJSVariant owner_;
    SqliteCompat *sqlite_ = nullptr;
    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    int bindPos_ = 1;
    ttstr sql_;
};

class SqliteThreadCompat {
public:
    static tjs_error factory(SqliteThreadCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        tTJSVariant sqliteOwner;
        SqliteCompat *sqlite = nullptr;
        for(tjs_int i = 0; i < numparams; ++i) {
            if(param && param[i] && param[i]->Type() == tvtObject) {
                sqlite = ncbInstanceAdaptor<SqliteCompat>::GetNativeInstance(
                    param[i]->AsObjectNoAddRef());
                if(sqlite) {
                    sqliteOwner = *param[i];
                    break;
                }
            }
        }
        if(!sqlite)
            TVPThrowExceptionMessage(TJS_W("use Sqlite class Object"));
        *result = new SqliteThreadCompat(sqliteOwner, sqlite);
        return TJS_S_OK;
    }

    SqliteThreadCompat(tTJSVariant owner, SqliteCompat *sqlite) :
        owner_(owner), sqlite_(sqlite), db_(sqlite ? sqlite->getHandle() : nullptr) {}

    bool select(ttstr sql, tTJSVariant params = tTJSVariant()) {
        abort();
        state_ = WORKING;
        errorCode_ = SQLITE_OK;
        iTJSDispatch2 *array = TJSCreateArrayObject();
        selectResult_ = tTJSVariant(array, array);
        array->Release();

        sqlite3_stmt *stmt = nullptr;
        errorCode_ = db_ ? sqlite3_prepare16_v2(db_, sql.c_str(), -1, &stmt,
                                                nullptr)
                         : SQLITE_MISUSE;
        if(errorCode_ == SQLITE_OK && stmt)
            errorCode_ = SqliteBindParams(stmt, &params);
        if(errorCode_ == SQLITE_OK && stmt) {
            tTJSVariantClosure target = selectResult_.AsObjectClosureNoAddRef();
            while((errorCode_ = sqlite3_step(stmt)) == SQLITE_ROW) {
                int columns = sqlite3_data_count(stmt);
                iTJSDispatch2 *line = TJSCreateArrayObject();
                for(int i = 0; i < columns; ++i) {
                    tTJSVariant value;
                    SqliteColumnToVariant(stmt, i, &value);
                    tTJSVariant *args[] = { &value };
                    line->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args,
                                   line);
                }
                tTJSVariant row(line, line);
                line->Release();
                tTJSVariant *args[] = { &row };
                target.FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, args,
                                nullptr);
            }
            if(errorCode_ == SQLITE_DONE)
                errorCode_ = SQLITE_OK;
        }
        if(stmt)
            sqlite3_finalize(stmt);
        state_ = DONE;
        return errorCode_ == SQLITE_OK;
    }

    bool update(ttstr sql, tTJSVariant data = tTJSVariant()) {
        abort();
        state_ = WORKING;
        errorCode_ = SQLITE_OK;
        if(!db_) {
            errorCode_ = SQLITE_MISUSE;
            state_ = DONE;
            return false;
        }

        sqlite3_stmt *stmt = nullptr;
        errorCode_ = sqlite3_prepare16_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if(errorCode_ != SQLITE_OK || !stmt) {
            state_ = DONE;
            return false;
        }

        sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        bool ok = true;
        if(data.Type() == tvtObject) {
            tTJSVariantClosure rows = data.AsObjectClosureNoAddRef();
            if(rows.IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr,
                                 TJS_W("Array"), nullptr) == TJS_S_TRUE) {
                tTJSVariant countVar;
                rows.PropGet(0, TJS_W("count"), nullptr, &countVar, nullptr);
                int count = static_cast<int>(countVar.AsInteger());
                for(int i = 0; i < count; ++i) {
                    tTJSVariant row;
                    rows.PropGetByNum(0, i, &row, nullptr);
                    sqlite3_reset(stmt);
                    sqlite3_clear_bindings(stmt);
                    errorCode_ = SqliteBindParams(stmt, &row);
                    if(errorCode_ != SQLITE_OK ||
                       ((errorCode_ = sqlite3_step(stmt)) != SQLITE_DONE &&
                        errorCode_ != SQLITE_ROW)) {
                        ok = false;
                        break;
                    }
                }
            } else {
                errorCode_ = SqliteBindParams(stmt, &data);
                ok = errorCode_ == SQLITE_OK &&
                     ((errorCode_ = sqlite3_step(stmt)) == SQLITE_DONE ||
                      errorCode_ == SQLITE_ROW);
            }
        } else {
            errorCode_ = SqliteBindParams(stmt, &data);
            ok = errorCode_ == SQLITE_OK &&
                 ((errorCode_ = sqlite3_step(stmt)) == SQLITE_DONE ||
                  errorCode_ == SQLITE_ROW);
        }
        sqlite3_finalize(stmt);
        sqlite3_exec(db_, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr,
                     nullptr);
        if(errorCode_ == SQLITE_DONE || errorCode_ == SQLITE_ROW)
            errorCode_ = SQLITE_OK;
        state_ = DONE;
        return ok && errorCode_ == SQLITE_OK;
    }
    void abort() {
        state_ = DONE;
    }
    tjs_int getState() const { return state_; }
    tjs_int getErrorCode() const { return errorCode_; }
    tTJSVariant getSelectResult() const { return selectResult_; }
    tjs_int getProgressUpdateCount() const { return progressUpdateCount_; }
    void setProgressUpdateCount(tjs_int value) { progressUpdateCount_ = value; }
    void onStateChange(tjs_int) {}
    void onProgress(tjs_int) {}

    enum { INIT = 0, WORKING = 1, DONE = 2 };

private:
    tTJSVariant owner_;
    SqliteCompat *sqlite_ = nullptr;
    sqlite3 *db_ = nullptr;
    tjs_int state_ = INIT;
    tjs_int errorCode_ = SQLITE_OK;
    tjs_int progressUpdateCount_ = 100;
    tTJSVariant selectResult_ = EmptyArrayCompat();
};

NCB_REGISTER_CLASS_DIFFER(Sqlite, SqliteCompat) {
    Factory(&SqliteCompat::factory);
    RawCallback(TJS_W("exec"), &Class::exec, 0);
    RawCallback(TJS_W("execValue"), &Class::execValue, 0);
    NCB_METHOD(begin);
    NCB_METHOD(commit);
    NCB_METHOD(rollback);
    NCB_PROPERTY_RO(lastInsertRowId, getLastInsertRowId);
    NCB_PROPERTY_RO(errorCode, getErrorCode);
    NCB_PROPERTY_RO(errorMessage, getErrorMessage);
    NCB_PROPERTY_RO(database, getDatabase);
    NCB_PROPERTY_RO(readonly, getReadOnly);
    Variant(TJS_W("SQLITE_OK"), (tjs_int)SQLITE_OK);
    Variant(TJS_W("SQLITE_ERROR"), (tjs_int)SQLITE_ERROR);
    Variant(TJS_W("SQLITE_INTERNAL"), (tjs_int)SQLITE_INTERNAL);
    Variant(TJS_W("SQLITE_PERM"), (tjs_int)SQLITE_PERM);
    Variant(TJS_W("SQLITE_ABORT"), (tjs_int)SQLITE_ABORT);
    Variant(TJS_W("SQLITE_BUSY"), (tjs_int)SQLITE_BUSY);
    Variant(TJS_W("SQLITE_LOCKED"), (tjs_int)SQLITE_LOCKED);
    Variant(TJS_W("SQLITE_NOMEM"), (tjs_int)SQLITE_NOMEM);
    Variant(TJS_W("SQLITE_READONLY"), (tjs_int)SQLITE_READONLY);
    Variant(TJS_W("SQLITE_INTERRUPT"), (tjs_int)SQLITE_INTERRUPT);
    Variant(TJS_W("SQLITE_IOERR"), (tjs_int)SQLITE_IOERR);
    Variant(TJS_W("SQLITE_CORRUPT"), (tjs_int)SQLITE_CORRUPT);
    Variant(TJS_W("SQLITE_NOTFOUND"), (tjs_int)SQLITE_NOTFOUND);
    Variant(TJS_W("SQLITE_FULL"), (tjs_int)SQLITE_FULL);
    Variant(TJS_W("SQLITE_CANTOPEN"), (tjs_int)SQLITE_CANTOPEN);
    Variant(TJS_W("SQLITE_PROTOCOL"), (tjs_int)SQLITE_PROTOCOL);
    Variant(TJS_W("SQLITE_EMPTY"), (tjs_int)SQLITE_EMPTY);
    Variant(TJS_W("SQLITE_SCHEMA"), (tjs_int)SQLITE_SCHEMA);
    Variant(TJS_W("SQLITE_TOOBIG"), (tjs_int)SQLITE_TOOBIG);
    Variant(TJS_W("SQLITE_CONSTRAINT"), (tjs_int)SQLITE_CONSTRAINT);
    Variant(TJS_W("SQLITE_MISMATCH"), (tjs_int)SQLITE_MISMATCH);
    Variant(TJS_W("SQLITE_MISUSE"), (tjs_int)SQLITE_MISUSE);
    Variant(TJS_W("SQLITE_NOLFS"), (tjs_int)SQLITE_NOLFS);
    Variant(TJS_W("SQLITE_AUTH"), (tjs_int)SQLITE_AUTH);
    Variant(TJS_W("SQLITE_FORMAT"), (tjs_int)SQLITE_FORMAT);
    Variant(TJS_W("SQLITE_RANGE"), (tjs_int)SQLITE_RANGE);
    Variant(TJS_W("SQLITE_NOTADB"), (tjs_int)SQLITE_NOTADB);
    Variant(TJS_W("SQLITE_ROW"), (tjs_int)SQLITE_ROW);
    Variant(TJS_W("SQLITE_DONE"), (tjs_int)SQLITE_DONE);
}

NCB_REGISTER_CLASS_DIFFER(SqliteStatement, SqliteStatementCompat) {
    Factory(&SqliteStatementCompat::factory);
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    NCB_PROPERTY_RO(sql, getSql);
    NCB_METHOD(reset);
    NCB_METHOD(bind);
    RawCallback(TJS_W("bindAt"), &Class::bindAt, 0);
    NCB_METHOD(exec);
    NCB_METHOD(step);
    NCB_PROPERTY_RO(count, getCount);
    NCB_PROPERTY_RO(columnCount, getColumnCount);
    NCB_METHOD(isNull);
    NCB_METHOD(getType);
    NCB_METHOD(getName);
    RawCallback(TJS_W("get"), &Class::get, 0);
    RawCallback(TJS_W("missing"), &Class::missing, 0);
    Variant(TJS_W("SQLITE_INTEGER"), (tjs_int)SQLITE_INTEGER);
    Variant(TJS_W("SQLITE_FLOAT"), (tjs_int)SQLITE_FLOAT);
    Variant(TJS_W("SQLITE_TEXT"), (tjs_int)SQLITE_TEXT);
    Variant(TJS_W("SQLITE_BLOB"), (tjs_int)SQLITE_BLOB);
    Variant(TJS_W("SQLITE_NULL"), (tjs_int)SQLITE_NULL);
}

NCB_REGISTER_CLASS_DIFFER(SqliteThread, SqliteThreadCompat) {
    Factory(&SqliteThreadCompat::factory);
    NCB_METHOD(select);
    NCB_METHOD(update);
    NCB_METHOD(abort);
    NCB_PROPERTY_RO(state, getState);
    NCB_PROPERTY_RO(errorCode, getErrorCode);
    NCB_PROPERTY_RO(selectResult, getSelectResult);
    NCB_PROPERTY(progressUpdateCount, getProgressUpdateCount,
                 setProgressUpdateCount);
    NCB_METHOD(onStateChange);
    NCB_METHOD(onProgress);
    Variant(TJS_W("INIT"), (tjs_int)SqliteThreadCompat::INIT);
    Variant(TJS_W("WORKING"), (tjs_int)SqliteThreadCompat::WORKING);
    Variant(TJS_W("DONE"), (tjs_int)SqliteThreadCompat::DONE);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sqlite3_xp3_vfs.dll")
static void sqliteXp3VfsAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("sqlite3.dll"));
}
NCB_PRE_REGIST_CALLBACK(sqliteXp3VfsAliasCompat);

// ---------------------------------------------------------------------------
// expat.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("expat.dll")

class XMLParserCompat {
public:
    static tjs_error factory(XMLParserCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_S_OK;
        auto *parser = new XMLParserCompat();
        if(objthis)
            parser->owner_ = objthis;
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject)
            parser->target_ = *param[0];
        *result = parser;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD parse(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           XMLParserCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;

        iTJSDispatch2 *target = self->resolveTarget(numparams, param);
        bool ok = self->parseText(ttstr(*param[0]), target);
        return ReturnBoolCompat(result, ok);
    }

    static tjs_error TJS_INTF_METHOD parseStorage(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  XMLParserCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;

        std::vector<tjs_uint8> bytes = ReadStorageBytes(ttstr(*param[0]));
        ttstr text = Utf8BytesToTtstrCompat(bytes);
        iTJSDispatch2 *target = self->resolveTarget(numparams, param);
        bool ok = self->parseText(text, target);
        return ReturnBoolCompat(result, ok);
    }

    tjs_int getErrorCode() const { return errorCode_; }
    ttstr getErrorString() const { return errorString_; }
    tjs_int getCurrentByteIndex() const { return currentByteIndex_; }
    tjs_int getCurrentLineNumber() const { return currentLineNumber_; }
    tjs_int getCurrentColumnNumber() const { return currentColumnNumber_; }
    tjs_int getCurrentByteCount() const { return currentByteCount_; }

private:
    static bool isSpace(tjs_char ch) {
        return ch == TJS_W(' ') || ch == TJS_W('\t') ||
               ch == TJS_W('\r') || ch == TJS_W('\n');
    }

    static bool isNameChar(tjs_char ch) {
        return ch > 0x7f || (ch >= TJS_W('a') && ch <= TJS_W('z')) ||
               (ch >= TJS_W('A') && ch <= TJS_W('Z')) ||
               (ch >= TJS_W('0') && ch <= TJS_W('9')) ||
               ch == TJS_W('_') || ch == TJS_W('-') ||
               ch == TJS_W(':') || ch == TJS_W('.');
    }

    static bool startsWith(const tjs_char *data, size_t len, size_t pos,
                           const tjs_char *needle) {
        size_t i = 0;
        while(needle[i]) {
            if(pos + i >= len || data[pos + i] != needle[i])
                return false;
            ++i;
        }
        return true;
    }

    static size_t findSequence(const tjs_char *data, size_t len, size_t pos,
                               const tjs_char *needle) {
        for(size_t i = pos; i < len; ++i) {
            if(startsWith(data, len, i, needle))
                return i;
        }
        return len;
    }

    static ttstr makeString(const tjs_char *begin, const tjs_char *end,
                            bool decodeEntities = false) {
        if(!decodeEntities)
            return ttstr(begin, static_cast<tjs_int>(end - begin));
        return TtstrFromChars(decodeXmlEntities(begin, end));
    }

    static std::basic_string<tjs_char>
    decodeXmlEntities(const tjs_char *begin, const tjs_char *end) {
        std::basic_string<tjs_char> out;
        for(const tjs_char *p = begin; p < end; ++p) {
            if(*p != TJS_W('&')) {
                out.push_back(*p);
                continue;
            }
            const tjs_char *semi = p + 1;
            while(semi < end && *semi != TJS_W(';'))
                ++semi;
            if(semi >= end) {
                out.push_back(*p);
                continue;
            }
            std::basic_string<tjs_char> entity(p + 1, semi);
            if(entity == std::basic_string<tjs_char>{ TJS_W('l'), TJS_W('t') })
                out.push_back(TJS_W('<'));
            else if(entity ==
                    std::basic_string<tjs_char>{ TJS_W('g'), TJS_W('t') })
                out.push_back(TJS_W('>'));
            else if(entity == std::basic_string<tjs_char>{
                                  TJS_W('a'), TJS_W('m'), TJS_W('p') })
                out.push_back(TJS_W('&'));
            else if(entity == std::basic_string<tjs_char>{
                                  TJS_W('q'), TJS_W('u'), TJS_W('o'),
                                  TJS_W('t') })
                out.push_back(TJS_W('"'));
            else if(entity == std::basic_string<tjs_char>{
                                  TJS_W('a'), TJS_W('p'), TJS_W('o'),
                                  TJS_W('s') })
                out.push_back(TJS_W('\''));
            else if(!entity.empty() && entity[0] == TJS_W('#')) {
                tjs_uint32 code = 0;
                size_t i = 1;
                int base = 10;
                if(i < entity.size() &&
                   (entity[i] == TJS_W('x') || entity[i] == TJS_W('X'))) {
                    base = 16;
                    ++i;
                }
                for(; i < entity.size(); ++i) {
                    tjs_char ch = entity[i];
                    int digit = -1;
                    if(ch >= TJS_W('0') && ch <= TJS_W('9'))
                        digit = ch - TJS_W('0');
                    else if(ch >= TJS_W('a') && ch <= TJS_W('f'))
                        digit = ch - TJS_W('a') + 10;
                    else if(ch >= TJS_W('A') && ch <= TJS_W('F'))
                        digit = ch - TJS_W('A') + 10;
                    if(digit < 0 || digit >= base) {
                        code = 0;
                        break;
                    }
                    code = code * base + static_cast<tjs_uint32>(digit);
                }
                if(code <= 0xffff) {
                    out.push_back(static_cast<tjs_char>(code));
                } else if(code <= 0x10ffff) {
                    code -= 0x10000;
                    out.push_back(static_cast<tjs_char>(0xd800 + (code >> 10)));
                    out.push_back(static_cast<tjs_char>(0xdc00 + (code & 0x3ff)));
                }
            } else {
                out.push_back(TJS_W('&'));
                out.append(entity);
                out.push_back(TJS_W(';'));
            }
            p = semi;
        }
        return out;
    }

    iTJSDispatch2 *resolveTarget(tjs_int numparams, tTJSVariant **param) {
        if(numparams > 1 && param && param[1] &&
           param[1]->Type() == tvtObject)
            return param[1]->AsObjectNoAddRef();
        if(target_.Type() == tvtObject)
            return target_.AsObjectNoAddRef();
        return owner_;
    }

    void mark(const ttstr &text, size_t pos, size_t count) {
        const tjs_char *data = text.c_str();
        currentLineNumber_ = 1;
        currentColumnNumber_ = 0;
        for(size_t i = 0; i < pos; ++i) {
            if(data[i] == TJS_W('\n')) {
                ++currentLineNumber_;
                currentColumnNumber_ = 0;
            } else {
                ++currentColumnNumber_;
            }
        }
        currentByteIndex_ = static_cast<tjs_int>(pos);
        currentByteCount_ = static_cast<tjs_int>(count);
    }

    bool fail(const ttstr &text, size_t pos, const tjs_char *message) {
        mark(text, pos, 0);
        errorCode_ = 1;
        errorString_ = message;
        return false;
    }

    void call0(iTJSDispatch2 *target, const tjs_char *name) {
        std::vector<tTJSVariant> args;
        CallTjsMethodCompat(target, name, args);
    }

    void call1(iTJSDispatch2 *target, const tjs_char *name,
               const tTJSVariant &arg) {
        std::vector<tTJSVariant> args;
        args.push_back(arg);
        CallTjsMethodCompat(target, name, args);
    }

    void callDefault(iTJSDispatch2 *target, const tTJSVariant &arg) {
        std::vector<tTJSVariant> args;
        args.push_back(arg);
        if(!CallTjsMethodCompat(target, TJS_W("defaultHandlerExpand"), args))
            CallTjsMethodCompat(target, TJS_W("defaultHandler"), args);
    }

#if KRKR2_PLUGIN_HAS_TINYXML2
    void callTinyProcessingInstruction(iTJSDispatch2 *target,
                                       const char *value) {
        std::string raw = value ? value : "";
        size_t nameEnd = 0;
        while(nameEnd < raw.size() &&
              !std::isspace(static_cast<unsigned char>(raw[nameEnd])))
            ++nameEnd;
        size_t bodyBegin = nameEnd;
        while(bodyBegin < raw.size() &&
              std::isspace(static_cast<unsigned char>(raw[bodyBegin])))
            ++bodyBegin;

        std::vector<tTJSVariant> args;
        args.emplace_back(Utf8TextToTtstrCompat(raw.data(), nameEnd));
        args.emplace_back(Utf8TextToTtstrCompat(raw.data() + bodyBegin,
                                                raw.size() - bodyBegin));
        CallTjsMethodCompat(target, TJS_W("processingInstruction"), args);
    }

    bool emitTinyNode(const tinyxml2::XMLNode *node, iTJSDispatch2 *target) {
        if(!node)
            return true;

        if(const tinyxml2::XMLElement *element = node->ToElement()) {
            iTJSDispatch2 *dict = TJSCreateDictionaryObject();
            for(const tinyxml2::XMLAttribute *attr = element->FirstAttribute();
                attr; attr = attr->Next()) {
                ttstr name = Utf8TextToTtstrCompat(attr->Name());
                tTJSVariant value(Utf8TextToTtstrCompat(attr->Value()));
                dict->PropSet(TJS_MEMBERENSURE, name.c_str(), nullptr, &value,
                              dict);
            }

            std::vector<tTJSVariant> args;
            args.emplace_back(Utf8TextToTtstrCompat(element->Name()));
            tTJSVariant attrs(dict, dict);
            dict->Release();
            args.push_back(attrs);
            CallTjsMethodCompat(target, TJS_W("startElement"), args);

            for(const tinyxml2::XMLNode *child = element->FirstChild(); child;
                child = child->NextSibling())
                emitTinyNode(child, target);

            call1(target, TJS_W("endElement"),
                  tTJSVariant(Utf8TextToTtstrCompat(element->Name())));
            return true;
        }

        if(const tinyxml2::XMLText *textNode = node->ToText()) {
            if(textNode->CData())
                call0(target, TJS_W("startCdataSection"));
            call1(target, TJS_W("characterData"),
                  tTJSVariant(Utf8TextToTtstrCompat(textNode->Value())));
            if(textNode->CData())
                call0(target, TJS_W("endCdataSection"));
            return true;
        }

        if(const tinyxml2::XMLComment *commentNode = node->ToComment()) {
            call1(target, TJS_W("comment"),
                  tTJSVariant(Utf8TextToTtstrCompat(commentNode->Value())));
            return true;
        }

        if(const tinyxml2::XMLDeclaration *declaration =
               node->ToDeclaration()) {
            callTinyProcessingInstruction(target, declaration->Value());
            return true;
        }

        if(const tinyxml2::XMLUnknown *unknown = node->ToUnknown()) {
            ttstr value = TJS_W("<");
            value += Utf8TextToTtstrCompat(unknown->Value());
            value += TJS_W(">");
            callDefault(target, tTJSVariant(value));
            return true;
        }

        for(const tinyxml2::XMLNode *child = node->FirstChild(); child;
            child = child->NextSibling())
            emitTinyNode(child, target);
        return true;
    }

    bool parseTinyXml2Text(const ttstr &text, iTJSDispatch2 *target) {
        errorCode_ = 0;
        errorString_.Clear();
        currentByteIndex_ = currentByteCount_ = 0;
        currentLineNumber_ = 1;
        currentColumnNumber_ = 0;

        std::string utf8 = TtstrToUtf8Compat(text);
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLError ret = doc.Parse(utf8.c_str(), utf8.size());
        if(ret != tinyxml2::XML_SUCCESS) {
            errorCode_ = static_cast<tjs_int>(ret);
            errorString_ =
                TJS_W("XML parse error: ") + ttstr(static_cast<tjs_int>(ret));
            return false;
        }

        for(const tinyxml2::XMLNode *child = doc.FirstChild(); child;
            child = child->NextSibling())
            emitTinyNode(child, target);
        return true;
    }
#endif

    bool parseText(const ttstr &text, iTJSDispatch2 *target) {
#if KRKR2_PLUGIN_HAS_TINYXML2
        if(parseTinyXml2Text(text, target))
            return true;
#endif
        return parseLegacyText(text, target);
    }

    bool parseLegacyText(const ttstr &text, iTJSDispatch2 *target) {
        errorCode_ = 0;
        errorString_.Clear();
        currentByteIndex_ = currentByteCount_ = 0;
        currentLineNumber_ = 1;
        currentColumnNumber_ = 0;

        const tjs_char *data = text.c_str();
        const size_t len = static_cast<size_t>(text.GetLen());
        size_t pos = 0;
        while(pos < len) {
            if(data[pos] != TJS_W('<')) {
                size_t next = pos;
                while(next < len && data[next] != TJS_W('<'))
                    ++next;
                if(next > pos) {
                    mark(text, pos, next - pos);
                    call1(target, TJS_W("characterData"),
                          tTJSVariant(makeString(data + pos, data + next, true)));
                }
                pos = next;
                continue;
            }

            if(startsWith(data, len, pos, TJS_W("<!--"))) {
                size_t end = findSequence(data, len, pos + 4, TJS_W("-->"));
                if(end >= len)
                    return fail(text, pos, TJS_W("unterminated XML comment"));
                mark(text, pos, end + 3 - pos);
                call1(target, TJS_W("comment"),
                      tTJSVariant(makeString(data + pos + 4, data + end)));
                pos = end + 3;
                continue;
            }

            if(startsWith(data, len, pos, TJS_W("<![CDATA["))) {
                size_t end = findSequence(data, len, pos + 9, TJS_W("]]>"));
                if(end >= len)
                    return fail(text, pos, TJS_W("unterminated CDATA section"));
                mark(text, pos, end + 3 - pos);
                call0(target, TJS_W("startCdataSection"));
                call1(target, TJS_W("characterData"),
                      tTJSVariant(makeString(data + pos + 9, data + end)));
                call0(target, TJS_W("endCdataSection"));
                pos = end + 3;
                continue;
            }

            if(startsWith(data, len, pos, TJS_W("<?"))) {
                size_t end = findSequence(data, len, pos + 2, TJS_W("?>"));
                if(end >= len)
                    return fail(text, pos,
                                TJS_W("unterminated processing instruction"));
                size_t nameBegin = pos + 2;
                size_t nameEnd = nameBegin;
                while(nameEnd < end && !isSpace(data[nameEnd]))
                    ++nameEnd;
                size_t bodyBegin = nameEnd;
                while(bodyBegin < end && isSpace(data[bodyBegin]))
                    ++bodyBegin;
                mark(text, pos, end + 2 - pos);
                std::vector<tTJSVariant> args;
                args.emplace_back(makeString(data + nameBegin, data + nameEnd));
                args.emplace_back(makeString(data + bodyBegin, data + end));
                CallTjsMethodCompat(target, TJS_W("processingInstruction"),
                                    args);
                pos = end + 2;
                continue;
            }

            if(startsWith(data, len, pos, TJS_W("</"))) {
                size_t cursor = pos + 2;
                while(cursor < len && isSpace(data[cursor]))
                    ++cursor;
                size_t nameBegin = cursor;
                while(cursor < len && isNameChar(data[cursor]))
                    ++cursor;
                size_t nameEnd = cursor;
                while(cursor < len && data[cursor] != TJS_W('>'))
                    ++cursor;
                if(cursor >= len)
                    return fail(text, pos, TJS_W("unterminated XML end tag"));
                mark(text, pos, cursor + 1 - pos);
                call1(target, TJS_W("endElement"),
                      tTJSVariant(makeString(data + nameBegin, data + nameEnd)));
                pos = cursor + 1;
                continue;
            }

            if(startsWith(data, len, pos, TJS_W("<!"))) {
                size_t end = pos + 2;
                tjs_char quote = 0;
                while(end < len) {
                    if(quote) {
                        if(data[end] == quote)
                            quote = 0;
                    } else if(data[end] == TJS_W('"') ||
                              data[end] == TJS_W('\'')) {
                        quote = data[end];
                    } else if(data[end] == TJS_W('>')) {
                        break;
                    }
                    ++end;
                }
                if(end >= len)
                    return fail(text, pos, TJS_W("unterminated XML declaration"));
                mark(text, pos, end + 1 - pos);
                callDefault(target,
                            tTJSVariant(makeString(data + pos, data + end + 1)));
                pos = end + 1;
                continue;
            }

            size_t cursor = pos + 1;
            while(cursor < len && isSpace(data[cursor]))
                ++cursor;
            size_t nameBegin = cursor;
            while(cursor < len && isNameChar(data[cursor]))
                ++cursor;
            size_t nameEnd = cursor;
            if(nameBegin == nameEnd)
                return fail(text, pos, TJS_W("invalid XML start tag"));

            iTJSDispatch2 *dict = TJSCreateDictionaryObject();
            bool selfClosing = false;
            bool tagClosed = false;
            while(cursor < len) {
                while(cursor < len && isSpace(data[cursor]))
                    ++cursor;
                if(cursor >= len)
                    break;
                if(data[cursor] == TJS_W('/')) {
                    if(cursor + 1 < len && data[cursor + 1] == TJS_W('>')) {
                        selfClosing = true;
                        tagClosed = true;
                        cursor += 2;
                        break;
                    }
                    ++cursor;
                    continue;
                }
                if(data[cursor] == TJS_W('>')) {
                    tagClosed = true;
                    ++cursor;
                    break;
                }

                size_t attrNameBegin = cursor;
                while(cursor < len && isNameChar(data[cursor]))
                    ++cursor;
                size_t attrNameEnd = cursor;
                while(cursor < len && isSpace(data[cursor]))
                    ++cursor;
                std::basic_string<tjs_char> attrValue;
                if(cursor < len && data[cursor] == TJS_W('=')) {
                    ++cursor;
                    while(cursor < len && isSpace(data[cursor]))
                        ++cursor;
                    if(cursor < len && (data[cursor] == TJS_W('"') ||
                                        data[cursor] == TJS_W('\''))) {
                        tjs_char quote = data[cursor++];
                        size_t valueBegin = cursor;
                        while(cursor < len && data[cursor] != quote)
                            ++cursor;
                        if(cursor >= len) {
                            if(dict)
                                dict->Release();
                            return fail(text, pos,
                                        TJS_W("unterminated XML attribute"));
                        }
                        attrValue =
                            decodeXmlEntities(data + valueBegin, data + cursor);
                        ++cursor;
                    } else {
                        size_t valueBegin = cursor;
                        while(cursor < len && !isSpace(data[cursor]) &&
                              data[cursor] != TJS_W('>'))
                            ++cursor;
                        attrValue =
                            decodeXmlEntities(data + valueBegin, data + cursor);
                    }
                }
                if(dict && attrNameBegin != attrNameEnd) {
                    std::basic_string<tjs_char> attrName(data + attrNameBegin,
                                                         data + attrNameEnd);
                    tTJSVariant value(TtstrFromChars(attrValue));
                    dict->PropSet(TJS_MEMBERENSURE, attrName.c_str(), nullptr,
                                  &value, dict);
                }
            }

            if(!tagClosed) {
                if(dict)
                    dict->Release();
                return fail(text, pos, TJS_W("unterminated XML start tag"));
            }

            mark(text, pos, cursor - pos);
            std::vector<tTJSVariant> args;
            args.emplace_back(makeString(data + nameBegin, data + nameEnd));
            if(dict) {
                tTJSVariant attrs(dict, dict);
                dict->Release();
                args.push_back(attrs);
            } else {
                args.push_back(EmptyDictionaryCompat());
            }
            CallTjsMethodCompat(target, TJS_W("startElement"), args);
            if(selfClosing)
                call1(target, TJS_W("endElement"),
                      tTJSVariant(makeString(data + nameBegin, data + nameEnd)));
            pos = cursor;
        }
        return true;
    }

    iTJSDispatch2 *owner_ = nullptr;
    tTJSVariant target_;
    tjs_int errorCode_ = 0;
    ttstr errorString_;
    tjs_int currentByteIndex_ = 0;
    tjs_int currentLineNumber_ = 1;
    tjs_int currentColumnNumber_ = 0;
    tjs_int currentByteCount_ = 0;
};

NCB_REGISTER_CLASS_DIFFER(XMLParser, XMLParserCompat) {
    Factory(&XMLParserCompat::factory);
    RawCallback(TJS_W("parse"), &Class::parse, 0);
    RawCallback(TJS_W("parseStorage"), &Class::parseStorage, 0);
    NCB_PROPERTY_RO(errorCode, getErrorCode);
    NCB_PROPERTY_RO(errorString, getErrorString);
    NCB_PROPERTY_RO(currentByteIndex, getCurrentByteIndex);
    NCB_PROPERTY_RO(currentLineNumber, getCurrentLineNumber);
    NCB_PROPERTY_RO(currentColumnNumber, getCurrentColumnNumber);
    NCB_PROPERTY_RO(currentByteCount, getCurrentByteCount);
}

// ---------------------------------------------------------------------------
// javascript.dll / wsh.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("javascript.dll")

class JavascriptScriptsCompat {
public:
    static tjs_error TJS_INTF_METHOD execJS(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        // No JavaScript runtime is linked here. Evaluating as TJS keeps simple
        // probe scripts useful and leaves unsupported JS syntax to the engine.
        TVPExecuteExpression(ttstr(*param[0]), result);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD execStorageJS(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> bytes = ReadStorageBytes(ttstr(*param[0]));
        std::string text(bytes.begin(), bytes.end());
        tTJSVariant script{ttstr(text)};
        tTJSVariant *args[1] = { &script };
        return execJS(result, 1, args, objthis);
    }

    static tjs_error TJS_INTF_METHOD enableDebugJS(tTJSVariant *result,
                                                  tjs_int, tTJSVariant **,
                                                  iTJSDispatch2 *) {
        return ReturnBoolCompat(result, false);
    }

    static tjs_error TJS_INTF_METHOD processDebugJS(tTJSVariant *result,
                                                    tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *) {
        return ReturnVoidCompat(result);
    }
};

NCB_ATTACH_CLASS(JavascriptScriptsCompat, Scripts) {
    RawCallback(TJS_W("execJS"), &JavascriptScriptsCompat::execJS,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("execStorageJS"), &JavascriptScriptsCompat::execStorageJS,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("enableDebugJS"), &JavascriptScriptsCompat::enableDebugJS,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("processDebugJS"),
                &JavascriptScriptsCompat::processDebugJS, TJS_STATICMEMBER);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wsh.dll")

class WshScriptsCompat {
public:
    static tjs_error TJS_INTF_METHOD addProgId(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               iTJSDispatch2 *) {
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD execWSH(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *objthis) {
        return JavascriptScriptsCompat::execJS(result, numparams, param,
                                               objthis);
    }

    static tjs_error TJS_INTF_METHOD execStorageWSH(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *objthis) {
        return JavascriptScriptsCompat::execStorageJS(result, numparams, param,
                                                      objthis);
    }
};

NCB_ATTACH_CLASS(WshScriptsCompat, Scripts) {
    RawCallback(TJS_W("addProgId"), &WshScriptsCompat::addProgId,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("execWSH"), &WshScriptsCompat::execWSH,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("execStorageWSH"), &WshScriptsCompat::execStorageWSH,
                TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// flashPlayer.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("flashPlayer.dll")

class FlashPlayerCompat {
public:
    static tjs_error factory(FlashPlayerCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(!result)
            return TJS_S_OK;
        tjs_int width = numparams > 0 && param && param[0]
                            ? static_cast<tjs_int>(param[0]->AsInteger())
                            : 0;
        tjs_int height = numparams > 1 && param && param[1]
                             ? static_cast<tjs_int>(param[1]->AsInteger())
                             : 0;
        *result = new FlashPlayerCompat(objthis, width, height);
        return TJS_S_OK;
    }

    FlashPlayerCompat(iTJSDispatch2 *objthis, tjs_int width, tjs_int height) :
        owner_(objthis), width_(width), height_(height) {}

    bool clearMovie() {
        movie_.Clear();
        movieData_.Clear();
        frameNum_ = 0;
        playing_ = false;
        return true;
    }

    bool initMovie(const tjs_char *storage) {
        movie_ = storage ? storage : TJS_W("");
        frameNum_ = 0;
        readyState_ = movie_.IsEmpty() ? 0 : 4;
        percentLoaded_ = movie_.IsEmpty() ? 0 : 100;
        return !movie_.IsEmpty();
    }

    void setSize(tjs_int width, tjs_int height) {
        width_ = width;
        height_ = height;
    }

    bool hitTest(tjs_int x, tjs_int y) const {
        return x >= 0 && y >= 0 && x < width_ && y < height_;
    }

    bool doKeyDown(tjs_int) { return false; }
    bool doKeyUp(tjs_int) { return false; }
    void doMouseEnter() {}
    void doMouseLeave() {}
    bool doMouseDown(tjs_int, tjs_int, tjs_int, tjs_int) { return false; }
    bool doMouseMove(tjs_int, tjs_int, tjs_int) { return false; }
    bool doMouseUp(tjs_int, tjs_int, tjs_int, tjs_int) { return false; }
    bool doMouseWheel(tjs_int, tjs_int, tjs_int, tjs_int) { return false; }

    tjs_int getReadyState() const { return readyState_; }
    tjs_int getTotalFrames() const { return totalFrames_; }
    bool getPlaying() const { return playing_; }
    void setPlaying(bool value) { playing_ = value; }
    tjs_int getQuality() const { return quality_; }
    void setQuality(tjs_int value) { quality_ = value; }
    tjs_int getScaleMode() const { return scaleMode_; }
    void setScaleMode(tjs_int value) { scaleMode_ = value; }
    tjs_int getAlignMode() const { return alignMode_; }
    void setAlignMode(tjs_int value) { alignMode_ = value; }
    tjs_int getBackgroundColor() const { return backgroundColor_; }
    void setBackgroundColor(tjs_int value) { backgroundColor_ = value; }
    bool getLoop() const { return loop_; }
    void setLoop(bool value) { loop_ = value; }
    ttstr getMovie() const { return movie_; }
    void setMovie(const tjs_char *value) { initMovie(value); }
    tjs_int getFrameNum() const { return frameNum_; }
    void setFrameNum(tjs_int value) { frameNum_ = value; }

    void setZoomRect(tjs_int, tjs_int, tjs_int, tjs_int) {}
    void zoom(tjs_int) {}
    void pan(tjs_int, tjs_int, tjs_int) {}
    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void back() {
        if(frameNum_ > 0)
            --frameNum_;
    }
    void forward() { ++frameNum_; }
    void rewind() { frameNum_ = 0; }
    void stopPlay() { stop(); }
    void gotoFrame(tjs_int frame) { frameNum_ = frame; }
    tjs_int getCurrentFrame() const { return frameNum_; }
    bool isPlaying() const { return playing_; }
    tjs_int getPercentLoaded() const { return percentLoaded_; }
    bool getFrameLoaded(tjs_int frame) const {
        return totalFrames_ == 0 || frame <= totalFrames_;
    }
    ttstr getFlashVersion() const { return TJS_W("0,0,0,0"); }

    ttstr getSAlign() const { return sAlign_; }
    void setSAlign(const tjs_char *value) { sAlign_ = value ? value : TJS_W(""); }
    bool getMenu() const { return menu_; }
    void setMenu(bool value) { menu_ = value; }
    ttstr getBase() const { return base_; }
    void setBase(const tjs_char *value) { base_ = value ? value : TJS_W(""); }
    ttstr getScale() const { return scale_; }
    void setScale(const tjs_char *value) { scale_ = value ? value : TJS_W(""); }
    bool getDeviceFont() const { return deviceFont_; }
    void setDeviceFont(bool value) { deviceFont_ = value; }
    bool getEmbedMovie() const { return embedMovie_; }
    void setEmbedMovie(bool value) { embedMovie_ = value; }
    ttstr getBgColor() const { return bgColor_; }
    void setBgColor(const tjs_char *value) { bgColor_ = value ? value : TJS_W(""); }
    ttstr getQuality2() const { return quality2_; }
    void setQuality2(const tjs_char *value) {
        quality2_ = value ? value : TJS_W("");
    }

    void loadMovie(tjs_int, const tjs_char *url) { initMovie(url); }
    void tGotoFrame(const tjs_char *, tjs_int frame) { frameNum_ = frame; }
    void tGotoLabel(const tjs_char *, const tjs_char *) {}
    tjs_int tCurrentFrame(const tjs_char *) const { return frameNum_; }
    ttstr tCurrentLabel(const tjs_char *) const { return ttstr(); }
    void tPlay(const tjs_char *) { play(); }
    void tStopPlay(const tjs_char *) { stop(); }
    void setVariable(const tjs_char *, tTJSVariant) {}
    tTJSVariant getVariable(const tjs_char *) const { return tTJSVariant(); }
    void tSetProperty(const tjs_char *, tjs_int, const tjs_char *) {}
    ttstr tGetProperty(const tjs_char *, tjs_int) const { return ttstr(); }
    void tCallFrame(const tjs_char *, tjs_int) {}
    void tCallLabel(const tjs_char *, const tjs_char *) {}
    void tSetPropertyNum(const tjs_char *, tjs_int, tjs_real) {}
    tjs_real tGetPropertyNum(const tjs_char *, tjs_int) const { return 0.0; }

    ttstr getSWRemote() const { return swRemote_; }
    void setSWRemote(const tjs_char *value) {
        swRemote_ = value ? value : TJS_W("");
    }
    ttstr getFlashVars() const { return flashVars_; }
    void setFlashVars(const tjs_char *value) {
        flashVars_ = value ? value : TJS_W("");
    }
    ttstr getAllowScriptAccess() const { return allowScriptAccess_; }
    void setAllowScriptAccess(const tjs_char *value) {
        allowScriptAccess_ = value ? value : TJS_W("");
    }
    tTJSVariant getMovieData() const { return movieData_; }
    void setMovieData(tTJSVariant value) { movieData_ = value; }
    bool getSeamlessTabbing() const { return seamlessTabbing_; }
    void setSeamlessTabbing(bool value) { seamlessTabbing_ = value; }
    bool getProfile() const { return profile_; }
    void setProfile(bool value) { profile_ = value; }
    ttstr getProfileAddress() const { return profileAddress_; }
    void setProfileAddress(const tjs_char *value) {
        profileAddress_ = value ? value : TJS_W("");
    }
    tjs_int getProfilePort() const { return profilePort_; }
    void setProfilePort(tjs_int value) { profilePort_ = value; }
    void enforceLocalSecurity() {}
    void disableLocalSecurity() {}

    static tjs_error TJS_INTF_METHOD draw(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          FlashPlayerCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        return ReturnBoolCompat(result, false);
    }

    static tjs_error TJS_INTF_METHOD callFunction(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  FlashPlayerCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->lastTJSErrorMsg_.Clear();
        if(numparams > 0 && param && param[0])
            self->lastExternalCall_ = ttstr(*param[0]);
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD getLastTJSError(tTJSVariant *result,
                                                     tjs_int, tTJSVariant **,
                                                     FlashPlayerCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(result)
            *result = self->lastTJSErrorMsg_;
        return TJS_S_OK;
    }

private:
    tTJSVariant owner_;
    tjs_int width_ = 0;
    tjs_int height_ = 0;
    tjs_int readyState_ = 0;
    tjs_int totalFrames_ = 0;
    bool playing_ = false;
    tjs_int quality_ = 0;
    tjs_int scaleMode_ = 0;
    tjs_int alignMode_ = 0;
    tjs_int backgroundColor_ = 0;
    bool loop_ = true;
    ttstr movie_;
    tjs_int frameNum_ = 0;
    tjs_int percentLoaded_ = 0;
    ttstr sAlign_;
    bool menu_ = false;
    ttstr base_;
    ttstr scale_;
    bool deviceFont_ = false;
    bool embedMovie_ = false;
    ttstr bgColor_;
    ttstr quality2_;
    ttstr swRemote_;
    ttstr flashVars_;
    ttstr allowScriptAccess_;
    tTJSVariant movieData_;
    bool seamlessTabbing_ = false;
    bool profile_ = false;
    ttstr profileAddress_;
    tjs_int profilePort_ = 0;
    ttstr lastTJSErrorMsg_;
    ttstr lastExternalCall_;
};

NCB_REGISTER_CLASS_DIFFER(FlashPlayer, FlashPlayerCompat) {
    Factory(&FlashPlayerCompat::factory);
    NCB_METHOD(clearMovie);
    NCB_METHOD(initMovie);
    NCB_METHOD(setSize);
    NCB_METHOD(hitTest);
    RawCallback(TJS_W("draw"), &Class::draw, 0);
    NCB_METHOD(doKeyDown);
    NCB_METHOD(doKeyUp);
    NCB_METHOD(doMouseEnter);
    NCB_METHOD(doMouseLeave);
    NCB_METHOD(doMouseDown);
    NCB_METHOD(doMouseMove);
    NCB_METHOD(doMouseUp);
    NCB_METHOD(doMouseWheel);
    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(totalFrames, getTotalFrames);
    NCB_PROPERTY(playing, getPlaying, setPlaying);
    NCB_PROPERTY(quality, getQuality, setQuality);
    NCB_PROPERTY(scaleMode, getScaleMode, setScaleMode);
    NCB_PROPERTY(alighMode, getAlignMode, setAlignMode);
    NCB_PROPERTY(backgroundColor, getBackgroundColor, setBackgroundColor);
    NCB_PROPERTY(loop, getLoop, setLoop);
    NCB_PROPERTY(movie, getMovie, setMovie);
    NCB_PROPERTY(frameNum, getFrameNum, setFrameNum);
    NCB_METHOD(setZoomRect);
    NCB_METHOD(zoom);
    NCB_METHOD(pan);
    NCB_METHOD(play);
    NCB_METHOD(stop);
    NCB_METHOD(back);
    NCB_METHOD(forward);
    NCB_METHOD(rewind);
    NCB_METHOD(stopPlay);
    NCB_METHOD(gotoFrame);
    NCB_PROPERTY_RO(currentFrame, getCurrentFrame);
    NCB_METHOD(isPlaying);
    NCB_PROPERTY_RO(percentLoaded, getPercentLoaded);
    NCB_METHOD(getFrameLoaded);
    NCB_PROPERTY_RO(flashVersion, getFlashVersion);
    NCB_PROPERTY(sAlign, getSAlign, setSAlign);
    NCB_PROPERTY(menu, getMenu, setMenu);
    NCB_PROPERTY(base, getBase, setBase);
    NCB_PROPERTY(scale, getScale, setScale);
    NCB_PROPERTY(deviceFont, getDeviceFont, setDeviceFont);
    NCB_PROPERTY(embedMovie, getEmbedMovie, setEmbedMovie);
    NCB_PROPERTY(bgColor, getBgColor, setBgColor);
    NCB_PROPERTY(quality2, getQuality2, setQuality2);
    NCB_METHOD(loadMovie);
    NCB_METHOD(tGotoFrame);
    NCB_METHOD(tGotoLabel);
    NCB_METHOD(tCurrentFrame);
    NCB_METHOD(tCurrentLabel);
    NCB_METHOD(tPlay);
    NCB_METHOD(tStopPlay);
    NCB_METHOD(setVariable);
    NCB_METHOD(getVariable);
    NCB_METHOD(tSetProperty);
    NCB_METHOD(tGetProperty);
    NCB_METHOD(tCallFrame);
    NCB_METHOD(tCallLabel);
    NCB_METHOD(tSetPropertyNum);
    NCB_METHOD(tGetPropertyNum);
    NCB_PROPERTY(swRemote, getSWRemote, setSWRemote);
    NCB_PROPERTY(flashVars, getFlashVars, setFlashVars);
    NCB_PROPERTY(allowScriptAccess, getAllowScriptAccess, setAllowScriptAccess);
    NCB_PROPERTY(movieData, getMovieData, setMovieData);
    NCB_PROPERTY(seamlessTabbing, getSeamlessTabbing, setSeamlessTabbing);
    NCB_METHOD(enforceLocalSecurity);
    NCB_PROPERTY(profile, getProfile, setProfile);
    NCB_PROPERTY(profileAddress, getProfileAddress, setProfileAddress);
    NCB_PROPERTY(profilePort, getProfilePort, setProfilePort);
    NCB_METHOD(disableLocalSecurity);
    RawCallback(TJS_W("callFunction"), &Class::callFunction, 0);
    RawCallback(TJS_W("getLastTJSError"), &Class::getLastTJSError, 0);
}

// ---------------------------------------------------------------------------
// gameswf.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gameswf.dll")

class SWFMovieCompat {
public:
    SWFMovieCompat() = default;

    void load(const tjs_char *storage) {
        storage_ = storage ? storage : TJS_W("");
        loaded_ = !storage_.IsEmpty();
        frame_ = 0;
    }

    bool update(tjs_int advance) {
        if(playing_)
            frame_ += advance;
        return loaded_ && playing_;
    }

    void notifyMouse(tjs_int, tjs_int, tjs_int) {}
    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void restart() {
        frame_ = 0;
        playing_ = true;
    }
    void back() {
        if(frame_ > 0)
            --frame_;
    }
    void next() { ++frame_; }
    void gotoFrame(tjs_int frame) { frame_ = frame; }

private:
    ttstr storage_;
    bool loaded_ = false;
    bool playing_ = false;
    tjs_int frame_ = 0;
};

NCB_REGISTER_CLASS_DIFFER(SWFMovie, SWFMovieCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(load);
    NCB_METHOD(update);
    NCB_METHOD(notifyMouse);
    NCB_METHOD(play);
    NCB_METHOD(stop);
    NCB_METHOD(restart);
    NCB_METHOD(back);
    NCB_METHOD(next);
    NCB_METHOD(gotoFrame);
}

class LayerExSWFCompat {
public:
    explicit LayerExSWFCompat(iTJSDispatch2 *) {}

    static tjs_error TJS_INTF_METHOD drawSWF(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnBoolCompat(result, false);
    }
};

NCB_GET_INSTANCE_HOOK(LayerExSWFCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExSWFCompat, Layer) {
    RawCallback(TJS_W("drawSWF"), &LayerExSWFCompat::drawSWF, 0);
}

// ---------------------------------------------------------------------------
// squirrel.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("squirrel.dll")

class ScriptsSquirrelCompat {
public:
    static tjs_error TJS_INTF_METHOD loadSQ(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = *param[0];
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD loadStorageSQ(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> bytes = ReadStorageBytes(ttstr(*param[0]));
        if(result)
            *result = Utf8BytesToTtstrCompat(bytes);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD execSQ(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *objthis) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        // A Squirrel VM is not linked on Android; keep probes non-fatal.
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD execStorageSQ(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        std::vector<tjs_uint8> bytes = ReadStorageBytes(ttstr(*param[0]));
        ttstr text = Utf8BytesToTtstrCompat(bytes);
        tTJSVariant script(text);
        tTJSVariant *args[] = { &script };
        return execSQ(result, 1, args, objthis);
    }

    static tjs_error TJS_INTF_METHOD callSQ(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **,
                                            iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD forkSQ(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **,
                                            iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = EmptyDictionaryCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD forkStorageSQ(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **,
                                                   iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = EmptyDictionaryCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD driveSQ(tTJSVariant *result, tjs_int,
                                             tTJSVariant **, iTJSDispatch2 *) {
        return ReturnIntCompat(result, 0);
    }

    static tjs_error TJS_INTF_METHOD triggerSQ(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **,
                                               iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD saveSQ(tTJSVariant *result,
                                            tjs_int numparams,
                                            tTJSVariant **param,
                                            iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        ttstr text = ttstr(*param[1]);
        std::string bytes = text.AsNarrowStdString();
        WriteStorageBytes(ttstr(*param[0]),
                          std::vector<tjs_uint8>(bytes.begin(), bytes.end()));
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD toSQString(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = ttstr(*param[0]);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD registerSQ(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **,
                                                iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD compileSQ(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **,
                                               iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD getThreadCountSQ(tTJSVariant *result,
                                                      tjs_int, tTJSVariant **,
                                                      iTJSDispatch2 *) {
        return ReturnIntCompat(result, 0);
    }

    static tjs_error TJS_INTF_METHOD compareSQ(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        ttstr lhs(*param[0]);
        ttstr rhs(*param[1]);
        return ReturnIntCompat(result, lhs == rhs ? 0 : (lhs < rhs ? -1 : 1));
    }
};

NCB_ATTACH_CLASS(ScriptsSquirrelCompat, Scripts) {
    RawCallback(TJS_W("loadSQ"), &ScriptsSquirrelCompat::loadSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("loadStorageSQ"), &ScriptsSquirrelCompat::loadStorageSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("execSQ"), &ScriptsSquirrelCompat::execSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("execStorageSQ"), &ScriptsSquirrelCompat::execStorageSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("forkSQ"), &ScriptsSquirrelCompat::forkSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("forkStorageSQ"), &ScriptsSquirrelCompat::forkStorageSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("driveSQ"), &ScriptsSquirrelCompat::driveSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("triggerSQ"), &ScriptsSquirrelCompat::triggerSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("callSQ"), &ScriptsSquirrelCompat::callSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("saveSQ"), &ScriptsSquirrelCompat::saveSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("toSQString"), &ScriptsSquirrelCompat::toSQString,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("registerSQ"), &ScriptsSquirrelCompat::registerSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("unregisterSQ"), &ScriptsSquirrelCompat::registerSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("compileSQ"), &ScriptsSquirrelCompat::compileSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("compileStorageSQ"), &ScriptsSquirrelCompat::compileSQ,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getThreadCountSQ"),
                &ScriptsSquirrelCompat::getThreadCountSQ, TJS_STATICMEMBER);
    RawCallback(TJS_W("threadCountSQ"),
                &ScriptsSquirrelCompat::getThreadCountSQ, (int)0,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("compareSQ"), &ScriptsSquirrelCompat::compareSQ,
                TJS_STATICMEMBER);
}

class SQContinuousCompat {
public:
    explicit SQContinuousCompat(const tjs_char *) {}
    void start() { running_ = true; }
    void stop() { running_ = false; }

private:
    bool running_ = false;
};

class SQFunctionCompat {
public:
    explicit SQFunctionCompat(const tjs_char *) {}
    static tjs_error TJS_INTF_METHOD call(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          SQFunctionCompat *) {
        return ReturnVoidCompat(result);
    }
};

NCB_REGISTER_CLASS_DIFFER(SQContinuous, SQContinuousCompat) {
    NCB_CONSTRUCTOR((const tjs_char *));
    NCB_METHOD(start);
    NCB_METHOD(stop);
}

NCB_REGISTER_CLASS_DIFFER(SQFunction, SQFunctionCompat) {
    NCB_CONSTRUCTOR((const tjs_char *));
    RawCallback(TJS_W("call"), &Class::call, 0);
}

// ---------------------------------------------------------------------------
// xmlhttprequest.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("xmlhttprequest.dll")

class XMLHttpRequestCompat {
public:
    enum { UNSENT = 0, OPENED = 1, HEADERS_RECEIVED = 2, LOADING = 3, DONE = 4 };

    static tjs_error factory(XMLHttpRequestCompat **result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = new XMLHttpRequestCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          XMLHttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 2 || !param || !param[0] || !param[1])
            return TJS_E_BADPARAMCOUNT;
        self->method_ = ttstr(*param[0]);
        self->url_ = ttstr(*param[1]);
        self->readyState_ = OPENED;
        self->status_ = 0;
        self->statusText_.Clear();
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD send(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          XMLHttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        std::vector<tjs_uint8> body;
        if(numparams > 0 && param && param[0]) {
            std::string bytes = VariantToBytes(param[0]);
            body.assign(bytes.begin(), bytes.end());
        }
        self->readyState_ = LOADING;
        self->executeCallback();
        HttpCompatResponse response;
        ttstr error;
        bool ok = HttpPerformCompat(self->method_, self->url_, self->headers_,
                                    body, &response, &error);
        self->readyState_ = DONE;
        self->status_ = response.status;
        self->statusText_ = ok ? response.statusText : error;
        self->responseHeaders_ = response.headers;
        self->response_ = response.body;
        self->responseText_ = Utf8BytesToTtstrCompat(self->response_);
        self->executeCallback();
        return ReturnVoidCompat(result);
    }

    void setRequestHeader(ttstr name, ttstr value) { headers_[name] = value; }
    void printRequestHeaders() {}
    ttstr getResponseHeader(ttstr name) {
        return HttpHeaderValueCompat(responseHeaders_, name);
    }
    void abort() {
        readyState_ = DONE;
        status_ = -1;
        statusText_ = TJS_W("cancelled");
    }
    void executeCallback() {
        if(onReadyStateChange_.Type() != tvtObject)
            return;
        tTJSVariantClosure callback =
            onReadyStateChange_.AsObjectClosureNoAddRef();
        callback.FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, nullptr);
    }

    tjs_int getReadyState() const { return readyState_; }
    tTJSVariant getResponseText() const { return responseText_; }
    tjs_int getStatus() const { return status_; }
    ttstr getStatusText() const { return statusText_; }
    tTJSVariant getOnReadyStateChange() const { return onReadyStateChange_; }
    void setOnReadyStateChange(tTJSVariant value) { onReadyStateChange_ = value; }

private:
    ttstr method_;
    ttstr url_;
    std::map<ttstr, ttstr> headers_;
    std::map<ttstr, ttstr> responseHeaders_;
    std::vector<tjs_uint8> response_;
    ttstr responseText_;
    tjs_int readyState_ = UNSENT;
    tjs_int status_ = 0;
    ttstr statusText_;
    tTJSVariant onReadyStateChange_;
};

NCB_REGISTER_CLASS_DIFFER(XMLHttpRequest, XMLHttpRequestCompat) {
    Factory(&XMLHttpRequestCompat::factory);
    RawCallback(TJS_W("open"), &Class::open, 0);
    RawCallback(TJS_W("send"), &Class::send, 0);
    NCB_METHOD(setRequestHeader);
    NCB_METHOD(printRequestHeaders);
    NCB_METHOD(getResponseHeader);
    NCB_METHOD(abort);
    NCB_METHOD(executeCallback);
    NCB_PROPERTY_RO(readyState, getReadyState);
    NCB_PROPERTY_RO(responseText, getResponseText);
    NCB_PROPERTY_RO(status, getStatus);
    NCB_PROPERTY_RO(statusText, getStatusText);
    NCB_PROPERTY(onreadystatechange, getOnReadyStateChange,
                 setOnReadyStateChange);
    Variant(TJS_W("UNSENT"), (tjs_int)XMLHttpRequestCompat::UNSENT);
    Variant(TJS_W("OPENED"), (tjs_int)XMLHttpRequestCompat::OPENED);
    Variant(TJS_W("HEADERS_RECEIVED"),
            (tjs_int)XMLHttpRequestCompat::HEADERS_RECEIVED);
    Variant(TJS_W("LOADING"), (tjs_int)XMLHttpRequestCompat::LOADING);
    Variant(TJS_W("DONE"), (tjs_int)XMLHttpRequestCompat::DONE);
}

// ---------------------------------------------------------------------------
// clipboardEx.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("clipboardEx.dll")

class ClipboardExCompat {
public:
    static tjs_error TJS_INTF_METHOD hasFormat(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        tjs_int format = static_cast<tjs_int>(param[0]->AsInteger());
        bool has = format == 1 && TVPClipboardHasFormat(cbfText);
        return ReturnBoolCompat(result, has);
    }

    static tjs_error TJS_INTF_METHOD getAsTJS(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        if(result)
            *result = asTJS_;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD setAsTJS(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
        if(numparams > 0 && param && param[0])
            asTJS_ = *param[0];
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnBoolCompat(result, false);
    }

    static tjs_error TJS_INTF_METHOD voidRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnVoidCompat(result);
    }

private:
    static tTJSVariant asTJS_;
};

tTJSVariant ClipboardExCompat::asTJS_;

static void clipboardExConstantsCompat() {
    TVPExecuteScript(TJS_W(
        "if (typeof global.cbfText == \"undefined\") global.cbfText = 1;"
        "if (typeof global.cbfBitmap == \"undefined\") global.cbfBitmap = 2;"
        "if (typeof global.cbfTJS == \"undefined\") global.cbfTJS = 3;"));
}

NCB_PRE_REGIST_CALLBACK(clipboardExConstantsCompat);

NCB_ATTACH_CLASS(ClipboardExCompat, Clipboard) {
    RawCallback(TJS_W("hasFormat"), &ClipboardExCompat::hasFormat,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("asTJS"), &ClipboardExCompat::getAsTJS,
                &ClipboardExCompat::setAsTJS, TJS_STATICMEMBER);
    RawCallback(TJS_W("setAsBitmap"), &ClipboardExCompat::voidRaw,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getAsBitmap"), &ClipboardExCompat::falseRaw,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("setMultipleData"), &ClipboardExCompat::voidRaw,
                TJS_STATICMEMBER);
}

class ClipboardWindowCompat {
public:
    explicit ClipboardWindowCompat(iTJSDispatch2 *) {}
    bool getClipboardWatchEnabled() const { return clipboardWatchEnabled_; }
    void setClipboardWatchEnabled(bool value) { clipboardWatchEnabled_ = value; }
    void onDrawClipboard() {}

private:
    bool clipboardWatchEnabled_ = false;
};

NCB_GET_INSTANCE_HOOK(ClipboardWindowCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(ClipboardWindowCompat, Window) {
    NCB_PROPERTY(clipboardWatchEnabled, getClipboardWatchEnabled,
                 setClipboardWatchEnabled);
    NCB_METHOD(onDrawClipboard);
}

// ---------------------------------------------------------------------------
// imagesaver.dll / adjustMonitor.dll / htmlhelp.dll / videoEncoder.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("imagesaver.dll")

static tjs_error TJS_INTF_METHOD saveLayerImageCompat(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *) {
    if(numparams < 3 || !param || !param[0] || !param[1] || !param[2])
        return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2 *layer = param[0]->AsObjectNoAddRef();
    if(!layer)
        return TJS_E_INVALIDPARAM;
    tTJSVariant argsValue[2] = { *param[1], *param[2] };
    tTJSVariant *args[2] = { &argsValue[0], &argsValue[1] };
    tjs_error hr = layer->FuncCall(0, TJS_W("saveLayerImage"), nullptr,
                                   result, 2, args, layer);
    if(TJS_FAILED(hr))
        return ReturnVoidCompat(result);
    return hr;
}

NCB_REGISTER_FUNCTION(saveLayerImage, saveLayerImageCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("adjustMonitor.dll")

static tjs_error TJS_INTF_METHOD AdjustMoniCompat(
    tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
    iTJSDispatch2 *) {
    if(numparams < 1 || !param || !param[0])
        return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2 *input = param[0]->AsObjectNoAddRef();
    if(!input) {
        if(result)
            *result = EmptyDictionaryCompat();
        return TJS_S_OK;
    }

    bool hasSecond = HasObjectValue(input, TJS_W("left2")) ||
                     HasObjectValue(input, TJS_W("top2")) ||
                     HasObjectValue(input, TJS_W("width2")) ||
                     HasObjectValue(input, TJS_W("height2"));
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return ReturnVoidCompat(result);

    if(hasSecond) {
        tjs_int left = GetObjectInt(input, TJS_W("left2"), 0);
        tjs_int top = GetObjectInt(input, TJS_W("top2"), 0);
        SetObjectInt(dict, TJS_W("x"), left);
        SetObjectInt(dict, TJS_W("y"), top);
    } else {
        tjs_int left = GetObjectInt(input, TJS_W("left"), 0);
        tjs_int top = GetObjectInt(input, TJS_W("top"), 0);
        tjs_int right = GetObjectInt(input, TJS_W("right"),
                                     left + GetObjectInt(input, TJS_W("width"), 0));
        tjs_int bottom = GetObjectInt(input, TJS_W("bottom"),
                                      top + GetObjectInt(input, TJS_W("height"), 0));
        SetObjectInt(dict, TJS_W("left"), left);
        SetObjectInt(dict, TJS_W("top"), top);
        SetObjectInt(dict, TJS_W("right"), right);
        SetObjectInt(dict, TJS_W("bottom"), bottom);
    }

    if(result)
        *result = tTJSVariant(dict, dict);
    dict->Release();
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(AdjustMoni, AdjustMoniCompat);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("htmlhelp.dll")

class HtmlHelpCompat {
public:
    HtmlHelpCompat() = default;
    bool displayTopic(ttstr) { return false; }
};

NCB_REGISTER_CLASS_DIFFER(HtmlHelp, HtmlHelpCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(displayTopic);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("videoEncoder.dll")

class VideoEncoderCompat {
public:
    static tjs_error factory(VideoEncoderCompat **result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = new VideoEncoderCompat();
        return TJS_S_OK;
    }

    bool open(ttstr filename) {
        filename_ = filename;
        opened_ = true;
        return false;
    }
    void close() { opened_ = false; }
    static tjs_error TJS_INTF_METHOD encodeVideoSample(tTJSVariant *result,
                                                       tjs_int numparams,
                                                       tTJSVariant **,
                                                       VideoEncoderCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnBoolCompat(result, false);
    }

    tjs_int getVideoQuality() const { return videoQuality_; }
    void setVideoQuality(tjs_int value) { videoQuality_ = value; }
    tjs_int getSecondPerKey() const { return secondPerKey_; }
    void setSecondPerKey(tjs_int value) { secondPerKey_ = value; }
    tjs_int getVideoTimeScale() const { return videoTimeScale_; }
    void setVideoTimeScale(tjs_int value) { videoTimeScale_ = value; }
    tjs_int getVideoTimeRate() const { return videoTimeRate_; }
    void setVideoTimeRate(tjs_int value) { videoTimeRate_ = value; }
    tjs_int getVideoWidth() const { return videoWidth_; }
    void setVideoWidth(tjs_int value) { videoWidth_ = value; }
    tjs_int getVideoHeight() const { return videoHeight_; }
    void setVideoHeight(tjs_int value) { videoHeight_ = value; }

private:
    ttstr filename_;
    bool opened_ = false;
    tjs_int videoQuality_ = 50;
    tjs_int secondPerKey_ = 5;
    tjs_int videoTimeScale_ = 1;
    tjs_int videoTimeRate_ = 30;
    tjs_int videoWidth_ = 640;
    tjs_int videoHeight_ = 480;
};

NCB_REGISTER_CLASS_DIFFER(videoEncoder, VideoEncoderCompat) {
    Factory(&VideoEncoderCompat::factory);
    NCB_METHOD(open);
    NCB_METHOD(close);
    RawCallback(TJS_W("encodeVideoSample"), &Class::encodeVideoSample, 0);
    NCB_PROPERTY(videoQuality, getVideoQuality, setVideoQuality);
    NCB_PROPERTY(secondPerKey, getSecondPerKey, setSecondPerKey);
    NCB_PROPERTY(videoTimeScale, getVideoTimeScale, setVideoTimeScale);
    NCB_PROPERTY(videoTimeRate, getVideoTimeRate, setVideoTimeRate);
    NCB_PROPERTY(videoWidth, getVideoWidth, setVideoWidth);
    NCB_PROPERTY(videoHeight, getVideoHeight, setVideoHeight);
}

// ---------------------------------------------------------------------------
// win32ole.dll / oleclass.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")

class OleObjectCompat {
public:
    static tjs_error factory(OleObjectCompat **result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = new OleObjectCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD invoke(tTJSVariant *result, tjs_int,
                                            tTJSVariant **,
                                            OleObjectCompat *) {
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD set(tTJSVariant *result,
                                         tjs_int numparams, tTJSVariant **,
                                         OleObjectCompat *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD get(tTJSVariant *result,
                                         tjs_int numparams, tTJSVariant **,
                                         OleObjectCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD addEvent(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **,
                                              OleObjectCompat *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD getConstant(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **,
                                                 OleObjectCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnIntCompat(result, 0);
    }
};

class ActiveXCompat : public OleObjectCompat {
public:
    static tjs_error factory(ActiveXCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        auto *obj = new ActiveXCompat();
        if(numparams > 1 && param && param[1])
            obj->left_ = static_cast<tjs_int>(param[1]->AsInteger());
        if(numparams > 2 && param && param[2])
            obj->top_ = static_cast<tjs_int>(param[2]->AsInteger());
        if(numparams > 3 && param && param[3])
            obj->width_ = static_cast<tjs_int>(param[3]->AsInteger());
        if(numparams > 4 && param && param[4])
            obj->height_ = static_cast<tjs_int>(param[4]->AsInteger());
        *result = obj;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD invoke(tTJSVariant *result, tjs_int,
                                            tTJSVariant **, ActiveXCompat *) {
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD set(tTJSVariant *result,
                                         tjs_int numparams, tTJSVariant **,
                                         ActiveXCompat *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD get(tTJSVariant *result,
                                         tjs_int numparams, tTJSVariant **,
                                         ActiveXCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD addEvent(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **,
                                              ActiveXCompat *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnVoidCompat(result);
    }

    static tjs_error TJS_INTF_METHOD getConstant(tTJSVariant *result,
                                                 tjs_int numparams,
                                                 tTJSVariant **,
                                                 ActiveXCompat *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnIntCompat(result, 0);
    }

    void setExternalUI() {}
    void setPos(tjs_int left, tjs_int top) {
        left_ = left;
        top_ = top;
    }
    void setSize(tjs_int width, tjs_int height) {
        width_ = width;
        height_ = height;
    }
    bool getIsValidWindow() const { return false; }
    bool getVisible() const { return visible_; }
    void setVisible(bool value) { visible_ = value; }
    tjs_int getLeft() const { return left_; }
    void setLeft(tjs_int value) { left_ = value; }
    tjs_int getTop() const { return top_; }
    void setTop(tjs_int value) { top_ = value; }
    tjs_int getWidth() const { return width_; }
    void setWidth(tjs_int value) { width_ = value; }
    tjs_int getHeight() const { return height_; }
    void setHeight(tjs_int value) { height_ = value; }

private:
    bool visible_ = false;
    tjs_int left_ = 0;
    tjs_int top_ = 0;
    tjs_int width_ = 100;
    tjs_int height_ = 100;
};

class ScriptsOleCompat {
public:
    static tjs_error TJS_INTF_METHOD createOleClass(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = ClassObjectCompat(TJS_W("WIN32OLE"));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD createActiveXClass(tTJSVariant *result,
                                                        tjs_int numparams,
                                                        tTJSVariant **,
                                                        iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = ClassObjectCompat(TJS_W("ActiveX"));
        return TJS_S_OK;
    }
};

NCB_REGISTER_CLASS_DIFFER(WIN32OLE, OleObjectCompat) {
    Factory(&OleObjectCompat::factory);
    RawCallback(TJS_W("invoke"), &Class::invoke, 0);
    RawCallback(TJS_W("set"), &Class::set, 0);
    RawCallback(TJS_W("get"), &Class::get, 0);
    RawCallback(TJS_W("missing"), &Class::invoke, 0);
    RawCallback(TJS_W("addEvent"), &Class::addEvent, 0);
    RawCallback(TJS_W("getConstant"), &Class::getConstant, 0);
}

NCB_REGISTER_CLASS_DIFFER(ActiveX, ActiveXCompat) {
    Factory(&ActiveXCompat::factory);
    RawCallback(TJS_W("invoke"), &Class::invoke, 0);
    RawCallback(TJS_W("set"), &Class::set, 0);
    RawCallback(TJS_W("get"), &Class::get, 0);
    RawCallback(TJS_W("missing"), &Class::invoke, 0);
    RawCallback(TJS_W("addEvent"), &Class::addEvent, 0);
    RawCallback(TJS_W("getConstant"), &Class::getConstant, 0);
    NCB_METHOD(setExternalUI);
    NCB_METHOD(setPos);
    NCB_METHOD(setSize);
    NCB_PROPERTY_RO(isValidWindow, getIsValidWindow);
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(left, getLeft, setLeft);
    NCB_PROPERTY(top, getTop, setTop);
    NCB_PROPERTY(width, getWidth, setWidth);
    NCB_PROPERTY(height, getHeight, setHeight);
}

NCB_ATTACH_CLASS(ScriptsOleCompat, Scripts) {
    RawCallback(TJS_W("createOleClass"), &ScriptsOleCompat::createOleClass,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("createActiveXClass"),
                &ScriptsOleCompat::createActiveXClass, TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// parser.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("parser.dll")

class ParserCompat {
public:
    static tjs_error factory(ParserCompat **result, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *) {
        if(result)
            *result = new ParserCompat();
        return TJS_S_OK;
    }

    void load(ttstr storage) {
        script_ = Utf8BytesToTtstrCompat(ReadStorageBytes(storage));
        pointer_ = 0;
    }

    tTJSVariant getNext() {
        if(pointer_ >= script_.GetLen())
            return tTJSVariant();

        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return tTJSVariant();

        ttstr ch(script_.c_str()[pointer_++]);
        SetObjectValue(dict, TJS_W("ch"), tTJSVariant(ch));
        tTJSVariant result(dict, dict);
        dict->Release();
        return result;
    }

private:
    ttstr script_;
    tjs_int pointer_ = 0;
};

NCB_REGISTER_CLASS_DIFFER(Parser, ParserCompat) {
    Factory(&ParserCompat::factory);
    NCB_METHOD(load);
    NCB_METHOD(getNext);
}

// ---------------------------------------------------------------------------
// magickpp.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("magickpp.dll")

class MagickGeometryCompat {
public:
    static tjs_error factory(MagickGeometryCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        auto *obj = new MagickGeometryCompat();
        if(numparams > 0 && param && param[0])
            obj->string_ = ttstr(*param[0]);
        *result = obj;
        return TJS_S_OK;
    }
    ttstr getString() const { return string_; }
    void setString(ttstr value) { string_ = value; }

private:
    ttstr string_;
};

class MagickColorCompat {
public:
    static tjs_error factory(MagickColorCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        auto *obj = new MagickColorCompat();
        if(numparams > 0 && param && param[0])
            obj->string_ = ttstr(*param[0]);
        *result = obj;
        return TJS_S_OK;
    }
    ttstr getString() const { return string_; }
    void setString(ttstr value) { string_ = value; }

private:
    ttstr string_;
};

class MagickCoderInfoCompat {
public:
    static tjs_error factory(MagickCoderInfoCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        auto *obj = new MagickCoderInfoCompat();
        if(numparams > 0 && param && param[0])
            obj->name_ = ttstr(*param[0]);
        *result = obj;
        return TJS_S_OK;
    }
    ttstr getName() const { return name_; }
    bool getIsReadable() const { return false; }
    bool getIsWritable() const { return false; }
    bool getIsMultiFrame() const { return false; }

private:
    ttstr name_;
};

class MagickImageCompat {
public:
    static tjs_error factory(MagickImageCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        auto *obj = new MagickImageCompat();
        if(numparams > 0 && param && param[0])
            obj->source_ = ttstr(*param[0]);
        *result = obj;
        return TJS_S_OK;
    }

    void read(ttstr storage) { source_ = storage; }
    bool write(ttstr) { return false; }
    void display(tTJSVariant = tTJSVariant()) {}
    tjs_int getColumns() const { return 0; }
    tjs_int getRows() const { return 0; }
    ttstr getMagick() const { return ttstr(); }
    void setMagick(ttstr) {}
    ttstr getFileName() const { return source_; }
    void setFileName(ttstr value) { source_ = value; }

private:
    ttstr source_;
};

class MagickPPCompat {
public:
    static tjs_error TJS_INTF_METHOD getVersion(tTJSVariant *result, tjs_int,
                                                tTJSVariant **,
                                                iTJSDispatch2 *) {
        if(result)
            *result = TJS_W("ImageMagick unavailable");
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getSupports(tTJSVariant *result, tjs_int,
                                                 tTJSVariant **,
                                                 iTJSDispatch2 *) {
        if(result)
            *result = EmptyArrayCompat();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD readImages(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **,
                                                iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = EmptyArrayCompat();
        return TJS_S_OK;
    }
};

NCB_REGISTER_SUBCLASS(MagickGeometryCompat) {
    Factory(&MagickGeometryCompat::factory);
    NCB_PROPERTY(string, getString, setString);
}

NCB_REGISTER_SUBCLASS(MagickColorCompat) {
    Factory(&MagickColorCompat::factory);
    NCB_PROPERTY(string, getString, setString);
}

NCB_REGISTER_SUBCLASS(MagickCoderInfoCompat) {
    Factory(&MagickCoderInfoCompat::factory);
    NCB_PROPERTY_RO(name, getName);
    NCB_PROPERTY_RO(isReadable, getIsReadable);
    NCB_PROPERTY_RO(isWritable, getIsWritable);
    NCB_PROPERTY_RO(isMultiFrame, getIsMultiFrame);
}

NCB_REGISTER_SUBCLASS(MagickImageCompat) {
    Factory(&MagickImageCompat::factory);
    NCB_METHOD(read);
    NCB_METHOD(write);
    NCB_METHOD(display);
    NCB_PROPERTY_RO(columns, getColumns);
    NCB_PROPERTY_RO(rows, getRows);
    NCB_PROPERTY(magick, getMagick, setMagick);
    NCB_PROPERTY(fileName, getFileName, setFileName);
}

NCB_REGISTER_CLASS_DIFFER(MagickPP, MagickPPCompat) {
    NCB_SUBCLASS(Geometry, MagickGeometryCompat);
    NCB_SUBCLASS(Color, MagickColorCompat);
    NCB_SUBCLASS(CoderInfo, MagickCoderInfoCompat);
    NCB_SUBCLASS(Image, MagickImageCompat);
    RawCallback(TJS_W("version"), &MagickPPCompat::getVersion, (int)0,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("supports"), &MagickPPCompat::getSupports, (int)0,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("readImages"), &MagickPPCompat::readImages,
                TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// Draw-device and low-value Win32 plugin compatibility.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceZ_D3D9.dll")

class DrawDeviceZCompat {
public:
    DrawDeviceZCompat() = default;
    tTJSVariant getInterface() const { return tTJSVariant(); }
    void recreate() {}
};

NCB_REGISTER_CLASS_DIFFER(DrawDeviceZ, DrawDeviceZCompat) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(interface, getInterface);
    NCB_METHOD(recreate);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceOgre.dll")

class OgreDrawDeviceCompat {
public:
    OgreDrawDeviceCompat() = default;
    tTJSVariant getInterface() const { return tTJSVariant(); }
};

NCB_REGISTER_CLASS_DIFFER(OgreDrawDevice, OgreDrawDeviceCompat) {
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY_RO(interface, getInterface);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("drawdeviceIrrlicht.dll")

class IrrlichtCompat {
public:
    IrrlichtCompat() = default;
};

NCB_REGISTER_CLASS_DIFFER(Irrlicht, IrrlichtCompat) {
    NCB_CONSTRUCTOR(());
}

static void layerExDrawAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("layerExDraw.dll"));
}

static void layerExPerspectiveAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("perspective.dll"));
}

static void libpsdAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("psdfile.dll"));
}

static void wmrdumpAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("msgreceiver.dll"));
}

static void adjustMoniAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("adjustMonitor.dll"));
}

static void krrlichtAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("drawdeviceIrrlicht.dll"));
}

static void drawdeviceZAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("drawdeviceZ_D3D9.dll"));
}

static void gdiplusAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("layerExDraw.dll"));
    TVPExecuteScript(TJS_W(
        "if(typeof Layer != \"undefined\" && typeof GdiPlus != \"undefined\" && "
        "Layer.__krkr2LayerExGdiPlusCompatInstalled === void) {"
        "  Layer.__krkr2LayerExGdiPlusCompatInstalled = true;"
        "  class __Krkr2LayerGdiBrush {"
        "    var color = 0xffffffff;"
        "    var __krkr2LayerGdiBrush = true;"
        "    function __Krkr2LayerGdiBrush(argb=0xffffffff) { color = argb; }"
        "    function setSolidBrush(argb) { color = argb; }"
        "  };"
        "  class __Krkr2LayerGdiPen {"
        "    var color = 0xffffffff;"
        "    var width = 1.0;"
        "    var alignment = 0;"
        "    var brush = void;"
        "    var __krkr2LayerGdiPen = true;"
        "    function __Krkr2LayerGdiPen(arg=0xffffffff, w=1.0) {"
        "      try {"
        "        if(arg !== void && arg.__krkr2LayerGdiBrush !== void) {"
        "          brush = arg; color = arg.color;"
        "        } else {"
        "          color = arg;"
        "        }"
        "      } catch(e) { color = arg; }"
        "      width = w;"
        "    }"
        "    function setAlignMent(value) { alignment = value; }"
        "    function setAlignment(value) { alignment = value; }"
        "    function setBrush(value) {"
        "      brush = value;"
        "      try { if(value.color !== void) color = value.color; } catch(e) {}"
        "    }"
        "    function setColor(value) { color = value; brush = void; }"
        "    function setWidth(value) { width = value; }"
        "  };"
        "  Layer.Brush = __Krkr2LayerGdiBrush;"
        "  Layer.Pen = __Krkr2LayerGdiPen;"
        "  if(Layer.Font === void) Layer.Font = GdiPlus.Font;"
        "  Layer.__krkr2GdiIsPen = function(v) {"
        "    if(v === void) return false;"
        "    try { return v.__krkr2LayerGdiPen !== void; } catch(e) { return false; }"
        "  };"
        "  Layer.__krkr2GdiIsBrush = function(v) {"
        "    if(v === void) return false;"
        "    try { return v.__krkr2LayerGdiBrush !== void; } catch(e) { return false; }"
        "  };"
        "  Layer.__krkr2GdiPenApp = function(pen) {"
        "    var app = new GdiPlus.Appearance();"
        "    var color = 0xffffffff; var width = 1.0;"
        "    try {"
        "      if(pen.brush !== void && pen.brush.color !== void) color = pen.brush.color;"
        "      else if(pen.color !== void) color = pen.color;"
        "      if(pen.width !== void) width = pen.width;"
        "    } catch(e) {}"
        "    app.addPen(color, width);"
        "    return app;"
        "  };"
        "  Layer.__krkr2GdiBrushApp = function(brush) {"
        "    var app = new GdiPlus.Appearance();"
        "    var color = 0xffffffff;"
        "    try { if(brush.color !== void) color = brush.color; } catch(e) {}"
        "    app.addBrush(color);"
        "    return app;"
        "  };"
        "  Layer.__krkr2GdiOrigDrawEllipse = Layer.drawEllipse;"
        "  Layer.__krkr2GdiOrigDrawLine = Layer.drawLine;"
        "  Layer.__krkr2GdiOrigDrawRectangle = Layer.drawRectangle;"
        "  Layer.__krkr2GdiOrigDrawBezier = Layer.drawBezier;"
        "  Layer.__krkr2GdiOrigDrawBeziers = Layer.drawBeziers;"
        "  Layer.__krkr2GdiOrigDrawString = Layer.drawString;"
        "  Layer.__krkr2GdiOrigDrawImage = Layer.drawImage;"
        "  Layer.__krkr2GdiOrigDrawImageRect = Layer.drawImageRect;"
        "  Layer.drawEllipse = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawEllipse === void) return void;"
        "    if(Layer.__krkr2GdiIsPen(a)) return "
        "(Layer.__krkr2GdiOrigDrawEllipse incontextof this)"
        "(Layer.__krkr2GdiPenApp(a), b, c, d, e);"
        "    if(Layer.__krkr2GdiIsPen(e)) return "
        "(Layer.__krkr2GdiOrigDrawEllipse incontextof this)"
        "(Layer.__krkr2GdiPenApp(e), a, b, c, d);"
        "    return (Layer.__krkr2GdiOrigDrawEllipse incontextof this)(*);"
        "  };"
        "  Layer.fillEllipse = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawEllipse === void) return void;"
        "    if(Layer.__krkr2GdiIsBrush(a)) return "
        "(Layer.__krkr2GdiOrigDrawEllipse incontextof this)"
        "(Layer.__krkr2GdiBrushApp(a), b, c, d, e);"
        "    if(Layer.__krkr2GdiIsBrush(e)) return "
        "(Layer.__krkr2GdiOrigDrawEllipse incontextof this)"
        "(Layer.__krkr2GdiBrushApp(e), a, b, c, d);"
        "    return void;"
        "  };"
        "  Layer.drawLine = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawLine === void) return void;"
        "    if(Layer.__krkr2GdiIsPen(a)) return "
        "(Layer.__krkr2GdiOrigDrawLine incontextof this)"
        "(Layer.__krkr2GdiPenApp(a), b, c, d, e);"
        "    if(Layer.__krkr2GdiIsPen(e)) return "
        "(Layer.__krkr2GdiOrigDrawLine incontextof this)"
        "(Layer.__krkr2GdiPenApp(e), a, b, c, d);"
        "    return (Layer.__krkr2GdiOrigDrawLine incontextof this)(*);"
        "  };"
        "  Layer.drawRectangle = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawRectangle === void) return void;"
        "    if(Layer.__krkr2GdiIsPen(a)) return "
        "(Layer.__krkr2GdiOrigDrawRectangle incontextof this)"
        "(Layer.__krkr2GdiPenApp(a), b, c, d, e);"
        "    if(Layer.__krkr2GdiIsPen(e)) return "
        "(Layer.__krkr2GdiOrigDrawRectangle incontextof this)"
        "(Layer.__krkr2GdiPenApp(e), a, b, c, d);"
        "    return (Layer.__krkr2GdiOrigDrawRectangle incontextof this)(*);"
        "  };"
        "  Layer.fillRectangle = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawRectangle === void) return void;"
        "    if(Layer.__krkr2GdiIsBrush(a)) return "
        "(Layer.__krkr2GdiOrigDrawRectangle incontextof this)"
        "(Layer.__krkr2GdiBrushApp(a), b, c, d, e);"
        "    if(Layer.__krkr2GdiIsBrush(e)) return "
        "(Layer.__krkr2GdiOrigDrawRectangle incontextof this)"
        "(Layer.__krkr2GdiBrushApp(e), a, b, c, d);"
        "    return void;"
        "  };"
        "  Layer.drawBezier = function(a,b,c,d,e,f,g,h,i) {"
        "    if(Layer.__krkr2GdiOrigDrawBezier === void) return void;"
        "    if(Layer.__krkr2GdiIsPen(a)) return "
        "(Layer.__krkr2GdiOrigDrawBezier incontextof this)"
        "(Layer.__krkr2GdiPenApp(a), b, c, d, e, f, g, h, i);"
        "    if(Layer.__krkr2GdiIsPen(i)) return "
        "(Layer.__krkr2GdiOrigDrawBezier incontextof this)"
        "(Layer.__krkr2GdiPenApp(i), a, b, c, d, e, f, g, h);"
        "    return (Layer.__krkr2GdiOrigDrawBezier incontextof this)(*);"
        "  };"
        "  Layer.drawBeziers = function(a,b) {"
        "    if(Layer.__krkr2GdiOrigDrawBeziers === void) return void;"
        "    if(Layer.__krkr2GdiIsPen(a)) return "
        "(Layer.__krkr2GdiOrigDrawBeziers incontextof this)"
        "(Layer.__krkr2GdiPenApp(a), b);"
        "    if(Layer.__krkr2GdiIsPen(b)) return "
        "(Layer.__krkr2GdiOrigDrawBeziers incontextof this)"
        "(Layer.__krkr2GdiPenApp(b), a);"
        "    return (Layer.__krkr2GdiOrigDrawBeziers incontextof this)(*);"
        "  };"
        "  Layer.drawString = function(a,b,c,d,e) {"
        "    if(Layer.__krkr2GdiOrigDrawString === void) return void;"
        "    if(Layer.__krkr2GdiIsBrush(e)) return "
        "(Layer.__krkr2GdiOrigDrawString incontextof this)"
        "(b, Layer.__krkr2GdiBrushApp(e), c, d, a);"
        "    return (Layer.__krkr2GdiOrigDrawString incontextof this)(*);"
        "  };"
        "  Layer.drawImage = function(a,b,c,d,e,f,g) {"
        "    if(typeof a == \"string\") {"
        "      if(c === void && Layer.__krkr2GdiOrigDrawImage !== void) return "
        "(Layer.__krkr2GdiOrigDrawImage incontextof this)(0, 0, a);"
        "      if(g === void && Layer.__krkr2GdiOrigDrawImage !== void) return "
        "(Layer.__krkr2GdiOrigDrawImage incontextof this)(b, c, a);"
        "      if(Layer.__krkr2GdiOrigDrawImageRect !== void) return "
        "(Layer.__krkr2GdiOrigDrawImageRect incontextof this)(b, c, a, d, e, f, g);"
        "    }"
        "    if(Layer.__krkr2GdiOrigDrawImage !== void) return "
        "(Layer.__krkr2GdiOrigDrawImage incontextof this)(*);"
        "    return void;"
        "  };"
        "}"));
}

static void oleclassAliasCompat() {
    ncbAutoRegister::LoadModule(TJS_W("win32ole.dll"));
}

#define REGISTER_EMPTY_COMPAT_PLUGIN(tag, name) \
    static ncbCallbackAutoRegister tag(         \
        TJS_W(name), ncbAutoRegister::PreRegist, EmptyPluginCompat, nullptr)
#define REGISTER_ALIAS_COMPAT_PLUGIN(tag, name, callback) \
    static ncbCallbackAutoRegister tag(                   \
        TJS_W(name), ncbAutoRegister::PreRegist, callback, nullptr)

REGISTER_ALIAS_COMPAT_PLUGIN(libpsd_dll_compat, "libpsd.dll",
                             libpsdAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(layerExAgg_dll_compat, "layerExAgg.dll",
                             layerExDrawAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(layerExCairo_dll_compat, "layerExCairo.dll",
                             layerExDrawAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(layerExGdiPlus_dll_compat, "layerExGdiPlus.dll",
                             gdiplusAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(layerExPerspective_dll_compat,
                             "layerExPerspective.dll",
                             layerExPerspectiveAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(wmrdump_dll_compat, "wmrdump.dll",
                             wmrdumpAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(adjustMoni_dll_compat, "AdjustMoni.dll",
                             adjustMoniAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(krrlicht_dll_compat, "krrlicht.dll",
                             krrlichtAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(drawdeviceZ_dll_compat, "drawdeviceZ.dll",
                             drawdeviceZAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(gdiplus_dll_compat, "gdiplus.dll",
                             gdiplusAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(oleclass_dll_compat, "oleclass.dll",
                             oleclassAliasCompat);

REGISTER_EMPTY_COMPAT_PLUGIN(layerExBase_dll_compat, "layerExBase.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(libjpeg_dll_compat, "libjpeg.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(basetest_dll_compat, "basetest.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(utf8hack_dll_compat, "utf8hack.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(k2utf8hack_dll_compat, "k2utf8hack.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(wumsadp_dll_compat, "wumsadp.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(exceptiontest_dll_compat, "exceptiontest.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(simplebinder_dll_compat, "simplebinder.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(xpressive_dll_compat, "xpressive.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(mkpj_dll_compat, "mkpj.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(zlib_dll_compat, "zlib.dll");

#undef REGISTER_ALIAS_COMPAT_PLUGIN
#undef REGISTER_EMPTY_COMPAT_PLUGIN
