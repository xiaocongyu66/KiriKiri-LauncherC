// SPDX-License-Identifier: MIT
//
// Compatibility implementation for PackinOne.dll.
//
// The Android/SDL references do not agree on one exact behaviour:
// krkrsdl3 uses PackinOne to install AffineSourceMovie helpers, while
// kirikiroid2-web treats it as a bundle loader. Keep both compatibility paths
// here because games use Plugins.link("PackinOne.dll") as a broad feature gate.

#define NCB_MODULE_NAME TJS_W("packinone.dll")

#include "ncbind.hpp"
#include "ScriptMgnIntf.h"

class PackinOneDummy {
public:
    static void Stub() {}
};

NCB_REGISTER_CLASS(PackinOneDummy) {
    NCB_METHOD(Stub);
}

static bool HasGlobalMember(const tjs_char *name) {
    tTJS *engine = TVPGetScriptEngine();
    if(!engine)
        return false;
    iTJSDispatch2 *global = engine->GetGlobalNoAddRef();
    if(!global)
        return false;
    tTJSVariant value;
    return TJS_SUCCEEDED(
               global->PropGet(0, name, nullptr, &value, global)) &&
        value.Type() != tvtVoid;
}

static void InitPlugin_PackinOne() {
    ncbAutoRegister::LoadModule(TJS_W("fstat.dll"));
    ncbAutoRegister::LoadModule(TJS_W("savestruct.dll"));
    ncbAutoRegister::LoadModule(TJS_W("scriptsEx.dll"));
    ncbAutoRegister::LoadModule(TJS_W("shrinkCopy.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExBTOA.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExImage.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExRaster.dll"));
    ncbAutoRegister::LoadModule(TJS_W("csvParser.dll"));
    ncbAutoRegister::LoadModule(TJS_W("layerExMovie.dll"));

    if(HasGlobalMember(TJS_W("AffineSource")) &&
       !HasGlobalMember(TJS_W("AffineSourceMovie"))) {
        TVPExecuteScript(TJS_W(
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
            "    }"
            "    clear();"
            "    return false;"
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
        ));
    }

    if(!HasGlobalMember(TJS_W("AffineSourceMovie")))
        return;

    TVPExecuteScript(TJS_W(
        "if (global.extSourceMap === void) {"
        "  global.extSourceMap = %[];"
        "}"
        "extSourceMap[\".WMV\"] = AffineSourceMovie;"
        "extSourceMap[\".MPG\"] = AffineSourceMovie;"
        "extSourceMap[\".MPEG\"] = AffineSourceMovie;"
    ));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_PackinOne);
