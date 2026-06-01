#include "ncbind.hpp"
#include "CharacterSet.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "Platform.h"
#include "PluginImpl.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

tTJSVariant EmptyArray() {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(!array)
        return tTJSVariant();
    tTJSVariant ret(array, array);
    array->Release();
    return ret;
}

tTJSVariant EmptyDictionary() {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();
    tTJSVariant ret(dict, dict);
    dict->Release();
    return ret;
}

tTJSVariant EmptyOctet() {
    static const tjs_uint8 empty = 0;
    return tTJSVariant(&empty, 0);
}

tjs_error ReturnBool(tTJSVariant *result, bool value) {
    if(result)
        *result = value ? 1 : 0;
    return TJS_S_OK;
}

tjs_error ReturnVoid(tTJSVariant *result) {
    if(result)
        result->Clear();
    return TJS_S_OK;
}

tjs_error ReturnInt(tTJSVariant *result, tjs_int64 value) {
    if(result)
        *result = static_cast<tTVInteger>(value);
    return TJS_S_OK;
}

void SetDictionaryValue(iTJSDispatch2 *dict, const tjs_char *name,
                        const tTJSVariant &value) {
    if(dict)
        dict->PropSet(TJS_MEMBERENSURE, name, nullptr,
                      const_cast<tTJSVariant *>(&value), dict);
}

std::string ToNarrow(const ttstr &text) { return text.AsNarrowStdString(); }

ttstr FromUtf8(const std::string &text) {
    if(text.empty())
        return ttstr(TJS_W(""));
    tjs_int len = TVPUtf8ToWideCharString(
        text.data(), static_cast<tjs_uint>(text.size()), nullptr);
    if(len <= 0)
        return ttstr(text.c_str());

    std::vector<tjs_char> buffer(static_cast<size_t>(len) + 1, 0);
    TVPUtf8ToWideCharString(text.data(), static_cast<tjs_uint>(text.size()),
                            buffer.data());
    return ttstr(buffer.data());
}

tTJSVariant ReadEnvironmentValue(const ttstr &name) {
    std::string narrow = ToNarrow(name);
    if(narrow.empty())
        return tTJSVariant();
    const char *value = std::getenv(narrow.c_str());
    return value ? tTJSVariant(ttstr(value)) : tTJSVariant();
}

ttstr ExpandEnvironmentString(const ttstr &text) {
    ttstr result;
    const tjs_char percent = static_cast<tjs_char>('%');
    const tjs_uint length = text.length();
    for(tjs_uint i = 0; i < length; ++i) {
        if(text[i] != percent) {
            result += text[i];
            continue;
        }

        tjs_uint end = i + 1;
        while(end < length && text[end] != percent)
            ++end;
        if(end >= length) {
            result += text[i];
            continue;
        }

        ttstr key = text.SubString(i + 1, end - i - 1);
        tTJSVariant value = ReadEnvironmentValue(key);
        if(value.Type() != tvtVoid)
            result += ttstr(value);
        i = end;
    }
    return result;
}

bool IsUrlSafe(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

int HexToInt(char c) {
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

ttstr UrlEncode(const ttstr &input) {
    static const char hex[] = "0123456789ABCDEF";
    std::string bytes = ToNarrow(input);
    std::string out;
    out.reserve(bytes.size() * 3);
    for(unsigned char c : bytes) {
        if(IsUrlSafe(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return ttstr(out.c_str());
}

ttstr UrlDecode(const ttstr &input, bool utf8) {
    std::string bytes = ToNarrow(input);
    std::string out;
    out.reserve(bytes.size());
    for(size_t i = 0; i < bytes.size(); ++i) {
        if(bytes[i] == '%' && i + 2 < bytes.size()) {
            int hi = HexToInt(bytes[i + 1]);
            int lo = HexToInt(bytes[i + 2]);
            if(hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(bytes[i] == '+' ? ' ' : bytes[i]);
    }
    return utf8 ? FromUtf8(out) : ttstr(out.c_str());
}

tTJSVariant MakeCommandExecuteResult(const tjs_char *status,
                                     const tjs_char *message) {
    iTJSDispatch2 *dict = TJSCreateDictionaryObject();
    if(!dict)
        return tTJSVariant();

    tTJSVariant statusValue(status ? status : TJS_W("error"));
    tTJSVariant stdoutValue = EmptyArray();
    tTJSVariant exitCodeValue((tjs_int)0);
    tTJSVariant messageValue(message ? message : TJS_W(""));
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("status"), nullptr, &statusValue,
                  dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("stdout"), nullptr, &stdoutValue,
                  dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("exitcode"), nullptr,
                  &exitCodeValue, dict);
    dict->PropSet(TJS_MEMBERENSURE, TJS_W("message"), nullptr, &messageValue,
                  dict);

    tTJSVariant ret(dict, dict);
    dict->Release();
    return ret;
}

} // namespace

// ---------------------------------------------------------------------------
// stdio.dll
// The original plugin is a Win32 console bridge. Android has no attachable
// console, but scripts commonly probe these functions during debug setup.
// ---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("stdio.dll")

class StdioCompat {
public:
    static int getState() { return 0x07; }

    static tjs_error TJS_INTF_METHOD attach(tTJSVariant *result, tjs_int,
                                            tTJSVariant **,
                                            iTJSDispatch2 *) {
        return ReturnBool(result, true);
    }

    static tjs_error TJS_INTF_METHOD alloc(tTJSVariant *result, tjs_int,
                                           tTJSVariant **,
                                           iTJSDispatch2 *) {
        return ReturnBool(result, true);
    }

    static tjs_error TJS_INTF_METHOD free(tTJSVariant *result, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *) {
        return ReturnBool(result, true);
    }

    static tjs_error TJS_INTF_METHOD in(tTJSVariant *result, tjs_int,
                                        tTJSVariant **, iTJSDispatch2 *) {
        if(result)
            *result = TJS_W("");
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD out(tTJSVariant *result, tjs_int numparams,
                                         tTJSVariant **param,
                                         iTJSDispatch2 *) {
        if(numparams > 0 && param && param[0]) {
            ttstr text = *param[0];
            TVPAddLog(text);
        }
        return ReturnVoid(result);
    }

    static tjs_error TJS_INTF_METHOD flush(tTJSVariant *result, tjs_int,
                                           tTJSVariant **,
                                           iTJSDispatch2 *) {
        return ReturnVoid(result);
    }
};

NCB_ATTACH_CLASS(StdioCompat, System) {
    Property(TJS_W("stdioState"), &StdioCompat::getState, 0);
    RawCallback(TJS_W("attachConsole"), &StdioCompat::attach, TJS_STATICMEMBER);
    RawCallback(TJS_W("allocConsole"), &StdioCompat::alloc, TJS_STATICMEMBER);
    RawCallback(TJS_W("freeConsole"), &StdioCompat::free, TJS_STATICMEMBER);
    RawCallback(TJS_W("stdin"), &StdioCompat::in, TJS_STATICMEMBER);
    RawCallback(TJS_W("stdout"), &StdioCompat::out, TJS_STATICMEMBER);
    RawCallback(TJS_W("stderr"), &StdioCompat::out, TJS_STATICMEMBER);
    RawCallback(TJS_W("flush"), &StdioCompat::flush, TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// fontInfo.dll
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fontInfo.dll")

class FontInfoCompat {
public:
    static tjs_error TJS_INTF_METHOD getFontInfoMap(tTJSVariant *result,
                                                    tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(result)
            *result = EmptyDictionary();
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(FontInfoCompat, System) {
    RawCallback(TJS_W("getFontInfoMap"), &FontInfoCompat::getFontInfoMap,
                TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// pkutil.dll
// Product-key helpers used by some commercial KRKR titles commonly expose
// Win32 locale/codepage functions as globals. Android has no LCID, but the
// scripts mainly need the calls to exist during startup/license checks.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("pkutil.dll")

static tjs_int GetUserDefaultLCIDCompat() { return 0x0411; }
static tjs_int GetSystemDefaultLCIDCompat() { return 0x0411; }
static tjs_int GetThreadLocaleCompat() { return 0x0411; }
static tjs_int GetUserDefaultLangIDCompat() { return 0x0411; }
static tjs_int GetSystemDefaultLangIDCompat() { return 0x0411; }
static tjs_int GetUserDefaultUILanguageCompat() { return 0x0411; }
static tjs_int GetACPCompat() { return 932; }
static tjs_int GetOEMCPCompat() { return 932; }
static bool SetThreadLocaleCompat(tjs_int) { return true; }

NCB_REGISTER_FUNCTION(GetUserDefaultLCID, GetUserDefaultLCIDCompat);
NCB_REGISTER_FUNCTION(GetSystemDefaultLCID, GetSystemDefaultLCIDCompat);
NCB_REGISTER_FUNCTION(GetThreadLocale, GetThreadLocaleCompat);
NCB_REGISTER_FUNCTION(GetUserDefaultLangID, GetUserDefaultLangIDCompat);
NCB_REGISTER_FUNCTION(GetSystemDefaultLangID, GetSystemDefaultLangIDCompat);
NCB_REGISTER_FUNCTION(GetUserDefaultUILanguage,
                      GetUserDefaultUILanguageCompat);
NCB_REGISTER_FUNCTION(GetACP, GetACPCompat);
NCB_REGISTER_FUNCTION(GetOEMCP, GetOEMCPCompat);
NCB_REGISTER_FUNCTION(SetThreadLocale, SetThreadLocaleCompat);

// ---------------------------------------------------------------------------
// systemEx.dll
// Cross-platform subset of WAMSoft's Win32-oriented System extension. Windows
// registry, DPI, known-folder and DLL-search APIs degrade to false/empty values;
// environment, URL and message-pump helpers remain useful on Android.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("systemEx.dll")

class SystemExCompat {
public:
    static tjs_error TJS_INTF_METHOD writeRegValue(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **,
                                                   iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        return ReturnBool(result, false);
    }

    static tjs_error TJS_INTF_METHOD readEnvValue(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(param[0]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;
        if(result)
            *result = ReadEnvironmentValue(ttstr(*param[0]));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD writeEnvValue(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *) {
        if(numparams < 2 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(param[0]->Type() != tvtString)
            return TJS_E_INVALIDPARAM;

        ttstr name = *param[0];
        if(result)
            *result = ReadEnvironmentValue(name);

        std::string narrowName = ToNarrow(name);
        if(narrowName.empty())
            return TJS_E_INVALIDPARAM;
        if(!param[1] || param[1]->Type() == tvtVoid) {
            unsetenv(narrowName.c_str());
        } else {
            ttstr value = *param[1];
            setenv(narrowName.c_str(), ToNarrow(value).c_str(), 1);
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD expandEnvString(tTJSVariant *result,
                                                     tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = ExpandEnvironmentString(ttstr(*param[0]));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD urlencode(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = UrlEncode(ttstr(*param[0]));
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD urldecode(tTJSVariant *result,
                                               tjs_int numparams,
                                               tTJSVariant **param,
                                               iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        bool utf8 = numparams < 2 || !param[1] || param[1]->AsInteger() != 0;
        if(result)
            *result = UrlDecode(ttstr(*param[0]), utf8);
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getAboutString(tTJSVariant *result,
                                                    tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(result)
            *result = TVPGetAboutString();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD confirm(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             iTJSDispatch2 *) {
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        ttstr text = *param[0];
        ttstr caption =
            (numparams > 1 && param[1] && param[1]->Type() != tvtVoid)
                ? ttstr(*param[1])
                : ttstr(TJS_W(""));
        return ReturnBool(result,
                          TVPShowSimpleMessageBoxYesNo(text, caption) == 0);
    }

    static tjs_error TJS_INTF_METHOD waitForAppLock(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnBool(result, true);
    }

    static tjs_error TJS_INTF_METHOD setDpiAwareness(tTJSVariant *result,
                                                     tjs_int numparams,
                                                     tTJSVariant **,
                                                     iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnInt(result, 0);
    }

    static tjs_error TJS_INTF_METHOD getOSVersion(tTJSVariant *result, tjs_int,
                                                  tTJSVariant **,
                                                  iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        if(!dict)
            return TJS_E_FAIL;
        SetDictionaryValue(dict, TJS_W("major"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("minor"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("build"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("platform"),
#ifdef __ANDROID__
                           tTJSVariant(TJS_W("Android"))
#else
                           tTJSVariant(TJS_W("Unknown"))
#endif
        );
        SetDictionaryValue(dict, TJS_W("spmajor"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("spminor"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("servicepack"),
                           tTJSVariant(TJS_W("")));
        SetDictionaryValue(dict, TJS_W("servevicepack"),
                           tTJSVariant(TJS_W("")));
        SetDictionaryValue(dict, TJS_W("suite"), tTJSVariant((tjs_int)0));
        SetDictionaryValue(dict, TJS_W("type"), tTJSVariant((tjs_int)0));
        *result = tTJSVariant(dict, dict);
        dict->Release();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD getKnownFolderPath(tTJSVariant *result,
                                                        tjs_int numparams,
                                                        tTJSVariant **,
                                                        iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(result)
            *result = TJS_W("");
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD processApplicationMessages(
        tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        TVPHandleApplicationMessage();
        return ReturnVoid(result);
    }

    static tjs_error TJS_INTF_METHOD handleApplicationMessage(
        tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
        TVPHandleApplicationMessage();
        return ReturnVoid(result);
    }

    static tjs_error TJS_INTF_METHOD setDefaultDllDirectories(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **,
        iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnBool(result, false);
    }

    static tjs_error TJS_INTF_METHOD addDllDirectory(tTJSVariant *result,
                                                     tjs_int numparams,
                                                     tTJSVariant **,
                                                     iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnInt(result, 0);
    }

    static tjs_error TJS_INTF_METHOD removeDllDirectory(tTJSVariant *result,
                                                        tjs_int numparams,
                                                        tTJSVariant **,
                                                        iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        return ReturnBool(result, false);
    }
};

NCB_ATTACH_CLASS(SystemExCompat, System) {
    RawCallback(TJS_W("writeRegValue"), &SystemExCompat::writeRegValue,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("readEnvValue"), &SystemExCompat::readEnvValue,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("writeEnvValue"), &SystemExCompat::writeEnvValue,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("expandEnvString"), &SystemExCompat::expandEnvString,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("urlencode"), &SystemExCompat::urlencode,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("urldecode"), &SystemExCompat::urldecode,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getAboutString"), &SystemExCompat::getAboutString,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("confirm"), &SystemExCompat::confirm, TJS_STATICMEMBER);
    RawCallback(TJS_W("waitForAppLock"), &SystemExCompat::waitForAppLock,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("setDpiAwareness"), &SystemExCompat::setDpiAwareness,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getOSVersion"), &SystemExCompat::getOSVersion,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("getKnownFolderPath"),
                &SystemExCompat::getKnownFolderPath, TJS_STATICMEMBER);
    RawCallback(TJS_W("processApplicationMessages"),
                &SystemExCompat::processApplicationMessages,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("handleApplicationMessage"),
                &SystemExCompat::handleApplicationMessage, TJS_STATICMEMBER);
    RawCallback(TJS_W("setDefaultDllDirectories"),
                &SystemExCompat::setDefaultDllDirectories, TJS_STATICMEMBER);
    RawCallback(TJS_W("addDllDirectory"), &SystemExCompat::addDllDirectory,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("removeDllDirectory"),
                &SystemExCompat::removeDllDirectory, TJS_STATICMEMBER);

    Variant(TJS_W("dacUnaware"), (tjs_int)-1, TJS_STATICMEMBER);
    Variant(TJS_W("dacSystemAware"), (tjs_int)-2, TJS_STATICMEMBER);
    Variant(TJS_W("dacPerMonitorAware"), (tjs_int)-3, TJS_STATICMEMBER);
    Variant(TJS_W("dacPerMonitorAwareV2"), (tjs_int)-4, TJS_STATICMEMBER);
    Variant(TJS_W("dacUnawareGdiScaled"), (tjs_int)-5, TJS_STATICMEMBER);
    Variant(TJS_W("llsApplicationDir"), (tjs_int)0x00000200,
            TJS_STATICMEMBER);
    Variant(TJS_W("llsDefaultDirs"), (tjs_int)0x00001000, TJS_STATICMEMBER);
    Variant(TJS_W("llsSystem32"), (tjs_int)0x00000800, TJS_STATICMEMBER);
    Variant(TJS_W("llsUserDirs"), (tjs_int)0x00000400, TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// resourceRW.dll
// Win32 PE resources do not exist on Android. Provide the public class/method
// shape and return empty/false values so scripts can keep running.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("resourceRW.dll")

class ResourceReaderCompat {
public:
    bool open(ttstr) { return false; }
    void close() {}
    void setLang(tjs_int) {}
    bool isExistentResource(tTJSVariant, tTJSVariant) { return false; }
    ttstr readToText(tTJSVariant, tTJSVariant) { return ttstr(TJS_W("")); }
    tTJSVariant readToOctet(tTJSVariant, tTJSVariant) { return EmptyOctet(); }
    bool readToFile(tTJSVariant, tTJSVariant, ttstr) { return false; }
    tTJSVariant enumTypes() { return EmptyArray(); }
    tTJSVariant enumNames(tTJSVariant) { return EmptyArray(); }
    tTJSVariant enumLangs(tTJSVariant, tTJSVariant) { return EmptyArray(); }
};

class ResourceWriterCompat {
public:
    bool open(ttstr) { return false; }
    void close() {}
    void setLang(tjs_int) {}
    bool clear(tTJSVariant, tTJSVariant) { return false; }
    bool writeFromText(tTJSVariant, tTJSVariant, ttstr) { return false; }
    bool writeFromOctet(tTJSVariant, tTJSVariant, tTJSVariant) {
        return false;
    }
    bool writeFromFile(tTJSVariant, tTJSVariant, ttstr) { return false; }
};

class ResourceIconImageCompat {
public:
    void fromOctet(tTJSVariant) {}
    tTJSVariant toOctet() { return EmptyOctet(); }
    void setID(tjs_int, tjs_int) {}
    tjs_int getID(tjs_int) { return 0; }
    void setImage(tjs_int, tTJSVariant) {}
    tTJSVariant getImage(tjs_int) { return EmptyOctet(); }
    void setHotSpot(tjs_int, tjs_int, tjs_int) {}
    tTJSVariant getHotSpot(tjs_int) { return EmptyArray(); }
    tjs_int getCount() const { return 0; }
    bool getIsCursor() const { return false; }
    void setIsCursor(bool) {}
};

class ResourceIconGroupCompat {
public:
    void fromIcon(tTJSVariant) {}
    tTJSVariant toIcon() { return tTJSVariant(); }
    void fromOctet(tTJSVariant) {}
    tTJSVariant toOctet() { return EmptyOctet(); }
};

class ResourceVersionInfoCompat {
public:
    void changeString(ttstr, ttstr, tjs_int) {}
    void changeInfo(ttstr, ttstr) {}
    tTJSVariant getLangList() { return EmptyArray(); }
    void addLang(tjs_int) {}
    void removeLang(tjs_int) {}
    void copyLang(tjs_int, tjs_int) {}
    void fromOctet(tTJSVariant) {}
    tTJSVariant toOctet() { return EmptyOctet(); }
};

NCB_REGISTER_CLASS_DIFFER(ResourceReader, ResourceReaderCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD(setLang);
    NCB_METHOD(isExistentResource);
    NCB_METHOD(readToText);
    NCB_METHOD(readToFile);
    NCB_METHOD(readToOctet);
    NCB_METHOD(enumTypes);
    NCB_METHOD(enumNames);
    NCB_METHOD(enumLangs);
}

NCB_REGISTER_CLASS_DIFFER(ResourceWriter, ResourceWriterCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(open);
    NCB_METHOD(close);
    NCB_METHOD(setLang);
    NCB_METHOD(clear);
    NCB_METHOD(writeFromText);
    NCB_METHOD(writeFromFile);
    NCB_METHOD(writeFromOctet);
}

NCB_REGISTER_CLASS_DIFFER(ResourceIconImage, ResourceIconImageCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(fromOctet);
    NCB_METHOD(toOctet);
    NCB_METHOD(setID);
    NCB_METHOD(getID);
    NCB_METHOD(setImage);
    NCB_METHOD(getImage);
    NCB_METHOD(setHotSpot);
    NCB_METHOD(getHotSpot);
    NCB_PROPERTY_RO(count, getCount);
    NCB_PROPERTY(isCursor, getIsCursor, setIsCursor);
}

NCB_REGISTER_CLASS_DIFFER(ResourceIconGroup, ResourceIconGroupCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(fromIcon);
    NCB_METHOD(toIcon);
    NCB_METHOD(fromOctet);
    NCB_METHOD(toOctet);
}

NCB_REGISTER_CLASS_DIFFER(ResourceVersionInfo, ResourceVersionInfoCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(changeString);
    NCB_METHOD(changeInfo);
    NCB_METHOD(getLangList);
    NCB_METHOD(addLang);
    NCB_METHOD(removeLang);
    NCB_METHOD(copyLang);
    NCB_METHOD(fromOctet);
    NCB_METHOD(toOctet);
}

static void resourceRWCompatInit() {
    TVPExecuteScript(TJS_W(
        "global.rtAccelerator = 9;"
        "global.rtAniCursor = 21;"
        "global.rtAniIcon = 22;"
        "global.rtBitmap = 2;"
        "global.rtCursor = 1;"
        "global.rtDialog = 5;"
        "global.rtFont = 8;"
        "global.rtFontDir = 7;"
        "global.rtGroupCursor = 12;"
        "global.rtGroupIcon = 14;"
        "global.rtHtml = 23;"
        "global.rtIcon = 3;"
        "global.rtMenu = 4;"
        "global.rtMessageTable = 11;"
        "global.rtRcData = 10;"
        "global.rtString = 6;"
        "global.rtVersion = 16;"));
}
NCB_PRE_REGIST_CALLBACK(resourceRWCompatInit);

// ---------------------------------------------------------------------------
// windowExProgress.dll
// Progress UI is Win32-only. Preserve Window method shape as no-op state.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("windowExProgress.dll")

class WindowExProgressCompat {
public:
    explicit WindowExProgressCompat(iTJSDispatch2 *) {}
    void startProgress(tTJSVariant = tTJSVariant()) { _running = true; }
    bool doProgress(tjs_int) { return false; }
    void endProgress() { _running = false; }
    bool running() const { return _running; }

private:
    bool _running = false;
};

NCB_GET_INSTANCE_HOOK(WindowExProgressCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowExProgressCompat, Window) {
    Variant(TJS_W("PBS_SMOOTH"), 1);
    Variant(TJS_W("PBS_VERTICAL"), 4);
    NCB_METHOD(startProgress);
    NCB_METHOD(doProgress);
    NCB_METHOD(endProgress);
    NCB_PROPERTY_RO(progressRunning, running);
}

// ---------------------------------------------------------------------------
// krkrsteam.dll / steam_api.dll
// Android launcher does not ship Steamworks. Expose a false/empty Steam object.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkrsteam.dll")

class SteamCompat {
public:
    ttstr getLanguage() { return ttstr(TJS_W("")); }
    bool requestInitialize() { return false; }
    bool getInitialized() const { return false; }
    tjs_int getAchievementsCount() const { return 0; }
    tTJSVariant getAchievement(tTJSVariant) { return tTJSVariant(); }
    bool setAchievement(tTJSVariant) { return false; }
    bool clearAchievement(tTJSVariant) { return false; }
    bool getCloudEnabled() const { return false; }
    void setCloudEnabled(bool) {}
    tTJSVariant getCloudQuota() { return tTJSVariant(); }
    tjs_int getCloudFileCount() const { return 0; }
    tTJSVariant getCloudFileInfo(tjs_int) { return tTJSVariant(); }
    bool deleteCloudFile(ttstr) { return false; }
    bool copyCloudFile(ttstr, ttstr) { return false; }
    bool triggerScreenshot() { return false; }
    bool hookScreenshots(tTJSVariant) { return false; }
    bool writeScreenshot(tTJSVariant) { return false; }
    bool isBroadcasting() { return false; }
    bool hookBroadcasting(tTJSVariant) { return false; }
    bool isIsSubscribedApp(tjs_int) { return false; }
    bool isDlcInstalled(tjs_int) { return false; }
    tjs_int getDLCCount() { return 0; }
    tTJSVariant getDLCData(tjs_int) { return tTJSVariant(); }
};

NCB_REGISTER_CLASS_DIFFER(Steam, SteamCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(getLanguage);
    NCB_METHOD(requestInitialize);
    NCB_PROPERTY_RO(initialized, getInitialized);
    NCB_PROPERTY_RO(achievementsCount, getAchievementsCount);
    NCB_METHOD(getAchievement);
    NCB_METHOD(setAchievement);
    NCB_METHOD(clearAchievement);
    NCB_PROPERTY(cloudEnabled, getCloudEnabled, setCloudEnabled);
    NCB_METHOD(getCloudQuota);
    NCB_PROPERTY_RO(cloudFileCount, getCloudFileCount);
    NCB_METHOD(getCloudFileInfo);
    NCB_METHOD(deleteCloudFile);
    NCB_METHOD(copyCloudFile);
    NCB_METHOD(triggerScreenshot);
    NCB_METHOD(hookScreenshots);
    NCB_METHOD(writeScreenshot);
    NCB_METHOD(isBroadcasting);
    NCB_METHOD(hookBroadcasting);
    NCB_METHOD(isIsSubscribedApp);
    NCB_METHOD(isDlcInstalled);
    NCB_METHOD(getDLCCount);
    NCB_METHOD(getDLCData);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("steam_api.dll")
static void steamApiCompatStub() {}
NCB_PRE_REGIST_CALLBACK(steamApiCompatStub);

// ---------------------------------------------------------------------------
// Heavy optional render/vector modules. Their real implementations depend on
// ThorVG/minikin or Win32 surfaces; register module names so plugin probes do
// not force failure branches. Existing layerExDraw/TextRender cover the common
// drawing/text paths in this engine.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExVector.dll")
static void layerExVectorCompatStub() {}
NCB_PRE_REGIST_CALLBACK(layerExVectorCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("richtext.dll")
static void richTextCompatStub() {}
NCB_PRE_REGIST_CALLBACK(richTextCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krkr_richtext.dll")
static void krkrRichTextCompatStub() {}
NCB_PRE_REGIST_CALLBACK(krkrRichTextCompatStub);

// ---------------------------------------------------------------------------
// layerExAVI.dll / layerExSave.dll
// Capture/export features are not available on Android, but some script
// systems attach these methods unconditionally after probing the DLL.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAVI.dll")

class LayerExAVICompat {
public:
    explicit LayerExAVICompat(iTJSDispatch2 *) {}

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnBool(result, false);
    }

    static tjs_error TJS_INTF_METHOD voidRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnVoid(result);
    }
};

NCB_GET_INSTANCE_HOOK(LayerExAVICompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExAVICompat, Layer) {
    RawCallback(TJS_W("openAVI"), &LayerExAVICompat::falseRaw, 0);
    RawCallback(TJS_W("openCompressedAVI"), &LayerExAVICompat::falseRaw, 0);
    RawCallback(TJS_W("closeAVI"), &LayerExAVICompat::voidRaw, 0);
    RawCallback(TJS_W("recordAVI"), &LayerExAVICompat::falseRaw, 0);
    RawCallback(TJS_W("openWAV"), &LayerExAVICompat::falseRaw, 0);
    RawCallback(TJS_W("startWAV"), &LayerExAVICompat::voidRaw, 0);
    RawCallback(TJS_W("stopWAV"), &LayerExAVICompat::voidRaw, 0);
    RawCallback(TJS_W("closeWAV"), &LayerExAVICompat::voidRaw, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSave.dll")

class LayerExSaveLayerCompat {
public:
    explicit LayerExSaveLayerCompat(iTJSDispatch2 *) {}

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnBool(result, false);
    }

    static tjs_error TJS_INTF_METHOD zeroRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD emptyArrayRaw(tTJSVariant *result,
                                                   tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *) {
        if(result)
            *result = EmptyArray();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD emptyOctetRaw(tTJSVariant *result,
                                                   tjs_int, tTJSVariant **,
                                                   iTJSDispatch2 *) {
        if(result)
            *result = EmptyOctet();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD voidRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnVoid(result);
    }
};

NCB_GET_INSTANCE_HOOK(LayerExSaveLayerCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExSaveLayerCompat, Layer) {
    RawCallback(TJS_W("saveLayerImagePng"),
                &LayerExSaveLayerCompat::falseRaw, 0);
    RawCallback(TJS_W("saveLayerImagePngOctet"),
                &LayerExSaveLayerCompat::emptyOctetRaw, 0);
    RawCallback(TJS_W("saveProvinceImage"),
                &LayerExSaveLayerCompat::falseRaw, 0);
    RawCallback(TJS_W("saveLayerImageTlg5"),
                &LayerExSaveLayerCompat::falseRaw, 0);
    RawCallback(TJS_W("getFingerPrintValue"),
                &LayerExSaveLayerCompat::zeroRaw, 0);
    RawCallback(TJS_W("getShrinkVectorOctet"),
                &LayerExSaveLayerCompat::emptyOctetRaw, 0);
}

class WindowSaveImageCompat {
public:
    explicit WindowSaveImageCompat(iTJSDispatch2 *) {}
    void startSaveLayerImage(tTJSVariant = tTJSVariant()) {}
    void cancelSaveLayerImage() {}
    void stopSaveLayerImage() {}
};

NCB_GET_INSTANCE_HOOK(WindowSaveImageCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowSaveImageCompat, Window) {
    NCB_METHOD(startSaveLayerImage);
    NCB_METHOD(cancelSaveLayerImage);
    NCB_METHOD(stopSaveLayerImage);
}

class LayerExSaveMathCompat {
public:
    static tjs_error TJS_INTF_METHOD octetVectorSum(tTJSVariant *result,
                                                    tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(LayerExSaveMathCompat, Math) {
    RawCallback(TJS_W("octetVectorSum"),
                &LayerExSaveMathCompat::octetVectorSum, TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// messenger.dll / msgreceiver.dll
// Win32 window messaging has no Android equivalent. Keep receiver and sender
// APIs present so scripts can register/probe them without aborting startup.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("messenger.dll")

class MessengerWindowCompat {
public:
    explicit MessengerWindowCompat(iTJSDispatch2 *) {}

    bool getMessageEnable() const { return messageEnable_; }
    void setMessageEnable(bool value) { messageEnable_ = value; }

    ttstr getStoreHWND() const { return storeHWND_; }
    void setStoreHWND(ttstr value) { storeHWND_ = value; }

    static tjs_error TJS_INTF_METHOD registerUserMessageReceiver(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *) {
        if(result) {
            if(numparams >= 2 && param && param[1] &&
               param[1]->Type() == tvtInteger)
                *result = *param[1];
            else
                *result = 0;
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnBool(result, false);
    }

private:
    bool messageEnable_ = false;
    ttstr storeHWND_;
};

NCB_GET_INSTANCE_HOOK(MessengerWindowCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(MessengerWindowCompat, Window) {
    NCB_PROPERTY(messageEnable, getMessageEnable, setMessageEnable);
    NCB_PROPERTY(storeHWND, getStoreHWND, setStoreHWND);
    RawCallback(TJS_W("registerUserMessageReceiver"),
                &MessengerWindowCompat::registerUserMessageReceiver, 0);
    RawCallback(TJS_W("sendUserMessage"), &MessengerWindowCompat::falseRaw, 0);
    RawCallback(TJS_W("sendMessage"), &MessengerWindowCompat::falseRaw, 0);
    RawCallback(TJS_W("sendUserMessageDirect"),
                &MessengerWindowCompat::falseRaw, 0);
    RawCallback(TJS_W("sendMessageDirect"),
                &MessengerWindowCompat::falseRaw, 0);
    RawCallback(TJS_W("postUserMessage"), &MessengerWindowCompat::falseRaw, 0);
    RawCallback(TJS_W("postUserMessageDirect"),
                &MessengerWindowCompat::falseRaw, 0);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("msgreceiver.dll")

static tjs_error TJS_INTF_METHOD WMRStartFunctionCompat(
    tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return ReturnBool(result, true);
}

static tjs_error TJS_INTF_METHOD WMRStopFunctionCompat(
    tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
    return ReturnBool(result, true);
}

NCB_REGISTER_FUNCTION(WMRStartFunction, WMRStartFunctionCompat);
NCB_REGISTER_FUNCTION(WMRStopFunction, WMRStopFunctionCompat);

// ---------------------------------------------------------------------------
// process.dll / shellExecute.dll
// External process launch is intentionally disabled on Android. Return the
// same public result shapes, but report failure instead of throwing.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("process.dll")

class ProcessCompat {
public:
    static tjs_error factory(ProcessCompat **result, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *) {
        if(result)
            *result = new ProcessCompat();
        return TJS_S_OK;
    }

    tjs_int getStatus() const { return 0; }
    void terminate(tjs_int = 0) {}
    bool sendSignal(bool = false) { return false; }

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              ProcessCompat *) {
        return ReturnBool(result, false);
    }
};

NCB_REGISTER_CLASS_DIFFER(Process, ProcessCompat) {
    Factory(&ProcessCompat::factory);
    RawCallback(TJS_W("execute"), &Class::falseRaw, 0);
    RawCallback(TJS_W("commandExecute"), &Class::falseRaw, 0);
    NCB_METHOD(terminate);
    NCB_METHOD(sendSignal);
    NCB_PROPERTY_RO(status, getStatus);
}

class ProcessSystemCompat {
public:
    static tjs_error TJS_INTF_METHOD commandExecute(tTJSVariant *result,
                                                    tjs_int, tTJSVariant **,
                                                    iTJSDispatch2 *) {
        if(result)
            *result = MakeCommandExecuteResult(
                TJS_W("error"),
                TJS_W("external process execution is unavailable on Android"));
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(ProcessSystemCompat, System) {
    RawCallback(TJS_W("commandExecute"),
                &ProcessSystemCompat::commandExecute, TJS_STATICMEMBER);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("shellExecute.dll")

class ShellExecuteWindowCompat {
public:
    explicit ShellExecuteWindowCompat(iTJSDispatch2 *) {}

    static tjs_error TJS_INTF_METHOD minusOneRaw(tTJSVariant *result, tjs_int,
                                                 tTJSVariant **,
                                                 iTJSDispatch2 *) {
        return ReturnInt(result, -1);
    }

    static tjs_error TJS_INTF_METHOD falseRaw(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnBool(result, false);
    }

    static tjs_error TJS_INTF_METHOD voidRaw(tTJSVariant *result, tjs_int,
                                             tTJSVariant **,
                                             iTJSDispatch2 *) {
        return ReturnVoid(result);
    }
};

NCB_GET_INSTANCE_HOOK(ShellExecuteWindowCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(ShellExecuteWindowCompat, Window) {
    RawCallback(TJS_W("shellExecute"), &ShellExecuteWindowCompat::minusOneRaw,
                0);
    RawCallback(TJS_W("commandExecute"), &ShellExecuteWindowCompat::minusOneRaw,
                0);
    RawCallback(TJS_W("terminateProcess"),
                &ShellExecuteWindowCompat::voidRaw, 0);
    RawCallback(TJS_W("commandSendSignal"),
                &ShellExecuteWindowCompat::falseRaw, 0);
}

// ---------------------------------------------------------------------------
// sigcheck.dll
// Optional verification helpers. They are non-critical for Android playback,
// but missing methods can stop script initialization.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("sigcheck.dll")

class SigCheckWindowCompat {
public:
    explicit SigCheckWindowCompat(iTJSDispatch2 *) {}

    tjs_int checkSignature(ttstr, ttstr, tTJSVariant = tTJSVariant()) {
        return 0;
    }
    void cancelCheckSignature(tjs_int = 0) {}
    void stopCheckSignature(tjs_int = 0) {}
};

NCB_GET_INSTANCE_HOOK(SigCheckWindowCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(SigCheckWindowCompat, Window) {
    NCB_METHOD(checkSignature);
    NCB_METHOD(cancelCheckSignature);
    NCB_METHOD(stopCheckSignature);
}

// ---------------------------------------------------------------------------
// binaryStream.dll
// A cross-platform subset backed by TVPCreateStream. This covers normal
// storage read/write and integer IO; DLL filters and zlib copy modes degrade
// to plain copy because external Windows filter DLLs are unavailable.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("binaryStream.dll")

class BinaryStreamCompat {
public:
    BinaryStreamCompat() = default;
    ~BinaryStreamCompat() { close(); }

    static tjs_error factory(BinaryStreamCompat **result, tjs_int count,
                             tTJSVariant **params, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        std::unique_ptr<BinaryStreamCompat> self(new BinaryStreamCompat());
        if(count > 0 && params && params[0] && params[0]->Type() != tvtVoid) {
            ttstr storage = *params[0];
            if(!storage.IsEmpty()) {
                tjs_int mode =
                    count > 1 && params[1] ? (tjs_int)params[1]->AsInteger()
                                           : TJS_BS_READ;
                if(!self->openStorage(storage, mode))
                    return TJS_E_FAIL;
            }
        }
        *result = self.release();
        return TJS_S_OK;
    }

    void close() {
        delete stream_;
        stream_ = nullptr;
        storage_.Clear();
        mode_ = -1;
    }

    tjs_int64 seek(tjs_int64 pos, tjs_int whence) {
        if(!stream_)
            return 0;
        return static_cast<tjs_int64>(stream_->Seek(pos, whence));
    }

    tjs_int64 tell() { return seek(0, TJS_BS_SEEK_CUR); }

    ttstr getStorage() const { return stream_ ? storage_ : ttstr(); }
    tjs_int getMode() const { return mode_; }

    void setProgressCallback(tTJSVariant) {}
    void setFilter(tTJSVariant = tTJSVariant()) {}

    static tjs_error TJS_INTF_METHOD open(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          BinaryStreamCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        ttstr storage = *param[0];
        tjs_int mode =
            numparams > 1 && param[1] ? (tjs_int)param[1]->AsInteger()
                                      : TJS_BS_READ;
        return ReturnBool(result, self->openStorage(storage, mode));
    }

    static tjs_error TJS_INTF_METHOD read(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          BinaryStreamCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(!self->stream_)
            return ReturnVoid(result);

        tjs_int64 requested = param[0]->AsInteger();
        if(requested <= 0)
            return ReturnVoid(result);
        tjs_uint size = static_cast<tjs_uint>(
            std::min<tjs_int64>(requested, 0x7fffffff));
        std::vector<tjs_uint8> data(size);
        tjs_uint readBytes = self->stream_->Read(data.data(), size);
        if(result) {
            if(readBytes > 0)
                *result = tTJSVariant(data.data(), readBytes);
            else
                result->Clear();
        }
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD write(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           BinaryStreamCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(!self->stream_)
            return ReturnInt(result, 0);

        tjs_uint written = 0;
        if(param[0]->Type() == tvtOctet) {
            tTJSVariantOctet *octet = param[0]->AsOctetNoAddRef();
            if(octet)
                written =
                    self->stream_->Write(octet->GetData(), octet->GetLength());
        } else {
            ttstr text = *param[0];
            const tjs_char *ptr = text.c_str();
            tjs_uint bytes =
                static_cast<tjs_uint>((text.length() + 1) * sizeof(tjs_char));
            written = self->stream_->Write(ptr, bytes);
        }
        return ReturnInt(result, written);
    }

    static tjs_error TJS_INTF_METHOD readI8(tTJSVariant *result, tjs_int,
                                            tTJSVariant **,
                                            BinaryStreamCompat *self) {
        return readInt(result, self, 1, false);
    }
    static tjs_error TJS_INTF_METHOD readI16LE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 2, false);
    }
    static tjs_error TJS_INTF_METHOD readI32LE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 4, false);
    }
    static tjs_error TJS_INTF_METHOD readI64LE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 8, false);
    }
    static tjs_error TJS_INTF_METHOD readI16BE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 2, true);
    }
    static tjs_error TJS_INTF_METHOD readI32BE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 4, true);
    }
    static tjs_error TJS_INTF_METHOD readI64BE(tTJSVariant *result, tjs_int,
                                               tTJSVariant **,
                                               BinaryStreamCompat *self) {
        return readInt(result, self, 8, true);
    }

    static tjs_error TJS_INTF_METHOD writeI8(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 1, false);
    }
    static tjs_error TJS_INTF_METHOD writeI16LE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 2, false);
    }
    static tjs_error TJS_INTF_METHOD writeI32LE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 4, false);
    }
    static tjs_error TJS_INTF_METHOD writeI64LE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 8, false);
    }
    static tjs_error TJS_INTF_METHOD writeI16BE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 2, true);
    }
    static tjs_error TJS_INTF_METHOD writeI32BE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 4, true);
    }
    static tjs_error TJS_INTF_METHOD writeI64BE(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return writeInt(result, numparams, param, self, 8, true);
    }

    static tjs_error TJS_INTF_METHOD copy(tTJSVariant *result,
                                          tjs_int numparams,
                                          tTJSVariant **param,
                                          BinaryStreamCompat *self) {
        return copyStorage(result, numparams, param, self);
    }

    static tjs_error TJS_INTF_METHOD compress(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              BinaryStreamCompat *self) {
        return copyStorage(result, numparams, param, self);
    }

    static tjs_error TJS_INTF_METHOD decompress(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                BinaryStreamCompat *self) {
        return copyStorage(result, numparams, param, self);
    }

private:
    bool openStorage(const ttstr &storage, tjs_int mode) {
        close();
        stream_ = TVPCreateStream(storage, mode & TJS_BS_ACCESS_MASK);
        if(!stream_)
            return false;
        storage_ = storage;
        mode_ = mode;
        return true;
    }

    static tjs_error readInt(tTJSVariant *result, BinaryStreamCompat *self,
                             int bytes, bool bigEndian) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(!self->stream_)
            return ReturnVoid(result);

        tjs_uint8 buffer[8] = { 0 };
        tjs_uint readBytes = self->stream_->Read(buffer, bytes);
        if(readBytes == 0)
            return ReturnVoid(result);

        tjs_uint64 value = 0;
        if(bigEndian) {
            for(int i = 0; i < bytes; ++i)
                value = (value << 8) | buffer[i];
        } else {
            for(int i = bytes - 1; i >= 0; --i)
                value = (value << 8) | buffer[i];
        }
        return ReturnInt(result, static_cast<tjs_int64>(value));
    }

    static tjs_error writeInt(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, BinaryStreamCompat *self,
                              int bytes, bool bigEndian) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(!self->stream_)
            return ReturnInt(result, 0);

        tjs_uint64 value = static_cast<tjs_uint64>(param[0]->AsInteger());
        tjs_uint8 buffer[8] = { 0 };
        for(int i = 0; i < bytes; ++i) {
            int index = bigEndian ? bytes - 1 - i : i;
            buffer[index] = static_cast<tjs_uint8>((value >> (i * 8)) & 0xff);
        }
        return ReturnInt(result, self->stream_->Write(buffer, bytes));
    }

    static tjs_error copyStorage(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param,
                                 BinaryStreamCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        if(!self->stream_)
            return ReturnInt(result, 0);

        ttstr source = *param[0];
        std::unique_ptr<tTJSBinaryStream> input(
            TVPCreateStream(source, TJS_BS_READ));
        if(!input)
            return ReturnInt(result, 0);

        std::vector<tjs_uint8> buffer(64 * 1024);
        tjs_int64 total = 0;
        for(;;) {
            tjs_uint readBytes = input->Read(buffer.data(), buffer.size());
            if(readBytes == 0)
                break;
            tjs_uint written = self->stream_->Write(buffer.data(), readBytes);
            total += written;
            if(written != readBytes)
                break;
        }
        return ReturnInt(result, total);
    }

    tTJSBinaryStream *stream_ = nullptr;
    ttstr storage_;
    tjs_int mode_ = -1;
};

NCB_REGISTER_CLASS_DIFFER(BinaryStream, BinaryStreamCompat) {
    Factory(&BinaryStreamCompat::factory);
    RawCallback(TJS_W("open"), &Class::open, 0);
    NCB_METHOD(close);
    NCB_METHOD(seek);
    NCB_METHOD(tell);
    NCB_PROPERTY_RO(storage, getStorage);
    NCB_PROPERTY_RO(mode, getMode);
    RawCallback(TJS_W("read"), &Class::read, 0);
    RawCallback(TJS_W("write"), &Class::write, 0);
    RawCallback(TJS_W("copy"), &Class::copy, 0);
    RawCallback(TJS_W("compress"), &Class::compress, 0);
    RawCallback(TJS_W("decompress"), &Class::decompress, 0);
    NCB_METHOD(setProgressCallback);
    NCB_METHOD(setFilter);
    RawCallback(TJS_W("readI8"), &Class::readI8, 0);
    RawCallback(TJS_W("readI16LE"), &Class::readI16LE, 0);
    RawCallback(TJS_W("readI32LE"), &Class::readI32LE, 0);
    RawCallback(TJS_W("readI64LE"), &Class::readI64LE, 0);
    RawCallback(TJS_W("readI16BE"), &Class::readI16BE, 0);
    RawCallback(TJS_W("readI32BE"), &Class::readI32BE, 0);
    RawCallback(TJS_W("readI64BE"), &Class::readI64BE, 0);
    RawCallback(TJS_W("writeI8"), &Class::writeI8, 0);
    RawCallback(TJS_W("writeI16LE"), &Class::writeI16LE, 0);
    RawCallback(TJS_W("writeI32LE"), &Class::writeI32LE, 0);
    RawCallback(TJS_W("writeI64LE"), &Class::writeI64LE, 0);
    RawCallback(TJS_W("writeI16BE"), &Class::writeI16BE, 0);
    RawCallback(TJS_W("writeI32BE"), &Class::writeI32BE, 0);
    RawCallback(TJS_W("writeI64BE"), &Class::writeI64BE, 0);
    Variant(TJS_W("bsRead"), (tjs_int)TJS_BS_READ, 0);
    Variant(TJS_W("bsWrite"), (tjs_int)TJS_BS_WRITE, 0);
    Variant(TJS_W("bsAppend"), (tjs_int)TJS_BS_APPEND, 0);
    Variant(TJS_W("bsUpdate"), (tjs_int)TJS_BS_UPDATE, 0);
    Variant(TJS_W("bsSeekSet"), (tjs_int)TJS_BS_SEEK_SET, 0);
    Variant(TJS_W("bsSeekCur"), (tjs_int)TJS_BS_SEEK_CUR, 0);
    Variant(TJS_W("bsSeekEnd"), (tjs_int)TJS_BS_SEEK_END, 0);
}

// ---------------------------------------------------------------------------
// console.dll / fpslimit.dll / gamepad.dll
// Development console and frame sleeping are no-ops. Gamepad is represented as
// a disconnected device, matching the core fallback while allowing CanLoadPlugin.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("console.dll")
static void consoleCompatStub() {}
NCB_PRE_REGIST_CALLBACK(consoleCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("fpslimit.dll")

class FpsLimitCompat {
public:
    static tjs_error TJS_INTF_METHOD getLimit(tTJSVariant *result, tjs_int,
                                              tTJSVariant **,
                                              iTJSDispatch2 *) {
        return ReturnInt(result, limit_);
    }

    static tjs_error TJS_INTF_METHOD setLimit(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *) {
        if(numparams > 0 && param && param[0]) {
            limit_ = (tjs_int)param[0]->AsInteger();
            if(limit_ <= 0)
                limit_ = 1000;
        }
        return ReturnVoid(result);
    }

private:
    static tjs_int limit_;
};

tjs_int FpsLimitCompat::limit_ = 1000;

NCB_ATTACH_CLASS(FpsLimitCompat, System) {
    RawCallback(TJS_W("fpslimit"), &FpsLimitCompat::getLimit,
                &FpsLimitCompat::setLimit, TJS_STATICMEMBER);
}

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gamepad.dll")

class GamepadPortCompat {
public:
    GamepadPortCompat() = default;
    void initialize(tTJSVariant = tTJSVariant()) {}
    tTJSVariant getController(tjs_int) { return tTJSVariant(); }
    tjs_int getCount() const { return 0; }
};

class GamepadCompat {
public:
    static tjs_error factory(GamepadCompat **result, tjs_int, tTJSVariant **,
                             iTJSDispatch2 *) {
        if(result)
            *result = new GamepadCompat();
        return TJS_S_OK;
    }

    void update() {}
    ttstr getName() const { return ttstr(TJS_W("")); }
    tjs_int getType() const { return 0; }
    tjs_int getKeyState() const { return 0; }
    tjs_real getZeroReal() const { return 0.0; }
    tjs_int getZeroInt() const { return 0; }
    void setLeftVibration(tjs_real) {}
    void setRightVibration(tjs_real) {}
};

NCB_REGISTER_CLASS_DIFFER(GamepadPort, GamepadPortCompat) {
    NCB_CONSTRUCTOR(());
    NCB_METHOD(initialize);
    NCB_METHOD(getController);
    NCB_PROPERTY_RO(count, getCount);
}

NCB_REGISTER_CLASS_DIFFER(Gamepad, GamepadCompat) {
    Factory(&GamepadCompat::factory);
    NCB_METHOD(update);
    NCB_PROPERTY_RO(name, getName);
    NCB_PROPERTY_RO(type, getType);
    NCB_PROPERTY_RO(keyState, getKeyState);
    NCB_PROPERTY_DETAIL_RO(leftTrigger, Const, tjs_real, Class::getZeroReal,
                           ());
    NCB_PROPERTY_DETAIL_RO(rightTrigger, Const, tjs_real, Class::getZeroReal,
                           ());
    NCB_PROPERTY_DETAIL_RO(leftThumbStickX, Const, tjs_real,
                           Class::getZeroReal, ());
    NCB_PROPERTY_DETAIL_RO(leftThumbStickY, Const, tjs_real,
                           Class::getZeroReal, ());
    NCB_PROPERTY_DETAIL_RO(rightThumbStickX, Const, tjs_real,
                           Class::getZeroReal, ());
    NCB_PROPERTY_DETAIL_RO(rightThumbStickY, Const, tjs_real,
                           Class::getZeroReal, ());
    NCB_PROPERTY_WO(leftVibration, setLeftVibration);
    NCB_PROPERTY_WO(rightVibration, setRightVibration);
    NCB_PROPERTY_DETAIL_RO(analogLeftUpCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogLeftDownCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogLeftLeftCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogLeftRightCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogRightUpCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogRightDownCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogRightLeftCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(analogRightRightCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(degitalUpCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(degitalDownCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(degitalLeftCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(degitalRightCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonStartCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonBackCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonLeftThumbCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonRightThumbCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonLeftShoulderCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonLeftTriggerCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonRightShoulderCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonRightTriggerCount, Const, tjs_int,
                           Class::getZeroInt, ());
    NCB_PROPERTY_DETAIL_RO(buttonACount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonBCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonXCount, Const, tjs_int, Class::getZeroInt,
                           ());
    NCB_PROPERTY_DETAIL_RO(buttonYCount, Const, tjs_int, Class::getZeroInt,
                           ());
}

static void gamepadCompatInit() {
    TVPExecuteScript(TJS_W(
        "const gpDInput = 3, gpFFDInput = 2, gpXInput = 1,"
        "gpButtonDpadUp = 0x00000001, gpButtonDpadDown = 0x00000002,"
        "gpButtonDpadLeft = 0x00000004, gpButtonDpadRight = 0x00000008,"
        "gpButtonStart = 0x00000010, gpButtonBack = 0x00000020,"
        "gpButtonLeftThumb = 0x00000040, gpButtonRightThumb = 0x00000080,"
        "gpButtonLeftShoulder = 0x00000100,"
        "gpButtonRightShoulder = 0x00000200,"
        "gpButtonA = 0x00001000, gpButtonB = 0x00002000,"
        "gpButtonX = 0x00004000, gpButtonY = 0x00008000,"
        "gpLeftAxisX = 0x00010000, gpLeftAxisY = 0x00020000,"
        "gpRightAxisX = 0x00040000, gpRightAxisY = 0x00080000,"
        "gpLeftTrigger = 0x00100000, gpRightTrigger = 0x00200000,"
        "gpDIAxisX = 0, gpDIAxisY = 1, gpDIAxisZ = 2,"
        "gpDIAxisRotX = 3, gpDIAxisRotY = 4, gpDIAxisRotZ = 5,"
        "gpDISlider_0 = 6, gpDISlider_1 = 7,"
        "gpDIPOV_0 = 8, gpDIPOV_1 = 9, gpDIPOV_2 = 10, gpDIPOV_3 = 11,"
        "gpDIButton1 = 12, gpDIButton2 = 13, gpDIButton3 = 14,"
        "gpDIButton4 = 15, gpDIButton5 = 16, gpDIButton6 = 17,"
        "gpDIButton7 = 18, gpDIButton8 = 19, gpDIButton9 = 20,"
        "gpDIButton10 = 21, gpDIButton11 = 22, gpDIButton12 = 23,"
        "gpDIButton13 = 24, gpDIButton14 = 25, gpDIButton15 = 26,"
        "gpDIButton16 = 27, gpDIButton17 = 28, gpDIButton18 = 29,"
        "gpDIButton19 = 30, gpDIButton20 = 31, gpDIButton21 = 32,"
        "gpDIButton22 = 33, gpDIButton23 = 34, gpDIButton24 = 35,"
        "gpDIButton25 = 36, gpDIButton26 = 37, gpDIButton27 = 38,"
        "gpDIButton28 = 39, gpDIButton29 = 40, gpDIButton30 = 41,"
        "gpDIButton31 = 42, gpDIButton32 = 43, gpDIDisable = 44,"
        "gpTouchNo = 0, gpTouchDown = 1, gpTouchLiftoff = 0;"));
}

NCB_PRE_REGIST_CALLBACK(gamepadCompatInit);

// ---------------------------------------------------------------------------
// Heavy library/package names from the plugin set. These are not KRKR runtime
// plugins by themselves or require large native stacks, so expose loadable
// module names only.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("minikin.dll")
static void minikinCompatStub() {}
NCB_PRE_REGIST_CALLBACK(minikinCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("freetype.dll")
static void freetypeCompatStub() {}
NCB_PRE_REGIST_CALLBACK(freetypeCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("agg.dll")
static void aggCompatStub() {}
NCB_PRE_REGIST_CALLBACK(aggCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("elements.dll")
static void elementsCompatStub() {}
NCB_PRE_REGIST_CALLBACK(elementsCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("cubism.dll")
static void cubismCompatStub() {}
NCB_PRE_REGIST_CALLBACK(cubismCompatStub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("live2dcubismcore.dll")
static void live2dCubismCoreCompatStub() {}
NCB_PRE_REGIST_CALLBACK(live2dCubismCoreCompatStub);
