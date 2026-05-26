// SPDX-License-Identifier: MIT
//
// voiceEffectPlugin / voiceEffectFactory native fallback
//
// Some KAG 系ゲーム (limelight 系など) ships a game-side `voiceeffect.tjs`
// that builds a `VoiceEffectPlugin` chain on top of the closed-source
// wamsoft DLLs (wfBasicEffect / wfTypicalDSP / wumultitrack / wuopus /
// wuvorbis / wvdecoder). On Android several of those plugins are not
// available, so the construction
//
//     kag.voiceEffectPlugin = new VoiceEffectPlugin();
//
// fails halfway, leaving `voiceEffectPlugin` undefined. Later,
// `storeVoiceMap()` (a free-identifier lookup that walks `this -> global`)
// throws `Member "voiceEffectPlugin" does not exist`.
//
// Rather than try to reimplement these huge DLL chains, we expose a
// permissive stub on `global` that:
//   * answers `PropGet` for any member with a no-op getter
//     (returning the same stub recursively, so chains like
//     `voiceEffectPlugin.foo.bar` survive),
//   * accepts `PropSet` silently (no-op),
//   * accepts `FuncCall` silently (returns void),
//   * accepts `IsValid` truthfully (it's a real dispatch),
//   * lets the game proceed without losing the rest of voiceeffect.tjs.
//
// This is registered AT engine boot, so by the time `voiceeffect.tjs`
// runs the binding is already in place and TJS's this-proxy fallback to
// `global` resolves the identifier without a thrown exception.
//
// Strict equivalents (real audio) belong in a future port of the wamsoft
// plugins; for now keep gameplay running.

#include <atomic>

#include <spdlog/spdlog.h>
#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsObject.h"
#include "tjsVariant.h"
#include "ScriptMgnIntf.h"
#include "DebugIntf.h"

extern "C" iTJSDispatch2 *TVPGetCompatPermissiveStub();

#if defined(__ANDROID__)
extern "C" void KR2RenderProbeWriteF(const char *fmt, ...);
#define KR2_DIAG(fmt_, ...) KR2RenderProbeWriteF(fmt_, ##__VA_ARGS__)
#else
#define KR2_DIAG(...) ((void)0)
#endif

namespace {

// ---------------------------------------------------------------------------
// Diagnostic helper.
//
// limelight crashes mid-option with `Cannot convert (object) to real`, where
// the (object) is *our* permissive stub. To pinpoint which member a script
// is actually reading on the stub (so we can return a real-typed default for
// just those names instead of throwing), we mirror the first ~N PropGet /
// FuncCall hits to render_probe.log so they show up in 78.log's engine log
// section. TVPAddLog is NOT used here because that engine log stream is not
// captured by the launcher's diagnostics panel on Android.
//
// The cap is intentional — once the stub is wired into `kag.voiceEffectPlugin`
// scripts will iterate over it and a few hundred entries is plenty to identify
// the culprit without flooding logcat.
// ---------------------------------------------------------------------------
std::atomic<int> g_stubLogCount{0};
constexpr int kStubLogMax = 200;

inline void LogStubHit(const char *op, const tjs_char *membername) {
    int n = g_stubLogCount.fetch_add(1, std::memory_order_relaxed);
    if(n >= kStubLogMax)
        return;
    if(membername) {
        ttstr name(membername);
        KR2_DIAG("[krkr][stub] %s: %s", op,
                 name.AsStdString().c_str());
    } else {
        KR2_DIAG("[krkr][stub] %s: (byNum)", op);
    }
}

// ---------------------------------------------------------------------------
// tTJSPermissiveStub
//
// `tTJSDispatch` derives all virtual methods from `iTJSDispatch2`; the
// default in tjsObject.h returns `TJS_E_MEMBERNOTFOUND` for everything,
// which is exactly what we want to *avoid*. Override every relevant slot
// to behave as a swallowing proxy.
// ---------------------------------------------------------------------------
class tTJSPermissiveStub : public tTJSDispatch {
public:
    explicit tTJSPermissiveStub(const tjs_char *debugName)
        : DebugName(debugName ? debugName : TJS_W("(anonymous)")) {}

    // Members whose names suggest a numeric value. When the script does
    // `mul`, `div`, `cgt` or any other arithmetic on a stub PropGet, TJS
    // calls `tTJSVariant::AsReal()` which throws on tvtObject — that's
    // exactly the "Cannot convert (object) to real" crash limelight hits
    // after a `loadFilter()` chain. By returning a real 0 instead of self
    // for these names, the arithmetic keeps the script alive.
    //
    // Conservative list: only names that semantically MUST be numeric in
    // KAG / wamsoft-style filter / waveFilters / voiceEffect APIs. Anything
    // else still falls through to "return self" so `.foo.bar` chains work.
    static bool IsNumericLikeMember(const tjs_char *name) {
        if(!name)
            return false;
        static const tjs_char *const kNumericMembers[] = {
            TJS_W("count"),
            TJS_W("length"),
            TJS_W("size"),
            TJS_W("volume"),
            TJS_W("level"),
            TJS_W("gain"),
            TJS_W("pan"),
            TJS_W("pitch"),
            TJS_W("speed"),
            TJS_W("freq"),
            TJS_W("frequency"),
            TJS_W("samplerate"),
            TJS_W("channels"),
            TJS_W("bitrate"),
            TJS_W("position"),
            TJS_W("time"),
            TJS_W("duration"),
            nullptr,
        };
        for(const tjs_char *const *p = kNumericMembers; *p; ++p) {
            if(TJS_strcmp(name, *p) == 0)
                return true;
        }
        return false;
    }

    // PropGet returns this stub itself wrapped in a tTJSVariant so that
    // chained accesses resolve without crashing. Numeric-looking members
    // return integer 0 instead so arithmetic does not throw.
    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override {
        LogStubHit("PropGet", membername);
        if(result) {
            if(IsNumericLikeMember(membername)) {
                *result = (tjs_int)0;
            } else {
                // returning self makes voiceEffectPlugin.foo.bar still work
                tTJSVariant v(this, this);
                *result = v;
            }
        }
        return TJS_S_OK;
    }

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                           tTJSVariant *result,
                           iTJSDispatch2 *objthis) override {
        LogStubHit("PropGetByNum", nullptr);
        if(result) {
            tTJSVariant v(this, this);
            *result = v;
        }
        return TJS_S_OK;
    }

    tjs_error PropSet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, const tTJSVariant *param,
                      iTJSDispatch2 *objthis) override {
        // accept silently — game scripts often write `obj.x = y` on us
        return TJS_S_OK;
    }

    tjs_error PropSetByNum(tjs_uint32 flag, tjs_int num,
                           const tTJSVariant *param,
                           iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error PropSetByVS(tjs_uint32 flag, tTJSVariantString *membername,
                          const tTJSVariant *param,
                          iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                       tjs_uint32 *hint, tTJSVariant *result,
                       tjs_int numparams, tTJSVariant **param,
                       iTJSDispatch2 *objthis) override {
        LogStubHit("FuncCall", membername);
        if(result) {
            // Return self so chained calls like
            //   kag.voiceEffectPlugin.loadFilter(...).foo
            // and
            //   waveFilters.add(kag.voiceEffectPlugin.loadFilter(...))
            // work even though we never actually built a filter.
            // Returning void here triggers "Cannot convert (() to Object)"
            // upstream in option.ks / movieaudiosample.tjs.
            tTJSVariant v(this, this);
            *result = v;
        }
        return TJS_S_OK;
    }

    tjs_error FuncCallByNum(tjs_uint32 flag, tjs_int num,
                            tTJSVariant *result, tjs_int numparams,
                            tTJSVariant **param,
                            iTJSDispatch2 *objthis) override {
        LogStubHit("FuncCallByNum", nullptr);
        if(result) {
            tTJSVariant v(this, this);
            *result = v;
        }
        return TJS_S_OK;
    }

    tjs_error IsValid(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, iTJSDispatch2 *objthis) override {
        return TJS_S_TRUE;
    }

    tjs_error IsValidByNum(tjs_uint32 flag, tjs_int num,
                           iTJSDispatch2 *objthis) override {
        return TJS_S_TRUE;
    }

    tjs_error DeleteMember(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint,
                           iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error DeleteMemberByNum(tjs_uint32 flag, tjs_int num,
                                iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error Invalidate(tjs_uint32 flag, const tjs_char *membername,
                         tjs_uint32 *hint,
                         iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error InvalidateByNum(tjs_uint32 flag, tjs_int num,
                              iTJSDispatch2 *objthis) override {
        return TJS_S_OK;
    }

    tjs_error IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
                           tjs_uint32 *hint, const tjs_char *classname,
                           iTJSDispatch2 *objthis) override {
        // Reply truthfully to common KAGPlugin / KAGEx checks.
        return TJS_S_TRUE;
    }

    tjs_error GetCount(tjs_int *result, const tjs_char *membername,
                       tjs_uint32 *hint, iTJSDispatch2 *objthis) override {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }

    tjs_error GetCountByNum(tjs_int *result, tjs_int num,
                            iTJSDispatch2 *objthis) override {
        if(result)
            *result = 0;
        return TJS_S_OK;
    }

private:
    ttstr DebugName;
};

// One instance per registered name; lifetime = engine lifetime.
void RegisterPermissiveStubOnGlobal(iTJSDispatch2 *global,
                                    const tjs_char *name) {
    if(!global || !name)
        return;

    // Skip if game / another plugin already provided a real implementation.
    // Important: do NOT treat plain void-success compat reads as "already
    // exists". Our tjsObject.cpp PropGet whitelist may return TJS_S_OK with
    // result=void for missing members such as kag.voiceEffectPlugin, and if we
    // stop here the real permissive stub never gets attached to kag.
    {
        tTJSVariant existing;
        if(global->PropGet(0, name, nullptr, &existing, global) == TJS_S_OK &&
           existing.Type() == tvtObject &&
           existing.AsObjectNoAddRef() != nullptr) {
            const tjs_char *compatStubName = TJS_W("(compat-void-stub)");
            iTJSDispatch2 *existingObj = existing.AsObjectNoAddRef();
            tTJSVariant existingTypeVar;
            ttstr existingType;
            if(existingObj) {
                tTJSVariant dummy;
                existingObj->Operation(0, TJS_W("typeof"), nullptr, &existingTypeVar,
                                       &dummy, nullptr);
                if(existingTypeVar.Type() == tvtString)
                    existingType = ttstr(existingTypeVar);
            }
            if(existingType == compatStubName) {
                TVPAddLog(ttstr(TJS_W("[krkr] replacing placeholder compat stub for ")) +
                          name);
            } else {
                spdlog::info("[krkr] {} already exists, leaving real impl in place",
                             ttstr(name).AsStdString());
                return;
            }
        }
    }

    // Reuse the singleton from tjsObject.cpp so that later registration can
    // detect and replace placeholder objects on kag/global.
    iTJSDispatch2 *stub = TVPGetCompatPermissiveStub();
    if(!stub)
        return;

    auto *val = new tTJSVariant(stub, stub);
    tjs_error hr =
        global->PropSet(TJS_MEMBERENSURE, name, nullptr, val, global);
    delete val;
    if(TJS_FAILED(hr)) {
        spdlog::warn("[krkr] failed to register stub for {}: 0x{:x}",
                     ttstr(name).AsStdString(),
                     static_cast<unsigned>(hr));
    } else {
        spdlog::info("[krkr] registered permissive stub on global.{}",
                     ttstr(name).AsStdString());
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry. Called once after the script engine is initialized.
//
// Safe to call repeatedly: existing real plugins are preserved.
// ---------------------------------------------------------------------------
void TVPRegisterVoiceEffectStubs() {
    iTJSDispatch2 *global = TVPGetScriptDispatch();
    if(!global) {
        TVPAddLog(TJS_W("[krkr] voiceEffect stub: TVPGetScriptDispatch returned null"));
        spdlog::warn("[krkr] TVPGetScriptDispatch returned null, "
                     "skip voiceEffect stub registration");
        return;
    }

    // game scripts typically reach for these two top-level identifiers.
    // Add more here when new "Member XXX does not exist" reports come in
    // for objects that should logically behave as a no-op KAGPlugin.
    RegisterPermissiveStubOnGlobal(global, TJS_W("voiceEffectPlugin"));
    RegisterPermissiveStubOnGlobal(global, TJS_W("voiceEffectFactory"));

    // Many wamsoft-shaped scripts read `kag.voiceEffectPlugin` (KAGWindow
    // instance member), not `voiceEffectPlugin` directly, so a global-only
    // stub does not help. Also try to attach the same stub to the running
    // KAGWindow instance (`global.kag`) if it has been created already.
    {
        tTJSVariant kagVar;
        if(global->PropGet(0, TJS_W("kag"), nullptr, &kagVar, global) ==
               TJS_S_OK &&
           kagVar.Type() == tvtObject) {
            iTJSDispatch2 *kag = kagVar.AsObjectNoAddRef();
            if(kag) {
                TVPAddLog(TJS_W("[krkr] voiceEffect stub: kag already exists, "
                                "attaching stubs to it"));
                RegisterPermissiveStubOnGlobal(kag,
                                               TJS_W("voiceEffectPlugin"));
                RegisterPermissiveStubOnGlobal(kag,
                                               TJS_W("voiceEffectFactory"));
                // wamsoft KAG plugins that we don't ship on Android. Each
                // is driven by a dedicated DLL on Windows; without the DLL
                // the stock script still publishes its instance member
                // name, then the `[trans method=...]` / sysHook handlers
                // call .processOpen / .processStop / .filterVoice on it.
                // Returning a permissive stub for those slots lets the
                // KAG dispatcher reach the no-op fast path instead of
                // raising "Member ... does not exist" repeatedly.
                RegisterPermissiveStubOnGlobal(kag,
                                               TJS_W("sysTransitionEffect"));
            }
        } else {
            TVPAddLog(TJS_W("[krkr] voiceEffect stub: kag not yet created, "
                            "global stub only"));
        }
    }

    global->Release();
}

// ---------------------------------------------------------------------------
// Public accessor for tjsObject.cpp's PropGet whitelist fallback.
// Returns a singleton permissive stub instance (lifetime = engine).
// Caller does NOT own the reference; AddRef yourself if you store it.
// ---------------------------------------------------------------------------

namespace {
// Singleton instance — definition outside anonymous block would also work
// but we keep it together with the class. Using a function-local static
// gives us thread-safe lazy init under C++11.
tTJSPermissiveStub *GetCompatPermissiveStubInternal() {
    static tTJSPermissiveStub *singleton = nullptr;
    if(!singleton) {
        singleton = new tTJSPermissiveStub(TJS_W("(compat-void-stub)"));
        // singleton stays alive forever; never Release it.
    }
    return singleton;
}
} // namespace

extern "C" iTJSDispatch2 *TVPGetCompatPermissiveStub() {
    return GetCompatPermissiveStubInternal();
}

// ---------------------------------------------------------------------------
// Diagnostic exit point used by tjsObject.cpp::PropGet right before it
// returns TJS_E_MEMBERNOTFOUND. We can't always tell from the upstream
// "Member XXX does not exist" exception which member was being read on
// which object — by funneling missing names through KR2RenderProbeWriteF
// here we get a chronological list in render_probe.log (and thus 78.log)
// that immediately precedes a crash.
//
// Filter out names that are produced by *typeof probes* in stock KAG3
// startup (Config.tjs uses `if (typeof patch_appendN != "undefined") ...`
// for slots 0..99, plus a small list of helper names that legitimate KAG
// games never define). These are noise and would otherwise drown out the
// real culprit by exhausting the per-run budget within startup.
//
// Independent of the stub's own LogStubHit budget.
// ---------------------------------------------------------------------------
namespace {
std::atomic<int> g_missingLogCount{0};
constexpr int kMissingLogMax = 800;

bool IsKnownTypeofProbeName(const tjs_char *name) {
    if(!name)
        return true;
    // Numeric-suffixed typeof probes from Config.tjs.
    static const tjs_char *const kPrefixProbes[] = {
        TJS_W("patch_append"),
        TJS_W("folder_append"),
        nullptr,
    };
    for(const tjs_char *const *p = kPrefixProbes; *p; ++p) {
        const tjs_char *prefix = *p;
        size_t plen = TJS_strlen(prefix);
        if(TJS_strncmp(name, prefix, plen) == 0) {
            // Tail must be all digits to count as a probe (don't accidentally
            // swallow a real "patch_append_thing" definition).
            const tjs_char *q = name + plen;
            if(*q == 0)
                continue; // "patch_append" with no suffix — not a probe
            bool allDigits = true;
            for(; *q; ++q) {
                if(*q < TJS_W('0') || *q > TJS_W('9')) {
                    allDigits = false;
                    break;
                }
            }
            if(allDigits)
                return true;
        }
    }
    // Bare typeof probes that show up on every limelight startup but never
    // matter to a game running on krkr2-pro. Update conservatively.
    static const tjs_char *const kExactProbes[] = {
        TJS_W("__krkr2_runtimePatchApplied"),
        TJS_W("kirikiriz_generic"),
        TJS_W("commitSavedata"),
        TJS_W("getToolsPath"),
        TJS_W("developMode"),
        TJS_W("setDefaultDllDirectories"),
        TJS_W("setuphook"),
        TJS_W("group"),
        TJS_W("min"),
        TJS_W("max"),
        TJS_W("dirlist"),
        TJS_W("motion"),
        TJS_W("thum"),
        TJS_W("scenario"),
        TJS_W("uipsd"),
        TJS_W("locale"),
        TJS_W("arcPath"),
        TJS_W("OGLDrawDevice"),
        TJS_W("ShortCutInitialPadKeyMap"),
        nullptr,
    };
    for(const tjs_char *const *p = kExactProbes; *p; ++p) {
        if(TJS_strcmp(name, *p) == 0)
            return true;
    }
    return false;
}
} // namespace

extern "C" void TVPCompatLogMissingMember(const tjs_char *membername) {
    if(!membername)
        return;
    if(IsKnownTypeofProbeName(membername))
        return;
    int n = g_missingLogCount.fetch_add(1, std::memory_order_relaxed);
    if(n >= kMissingLogMax)
        return;
    ttstr name(membername);
    KR2_DIAG("[krkr][missing] %s", name.AsStdString().c_str());
}
