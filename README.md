# KiriKiri LauncherC

**语言 / Language**: 中文 | [English](README_EN.md)

## 项目介绍

KiriKiri LauncherC 是面向吉里吉里 / KAG 游戏的跨平台兼容启动器。项目目标不是只做一个简单的游戏入口，而是把 KiriKiri2/TJS2 运行时、资源系统、渲染、音频、视频、插件兼容层和平台启动器整理成可维护的 C++ 架构，让更多旧游戏能在 Android、Windows、Linux 和 macOS 上继续运行。

本项目 fork 自 [2468785842/krkr2](https://github.com/2468785842/krkr2)，并在其模块化架构基础上继续维护和适配：核心运行时被拆分到 `cpp/core`，平台入口集中在 `platforms`，插件兼容代码集中在 `cpp/plugins`，依赖和交叉编译由 CMake、vcpkg overlay ports/triplets 以及 Android Gradle 工程统一驱动。Android 端同时接入 SDL/Cocos2d-x 平台层、Breakpad、Dobby hook 和移动端资源路径适配；桌面端保留 Windows、Linux、macOS 入口，方便验证核心兼容行为。

项目目前重点补齐真实游戏会依赖的行为，包括 XP3/ZIP/TAR 等资源读取、TJS2/KAG 脚本执行、OpenGL/Cocos2d-x 渲染桥接、FFmpeg 音视频、PSD/PSB/EMote/motionplayer、layerEx 系列插件、Kirikiroid 兼容补丁、运行时 TJS patch、移动端输入与存储路径等。不同游戏的兼容性仍会受到脚本写法、插件组合、封包格式和平台差异影响，欢迎用可复现日志和测试游戏反馈问题。

## 架构概览

- `cpp/core`: 引擎兼容核心，包含 TJS2、基础存储与归档、环境层、扩展系统、插件桥、音频、视频、图像和工具模块。
- `cpp/plugins`: 内置插件兼容包，覆盖常见 KiriKiri 插件、Kirikiroid 兼容行为、runtime patches、PSB/PSD、motionplayer、layerEx、json、fstat、kagparserex 等模块。
- `platforms`: 平台入口和原生工程。Android 使用 Gradle + CMake 构建共享库，桌面端分别维护 Windows、Linux、macOS 启动入口。
- `vcpkg` 与 `cmake`: 维护 overlay ports、triplets、Android toolchain 接入和各平台依赖解析，尽量让 CI 与本地构建走同一套配置。
- `ui`、`doc`、`tools`、`tests`: 分别放置启动器资源、说明文档、辅助工具和非 Android 测试工程。

## 当前开发重点

- 提升 Android 端启动、存储、输入、渲染和崩溃诊断的稳定性。
- 补全真实游戏常用插件，特别是 motionplayer、PSB/PSD、layerEx 和 Kirikiroid 兼容行为。
- 可选接入 Live2D Cubism：插件桥接源码已入库，但需要通过本机 SDK 路径提供 Cubism Core 头文件和对应 ABI 静态库后才会参与 Android 构建。
- 保持插件静态注册、whole-archive/force-load 链接策略和运行时 patch 可维护。
- 修复 CI、vcpkg 二进制缓存、NDK/CMake 版本和跨平台构建差异。
- 在移植或改写其他项目实现时保留来源、许可证和必要的兼容说明。

## 目录

- [项目介绍](#项目介绍)
- [架构概览](#架构概览)
- [当前开发重点](#当前开发重点)
- [支持平台](#支持平台)
- [依赖构建工具](#依赖构建工具)
- [编译环境配置](#编译环境配置)
  - [环境变量](#环境变量)
  - [编译步骤](#编译步骤)
- [可执行文件位置](#可执行文件位置)
- [代码格式化](#代码格式化)
- [支持的游戏列表](#支持的游戏列表)
- [插件资源](#插件资源)
- [可选组件与第三方许可](#可选组件与第三方许可)
- [致谢](#致谢)
- [许可证](#许可证)

## 支持平台

- **Android**:
  - `arm64-v8a`
  - `x86_64`
- **Windows**:
  - x86_64
- **Linux**:
  - x86_64
- **MacOS**:
  - arm64

---

## 依赖构建工具

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
- **MacOS**:
  - Xcode
  - `vcpkg@latest`
  - [ninja@latest](https://github.com/ninja-build/ninja/releases)
  - [cmake@3.31.1+](https://cmake.org/download/)
  - `bison@3.8.2+`
  - `python3`
  - `NASM@latest`

---

## 编译环境配置

### 环境变量

- **Android**:
  - `VCPKG_ROOT=/path/to/vcpkg`
  - `ANDROID_SDK=/path/to/androidsdk`
  - `ANDROID_NDK=/path/to/androidndk`
- **Windows**:
  - `VCPKG_ROOT=D:/vcpkg`
  - 将 `winflexbison` 的路径添加到 `PATH` 环境变量中。
- **Linux / macOS**:
  - `VCPKG_ROOT=/path/to/vcpkg`

> **注意**: Windows 路径建议使用 `/` 或 `\\`，避免使用单个 `\`。

---

### 编译步骤

- **Android**:
  ```bash
  ./platforms/android/gradlew -p ./platforms/android assembleDebug
  ```

  > 如果遇到 `glib` 安装问题，请查看 [FAQ#安装glib失败](./doc/FAQ.md#安装glib失败)

* **Windows**:

  ```powershell
  ./scripts/build-windows.bat
  ```

* **Linux**:

  ```bash
  ./scripts/build-linux.sh
  ```

* **MacOS**:

  ```bash
  cmake --preset="MacOS Debug Config"
  cmake --build --preset="MacOS Debug Build"
  ```

* **使用 Docker**:
* Build Linux: `docker build -f dockers/linux.Dockerfile -t linux-builder .`
* Build Android: `docker build -f dockers/android.Dockerfile -t android-builder .`

---

## 可执行文件位置

* **Android**:
  * Debug 版本: `platforms/android/out/android/app/outputs/apk/debug/*.apk`
  * Release 版本: `platforms/android/out/android/app/outputs/apk/release/*.apk`
* **Windows**: `out/windows/debug/bin/krkr2/krkr2.exe`
* **Linux**: `out/linux/debug/bin/krkr2/krkr2`
* **MacOS**: `out/macos/debug/bin/krkr2/krkr2.app`

---

## 代码格式化
- **clang-format@20**
- **Linux**:
    ```bash
    clang-format -i --verbose $(find ./cpp ./platforms ./tests ./tools -regex ".+\.\(cpp\|cc\|h\|hpp\|inc\)")
    ```

- **MacOS**:
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

## 支持的游戏列表

* 查看 [games list](./doc/support_games.txt)

---

## 插件资源

* 可在 [wamsoft GitHub repositories](https://github.com/orgs/wamsoft/repositories?type=all) 查找相关插件和工具库。

---

## 可选组件与第三方许可

项目会按兼容需求引入第三方组件。主项目代码仍以仓库根目录的 [LICENSE](./LICENSE) 为准；第三方模块、移植代码和专有 SDK 组件遵循各自许可证，相关文本会保留在源码目录、`licenses` 目录或 overlay port 中。

* **Live2D Cubism**: `cpp/plugins/live2d` 已导入 KrKr2-Next 的 `krkrlive2d.cpp` / `krkrgles.cpp` 桥接代码，并放入官方 Cubism Native Framework 5 r.5 的 Framework 源码。Cubism Core 头文件和静态库不提交到仓库；需要构建时通过私有 submodule `third_party/live2d-sdk/Core` 提供，或通过 `KRKR2_LIVE2D_CORE_DIR` 显式指向本机 SDK 的 `Core` 目录。CMake 开关为 `KRKR2_ENABLE_LIVE2D`，默认开启检查；Android 构建只有在存在 `include/Live2DCubismCore.h` 与 `lib/android/<ABI>/libLive2DCubismCore.a` 时才会真正编译插件，否则会自动跳过，不影响普通构建。KrKr2-Next 桥接代码遵循 GPL-3.0-or-later，许可证副本保存在 `cpp/plugins/live2d/licenses/KrKr2-Next-GPL-3.0-or-later.LICENSE`；Cubism Framework 相关许可证和 notice 也保存在 `cpp/plugins/live2d/licenses`。私有 SDK submodule 流程见 [docs/live2d-private-sdk.md](docs/live2d-private-sdk.md)。
* **oneTBB**: 用于原生层文件扫描和短任务并发调度，尽量把 I/O 密集任务移出主线程，同时保留跨平台构建边界。
* **mimalloc**: 用于原生内存分配，降低多线程场景下频繁分配释放带来的锁竞争。
* **libarchive / zlib-ng / minizip**: 用于归档读取、压缩格式兼容和 ZIP 相关处理；overlay port 中保留对应版本和许可证信息。

---

## 致谢

KiriKiri LauncherC 的开发参考并受益于多个 KiriKiri / KrKr 相关开源项目。感谢这些项目在引擎兼容、平台移植、插件实现和移动端运行环境上的探索：

* [2468785842/krkr2](https://github.com/2468785842/krkr2)
* [reAAAq/KrKr2-Next](https://github.com/reAAAq/KrKr2-Next)
* [krkrsdl3/krkrsdl3](https://github.com/krkrsdl3/krkrsdl3)
* [fenghengzhi/kirikiroid2-web](https://github.com/fenghengzhi/kirikiroid2-web)
* [AetherKiri/AetherKiri](https://github.com/AetherKiri/AetherKiri)

如果后续从其他项目移植或改写具体模块，会在对应源码、提交记录或文档中保留必要的来源与许可证说明。

---

## 许可证

此项目原始代码遵循 MIT 许可证。详细信息请参阅 [LICENSE](./LICENSE) 文件。仓库中引入的第三方组件、移植代码和可选 SDK 组件遵循各自许可证，请同时查看对应目录内的许可证与 notice 文件。

---
