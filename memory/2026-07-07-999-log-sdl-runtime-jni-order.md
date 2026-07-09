# 2026-07-07 999.log SDL runtime JNI ordering fix

## Hard target

- Continue migration to Flutter + SDL3.
- Remove Cocos from the new runtime path.
- Keep the game surface fixed at 1920x1080.
- Keep code fast and compatible; do not add per-frame validation.

## Log inspected

- Device log: `/root/log/999.log`
- App package: `org.github.krkr2`
- Log length: 171512 lines.

## Main failure in this log

The SDL runtime path crashed before native runtime initialization.

Important lines:

- `13748`: `am_crash` from `java.lang.UnsatisfiedLinkError`
- `19615`: `KR2Activity.configureFileLogging(boolean, String)` has no JNI implementation loaded yet
- `19643`: `KR2Activity.setUseFFmpegImageDecoder(boolean)` has no JNI implementation loaded yet
- `19695`-`19699`: `FATAL EXCEPTION: main`, crash at `SdlRuntimeActivity.onCreate(SdlRuntimeActivity.kt:106)`
- `23548`-`23584`: same launch crash repeats

The native symbols exist in `platforms/android/cpp/krkr2_android.cpp`, so the problem is call ordering, not missing C++ implementation.

## Root cause

`SdlRuntimeActivity` is a plain Android activity for the SDL3/Flutter path. It does not inherit the old `KR2Activity/Cocos2dxActivity` load/init sequence.

Before the fix, `SdlRuntimeActivity.onCreate()` called these `KR2Activity` native methods before forcing `libkrkr2` load and `AndroidRuntimeBridge.nativeInitRuntime()`:

- `LauncherPrefs.configureNativeLogging(this)` -> `KR2Activity.configureFileLogging(...)`
- `KR2Activity.setUseFFmpegImageDecoder(...)`
- `KR2Activity.setFFmpegDecodeMode(...)`

That ordering produced `UnsatisfiedLinkError` and killed the process.

## Fix applied

File changed:

- `platforms/android/app/java/org/github/krkr2/SdlRuntimeActivity.kt`

New ordering:

1. Read FFmpeg preferences from launcher storage.
2. Call `AndroidRuntimeBridge.setApplicationContext(applicationContext)`.
3. Call `AndroidRuntimeBridge.ensureInitialized()`.
4. Only if `runtimeReady` is true:
   - configure native file logging through `LauncherPrefs.configureNativeLogging(this)`
   - call `KR2Activity.setUseFFmpegImageDecoder(...)`
   - call `KR2Activity.setFFmpegDecodeMode(...)`
5. If runtime init failed, create/keep the unified launcher log session with `LauncherPrefs.beginUnifiedLogSession(this)` and write the bridge failure message, without calling native KR2Activity setters.
6. `ensureSdlJavaReady(this)` remains short-circuited behind successful runtime init.

This removes the startup JNI crash without catching and ignoring `UnsatisfiedLinkError` at the call site. The native setters are still treated as real native runtime calls; they now run only after the runtime is initialized.

## Verification status

- `git diff --check` passed.
- Android build command attempted:
  - `./gradlew :app:assembleDebug`
  - Working directory: `platforms/android`
  - Result: blocked before compile because this container has no Android SDK configured:
    - missing `ANDROID_HOME`
    - missing `platforms/android/local.properties`

## Next device check

After installing a build with this change, search the fresh log for:

- `UnsatisfiedLinkError`
- `No implementation found`
- `SdlRuntimeActivity.onCreate`
- `configureFileLogging`
- `setUseFFmpegImageDecoder`

Expected result:

- No `UnsatisfiedLinkError` from `SdlRuntimeActivity.onCreate`.
- Native logging line should appear only after runtime init, for example:
  - `KrKr2Breadcrumb: [log] configureFileLogging enabled=1 path=...`

