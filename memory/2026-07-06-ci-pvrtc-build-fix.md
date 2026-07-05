# 2026-07-06 CI PVRTC build fix

## Trigger

User pointed to GitHub Actions job:

`https://github.com/xiaocongyu66/KiriKiri-LauncherC/actions/runs/28748544289/job/85243548365`

The Android build failed while compiling `cpp/core/visual/LoadPVRv3.cpp`.

## Failure

CI error:

```text
LoadPVRv3.cpp:235:13: error: use of undeclared identifier 'PVRTDecompressPVRTC'
LoadPVRv3.cpp:239:13: error: use of undeclared identifier 'PVRTDecompressPVRTC'
```

Cause:

- The previous Cocos extraction removed the old hard include
  `<cocos/base/pvr.h>`.
- That header had supplied `PVRTDecompressPVRTC`.
- The local project already had PVRTC encoder code, but did not have the PVRTC
  decompressor declaration/source wired into `core_visual_module`.

## Fix

Added the reference PVRTC decompressor used by AetherKiri:

- `cpp/core/visual/ogl/PVRTDecompress.h`
- `cpp/core/visual/ogl/PVRTDecompress.cpp`

Then wired it into:

- `cpp/core/visual/LoadPVRv3.cpp`
  - includes `ogl/PVRTDecompress.h`
- `cpp/core/visual/CMakeLists.txt`
  - adds `ogl/PVRTDecompress.cpp` to `VISUAL_SOURCE_FILES`

## Why this path

This keeps `LoadPVRv3.cpp` independent from Cocos while preserving software
PVRTC decode support for non-native/direct image loading paths.

This is aligned with the hard migration goal:

- continue removing Cocos dependencies;
- use reference-project implementation where stable;
- avoid adding validation or runtime checks in hot render paths.

## Commits

- `0186114 Restore PVRTC decompressor for PVR loading`

## CI status after push

After pushing `0186114`:

- `format-check` run `28749378354` completed successfully.
- `build-android` run `28749395537` started and was still in progress when this
  note was written.
