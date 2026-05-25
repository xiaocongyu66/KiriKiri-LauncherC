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

#include <spdlog/spdlog.h>
#include "tjsCommHead.h"
#include "tjs.h"
#include "tjsObject.h"
#include "tjsVariant.h"
#include "ScriptMgnIntf.h"
#include "DebugIntf.h"

extern "C" iTJSDispatch2 *TVPGetCompatPermissiveStub();

namespace {

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

    // PropGet returns this stub itself wrapped in a tTJSVariant so that
    // chained accesses resolve without crashing.
    tjs_error PropGet(tjs_uint32 flag, const tjs_char *membername,
                      tjs_uint32 *hint, tTJSVariant *result,
                      iTJSDispatch2 *objthis) override {
        if(result) {
            // returning self makes voiceEffectPlugin.foo.bar still work
            tTJSVariant v(this, this);
            *result = v;
        }
        return TJS_S_OK;
    }

    tjs_error PropGetByNum(tjs_uint32 flag, tjs_int num,
                           tTJSVariant *result,
                           iTJSDispatch2 *objthis) override {
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
