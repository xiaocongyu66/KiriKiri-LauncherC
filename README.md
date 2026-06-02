# KiriKiri LauncherC

**语言 / Language**: 中文 | [English](README_EN.md)

## 项目介绍

KiriKiri LauncherC 是面向 KiriKiri2 / KAG / TJS2 游戏的跨平台兼容运行环境和启动器。项目目标不是只提供一个游戏入口，而是把脚本运行时、资源系统、图像与视频解码、音频输出、插件兼容层、渲染后端和平台启动器整理成可维护的 C++/CMake 工程，让更多依赖旧版吉里吉里生态的游戏能在 Android、Windows、Linux 和 macOS 上继续运行。

本项目 fork 自 [2468785842/krkr2](https://github.com/2468785842/krkr2)，并在其模块化架构基础上继续工程化和兼容性维护：核心运行时位于 `cpp/core`，平台入口集中在 `platforms`，内置插件和兼容补丁集中在 `cpp/plugins`，依赖和交叉编译由 CMake、vcpkg overlay ports/triplets 以及 Android Gradle 工程统一驱动。Android 端接入 Cocos2d-x/SDL 平台层、Breakpad、Dobby hook、oneTBB、mimalloc、移动端资源路径和 per-game 偏好配置；桌面端保留 Windows、Linux、macOS 入口，方便验证核心行为和跨平台构建。

项目当前重点是补齐真实游戏会依赖的行为，而不是只跑通最小示例：XP3/ZIP/TAR/7z 等资源与归档读取、TJS2/KAG 脚本执行、软件渲染与 OpenGL/GLES/ANGLE/ANGLE-VK 路径、Native Vulkan 探测、FFmpeg 音视频、PSD/PSB/EMote/motionplayer、layerEx 系列插件、Kirikiroid 兼容补丁、运行时 TJS patch、移动端输入、字体、存储路径和崩溃日志。不同游戏的兼容性仍会受到脚本写法、插件组合、封包格式、渲染路径和平台差异影响，欢迎用可复现日志和测试游戏反馈问题。

## 架构概览

- `cpp/core`: 引擎兼容核心，包含 TJS2、基础存储与归档、环境层、扩展系统、插件桥、音频、视频、图像和工具模块。
- `cpp/plugins`: 内置插件兼容包，覆盖常见 KiriKiri 插件、Kirikiroid 兼容行为、runtime patches、PSB/PSD、motionplayer、layerEx、json、fstat、kagparserex 等模块。
- `platforms`: 平台入口和原生工程。Android 使用 Gradle + CMake 构建共享库，桌面端分别维护 Windows、Linux、macOS 启动入口。
- `vcpkg` 与 `cmake`: 维护 overlay ports、triplets、Android toolchain 接入和各平台依赖解析，尽量让 CI 与本地构建走同一套配置。
- `ui`、`doc`、`tools`、`tests`: 分别放置启动器资源、说明文档、辅助工具和非 Android 测试工程。

## 当前开发重点

- 提升 Android 端启动、存储、输入、渲染和崩溃诊断的稳定性。
- 完善软件渲染、OpenGL/GLES、ANGLE、ANGLE-VK 的选择与回退；Native Vulkan 目前已有探测和选项，完整 Vulkan RenderManager 仍在后续实现范围内。
- 补全真实游戏常用插件，特别是 motionplayer、PSB/PSD、layerEx 和 Kirikiroid 兼容行为。
- 可选接入 Live2D Cubism：插件桥接源码已入库，但需要通过本机 SDK 路径提供 Cubism Core 头文件和对应 ABI 静态库后才会参与 Android 构建。
- 保持插件静态注册、whole-archive/force-load 链接策略和运行时 patch 可维护。
- 修复 CI、vcpkg 二进制缓存、NDK/CMake 版本和跨平台构建差异。
- 在移植或改写其他项目实现时保留来源、许可证和必要的兼容说明。

## 文本编码与角色资源修复

已修复一类由脚本文本编码误判引起的兼容问题：部分游戏的 `.ks`、`.tjs`、`.stand` 等脚本使用无 BOM 的 CP932/Shift_JIS 或其他旧式本地编码。如果这些脚本被误判为西文单字节编码，脚本中的角色名、差分名、立绘文件名或模型资源名会被读成乱码，进而导致部分游戏的全部角色模型、部分角色模型、立绘或表情差分无法显示。当前文本流读取会先处理 BOM、UTF、ISO-2022 escape 和 uchardet 结果，再参考 ICU/uchardet 的多字节编码规则做 CJK 结构识别；主解码失败时会使用 oneTBB 并发验证多编码 fallback，并按优先级选择可读取的编码。

当前覆盖的语言和区域包括：日文、简体中文、繁体中文、韩文、英文、西欧/中欧/北欧/土耳其/波罗的海语言、俄语/乌克兰语/保加利亚语等西里尔文字、希腊语、希伯来语、阿拉伯语、泰语、越南语、亚美尼亚语、格鲁吉亚语、哈萨克语、老挝语，以及部分 DOS、Macintosh 和区域性旧代码页。

支持的编码包括：

- Unicode / 基础文本：`ASCII`, `UTF-8`, `UTF-16`, `UTF-16LE`, `UTF-16BE`, `UTF-32`, `UTF-32LE`, `UTF-32BE`
- 日文：`CP932`, `Shift_JIS`, `Windows-31J`, `EUC-JP`, `ISO-2022-JP`, `ISO-2022-JP-1`, `ISO-2022-JP-2`
- 中文：`GB18030`, `GBK`, `CP936`, `GB2312`, `EUC-CN`, `HZ-GB-2312`, `Big5`, `CP950`, `Big5-HKSCS`, `EUC-TW`, `ISO-2022-CN`, `ISO-2022-CN-EXT`
- 韩文：`EUC-KR`, `CP949`, `UHC`, `JOHAB`, `ISO-2022-KR`
- 西欧/中欧/土耳其/波罗的海：`Windows-1250`, `Windows-1252`, `Windows-1254`, `Windows-1257`, `ISO-8859-1`, `ISO-8859-2`, `ISO-8859-3`, `ISO-8859-4`, `ISO-8859-9`, `ISO-8859-10`, `ISO-8859-13`, `ISO-8859-14`, `ISO-8859-15`, `ISO-8859-16`, `CP852`, `Mac-CentralEurope`
- 西里尔/希腊/希伯来/阿拉伯：`Windows-1251`, `Windows-1253`, `Windows-1255`, `Windows-1256`, `ISO-8859-5`, `ISO-8859-6`, `ISO-8859-7`, `ISO-8859-8`, `ISO-8859-8-I`, `KOI8-R`, `KOI8-U`, `KOI8-RU`, `CP866`, `Mac-Cyrillic`
- 泰语/越南语/其他旧编码：`Windows-874`, `TIS-620`, `ISO-8859-11`, `Windows-1258`, `VISCII`, `TCVN`, `CP437`, `CP850`, `CP858`, `Macintosh`, `ARMSCII-8`, `PT154`, `RK1048`, `Georgian-Academy`, `Georgian-PS`, `CP855`, `CP857`, `CP860`, `CP861`, `CP862`, `CP863`, `CP864`, `CP865`, `CP869`, `CP1125`, `ISO-IR-111`, `HP-ROMAN8`, `CP1133`

## 目录

- [项目介绍](#项目介绍)
- [架构概览](#架构概览)
- [当前开发重点](#当前开发重点)
- [文本编码与角色资源修复](#文本编码与角色资源修复)
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
- [联系我们](#联系我们)
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

项目会按兼容需求引入第三方组件。KiriKiri-LauncherC 自有新增和修改代码以仓库根目录的 [LICENSE](./LICENSE) 中 MIT modifications 条款为准；第三方模块、移植代码和专有 SDK 组件遵循各自许可证，相关文本会保留在源码目录、`licenses` 目录或 overlay port 中。

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

## 联系我们

如需反馈兼容性问题、提交可复现日志或讨论项目维护事项，可以发送邮件到：`xiaocongyu1@qq.com`

---

## 许可证

KiriKiri-LauncherC 自有新增和修改代码按 MIT 许可证发布。详细信息请参阅 [LICENSE](./LICENSE) 文件头部的 MIT modifications 条款。仓库中引入的第三方组件、上游代码、移植代码和可选 SDK 组件遵循各自许可证，请同时查看对应目录内的许可证与 notice 文件。

---
