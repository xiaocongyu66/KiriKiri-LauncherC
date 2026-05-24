//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "Plugins" class implementation / Service for plug-ins
//---------------------------------------------------------------------------
#include <set>
#include <algorithm>
#include <functional>

#include <spdlog/spdlog.h>

#include "tjsCommHead.h"

#include "ScriptMgnIntf.h"
#include "PluginImpl.h"

#include "StorageImpl.h"

#include "EventIntf.h"
#include "TransIntf.h"
#include "tjsArray.h"
#include "DebugIntf.h"

#include "tjs.h"
#include "tjsConfig.h"
#include "ncbind.hpp"

#ifdef TVP_SUPPORT_KPI
#include "kmp_pi.h"
#endif

#include "FilePathUtil.h"
#include "Application.h"
#include "SysInitImpl.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

//---------------------------------------------------------------------------
// gamepad.dll の代替スタブ実装
//
// 公式の gamepad.dll (x-row.net 製) は DirectInput / XInput を使う
// Windows 専用プラグイン。Android では読み込めないため、TJS グローバルに
// 「GamepadPort」「Gamepad」両クラスを登録し、count=0 / 各種カウント=0 を
// 返す無効デバイスとして振る舞わせる。エラー
//   "Member \"count\" does not exist @line(1) exgamepad.tjs"
// は、このスタブが GamepadPort.count を実装していないことが原因。
//
// 対象 API は以下のソースに基づく（NCB バインディング）:
//   /storage/emulated/0/逆向/plugin/gamepad-master/Main.cpp
//---------------------------------------------------------------------------
class tTJSNI_GamepadPortStub : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        return TJS_S_OK;
    }
    void Invalidate() override {}
};

class tTJSNC_GamepadPortStub : public tTJSNativeClass {
public:
    static tjs_uint32 ClassID;

    tTJSNC_GamepadPortStub() : tTJSNativeClass(TJS_W("GamepadPort")) {
        TJS_BEGIN_NATIVE_MEMBERS(GamepadPort)
        TJS_DECL_EMPTY_FINALIZE_METHOD

        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, tTJSNI_GamepadPortStub,
                                          GamepadPort) {
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(GamepadPort)

        // initialize(window) — 実機ではコントローラ列挙。スタブは何もしない
        TJS_BEGIN_NATIVE_METHOD_DECL(initialize) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(initialize)

        // getController(index) — 接続されていない: void を返す
        TJS_BEGIN_NATIVE_METHOD_DECL(getController) {
            if(result)
                result->Clear();
            return TJS_S_OK;
        }
        TJS_END_NATIVE_METHOD_DECL(getController)

        // count（読み出し専用プロパティ）— 接続コントローラ数 = 0
        TJS_BEGIN_NATIVE_PROP_DECL(count) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                if(result)
                    *result = (tjs_int)0;
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(count)

        TJS_END_NATIVE_MEMBERS
    }

protected:
    tTJSNativeInstance *CreateNativeInstance() override {
        return new tTJSNI_GamepadPortStub();
    }
};

tjs_uint32 tTJSNC_GamepadPortStub::ClassID = static_cast<tjs_uint32>(-1);

//---------------------------------------------------------------------------
class tTJSNI_GamepadStub : public tTJSNativeInstance {
public:
    tjs_error Construct(tjs_int, tTJSVariant **, iTJSDispatch2 *) override {
        return TJS_S_OK;
    }
    void Invalidate() override {}
};

class tTJSNC_GamepadStub : public tTJSNativeClass {
public:
    static tjs_uint32 ClassID;

    tTJSNC_GamepadStub() : tTJSNativeClass(TJS_W("Gamepad")) {
        TJS_BEGIN_NATIVE_MEMBERS(Gamepad)
        TJS_DECL_EMPTY_FINALIZE_METHOD

        TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(_this, tTJSNI_GamepadStub, Gamepad) {
            return TJS_S_OK;
        }
        TJS_END_NATIVE_CONSTRUCTOR_DECL(Gamepad)

        // update() — 入力コンテキスト更新。スタブは何もしない
        TJS_BEGIN_NATIVE_METHOD_DECL(update) { return TJS_S_OK; }
        TJS_END_NATIVE_METHOD_DECL(update)

        // 0 を返すだけのカウント／状態系プロパティをまとめて生成するマクロ
#define TVP_GAMEPAD_STUB_INT_RO_PROP(propname)                                 \
    TJS_BEGIN_NATIVE_PROP_DECL(propname) {                                     \
        TJS_BEGIN_NATIVE_PROP_GETTER {                                         \
            if(result)                                                         \
                *result = (tjs_int)0;                                          \
            return TJS_S_OK;                                                   \
        }                                                                      \
        TJS_END_NATIVE_PROP_GETTER                                             \
        TJS_DENY_NATIVE_PROP_SETTER                                            \
    }                                                                          \
    TJS_END_NATIVE_PROP_DECL(propname)

#define TVP_GAMEPAD_STUB_REAL_RO_PROP(propname)                                \
    TJS_BEGIN_NATIVE_PROP_DECL(propname) {                                     \
        TJS_BEGIN_NATIVE_PROP_GETTER {                                         \
            if(result)                                                         \
                *result = (tjs_real)0.0;                                       \
            return TJS_S_OK;                                                   \
        }                                                                      \
        TJS_END_NATIVE_PROP_GETTER                                             \
        TJS_DENY_NATIVE_PROP_SETTER                                            \
    }                                                                          \
    TJS_END_NATIVE_PROP_DECL(propname)

        // 文字列プロパティ: name
        TJS_BEGIN_NATIVE_PROP_DECL(name) {
            TJS_BEGIN_NATIVE_PROP_GETTER {
                if(result)
                    *result = ttstr(TJS_W(""));
                return TJS_S_OK;
            }
            TJS_END_NATIVE_PROP_GETTER
            TJS_DENY_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(name)

        // int 系（type, keyState）
        TVP_GAMEPAD_STUB_INT_RO_PROP(type)
        TVP_GAMEPAD_STUB_INT_RO_PROP(keyState)

        // real 系（アナログ入力）
        TVP_GAMEPAD_STUB_REAL_RO_PROP(leftTrigger)
        TVP_GAMEPAD_STUB_REAL_RO_PROP(rightTrigger)
        TVP_GAMEPAD_STUB_REAL_RO_PROP(leftThumbStickX)
        TVP_GAMEPAD_STUB_REAL_RO_PROP(leftThumbStickY)
        TVP_GAMEPAD_STUB_REAL_RO_PROP(rightThumbStickX)
        TVP_GAMEPAD_STUB_REAL_RO_PROP(rightThumbStickY)

        // 振動セッター（書き込みのみ）
        TJS_BEGIN_NATIVE_PROP_DECL(leftVibration) {
            TJS_DENY_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(leftVibration)
        TJS_BEGIN_NATIVE_PROP_DECL(rightVibration) {
            TJS_DENY_NATIVE_PROP_GETTER
            TJS_BEGIN_NATIVE_PROP_SETTER { return TJS_S_OK; }
            TJS_END_NATIVE_PROP_SETTER
        }
        TJS_END_NATIVE_PROP_DECL(rightVibration)

        // アナログ／デジタル方向キー、各ボタンの押下回数 (全て 0)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogLeftUpCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogLeftDownCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogLeftLeftCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogLeftRightCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogRightUpCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogRightDownCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogRightLeftCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(analogRightRightCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(degitalUpCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(degitalDownCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(degitalLeftCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(degitalRightCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonStartCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonBackCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonLeftThumbCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonRightThumbCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonLeftShoulderCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonLeftTriggerCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonRightShoulderCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonRightTriggerCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonACount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonBCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonXCount)
        TVP_GAMEPAD_STUB_INT_RO_PROP(buttonYCount)

#undef TVP_GAMEPAD_STUB_INT_RO_PROP
#undef TVP_GAMEPAD_STUB_REAL_RO_PROP

        TJS_END_NATIVE_MEMBERS
    }

protected:
    tTJSNativeInstance *CreateNativeInstance() override {
        return new tTJSNI_GamepadStub();
    }
};

tjs_uint32 tTJSNC_GamepadStub::ClassID = static_cast<tjs_uint32>(-1);

// gamepad.dll が定義する gp* 系グローバル定数。exgamepad.tjs は
// gpButtonA / gpLeftTrigger などの数値をマスクとして使用するため、未定義の
// ままだと TJS スクリプト側で ReferenceError が発生する。
static void TVPRegisterGamepadConstants() {
    static const tjs_char *const expression =
        TJS_W("const gpDInput = 3, gpFFDInput = 2, gpXInput = 1,")
        TJS_W("gpButtonDpadUp = 0x00000001, gpButtonDpadDown = 0x00000002,")
        TJS_W("gpButtonDpadLeft = 0x00000004, gpButtonDpadRight = 0x00000008,")
        TJS_W("gpButtonStart = 0x00000010, gpButtonBack = 0x00000020,")
        TJS_W("gpButtonLeftThumb = 0x00000040, gpButtonRightThumb = 0x00000080,")
        TJS_W("gpButtonLeftShoulder = 0x00000100,")
        TJS_W("gpButtonRightShoulder = 0x00000200,")
        TJS_W("gpButtonA = 0x00001000, gpButtonB = 0x00002000,")
        TJS_W("gpButtonX = 0x00004000, gpButtonY = 0x00008000,")
        TJS_W("gpLeftAxisX = 0x00010000, gpLeftAxisY = 0x00020000,")
        TJS_W("gpRightAxisX = 0x00040000, gpRightAxisY = 0x00080000,")
        TJS_W("gpLeftTrigger = 0x00100000, gpRightTrigger = 0x00200000,")
        TJS_W("gpDIAxisX = 0, gpDIAxisY = 1, gpDIAxisZ = 2,")
        TJS_W("gpDIAxisRotX = 3, gpDIAxisRotY = 4, gpDIAxisRotZ = 5,")
        TJS_W("gpDISlider_0 = 6, gpDISlider_1 = 7,")
        TJS_W("gpDIPOV_0 = 8, gpDIPOV_1 = 9, gpDIPOV_2 = 10, gpDIPOV_3 = 11,")
        TJS_W("gpDIButton1 = 12, gpDIButton2 = 13, gpDIButton3 = 14,")
        TJS_W("gpDIButton4 = 15, gpDIButton5 = 16, gpDIButton6 = 17,")
        TJS_W("gpDIButton7 = 18, gpDIButton8 = 19, gpDIButton9 = 20,")
        TJS_W("gpDIButton10 = 21, gpDIButton11 = 22, gpDIButton12 = 23,")
        TJS_W("gpDIButton13 = 24, gpDIButton14 = 25, gpDIButton15 = 26,")
        TJS_W("gpDIButton16 = 27, gpDIButton17 = 28, gpDIButton18 = 29,")
        TJS_W("gpDIButton19 = 30, gpDIButton20 = 31, gpDIButton21 = 32,")
        TJS_W("gpDIButton22 = 33, gpDIButton23 = 34, gpDIButton24 = 35,")
        TJS_W("gpDIButton25 = 36, gpDIButton26 = 37, gpDIButton27 = 38,")
        TJS_W("gpDIButton28 = 39, gpDIButton29 = 40, gpDIButton30 = 41,")
        TJS_W("gpDIButton31 = 42, gpDIButton32 = 43, gpDIDisable = 44,")
        TJS_W("gpTouchNo = 0, gpTouchDown = 1, gpTouchLiftoff = 0;");
    try {
        TVPExecuteExpression(expression);
    } catch(...) {
        // 既に定義済みなど何らかの例外が起きても本体起動を阻害しない
        spdlog::warn("Gamepad stub: failed to register gp* constants");
    }
}

static void TVPRegisterGamepadStub() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return;

    // GamepadPort クラスを登録
    iTJSDispatch2 *portClass = new tTJSNC_GamepadPortStub();
    if(portClass) {
        tTJSVariant val(portClass);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("GamepadPort"), nullptr, &val,
                        global);

        // 一部のゲーム (例: limelight) は SystemConfig.GamepadPort も参照する
        tTJSVariant configVar;
        if(global->PropGet(0, TJS_W("SystemConfig"), nullptr, &configVar,
                           global) == TJS_S_OK &&
           configVar.Type() == tvtObject && configVar.AsObjectNoAddRef()) {
            configVar.AsObjectNoAddRef()->PropSet(
                TJS_MEMBERENSURE, TJS_W("GamepadPort"), nullptr, &val,
                configVar.AsObjectNoAddRef());
        }
        portClass->Release();
    }

    // Gamepad クラスを登録
    iTJSDispatch2 *padClass = new tTJSNC_GamepadStub();
    if(padClass) {
        tTJSVariant val(padClass);
        global->PropSet(TJS_MEMBERENSURE, TJS_W("Gamepad"), nullptr, &val,
                        global);
        padClass->Release();
    }

    global->Release();

    TVPRegisterGamepadConstants();
    spdlog::info("Registered GamepadPort/Gamepad stub for missing gamepad.dll");
}

//---------------------------------------------------------------------------
bool TVPLoadInternalPlugin(const ttstr &_name);

void TVPLoadPlugin(const ttstr &name) {
    auto pluginName = name;
    // motionplayer.dll and emoteplayer.dll may be same?
    if(name == TJS_W("emoteplayer.dll"))
        pluginName = "motionplayer.dll";

    if(TVPLoadInternalPlugin(pluginName)) {
        spdlog::debug("Loading Plugin: {} Success", name.AsStdString());
    } else {
        spdlog::error("Loading Plugin: {} Failed", name.AsStdString());
        if(pluginName == TJS_W("gamepad.dll"))
            TVPRegisterGamepadStub();
    }
}

//---------------------------------------------------------------------------
bool TVPUnloadPlugin(const ttstr &name) {
    // unload plugin
    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// plug-in autoload support
//---------------------------------------------------------------------------
struct tTVPFoundPlugin {
    std::string Path;
    std::string Name;

    bool operator<(const tTVPFoundPlugin &rhs) const { return Name < rhs.Name; }
};

static tjs_int TVPAutoLoadPluginCount = 0;

static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin> &list,
                               std::string folder) {
    TVPListDir(folder, [&](const std::string &filename, int mask) {
        if(mask & S_IFREG) {
            if(!strcasecmp(filename.c_str() + filename.length() - 4, ".tpm")) {
                tTVPFoundPlugin fp;
                fp.Path = folder;
                fp.Name = filename;
                list.emplace_back(fp);
            }
        }
    });
}

void TVPLoadInternalPlugins() {
    ncbAutoRegister::AllRegist();
    ncbAutoRegister::LoadModule(TJS_W("xp3filter.dll"));
    // motionplayer.dll: E-mote / Motion model player (reverse-engineered
    // from libkrkr2.so by kirikiroid2-web). Required by commercial KAG VNs
    // that use Live2D-like animated models (e.g. limelight).
    ncbAutoRegister::LoadModule(TJS_W("motionplayer.dll"));
    // emoteplayer.dll: must be pre-loaded so CanLoadPlugin("emoteplayer.dll")
    // returns true. Its PreRegist callback chains LoadModule("motionplayer.dll").
    ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
}

bool TVPLoadInternalPlugin(const ttstr &_name) {
    /* 1. 拿到 ttstr 的原始缓冲区 */
    const tjs_char *src = _name.c_str();
    size_t len = _name.length();

    /* 2. 在 src 里找最后一个 '/' 或 '\\'，定位纯文件名起始 */
    const tjs_char *fileBegin = src;
    for(const tjs_char *p = src; *p; ++p) {
        if(*p == TJS_W('/') || *p == TJS_W('\\'))
            fileBegin = p + 1;
    }

    /* 3. 在 fileBegin 里找最后一个 '.' */
    const tjs_char *dot = nullptr;
    for(const tjs_char *p = fileBegin; *p; ++p) {
        if(*p == TJS_W('.'))
            dot = p; // 记录最后一个 '.'
    }

    /* 4. 检查后缀 .tpm（不区分大小写） */
    bool needReplace = false;
    if(dot && dot[1] && dot[2] && dot[3] && !dot[4]) // 长度正好 4：".tpm"
    {
        tjs_char low[5]; // 存放小写副本
        for(int i = 0; i < 4; ++i)
            low[i] = (tjs_char)towlower(dot[i]);
        low[4] = 0;

        if(TJS_strncmp(low, TJS_W(".tpm"), 4) == 0)
            needReplace = true;
    }

    /* 5. 构造结果字符串 */
    if(needReplace) {
        /* 需要替换为 .dll，计算新长度 */
        size_t newLen = len - 3 + 3; // 去掉 "tpm" 加上 "dll"
        tjs_char *buf = new tjs_char[newLen + 1];

        /* 拷贝前缀（含 .） */
        TJS_strncpy(buf, src, dot - src + 1);
        buf[dot - src + 1] = 0;

        /* 追加 dll */
        TJS_strcat(buf, TJS_W("dll"));

        ttstr fixed(buf);
        delete[] buf;

        return ncbAutoRegister::LoadModule(TVPExtractStorageName(fixed));
    }
    return ncbAutoRegister::LoadModule(TVPExtractStorageName(_name));
}

void tvpLoadPlugins() {
    TVPLoadInternalPlugins();
    // This function searches plugins which have an extension of
    // ".tpm" in the default path:
    //    1. a folder which holds kirikiri executable
    //    2. "plugin" folder of it
    // Plugin load order is to be decided using its name;
    // aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

    // search plugins from path: (exepath), (exepath)\system,
    // (exepath)\plugin
    std::vector<tTVPFoundPlugin> list;

    std::string exepath = ExtractFileDir(TVPNativeProjectDir.AsStdString());

    TVPSearchPluginsAt(list, exepath);
    TVPSearchPluginsAt(list, exepath + "/system");
    TVPSearchPluginsAt(list, exepath + "/plugin");

    // sort by filename
    std::sort(list.begin(), list.end());

    // load each plugin
    TVPAutoLoadPluginCount = (tjs_int)list.size();
    for(auto &i : list) {
        TVPAddImportantLog(ttstr(TJS_W("(info) Loading ")) +
                           ttstr(i.Name.c_str()));
        TVPLoadPlugin((i.Path + "/" + i.Name).c_str());
    }
}

//---------------------------------------------------------------------------
tjs_int TVPGetAutoLoadPluginCount() { return TVPAutoLoadPluginCount; }

//---------------------------------------------------------------------------
// some service functions for plugin
//---------------------------------------------------------------------------
#include <zlib.h>

int ZLIB_uncompress(unsigned char *dest, unsigned long *destlen,
                    const unsigned char *source, unsigned long sourcelen) {
    return uncompress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress(unsigned char *dest, unsigned long *destlen,
                  const unsigned char *source, unsigned long sourcelen) {
    return compress(dest, destlen, source, sourcelen);
}

//---------------------------------------------------------------------------
int ZLIB_compress2(unsigned char *dest, unsigned long *destlen,
                   const unsigned char *source, unsigned long sourcelen,
                   int level) {
    return compress2(dest, destlen, source, sourcelen, level);
}
//---------------------------------------------------------------------------
#include "md5.h"

static char TVP_assert_md5_state_t_size[(sizeof(TVP_md5_state_t) >=
                                         sizeof(md5_state_t))];

// if this errors, sizeof(TVP_md5_state_t) is not equal to
// sizeof(md5_state_t). sizeof(TVP_md5_state_t) must be equal to
// sizeof(md5_state_t).
//---------------------------------------------------------------------------
void TVP_md5_init(TVP_md5_state_t *pms) { md5_init((md5_state_t *)pms); }

//---------------------------------------------------------------------------
void TVP_md5_append(TVP_md5_state_t *pms, const tjs_uint8 *data, int nbytes) {
    md5_append((md5_state_t *)pms, (const md5_byte_t *)data, nbytes);
}

//---------------------------------------------------------------------------
void TVP_md5_finish(TVP_md5_state_t *pms, tjs_uint8 *digest) {
    md5_finish((md5_state_t *)pms, digest);
}

//---------------------------------------------------------------------------
bool TVPRegisterGlobalObject(const tjs_char *name, iTJSDispatch2 *dsp) {
    // register given object to global object
    tTJSVariant val(dsp);
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    tjs_error er;
    try {
        er = global->PropSet(TJS_MEMBERENSURE, name, nullptr, &val, global);
    } catch(...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
bool TVPRemoveGlobalObject(const tjs_char *name) {
    // remove registration of global object
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return false;
    tjs_error er;
    try {
        er = global->DeleteMember(0, name, nullptr, global);
    } catch(...) {
        global->Release();
        return false;
    }
    global->Release();
    return TJS_SUCCEEDED(er);
}

//---------------------------------------------------------------------------
void TVPDoTryBlock(tTVPTryBlockFunction tryblock,
                   tTVPCatchBlockFunction catchblock,
                   tTVPFinallyBlockFunction finallyblock, void *data) {
    try {
        tryblock(data);
    } catch(const eTJS &e) {
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("eTJS");
        desc.message = e.GetMessage();
        if(catchblock(data, desc))
            throw;
        return;
    } catch(...) {
        if(finallyblock)
            finallyblock(data);
        tTVPExceptionDesc desc;
        desc.type = TJS_W("unknown");
        if(catchblock(data, desc))
            throw;
        return;
    }
    if(finallyblock)
        finallyblock(data);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// TVPCreateNativeClass_Plugins
//---------------------------------------------------------------------------
tTJSNativeClass *TVPCreateNativeClass_Plugins() {
    auto *cls = new tTJSNC_Plugins();

    // setup some platform-specific members
    //---------------------------------------------------------------------------

    //-- methods

    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ link) {

        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        TVPLoadPlugin(name);

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ link)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ unlink) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;

        ttstr name = *param[0];

        bool res = TVPUnloadPlugin(name);

        if(result)
            *result = (tjs_int)res;

        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(
        /*object to register*/ cls,
        /*func. name*/ unlink)
    //----------------------------------------------------------------------
    TJS_BEGIN_NATIVE_METHOD_DECL(getList) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        try {
            tjs_int idx = 0;
            for(const ttstr &name : TVPRegisteredPlugins) {
                tTJSVariant val(name);
                array->PropSetByNum(TJS_MEMBERENSURE, idx++, &val, array);
            }
            if(result)
                *result = tTJSVariant(array, array);
        } catch(...) {
            array->Release();
            throw;
        }
        array->Release();
        return TJS_S_OK;
    }
    TJS_END_NATIVE_STATIC_METHOD_DECL_OUTER(cls, getList)
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    return cls;
}
//---------------------------------------------------------------------------
