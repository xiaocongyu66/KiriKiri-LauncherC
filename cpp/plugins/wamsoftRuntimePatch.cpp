// SPDX-License-Identifier: MIT
//
// Wamsoft 系プラグインが Android では未実装であることを前提に、
// 旧 Windows 版 KAG ゲームスクリプトが期待するグローバルオブジェクトを
// 走時に注入してスクリプト顶层崩溃を回避する補丁レイヤ。
//
// 想定: 商用 Wamsoft ライブラリ (wfBasicEffect / wfTypicalDSP /
// wumultitrack / wuopus / wuvorbis / wvdecoder) はクローズドソースで、
// Android 版 krkr2 では ncb スタブを部分的にしか移植できない。
//
// その結果、たとえば limelight 系ゲームの `voiceeffect.tjs` トップレベルで
// `kag.addPlugin(new VoiceEffectPlugin())` が失敗すると、グローバルに
// `voiceEffectPlugin` が登録されない。LineModeEx の onStore フックに
// 登録された storeVoiceMap が走るタイミングで `voiceEffectPlugin does not
// exist` で全体停止してしまう。
//
// 本パッチは patch.tjs 実行後に毎回走る軽量 TJS 片で、
// 「未定義の場合のみ」 dummy の voiceEffectPlugin を Dictionary として
// グローバルに用意する。実装が完成しているゲームでは何もしない。
#include <spdlog/spdlog.h>

#include "tjsCommHead.h"
#include "ScriptMgnIntf.h"

namespace {

// 注入する TJS スクリプト本体。`global.foo == void` チェックで重複適用を
// 回避し、すでに本物が登録されている場合は何もしない。
//
// Dictionary を使う理由:
//   - TJS の Dictionary は任意のメンバ追加が許される
//   - メソッド = function 値の代入で十分 (ゲームは duck-typing なので
//     `voiceEffectPlugin.storeVoiceMap()` のようにメソッド呼び出しさえ
//     成立すればよい)
const tjs_char *kRuntimePatchSource =
    TJS_W("// auto-generated runtime compatibility patch (do not edit)\n")
    TJS_W("if(typeof global.__krkr2_runtimePatchApplied == \"undefined\") {\n")
    TJS_W("  global.__krkr2_runtimePatchApplied = true;\n")

    // ---------- 1) ダミー Plugin プレースホルダ生成器 ----------
    // 任意名のメソッドを no-op として返すヘルパ
    TJS_W("  function __krkr2_makeNoopFn() {\n")
    TJS_W("    return function() { return void; };\n")
    TJS_W("  }\n")

    // KAGPlugin 互換 + voiceeffect.tjs 内部メソッドを最低限備えた
    // Dictionary を返す。
    TJS_W("  function __krkr2_makeDummyPlugin(name) {\n")
    TJS_W("    var d = %[ ];\n")
    TJS_W("    d.__pluginName = name;\n")
    // KAGPlugin 標準フック
    TJS_W("    d.onStore = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onRestore = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onCompare = function() { return true; };\n")
    TJS_W("    d.onScenarioLoaded = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onScenarioStored = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onMessageHistoryClear = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onCopyFront = __krkr2_makeNoopFn();\n")
    // voiceeffect.tjs が外部から呼ぶメソッド群
    TJS_W("    d.storeVoiceMap = __krkr2_makeNoopFn();\n")
    TJS_W("    d.restoreVoiceMap = __krkr2_makeNoopFn();\n")
    TJS_W("    d.setVoiceEffect = __krkr2_makeNoopFn();\n")
    TJS_W("    d.checkVoiceEffectParam = __krkr2_makeNoopFn();\n")
    TJS_W("    d.filterVoice = __krkr2_makeNoopFn();\n")
    TJS_W("    d.checkVoiceParam = __krkr2_makeNoopFn();\n")
    TJS_W("    d.onVoiceEffectEnabledChanged = __krkr2_makeNoopFn();\n")
    TJS_W("    d._voiceEffect_storeVoiceMap = __krkr2_makeNoopFn();\n")
    TJS_W("    d._voiceEffect_restoreVoiceMap = __krkr2_makeNoopFn();\n")
    TJS_W("    d._voiceEffect_filterVoice = __krkr2_makeNoopFn();\n")
    TJS_W("    d._voiceEffect_checkVoiceParam = __krkr2_makeNoopFn();\n")
    TJS_W("    d.enabled = false;\n")
    TJS_W("    return d;\n")
    TJS_W("  }\n")

    // ---------- 2) KAG プラグイン用ショートカット ----------
    // KAGWindow.addPlugin が失敗したときのフォールバック。
    // typeof global[name] のような動的アクセスは TJS2 では受け付けない
    // 場合があるため、try/catch で実存チェックを行う。
    TJS_W("  function __krkr2_ensureKagPluginShortcut(name) {\n")
    TJS_W("    var alreadyExists = false;\n")
    TJS_W("    try {\n")
    TJS_W("      var probe = global[name];\n")
    TJS_W("      if(probe !== void) alreadyExists = true;\n")
    TJS_W("    } catch(e) {\n")
    TJS_W("      alreadyExists = false;\n")
    TJS_W("    }\n")
    TJS_W("    if(!alreadyExists) {\n")
    TJS_W("      global[name] = __krkr2_makeDummyPlugin(name);\n")
    TJS_W("    }\n")
    TJS_W("  }\n")
    TJS_W("  __krkr2_ensureKagPluginShortcut(\"voiceEffectPlugin\");\n")
    TJS_W("  __krkr2_ensureKagPluginShortcut(\"voiceEffectFactory\");\n")
    TJS_W("}\n");

} // namespace

void TVPExecuteWamsoftRuntimePatch() {
    try {
        // class やステートメントを含むため Script として実行する
        TVPExecuteScript(ttstr(kRuntimePatchSource));
        spdlog::info("[krkr] wamsoft runtime patch applied");
    } catch(const TJS::eTJSScriptError &e) {
        spdlog::error("[krkr] wamsoft runtime patch script error: {}",
                      ttstr(e.GetMessage()).AsStdString());
    } catch(const TJS::eTJS &e) {
        spdlog::error("[krkr] wamsoft runtime patch tjs error: {}",
                      ttstr(e.GetMessage()).AsStdString());
    } catch(const std::exception &e) {
        spdlog::error("[krkr] wamsoft runtime patch failed: {}", e.what());
    } catch(...) {
        spdlog::error("[krkr] wamsoft runtime patch failed: unknown error");
    }
}
