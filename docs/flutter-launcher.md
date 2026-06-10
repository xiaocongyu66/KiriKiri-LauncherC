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

The Cocos right-bottom draggable `TVPGameMainMenu` floating control is being replaced by a shared Flutter overlay route: `/game-overlay`. The overlay starts as the same small draggable handle, expands into the old quick actions, and uses C ABI calls for engine actions.

The Flutter overlay can also render `KR2LauncherGetMainMenuJson()` data and invokes `KR2LauncherActivateMenuItem()` for leaf actions. It is not a separate launcher page; it is layered over the game surface.

Next bridge step per platform:

1. Android: `MainScene` no longer creates the Cocos `TVPGameMainMenu`; `MainActivity` installs a transparent FlutterView on `/game-overlay` instead.
2. iOS: use the same `/game-overlay` route in a transparent `FlutterViewController` layered over the game view when the iOS runner is added to this repository.
3. Desktop: add an equivalent direct platform overlay through the Flutter runner/window integration.
4. Flutter: keep improving `/game-overlay` as the shared in-game floating control, reusing assets from `ui/cocos-studio`.

## Migration Boundaries

Detailed ownership rules for what stays in platform code and what moves to Flutter or C ABI are documented in `docs/platform-migration-boundaries.md`.

## Android Build Direction

The Android CI no longer builds the legacy Kotlin/Compose launcher app. `build-android.yml` now creates a Java-based Flutter Android shell, copies the existing Android resources into it, wires the native `krkr2` CMake target into that Flutter app, and uploads only the Flutter APK. Kotlin code under `platforms/android/app/java/org/github/krkr2` is now reference/legacy code until the remaining scanner and permission helpers are moved behind C ABI or Flutter platform-host code.
