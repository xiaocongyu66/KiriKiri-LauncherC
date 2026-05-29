// layerStwCopy.dll compatibility
//
// Some KAG systems use a small Windows plug-in that adds
// Layer.stitchWrappedCopy for tiled transition effects.  Android builds cannot
// load the DLL, so provide the method directly and keep the behavior generic:
// copy a source rectangle into this layer, wrapping source coordinates.

#include "ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("layerStwCopy.dll")

#include "layerExBase_wamsoft.hpp"

namespace {

static bool GetLayerInt(iTJSDispatch2 *obj, const tjs_char *name,
                        tjs_int &value) {
    if(!obj)
        return false;
    tTJSVariant var;
    if(TJS_FAILED(obj->PropGet(0, name, nullptr, &var, obj)))
        return false;
    if(var.Type() == tvtVoid)
        return false;
    value = static_cast<tjs_int>(var);
    return true;
}

static bool GetLayerPtr(iTJSDispatch2 *obj, const tjs_char *name,
                        unsigned char *&value) {
    if(!obj)
        return false;
    tTJSVariant var;
    if(TJS_FAILED(obj->PropGet(0, name, nullptr, &var, obj)))
        return false;
    if(var.Type() == tvtVoid)
        return false;
    value = reinterpret_cast<unsigned char *>(
        static_cast<tjs_intptr_t>(static_cast<tTVInteger>(var)));
    return value != nullptr;
}

static tjs_int WrapCoord(tjs_int value, tjs_int limit) {
    if(limit <= 0)
        return 0;
    value %= limit;
    if(value < 0)
        value += limit;
    return value;
}

} // namespace

struct layerStwCopy : public layerExBase {
    explicit layerStwCopy(DispatchT obj) : layerExBase(obj) {}

    static tjs_error stitchWrappedCopyCompat(tTJSVariant *result,
                                             tjs_int numparams,
                                             tTJSVariant **param,
                                             layerStwCopy *self) {
        if(result)
            result->Clear();
        if(!self)
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

        self->reset();
        if(!self->_buffer || self->_width <= 0 || self->_height <= 0)
            return TJS_S_OK;

        tjs_int srcW = 0;
        tjs_int srcH = 0;
        tjs_int srcPitch = 0;
        unsigned char *srcBuffer = nullptr;
        if(!GetLayerInt(srcObj, TJS_W("imageWidth"), srcW) ||
           !GetLayerInt(srcObj, TJS_W("imageHeight"), srcH) ||
           !GetLayerInt(srcObj, TJS_W("mainImageBufferPitch"), srcPitch) ||
           !GetLayerPtr(srcObj, TJS_W("mainImageBuffer"), srcBuffer) ||
           srcW <= 0 || srcH <= 0) {
            return TJS_S_OK;
        }

        tjs_int dx = static_cast<tjs_int>(*param[0]);
        tjs_int dy = static_cast<tjs_int>(*param[1]);
        tjs_int width = static_cast<tjs_int>(*param[2]);
        tjs_int height = static_cast<tjs_int>(*param[3]);
        tjs_int sx = static_cast<tjs_int>(*param[5]);
        tjs_int sy = static_cast<tjs_int>(*param[6]);

        if(width <= 0)
            width = self->_width;
        if(height <= 0)
            height = self->_height;

        const tjs_int clipL = self->_clipLeft;
        const tjs_int clipT = self->_clipTop;
        const tjs_int clipR = clipL + self->_clipWidth;
        const tjs_int clipB = clipT + self->_clipHeight;

        for(tjs_int y = 0; y < height; ++y) {
            const tjs_int dstY = dy + y;
            if(dstY < 0 || dstY >= self->_height || dstY < clipT ||
               dstY >= clipB) {
                continue;
            }

            const tjs_int wrappedY = WrapCoord(sy + y, srcH);
            auto *dstLine = reinterpret_cast<tjs_uint32 *>(
                self->_buffer + dstY * self->_pitch);
            auto *srcLine = reinterpret_cast<const tjs_uint32 *>(
                srcBuffer + wrappedY * srcPitch);

            for(tjs_int x = 0; x < width; ++x) {
                const tjs_int dstX = dx + x;
                if(dstX < 0 || dstX >= self->_width || dstX < clipL ||
                   dstX >= clipR) {
                    continue;
                }
                dstLine[dstX] = srcLine[WrapCoord(sx + x, srcW)];
            }
        }

        self->redraw();
        if(result)
            *result = 1;
        return TJS_S_OK;
    }
};

NCB_GET_INSTANCE_HOOK(layerStwCopy) {
    NCB_INSTANCE_GETTER(objthis) {
        ClassT *obj = GetNativeInstance(objthis);
        if(!obj) {
            obj = new ClassT(objthis);
            SetNativeInstance(objthis, obj);
        }
        obj->reset();
        return obj;
    }
    ~NCB_GET_INSTANCE_HOOK_CLASS() {}
};

NCB_ATTACH_CLASS_WITH_HOOK(layerStwCopy, Layer) {
    NCB_METHOD_RAW_CALLBACK(stitchWrappedCopy,
                            &layerStwCopy::stitchWrappedCopyCompat, 0);
}
