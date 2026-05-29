// SPDX-License-Identifier: MIT
//
// pluginNameStubs.cpp
//
// Lightweight ncbind module-name placeholders so that KAG scripts can call
// `Plugins.link("xxx.dll")` without triggering their fallback / catch
// branches even when our engine does not implement the named plugin.
//
// The functionality these dlls used to provide is either already covered
// by other compatibility shims (e.g. wfBasicEffectCompat / extrans) or is
// not relevant on Android. We simply need the ncbind auto-register table
// to contain an entry under the lower-cased dll name so that
// TVPLoadInternalPlugin -> ncbAutoRegister::LoadModule returns true.
//
// Reference: AetherKiri-main/cpp/plugins/dummy_plugin_stubs.cpp
//            AetherKiri-main/cpp/plugins/packinone.cpp
//            krkrsdl3-main/cpp/plugins/{kirikiroid2,expat,PackinOne}.cpp
// ---------------------------------------------------------------------------

#include "ncbind.hpp"
#include "ScriptMgnIntf.h"
#include "TextStream.h"
#include <ctype.h>

// Some headers do not pull in TJS_INTF_METHOD on Android (it is __stdcall
// on win32 and empty everywhere else); guard the same way layerExBTOA.cpp
// and layerExAreaAverage.cpp do in this directory.
#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

// ---------------------------------------------------------------------------
// Generic dummy stubs (registered via NCB_PRE_REGIST_CALLBACK).
// Each block defines NCB_MODULE_NAME, declares an empty static callback,
// then registers it. The macro pulls in the current NCB_MODULE_NAME at
// expansion time, so we have to #undef + #define between blocks.
// ---------------------------------------------------------------------------

#define NCB_MODULE_NAME TJS_W("k2compat.dll")
static void k2compat_stub() {}
NCB_PRE_REGIST_CALLBACK(k2compat_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kagexopt.dll")
static void kagexopt_stub() {}
NCB_PRE_REGIST_CALLBACK(kagexopt_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("krmovie.dll")
static void krmovie_stub() {}
NCB_PRE_REGIST_CALLBACK(krmovie_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kztouch.dll")
static void kztouch_stub() {}
NCB_PRE_REGIST_CALLBACK(kztouch_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("lzfs.dll")
static void lzfs_stub() {}
NCB_PRE_REGIST_CALLBACK(lzfs_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("win32ole.dll")
static void win32ole_stub() {}
NCB_PRE_REGIST_CALLBACK(win32ole_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("menu.dll")
static void menu_dll_stub() {}
NCB_PRE_REGIST_CALLBACK(menu_dll_stub);

class MenuItemCompat {
public:
    static tjs_error TJS_INTF_METHOD textToKeycode(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }

    static tjs_error TJS_INTF_METHOD keycodeToText(tTJSVariant *result,
                                                   tjs_int numparams,
                                                   tTJSVariant **param,
                                                   iTJSDispatch2 *objthis) {
        if(result)
            *result = TJS_W("");
        return TJS_S_OK;
    }

    tTJSVariant getHMENU() const { return tTJSVariant((tjs_int)0); }
    void setHMENU(tTJSVariant v) {}
};

NCB_GET_INSTANCE_HOOK(MenuItemCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT()));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(MenuItemCompat, MenuItem) {
    NCB_METHOD_RAW_CALLBACK(textToKeycode, MenuItemCompat::textToKeycode, 0);
    NCB_METHOD_RAW_CALLBACK(keycodeToText, MenuItemCompat::keycodeToText, 0);
    NCB_PROPERTY(HMENU, getHMENU, setHMENU);
}

class WindowMenuCompat {
public:
    explicit WindowMenuCompat(iTJSDispatch2 *obj) {}

    tTJSVariant getMenu() const { return Menu; }
    void setMenu(tTJSVariant v) { Menu = v; }

private:
    tTJSVariant Menu;
};

NCB_GET_INSTANCE_HOOK(WindowMenuCompat) {
    NCB_GET_INSTANCE_HOOK_CLASS() {}
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj)
            SetNativeInstance(objthis, (obj = new ClassT(objthis)));
        return obj;
    }
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowMenuCompat, Window) {
    NCB_PROPERTY(menu, getMenu, setMenu);
}

// ---------------------------------------------------------------------------
// VoiceEffect-related stubs.
// voiceeffect.tjs (used by wamsoft titles such as ライムライト・レモネードジャム)
// links wumultitrack.dll / wvdecoder.dll at top level. The actual filter and
// codec features are already provided by wfBasicEffectCompat /
// wfTypicalDSPCompat / extrans (wuvorbis, wuopus, wuflac). We only need the
// module name to resolve so the script's top-level Plugins.link does not go
// down the SystemConfig.voiceEffectForceDisabled = 1 catch branch.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wumultitrack.dll")
static void wumultitrack_stub() {}
NCB_PRE_REGIST_CALLBACK(wumultitrack_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wvdecoder.dll")
static void wvdecoder_stub() {}
NCB_PRE_REGIST_CALLBACK(wvdecoder_stub);

// ---------------------------------------------------------------------------
// PackinOne is just a re-export bundle of fstat / dirlist / addFont /
// saveStruct / getMD5HashString. All of those are already registered as
// individual ncbind modules in this directory, so we only need the alias.
//
// In addition, we mirror krkrsdl3-main's PackinOne behaviour: it pulls in
// LayerExMovie.dll and injects an AffineSourceMovie TJS class plus
// extSourceMap entries for .WMV / .MPG / .MPEG so KAG scripts that
// `Plugins.link("PackinOne.dll")` can fall through to their movie-dispatch
// branch instead of the catch path.
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("packinone.dll")

class PackinOneDummy {
public:
    static void Stub() {}
};

NCB_REGISTER_CLASS(PackinOneDummy) {
    NCB_METHOD(Stub);
}

static void InitPlugin_PackinOne() {
    // Pull the movie-layer module so AffineSourceMovie can rely on it.
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));

    // Source kept verbatim from krkrsdl3-main with two TJS syntax fixes:
    //   - `!= = void`  ->  `!= void`
    //   - `== = void`  ->  `== void`
    // (the original file has whitespace typos that make TJS reject the
    // expression).
    TVPExecuteScript(TJS_W(
        "if (typeof global.extSourceMap == \"undefined\") {"
        "  global.extSourceMap = %[];"
        "}"
        "class AffineSourceMovie extends AffineSource {"
        "  var _movie;"
        "  var _width = 0;"
        "  var _height = 0;"
        "  var _lastOwner = true;"
        "  function AffineSourceMovie(window) {"
        "    super.AffineSource(window);"
        "  }"
        "  function createLayer(orig=void) {"
        "    var src = new global.Layer(_window, _pool);"
        "    if (orig != void) {"
        "      src.assignImages(orig);"
        "      src.width = orig.width;"
        "      src.height = orig.height;"
        "      src.scale = orig.scale;"
        "    } else {"
        "      src.scale = 1.0;"
        "    }"
        "    return src;"
        "  }"
        "  function finalize() {"
        "    if (_lastOwner) {"
        "      clear();"
        "      invalidate _movie;"
        "    }"
        "  }"
        "  function clear() {"
        "    notifyOwner(\"onMotionStop\");"
        "    onMovieStop();"
        "    if (typeof kag != \"undefined\" && kag !== void)"
        "      kag.conductor.trigger(\"movie_world_foremovie\");"
        "  }"
        "  function clone(newwindow, instance) {"
        "    if (newwindow == void) {"
        "      newwindow = _window;"
        "    }"
        "    if (instance == void) {"
        "      instance = new global.AffineSourceMovie(newwindow);"
        "    }"
        "    instance._movie = _movie;"
        "    instance._width = _width;"
        "    instance._height = _height;"
        "    _lastOwner = false;"
        "    super.clone(newwindow, instance);"
        "    return instance;"
        "  }"
        "  function canWaitMovie() {"
        "    return _movie.isPlayingMovie();"
        "  }"
        "  function isFlip() {"
        "    if (_movie.isPlayingMovie()) {"
        "      return true;"
        "    } else {"
        "      clear();"
        "      return false;"
        "    }"
        "  }"
        "  function stopMovie() {"
        "    _movie.stopMovie();"
        "  }"
        "  function drawAffine(target, mtx, src) {"
        "    (global.Layer.copyRect incontextof target)("
        "      0, 0, _movie, 0, 0, _width, _height);"
        "  }"
        "  function loadImages(storage, colorKey=clNone, options=void) {"
        "    _movie = createLayer();"
        "    _movie.openMovie(storage, false);"
        "    _movie.setSizeToImageSize();"
        "    _width = _movie.width;"
        "    _height = _movie.height;"
        "    _movie.startMovie(false);"
        "  }"
        "};"
        "extSourceMap[\".WMV\"] = AffineSourceMovie;"
        "extSourceMap[\".MPG\"] = AffineSourceMovie;"
        "extSourceMap[\".MPEG\"] = AffineSourceMovie;"
    ));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);

// ---------------------------------------------------------------------------
// expat.dll — stub for older KAG scripts that probe Plugins.link("expat.dll")
// before parsing XML resources. We do not bundle libexpat on Android; this
// just needs the module name registered so CanLoadPlugin reports true.
// (mirrors krkrsdl3-main/cpp/plugins/expat.cpp)
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("expat.dll")
static void expat_stub() {}
NCB_PRE_REGIST_CALLBACK(expat_stub);

// ---------------------------------------------------------------------------
// kirikiroid2.dll — exposes the helpers expected by some KAG titles that
// were originally targeted at the kirikiroid2 fork:
//   - global function `_str_ord(str)` returning the first wchar code point
//   - Storages.setTextEncoding(encoding)
// (mirrors krkrsdl3-main/cpp/plugins/kirikiroid2.cpp)
// ---------------------------------------------------------------------------

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("kirikiroid2.dll")

static tjs_error TJS_INTF_METHOD krkr_str_ord(tTJSVariant *result,
                                              tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
    if(numparams == 0)
        return TJS_E_FAIL;
    const tTJSVariant &arg = *param[0];
    if(arg.Type() == tvtString) {
        ttstr s = arg;
        const tjs_char *p = s.c_str();
        *result = (tjs_int)(p && *p ? (unsigned int)*p : 0);
    } else {
        *result = arg;
    }
    return TJS_S_OK;
}

NCB_REGISTER_FUNCTION(_str_ord, krkr_str_ord);

static tjs_error TJS_INTF_METHOD krkr_setTextEncoding(tTJSVariant *result,
                                                      tjs_int numparams,
                                                      tTJSVariant **param,
                                                      iTJSDispatch2 *objthis) {
    if(numparams == 0)
        return TJS_E_FAIL;
    tTJSVariant arg = *param[0];
    if(arg.Type() == tvtString) {
        TVPSetDefaultReadEncoding(arg);
    }
    return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(setTextEncoding, Storages, krkr_setTextEncoding);
