# 2026-07-06 SDL runtime UI/IME and context bridge slice

## Goal

Continue the hard migration target toward Flutter + SDL3 while keeping the
shipping Cocos path intact by default.

Reference rule from AetherKiri still applies:

- host-owned frame loop;
- full-frame render/present to the fixed native surface;
- dirty state is only a frame/upload hint;
- avoid hot-path pixel readback/checksum validation.

## Changes

### No-Cocos Android UI host

Added:

- `platforms/android/app/java/org/tvp/kirikiri2/NativeUiHost.java`

It is a host-neutral Java UI facade for:

- current `Activity`/application context;
- IME dummy edit attachment;
- message boxes / input boxes;
- plugin toast;
- native text/key/dialog callbacks.

The legacy `KR2Activity` now attaches this host to its Cocos `mFrameLayout`.
The new `SdlRuntimeActivity` attaches it to its plain `FrameLayout` plus the
fixed `1920x1080` game `SurfaceView`, so IME coordinates are scaled from game
space into the centered letterboxed surface.

Updated:

- `KR2Activity.java`
- `ShowTextInputTask.java`
- `DummyEdit.java`
- `DialogMessage.java`
- `SDLInputConnection.java`
- `krkr2_android.cpp`

Why:

- no-Cocos runtime cannot depend on `KR2Activity.sInstance` or
  `Cocos2dxActivity.mFrameLayout`;
- script dialogs and IME must keep working when `SdlRuntimeActivity` owns the
  Activity;
- Java text input and message box native callbacks now route through
  `NativeUiHost` JNI instead of directly through `KR2Activity` only.

### SDL Java bootstrap and bridge failure handling

Updated:

- `AndroidRuntimeBridge.kt`
- `SdlRuntimeActivity.kt`

Added:

- guarded `System.loadLibrary("krkr2")` state;
- `lastFailureMessage()`;
- `ensureSdlJavaReady(Activity)` mirroring the legacy
  `SDL.setupJNI() -> SDL.initialize() -> SDL.setContext(activity)` chain;
- runCatching guards around bridge JNI calls;
- startup logging when native/SDL Java init fails.

Why:

- previous no-Cocos activity could silently continue to a black screen if
  native init failed;
- SDL Java setup was still only performed by `KR2Activity`.

### AndroidUtils context fallback

Updated:

- `cpp/core/environ/android/AndroidUtils.cpp`

Change:

- `GetAndroidContext()` now falls back from `KR2Activity.GetInstance()` to the
  application context supplied through `krkr_GetApplicationContext()`;
- package name, APK path, internal/external app storage no longer require
  `KR2Activity.sInstance`;
- cleaned local JNI refs in the touched context/file helpers.

Why:

- `SdlRuntimeActivity` intentionally does not set `KR2Activity.sInstance`;
- startup path, dump path, package/version/storage helpers still need a valid
  Android context.

### SDL3 runtime host and build wiring

Updated:

- `SdlRuntimeActivity.kt`
- `platforms/android/app/build.gradle`
- `.github/workflows/build-android.yml`
- `cpp/core/environ/CMakeLists.txt`

Change:

- no-Cocos activity now forwards lifecycle, low-memory, key, hover, mouse wheel,
  touch, and launch path ranking closer to legacy `KR2Activity`;
- optional Gradle property `-Pkrkr2NoCocosHost=true` passes
  `-DKRKR2_ENABLE_COCOS_HOST=OFF` to native CMake without changing default;
- generated Flutter Android shell also emits the same optional CMake argument;
- `core_environ_module` directly links `core_render_sdlgpu_module` because it
  directly uses SDL_GPU presenter/cache types.

## Verification

Passed:

- `git diff --check`

Attempted:

```sh
/bin/sh /root/kiriki-work/KiriKiri-LauncherC/platforms/android/gradlew \
  -p /root/kiriki-work/KiriKiri-LauncherC/platforms/android \
  :app:compileDebugKotlin :app:compileDebugJavaWithJavac
```

Blocked by environment:

- Android SDK is not configured:
  `SDK location not found. Define a valid SDK location with ANDROID_HOME or local.properties`.

Available local tools:

- `java`

Missing local tools remain:

- Android SDK path / `ANDROID_HOME`;
- `cmake`, `ninja`, `clang++`, `g++`, `flutter`, `dart`.

## Next

- Verify on CI/device with `-Pkrkr2NoCocosHost=true` as an audit build only.
- Continue removing `KR2Activity` static Java surface from native helpers that
  are still safe to lift.
- Do not flip the default shipping build away from Cocos until no-Cocos launch,
  IME, dialog, audio, lifecycle, and video overlay are proven.
