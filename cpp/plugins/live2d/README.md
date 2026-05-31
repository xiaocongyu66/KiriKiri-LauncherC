# Live2D Cubism plugin import

This directory contains an optional Live2D Cubism plugin import.

Imported sources:

- `krkrlive2d.cpp` and `krkrgles.cpp` come from KrKr2-Next.
- `cubism/Framework` comes from the official Cubism Native Framework 5 r.5 package.

Licenses and notices are preserved under `licenses/`. The KrKr2-Next bridge
source is GPL-3.0-or-later; see
`licenses/KrKr2-Next-GPL-3.0-or-later.LICENSE`.

Live2D Cubism Core files are not committed here. To enable the plugin on
Android, point CMake at a local SDK Core directory:

```text
-DKRKR2_LIVE2D_CORE_DIR=/path/to/CubismSdkForNative/Core
```

or set the same value in the `KRKR2_LIVE2D_CORE_DIR` environment variable.
That directory must provide the Core public header and the ABI static library:

```text
include/Live2DCubismCore.h
lib/android/<ABI>/libLive2DCubismCore.a
```

`cpp/plugins/CMakeLists.txt` keeps the plugin disabled until the required Core
header and library are present for the active ABI. This lets the source import
stay in-tree without committing proprietary Core files or breaking existing
builds.

For the private SDK submodule workflow used by CI or release builds, see
`../../../docs/live2d-private-sdk.md`.
