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
            TJS_W("try { Plugins.link(\"kirikiroid2.dll\"); } catch(e) {}\n");

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
