#include "ncbind.hpp"
#include "CharacterSet.h"
#include "StorageIntf.h"

#include <algorithm>
#include <memory>
#include <vector>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("lineParser.dll")

namespace {

ttstr Utf8ToTtstr(const char *data, tjs_uint size) {
    if(!data || size == 0)
        return ttstr(TJS_W(""));
    tjs_int len = TVPUtf8ToWideCharString(data, size, nullptr);
    if(len <= 0)
        return ttstr(TJS_W(""));
    std::vector<tjs_char> out(static_cast<size_t>(len) + 1, 0);
    TVPUtf8ToWideCharString(data, size, out.data());
    return ttstr(out.data());
}

ttstr LoadTextStorage(const ttstr &filename, bool utf8) {
    std::unique_ptr<tTJSBinaryStream> stream(
        TVPCreateStream(filename, TJS_BS_READ));
    if(!stream) {
        TVPThrowExceptionMessage(
            (ttstr(TJS_W("cannot open : ")) + filename).c_str());
    }

    tjs_uint64 total64 = stream->GetSize();
    tjs_uint total =
        static_cast<tjs_uint>(std::min<tjs_uint64>(total64, 0x7fffffff));
    std::vector<char> buffer(static_cast<size_t>(total) + 1, 0);
    tjs_uint read = total ? stream->Read(buffer.data(), total) : 0;
    if(utf8)
        return Utf8ToTtstr(buffer.data(), read);
    return ttstr(buffer.data());
}

std::vector<ttstr> SplitLines(const ttstr &text) {
    std::vector<ttstr> lines;
    ttstr current;
    const tjs_char cr = static_cast<tjs_char>('\r');
    const tjs_char lf = static_cast<tjs_char>('\n');
    bool endedWithEol = false;

    for(tjs_uint i = 0; i < text.length(); ++i) {
        tjs_char ch = text[i];
        if(ch == cr || ch == lf) {
            lines.push_back(current);
            current.Clear();
            endedWithEol = true;
            if(ch == cr && i + 1 < text.length() && text[i + 1] == lf)
                ++i;
            continue;
        }
        current += ch;
        endedWithEol = false;
    }

    if(current.length() > 0 || (text.length() > 0 && !endedWithEol))
        lines.push_back(current);
    return lines;
}

} // namespace

class LineParserCompat {
public:
    LineParserCompat() = default;

    ~LineParserCompat() {
        if(target_)
            target_->Release();
        if(selfObject_)
            selfObject_->Release();
    }

    static tjs_error factory(LineParserCompat **result, tjs_int numparams,
                             tTJSVariant **param, iTJSDispatch2 *) {
        if(!result)
            return TJS_S_OK;
        LineParserCompat *self = new LineParserCompat();
        self->selfObject_ = objthis;
        if(self->selfObject_)
            self->selfObject_->AddRef();
        if(numparams > 0 && param && param[0] &&
           param[0]->Type() == tvtObject) {
            self->target_ = param[0]->AsObject();
        }
        *result = self;
        return TJS_S_OK;
    }

    void init(ttstr text) {
        lines_ = SplitLines(text);
        next_ = 0;
        currentLineNumber_ = 0;
    }

    void initStorage(ttstr filename, bool utf8 = false) {
        init(LoadTextStorage(filename, utf8));
    }

    tjs_int getCurrentLineNumber() const { return currentLineNumber_; }

    static tjs_error TJS_INTF_METHOD getNextLine(tTJSVariant *result, tjs_int,
                                                 tTJSVariant **,
                                                 LineParserCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(self->next_ >= self->lines_.size()) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        if(result)
            *result = self->lines_[self->next_];
        ++self->next_;
        ++self->currentLineNumber_;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD parse(tTJSVariant *result,
                                           tjs_int numparams,
                                           tTJSVariant **param,
                                           LineParserCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams > 0 && param && param[0] && param[0]->Type() != tvtVoid)
            self->init(ttstr(*param[0]));
        self->parseCurrent();
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD parseStorage(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  LineParserCompat *self) {
        if(!self)
            return TJS_E_NATIVECLASSCRASH;
        if(numparams < 1 || !param || !param[0])
            return TJS_E_BADPARAMCOUNT;
        bool utf8 = numparams > 1 && param[1] && param[1]->AsInteger() != 0;
        self->initStorage(ttstr(*param[0]), utf8);
        self->parseCurrent();
        if(result)
            result->Clear();
        return TJS_S_OK;
    }

private:
    void parseCurrent() {
        iTJSDispatch2 *target = target_ ? target_ : selfObject_;
        if(!target ||
           target->IsValid(TJS_IGNOREPROP, TJS_W("doLine"), nullptr,
                           target) != TJS_S_TRUE) {
            return;
        }

        ttstr line;
        tTJSVariant ignored;
        while(next_ < lines_.size()) {
            line = lines_[next_++];
            ++currentLineNumber_;
            tTJSVariant vline(line);
            tTJSVariant vlineNo(currentLineNumber_);
            tTJSVariant *params[] = { &vline, &vlineNo };
            target->FuncCall(0, TJS_W("doLine"), nullptr, &ignored, 2, params,
                             target);
        }
    }

    iTJSDispatch2 *target_ = nullptr;
    iTJSDispatch2 *selfObject_ = nullptr;
    std::vector<ttstr> lines_;
    size_t next_ = 0;
    tjs_int currentLineNumber_ = 0;
};

NCB_REGISTER_CLASS_DIFFER(LineParser, LineParserCompat) {
    Factory(&LineParserCompat::factory);
    NCB_METHOD(init);
    NCB_METHOD(initStorage);
    RawCallback(TJS_W("getNextLine"), &Class::getNextLine, 0);
    RawCallback(TJS_W("parse"), &Class::parse, 0);
    RawCallback(TJS_W("parseStorage"), &Class::parseStorage, 0);
    NCB_PROPERTY_RO(currentLineNumber, getCurrentLineNumber);
}
