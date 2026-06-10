# Flutter Launcher Migration

`flutter_launcher/` is the new Flutter UI surface for KiriKiri-LauncherC. It is intentionally kept separate from the Cocos/TJS runtime so the launcher can become multi-platform without forcing an immediate engine rewrite.

## Goals

- Replace the Cocos `TVPGameMainMenu` UI surface incrementally with Flutter screens.
- Reuse existing UI assets from `ui/cocos-studio/img` and `ui/cocos-studio/NotoSansCJK-Regular.ttc`.
- Keep native engine/runtime builds in GitHub Actions instead of relying on local builds.
- Support Android first, then desktop/web shells through the same Flutter codebase.

## Native Bridge Contract

Flutter must call the engine through C ABI + `dart:ffi`, not Kotlin-only APIs. Kotlin/Activity code can still host Android UI and permissions, but engine actions that cross into C++ should be exported as stable C functions.

Initial exported C API in `cpp/core/environ/ui/FlutterGameMenuBridge.cpp`:

| C API | Purpose |
| --- | --- |
| `KR2LauncherGetMainMenuJson()` | Returns active TJS main menu as UTF-8 JSON. |
| `KR2LauncherActivateMenuItem(const char*)` | Invokes a menu item by slash-separated index path. |
| `KR2LauncherLaunchGame(const char*)` | Enters a game through the existing native `TVPMainScene::startupFrom` path. |

The Dart side loads `libkrkr2.so`/process symbols from `flutter_launcher/lib/src/bridge/launcher_bridge.dart`. Game scanning should follow the same C ABI pattern next. Platform UI only handles file pickers, permissions, and hosting; entering the game is done through `KR2LauncherLaunchGame`.

## `TVPGameMainMenu` Replacement Seam

`cpp/core/environ/ui/GameMainMenu.cpp` now calls `TVPShowFlutterGameMainMenu()` before opening the legacy `TVPInGameMenuForm`. On Android, `FlutterGameMenuBridge.cpp` directly calls `MainActivity.showFlutterGameMainMenu()` through JNI. Other platforms currently return `false`, so existing desktop builds keep the legacy menu until their Flutter host integration is added.

The Flutter side now has a dedicated game-menu page that renders `KR2LauncherGetMainMenuJson()` data and invokes `KR2LauncherActivateMenuItem()` for leaf actions. This makes the visual replacement ready independently of the Android/desktop host handoff.

Next bridge step per platform:

1. Android: `TVPShowFlutterGameMainMenu()` calls `MainActivity.showFlutterGameMainMenu()`, which opens `GameMenuFlutterActivity` on the `/game-menu` route. Returning `true` prevents the legacy Cocos `TVPInGameMenuForm` from opening.
2. Desktop: add an equivalent direct platform implementation through the Flutter runner/window integration.
3. Flutter: keep improving `/game-menu` as the shared in-game menu surface, reusing assets from `ui/cocos-studio`.

## Migration Boundaries

Detailed ownership rules for what stays in platform code and what moves to Flutter or C ABI are documented in `docs/platform-migration-boundaries.md`.

## Android Build Direction

The Android CI no longer builds the legacy Kotlin/Compose launcher app. `build-android.yml` now creates a Java-based Flutter Android shell, copies the existing Android resources into it, wires the native `krkr2` CMake target into that Flutter app, and uploads only the Flutter APK. Kotlin code under `platforms/android/app/java/org/github/krkr2` is now reference/legacy code until the remaining scanner and permission helpers are moved behind C ABI or Flutter platform-host code.
