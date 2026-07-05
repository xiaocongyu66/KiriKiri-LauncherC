# 2026-07-06 Android runtime bridge and Cocos init decoupling

## Hard target

- The project target remains Flutter + SDL3.
- Cocos2d-x is compatibility only and must be removed from launch, frame
  ticking, UI, and final presentation.
- The game/native presenter surface remains fixed at `1920x1080`.
- Final visible presentation must be deterministic full-frame. Dirty rects are
  upload/cache/new-frame hints only, never partial final-present rectangles.
- Preferred render chain is AetherKiri-style:
  `engine produced frame -> full target clear/draw -> mark frame dirty -> one
  swap/post only when dirty`.
- Do not add hot-path pixel integrity checks, `glReadPixels`, checksums, or
  repeated `glGet*` validation as correctness mechanisms.

## Why this change

Before this slice, Android native runtime initialization was reachable only
through the Cocos Java/native bootstrap name:

- `cocos_android_app_init(JNIEnv*)`
- `Cocos2dxActivity -> Cocos2dxHelper -> Cocos2dxRenderer`

Even though `KRKR2_ENABLE_COCOS_HOST=OFF` already has a C++ SDL host skeleton,
that host could not be initialized from a Flutter/SDL shell without depending
on the Cocos init entry point. This kept the future no-Cocos path blocked by
Java Activity plumbing instead of native runtime ownership.

## Code changes

- `platforms/android/cpp/krkr2_android.cpp`
  - Split Android native base initialization out of `cocos_android_app_init`.
  - New internal base initializer owns:
    - native logging bootstrap
    - JavaVM capture
    - `krkr::JniHelper::setJavaVM`
    - SDL3 `JNI_OnLoad` invocation through `dlopen("libSDL3.so")`
  - `cocos_android_app_init()` is now a legacy wrapper only.
  - The Android SDL3 runtime host (`android-sdl3`) is now compiled even when
    the Cocos host is compiled in. It is not automatically selected by the
    legacy Cocos path, but a Flutter/SDL shell can explicitly register it.
  - Added JNI bridge functions for a future Flutter/SDL host:
    - `AndroidRuntimeBridge.nativeInitRuntime()`
    - `AndroidRuntimeBridge.nativeStartGame(gamePath, preferenceRoot)`
    - `AndroidRuntimeBridge.nativeRunFrame(deltaSeconds)`
    - `AndroidRuntimeBridge.nativePumpPresenter()`
  - Added `KR2Activity.nativeInitRuntime()` so the current legacy activity also
    explicitly initializes the native runtime without relying solely on the
    Cocos-named hook.

- `platforms/android/app/java/org/tvp/kirikiri2/KR2Activity.java`
  - Calls `nativeInitRuntime()` during `onCreate()` after the native library has
    been loaded by the current superclass path.
  - This preserves legacy behavior while making the initialization boundary
    visible and reusable.

- `platforms/android/app/java/org/github/krkr2/AndroidRuntimeBridge.kt`
  - New no-Cocos-facing Android bridge object.
  - Provides a stable Java/Kotlin callable surface for Flutter/SDL host work:
    `ensureInitialized`, `startGame`, `runFrame`, and `pumpPresenter`.
  - Loads `libkrkr2` on demand and then initializes the native SDL runtime host.

- `.github/workflows/build-android.yml`
  - Copies `AndroidRuntimeBridge.kt` into the generated Flutter Android shell.

## Current ownership after this slice

- Shipping Android still uses `MainActivity : KR2Activity : Cocos2dxActivity`.
- The legacy Cocos path remains enabled by default.
- Flutter launcher APK generation still copies the Cocos Java shell because the
  Java Activity/view layer has not been replaced yet.
- Native initialization is no longer conceptually owned only by the Cocos hook.
- The next no-Cocos Android host can initialize native runtime state directly
  through `AndroidRuntimeBridge`.

## Next migration cut

1. Add a real no-Cocos Android game activity/shell that owns:
   - a root `FrameLayout`;
   - Flutter overlay or Flutter route host;
   - fixed `1920x1080` game `SurfaceTexture`/`SurfaceProducer`;
   - a Choreographer/SDL frame pump calling `AndroidRuntimeBridge.runFrame`.
2. Route game launch from Flutter to that host, then eventually stop starting
   `MainActivity`.
3. Move IME/input dispatch out of `KR2Activity`/`Cocos2dxGLSurfaceView` into a
   platform input bridge that pushes SDL/runtime input directly.
4. Once the Java shell can run without Cocos, pass
   `-DKRKR2_ENABLE_COCOS_HOST=OFF` in a non-shipping audit build.
5. After parity, remove `implementation project(':cocos2dx')`, the Cocos Java
   copy step, and the Cocos native host sources.

## Guardrails for the next agent

- Do not flip shipping builds to `KRKR2_ENABLE_COCOS_HOST=OFF` yet.
- Do not delete `MainActivity`, `KR2Activity`, or Cocos Java files until a
  Flutter/SDL activity can launch a game, present frames, accept input, show
  IME, and survive pause/resume.
- If adding a frame pump, do not also let Cocos `GLSurfaceView.Renderer` tick
  the same engine in the same process.
- Keep final present full-frame and use dirty rect only for upload/cache.
