# 2026-07-22 Android Native Cocos Cut

## Goal

Continue removing Cocos2d from the active Android production path after the
2026-07-11 Java/Gradle cut. Keep Flutter + SDL3 as the only runtime host.

## Hard requirements (unchanged)

- Fixed game surface: 1920x1080.
- Flutter overlay is menu/input only; no second game presenter surface.
- Do not restore Cocos Activity / GLSurfaceView / MainActivity.
- Do not re-add `project(':cocos2dx')` or package `libcocos2dx`.

## What was cut

### Android native JNI (`platforms/android/cpp/krkr2_android.cpp`)

- Removed all `KRKR2_ENABLE_COCOS_HOST` / `cocos2d::*` branches.
- Removed `TVPAppDelegate` legacy host creation path.
- Removed `cocos_android_app_init` and `Cocos2dxRenderer.nativeFrameEnd`.
- Removed `ShouldRouteLegacyInputToCocos()`; input is SDL-only.
- Legacy `KR2Activity` touch/key/IME JNI now forwards to SDL dispatch /
  `TVPSDLQueueFlutterTouch*` instead of Cocos Director.
- Removed deleted-class `MainActivity` JNI exports; only
  `AndroidRuntimeBridge` remains for surface/touch/metrics.

### Shared interface

- `iWindowLayer::GetPrimaryArea()` is now `#if KRKR2_ENABLE_COCOS_HOST` only.
- `SDLRuntimeWindowHost` no longer implements a dummy Cocos node getter.
- Cocos movie overlay (`MoviePlayerOverlay::SetWindow`) remains behind the same
  host flag and is not part of Android no-Cocos builds.

### Android tree / deps

- Deleted `platforms/android/libcocos2dx` (entire Cocos Java module, already
  unlinked from `settings.gradle`).
- `vcpkg.json`: `cocos2dx` is now `"platform": "!android"` so Android vcpkg
  installs no longer pull Cocos.

### Already-in-progress shell cleanup (kept)

- `SdlRuntimeActivity` no longer owns Flutter Texture game surface producers.
- Overlay `showFlutterGameMainMenu` is a static bridge on `SdlRuntimeActivity`.
- Overlay page remains menu-only.

## What remains (next cuts)

1. Desktop hosts still default `KRKR2_ENABLE_COCOS_HOST=ON`
   (`platforms/windows|linux|apple` still enter via Cocos `AppDelegate`).
2. Native Cocos sources under `cpp/core/environ/cocos2d/` and UI forms under
   `cpp/core/environ/ui/*` remain for desktop legacy builds only.
3. `KR2Activity` class name is still used for JNI/platform helpers; rename to a
   Cocos-free bridge when convenient.
4. `krkr::Texture2D` still aliases Cocos textures when host is ON; standalone
   texture type is already used when host is OFF.
5. Optional: delete or quarantine Cocos vcpkg port once desktop also migrates.

## Do not regress

- Android must keep `KRKR2_ENABLE_COCOS_HOST=OFF` (CMake FORCE + Gradle arg).
- Do not reintroduce `libcocos2dx` or Cocos Java packages.
- Do not reintroduce MainActivity game-surface Texture path.
- Keep fixed 1920x1080 game surface contract.
