#include "ncbind.hpp"

#include <algorithm>
#include <cstring>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("layerExSave.dll")

typedef const tjs_uint8 *LayerExSaveBuf;
typedef tjs_uint8 *LayerExSaveWrt;

static bool LayerExSaveGetLayerSize(iTJSDispatch2 *layer, long &width,
                                    long &height, long &pitch) {
    if(!layer ||
       TJS_FAILED(layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                      layer)))
        return false;

    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, TJS_W("hasImage"), nullptr, &value,
                                 layer)) ||
       value.AsInteger() == 0)
        return false;

    value.Clear();
    if(TJS_FAILED(layer->PropGet(0, TJS_W("imageWidth"), nullptr, &value,
                                 layer)))
        return false;
    width = static_cast<long>(value.AsInteger());

    value.Clear();
    if(TJS_FAILED(layer->PropGet(0, TJS_W("imageHeight"), nullptr, &value,
                                 layer)))
        return false;
    height = static_cast<long>(value.AsInteger());

    value.Clear();
    if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBufferPitch"), nullptr,
                                 &value, layer)))
        return false;
    pitch = static_cast<long>(value.AsInteger());

    return width > 0 && height > 0 && pitch != 0;
}

static bool LayerExSaveGetLayerBuffer(iTJSDispatch2 *layer, long &width,
                                      long &height, LayerExSaveBuf &buffer,
                                      long &pitch) {
    if(!LayerExSaveGetLayerSize(layer, width, height, pitch))
        return false;

    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBuffer"), nullptr, &value,
                                 layer)))
        return false;

    buffer = reinterpret_cast<LayerExSaveBuf>(
        static_cast<tjs_intptr_t>(value.AsInteger()));
    return buffer != nullptr;
}

static bool LayerExSaveGetLayerBuffer(iTJSDispatch2 *layer, long &width,
                                      long &height, LayerExSaveWrt &buffer,
                                      long &pitch) {
    if(!LayerExSaveGetLayerSize(layer, width, height, pitch))
        return false;

    tTJSVariant value;
    if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBufferForWrite"), nullptr,
                                 &value, layer)))
        return false;

    buffer = reinterpret_cast<LayerExSaveWrt>(
        static_cast<tjs_intptr_t>(value.AsInteger()));
    return buffer != nullptr;
}

static void LayerExSaveMakeRect(tTJSVariant *result, long x, long y, long w,
                                long h) {
    if(!result)
        return;

    ncbDictionaryAccessor dict;
    dict.SetValue(TJS_W("x"), x);
    dict.SetValue(TJS_W("y"), y);
    dict.SetValue(TJS_W("w"), w);
    dict.SetValue(TJS_W("h"), h);
    *result = dict;
}

static bool LayerExSaveHasAlpha(LayerExSaveBuf pixel, long next, long count) {
    for(; count > 0; --count, pixel += next) {
        if(pixel[3] != 0)
            return true;
    }
    return false;
}

static tjs_error TJS_INTF_METHOD
LayerExSaveGetCropRect(tTJSVariant *result, tjs_int, tTJSVariant **,
                       iTJSDispatch2 *layer) {
    LayerExSaveBuf row = nullptr;
    LayerExSaveBuf buffer = nullptr;
    long width;
    long height;
    long pitch;
    const long pixelSize = 4;

    if(!LayerExSaveGetLayerBuffer(layer, width, height, buffer, pitch))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    long left = 0;
    long top = 0;
    long right = width - 1;
    long bottom = height - 1;
    if(result)
        result->Clear();

    for(row = buffer; left < width; ++left, row += pixelSize) {
        if(LayerExSaveHasAlpha(row, pitch, height))
            break;
    }
    if(left >= width)
        return TJS_S_OK;

    for(row = buffer + right * pixelSize; right >= 0;
        --right, row -= pixelSize) {
        if(LayerExSaveHasAlpha(row, pitch, height))
            break;
    }

    const long rectWidth = right - left + 1;
    for(row = buffer + left * pixelSize; top < height; ++top, row += pitch) {
        if(LayerExSaveHasAlpha(row, pixelSize, rectWidth))
            break;
    }
    for(row = buffer + left * pixelSize + bottom * pitch; bottom >= 0;
        --bottom, row -= pitch) {
        if(LayerExSaveHasAlpha(row, pixelSize, rectWidth))
            break;
    }

    LayerExSaveMakeRect(result, left, top, rectWidth, bottom - top + 1);
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(getCropRect, Layer, LayerExSaveGetCropRect);

static bool LayerExSaveHasNonZero(LayerExSaveBuf pixel, long next,
                                  long count) {
    for(; count > 0; --count, pixel += next) {
        if(pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0 || pixel[3] != 0)
            return true;
    }
    return false;
}

static tjs_error TJS_INTF_METHOD
LayerExSaveGetCropRectZero(tTJSVariant *result, tjs_int, tTJSVariant **,
                           iTJSDispatch2 *layer) {
    LayerExSaveBuf row = nullptr;
    LayerExSaveBuf buffer = nullptr;
    long width;
    long height;
    long pitch;
    const long pixelSize = 4;

    if(!LayerExSaveGetLayerBuffer(layer, width, height, buffer, pitch))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    long left = 0;
    long top = 0;
    long right = width - 1;
    long bottom = height - 1;
    if(result)
        result->Clear();

    for(row = buffer; left < width; ++left, row += pixelSize) {
        if(LayerExSaveHasNonZero(row, pitch, height))
            break;
    }
    if(left >= width)
        return TJS_S_OK;

    for(row = buffer + right * pixelSize; right >= 0;
        --right, row -= pixelSize) {
        if(LayerExSaveHasNonZero(row, pitch, height))
            break;
    }

    const long rectWidth = right - left + 1;
    for(row = buffer + left * pixelSize; top < height; ++top, row += pitch) {
        if(LayerExSaveHasNonZero(row, pixelSize, rectWidth))
            break;
    }
    for(row = buffer + left * pixelSize + bottom * pitch; bottom >= 0;
        --bottom, row -= pitch) {
        if(LayerExSaveHasNonZero(row, pixelSize, rectWidth))
            break;
    }

    LayerExSaveMakeRect(result, left, top, rectWidth, bottom - top + 1);
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(getCropRectZero, Layer, LayerExSaveGetCropRectZero);

static bool LayerExSaveSameColor(LayerExSaveBuf lhs, LayerExSaveBuf rhs) {
    return lhs[3] == rhs[3] &&
           (lhs[3] == 0 ||
            (lhs[2] == rhs[2] && lhs[1] == rhs[1] && lhs[0] == rhs[0]));
}

static bool LayerExSaveHasDiff(LayerExSaveBuf lhs, long lhsNext,
                               LayerExSaveBuf rhs, long rhsNext,
                               long count) {
    for(; count > 0; --count, lhs += lhsNext, rhs += rhsNext) {
        if(!LayerExSaveSameColor(lhs, rhs))
            return true;
    }
    return false;
}

static tjs_error TJS_INTF_METHOD
LayerExSaveGetDiffRect(tTJSVariant *result, tjs_int numparams,
                       tTJSVariant **param, iTJSDispatch2 *layer) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    iTJSDispatch2 *base = param[0]->AsObjectNoAddRef();
    LayerExSaveBuf baseRow = nullptr;
    LayerExSaveBuf layerRow = nullptr;
    LayerExSaveBuf baseBuffer = nullptr;
    LayerExSaveBuf layerBuffer = nullptr;
    long width;
    long height;
    long layerPitch;
    long baseWidth;
    long baseHeight;
    long basePitch;
    const long pixelSize = 4;

    if(!LayerExSaveGetLayerBuffer(layer, width, height, layerBuffer,
                                  layerPitch) ||
       !LayerExSaveGetLayerBuffer(base, baseWidth, baseHeight, baseBuffer,
                                  basePitch))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    if(width != baseWidth || height != baseHeight)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    long left = 0;
    long top = 0;
    long right = width - 1;
    long bottom = height - 1;
    if(result)
        result->Clear();

    for(baseRow = baseBuffer, layerRow = layerBuffer; left < width;
        ++left, baseRow += pixelSize, layerRow += pixelSize) {
        if(LayerExSaveHasDiff(baseRow, basePitch, layerRow, layerPitch,
                              height))
            break;
    }
    if(left >= width)
        return TJS_S_OK;

    for(baseRow = baseBuffer + right * pixelSize,
        layerRow = layerBuffer + right * pixelSize;
        right >= 0; --right, baseRow -= pixelSize, layerRow -= pixelSize) {
        if(LayerExSaveHasDiff(baseRow, basePitch, layerRow, layerPitch,
                              height))
            break;
    }

    const long rectWidth = right - left + 1;
    for(baseRow = baseBuffer + left * pixelSize,
        layerRow = layerBuffer + left * pixelSize;
        top < height; ++top, baseRow += basePitch, layerRow += layerPitch) {
        if(LayerExSaveHasDiff(baseRow, pixelSize, layerRow, pixelSize,
                              rectWidth))
            break;
    }
    for(baseRow = baseBuffer + left * pixelSize + bottom * basePitch,
        layerRow = layerBuffer + left * pixelSize + bottom * layerPitch;
        bottom >= 0; --bottom, baseRow -= basePitch, layerRow -= layerPitch) {
        if(LayerExSaveHasDiff(baseRow, pixelSize, layerRow, pixelSize,
                              rectWidth))
            break;
    }

    LayerExSaveMakeRect(result, left, top, rectWidth, bottom - top + 1);
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(getDiffRect, Layer, LayerExSaveGetDiffRect);

static tjs_error TJS_INTF_METHOD
LayerExSaveGetDiffPixel(tTJSVariant *result, tjs_int numparams,
                        tTJSVariant **param, iTJSDispatch2 *layer) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tjs_uint32 sameColor = 0;
    tjs_uint32 diffColor = 0;
    bool fillSame = false;
    bool fillDiff = false;
    tTVInteger count = 0;

    if(numparams >= 2 && param[1]->Type() != tvtVoid) {
        sameColor = static_cast<tjs_uint32>(param[1]->AsInteger());
        fillSame = true;
    }
    if(numparams >= 3 && param[2]->Type() != tvtVoid) {
        diffColor = static_cast<tjs_uint32>(param[2]->AsInteger());
        fillDiff = true;
    }

    iTJSDispatch2 *base = param[0]->AsObjectNoAddRef();
    LayerExSaveBuf baseLine = nullptr;
    LayerExSaveBuf baseBuffer = nullptr;
    LayerExSaveWrt layerLine = nullptr;
    LayerExSaveWrt layerBuffer = nullptr;
    long width;
    long height;
    long layerPitch;
    long baseWidth;
    long baseHeight;
    long basePitch;
    const long pixelSize = 4;

    if(!LayerExSaveGetLayerBuffer(layer, width, height, layerBuffer,
                                  layerPitch) ||
       !LayerExSaveGetLayerBuffer(base, baseWidth, baseHeight, baseBuffer,
                                  basePitch))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    if(width != baseWidth || height != baseHeight)
        TVPThrowExceptionMessage(TJS_W("Different layer size."));

    for(long y = 0; y < height;
        ++y, baseBuffer += basePitch, layerBuffer += layerPitch) {
        baseLine = baseBuffer;
        layerLine = layerBuffer;
        for(long x = 0; x < width;
            ++x, baseLine += pixelSize, layerLine += pixelSize) {
            const bool same = LayerExSaveSameColor(baseLine, layerLine);
            if(same && fillSame) {
                *reinterpret_cast<tjs_uint32 *>(layerLine) = sameColor;
            } else if(!same) {
                if(fillDiff)
                    *reinterpret_cast<tjs_uint32 *>(layerLine) = diffColor;
                ++count;
            }
        }
    }

    if(result)
        *result = count;
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(getDiffPixel, Layer, LayerExSaveGetDiffPixel);

static inline void LayerExSaveAddColor(tjs_uint32 &red, tjs_uint32 &green,
                                       tjs_uint32 &blue,
                                       LayerExSaveBuf pixel) {
    red += pixel[2];
    green += pixel[1];
    blue += pixel[0];
}

static tjs_error TJS_INTF_METHOD
LayerExSaveOozeColor(tTJSVariant *, tjs_int numparams, tTJSVariant **param,
                     iTJSDispatch2 *layer) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    const int level = static_cast<int>(param[0]->AsInteger());
    if(level <= 0)
        TVPThrowExceptionMessage(TJS_W("Invalid level count."));

    tjs_uint8 threshold =
        static_cast<tjs_uint8>(numparams > 1 ? param[1]->AsInteger() : 1);
    const tjs_uint32 fillColor =
        static_cast<tjs_uint32>(numparams > 2 ? param[2]->AsInteger() : 0);
    const tjs_uint8 fillR = static_cast<tjs_uint8>((fillColor >> 16) & 0xff);
    const tjs_uint8 fillG = static_cast<tjs_uint8>((fillColor >> 8) & 0xff);
    const tjs_uint8 fillB = static_cast<tjs_uint8>(fillColor & 0xff);
    if(threshold < 1)
        threshold = 1;

    LayerExSaveWrt buffer = nullptr;
    long width;
    long height;
    long pitch;
    const long pixelSize = 4;
    if(!LayerExSaveGetLayerBuffer(layer, width, height, buffer, pitch))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));

    const long mapWidth = width + 2;
    const long mapHeight = height + 2;
    char *oozed = new char[static_cast<size_t>(mapWidth * mapHeight)];
    char *mapTop = oozed + mapWidth + 1;
    std::memset(oozed, 0, static_cast<size_t>(mapWidth * mapHeight));

    try {
        for(long y = 0; y < height; ++y) {
            char *mark = mapTop + y * mapWidth;
            LayerExSaveWrt pixel = buffer + y * pitch;
            for(long x = 0; x < width; ++x, ++mark, pixel += pixelSize) {
                if(pixel[3] >= threshold) {
                    *mark = -1;
                } else {
                    pixel[2] = fillR;
                    pixel[1] = fillG;
                    pixel[0] = fillB;
                }
            }
        }

        for(int i = 0; i < level; ++i) {
            for(long y = 0; y < height; ++y) {
                char *mark = mapTop + y * mapWidth;
                LayerExSaveWrt pixel = buffer + y * pitch;
                for(long x = 0; x < width;
                    ++x, pixel += pixelSize, ++mark) {
                    if(*mark)
                        continue;

                    const bool up = mark[-mapWidth] < 0;
                    const bool down = mark[mapWidth] < 0;
                    const bool left = mark[-1] < 0;
                    const bool right = mark[1] < 0;
                    if(!up && !down && !left && !right)
                        continue;

                    tjs_uint32 red = 0;
                    tjs_uint32 green = 0;
                    tjs_uint32 blue = 0;
                    int count = 0;
                    if(up) {
                        LayerExSaveAddColor(red, green, blue, pixel - pitch);
                        ++count;
                    }
                    if(down) {
                        LayerExSaveAddColor(red, green, blue, pixel + pitch);
                        ++count;
                    }
                    if(left) {
                        LayerExSaveAddColor(red, green, blue,
                                            pixel - pixelSize);
                        ++count;
                    }
                    if(right) {
                        LayerExSaveAddColor(red, green, blue,
                                            pixel + pixelSize);
                        ++count;
                    }
                    pixel[2] = static_cast<tjs_uint8>(red / count);
                    pixel[1] = static_cast<tjs_uint8>(green / count);
                    pixel[0] = static_cast<tjs_uint8>(blue / count);
                    *mark = 1;
                }
            }

            for(long y = 0; y < height; ++y) {
                char *mark = mapTop + y * mapWidth;
                for(long x = 0; x < width; ++x, ++mark) {
                    if(*mark > 0)
                        *mark = -1;
                }
            }
        }
    } catch(...) {
        delete[] oozed;
        throw;
    }

    delete[] oozed;
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(oozeColor, Layer, LayerExSaveOozeColor);

static tjs_error TJS_INTF_METHOD
LayerExSaveCopyBlueToAlpha(tTJSVariant *, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *layer) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    LayerExSaveBuf src = nullptr;
    long srcWidth;
    long srcHeight;
    long srcPitch;
    if(!LayerExSaveGetLayerBuffer(param[0]->AsObjectNoAddRef(), srcWidth,
                                  srcHeight, src, srcPitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    LayerExSaveWrt dst = nullptr;
    long dstWidth;
    long dstHeight;
    long dstPitch;
    if(!LayerExSaveGetLayerBuffer(layer, dstWidth, dstHeight, dst, dstPitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));

    const long width = std::min(srcWidth, dstWidth);
    const long height = std::min(srcHeight, dstHeight);
    for(long y = 0; y < height; ++y) {
        LayerExSaveBuf srcPixel = src;
        LayerExSaveWrt dstAlpha = dst + 3;
        for(long x = 0; x < width; ++x, srcPixel += 4, dstAlpha += 4)
            *dstAlpha = *srcPixel;
        src += srcPitch;
        dst += dstPitch;
    }
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(copyBlueToAlpha, Layer, LayerExSaveCopyBlueToAlpha);

static tjs_error TJS_INTF_METHOD
LayerExSaveIsBlank(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
                   iTJSDispatch2 *layer) {
    if(numparams < 4)
        return TJS_E_BADPARAMCOUNT;

    LayerExSaveBuf buffer = nullptr;
    long imageWidth;
    long imageHeight;
    long pitch;
    if(!LayerExSaveGetLayerBuffer(layer, imageWidth, imageHeight, buffer,
                                  pitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    tjs_int left = *param[0];
    tjs_int top = *param[1];
    tjs_int width = *param[2];
    tjs_int height = *param[3];

    if(left < 0) {
        width += left;
        left = 0;
    }
    if(top < 0) {
        height += top;
        top = 0;
    }

    tjs_int cut;
    if((cut = left + width - imageWidth) > 0)
        width -= cut;
    if((cut = top + height - imageHeight) > 0)
        height -= cut;

    if(width >= 0 && height >= 0) {
        for(tjs_int y = top; y < top + height; ++y) {
            LayerExSaveBuf pixel = buffer + left * 4 + pitch * y;
            for(tjs_int x = left; x < left + width; ++x, pixel += 4) {
                if(*pixel) {
                    if(result)
                        *result = 0;
                    return TJS_S_OK;
                }
            }
        }
    }

    if(result)
        *result = 1;
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(isBlank, Layer, LayerExSaveIsBlank);

static tjs_error TJS_INTF_METHOD
LayerExSaveClearAlpha(tTJSVariant *, tjs_int numparams, tTJSVariant **param,
                      iTJSDispatch2 *layer) {
    const int threshold =
        numparams <= 0 ? 0 : static_cast<int>(param[0]->AsInteger());
    const tjs_uint32 fillColor =
        static_cast<tjs_uint32>(numparams > 1 ? param[1]->AsInteger() : 0) &
        0x00ffffffU;

    LayerExSaveWrt buffer = nullptr;
    long width;
    long height;
    long pitch;
    if(!LayerExSaveGetLayerBuffer(layer, width, height, buffer, pitch))
        TVPThrowExceptionMessage(TJS_W("dest must be Layer."));

    for(long y = 0; y < height; ++y) {
        LayerExSaveWrt pixel = buffer;
        for(long x = 0; x < width; ++x, pixel += 4) {
            if(pixel[3] <= threshold)
                *reinterpret_cast<tjs_uint32 *>(pixel) = fillColor;
        }
        buffer += pitch;
    }
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(clearAlpha, Layer, LayerExSaveClearAlpha);

static tjs_error TJS_INTF_METHOD
LayerExSaveGetAverageColor(tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *layer) {
    if(numparams < 4)
        return TJS_E_BADPARAMCOUNT;

    LayerExSaveBuf buffer = nullptr;
    long imageWidth;
    long imageHeight;
    long pitch;
    if(!LayerExSaveGetLayerBuffer(layer, imageWidth, imageHeight, buffer,
                                  pitch))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));

    tjs_int left = *param[0];
    tjs_int top = *param[1];
    tjs_int width = *param[2];
    tjs_int height = *param[3];

    if(left < 0) {
        width += left;
        left = 0;
    }
    if(top < 0) {
        height += top;
        top = 0;
    }

    tjs_int cut;
    if((cut = left + width - imageWidth) > 0)
        width -= cut;
    if((cut = top + height - imageHeight) > 0)
        height -= cut;

    if(width <= 0 || height <= 0)
        TVPThrowExceptionMessage(TJS_W("invalid layer range"));

    double a = 0.0;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    const double size = static_cast<double>(width) * height;

    for(tjs_int y = top; y < top + height; ++y) {
        LayerExSaveBuf pixel = buffer + left * 4 + pitch * y;
        for(tjs_int x = left; x < left + width; ++x, pixel += 4) {
            a += pixel[0];
            r += pixel[1];
            g += pixel[2];
            b += pixel[3];
        }
    }

    a /= size;
    r /= size;
    g /= size;
    b /= size;

    const tjs_uint32 color =
        ((static_cast<tjs_uint32>(a) & 0xffU) << 24) |
        ((static_cast<tjs_uint32>(r) & 0xffU) << 16) |
        ((static_cast<tjs_uint32>(g) & 0xffU) << 8) |
        (static_cast<tjs_uint32>(b) & 0xffU);
    if(result)
        *result = static_cast<tjs_int>(color);
    return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(getAverageColor, Layer, LayerExSaveGetAverageColor);
