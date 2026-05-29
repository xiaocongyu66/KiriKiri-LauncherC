#include "tp_stub.h"
#include "ncbind.hpp"

#include "BinaryStream.h"
#include "CharacterSet.h"
#include "TextStream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#define NCB_MODULE_NAME TJS_W("json.dll")

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

using Json = nlohmann::json;

std::string TtstrToUtf8(const ttstr &value) {
    const tjs_char *src = value.c_str();
    tjs_int len = TVPWideCharToUtf8String(src, nullptr);
    if(len <= 0)
        return {};

    std::string out(static_cast<size_t>(len), '\0');
    TVPWideCharToUtf8String(src, out.data());
    return out;
}

ttstr Utf8ToTtstr(const std::string &value) {
    if(value.empty())
        return ttstr(TJS_W(""));

    tjs_int len = TVPUtf8ToWideCharString(
        value.data(), static_cast<tjs_uint>(value.size()), nullptr);
    if(len < 0)
        return ttstr(value.c_str());

    std::vector<tjs_char> out(static_cast<size_t>(len) + 1, 0);
    len = TVPUtf8ToWideCharString(
        value.data(), static_cast<tjs_uint>(value.size()), out.data());
    if(len < 0)
        return ttstr(value.c_str());

    out[static_cast<size_t>(len)] = 0;
    return ttstr(out.data());
}

[[noreturn]] void ThrowJsonError(const char *prefix, const std::string &detail) {
    ttstr msg(TJS_W("json.dll: "));
    msg += ttstr(prefix);
    if(!detail.empty()) {
        msg += TJS_W(": ");
        msg += Utf8ToTtstr(detail);
    }
    TVPThrowExceptionMessage(msg.c_str());
    throw std::runtime_error("unreachable");
}

[[noreturn]] void ThrowJsonError(const char *prefix,
                                 const std::exception &error) {
    ThrowJsonError(prefix, error.what() ? std::string(error.what())
                                        : std::string());
}

bool ParamAsBool(tTJSVariant **param, tjs_int index, tjs_int numparams,
                 bool fallback) {
    if(index >= numparams || !param[index] || param[index]->Type() == tvtVoid)
        return fallback;
    return param[index]->AsInteger() != 0;
}

tjs_int ParamAsInt(tTJSVariant **param, tjs_int index, tjs_int numparams,
                   tjs_int fallback) {
    if(index >= numparams || !param[index] || param[index]->Type() == tvtVoid)
        return fallback;
    return static_cast<tjs_int>(param[index]->AsInteger());
}

void StripUtf8Bom(std::string &text) {
    if(text.size() >= 3 &&
       static_cast<unsigned char>(text[0]) == 0xef &&
       static_cast<unsigned char>(text[1]) == 0xbb &&
       static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }
}

bool IsIdentifierChar(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

bool MatchesToken(const std::string &text, size_t pos, const char *token) {
    const size_t len = std::char_traits<char>::length(token);
    if(pos + len > text.size() || text.compare(pos, len, token) != 0)
        return false;
    const bool before =
        pos > 0 && IsIdentifierChar(text[static_cast<size_t>(pos - 1)]);
    const bool after =
        pos + len < text.size() && IsIdentifierChar(text[pos + len]);
    return !before && !after;
}

std::string NormalizeRelaxedJson(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    bool inDouble = false;
    bool inSingle = false;
    bool escaped = false;

    for(size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];

        if(inDouble) {
            out += ch;
            if(escaped) {
                escaped = false;
            } else if(ch == '\\') {
                escaped = true;
            } else if(ch == '"') {
                inDouble = false;
            }
            continue;
        }

        if(inSingle) {
            if(escaped) {
                if(ch == '\'') {
                    out += '\'';
                } else if(ch == '"') {
                    out += "\\\"";
                } else {
                    out += '\\';
                    out += ch;
                }
                escaped = false;
            } else if(ch == '\\') {
                escaped = true;
            } else if(ch == '\'') {
                out += '"';
                inSingle = false;
            } else if(ch == '"') {
                out += "\\\"";
            } else {
                out += ch;
            }
            continue;
        }

        if(ch == '"') {
            inDouble = true;
            out += ch;
        } else if(ch == '\'') {
            inSingle = true;
            out += '"';
        } else if(ch == '#') {
            while(i < text.size() && text[i] != '\n' && text[i] != '\r')
                ++i;
            if(i < text.size())
                out += text[i];
        } else if(ch == ';') {
            out += ',';
        } else if(ch == '=') {
            if(i + 1 < text.size() && text[i + 1] == '>')
                ++i;
            out += ':';
        } else if(MatchesToken(text, i, "void") ||
                  MatchesToken(text, i, "nil")) {
            out += "null";
            i += text[i] == 'n' ? 2 : 3;
        } else {
            out += ch;
        }
    }

    std::string trimmed;
    trimmed.reserve(out.size());
    inDouble = false;
    escaped = false;
    for(char ch : out) {
        if(inDouble) {
            trimmed += ch;
            if(escaped) {
                escaped = false;
            } else if(ch == '\\') {
                escaped = true;
            } else if(ch == '"') {
                inDouble = false;
            }
            continue;
        }

        if(ch == '"') {
            inDouble = true;
            trimmed += ch;
        } else if(ch == '}' || ch == ']') {
            size_t pos = trimmed.size();
            while(pos > 0 &&
                  (trimmed[pos - 1] == ' ' || trimmed[pos - 1] == '\t' ||
                   trimmed[pos - 1] == '\n' || trimmed[pos - 1] == '\r')) {
                --pos;
            }
            if(pos > 0 && trimmed[pos - 1] == ',')
                trimmed.erase(pos - 1, 1);
            trimmed += ch;
        } else {
            trimmed += ch;
        }
    }
    return trimmed;
}

Json ParseJsonText(std::string text) {
    StripUtf8Bom(text);
    try {
        return Json::parse(text, nullptr, true, true);
    } catch(const std::exception &e) {
        std::string firstError = e.what() ? e.what() : "";
        std::string relaxed = NormalizeRelaxedJson(text);
        if(relaxed != text) {
            try {
                return Json::parse(relaxed, nullptr, true, true);
            } catch(const std::exception &fallbackError) {
                firstError += " / relaxed parse failed: ";
                firstError += fallbackError.what() ? fallbackError.what() : "";
            }
        }
        ThrowJsonError("parse failed", firstError);
    }
}

ttstr LoadTextStorage(const ttstr &storage) {
    std::unique_ptr<iTJSTextReadStream, void (*)(iTJSTextReadStream *)> stream(
        TVPCreateTextStreamForRead(storage, TJS_W("")),
        [](iTJSTextReadStream *s) {
            if(s)
                s->Destruct();
        });

    ttstr out;
    for(;;) {
        ttstr chunk;
        tjs_uint read = stream->Read(chunk, 4096);
        if(read == 0)
            break;
        out += chunk;
    }
    return out;
}

std::string LoadUtf8Storage(const ttstr &storage) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateBinaryStreamForRead(storage, TJS_W("")));
    const tjs_uint64 size64 = stream->GetSize();
    if(size64 > static_cast<tjs_uint64>(std::numeric_limits<size_t>::max()))
        ThrowJsonError("file too large", TtstrToUtf8(storage));
    if(size64 > static_cast<tjs_uint64>(std::numeric_limits<tjs_uint>::max()))
        ThrowJsonError("file too large", TtstrToUtf8(storage));

    std::string out(static_cast<size_t>(size64), '\0');
    if(!out.empty())
        stream->ReadBuffer(out.data(), static_cast<tjs_uint>(out.size()));
    return out;
}

void SaveBytesStorage(const ttstr &storage, const std::string &bytes) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateBinaryStreamForWrite(storage, TJS_W("")));
    if(!bytes.empty())
        stream->WriteBuffer(bytes.data(), static_cast<tjs_uint>(bytes.size()));
}

void JsonToVariant(const Json &json, tTJSVariant &out) {
    if(json.is_null()) {
        out.Clear();
    } else if(json.is_boolean()) {
        out = json.get<bool>();
    } else if(json.is_number_integer()) {
        out = static_cast<tjs_int64>(json.get<Json::number_integer_t>());
    } else if(json.is_number_unsigned()) {
        auto value = json.get<Json::number_unsigned_t>();
        if(value <= static_cast<Json::number_unsigned_t>(
                        std::numeric_limits<tjs_int64>::max())) {
            out = static_cast<tjs_int64>(value);
        } else {
            out = static_cast<tjs_real>(value);
        }
    } else if(json.is_number_float()) {
        out = static_cast<tjs_real>(json.get<double>());
    } else if(json.is_string()) {
        out = Utf8ToTtstr(json.get_ref<const std::string &>());
    } else if(json.is_array()) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        for(size_t i = 0; i < json.size(); ++i) {
            tTJSVariant value;
            JsonToVariant(json[i], value);
            array->PropSetByNum(TJS_MEMBERENSURE, static_cast<tjs_int>(i),
                                &value, array);
        }
        out = tTJSVariant(array, array);
        array->Release();
    } else if(json.is_object()) {
        iTJSDispatch2 *dict = TJSCreateDictionaryObject();
        for(const auto &item : json.items()) {
            ttstr key = Utf8ToTtstr(item.key());
            tTJSVariant value;
            JsonToVariant(item.value(), value);
            dict->PropSet(TJS_MEMBERENSURE, key.c_str(), nullptr, &value,
                          dict);
        }
        out = tTJSVariant(dict, dict);
        dict->Release();
    } else {
        out.Clear();
    }
}

struct ToJsonContext {
    std::unordered_set<const iTJSDispatch2 *> visiting;
    tjs_int depth = 0;
};

struct VisitGuard {
    ToJsonContext &context;
    const iTJSDispatch2 *object;
    bool inserted = false;

    VisitGuard(ToJsonContext &context, const iTJSDispatch2 *object) :
        context(context), object(object) {
        inserted = context.visiting.insert(object).second;
        if(inserted)
            ++context.depth;
    }

    ~VisitGuard() {
        if(inserted) {
            context.visiting.erase(object);
            --context.depth;
        }
    }
};

Json VariantToJson(const tTJSVariant &value, ToJsonContext &context);

tjs_int GetArrayCount(iTJSDispatch2 *array) {
    tTJSVariant count;
    if(TJS_SUCCEEDED(array->PropGet(TJS_IGNOREPROP, TJS_W("count"), nullptr,
                                    &count, array))) {
        const tjs_int64 raw = count.AsInteger();
        if(raw > 0)
            return raw > std::numeric_limits<tjs_int>::max()
                       ? std::numeric_limits<tjs_int>::max()
                       : static_cast<tjs_int>(raw);
    }
    return 0;
}

Json ArrayToJson(iTJSDispatch2 *array, ToJsonContext &context) {
    Json out = Json::array();
    const tjs_int count = GetArrayCount(array);
    for(tjs_int i = 0; i < count; ++i) {
        tTJSVariant item;
        if(TJS_SUCCEEDED(
               array->PropGetByNum(TJS_IGNOREPROP, i, &item, array))) {
            out.push_back(VariantToJson(item, context));
        } else {
            out.push_back(nullptr);
        }
    }
    return out;
}

class JsonMemberCollector : public tTJSDispatch {
public:
    JsonMemberCollector(Json &out, ToJsonContext &context) :
        out(out), context(context) {}

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag,
                                       const tjs_char *membername,
                                       tjs_uint32 *hint, tTJSVariant *result,
                                       tjs_int numparams,
                                       tTJSVariant **param,
                                       iTJSDispatch2 *objthis) override {
        if(numparams > 2) {
            const tTVInteger memberFlags = param[1]->AsInteger();
            if((memberFlags & TJS_HIDDENMEMBER) == 0) {
                std::string key;
                if(param[0]->GetString())
                    key = TtstrToUtf8(ttstr(param[0]->GetString()));
                out[key] = VariantToJson(*param[2], context);
            }
        }
        if(result)
            *result = true;
        return TJS_S_OK;
    }

private:
    Json &out;
    ToJsonContext &context;
};

Json ObjectToJson(iTJSDispatch2 *object, ToJsonContext &context) {
    Json out = Json::object();
    JsonMemberCollector collector(out, context);
    tTJSVariantClosure closure(&collector, nullptr);
    object->EnumMembers(TJS_IGNOREPROP, &closure, object);
    return out;
}

Json VariantToJson(const tTJSVariant &value, ToJsonContext &context) {
    switch(value.Type()) {
        case tvtVoid:
            return nullptr;
        case tvtString:
            return TtstrToUtf8(ttstr(value.GetString()));
        case tvtInteger:
            return static_cast<tjs_int64>(value.AsInteger());
        case tvtReal: {
            const tjs_real real = value.AsReal();
            if(!std::isfinite(real))
                return nullptr;
            return real;
        }
        case tvtObject: {
            iTJSDispatch2 *object = value.AsObjectNoAddRef();
            if(!object)
                return nullptr;

            VisitGuard guard(context, object);
            if(!guard.inserted || context.depth > 128)
                return nullptr;

            if(object->IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr,
                                    TJS_W("Array"), object) == TJS_S_TRUE) {
                return ArrayToJson(object, context);
            }
            return ObjectToJson(object, context);
        }
        case tvtOctet:
        default:
            return nullptr;
    }
}

std::string DumpJson(const tTJSVariant &value, tjs_int newline) {
    ToJsonContext context;
    std::string text = VariantToJson(value, context).dump(1, ' ', false);
    if(newline == 0) {
        std::string converted;
        converted.reserve(text.size() + 16);
        for(char ch : text) {
            if(ch == '\n')
                converted += '\r';
            converted += ch;
        }
        text.swap(converted);
    }
    return text;
}

} // namespace

struct Scripts {
    static tjs_error evalJSON(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result) {
            Json json = ParseJsonText(TtstrToUtf8(ttstr(*param[0])));
            JsonToVariant(json, *result);
        }
        return TJS_S_OK;
    }

    static tjs_error evalJSONStorage(tTJSVariant *result, tjs_int numparams,
                                     tTJSVariant **param,
                                     iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result) {
            const ttstr storage = ttstr(*param[0]);
            const bool utf8 = ParamAsBool(param, 1, numparams, false);
            const std::string text =
                utf8 ? LoadUtf8Storage(storage)
                     : TtstrToUtf8(LoadTextStorage(storage));
            Json json = ParseJsonText(text);
            JsonToVariant(json, *result);
        }
        return TJS_S_OK;
    }

    static tjs_error saveJSON(tTJSVariant *result, tjs_int numparams,
                              tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        const ttstr storage = ttstr(*param[0]);
        const bool utf8 = ParamAsBool(param, 2, numparams, false);
        const tjs_int newline = ParamAsInt(param, 3, numparams, 0);
        const std::string text = DumpJson(*param[1], newline);

        if(utf8) {
            SaveBytesStorage(storage, text);
        } else {
            const ttstr wide = Utf8ToTtstr(text);
            const tjs_int len = wide.GetNarrowStrLen();
            if(len < 0)
                ThrowJsonError("narrow output conversion failed",
                               TtstrToUtf8(storage));
            std::vector<char> buffer(static_cast<size_t>(len) + 1, 0);
            if(len > 0)
                wide.ToNarrowStr(buffer.data(), len);
            std::string narrow(buffer.data(), static_cast<size_t>(len));
            SaveBytesStorage(storage, narrow);
        }

        if(result)
            *result = true;
        return TJS_S_OK;
    }

    static tjs_error toJSONString(tTJSVariant *result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        if(result) {
            const tjs_int newline = ParamAsInt(param, 1, numparams, 0);
            *result = Utf8ToTtstr(DumpJson(*param[0], newline));
        }
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(Scripts, Scripts) {
    RawCallback("evalJSON", &Scripts::evalJSON, TJS_STATICMEMBER);
    RawCallback("evalJSONStorage", &Scripts::evalJSONStorage,
                TJS_STATICMEMBER);
    RawCallback("saveJSON", &Scripts::saveJSON, TJS_STATICMEMBER);
    RawCallback("toJSONString", &Scripts::toJSONString, TJS_STATICMEMBER);
}
