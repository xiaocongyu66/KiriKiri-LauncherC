#include "ncbind.hpp"
#include "TransIntf.h"

class tExtransFallbackProvider : public iTVPTransHandlerProvider {
    tjs_int RefCount;
    ttstr Name;
    ttstr FallbackName;

public:
    tExtransFallbackProvider(const tjs_char *name,
                             const tjs_char *fallback_name) :
        RefCount(1), Name(name), FallbackName(fallback_name) {}

    tjs_error AddRef() override {
        RefCount++;
        return TJS_S_OK;
    }

    tjs_error Release() override {
        if(RefCount == 1) {
            delete this;
        } else {
            RefCount--;
        }
        return TJS_S_OK;
    }

    tjs_error GetName(const tjs_char **name) override {
        if(!name)
            return TJS_E_FAIL;
        *name = Name.c_str();
        return TJS_S_OK;
    }

    tjs_error StartTransition(iTVPSimpleOptionProvider *options,
                              iTVPSimpleImageProvider *imagepro,
                              tTVPLayerType layertype, tjs_uint src1w,
                              tjs_uint src1h, tjs_uint src2w,
                              tjs_uint src2h, tTVPTransType *type,
                              tTVPTransUpdateType *updatetype,
                              iTVPBaseTransHandler **handler) override {
        iTVPTransHandlerProvider *fallback =
            TVPFindTransHandlerProvider(FallbackName);
        if(!fallback)
            return TJS_E_FAIL;
        tjs_error err = fallback->StartTransition(
            options, imagepro, layertype, src1w, src1h, src2w, src2h, type,
            updatetype, handler);
        fallback->Release();
        return err;
    }
};

static void AddExtransProvider(const tjs_char *name,
                               const tjs_char *fallback_name) {
    iTVPTransHandlerProvider *provider =
        new tExtransFallbackProvider(name, fallback_name);
    try {
        TVPAddTransHandlerProvider(provider);
    } catch(...) {
        provider->Release();
    }
}

static void extrans_init() {
    AddExtransProvider(TJS_W("wave"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("mosaic"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("turn"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("rotatezoom"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("rotatevanish"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("rotateswap"), TJS_W("crossfade"));
    AddExtransProvider(TJS_W("ripple"), TJS_W("crossfade"));
}

// Lightweight compatibility modules. extrans registers transition names
// provided by the original plugin and maps them to the stable built-in
// transition pipeline; the audio/image entries keep Plugins.link() probes
// working on Android.

#define NCB_MODULE_NAME TJS_W("extrans.dll")
NCB_PRE_REGIST_CALLBACK(extrans_init);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuvorbis.dll")
static void wuvorbis_stub() {}
NCB_PRE_REGIST_CALLBACK(wuvorbis_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuopus.dll")
static void wuopus_stub() {}
NCB_PRE_REGIST_CALLBACK(wuopus_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("wuflac.dll")
static void wuflac_stub() {}
NCB_PRE_REGIST_CALLBACK(wuflac_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExColor.dll")
static void layerExColor_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExColor_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExMosaic.dll")
static void layerExMosaic_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExMosaic_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExSave.dll")
static void layerExSave_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExSave_stub);

#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("layerExAVI.dll")
static void layerExAVI_stub() {}
NCB_PRE_REGIST_CALLBACK(layerExAVI_stub);
