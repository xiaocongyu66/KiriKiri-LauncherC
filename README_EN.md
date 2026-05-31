# KiriKiri LauncherC

**Language**: [Chinese](README.md) | English

## Project Overview

KiriKiri LauncherC is a cross-platform compatibility launcher for KiriKiri / KAG games. It is not meant to be only a simple game entry point; the goal is to keep the KiriKiri2/TJS2 runtime, resource system, rendering, audio, video, plugin compatibility layer, and platform launcher organized as a maintainable C++ codebase so older games can continue to run on Android, Windows, Linux, and macOS.

This project is forked from [2468785842/krkr2](https://github.com/2468785842/krkr2) and continues development on top of its modular architecture. The runtime core lives under `cpp/core`, platform entry points live under `platforms`, plugin compatibility code lives under `cpp/plugins`, and dependency/cross-compilation setup is driven by CMake, vcpkg overlay ports/triplets, and the Android Gradle project. On Android, the project integrates SDL/Cocos2d-x platform code, Breakpad, Dobby hooks, and mobile resource path handling. Desktop entry points are kept for Windows, Linux, and macOS to help validate core compatibility behavior.

Current work focuses on behavior required by real games: XP3/ZIP/TAR resource loading, TJS2/KAG script execution, OpenGL/Cocos2d-x rendering bridges, FFmpeg audio/video, PSD/PSB/EMote/motionplayer, layerEx plugins, Kirikiroid compatibility patches, runtime TJS patches, mobile input, and storage path handling. Compatibility can still vary by script behavior, plugin set, archive format, and target platform. Reproducible logs and test cases are welcome.

## Architecture

- `cpp/core`: Engine compatibility core, including TJS2, storage/archive handling, environment layer, extension system, plugin bridge, audio, video, graphics, and utility modules.
- `cpp/plugins`: Built-in plugin compatibility pack, including common KiriKiri plugins, Kirikiroid compatibility behavior, runtime patches, PSB/PSD, motionplayer, layerEx, json, fstat, kagparserex, and related modules.
- `platforms`: Platform entry points and native projects. Android uses Gradle + CMake to build the shared library; desktop builds keep separate Windows, Linux, and macOS entry points.
- `vcpkg` and `cmake`: Overlay ports, triplets, Android toolchain integration, and dependency resolution shared by CI and local builds.
- `ui`, `doc`, `tools`, and `tests`: Launcher resources, documentation, helper tools, and non-Android test projects.

## Current Focus

- Improve Android startup, storage, input, rendering, and crash diagnostics.
- Fill in real-game plugin compatibility, especially motionplayer, PSB/PSD, layerEx, and Kirikiroid behavior.
- Keep static plugin registration, whole-archive/force-load linking, and runtime patches maintainable.
- Fix CI, vcpkg binary cache issues, NDK/CMake version drift, and cross-platform build differences.
- Preserve source, license, and compatibility notes when code or behavior is ported or rewritten from other projects.

## Table of Contents

- [Project Overview](#project-overview)
- [Architecture](#architecture)
- [Current Focus](#current-focus)
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

When code or behavior is ported or rewritten from another project, the relevant source files, commits, or documentation should preserve the required origin and license notes.

---

## License

MIT License. See [LICENSE](./LICENSE) for details.

---
