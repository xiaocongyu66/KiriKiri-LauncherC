# 2026-07-11 Android Java Cocos Cut

## Hard Requirements

- Continue the migration toward Flutter + SDL3.
- Keep the native/game surface fixed at 1920x1080.
- Keep the floating Flutter overlay as input/menu only; it must not create a second game presenter surface.
- Remove Cocos from the active Android runtime path instead of patching Cocos frame/render behavior.
- Keep hot render/input paths lean. Diagnostics must be optional, not always-on validation/log spam.

## Latest Runtime State Before This Cut

- The current stable runtime path is `LauncherActivity -> SdlRuntimeActivity -> AndroidRuntimeBridge -> SDL runtime host`.
- `SdlRuntimeActivity` owns a fixed-size `SurfaceView` with `holder.setFixedSize(1920, 1080)`.
- Flutter overlay is enabled for menu/input only and no longer schedules/creates a Flutter `Texture` game surface.
- Latest reviewed log `/root/log/20260711080814267.log` showed no uncaught Java crash, no `UnsatisfiedLinkError`, no `MainActivity` launch, and no Flutter `createGameSurfaceTexture` entries.

## What Was Removed In This Step

- Deleted `platforms/android/app/java/org/github/krkr2/MainActivity.kt`.
  - This was the old Android game activity that extended `KR2Activity`.
  - It contained the old Flutter game surface producer path and could recreate a second presenter surface.
  - It is no longer declared in the manifest and should not be restored.
- Rewrote `platforms/android/app/java/org/tvp/kirikiri2/KR2Activity.java` into a Cocos-free static compatibility bridge.
  - It no longer extends `org.cocos2dx.lib.Cocos2dxActivity`.
  - It no longer imports or creates `Cocos2dxGLSurfaceView`.
  - It no longer calls Cocos lifecycle methods, `super.onCreate`, Cocos GLView setup, or Cocos SDL bootstrap.
  - It only preserves static JNI/platform helpers still called from native code: memory info, app version, FFmpeg preference JNI, logging JNI, IME bridge, message/input/plugin toast bridge, launch path helpers, simple file helpers, locale, and orientation.
- Removed Android Gradle app dependency on `project(':cocos2dx')`.
- Removed `:cocos2dx` from `platforms/android/settings.gradle`.
- Removed Cocos/OPPO keep and dontwarn rules from `platforms/android/app/proguard-rules.pro`.

## Why This Is A Large Cocos Cut

Before this step, even though normal launch already used `SdlRuntimeActivity`, the Android app still packaged Cocos Java and kept a dormant `MainActivity -> KR2Activity -> Cocos2dxActivity -> Cocos2dxGLSurfaceView` chain. That chain could be revived by manifest merge, reflection, or future accidental intent use, and it kept Cocos Java on the runtime classpath.

After this step, the active Android Java layer has no Cocos Activity dependency and Gradle no longer packages the Cocos Java module. The remaining Cocos code is native/CMake-side legacy code behind `KRKR2_ENABLE_COCOS_HOST`, plus non-Android desktop legacy paths and old source files that are not part of the Android no-Cocos runtime.

## Compatibility Bridge Notes

`KR2Activity` is intentionally still named `org.tvp.kirikiri2.KR2Activity` for now because native Android code still performs JNI lookups against that class name. It is no longer an Activity. Treat it as a temporary bridge class until native lookups are moved to a clearer Cocos-free name such as `AndroidRuntimeBridge` or `NativeUiHost`.

Preserved methods include:

- `updateMemoryInfo`, `getAvailMemory`, `getUsedMemory`
- `GetVersion`
- `showTextInput`, `hideTextInput`
- `ShowMessageBox`, `ShowInputBox`, `ShowPluginToast`
- `MessageController`
- `getLaunchGamePath`, `getLaunchGameDir`
- `GetDataPath`, `getStoragePath`, `getLocaleName`
- `RenameFile`, `DeleteFile`, `WriteFile`, `CreateFolders`, `isWritableNormalOrSaf`
- `setUseFFmpegImageDecoder`, `setFFmpegDecodeMode`, `configureFileLogging`, `nativeLauncherLog`

## Native Follow-Up Required

- Android CMake now force-sets `KRKR2_ENABLE_COCOS_HOST=OFF`; Gradle also passes `OFF` as a fixed argument. A stale cache or `-Pkrkr2NoCocosHost=false` can no longer re-enable Cocos in Android builds.
- The Android app no longer packages `ui/cocos-studio` as app assets. Flutter assets and SDL runtime resources remain independent.
- Move native JNI class lookups away from `org/tvp/kirikiri2/KR2Activity` into true Cocos-free bridge classes.
- `AndroidUtils.cpp` still has `KR2ActJavaPath` pointing at `KR2Activity`. This is acceptable only as a transition because the Java class is no longer Cocos-backed.
- `TVPGetDriverPath()` was adjusted to use static `KR2Activity.getStoragePath()` when no Activity instance exists, because `KR2Activity` no longer has an instance.
- Next stronger native cut should remove or hard-disable Android CMake paths that can re-enable `KRKR2_ENABLE_COCOS_HOST` from Gradle parameters.
- Another later cut should address `iWindowLayer::GetPrimaryArea()` still exposing `cocos2d::Node *` in a shared interface, especially for movie overlay paths.

## Verification

- Attempted Gradle verification locally with `ANDROID_HOME=/root/aidepro-tools/android-sdk`.
- `:app:compileDebugKotlin :app:compileDebugJavaWithJavac` without SDK env failed because `local.properties` only has `flutter.sdk=/root/flutter` and no `sdk.dir`.
- Retried with Android SDK env. Build proceeded through Flutter/resource setup but failed in local environment at AAPT2 daemon startup while processing AndroidX resources.
- Retried with `-x processDebugResources`; Kotlin still needs the generated `R.jar` and failed with a classpath snapshot check because resources were skipped.
- Java source compilation was then verified directly with `:app:compileDebugJavaWithJavac -x processDebugResources -x compileDebugKotlin`; it passed.
- This looks like a local Android tool/resource processing issue, not a direct Java source error. CI should be used as the final build authority for this cut.

## Do Not Regress

- Do not restore `MainActivity.kt` as a game runtime fallback.
- Do not re-add `implementation project(':cocos2dx')` to Android app dependencies.
- Do not let overlay call Flutter game surface creation again.
- Do not change game surface sizing away from fixed 1920x1080.
- Do not reintroduce `Cocos2dxActivity` or `Cocos2dxGLSurfaceView` into `KR2Activity`.
