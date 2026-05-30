#include "ncbind.hpp"
#include "ClipboardIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "md5.h"

#include <minizip/zip.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
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

    void setRequestHeader(ttstr, ttstr) {}

    static tjs_error TJS_INTF_METHOD send(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->completeUnavailable();
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
                                                 tTJSVariant **,
                                                 HttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        self->completeUnavailable();
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

    tTJSVariant getAllResponseHeaders() { return EmptyDictionaryCompat(); }
    ttstr getResponseHeader(ttstr) { return ttstr(TJS_W("")); }

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
    tTJSVariant getResponseData() { return EmptyOctetCompat(); }
    tjs_int getStatus() const { return status_; }
    ttstr getStatusText() const { return statusText_; }
    ttstr getContentType() const { return ttstr(TJS_W("")); }
    ttstr getContentTypeEncoding() const { return ttstr(TJS_W("")); }
    tjs_int64 getContentLength() const { return 0; }

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
    void completeUnavailable() {
        readyState_ = LOADED;
        status_ = 0;
        statusText_ = TJS_W("HTTP request is unavailable on this platform");
        responseText_.Clear();
    }

    ttstr method_;
    ttstr url_;
    ttstr responseText_;
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
        database_(database), readonly_(readonly) {}

    static tjs_error TJS_INTF_METHOD exec(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          SqliteCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->errorCode_ = SQLITE_OK;
        self->errorMessage_.Clear();
        return ReturnBoolCompat(result, true);
    }

    static tjs_error TJS_INTF_METHOD execValue(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               SqliteCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->errorCode_ = SQLITE_OK;
        self->errorMessage_.Clear();
        return ReturnVoidCompat(result);
    }

    bool begin() { return true; }
    bool commit() { return true; }
    bool rollback() { return true; }
    tjs_int64 getLastInsertRowId() const { return 0; }
    tjs_int getErrorCode() const { return errorCode_; }
    ttstr getErrorMessage() const { return errorMessage_; }
    ttstr getDatabase() const { return database_; }
    bool getReadOnly() const { return readonly_; }

    enum {
        SQLITE_OK = 0,
        SQLITE_ERROR = 1,
        SQLITE_INTERNAL = 2,
        SQLITE_PERM = 3,
        SQLITE_ABORT = 4,
        SQLITE_BUSY = 5,
        SQLITE_LOCKED = 6,
        SQLITE_NOMEM = 7,
        SQLITE_READONLY = 8,
        SQLITE_INTERRUPT = 9,
        SQLITE_IOERR = 10,
        SQLITE_CORRUPT = 11,
        SQLITE_NOTFOUND = 12,
        SQLITE_FULL = 13,
        SQLITE_CANTOPEN = 14,
        SQLITE_PROTOCOL = 15,
        SQLITE_EMPTY = 16,
        SQLITE_SCHEMA = 17,
        SQLITE_TOOBIG = 18,
        SQLITE_CONSTRAINT = 19,
        SQLITE_MISMATCH = 20,
        SQLITE_MISUSE = 21,
        SQLITE_NOLFS = 22,
        SQLITE_AUTH = 23,
        SQLITE_FORMAT = 24,
        SQLITE_RANGE = 25,
        SQLITE_NOTADB = 26,
        SQLITE_ROW = 100,
        SQLITE_DONE = 101
    };

private:
    ttstr database_;
    bool readonly_ = false;
    tjs_int errorCode_ = SQLITE_OK;
    ttstr errorMessage_;
};

class SqliteStatementCompat {
public:
    static tjs_error factory(SqliteStatementCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        std::unique_ptr<SqliteStatementCompat> self(
            new SqliteStatementCompat());
        if(numparams > 1 && param && param[1] && param[1]->Type() != tvtVoid)
            self->sql_ = ttstr(*param[1]);
        *result = self.release();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        self->sql_ = ttstr(*param[0]);
        return ReturnIntCompat(result, SqliteCompat::SQLITE_OK);
    }

    void close() { sql_.Clear(); }
    ttstr getSql() const { return sql_; }
    tjs_int reset() { return SqliteCompat::SQLITE_OK; }
    tjs_int bind(tTJSVariant = tTJSVariant()) { return SqliteCompat::SQLITE_OK; }

    static tjs_error TJS_INTF_METHOD bindAt(tTJSVariant *result, tjs_int,
                                            tTJSVariant **,
                                            SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        return ReturnIntCompat(result, SqliteCompat::SQLITE_OK);
    }

    tjs_int exec() { return SqliteCompat::SQLITE_OK; }
    bool step() { return false; }
    tjs_int getCount() const { return 0; }
    tjs_int getColumnCount() const { return 0; }
    bool isNull(tTJSVariant = tTJSVariant()) { return true; }
    tjs_int getType(tTJSVariant = tTJSVariant()) { return SQLITE_NULL; }
    ttstr getName(tTJSVariant = tTJSVariant()) { return ttstr(); }

    static tjs_error TJS_INTF_METHOD get(tTJSVariant *result,
                                         tjs_int numparams,
                                         tTJSVariant **param,
                                         SqliteStatementCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(result && numparams > 1 && param && param[1] &&
           param[1]->Type() != tvtVoid)
            *result = *param[1];
        else if(result)
            result->Clear();
        return TJS_S_OK;
    }

    enum {
        SQLITE_INTEGER = 1,
        SQLITE_FLOAT = 2,
        SQLITE_TEXT = 3,
        SQLITE_BLOB = 4,
        SQLITE_NULL = 5
    };

private:
    ttstr sql_;
};

class SqliteThreadCompat {
public:
    static tjs_error factory(SqliteThreadCompat **result, tjs_int,
                             tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = new SqliteThreadCompat();
        return TJS_S_OK;
    }

    bool select(ttstr, tTJSVariant = tTJSVariant()) {
        state_ = DONE;
        return true;
    }
    bool update(ttstr, tTJSVariant = tTJSVariant()) {
        state_ = DONE;
        return true;
    }
    void abort() { state_ = DONE; }
    tjs_int getState() const { return state_; }
    tjs_int getErrorCode() const { return SqliteCompat::SQLITE_OK; }
    tTJSVariant getSelectResult() const { return EmptyArrayCompat(); }
    tjs_int getProgressUpdateCount() const { return progressUpdateCount_; }
    void setProgressUpdateCount(tjs_int value) { progressUpdateCount_ = value; }
    void onStateChange(tjs_int) {}
    void onProgress(tjs_int) {}

    enum { INIT = 0, WORKING = 1, DONE = 2 };

private:
    tjs_int state_ = INIT;
    tjs_int progressUpdateCount_ = 100;
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
    Variant(TJS_W("SQLITE_OK"), (tjs_int)SqliteCompat::SQLITE_OK);
    Variant(TJS_W("SQLITE_ERROR"), (tjs_int)SqliteCompat::SQLITE_ERROR);
    Variant(TJS_W("SQLITE_INTERNAL"), (tjs_int)SqliteCompat::SQLITE_INTERNAL);
    Variant(TJS_W("SQLITE_PERM"), (tjs_int)SqliteCompat::SQLITE_PERM);
    Variant(TJS_W("SQLITE_ABORT"), (tjs_int)SqliteCompat::SQLITE_ABORT);
    Variant(TJS_W("SQLITE_BUSY"), (tjs_int)SqliteCompat::SQLITE_BUSY);
    Variant(TJS_W("SQLITE_LOCKED"), (tjs_int)SqliteCompat::SQLITE_LOCKED);
    Variant(TJS_W("SQLITE_NOMEM"), (tjs_int)SqliteCompat::SQLITE_NOMEM);
    Variant(TJS_W("SQLITE_READONLY"), (tjs_int)SqliteCompat::SQLITE_READONLY);
    Variant(TJS_W("SQLITE_INTERRUPT"), (tjs_int)SqliteCompat::SQLITE_INTERRUPT);
    Variant(TJS_W("SQLITE_IOERR"), (tjs_int)SqliteCompat::SQLITE_IOERR);
    Variant(TJS_W("SQLITE_CORRUPT"), (tjs_int)SqliteCompat::SQLITE_CORRUPT);
    Variant(TJS_W("SQLITE_NOTFOUND"), (tjs_int)SqliteCompat::SQLITE_NOTFOUND);
    Variant(TJS_W("SQLITE_FULL"), (tjs_int)SqliteCompat::SQLITE_FULL);
    Variant(TJS_W("SQLITE_CANTOPEN"), (tjs_int)SqliteCompat::SQLITE_CANTOPEN);
    Variant(TJS_W("SQLITE_PROTOCOL"), (tjs_int)SqliteCompat::SQLITE_PROTOCOL);
    Variant(TJS_W("SQLITE_EMPTY"), (tjs_int)SqliteCompat::SQLITE_EMPTY);
    Variant(TJS_W("SQLITE_SCHEMA"), (tjs_int)SqliteCompat::SQLITE_SCHEMA);
    Variant(TJS_W("SQLITE_TOOBIG"), (tjs_int)SqliteCompat::SQLITE_TOOBIG);
    Variant(TJS_W("SQLITE_CONSTRAINT"), (tjs_int)SqliteCompat::SQLITE_CONSTRAINT);
    Variant(TJS_W("SQLITE_MISMATCH"), (tjs_int)SqliteCompat::SQLITE_MISMATCH);
    Variant(TJS_W("SQLITE_MISUSE"), (tjs_int)SqliteCompat::SQLITE_MISUSE);
    Variant(TJS_W("SQLITE_NOLFS"), (tjs_int)SqliteCompat::SQLITE_NOLFS);
    Variant(TJS_W("SQLITE_AUTH"), (tjs_int)SqliteCompat::SQLITE_AUTH);
    Variant(TJS_W("SQLITE_FORMAT"), (tjs_int)SqliteCompat::SQLITE_FORMAT);
    Variant(TJS_W("SQLITE_RANGE"), (tjs_int)SqliteCompat::SQLITE_RANGE);
    Variant(TJS_W("SQLITE_NOTADB"), (tjs_int)SqliteCompat::SQLITE_NOTADB);
    Variant(TJS_W("SQLITE_ROW"), (tjs_int)SqliteCompat::SQLITE_ROW);
    Variant(TJS_W("SQLITE_DONE"), (tjs_int)SqliteCompat::SQLITE_DONE);
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
    Variant(TJS_W("SQLITE_INTEGER"), (tjs_int)SqliteStatementCompat::SQLITE_INTEGER);
    Variant(TJS_W("SQLITE_FLOAT"), (tjs_int)SqliteStatementCompat::SQLITE_FLOAT);
    Variant(TJS_W("SQLITE_TEXT"), (tjs_int)SqliteStatementCompat::SQLITE_TEXT);
    Variant(TJS_W("SQLITE_BLOB"), (tjs_int)SqliteStatementCompat::SQLITE_BLOB);
    Variant(TJS_W("SQLITE_NULL"), (tjs_int)SqliteStatementCompat::SQLITE_NULL);
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

    static tjs_error TJS_INTF_METHOD send(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          XMLHttpRequestCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        self->readyState_ = DONE;
        self->status_ = 0;
        self->statusText_ = TJS_W("XMLHttpRequest is unavailable on this platform");
        self->response_.clear();
        return ReturnVoidCompat(result);
    }

    void setRequestHeader(ttstr name, ttstr value) { headers_[name] = value; }
    void printRequestHeaders() {}
    ttstr getResponseHeader(ttstr) { return ttstr(); }
    void abort() {
        readyState_ = DONE;
        status_ = -1;
        statusText_ = TJS_W("cancelled");
    }
    void executeCallback() {}

    tjs_int getReadyState() const { return readyState_; }
    tTJSVariant getResponseText() const { return EmptyOctetCompat(); }
    tjs_int getStatus() const { return status_; }
    ttstr getStatusText() const { return statusText_; }
    tTJSVariant getOnReadyStateChange() const { return onReadyStateChange_; }
    void setOnReadyStateChange(tTJSVariant value) { onReadyStateChange_ = value; }

private:
    ttstr method_;
    ttstr url_;
    std::map<ttstr, ttstr> headers_;
    std::vector<tjs_uint8> response_;
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
                             layerExDrawAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(layerExPerspective_dll_compat,
                             "layerExPerspective.dll",
                             layerExPerspectiveAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(wmrdump_dll_compat, "wmrdump.dll",
                             wmrdumpAliasCompat);
REGISTER_ALIAS_COMPAT_PLUGIN(adjustMoni_dll_compat, "AdjustMoni.dll",
                             adjustMoniAliasCompat);

REGISTER_EMPTY_COMPAT_PLUGIN(layerExBase_dll_compat, "layerExBase.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(libjpeg_dll_compat, "libjpeg.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(basetest_dll_compat, "basetest.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(utf8hack_dll_compat, "utf8hack.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(k2utf8hack_dll_compat, "k2utf8hack.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(wumsadp_dll_compat, "wumsadp.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(exceptiontest_dll_compat, "exceptiontest.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(flashPlayer_dll_compat, "flashPlayer.dll");
REGISTER_EMPTY_COMPAT_PLUGIN(gameswf_dll_compat, "gameswf.dll");

#undef REGISTER_ALIAS_COMPAT_PLUGIN
#undef REGISTER_EMPTY_COMPAT_PLUGIN
