#include "tjsCommHead.h"

#if defined(ANDROID)

#include <android/log.h>
#include <dlfcn.h>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <spdlog/spdlog.h>

#include "tjsInterCodeGen.h"
#include "tjsScriptBlock.h"
#include "tjsVariant.h"
#include "tjsDictionary.h"
#include "../utils/DebugIntf.h"
#include "dobby.h"

namespace TJS {

static std::once_flag g_hook_once;
static bool g_hook_installed = false;

using CallFunctionType = tjs_int (*)(tTJSInterCodeContext *thiz,
                                     tTJSVariant *ra,
                                     const tjs_int32 *code,
                                     tTJSVariant **args,
                                     tjs_int numargs);

static CallFunctionType orig_CallFunction = nullptr;

static const char *VariantTypeName(tTJSVariantType type) {
    switch(type) {
        case tvtVoid: return "void";
        case tvtObject: return "object";
        case tvtString: return "string";
        case tvtInteger: return "int";
        case tvtReal: return "real";
        case tvtOctet: return "octet";
        default: return "unknown";
    }
}

static void HookLog(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_DEBUG, "KrKr2Hook", "%s", buf);
    try {
        spdlog::debug("[hook] {}", buf);
    } catch(...) {
    }
    FILE *fp = fopen("/storage/emulated/0/Android/data/org.github.krkr2/files/krkr2_hook.log", "a");
    if(fp) {
        fputs(buf, fp);
        fputc('\n', fp);
        fclose(fp);
    }
}

static tjs_int fake_CallFunction(tTJSInterCodeContext *thiz,
                                 tTJSVariant *ra,
                                 const tjs_int32 *code,
                                 tTJSVariant **args,
                                 tjs_int numargs) {
    if(thiz && code) {
        const tjs_char *ctx_name = thiz->GetName();
        if(ctx_name && TJS_strstr(ctx_name, TJS_W("keybinder.tjs")) &&
           code[0] == VM_CALL) {
            tTJSVariantClosure clo;
            bool closure_ok = false;
            try {
                clo = TJS_GET_VM_REG(ra, code[2]).AsObjectClosure();
                closure_ok = true;
            } catch(...) {
            }

            iTJSDispatch2 *default_this = nullptr;
            try {
                default_this = ra[-1].AsObjectNoAddRef();
            } catch(...) {
            }

            const char *arg0_type = "(none)";
            if(numargs > 0 && args && args[0]) arg0_type = VariantTypeName(args[0]->Type());

            HookLog(
                "CallFunction hit ctx=%ls opcode=%d dst=%d clo_reg=%d obj_reg=%d numargs=%d arg0_type=%s clo.Object=%p clo.ObjThis=%p defaultThis=%p thiz=%p code=%p",
                ctx_name, (int)code[0], (int)code[1], (int)code[2], (int)code[3], (int)numargs,
                arg0_type,
                closure_ok ? clo.Object : nullptr,
                closure_ok ? clo.ObjThis : nullptr,
                default_this,
                thiz, code);

            if(closure_ok && clo.Object) {
                xdl_info_t info{};
                void *cache = nullptr;
                if(DobbyXdlAddr((void *)clo.Object, &info, &cache) == 0) {
                    HookLog("clo.Object addrinfo image=%s sym=%s saddr=%p ssize=%zu",
                            info.dli_fname ? info.dli_fname : "(null)",
                            info.dli_sname ? info.dli_sname : "(null)",
                            info.dli_saddr,
                            info.dli_ssize);
                }
                xdl_addr_clean(&cache);
            }

            if(closure_ok) clo.Release();
        }
    }
    return orig_CallFunction(thiz, ra, code, args, numargs);
}

void TVPInstallKrkrHook() {
    std::call_once(g_hook_once, []() {
        HookLog("install begin");
        size_t symbol_size = 0;
        const char *resolved_symbol = nullptr;
        const char *symbols[] = {
            // tjs_int32 is std::int32_t, which mangles as int on Android/arm64.
            "_ZN3TJS20tTJSInterCodeContext12CallFunctionEPNS_11tTJSVariantEPKiPPS1_i",
            // Keep the previous unsigned-int form for old cached builds.
            "_ZN3TJS20tTJSInterCodeContext12CallFunctionEPNS_11tTJSVariantEPKjPPS1_i",
        };

        void *sym = nullptr;
        for(const char *symbol : symbols) {
            sym = DobbySymbolResolverEx("libkrkr2.so", symbol,
                                        DOBBY_SYMBOL_RESOLVER_DEFAULT,
                                        &symbol_size);
            if(!sym) {
                sym = DobbySymbolResolverEx(nullptr, symbol,
                                            DOBBY_SYMBOL_RESOLVER_DEFAULT,
                                            &symbol_size);
            }
            if(sym) {
                resolved_symbol = symbol;
                break;
            }
            HookLog("resolver failed for CallFunction mangled symbol: %s",
                    symbol);
        }
        if(!sym) {
            HookLog("resolver failed for all CallFunction mangled symbols");
            return;
        }
        HookLog("resolver ok sym=%p size=%zu symbol=%s", sym, symbol_size,
                resolved_symbol);
        int rc = DobbyHook(sym, (void *)fake_CallFunction,
                           (void **)&orig_CallFunction);
        if(rc == 0 && orig_CallFunction) {
            g_hook_installed = true;
            HookLog("CallFunction hook installed sym=%p size=%zu orig=%p",
                    sym, symbol_size, (void *)orig_CallFunction);
        } else {
            HookLog("CallFunction hook failed rc=%d sym=%p orig=%p",
                    rc, sym, (void *)orig_CallFunction);
        }
    });
}

bool TVPIsKrkrHookInstalled() { return g_hook_installed; }

} // namespace TJS

#endif
