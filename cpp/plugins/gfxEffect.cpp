// SPDX-License-Identifier: MIT
//
// gfxEffect.dll compatibility shim
//
// Imported (and lightly adapted) from krkrsdl3-main/cpp/plugins/gfxEffect.cpp.
//
// Some KAG VNs probe `Plugins.CanLoadPlugin("gfxEffect.dll")` and, when
// available, instantiate small effect classes such as `gfxFire`.  On the
// upstream Windows engine those are provided by gfxEffect.dll; we don't have
// the binary on Android, so without this stub the script either crashes on
// `new gfxFire()` ("Member \"gfxFire\" does not exist") or silently disables
// the entire visual-effect chain because CanLoadPlugin reports false.
//
// We mirror the krkrsdl3 trick: register the module name with ncbind so
// CanLoadPlugin returns true, and pre-define empty `gfxFire` / `gfxEffect`
// TJS classes through TVPExecuteScript. The construction succeeds, the game
// proceeds, and any further calls hit the script-level no-op.
#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("gfxEffect.dll")

static bool GfxCompatHasGlobal(const tjs_char *name) {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global)
        return false;
    tjs_error hr = global->IsValid(TJS_IGNOREPROP, name, nullptr, global);
    global->Release();
    return hr == TJS_S_TRUE;
}

static void GfxCompatInstallClassIfMissing(const tjs_char *name,
                                           const tjs_char *script) {
    if(!GfxCompatHasGlobal(name))
        TVPExecuteScript(script);
}

static void InitPlugin_GFXEffect() {
    GfxCompatInstallClassIfMissing(
        TJS_W("gfxFire"),
        TJS_W("class gfxFire {")
        TJS_W("  function gfxFire() {}")
        TJS_W("  function finalize() {}")
        TJS_W("};"));
    GfxCompatInstallClassIfMissing(
        TJS_W("gfxEffect"),
        TJS_W("class gfxEffect {")
        TJS_W("  function gfxEffect() {}")
        TJS_W("  function finalize() {}")
        TJS_W("};"));
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_GFXEffect);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("gfxFire.dll")
static void InitPlugin_GFXFire() {
    InitPlugin_GFXEffect();
}
NCB_PRE_REGIST_CALLBACK(InitPlugin_GFXFire);
