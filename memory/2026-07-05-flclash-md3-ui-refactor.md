# 2026-07-05 FlClash + MD3 Flutter UI refactor

## User direction

- Continue migrating the project toward Flutter + SDL3 and away from old Cocos2dx runtime/presenter UI.
- For this turn, UI direction was narrowed: only use `/root/FlClash-main` as the style/reference project.
- Use Flutter Material 3 and FlClash component ideas instead of ad hoc custom UI.
- Abandon old launcher UI assets where possible, especially the cocos-studio PNG icon buttons in the Flutter overlay.

## Skills used as local guidance

The Flutter skills installed from `https://github.com/flutter/skills` are not hot-loaded into this running Codex session until restart, so their `SKILL.md` files were read directly and used as local instructions:

- `flutter-build-responsive-layout`: use `LayoutBuilder`, parent constraints, `Expanded`, and lazy `GridView.builder`/`ListView.builder`; avoid hardware/orientation assumptions.
- `flutter-fix-layout-issues`: fix unbounded scrollables and likely overflow points by constraining fields, sliders, segmented buttons, and list/grid panes.
- `flutter-apply-architecture-best-practices`: move reusable presentation components out of page files and keep page widgets thinner.
- `flutter-add-widget-test`: existing smoke test remains relevant; no new test could be run locally because `flutter`/`dart` are not installed.

## FlClash reference patterns used

Reference files read:

- `/root/FlClash-main/lib/application.dart`
- `/root/FlClash-main/lib/pages/home.dart`
- `/root/FlClash-main/lib/widgets/card.dart`
- `/root/FlClash-main/lib/widgets/list.dart`
- `/root/FlClash-main/lib/widgets/setting.dart`
- `/root/FlClash-main/lib/widgets/theme.dart`
- `/root/FlClash-main/lib/views/application_setting.dart`

Adopted patterns:

- FlClash `CommonCard` idea: selected state, filled/plain variants, MD3 surface/secondary-container colors, no elevation-heavy cards.
- FlClash `InfoHeader` idea: compact section headers with icon + label + actions.
- FlClash `ListItem` idea: settings rows are `ListTile`-based, with switch/options/actions expressed through small delegates/wrappers instead of bespoke row layouts everywhere.
- FlClash navigation approach: Material 3 `NavigationBar`/`NavigationRail` with `secondaryContainer` selected indicator and neutral surfaces.
- FlClash settings organization: settings groups are repeated MD3 sections with header + divider + rows.

## Files changed

- Added `flutter_launcher/lib/src/widgets/md3_components.dart`
  - `LauncherInfo`
  - `LauncherInfoHeader`
  - `LauncherCard`
  - `LauncherSection`
  - `LauncherListItem`
  - `LauncherInfoRow`
  - `LauncherEmptyState`
- Updated `flutter_launcher/lib/src/theme/launcher_theme.dart`
  - Keeps `ThemeData(useMaterial3: true)`.
  - Moved from purple seed to neutral teal seed `0xff386a6a`.
  - Aligns app bar, card, list tile, navigation bar/rail, buttons, chips, and inputs with MD3 surface/secondary-container behavior.
  - Cards/components use 8px radius to avoid the over-rounded old/custom look.
- Updated `flutter_launcher/lib/src/pages/launcher_home_page.dart`
  - Imports `md3_components.dart`.
  - App title now shows `KiriKiri Launcher`, matching the existing smoke test expectation.
  - Replaces many local one-off widgets with `LauncherSection`, `LauncherListItem`, `LauncherInfoRow`, `LauncherEmptyState`, and `LauncherCard`.
  - Game grid now uses `SliverGridDelegateWithMaxCrossAxisExtent` for adaptive columns instead of fixed breakpoints.
  - Mobile layout avoids shrink-wrapped full game grids; it keeps the grid lazy and shows detail inline only when height allows.
  - On short mobile/landscape layouts, selecting a game opens an MD3 bottom sheet for details instead of forcing an unbounded `ListView` + `GridView` stack.
  - Settings controls were made layout-aware: dropdown rows, segmented buttons, and sliders switch to vertical/scrollable forms on narrow widths.
  - The old visible label `菜单按钮透明度` was renamed to `悬浮菜单透明度`; the storage key remains `menu_handler_opa` for compatibility.
- Updated `flutter_launcher/lib/src/pages/game_overlay_page.dart`
  - Removed dependency on old `ResourceIcon`.
  - Floating tray now uses MD3 icons (`Icons.menu_rounded`, `Icons.fit_screen_rounded`, `Icons.mouse_rounded`, `Icons.touch_app_rounded`, `Icons.power_settings_new_rounded`, `Icons.drag_indicator_rounded`).
  - Floating menu panel uses MD3 surfaces, outline variants, icon buttons, `LauncherEmptyState`, and `LauncherListItem`.
- Deleted `flutter_launcher/lib/src/widgets/resource_icon.dart`
  - It is no longer referenced.
- Updated `flutter_launcher/pubspec.yaml`
  - Removed old cocos-studio PNG asset declarations from Flutter assets.
  - Kept the NotoSansCJK font.
- Updated `flutter_launcher/tool/sync_assets.sh`
  - It now syncs only `NotoSansCJK-Regular.ttc` for Flutter launcher use.

## Verification and limitations

- `git diff --check` passed.
- Local `dart format`, `flutter analyze`, and `flutter test` could not run because this machine currently has no `dart` or `flutter` command available.
- GitHub Android build installs Flutter in CI, so CI will be the first full Dart/Flutter syntax validation unless Flutter is installed locally.

## Follow-up recommendations

- After Codex restart, use the installed Flutter skills normally instead of reading the local `SKILL.md` files manually.
- Continue extracting page-local controls into `md3_components.dart` or small feature widgets if `launcher_home_page.dart` remains too large.
- If CI catches Dart formatting differences, run `dart format flutter_launcher/lib flutter_launcher/test` in an environment with Flutter/Dart.
- Later, consider removing the unused old PNG files under `flutter_launcher/assets/cocos-studio/img/` from git; this turn removed references and pubspec declarations, but binary deletion via `apply_patch` was not possible.
