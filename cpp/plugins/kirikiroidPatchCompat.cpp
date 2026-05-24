#include <spdlog/spdlog.h>

#include "StorageIntf.h"
#include "TextStream.h"
#include "tjsCommHead.h"

void TVPEnsureKirikiroidCompatibilityPatch() {
    try {
        ttstr path = TVPGetAppPath() + TJS_W("patch.tjs");
        if(TVPIsExistentStorageNoSearch(path)) {
            spdlog::info("[krkr] keep existing patch.tjs: {}",
                         path.AsStdString());
            return;
        }

        static const tjs_char *kAutoPatch =
            TJS_W("// auto-generated compatibility patch\n")
            TJS_W("try { System.setArgument(\"-debugwin\", \"no\"); } catch(e) {}\n")
            TJS_W("try { Plugins.link(\"kirikiroid2.dll\"); } catch(e) {}\n")
            TJS_W("try {\n")
            TJS_W("  if(typeof Scripts != \"undefined\" && typeof Scripts.execStorage == \"function\") {\n")
            TJS_W("    if(typeof Scripts._execStorage_krkrCompat == \"undefined\") {\n")
            TJS_W("      Scripts._execStorage_krkrCompat = Scripts.execStorage;\n")
            TJS_W("      Scripts.execStorage = function(name) {\n")
            TJS_W("        var ret = Scripts._execStorage_krkrCompat(*);\n")
            TJS_W("        try {\n")
            TJS_W("          if(name == \"Override.tjs\" || name == \"override.tjs\") {\n")
            TJS_W("            if(Storages.isExistentStorage(System.exePath + \"Override2.tjs\"))\n")
            TJS_W("              Scripts._execStorage_krkrCompat(System.exePath + \"Override2.tjs\");\n")
            TJS_W("          }\n")
            TJS_W("          if(name == \"AfterInit.tjs\" || name == \"afterinit.tjs\") {\n")
            TJS_W("            if(Storages.isExistentStorage(System.exePath + \"AfterInit2.tjs\"))\n")
            TJS_W("              Scripts._execStorage_krkrCompat(System.exePath + \"AfterInit2.tjs\");\n")
            TJS_W("          }\n")
            TJS_W("        } catch(e) {}\n")
            TJS_W("        return ret;\n")
            TJS_W("      };\n")
            TJS_W("    }\n")
            TJS_W("  }\n")
            TJS_W("} catch(e) {}\n");

        iTJSTextWriteStream *stream =
            TVPCreateTextStreamForWrite(path, TJS_W("utf-8"));
        try {
            stream->Write(kAutoPatch);
        } catch(...) {
            stream->Destruct();
            throw;
        }
        stream->Destruct();
        spdlog::info("[krkr] wrote auto patch.tjs: {}", path.AsStdString());
    } catch(const std::exception &e) {
        spdlog::error("[krkr] failed to write auto patch.tjs: {}", e.what());
    } catch(const char *e) {
        spdlog::error("[krkr] failed to write auto patch.tjs: {}",
                      e ? e : "(null)");
    } catch(const tjs_char *e) {
        spdlog::error("[krkr] failed to write auto patch.tjs: {}",
                      e ? ttstr(e).AsStdString() : std::string("(null)"));
    } catch(...) {
        spdlog::error("[krkr] failed to write auto patch.tjs: unknown error");
    }
}
