#include "ncbind.hpp"
#include "CharacterSet.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "qrcode/QR_Encode.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include "KrkrJniHelper.h"
using JniHelper = krkr::JniHelper;
#endif

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

std::string TtstrToUtf8(const ttstr &value) {
    const tjs_char *src = value.c_str();
    tjs_int len = TVPWideCharToUtf8String(src, nullptr);
    if(len >= 0) {
        std::string out(static_cast<size_t>(len), '\0');
        TVPWideCharToUtf8String(src, out.data());
        return out;
    }
    return value.AsNarrowStdString();
}

tjs_error ReturnVoid(tTJSVariant *result) {
    if(result)
        result->Clear();
    return TJS_S_OK;
}

void AndroidShowPluginToast(const ttstr &title, const ttstr &message) {
#if defined(__ANDROID__)
    JniMethodInfo methodInfo;
    if(JniHelper::getStaticMethodInfo(
           methodInfo, "org/tvp/kirikiri2/KR2Activity", "ShowPluginToast",
           "(Ljava/lang/String;Ljava/lang/String;)V")) {
        const std::string titleUtf8 = TtstrToUtf8(title);
        const std::string messageUtf8 = TtstrToUtf8(message);
        jstring jTitle = methodInfo.env->NewStringUTF(titleUtf8.c_str());
        jstring jMessage = methodInfo.env->NewStringUTF(messageUtf8.c_str());
        methodInfo.env->CallStaticVoidMethod(methodInfo.classID,
                                             methodInfo.methodID, jTitle,
                                             jMessage);
        methodInfo.env->DeleteLocalRef(jTitle);
        methodInfo.env->DeleteLocalRef(jMessage);
        methodInfo.env->DeleteLocalRef(methodInfo.classID);
        return;
    }
#else
    (void)title;
    (void)message;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// qrcode.dll
// Original plugin adds Layer.drawQRCode(value, ecLevel, qrVersion,
// autoExtent, maskPattern). Keep the same surface and draw directly into the
// layer buffer so games that render QR codes into their own UI still work.
// ---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("qrcode.dll")

class LayerQRCodeCompat {
public:
    static tjs_error TJS_INTF_METHOD drawQRCode(tTJSVariant *result,
                                                tjs_int numparams,
                                                tTJSVariant **param,
                                                iTJSDispatch2 *objthis) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if(!objthis)
            return TJS_E_FAIL;

        const ttstr text = ttstr(*param[0]);
        const std::string bytes = TtstrToUtf8(text);
        const tjs_int ecLevel =
            numparams > 1 && param[1]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[1]->AsInteger())
                : QR_LEVEL_L;
        const tjs_int qrVersion =
            numparams > 2 && param[2]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[2]->AsInteger())
                : QR_VRESION_S;
        const BOOL autoExtent =
            numparams > 3 && param[3]->Type() != tvtVoid
                ? (param[3]->AsInteger() != 0)
                : TRUE;
        const tjs_int maskPattern =
            numparams > 4 && param[4]->Type() != tvtVoid
                ? static_cast<tjs_int>(param[4]->AsInteger())
                : -1;

        CQR_Encode encoder;
        if(!encoder.EncodeData(static_cast<int>(ecLevel),
                               static_cast<int>(qrVersion), autoExtent,
                               static_cast<int>(maskPattern), bytes.c_str(),
                               static_cast<int>(bytes.size()))) {
            if(result)
                *result = TJS_W("QR code data is empty or too large");
            return TJS_S_OK;
        }

        const tjs_int symbolSize = encoder.m_nSymbleSize;
        const tjs_int whSize = symbolSize + QR_MARGIN * 2;
        tTJSVariant width(whSize);
        tTJSVariant height(whSize);
        tTJSVariant *resizeParams[] = { &width, &height };
        if(TJS_FAILED(objthis->FuncCall(0, TJS_W("setImageSize"), nullptr,
                                        nullptr, 2, resizeParams, objthis))) {
            return TJS_E_FAIL;
        }

        tTJSVariant val;
        if(TJS_FAILED(objthis->PropGet(0, TJS_W("mainImageBufferPitch"),
                                       nullptr, &val, objthis))) {
            return TJS_E_FAIL;
        }
        const tjs_int pitch = static_cast<tjs_int>(val.AsInteger());
        if(TJS_FAILED(objthis->PropGet(0, TJS_W("mainImageBufferForWrite"),
                                       nullptr, &val, objthis))) {
            return TJS_E_FAIL;
        }
        auto *buffer =
            reinterpret_cast<tjs_uint8 *>(static_cast<tjs_intptr_t>(
                val.AsInteger()));
        if(!buffer || pitch <= 0)
            return TJS_E_FAIL;

        for(tjs_int y = 0; y < whSize; ++y) {
            auto *line =
                reinterpret_cast<tjs_uint32 *>(buffer + y * pitch);
            for(tjs_int x = 0; x < whSize; ++x) {
                bool dark = false;
                if(x >= QR_MARGIN && x < QR_MARGIN + symbolSize &&
                   y >= QR_MARGIN && y < QR_MARGIN + symbolSize) {
                    dark =
                        encoder.m_byModuleData[x - QR_MARGIN][y - QR_MARGIN] !=
                        0;
                }
                line[x] = dark ? 0xff000000u : 0xffffffffu;
            }
        }

        tTJSVariant left(0);
        tTJSVariant top(0);
        tTJSVariant updateWidth(whSize);
        tTJSVariant updateHeight(whSize);
        tTJSVariant *updateParams[] = { &left, &top, &updateWidth,
                                        &updateHeight };
        objthis->FuncCall(0, TJS_W("update"), nullptr, nullptr, 4,
                          updateParams, objthis);

        return ReturnVoid(result);
    }
};

NCB_ATTACH_CLASS(LayerQRCodeCompat, Layer) {
    RawCallback(TJS_W("drawQRCode"), &Class::drawQRCode, 0);
}

// ---------------------------------------------------------------------------
// registory.dll
// Windows registry compatibility. There is no registry on Android; keep the
// call shape and retain values in-process so write/delete calls do not trip
// game initialization branches.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("registory.dll")

class SystemRegistoryCompat {
public:
    static tjs_error TJS_INTF_METHOD writeRegValue(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        Registry()[Key(param[0])] = TtstrToUtf8(ttstr(*param[1]));
        return ReturnVoid(result);
    }

    static tjs_error TJS_INTF_METHOD deleteRegValue(tTJSVariant *result,
                                                    tjs_int numparams,
                                                    tTJSVariant **param,
                                                    iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        Registry().erase(Key(param[0]));
        return ReturnVoid(result);
    }

    static tjs_error TJS_INTF_METHOD deleteRegKey(tTJSVariant *result,
                                                  tjs_int numparams,
                                                  tTJSVariant **param,
                                                  iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        const std::string prefix = Key(param[0]);
        auto &registry = Registry();
        for(auto it = registry.begin(); it != registry.end();) {
            if(it->first == prefix || it->first.rfind(prefix + "\\", 0) == 0)
                it = registry.erase(it);
            else
                ++it;
        }
        return ReturnVoid(result);
    }

private:
    static std::string Key(tTJSVariant *value) {
        return value ? TtstrToUtf8(ttstr(*value)) : std::string();
    }

    static std::map<std::string, std::string> &Registry() {
        static std::map<std::string, std::string> registry;
        return registry;
    }
};

NCB_ATTACH_CLASS(SystemRegistoryCompat, System) {
    RawCallback(TJS_W("writeRegValue"), &Class::writeRegValue,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("deleteRegValue"), &Class::deleteRegValue,
                TJS_STATICMEMBER);
    RawCallback(TJS_W("deleteRegKey"), &Class::deleteRegKey,
                TJS_STATICMEMBER);
}

// ---------------------------------------------------------------------------
// tasktray.dll
// Android has no Windows task tray. Preserve Window properties/methods and map
// balloon info to a non-blocking Android toast when available.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("tasktray.dll")

class WindowTasktrayCompat {
public:
    explicit WindowTasktrayCompat(iTJSDispatch2 *) {}

    void setTasktrayEnable(bool enable) { enabled_ = enable; }
    bool getTasktrayEnable() const { return enabled_; }

    void setTasktrayHint(ttstr text) { hint_ = text; }
    ttstr getTasktrayHint() const { return hint_; }

    void popupTasktrayInfo(tjs_int, ttstr title, ttstr message, tjs_int) {
        if(title.IsEmpty())
            title = hint_;
        AndroidShowPluginToast(title, message);
        lastTitle_ = title;
        lastMessage_ = message;
    }

private:
    bool enabled_ = false;
    ttstr hint_;
    ttstr lastTitle_;
    ttstr lastMessage_;
};

void TasktrayPreRegistCallback() {
    TVPExecuteExpression(TJS_W("global.niifNone = 0"));
    TVPExecuteExpression(TJS_W("global.niifInfo = 1"));
    TVPExecuteExpression(TJS_W("global.niifWarning = 2"));
    TVPExecuteExpression(TJS_W("global.niifError = 3"));
}

void TasktrayPostUnregistCallback() {
    TVPExecuteScript(TJS_W("delete global[\"niifNone\"];"));
    TVPExecuteScript(TJS_W("delete global[\"niifInfo\"];"));
    TVPExecuteScript(TJS_W("delete global[\"niifWarning\"];"));
    TVPExecuteScript(TJS_W("delete global[\"niifError\"];"));
}

NCB_GET_INSTANCE_HOOK(WindowTasktrayCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowTasktrayCompat, Window) {
    NCB_PROPERTY(tasktrayEnable, getTasktrayEnable, setTasktrayEnable);
    NCB_PROPERTY(tasktrayHint, getTasktrayHint, setTasktrayHint);
    NCB_METHOD(popupTasktrayInfo);
}

NCB_PRE_REGIST_CALLBACK(TasktrayPreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(TasktrayPostUnregistCallback);
