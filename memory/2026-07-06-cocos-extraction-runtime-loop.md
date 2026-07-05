# 2026-07-06 Cocos Extraction And Runtime Loop Continuation

## Scope

Only modify:

- `/root/kiriki-work/KiriKiri-LauncherC`

Reference only:

- `/root/kiriki-work/AetherKiri`
- `/root/kiriki-work/docs`
- `/root/kiriki-work/kirikiroid2-web`
- `/root/kiriki-work/KrKr2-Next`
- `/root/kiriki-work/krkrsdl2-main`
- `/root/kiriki-work/krkrsdl3-main`
- `/root/kiriki-work/SDL-release-3.4.10`

Hard target remains:

- migrate to Flutter + SDL3;
- gradually remove Cocos2dx;
- keep the native/game buffer fixed at `1920x1080`;
- copy proven complete rendering/lifecycle chains, not disconnected fragments;
- avoid hot-path graphical validation, checksums, `glReadPixels`, or noisy
  per-frame checks unless behind diagnostics.

## Global Agent Instruction

The user requested global `/root/AGENTS.md` with:

```text
No optional commentary.
Prefer concise correct code.
```

This means future continuation should be direct and implementation-focused.

## Work Completed In This Pass

### Runtime engine loop extraction

Added:

- `cpp/core/environ/runtime/RuntimeEngineLoop.h`
- `cpp/core/environ/runtime/RuntimeEngineLoop.cpp`

Purpose:

- move host-neutral launch/frame work out of `TVPMainScene`;
- make Cocos only one temporary host implementation;
- provide a path for Android SDL3/Flutter host to start and run the engine
  without calling Cocos scene APIs.

Functions added:

- `TVPRuntimeConfigureGameLaunch(request)`
  - calls `TVPCheckStartupPath(request.gamePath)`;
  - chooses preference root from `request.preferenceRoot`, or the parent of
    `request.gamePath` when empty;
  - calls `IndividualConfigManager::UsePreferenceAt(...)`.
- `TVPRuntimeStartApplication(path)`
  - currently calls `Application->StartApplication(path)`;
  - returns true to preserve existing behavior because current
    `StartApplication` returns false even after normal startup.
- `TVPRuntimeRunApplicationFrame(deltaSeconds)`
  - calls `Application->Run()`.
- `TVPRuntimeRecycleFrameResources()`
  - calls `iTVPTexture2D::RecycleProcess()`.

Modified:

- `cpp/core/environ/CMakeLists.txt`
  - now compiles `RuntimeEngineLoop.cpp`.
- `cpp/core/environ/cocos2d/CocosRuntimeHost.cpp`
  - `RunFrame` now calls `TVPRuntimeRunApplicationFrame`.
- `cpp/core/environ/cocos2d/MainScene.cpp`
  - `startupFrom` now uses `TVPRuntimeConfigureGameLaunch`;
  - removed local `TVPPathParent`;
  - `doStartup` now uses `TVPRuntimeStartApplication`.

Default Cocos-on runtime behavior should remain the same.

### Android SDL3 runtime host skeleton

Modified:

- `platforms/android/cpp/krkr2_android.cpp`

When `KRKR2_ENABLE_COCOS_HOST=0`, it now registers a non-Cocos
`TVPAndroidSDLRuntimeHost` during JNI init:

- host name: `android-sdl3`;
- `StartGame` uses `TVPRuntimeConfigureGameLaunch` and
  `TVPRuntimeStartApplication`;
- `RunFrame` calls:
  - `TVPRuntimeRunApplicationFrame`;
  - `TVPRuntimeRecycleFrameResources`;
  - `TVPRuntimePumpScreenPresenter("android-sdl3")`;
- frame metrics are fixed `1920x1080`, matching the hard game-buffer contract.

This is still a skeleton. The current Java Android activity still inherits the
Cocos activity, and `KRKR2_ENABLE_COCOS_HOST` should not be flipped OFF in
Gradle yet.

### Java context facade cleanup

Modified:

- `platforms/android/app/java/org/tvp/kirikiri2/KR2Activity.java`
- `platforms/android/app/java/org/tvp/kirikiri2/ShowTextInputTask.java`
- `platforms/android/app/java/org/tvp/kirikiri2/DialogMessage.java`

Changes:

- `KR2Activity.requireActivityContext()`;
- `KR2Activity.requireApplicationContext()`;
- text input/dialog code no longer calls `Cocos2dxActivity.getContext()` or
  `KR2Application.context` directly.

This is a small Java step toward host decoupling. `KR2Activity` still extends
`Cocos2dxActivity`.

### Cocos-gated native UI and Android fallback paths

Modified:

- `cpp/core/environ/CMakeLists.txt`
- `cpp/core/environ/ConfigManager/LocaleConfigManager.h`
- `cpp/core/environ/ConfigManager/LocaleConfigManager.cpp`
- `cpp/core/environ/DumpSend.cpp`
- `cpp/core/environ/android/AndroidUtils.cpp`
- `cpp/core/environ/ui/FlutterGameMenuBridge.cpp`
- `platforms/android/cpp/krkr2_android.cpp`

Changes:

- legacy Cocos UI/form source files now compile only when
  `KRKR2_ENABLE_COCOS_HOST`;
- `LocaleConfigManager::initText(...)` Cocos UI overloads are gated;
- Cocos `HttpClient`/base64 dump upload path is gated; no-Cocos fallback clears
  dumps;
- Android Cocos scheduler/event queue registration is gated;
- `Android_PushEvents` runs the function directly in no-Cocos builds;
- Android legacy input forwarding to Cocos is gated;
- Flutter menu bridge only uses `TVPMainScene` for Cocos-only window manager and
  virtual mouse actions.

### Visual module no-Cocos progress

Modified:

- `cpp/core/visual/LoadPVRv3.cpp`
- `cpp/core/visual/FontImpl.cpp`
- `cpp/core/visual/impl/TVPScreen.cpp`
- `cpp/core/visual/CMakeLists.txt`
- `cpp/core/visual/RenderManager.h`
- `cpp/core/visual/ogl/krkr_texture2d.h`

Changes:

- removed unused hard include `<cocos/base/pvr.h>`;
- bundled `NotoSansCJK-Regular.ttc` loading now uses
  `TVPLoadBundledConfigText(...)` instead of `cocos2d::FileUtils`;
- no-Cocos Android `tTVPScreen` reports fixed `1920x1080`;
- visual CMake now only finds/links `cocos2dx` when
  `KRKR2_ENABLE_COCOS_HOST`;
- visual CMake exports `KRKR2_ENABLE_COCOS_HOST=1/0`;
- `krkr_texture2d.h` now acts as a transitional boundary:
  - Cocos build still aliases `krkr::Texture2D` to `cocos2d::Texture2D`;
  - no-Cocos build gets a minimal standalone `krkr::Texture2D` stub sufficient
    for adapter texture compilation.

The no-Cocos `Texture2D` stub is only a compatibility bridge. It is not the
final high-performance SDL3/GPU texture implementation.

### Movie module no-Cocos progress

Modified:

- `cpp/core/movie/CMakeLists.txt`
- `cpp/core/movie/ffmpeg/KRMoviePlayer.h`
- `cpp/core/movie/ffmpeg/KRMoviePlayer.cpp`

Changes:

- movie CMake now only finds/links `cocos2dx` when
  `KRKR2_ENABLE_COCOS_HOST`;
- movie CMake exports `KRKR2_ENABLE_COCOS_HOST=1/0`;
- Cocos `TVPYUVSprite` overlay presenter declarations/definitions are gated;
- no-Cocos `MoviePlayerOverlay` builds as a headless `TVPMoviePlayer` wrapper;
- no-Cocos `MoviePlayerOverlay::AddVideoPicture` returns immediately to avoid
  filling decode queues when there is no Cocos video presenter;
- fixed the nested function definition from
  `VideoPresentOverlay::BitmapPicture::swap` to
  `TVPMoviePlayer::BitmapPicture::swap`.

This is a compatibility step only. Proper SDL3 video presentation still needs a
new presenter path.

## Current Verification

Passed locally:

- `git diff --check`

Could not run locally:

- CMake/Ninja build;
- Android Gradle build;
- Flutter build/tests.

Reason:

- this environment currently has no `cmake`, `ninja`, `java`, `flutter`,
  `dart`, `clang++`, or `g++` on PATH.

## Current Known Remaining Cocos Dependencies

Do not flip Android Gradle to `KRKR2_ENABLE_COCOS_HOST=OFF` yet.

Still Cocos-dependent:

- Android Java host:
  - `KR2Activity extends Cocos2dxActivity`;
  - `KR2GLSurfaceView extends Cocos2dxGLSurfaceView`;
  - `settings.gradle` includes `:cocos2dx`;
  - app Gradle depends on `project(':cocos2dx')`.
- Cocos scene/window layer:
  - `cpp/core/environ/cocos2d/MainScene.cpp`;
  - `TVPWindowLayer` is still the legacy Cocos window/layer implementation.
- Visual render adapter:
  - Cocos build still uses `krkr::Texture2D = cocos2d::Texture2D`;
  - no-Cocos stub exists only so interfaces can compile while SDL3 presenter
    takes over.
- Movie/video:
  - Cocos video overlay is gated, but no real SDL3 video presenter exists yet.

## Next Recommended Steps

1. Add a real no-Cocos Android frame pump from Flutter/SDL3 so
   `iTVPRuntimeHost::RunFrame` is called without Cocos `GLSurfaceView`.
2. Extract a Cocos-free `iWindowLayer`/host window layer based on AetherKiri
   `ui_stubs.cpp`.
3. Replace `krkr::Texture2D` stub with a real SDL3/GPU texture object or remove
   adapter texture use from the no-Cocos path.
4. Add SDL3 movie/video presenter if video overlay support is required in the
   new architecture.
5. Only after native no-Cocos compiles, start removing Java/Gradle Cocos
   dependencies.

## Reference Mapping To Keep In Mind

- AetherKiri `EngineLoop` is the correct extraction shape for lifecycle and
  frame ticking.
- AetherKiri Android JNI bridge is the correct model for JavaVM/context/surface
  ownership.
- krkrsdl3 is the better reference for SDL3 deterministic present loop.
- krkrsdl2 is useful for event/window ownership ideas, but should not be copied
  wholesale.

