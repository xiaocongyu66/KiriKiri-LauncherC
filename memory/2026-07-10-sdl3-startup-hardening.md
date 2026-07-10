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

