# 2026-07-23 Full Cocos Cut

## Removed
- `cpp/core/environ/cocos2d/` (AppDelegate, MainScene, YUVSprite, …)
- `vcpkg/ports/cocos2dx` + `vcpkg.json` cocos2dx dependency
- `cmake/CocosBuildHelpers.cmake`, `FixCocos2dxImportedTargets.cmake`
- Cocos UI forms under `cpp/core/environ/ui/*` (kept only FlutterGameMenuBridge)
- Desktop Cocos entry (`platforms/{linux,windows,apple}/main` now stubs)
- Gradle `-DKRKR2_ENABLE_COCOS_HOST=OFF` (CMake forces OFF globally)

## Renamed
- `ui/cocos-studio` → `ui/runtime-ui`
- `flutter_launcher/assets/cocos-studio` → `flutter_launcher/assets/runtime-ui`

## Build
- `KRKR2_ENABLE_COCOS_HOST` forced OFF in root + core modules; always `=0`
- No `find_package(cocos2dx)` / no `cocos2dx::cocos2d` link

## Runtime
- Android production path remains Flutter + SDL3 only
- Desktop native binary exits with guidance to Flutter Android runtime
