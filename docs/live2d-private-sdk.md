# Live2D Private SDK Workflow

This project must not publish Live2D Cubism Core files in the public source
repository. The safe workflow is to keep the public repository limited to bridge
code and open/framework sources, while CI or local builds fetch proprietary Core
files from a private Git submodule.

## Repository Split

Public repository:

```text
cpp/plugins/live2d/krkrlive2d.cpp
cpp/plugins/live2d/krkrgles.cpp
cpp/plugins/live2d/cubism/Framework/
cpp/plugins/live2d/licenses/
```

Private submodule repository, suggested name `KiriKiri-Live2D-SDK`:

```text
Core/include/Live2DCubismCore.h
Core/lib/android/arm64-v8a/libLive2DCubismCore.a
Core/lib/android/x86/libLive2DCubismCore.a
Core/lib/android/x86_64/libLive2DCubismCore.a
README.md
LICENSES/
```

The private repository must stay private and should only be visible to people
or build machines that are allowed to use the Live2D SDK. Do not fork it into a
public namespace, attach it to public releases, or mirror it through public CI
artifacts.

## Add The Submodule

After creating the private repository, add it as an optional submodule:

```sh
git submodule add git@github.com:xiaocongyu66/KiriKiri-Live2D-SDK.git third_party/live2d-sdk
git commit -m "Add private Live2D SDK submodule"
```

This records only the submodule pointer in the public repository. The SDK files
remain in the private repository.

## Local Build

Initialize the private submodule on machines that have access:

```sh
git submodule update --init --depth 1 third_party/live2d-sdk
```

CMake will automatically use:

```text
third_party/live2d-sdk/Core
```

You can still override it explicitly with
`-DKRKR2_LIVE2D_CORE_DIR=/path/to/Core` when needed.

`cpp/plugins/CMakeLists.txt` automatically disables the Live2D plugin when the
Core header or ABI library is missing. Normal builds are not broken when the
private SDK is unavailable.

## CI Build

Use a read-only deploy key or token that can read the private SDK repository.
Store it as a CI secret, for example `LIVE2D_SDK_TOKEN`.

Example GitHub Actions checkout using a token:

```yaml
- uses: actions/checkout@v4
  with:
    submodules: false

- name: Initialize private Live2D SDK submodule
  env:
    LIVE2D_SDK_TOKEN: ${{ secrets.LIVE2D_SDK_TOKEN }}
  run: |
    git config --global url."https://x-access-token:${LIVE2D_SDK_TOKEN}@github.com/".insteadOf "git@github.com:"
    git submodule update --init --depth 1 third_party/live2d-sdk

- name: Configure
  run: cmake -S . -B build/android
```

Keep CI artifacts private if they contain the SDK files directly. If the output
is a public APK or plugin package, review the Live2D publication/license terms
before release.

## Alternative Path

Inochi2D is an open-source 2D puppet ecosystem:

```text
https://github.com/Inochi2D/inochi2d
```

It is not a drop-in Cubism Core replacement for existing `.moc3` commercial
games. Treat it as a future separate renderer/model format path, not as a
compatibility fix for Cubism-based titles.
