// SPDX-License-Identifier: MIT
//
// Runtime compatibility patch loader for Android krkr2.
//
// At every `TVPExecuteStartupScript` we:
//   1. Optionally inject the engine-embedded default TJS patch
//      (RUNTIME_PATCH_BUILTIN_DEFAULT_TJS, generated from
//       cpp/plugins/runtime_patches/builtin_default.tjs).
//   2. Walk `<gamedir>/compat/*.tjs` in lexical order and run each file as
//      an additional TJS script.
//
// Both stages are entirely opt-in:
//   * If a sentinel file `<gamedir>/compat/no_default` exists, stage 1 is
//     skipped — useful when a game already ships the real plugin and the
//     fallback would only confuse it.
//   * Files matching `*.disabled.tjs` in stage 2 are skipped, giving users
//     a way to keep an unused override around without enabling it.
//
// Errors thrown by any patch are logged via spdlog/warn and swallowed — a
// broken compat script must NEVER stop the engine from booting.
//
// Why TJS scripts and not native NCB? Game scripts often reach for
// game-specific symbols (e.g. `kag.voiceEffectPlugin`) that are easier to
// stub at the script level than to fake from C++. NCB is reserved for
// real plugin reimplementations (see wfBasicEffectCompat.cpp etc.).
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <spdlog/spdlog.h>

#include "tjsCommHead.h"
#include "ScriptMgnIntf.h"
#include "StorageImpl.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"  // TVPNativeProjectDir
#include "FilePathUtil.h" // ExtractFileDir

#include "runtime_patch_strings.h"

namespace {

// 安全に TJS スクリプトを実行する。失敗してもログのみで例外は外に伝えない。
void RunPatchScript(const ttstr &name, const ttstr &source) {
    try {
        TVPExecuteScript(source);
        spdlog::info("[krkr] runtime patch '{}' applied",
                     name.AsStdString());
    } catch(const TJS::eTJSScriptError &e) {
        ttstr msg = e.GetMessage();
        const tjs_char *block = e.GetBlockName();
        spdlog::warn("[krkr] runtime patch '{}' script error: {} (block={}, line={})",
                     name.AsStdString(), msg.AsStdString(),
                     block ? ttstr(block).AsStdString() : std::string("(null)"),
                     static_cast<long long>(e.GetSourceLine()));
    } catch(const TJS::eTJS &e) {
        spdlog::warn("[krkr] runtime patch '{}' tjs error: {}",
                     name.AsStdString(),
                     ttstr(e.GetMessage()).AsStdString());
    } catch(const std::exception &e) {
        spdlog::warn("[krkr] runtime patch '{}' failed: {}",
                     name.AsStdString(), e.what());
    } catch(...) {
        spdlog::warn("[krkr] runtime patch '{}' failed: unknown error",
                     name.AsStdString());
    }
}

bool FileExists(const std::string &path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::string JoinDir(const std::string &dir, const std::string &name) {
    if(dir.empty())
        return name;
    if(dir.back() == '/' || dir.back() == '\\')
        return dir + name;
    return dir + "/" + name;
}

// 指定ディレクトリから *.tjs を集めて、.disabled.tjs を除外し、
// 文字列順 (systemd の *.d/ 互換) でソートして返す。
std::vector<std::string> CollectGamePatchFiles(const std::string &compatDir) {
    std::vector<std::string> files;
    TVPListDir(compatDir, [&](const std::string &filename, int mask) {
        if(!(mask & S_IFREG))
            return;
        const auto ext_pos = filename.rfind('.');
        if(ext_pos == std::string::npos)
            return;
        const std::string ext = filename.substr(ext_pos);
        if(ext != ".tjs")
            return;
        // *.disabled.tjs を除外
        const std::string disabled_suffix = ".disabled.tjs";
        if(filename.size() >= disabled_suffix.size() &&
           filename.compare(filename.size() - disabled_suffix.size(),
                            disabled_suffix.size(), disabled_suffix) == 0) {
            return;
        }
        files.emplace_back(filename);
    });
    std::sort(files.begin(), files.end());
    std::vector<std::string> abs;
    abs.reserve(files.size());
    for(const auto &name : files)
        abs.emplace_back(JoinDir(compatDir, name));
    return abs;
}

ttstr LoadFileAsTtstr(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if(!ifs)
        throw std::runtime_error("failed to open: " + path);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    // ttstr は UTF-8 narrow string を受け付けるコンストラクタを持つ
    return ttstr(oss.str().c_str());
}

} // namespace

// 公開エントリ。`TVPExecuteStartupScript` が patch.tjs 実行直前に呼ぶ。
void TVPRunRuntimeCompatibilityPatches() {
    // TVPNativeProjectDir はゲーム XP3 ファイルへの絶対パス
    // (Application.cpp で StartApplication(path) として渡される)。
    // ディレクトリ部分を取り出してから compat/ サブフォルダを組み立てる。
    std::string projectDir =
        ExtractFileDir(TVPNativeProjectDir.AsStdString());
    if(projectDir.empty()) {
        spdlog::warn("[krkr] TVPNativeProjectDir is empty, "
                     "skip runtime compat patches");
        return;
    }
    if(projectDir.back() != '/' && projectDir.back() != '\\')
        projectDir.push_back('/');

    const std::string compatDir = projectDir + "compat";
    const std::string noDefaultSentinel = compatDir + "/no_default";

    // ステージ 1: 内蔵デフォルトパッチ
    if(FileExists(noDefaultSentinel)) {
        spdlog::info("[krkr] runtime patch builtin_default skipped: "
                     "found sentinel {}",
                     noDefaultSentinel);
    } else {
        RunPatchScript(TJS_W("<builtin>/builtin_default.tjs"),
                       ttstr(RUNTIME_PATCH_BUILTIN_DEFAULT_TJS));
    }

    // ステージ 2: ユーザー上書き (`<gamedir>/compat/*.tjs`)
    if(!FileExists(compatDir)) {
        spdlog::debug("[krkr] no per-game compat dir at {}", compatDir);
        return;
    }

    std::vector<std::string> files;
    try {
        files = CollectGamePatchFiles(compatDir);
    } catch(const std::exception &e) {
        spdlog::warn("[krkr] failed to enumerate {}: {}", compatDir, e.what());
        return;
    }

    for(const auto &absPath : files) {
        ttstr label;
        label += TJS_W("compat/");
        const std::string relname = absPath.size() > compatDir.size() + 1
            ? absPath.substr(compatDir.size() + 1)
            : absPath;
        label += ttstr(relname.c_str());
        try {
            ttstr body = LoadFileAsTtstr(absPath);
            RunPatchScript(label, body);
        } catch(const std::exception &e) {
            spdlog::warn("[krkr] failed to read {}: {}", absPath, e.what());
        }
    }
}