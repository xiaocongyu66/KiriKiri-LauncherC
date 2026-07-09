# 2026-07-07 sdl3.log JNI crash and Android writable path fix

## Hard target

- Continue migrating to Flutter + SDL3.
- Remove Cocos from the runtime path instead of depending on Cocos Java/GL activity setup.
- Keep the game surface contract fixed at 1920x1080.
- Keep compatibility and performance high; do not add per-frame heavy validation.

## Log inspected

- Device log: `/root/log/sdl3.log`
- Relevant process/package: `org.github.krkr2`
- The log also contains unrelated AetherKiri/Godot activity noise and many system/Oplus warnings; those are not the target crash.

## Actual crash

The current SDL3 runtime activity starts, creates the fixed 1920x1080 `SurfaceView`, then aborts inside native start.

Important lines in `/root/log/sdl3.log`:

- `24681`: `JNI DETECTED ERROR IN APPLICATION: obj == null`
- `24683`: from `org.github.krkr2.AndroidRuntimeBridge.nativeStartGame(java.lang.String, java.lang.String)`
- `25249`: `Fatal signal 6 (SIGABRT)`
- `25323`: abort message repeats `obj == null` in `CallObjectMethodV`
- `25343`-`25350`: native backtrace enters Cocos:
  - `_JNIEnv::CallObjectMethod`
  - `_getClassID`
  - `cocos2d::JniHelper::getStaticMethodInfo`
  - `cocos2d::JniHelper::callStaticStringMethod`
  - `getApkPath`
  - `cocos2d::FileUtilsAndroid::init`
  - `cocos2d::FileUtils::getInstance`
- `25351`-`25359`: KiriKiri path that triggered it:
  - `TVPGetInternalPreferencePath`
  - `GlobalConfigManager::GetFilePath`
  - `iSysConfigManager::Initialize`
  - `GlobalConfigManager::GetInstance`
  - `IndividualConfigManager::GetValue<bool>`
  - `TVPConsoleLog`
  - `TVPAddLog`
- `25371`-`25373`: Java caller:
  - `SdlRuntimeActivity.startGameIfReady`
  - `SdlRuntimeActivity.surfaceCreated`

The same crash reproduces again around the later tombstone section near lines `30436`-`30466`.

## Root cause

`SdlRuntimeActivity` is the new plain Android/SDL3 activity, not a Cocos activity. It correctly calls:

- `AndroidRuntimeBridge.setApplicationContext(applicationContext)`
- `AndroidRuntimeBridge.ensureInitialized()`
- `AndroidRuntimeBridge.ensureSdlJavaReady(this)`
- `AndroidRuntimeBridge.setGameSurface(...)`
- `AndroidRuntimeBridge.startGame(...)`

But the native startup still reaches legacy `cpp/core/environ/cocos2d/MainScene.cpp::TVPGetInternalPreferencePath()`.

That function used:

```cpp
cocos2d::FileUtils::getInstance()->getWritablePath()
```

On the SDL3 runtime path, Cocos Java helper state/class loader was never initialized by `Cocos2dxActivity/Cocos2dxHelper.init`, so Cocos' internal JNI helper calls `CallObjectMethod` on a null object while trying to resolve `org/cocos2dx/lib/Cocos2dxHelper.getAssetsPath`.

This is not an SDL surface crash and not a missing native library. It is a remaining Cocos dependency in the native path initialization.

## Fix applied

File changed:

- `cpp/core/environ/cocos2d/MainScene.cpp`

Added Android runtime writable path helper:

- Uses existing `TVPGetAppStoragePath()`.
- Picks the first non-empty app storage path.
- Ensures a trailing slash.
- Does not call Cocos `FileUtils`.

Changed:

- `TVPGetDataPath()` on Android now returns the Android app storage path first.
- `_TVPGetInternalPreferencePath()` on Android now builds `.preference/` under the Android app storage path first.
- Only if no Android app storage path is available does it fall back to Cocos `FileUtils`.

This keeps legacy Cocos behavior available as fallback, while making the SDL3 activity startup path no longer depend on Cocos JNI/classloader state for preference initialization.

## Verification status

- Local Gradle native build was attempted:
  - Command: `./gradlew :app:externalNativeBuildDebug -Pkrkr2NoCocosHost=false --parallel --max-workers=8`
  - Result: blocked before compilation because this container has no Android SDK configured:
    - `SDK location not found`
    - missing `ANDROID_HOME` or `platforms/android/local.properties`
- No code-level compile result was produced in this environment.

## Next checks after CI/device build

1. Re-run with SDL3 runtime enabled.
2. Confirm `sdl3.log` no longer shows:
   - `JNI DETECTED ERROR IN APPLICATION: obj == null`
   - `cocos2d::FileUtilsAndroid::init`
   - `getApkPath`
   - `_getClassID`
3. If another Cocos JNI crash appears, treat it as another remaining Cocos path and replace it with Android/SDL3-native logic rather than initializing Cocos Java globally.
4. Keep this direction: remove Cocos usage from new runtime path piece by piece; do not fix SDL3 by reintroducing `Cocos2dxActivity`.

