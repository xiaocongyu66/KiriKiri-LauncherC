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
