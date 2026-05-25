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
// ---------------------------------------------------------------------------

#include "ncbind.hpp"

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
