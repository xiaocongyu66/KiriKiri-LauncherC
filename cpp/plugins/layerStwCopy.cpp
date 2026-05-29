// layerStwCopy.dll compatibility
//
// Some KAG systems use a small Windows plug-in that adds
// Layer.stitchWrappedCopy for tiled transition effects.  Android builds cannot
// load the DLL, so provide the method directly and keep the behavior generic:
// copy a source rectangle into this layer, wrapping source coordinates.

#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("layerStwCopy.dll")

#include "LayerImpl.h"

#include <algorithm>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace {

static tTJSNI_Layer *GetNativeLayer(iTJSDispatch2 *obj) {
    if(!obj)
        return nullptr;
    tTJSNI_Layer *layer = nullptr;
    if(TJS_FAILED(obj->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer)))) {
        return nullptr;
    }
    return layer;
}

static tjs_int WrapCoord(tjs_int value, tjs_int limit) {
    if(limit <= 0)
        return 0;
    value %= limit;
    if(value < 0)
        value += limit;
    return value;
}

static tjs_int VariantToInt(tTJSVariant **param, tjs_int index,
                            tjs_int fallback = 0) {
    if(!param[index] || param[index]->Type() == tvtVoid)
        return fallback;
    try {
        return static_cast<tjs_int>(*param[index]);
    } catch(...) {
        return fallback;
    }
}

} // namespace

struct layerStwCopy {
    static tjs_error TJS_INTF_METHOD stitchWrappedCopyCompat(
        tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
        iTJSDispatch2 *objthis) {
        if(result)
            result->Clear();
        tTJSNI_Layer *dstLayer = GetNativeLayer(objthis);
        if(!dstLayer)
            return TJS_S_OK;

        // Original calls use:
        //   dest.stitchWrappedCopy(dx, dy, w, h, src, sx, sy, ...)
        // Extra parameters are transition metadata and are not needed for the
        // copy itself.
        if(numparams < 7)
            return TJS_S_OK;
        if(!param[4] || param[4]->Type() != tvtObject)
            return TJS_S_OK;

        iTJSDispatch2 *srcObj = param[4]->AsObjectNoAddRef();
        if(!srcObj)
            return TJS_S_OK;

        tTJSNI_Layer *srcLayer = GetNativeLayer(srcObj);
        if(!srcLayer)
            return TJS_S_OK;

        auto *dstBuffer = reinterpret_cast<unsigned char *>(
            dstLayer->GetMainImagePixelBufferForWrite());
        auto *srcBuffer = reinterpret_cast<const unsigned char *>(
            srcLayer->GetMainImagePixelBuffer());
        const tjs_int dstW = static_cast<tjs_int>(dstLayer->GetImageWidth());
        const tjs_int dstH = static_cast<tjs_int>(dstLayer->GetImageHeight());
        const tjs_int srcW = static_cast<tjs_int>(srcLayer->GetImageWidth());
        const tjs_int srcH = static_cast<tjs_int>(srcLayer->GetImageHeight());
        const tjs_int dstPitch = dstLayer->GetMainImagePixelBufferPitch();
        const tjs_int srcPitch = srcLayer->GetMainImagePixelBufferPitch();
        if(!dstBuffer || !srcBuffer || dstW <= 0 || dstH <= 0 || srcW <= 0 ||
           srcH <= 0 || dstPitch == 0 || srcPitch == 0) {
            return TJS_S_OK;
        }

        const tjs_int dx = VariantToInt(param, 0);
        const tjs_int dy = VariantToInt(param, 1);
        tjs_int width = VariantToInt(param, 2);
        tjs_int height = VariantToInt(param, 3);
        const tjs_int sx = VariantToInt(param, 5);
        const tjs_int sy = VariantToInt(param, 6);

        if(width <= 0)
            width = dstW;
        if(height <= 0)
            height = dstH;

        const tTVPRect &clip = dstLayer->GetClip();
        const tjs_int copyL = std::max({ dx, clip.left, 0 });
        const tjs_int copyT = std::max({ dy, clip.top, 0 });
        const tjs_int copyR = std::min({ dx + width, clip.right, dstW });
        const tjs_int copyB = std::min({ dy + height, clip.bottom, dstH });
        if(copyL >= copyR || copyT >= copyB)
            return TJS_S_OK;

        for(tjs_int dstY = copyT; dstY < copyB; ++dstY) {
            const tjs_int wrappedY = WrapCoord(sy + dstY - dy, srcH);
            auto *dstLine = reinterpret_cast<tjs_uint32 *>(
                dstBuffer + dstY * dstPitch);
            auto *srcLine = reinterpret_cast<const tjs_uint32 *>(
                srcBuffer + wrappedY * srcPitch);

            for(tjs_int dstX = copyL; dstX < copyR; ++dstX) {
                dstLine[dstX] = srcLine[WrapCoord(sx + dstX - dx, srcW)];
            }
        }

        dstLayer->Update(tTVPRect(copyL, copyT, copyR, copyB));
        if(result)
            *result = 1;
        return TJS_S_OK;
    }
};

NCB_ATTACH_CLASS(layerStwCopy, Layer) {
    NCB_METHOD_RAW_CALLBACK(stitchWrappedCopy,
                            &layerStwCopy::stitchWrappedCopyCompat, 0);
}
