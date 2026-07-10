# 2026-07-10 SDL3 startup hardening

## Hard targets

- Continue migration to Flutter + SDL3 and remove Cocos step by step.
- Keep the game/native surface fixed at 1920x1080. Do not resize the game render target to device size, Flutter logical size, DPR, or Cocos scene size.
- Startup must be deterministic: Java/Kotlin resolves permissions and game path, native SDL3 host owns timing/render frame execution, and presenter only swaps valid completed frames.
- Do not add heavy validation in hot render/upload paths. Startup/lifecycle guards are acceptable; per-frame checks must be minimal and gate only unsafe states.

## This change set

Files changed:

- `platforms/android/app/java/org/github/krkr2/AndroidRuntimeBridge.kt`
- `platforms/android/app/java/org/github/krkr2/SdlRuntimeActivity.kt`
- `platforms/android/app/java/org/github/krkr2/LauncherActivity.kt`
- `platforms/android/app/java/org/github/krkr2/MainActivity.kt`
- `platforms/android/app/java/org/github/krkr2/StoragePermission.kt`
- `platforms/android/cpp/krkr2_android.cpp`
- `cpp/core/environ/sdl/SDLGameManager.cpp`

## Kotlin/Android startup fixes

`AndroidRuntimeBridge.ensureInitialized()` is now serialized with an explicit lock. Before this, two lifecycle or bridge calls could both see `initialized=false` and enter `nativeInitRuntime()` concurrently. Native uses `call_once` for part of init, but SDL host/presenter registration was not fully protected on the Java side.

Most bridge wrapper methods no longer call `ensureInitialized()`. This is important because lifecycle callbacks such as `onDestroy`, `surfaceDestroyed`, input events, overlay metrics, and `detachGameSurface()` must not accidentally start the native runtime. Only the intentional startup paths should initialize native state.

Added `AndroidRuntimeBridge.clearSdlContext(activity)`. `SdlRuntimeActivity.onDestroy()` now clears SDL Java's static Activity context when it points at the destroyed activity, avoiding stale Activity retention across SDL runtime launches.

Added shared `StoragePermission` helper. `LauncherActivity` now gates game launch on storage access before starting either `SdlRuntimeActivity` or legacy `MainActivity`. `SdlRuntimeActivity` also checks storage access before native init and surface startup, preventing black/log-only launches when `resolveLaunchPath()` cannot read the game directory.

`SdlRuntimeActivity` now falls back explicitly to `MainActivity` when SDL runtime/JNI init fails, SDL Java is not ready, or `startGame()` returns false. The fallback targets `MainActivity::class.java` directly and copies the original extras, so it does not loop back through `LauncherPrefs.getUseSdlRuntimeActivity()`.

`SdlRuntimeActivity` now checks `Surface.isValid` before attaching the `SurfaceView` to native. Flutter SurfaceProducer cleanup and texture disposal can restore the `SurfaceView` fallback if it is valid, rather than leaving native detached forever.

`SdlRuntimeActivity.onDestroy()` now stops frame pumping first, clears the overlay channel handler, detaches native surface before destroying Flutter overlay objects, then clears SDL context and native UI host.

## Native SDL3/EGL startup fixes

`TVPAndroidSDLRuntimeHost::StartGame()` now fails early if the EGL render context cannot be made current. Previously it ignored the failure and called `TVPRuntimeStartApplication()`, which could leave a half-started engine with undefined GL state.

`RunFrameTransaction()` now has a lightweight reentry guard and a hard EGL-current gate. If EGL is not current, the frame is skipped and the engine frame is not advanced. This directly addresses startup/surface churn cases where rendering could run into invalid GL state.

Android EGL context creation no longer permanently latches failure through `state.tried`. Failure paths now reset/destroy partial EGL resources, allowing retry after lifecycle/surface churn. `eglBindAPI()` is checked, and `eglMakeCurrent()` failure resets the synthetic context state.

SDL3 JNI readiness is now represented by `gAndroidSDLJniReady`. `TVPAndroidInitializeSDLHost()` and no-Cocos legacy initialization refuse to register the SDL host if SDL3 JNI bootstrap failed. `nativeStartGame()` also rejects start if base init, SDL JNI, runtime host, or game surface are not ready.

`TVPRegisterAndroidSDLRuntimeHost()` is protected by `std::call_once`, avoiding repeated runtime host/presenter registration from multiple Java/native entrypoints.

`nativeRunFrame()` no longer runs a raw fallback engine frame when no runtime host exists. For SDL3 migration this fallback bypassed the EGL gate and presenter pump, so the correct behavior is to log and skip.

`ANativeWindow_setBuffersGeometry()` results are now logged in set/resize paths. Resize no longer reports fixed nonzero size when there is no native window.

## SDL runtime/presenter startup fixes

`TVPSDLInitializeRuntime()` no longer uses a single `call_once` to permanently latch `SDL_INIT_EVENTS` failure. One-time SDL setup remains one-time, but the actual events subsystem init can retry. This matches Android startup reality better, where SDL may fail early and recover after lifecycle/surface readiness.

`TVPSDLNotifyAndroidFlutterGameSurfaceChanged()` now clears transient SDL screen presenter failure latches: `videoInitTried`, `windowFailed`, `rendererFailed`, and `hybridWindowDeferred`. It also refreshes `videoReady` from `SDL_WasInit`. This lets Android surface attach/resize recover from earlier SDL video/window/renderer failures.

`EnsureSDLScreenPresenterLocked()` now clears `videoInitTried` when `SDL_InitSubSystem(SDL_INIT_VIDEO)` fails, so video init can retry instead of staying dead until process restart.

## Remaining risks

- The current SDL3 Android path is still a plain `Activity`, not `SDLActivity`. Reference `krkrsdl3` uses `SDLActivity` and SDL's managed thread; longer-term migration should move closer to that shape.
- `KR2Activity` still owns some static native-facing settings APIs and the legacy fallback. Do not delete Cocos Java/classes until these APIs are fully moved to an activity-neutral bridge.
- No native stop-game bridge was added in this pass. The engine shutdown API needs separate review before wiring `TVPTerminateAsync` or system uninit into Activity destruction.
- Android Gradle validation is blocked locally by a corrupt/missing NDK install at `/root/aidepro-tools/android-sdk/ndk/28.0.13004108` without `source.properties`.

## Validation performed

- `git diff --check` passed.
- `:app:compileDebugKotlin -Pkrkr2NoCocosHost=true` could not configure because local Android SDK/NDK is broken: NDK `28.0.13004108` has no `source.properties`.

## 2026-07-11 CI follow-up

Latest GitHub Actions run for commit `22a94dc` failed in `Build Flutter launcher APK`:

```text
cpp/core/movie/ffmpeg/VideoPlayer.cpp:15:10: fatal error: 'platform/CCPlatformConfig.h' file not found
```

Root cause: no-Cocos Flutter/SDL3 build intentionally does not include Cocos headers, but `VideoPlayer.cpp` still had a naked include of Cocos `platform/CCPlatformConfig.h`. That header was not used by the file.

Fix: removed the unused `#include "platform/CCPlatformConfig.h"` from `cpp/core/movie/ffmpeg/VideoPlayer.cpp`. Other movie Cocos includes, notably in `KRMoviePlayer.cpp`, are already protected by `#if KRKR2_ENABLE_COCOS_HOST` and should not compile in no-Cocos mode.

## 2026-07-11 overlay fallback visibility follow-up

Sub-agent review found a startup/display trap: `SdlRuntimeActivity` still creates a bottom `SurfaceView` fallback, but then adds a full-screen Flutter overlay above it. The overlay used an opaque `FlutterTextureView`, black `FlutterView` background, and the Dart game overlay painted a full-screen black `ColoredBox` even when `_gameTextureId == null`.

That meant fallback could be technically attached and rendering, but visually hidden behind Flutter black. This exactly matches a class of confusing symptoms: startup succeeds, native surface is valid, frame loop runs, but user sees black when Flutter SurfaceProducer/Texture is unavailable or disposed.

Fix:

- `SdlRuntimeActivity.installFlutterGameOverlay()` now sets the overlay `FlutterTextureView` non-opaque and `FlutterView` background transparent.
- `GameOverlayPage` now paints transparent background when `_gameTextureId == null`, and keeps black letterbox only when a Flutter texture is actually active.

This preserves the fixed 1920x1080 native fallback path while the Flutter texture path is being stabilized. Longer term we should choose one primary game surface host, but until SDL3 fully replaces Cocos and the Flutter texture producer path is proven everywhere, fallback must be visible.

## 2026-07-11 Android no-Cocos default follow-up

Sub-agent CI/CMake review found that CMake still defaulted `KRKR2_ENABLE_COCOS_HOST` to `ON` in the top-level project and several core subprojects. Gradle currently passes `-DKRKR2_ENABLE_COCOS_HOST=OFF`, but relying on every build path to remember that flag is fragile during the Flutter + SDL3 migration.

Fix: Android now defaults `KRKR2_ENABLE_COCOS_HOST` to `OFF` in:

- `CMakeLists.txt`
- `cpp/core/CMakeLists.txt`
- `cpp/core/environ/CMakeLists.txt`
- `cpp/core/movie/CMakeLists.txt`
- `cpp/core/visual/CMakeLists.txt`

Desktop/default non-Android behavior remains `ON`, and explicit `-DKRKR2_ENABLE_COCOS_HOST=...` still overrides the default.

## 2026-07-11 CI follow-up: OGL no-Cocos compile

Build run `29116965046` for commit `5c6899a` progressed past the previous `VideoPlayer.cpp` Cocos include failure. The next fatal errors were in `cpp/core/visual/ogl/RenderManager_ogl.cpp`:

```text
error: use of undeclared identifier 'CHECK_GL_ERROR_DEBUG'
error: use of undeclared identifier 'GL_DEPTH24_STENCIL8'
```

Root cause: `CHECK_GL_ERROR_DEBUG` existed only inside a disabled `#if 0` debug block, but release/no-Cocos builds still referenced it. `GL_DEPTH24_STENCIL8` may not be exposed by the Android GLES2 headers used by this CI toolchain.

Fix:

- Define `CHECK_GL_ERROR_DEBUG()` and `CHECK_GL_ERROR_DEBUG_WITH_FMT(...)` as no-op when not provided by a debug build. This avoids adding hot-path `glGetError()` checks in release.
- Define `GL_DEPTH24_STENCIL8` to the standard enum value `0x88F0` when the platform headers do not expose it.

## 2026-07-11 manifest version cleanup

`platforms/android/app/AndroidManifest.xml` previously duplicated `versionCode` and `versionName` while Gradle already owned the version fields. The manifest copy was removed so Android packaging now has a single version source of truth in Gradle.

## 2026-07-11 whole-project risk review and no-Cocos host follow-up

Hard constraints that must not be forgotten:

- Keep migrating to Flutter + SDL3. Do not solve new failures by restoring Cocos as the Android runtime host.
- The game/native render surface is fixed at 1920x1080. Flutter/device/Cocos sizes are presentation concerns only.
- Prefer proven AetherKiri / krkrsdl2 / krkrsdl3 behavior: full deterministic present when required, swap only after a complete new frame, no hot-path integrity validation.
- Do not add expensive per-frame defensive checks. Fix ownership, pitch, dirty-rect, and lifecycle contracts instead.

CI run `29117956842` for commit `5166294` failed in the Android native link step, not in Java/Kotlin:

```text
ld.lld: error: undefined symbol: TVPCreateAndAddWindow(tTJSNI_Window*)
ld.lld: error: undefined symbol: TVPGetActiveWindow()
ld.lld: error: undefined symbol: TJS::TVPConsoleLog(TJS::tTJSString const&)
ld.lld: error: undefined symbol: TVPSetPostUpdateEvent(void (*)())
ld.lld: error: undefined symbol: FT_Init_FreeType
```

Root cause: Android now builds native no-Cocos by default. The old Cocos `MainScene.cpp` used to provide the global window-host functions and also pulled in FreeType transitively through the Cocos external target. In no-Cocos mode those symbols must be provided by the SDL/Flutter runtime host and explicit library links.

Fix implemented in this pass:

- Added `cpp/core/environ/sdl/SDLRuntimeWindowHost.cpp` for no-Cocos builds. It provides the minimum SDL3/Flutter runtime window host: `TVPCreateAndAddWindow`, `TVPGetActiveWindow`, `TVPSetPostUpdateEvent`, `TVPConsoleLog`, and a lightweight `iWindowLayer` implementation.
- The no-Cocos window host does not create Cocos nodes, sprites, or textures. It keeps width/height fixed at the engine surface contract and submits textures to `TVPRuntimePresentHostWindowTexture()`.
- `cpp/core/environ/CMakeLists.txt` compiles `SDLRuntimeWindowHost.cpp` only when `KRKR2_ENABLE_COCOS_HOST=OFF`.
- `cpp/core/visual/CMakeLists.txt` now explicitly finds and links `Freetype::Freetype`, because Cocos no longer supplies that dependency.
- `cpp/core/render/sdlgpu/CMakeLists.txt` now receives the same `KRKR2_ENABLE_COCOS_HOST=0/1` compile definition as visual/environ/movie modules. This avoids different modules seeing incompatible `RenderManager.h` / texture adapter ABI.

Render correctness fix implemented in this pass:

- `cpp/core/visual/ogl/RenderManager_ogl.cpp` had a high-risk `PixelData` partial-update path. When a cached CPU pixel buffer existed and a partial rect update arrived, the code copied `Height` rows and `internalW * 4` bytes per row from the rect source pointer, then uploaded from the wrong source origin. This can copy old or unrelated layer data into the wrong region and matches the previously observed symptoms: stale chunks, incorrect left/top pieces, and partial old-frame blocks.
- The path now allocates the cache using `internalW * internalH * pixsize`, copies only `rc.get_height()` rows and `rc.get_width() * pixsize` bytes into `rc.left/rc.top`, then uploads from the matching cached rect pointer with a pitch of `internalW * pixsize`.

Important unresolved risks from sub-agent review:

- Android Java/Gradle is still not a true no-Cocos app. `settings.gradle` includes `:cocos2dx`, `app/build.gradle` depends on it, and `MainActivity -> KR2Activity -> Cocos2dxActivity` still exists. Proper next step is to split legacy Cocos activity/sourceSet or move shared static native settings APIs to a Cocos-free bridge.
- `SdlRuntimeActivity` is still a plain `Activity`, not SDL's `SDLActivity`. krkrsdl3's final shape uses SDL-owned lifecycle callbacks and frame iteration. Current bridge is acceptable as a migration step but not final architecture.
- Native init still reports success to Java through a `void nativeInitRuntime()`. If `dlopen("libSDL3.so")` or SDL `JNI_OnLoad` fails, Java can mark the bridge initialized anyway. This should become a native boolean result with retryable failure state.
- There is still no complete native stop/reset entrypoint for SDL runtime host/presenter. `onDestroy()` detaches surfaces and clears Java context, but process-global runtime host, presenter, EGL/pbuffer state, and frame clock are not fully reset.
- `TVPAndroidSDLRuntimeHost::StartGame()` enables screen takeover before `TVPRuntimeStartApplication()`. If game startup fails after takeover is enabled, presenter state can remain half-active. Move takeover after successful start or add rollback.
- Android presenter still has risky raw `iTVPTexture2D*` deferred state in `SDLAndroidFlutterPresenter.cpp`. If a texture is queued for deferred EGL swap and then recycled/destroyed before swap, this can become stale-pointer or wrong dirty-clear behavior. Future fix: `AddRef/Release` queued textures or use a stable texture generation token.
- Android direct-present copy paths can still post after partial copy failure. Future fix: make copy helpers return `bool` and do not `unlockAndPost` a failed or half-written buffer.
- Dirty rect performance is still limited because Android presenter often expands to full frame. Keep full-frame on first frame/surface changes/recovery, but restore real dirty-rect uploads when the backend can honor them safely.
- `TVPSDLPumpScreenPresenter()` holds the SDL surface mirror mutex across expensive copy/post work. Snapshot state under lock, release before copy/post, then validate generation.

## 2026-07-11 startup review follow-up

Additional review after commit `61ea710` found two startup-state risks that were cheap to fix without changing the render hot path:

- `AndroidRuntimeBridge.nativeInitRuntime()` was still declared as `void`, so Java marked `initialized=true` whenever the JNI call returned, even if native failed to load/bootstrap SDL3. It now returns `Boolean`, and `ensureInitialized()` throws/records a failure unless native reports that base init, SDL JNI, and the Android SDL runtime host are ready.
- `TVPAndroidSDLRuntimeHost::StartGame()` enabled screen takeover before `TVPRuntimeStartApplication()`. It now enables takeover only after the game start succeeds, and explicitly disables takeover on start failure. This prevents a failed launch from leaving a half-active SDL presenter state.

Repository hygiene:

- Added `.dart_tool/` to the top-level `.gitignore`, because local Flutter checks create `flutter_launcher/.dart_tool/` and it should never appear in review status.

## 2026-07-11 CI follow-up: tinyxml2 and XXH32 link errors

GitHub Actions job `86459812250` for run `29122257409` failed while linking `libkrkr2.so` from commit `61ea710`:

```text
ld.lld: error: undefined symbol: XXH32
ld.lld: error: undefined symbol: tinyxml2::XMLDocument::XMLDocument(...)
```

Root cause: after removing the native Cocos host from Android, dependencies that used to arrive transitively through Cocos/external targets must be provided by the core modules themselves.

Fix:

- `core_environ_module` now explicitly finds and links `tinyxml2::tinyxml2`, matching the AetherKiri/KrKr2-Next style for config XML parsing.
- `RenderManager.cpp` no longer depends on an external `XXH32` symbol. It now carries the small local `XXH32()` implementation used by AetherKiri/KrKr2-Next. This avoids adding a new xxhash link dependency and avoids symbol conflicts with graphics backends' bundled xxhash.

## 2026-07-11 CI follow-up: tinyxml2 manifest conflict with cocos2dx

CI run `29124072390`, job `86465597909` failed during Android CMake configure while vcpkg installed manifest dependencies. The first fatal error was not a C++ compile error; it was a vcpkg installed-file conflict:

```text
error: The following files are already installed .../vcpkg_installed/arm64-android and are in conflict with tinyxml2:arm64-android
Installed by cocos2dx:arm64-android: include/tinyxml2.h
```

Root cause:

- We added the real `tinyxml2` vcpkg package so `core_environ_module` and `krkr2plugin` can link `tinyxml2::tinyxml2` directly.
- The custom `cocos2dx` vcpkg port also builds Cocos' internal `ext_tinyxml2` and exported `include/tinyxml2.h` into the same vcpkg installed tree.
- vcpkg tracks installed files per package, so once `cocos2dx` claimed `include/tinyxml2.h`, the official `tinyxml2` package could not install its own public header.

Fix applied:

- Keep `tinyxml2` in `vcpkg.json` as the canonical dependency for our own modules.
- Keep Cocos' private `ext_tinyxml2` build intact for legacy Cocos internals.
- Patch `vcpkg/ports/cocos2dx/portfile.cmake` to remove only `${CURRENT_PACKAGES_DIR}/include/tinyxml2.h` before vcpkg packages/registers the Cocos port.

Reasoning:

- This avoids the package database conflict without changing Cocos' legacy build graph yet.
- It also keeps the migration direction correct: new SDL3/no-Cocos modules should depend on normal vcpkg targets, not Cocos private external targets.
- If a later duplicate-symbol issue appears, the next step is to patch the Cocos port to use the official `tinyxml2::tinyxml2` target instead of building `ext_tinyxml2`, but that is a larger port change and not needed for this failure.

## 2026-07-11 CI follow-up: no-Cocos SDL host link failures

Manual Android run `29124894698`, job `86468221394` was triggered for commit `5945d72` because the previous push only changed `vcpkg/` and `memory/`, which does not match the `Code Format Check` push path filter and therefore does not automatically chain into `Build Flutter Android`.

The run passed vcpkg configure/install after the Cocos/tinyxml2 package-file conflict was fixed, then failed at the final `libkrkr2.so` link. The first link errors were:

```text
ld.lld: error: undefined symbol: tinyxml2::XMLNode::ParseDeep(char*, tinyxml2::StrPair*)
ld.lld: error: undefined symbol: tinyxml2::XMLPrinter::XMLPrinter(__sFILE*, bool)
ld.lld: error: undefined symbol: tinyxml2::XMLDocument::Print(tinyxml2::XMLPrinter*)
ld.lld: error: undefined symbol: TVPGetInternalPreferencePath()
ld.lld: error: undefined symbol: TVPGetOSName()
ld.lld: error: undefined symbol: TVPGetPlatformName()
ld.lld: error: undefined symbol: TVPGetJoyPadAsyncState(unsigned int, bool)
ld.lld: error: undefined symbol: TVPGetKeyMouseAsyncState(unsigned int, bool)
ld.lld: error: undefined symbol: TVPOpenPatchLibUrl()
ld.lld: error: undefined symbol: TVPCopyFile(...)
ld.lld: error: undefined symbol: TVPSetPostDrawHook(void (*)())
```

Root causes:

- Tinyxml2 headers were still included as `"tinyxml2/tinyxml2.h"`. In the mixed legacy dependency graph this can resolve to Cocos' older bundled tinyxml2 header while linking against vcpkg's newer `libtinyxml2.a`. That causes ABI-signature mismatches such as `XMLPrinter(FILE*, bool)` vs the newer `XMLPrinter(FILE*, bool, int)` symbol.
- No-Cocos Android builds exclude `cocos2d/MainScene.cpp`, `cocos2d/AppDelegate.cpp`, and `cocos2d/CustomFileUtils.cpp`. Those files used to provide several process-wide platform/window helpers. The new `SDLRuntimeWindowHost.cpp` already replaced the main window creation path, but it did not yet replace every global helper consumed by base/plugin modules.

Fix applied locally after this CI run:

- Updated our tinyxml2 users to include canonical vcpkg header `<tinyxml2.h>`:
  - `cpp/core/environ/ConfigManager/GlobalConfigManager.cpp`
  - `cpp/core/environ/ConfigManager/LocaleConfigManager.cpp`
  - `cpp/core/environ/ui/MainFileSelectorForm.cpp`
  - `cpp/core/environ/ui/PreferenceForm.cpp`
  - `cpp/plugins/pluginSurfaceCompat.cpp`
- Extended `cpp/core/environ/sdl/SDLRuntimeWindowHost.cpp` with no-Cocos replacements for the previously Cocos-owned process helpers:
  - `TVPGetInternalPreferencePath()` stores preferences under the first Android app storage path plus `/.preference/`, and creates the folder if missing.
  - `TVPGetPlatformName()` / `TVPGetOSName()` return platform strings without calling Cocos.
  - `TVPGetKeyMouseAsyncState()` and `TVPGetJoyPadAsyncState()` currently return false. This is intentionally minimal for the link fix; later SDL input migration should wire these to the SDL input state manager instead of Cocos' scancode table.
  - `TVPOpenPatchLibUrl()` is a no-op for now. Later Flutter/Android bridge can open the URL from Java/Kotlin without bringing Cocos back.
  - `TVPCopyFile()` and recursive folder copy now use `FILE*` and `TVPListDir`, matching the old Cocos implementation shape but living in the SDL host.
  - `TVPSetPostDrawHook()` stores the callback and `TVPSDLRuntimeInvokePostDrawHook()` invokes it. Later the SDL frame loop/presenter should call this at the deterministic post-present/post-draw point so Live2D keeps working without Cocos' `TVPPostDrawHookNode`.

Design note:

- These are migration shims, not a new dependency on Cocos. They should remain owned by the SDL/no-Cocos runtime until the functions are split into cleaner platform, filesystem, input, and frame-hook modules.
- The fixed 1920x1080 game-surface contract remains unchanged.

## 2026-07-11 CI follow-up: tinyxml2 v11 ParseDeep API

Android run `29126379191`, job `86472757852` failed earlier in compile after switching to the canonical vcpkg tinyxml2 header:

```text
LocaleConfigManager.cpp:50:9: error: 'ParseDeep' is a protected member of 'tinyxml2::XMLNode'
LocaleConfigManager.cpp:50:37: error: too few arguments to function call, expected 3, have 2
```

Root cause:

- The old Cocos-bundled tinyxml2 header exposed/allowed the `ParseDeep(char*, StrPair*)` style used by `LocaleConfigManager`.
- vcpkg tinyxml2 v11 makes that a protected internal API and exposes `XMLDocument::Parse(...)` as the public parser entry point.

Fix applied:

- `LocaleConfigManager::Initialize()` still strips a UTF BOM with `tinyxml2::XMLUtil::ReadBOM`, then calls `doc.Parse(p)` instead of `doc.ParseDeep(...)`.
- This keeps the same behavior without depending on Cocos' old tinyxml2 internals.

## 2026-07-11 CI follow-up: Flutter shell missing StoragePermission helper

Android run `29126970798`, job `86474525204` reached Kotlin compilation and failed with:

```text
MainActivity.kt:664:16 Unresolved reference 'StoragePermission'
MainActivity.kt:689:21 Unresolved reference 'StoragePermission'
SdlRuntimeActivity.kt:104:14 Unresolved reference 'StoragePermission'
SdlRuntimeActivity.kt:106:27 Unresolved reference 'StoragePermission'
```

Root cause:

- The Flutter Android CI shell is generated in `flutter_launcher/` during `.github/workflows/build-android.yml`.
- The workflow copied `MainActivity.kt` and `SdlRuntimeActivity.kt`, both of which now call the shared permission helper.
- It did not copy `platforms/android/app/java/org/github/krkr2/StoragePermission.kt`, so the generated shell had references but not the helper source.

Fix applied:

- Added `StoragePermission.kt` to the explicit Kotlin copy list in `build-android.yml`.
- Kept the copy list explicit instead of copying all Kotlin files, because the Flutter shell intentionally does not compile every legacy Compose launcher source yet.

## 2026-07-11 runtime logs: force SDL3 path and disable in-game Flutter overlay

User provided logs:

- `/root/log/20260711064457838.log`
- `/root/log/20260711064528563.log`
- `/root/log/20260711064535364.log`
- `/root/log/20260711064541837.log`
- `/root/log/78.log`

Findings:

1. Three short launcher logs crashed immediately through the legacy Cocos activity path:

```text
java.lang.UnsatisfiedLinkError: No implementation found for void org.cocos2dx.lib.Cocos2dxHelper.nativeSetAudioDeviceInfo(boolean, int, int)
    at org.cocos2dx.lib.Cocos2dxHelper.init(Cocos2dxHelper.java:158)
    at org.cocos2dx.lib.Cocos2dxActivity.onCreate(Cocos2dxActivity.java:143)
    at org.tvp.kirikiri2.KR2Activity.onCreate(KR2Activity.java:171)
    at org.github.krkr2.MainActivity.onCreate(MainActivity.kt:101)
```

This means some launch paths still reached `MainActivity`/`KR2Activity`/`Cocos2dxActivity` while the native build is already moving to the no-Cocos `libkrkr2.so` path. That is exactly the failure mode we want to eliminate during the Flutter + SDL3 migration.

Fixes applied:

- `LauncherPrefs.getUseSdlRuntimeActivity()` now always returns true. The old setting is still accepted by `setUseSdlRuntimeActivity(...)`, but requests to disable SDL are ignored and logged as forced true.
- `LauncherActivity.createGameIntent()` always targets `SdlRuntimeActivity`.
- `LauncherSettingsActivity` launch-original path always targets `SdlRuntimeActivity`.
- `platforms/android/flutter/java/org/github/krkr2/LauncherHostActivity.java` always launches `SdlRuntimeActivity`, even when the game directory string is empty.
- `SdlRuntimeActivity` no longer falls back to `MainActivity` on runtime init/start failure; it logs and finishes instead. Fallback to Cocos was causing a crash and is contrary to the hard migration goal.
- `ForceLandscapeHelper` now treats only `SdlRuntimeActivity` as an engine launch.
- `platforms/android/app/AndroidManifest.xml` marks `MainActivity` non-exported and removes its VIEW/USB intent filters so external intents do not accidentally enter the legacy Cocos activity.

2. The long SDL runtime log successfully used the SDL path and fixed 1920x1080 surface:

```text
[flutter-surface] AndroidRuntimeBridge set game surface ... size=1920x1080 requested=1920x1080
[launcher] SdlRuntimeActivity.surfaceCreated surface-view size=1920x1080
[android-egl-presenter] queue-android-egl ... output=1920x1080 viewport=0,0,1920x1080 rect=0,0,1920x1080
```

But it also showed a second surface attachment shortly after startup, caused by the in-game Flutter overlay creating a Flutter `Texture`/`SurfaceProducer` for the game surface. User reported the floating menu and mouse path as useless and seemingly tied to game frame rate, and also reported a black strip / alignment issue. This double-surface overlay path is no longer the right default while migrating to SDL3.

Fix applied:

- `SdlRuntimeActivity` no longer installs the in-game Flutter overlay by default. The native `SurfaceView` remains the single active 1920x1080 game presenter.
- This removes the floating Flutter menu/mouse layer from the hot path and prevents it from replacing the `SurfaceView` with a Flutter texture-backed surface.
- The overlay code is left in the file for now as dormant code so it can be reworked later as a proper SDL/runtime menu, but it is not attached during game startup.

3. Black area / bottom alignment:

- `SdlRuntimeActivity` now applies immersive sticky fullscreen in `onCreate`, `onResume`, and `onWindowFocusChanged`.
- This hides Android status/navigation bars for the game activity and reduces bottom black bar artifacts caused by system decor shrinking the available surface area.
- The game buffer contract remains fixed 1920x1080. The view is still aspect-preserving and centered; it must not stretch to device aspect ratio.

Important invariant kept:

- Game/native rendering surface remains 1920x1080.
- No new Cocos path was introduced.
- Legacy `MainActivity` is still present in source for now because some copied Java/Kotlin bridge code still references Cocos/KR2 static helpers, but launch flow no longer routes to it. Later migration should split static bridge methods out of `KR2Activity` and then stop copying/declaring `MainActivity` in the Flutter shell entirely.
