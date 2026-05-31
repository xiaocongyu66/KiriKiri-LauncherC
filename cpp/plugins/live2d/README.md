# Live2D Cubism plugin import

This directory contains an optional Live2D Cubism plugin import.

Imported sources:

- `krkrlive2d.cpp` and `krkrgles.cpp` come from KrKr2-Next.
- `cubism/Framework` comes from the official Cubism Native Framework 5 r.5 package.
- `cubism/Core/include/Live2DCubismCore.h` is the Cubism Core public header required by the framework.

Licenses and notices are preserved under `licenses/`. The KrKr2-Next bridge
source is GPL-3.0-or-later; see
`licenses/KrKr2-Next-GPL-3.0-or-later.LICENSE`.

The Live2D Cubism Core binary is not committed here. To enable the plugin on
Android, place the Cubism Core static libraries at:

```text
cubism/Core/lib/android/arm64-v8a/libLive2DCubismCore.a
cubism/Core/lib/android/armeabi-v7a/libLive2DCubismCore.a
cubism/Core/lib/android/x86/libLive2DCubismCore.a
cubism/Core/lib/android/x86_64/libLive2DCubismCore.a
```

`cpp/plugins/CMakeLists.txt` keeps the plugin disabled until the required Core
library is present for the active ABI. This lets the source import stay in-tree
without breaking existing builds.
