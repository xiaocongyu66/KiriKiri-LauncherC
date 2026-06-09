# Platform Migration Boundaries

This project should move launcher and runtime-control logic out of Android/Kotlin code over time so the launcher can become truly cross-platform. The target split is:

- **Flutter**: user-facing launcher UI, library browsing, settings pages, diagnostics pages, menu presentation.
- **C ABI / native core**: engine actions, game scanning, game metadata, launch requests, runtime menu data, runtime settings persistence that must be shared across platforms.
- **Platform host code**: permissions, platform pickers, Activity/ViewController/window hosting, native surface lifecycle, OS integrations that cannot be cross-platform.

## Keep In Platform Code

These should remain platform-specific because Android, iOS, macOS, Windows, and Linux all expose them differently:

- Storage/photo/document permissions and permission rationale UI.
- Android `MANAGE_EXTERNAL_STORAGE`, SAF entry points, and iOS/macOS security-scoped access.
- Activity/ViewController/window lifecycle and native surface attachment.
- IME/text input plumbing when it depends on platform widgets.
- OS intent/deep-link/file-open entry points.
- Crash dump collection paths and OS log export hooks.
- Platform driver toggles that have no cross-platform equivalent.

Platform code should expose the smallest possible bridge to Flutter or C. It should not own launcher business logic.

## Move To Flutter

These are UI/application concerns and should leave `platforms/android/app/java/org/github/krkr2`:

- Launcher home/library UI currently in `LauncherActivity.kt`.
- Settings UI currently in `LauncherSettingsActivity.kt` and `RenderSettingsActivity.kt`.
- Diagnostics UI currently in `DiagnosticsActivity.kt`.
- Localized launcher strings currently in `LauncherStrings.kt`.
- Visual design tokens currently in `LauncherTheme.kt`.
- Game detail panes, root configuration sheets, scan depth controls, and game cards.

Flutter should call C ABI or minimal platform-host methods. Avoid reintroducing Android-only UI dependencies unless they are host-only permission affordances.

## Move To C ABI / Native Core

These should become stable exported C APIs because they are engine/runtime concepts and must be usable from Flutter on all platforms:

- Game discovery and validation currently represented by `GameScanner.kt` / `LauncherNativeScanner.cpp`.
- Launch path validation and game start requests.
- Recent games, per-game metadata, launch counters, play time, and preferred launch file.
- Runtime settings read/write when they map to engine `GlobalPreference.xml` or per-game preferences.
- Render/audio/input engine settings schemas currently represented by `KrkrPrefsSchema.kt`, `KrkrPrefsStore.kt`, and `KrkrPrefsCaptions.kt`.
- In-game menu tree and item activation. Initial APIs already exist in `FlutterGameMenuBridge.cpp`.
- Diagnostics snapshots that are platform-neutral: build ID, engine version, loaded renderer, memory stats, cache stats.

Prefer narrow functions returning UTF-8 JSON or explicit POD structs across FFI. Keep ownership rules documented for every pointer returned to Flutter.

## Current Android Legacy Buckets

| File | Current role | Migration target |
| --- | --- | --- |
| `LauncherActivity.kt` | Compose launcher UI and launch orchestration | Flutter UI + C launch API |
| `LauncherSettingsActivity.kt` | Compose launcher/settings UI | Flutter settings pages |
| `RenderSettingsActivity.kt` | Compose render prefs UI | Flutter settings + C prefs API |
| `DiagnosticsActivity.kt` | Compose diagnostics UI | Flutter diagnostics + small platform log export bridge |
| `GameScanner.kt` | Android-side game scanning | C ABI scanner, optionally using platform permission roots |
| `GamePrefsDb.kt` | Game stats and metadata DB | Cross-platform native/Flutter persistence decision needed |
| `LauncherPrefs.kt` | Launcher settings and game overrides | Flutter settings + C/native config where engine-owned |
| `LauncherSettingsDb.kt` | Android launcher key-value DB | Replace with Flutter persistence or C config API |
| `LauncherStrings.kt` | Android UI strings | Flutter localization |
| `LauncherTheme.kt` | Compose theme | Flutter theme |
| `KrkrPrefsSchema.kt` | Engine prefs schema | C/JSON schema generated for Flutter |
| `KrkrPrefsStore.kt` | Engine prefs XML bridge | C ABI prefs API |
| `AngleDriverController.kt` | Android driver settings | Platform host code |
| `ForceLandscapeHelper.kt` | Android orientation control | Platform host code |
| `MainActivity.kt` | Native game Activity lifecycle | Platform host/native surface lifecycle |
| `KR2Application.kt` | Android app bootstrap | Platform host bootstrap |

## Migration Order

1. **Stabilize Flutter APK build**: keep CI authoritative and avoid local-only assumptions.
2. **Solidify Android Flutter host**: commit generated Android shell or template it in-repo instead of patching it entirely in CI.
3. **Add C ABI scanner**: move game discovery out of `GameScanner.kt`, return JSON game entries to Flutter.
4. **Add C ABI settings schema**: expose render/engine prefs schema and values for Flutter settings screens.
5. **Move launcher UI**: reimplement library, settings, diagnostics in Flutter using the new APIs.
6. **Minimize Android host**: keep only permission requests, file picker/root grants, lifecycle, and native surface hosting.
7. **Replace in-game menu presentation**: implement platform event from native runtime to Flutter, then show menu UI in Flutter and activate items via C ABI.
8. **Delete obsolete Compose/Kotlin UI** once parity is reached and native code no longer references those classes.

## Guardrails

- Do not move permissions into C++ or Flutter-only abstractions that cannot represent platform requirements.
- Do not make Flutter call Kotlin for engine actions; use C ABI for engine actions.
- Do not delete old Android UI until the Flutter replacement can launch games and configure required settings.
- Keep fallback paths until CI and device smoke tests pass.
- Prefer additive C ABI functions with explicit versioning over changing existing signatures.
