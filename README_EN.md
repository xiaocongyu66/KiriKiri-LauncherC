# KiriKiri LauncherC

**Language**: [Chinese](README.md) | English

## Project Overview

KiriKiri LauncherC is a cross-platform compatibility runtime and launcher for KiriKiri2 / KAG / TJS2 games. It is not meant to be only a game entry point; the goal is to keep the script runtime, resource system, image and video decoding, audio output, plugin compatibility layer, renderer backends, and platform launcher organized as a maintainable C++/CMake project so games that depend on the older KiriKiri ecosystem can continue to run on Android, Windows, Linux, and macOS.

This project is forked from [2468785842/krkr2](https://github.com/2468785842/krkr2) and continues engineering and compatibility work on top of its modular architecture. The runtime core lives under `cpp/core`, platform entry points live under `platforms`, built-in plugins and compatibility patches live under `cpp/plugins`, and dependency/cross-compilation setup is driven by CMake, vcpkg overlay ports/triplets, and the Android Gradle project. On Android, the project integrates Cocos2d-x/SDL platform code, Breakpad, Dobby hooks, oneTBB, mimalloc, mobile resource path handling, and per-game preferences. Desktop entry points are kept for Windows, Linux, and macOS to help validate core behavior and cross-platform builds.

Current work focuses on behavior required by real games rather than only minimal samples: XP3/ZIP/TAR/7z resource and archive loading, TJS2/KAG script execution, software rendering and OpenGL/GLES/ANGLE/ANGLE-VK paths, native Vulkan probing, FFmpeg audio/video, PSD/PSB/EMote/motionplayer, layerEx plugins, Kirikiroid compatibility patches, runtime TJS patches, mobile input, fonts, storage paths, and crash logs. Compatibility can still vary by script behavior, plugin set, archive format, renderer path, and target platform. Reproducible logs and test cases are welcome.

## Architecture

- `cpp/core`: Engine compatibility core, including TJS2, storage/archive handling, environment layer, extension system, plugin bridge, audio, video, graphics, and utility modules.
- `cpp/plugins`: Built-in plugin compatibility pack, including common KiriKiri plugins, Kirikiroid compatibility behavior, runtime patches, PSB/PSD, motionplayer, layerEx, json, fstat, kagparserex, and related modules.
- `platforms`: Platform entry points and native projects. Android uses Gradle + CMake to build the shared library; desktop builds keep separate Windows, Linux, and macOS entry points.
- `vcpkg` and `cmake`: Overlay ports, triplets, Android toolchain integration, and dependency resolution shared by CI and local builds.
- `ui`, `doc`, `tools`, and `tests`: Launcher resources, documentation, helper tools, and non-Android test projects.

## Current Focus

- Improve Android startup, storage, input, rendering, and crash diagnostics.
- Improve renderer selection and fallback across software rendering, OpenGL/GLES, ANGLE, and ANGLE-VK. Native Vulkan now has a dedicated backend that initializes a native Vulkan instance/device/graphics queue and multi-worker command pools; compositing currently reuses the software path while texture upload and blend pipelines are migrated.
- Fill in real-game plugin compatibility, especially motionplayer, PSB/PSD, layerEx, and Kirikiroid behavior.
- Keep the optional Live2D Cubism bridge buildable only when a local Cubism Core SDK is provided.
- Keep static plugin registration, whole-archive/force-load linking, and runtime patches maintainable.
- Fix CI, vcpkg binary cache issues, NDK/CMake version drift, and cross-platform build differences.
- Preserve source, license, and compatibility notes when code or behavior is ported or rewritten from other projects.

## Text Encoding and Character Assets

A compatibility issue caused by script text encoding misdetection has been fixed. Some games store `.ks`, `.tjs`, `.stand`, and similar scripts as BOM-less CP932/Shift_JIS or other legacy local encodings. When those scripts were misdetected as western single-byte text, character names, expression names, standing image file names, or model asset names could turn into mojibake. That could make all character models, some character models, standing images, or expression variants fail to load. Text stream reading now handles BOMs, UTF variants, ISO-2022 escape sequences, uchardet results, and ICU/uchardet-inspired CJK byte-structure checks; if the primary decode fails, oneTBB is used to validate multi-encoding fallbacks in parallel and then decode with the highest-priority working charset.

Covered languages and regions include Japanese, Simplified Chinese, Traditional Chinese, Korean, English, Western/Central/Northern European, Turkish, Baltic languages, Cyrillic languages such as Russian/Ukrainian/Bulgarian, Greek, Hebrew, Arabic, Thai, Vietnamese, Armenian, Georgian, Kazakh, Lao, and several DOS, Macintosh, and regional legacy code pages.

Supported encodings include:

- Unicode / base text: `ASCII`, `UTF-8`, `UTF-16`, `UTF-16LE`, `UTF-16BE`, `UTF-32`, `UTF-32LE`, `UTF-32BE`
- Japanese: `CP932`, `Shift_JIS`, `Windows-31J`, `EUC-JP`, `ISO-2022-JP`, `ISO-2022-JP-1`, `ISO-2022-JP-2`
- Chinese: `GB18030`, `GBK`, `CP936`, `GB2312`, `EUC-CN`, `HZ-GB-2312`, `Big5`, `CP950`, `Big5-HKSCS`, `EUC-TW`, `ISO-2022-CN`, `ISO-2022-CN-EXT`
- Korean: `EUC-KR`, `CP949`, `UHC`, `JOHAB`, `ISO-2022-KR`
- Western/Central European/Turkish/Baltic: `Windows-1250`, `Windows-1252`, `Windows-1254`, `Windows-1257`, `ISO-8859-1`, `ISO-8859-2`, `ISO-8859-3`, `ISO-8859-4`, `ISO-8859-9`, `ISO-8859-10`, `ISO-8859-13`, `ISO-8859-14`, `ISO-8859-15`, `ISO-8859-16`, `CP852`, `Mac-CentralEurope`
- Cyrillic/Greek/Hebrew/Arabic: `Windows-1251`, `Windows-1253`, `Windows-1255`, `Windows-1256`, `ISO-8859-5`, `ISO-8859-6`, `ISO-8859-7`, `ISO-8859-8`, `ISO-8859-8-I`, `KOI8-R`, `KOI8-U`, `KOI8-RU`, `CP866`, `Mac-Cyrillic`
- Thai/Vietnamese/other legacy encodings: `Windows-874`, `TIS-620`, `ISO-8859-11`, `Windows-1258`, `VISCII`, `TCVN`, `CP437`, `CP850`, `CP858`, `Macintosh`, `ARMSCII-8`, `PT154`, `RK1048`, `Georgian-Academy`, `Georgian-PS`, `CP855`, `CP857`, `CP860`, `CP861`, `CP862`, `CP863`, `CP864`, `CP865`, `CP869`, `CP1125`, `ISO-IR-111`, `HP-ROMAN8`, `CP1133`

## Table of Contents

- [Project Overview](#project-overview)
- [Architecture](#architecture)
- [Current Focus](#current-focus)
- [Text Encoding and Character Assets](#text-encoding-and-character-assets)
- [Supported Platforms](#supported-platforms)
- [Build Tools](#build-tools)
- [Build Environment Setup](#build-environment-setup)
  - [Environment Variables](#environment-variables)
  - [Build Steps](#build-steps)
- [Executable Location](#executable-location)
- [Code Formatting](#code-formatting)
- [Supported Games](#supported-games)
- [Plugin Resources](#plugin-resources)
- [Acknowledgements](#acknowledgements)
- [Contact](#contact)
- [License](#license)

## Supported Platforms

- **Android**:
  - `arm64-v8a`
  - `x86_64`
- **Windows**:
  - x86_64
- **Linux**:
  - x86_64
- **macOS**:
  - arm64

---

## Build Tools

- **Android**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - [vcpkg@latest](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
  - [Android SDK@33](https://developer.android.com)
  - [Android NDK@28.0.13004108](https://developer.android.com/ndk/downloads)
  - [JDK@17](https://jdk.java.net/archive/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`
- **Windows**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - `Visual Studio 2022`
  - `vcpkg@latest`
  - [cmake@3.31.1+](https://cmake.org/download/)
  - [winflexbison@2.5.25](https://github.com/lexxmark/winflexbison)
  - `python3`
  - `NASM@latest`
- **Linux**:
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - `GCC`
  - `vcpkg@latest`
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`
  - `YASM`
- **macOS**:
  - Xcode
  - `vcpkg@latest`
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`

---

## Build Environment Setup

### Environment Variables

- **Android**:
  - `VCPKG_ROOT=/path/to/vcpkg`
  - `ANDROID_SDK=/path/to/androidsdk`
  - `ANDROID_NDK=/path/to/androidndk`
- **Windows**:
  - `VCPKG_ROOT=D:/vcpkg`
  - Add the `winflexbison` directory to `PATH`.
- **Linux / macOS**:
  - `VCPKG_ROOT=/path/to/vcpkg`

> **Note**: On Windows, use `/` or `\\` in paths instead of a single `\`.

---

### Build Steps

- **Android**:
  ```bash
  ./platforms/android/gradlew -p ./platforms/android assembleDebug
  ```

  > If `glib` installation fails, see [FAQ](./doc/FAQ.md).

* **Windows**:

  ```powershell
  ./scripts/build-windows.bat
  ```

* **Linux**:

  ```bash
  ./scripts/build-linux.sh
  ```

* **macOS**:

  ```bash
  cmake --preset="MacOS Debug Config"
  cmake --build --preset="MacOS Debug Build"
  ```

* **Using Docker**:
* Build Linux: `docker build -f dockers/linux.Dockerfile -t linux-builder .`
* Build Android: `docker build -f dockers/android.Dockerfile -t android-builder .`

---

## Executable Location

* **Android**:
  * Debug: `platforms/android/out/android/app/outputs/apk/debug/*.apk`
  * Release: `platforms/android/out/android/app/outputs/apk/release/*.apk`
* **Windows**: `out/windows/debug/bin/krkr2/krkr2.exe`
* **Linux**: `out/linux/debug/bin/krkr2/krkr2`
* **macOS**: `out/macos/debug/bin/krkr2/krkr2.app`

---

## Code Formatting

- **clang-format@20**
- **Linux**:
    ```bash
    clang-format -i --verbose $(find ./cpp ./platforms ./tests ./tools -regex ".+\.\(cpp\|cc\|h\|hpp\|inc\)")
    ```

- **macOS**:
    ```bash
    clang-format -i --verbose $(find ./cpp ./platforms ./tests ./tools -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" -o -name "*.inc")
    ```

- **Windows**:
    ```powershell
    Get-ChildItem -Path ./cpp, ./platforms, ./tests, ./tools -Recurse -File |
    Where-Object { $_.Name -match '\.(cpp|cc|h|hpp|inc)$' } |
    ForEach-Object { clang-format -i --verbose $_.FullName }
    ```

---

## Supported Games

* See [games list](./doc/support_games.txt)

---

## Plugin Resources

* Related plugins and utility libraries are available from [wamsoft GitHub repositories](https://github.com/orgs/wamsoft/repositories?type=all).

---

## Acknowledgements

KiriKiri LauncherC is informed by and benefits from several open-source projects in the KiriKiri / KrKr ecosystem. Thanks to these projects for their work on engine compatibility, platform ports, plugin behavior, and mobile runtime support:

* [2468785842/krkr2](https://github.com/2468785842/krkr2)
* [reAAAq/KrKr2-Next](https://github.com/reAAAq/KrKr2-Next)
* [krkrsdl3/krkrsdl3](https://github.com/krkrsdl3/krkrsdl3)
* [fenghengzhi/kirikiroid2-web](https://github.com/fenghengzhi/kirikiroid2-web)
* [AetherKiri/AetherKiri](https://github.com/AetherKiri/AetherKiri)

When code or behavior is ported or rewritten from another project, the relevant source files, commits, or documentation should preserve the required origin and license notes.

---

## Contact

For compatibility reports, reproducible logs, or project maintenance discussions, email: `xiaocongyu1@qq.com`

---

## License

MIT License. See [LICENSE](./LICENSE) for details.

---
