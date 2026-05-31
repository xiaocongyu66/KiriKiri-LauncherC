#include "ncbind.hpp"

#include <cstdlib>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

#define NCB_MODULE_NAME TJS_W("shrinkCopy.dll")

struct ShrinkCopyLayerUtils {
    typedef tjs_uint32 PixelT;
    typedef tjs_uint8 UnitT;
    typedef const UnitT *BufRefT;
    typedef UnitT *WrtRefT;
    typedef tTVReal RealT;

    static bool IsValidLayer(iTJSDispatch2 *layer) {
        if(!layer ||
           TJS_FAILED(layer->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"),
                                          layer)))
            return false;

        tTJSVariant value;
        if(TJS_FAILED(layer->PropGet(0, TJS_W("hasImage"), nullptr, &value,
                                     layer)) ||
           value.AsInteger() == 0)
            return false;
        return true;
    }

    static bool GetLayerSize(iTJSDispatch2 *layer, long &width, long &height,
                             long &pitch) {
        if(!IsValidLayer(layer))
            return false;

        tTJSVariant value;
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
        if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBufferPitch"),
                                     nullptr, &value, layer)))
            return false;
        pitch = static_cast<long>(value.AsInteger());

        return width > 0 && height > 0 && pitch != 0;
    }

    static bool GetLayerBufferAndSize(iTJSDispatch2 *layer, long &width,
                                      long &height, BufRefT &buffer,
                                      long &pitch) {
        if(!GetLayerSize(layer, width, height, pitch))
            return false;

        tTJSVariant value;
        if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBuffer"), nullptr,
                                     &value, layer)))
            return false;
        buffer = reinterpret_cast<BufRefT>(
            static_cast<tjs_intptr_t>(value.AsInteger()));
        return buffer != nullptr;
    }

    static bool GetLayerBufferAndSize(iTJSDispatch2 *layer, long &width,
                                      long &height, WrtRefT &buffer,
                                      long &pitch) {
        if(!GetLayerSize(layer, width, height, pitch))
            return false;

        tTJSVariant value;
        if(TJS_FAILED(layer->PropGet(0, TJS_W("mainImageBufferForWrite"),
                                     nullptr, &value, layer)))
            return false;
        buffer = reinterpret_cast<WrtRefT>(
            static_cast<tjs_intptr_t>(value.AsInteger()));
        return buffer != nullptr;
    }
};

struct ShrinkCopy : public ShrinkCopyLayerUtils {
    static tjs_error TJS_INTF_METHOD layerShrinkCopy(tTJSVariant *, tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *dst) {
        if(numparams < 9)
            return TJS_E_BADPARAMCOUNT;

        ShrinkCopy instance(
            dst, param[0]->AsReal(), param[1]->AsReal(), param[2]->AsReal(),
            param[3]->AsReal(), param[4]->AsObjectNoAddRef(),
            static_cast<long>(param[5]->AsInteger()),
            static_cast<long>(param[6]->AsInteger()),
            static_cast<long>(param[7]->AsInteger()),
            static_cast<long>(param[8]->AsInteger()));

        if(!instance.check())
            return TJS_E_INVALIDPARAM;
        if(!instance.clip())
            return TJS_S_OK;

        instance.copy();
        return TJS_S_OK;
    }

    ShrinkCopy(iTJSDispatch2 *dstLayer, RealT dstX, RealT dstY, RealT dstW,
               RealT dstH, iTJSDispatch2 *srcLayer, long srcX, long srcY,
               long srcW, long srcH) :
        dst(dstLayer),
        dx(dstX),
        dy(dstY),
        dw(dstW),
        dh(dstH),
        src(srcLayer),
        sx(srcX),
        sy(srcY),
        sw(srcW),
        sh(srcH),
        ps(nullptr),
        siw(0),
        sih(0),
        spch(0),
        pd(nullptr),
        diw(0),
        dih(0),
        dpch(0) {}

    bool check() {
        if(sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 ||
           sw < static_cast<long>(dw) || sh < static_cast<long>(dh))
            return false;
        return GetLayerBufferAndSize(src, siw, sih, ps, spch) &&
               GetLayerBufferAndSize(dst, diw, dih, pd, dpch);
    }

    static inline long RealToLong(RealT value) {
        return value < 0 ? -static_cast<long>(-value)
                         : static_cast<long>(value);
    }

    bool clip() {
        const RealT zoomX = dw / static_cast<RealT>(sw);
        const RealT zoomY = dh / static_cast<RealT>(sh);
        RealT dstCut;
        long srcCut;

        if(sx + sw <= 0 || sy + sh <= 0 || sx >= siw || sy >= sih)
            return false;

        if(sx < 0) {
            sw += sx;
            dw -= (dstCut = zoomX * static_cast<RealT>(-sx));
            sx = 0;
            dx += dstCut;
        }
        if(sy < 0) {
            sh += sy;
            dh -= (dstCut = zoomY * static_cast<RealT>(-sy));
            sy = 0;
            dy += dstCut;
        }
        if((srcCut = sx + sw - siw) > 0) {
            sw -= srcCut;
            dw -= zoomX * static_cast<RealT>(srcCut);
        }
        if((srcCut = sy + sh - sih) > 0) {
            sh -= srcCut;
            dh -= zoomY * static_cast<RealT>(srcCut);
        }

        dtx = RealToLong(dx);
        dty = RealToLong(dy);
        dtw = RealToLong(dx + dw) - dtx;
        dth = RealToLong(dy + dh) - dty;
        if(dx + dw > static_cast<RealT>(dtx + dtw))
            ++dtw;
        if(dy + dh > static_cast<RealT>(dty + dth))
            ++dth;

        if(dtx + dtw <= 0 || dty + dth <= 0 || dtx >= diw || dty >= dih)
            return false;

        dsx = dtx < 0 ? -dtx : 0;
        dsy = dty < 0 ? -dty : 0;
        dex = dtx + dtw > diw ? diw - dtx : dtw;
        dey = dty + dth > dih ? dih - dty : dth;
        return true;
    }

    typedef tjs_uint32 AvgT;
    struct AvgInfoT {
        int offset;
        int step;
        AvgT ta;
        AvgT tc;
        AvgT ba;
        AvgT bc;
        AvgT total;
    };
    struct ElementT {
        AvgT r;
        AvgT g;
        AvgT b;
        AvgT a;
    };

    void copy() {
        AvgInfoT *horizontal = nullptr;
        AvgInfoT *vertical = nullptr;
        void *buffer = allocAvgBuffer(horizontal, vertical, dex - dsx,
                                      dey - dsy);
        if(!horizontal || !vertical)
            return;

        const AvgT horizontalUnit = makeAvgTable(horizontal, true);
        const AvgT verticalUnit = makeAvgTable(vertical, false);
        const AvgT unit = horizontalUnit * verticalUnit;

        WrtRefT out;
        WrtRefT outLine = pd + ((dtx + dsx) << 2) + ((dty + dsy) * dpch);
        const AvgInfoT *verticalInfo = vertical;
        for(long y = dsy; y < dey; ++y, ++verticalInfo, outLine += dpch) {
            BufRefT inLine = ps + verticalInfo->offset;
            const AvgInfoT *horizontalInfo = horizontal;
            out = outLine;
            for(long x = dsx; x < dex; ++x, ++horizontalInfo, out += 4) {
                ElementT sum = { 0, 0, 0, 0 };
                BufRefT in = inLine + horizontalInfo->offset;
                const int w = horizontalInfo->step;
                const int h = verticalInfo->step;
                const int ox = w << 2;
                const int oy = h * spch;

                addPoint(sum, in - 4 - spch,
                         horizontalInfo->ta * verticalInfo->ta,
                         horizontalInfo->tc * verticalInfo->tc);
                addHorz(sum, in - spch, w, horizontalUnit * verticalInfo->ta,
                        horizontalUnit * verticalInfo->tc);
                addPoint(sum, in + ox - spch,
                         horizontalInfo->ba * verticalInfo->ta,
                         horizontalInfo->bc * verticalInfo->tc);
                addVert(sum, in - 4, h, horizontalInfo->ta * verticalUnit,
                        horizontalInfo->tc * verticalUnit);
                addRect(sum, in, w, h, unit);
                addVert(sum, in + ox, h, horizontalInfo->ba * verticalUnit,
                        horizontalInfo->bc * verticalUnit);
                addPoint(sum, in - 4 + oy,
                         horizontalInfo->ta * verticalInfo->ba,
                         horizontalInfo->tc * verticalInfo->bc);
                addHorz(sum, in + oy, w, horizontalUnit * verticalInfo->ba,
                        horizontalUnit * verticalInfo->bc);
                addPoint(sum, in + ox + oy,
                         horizontalInfo->ba * verticalInfo->ba,
                         horizontalInfo->bc * verticalInfo->bc);

                const AvgT div = horizontalInfo->total * verticalInfo->total;
                out[0] = static_cast<UnitT>(sum.r / div);
                out[1] = static_cast<UnitT>(sum.g / div);
                out[2] = static_cast<UnitT>(sum.b / div);
                out[3] = static_cast<UnitT>(sum.a / div);
            }
        }

        freeAvgBuffer(buffer);
    }

    struct MakeAvgWorkT {
        AvgT unit;
        RealT max;
        RealT ratio;
        RealT diff;
        long stop;
        long ofmul;
    };

    AvgT makeAvgTable(AvgInfoT *table, bool horizontal) {
        MakeAvgWorkT work;
        long pos;
        long end;

        if(horizontal) {
            pos = dsx;
            end = dex - 1;
            work.max = static_cast<RealT>(sw);
            work.ratio = work.max / dw;
            work.diff = dx - static_cast<RealT>(dtx);
            work.stop = sx;
            work.ofmul = 4;
        } else {
            pos = dsy;
            end = dey - 1;
            work.max = static_cast<RealT>(sh);
            work.ratio = work.max / dh;
            work.diff = dy - static_cast<RealT>(dty);
            work.stop = sy;
            work.ofmul = spch;
        }

        work.unit = 256;
        if(work.ratio <= 1.0 / 16) {
            work.unit /= static_cast<int>((2.0 / 16.0) / work.ratio);
            if(work.unit <= 0)
                work.unit = 1;
        }

        if(pos == end) {
            setAvgInfoEdge(work, table, pos);
        } else {
            setAvgInfoEdge(work, table++, pos++);
            while(pos < end)
                setAvgInfo(work, table++, pos++);
            setAvgInfoEdge(work, table++, pos++);
        }
        return work.unit;
    }

    void setAvgInfo(const MakeAvgWorkT &work, AvgInfoT *table, long pos) {
        const AvgT unit = work.unit;
        RealT r1 = (static_cast<RealT>(pos) - work.diff) *
                   (work.ratio);
        RealT r2 = work.ratio + r1;
        const long t1 = RealToLong(r1);
        const long t2 = RealToLong(r2);
        table->offset = static_cast<int>((work.stop + t1 + 1) * work.ofmul);
        table->ta = table->tc =
            unit - RealToLong((r1 - static_cast<RealT>(t1)) * unit);
        table->ba = table->bc =
            RealToLong((r2 - static_cast<RealT>(t2)) * unit);
        table->step = static_cast<int>(t2 - t1 - 1);
        table->total = table->ta + table->ba + table->step * unit;
    }

    void setAvgInfoEdge(const MakeAvgWorkT &work, AvgInfoT *table, long pos) {
        const AvgT unit = work.unit;
        RealT r1 = (static_cast<RealT>(pos) - work.diff) *
                   (work.ratio);
        RealT r2 = work.ratio + r1;
        RealT f1 = r1;
        RealT f2 = r2;
        if(f1 < 0)
            f1 = 0;
        if(f2 > work.max)
            f2 = work.max;

        const long t1 = RealToLong(r1);
        const long t2 = RealToLong(r2);
        const long u1 = RealToLong(f1);
        const long u2 = RealToLong(f2);
        table->offset = static_cast<int>((work.stop + u1 + 1) * work.ofmul);
        table->tc = unit - RealToLong((r1 - static_cast<RealT>(t1)) * unit);
        table->bc = RealToLong((r2 - static_cast<RealT>(t2)) * unit);
        table->ta = unit - RealToLong((f1 - static_cast<RealT>(u1)) * unit);
        table->ba = RealToLong((f2 - static_cast<RealT>(u2)) * unit);
        table->step = static_cast<int>(u2 - u1 - 1);
        table->total = table->tc + table->bc + (t2 - t1 - 1) * unit;
    }

    void *allocAvgBuffer(AvgInfoT *&horizontal, AvgInfoT *&vertical, long w,
                         long h) {
        void *ret =
            std::malloc(sizeof(AvgInfoT) * static_cast<size_t>(w + h));
        horizontal = static_cast<AvgInfoT *>(ret);
        vertical = horizontal ? horizontal + w : nullptr;
        return ret;
    }

    void freeAvgBuffer(void *buffer) {
        if(buffer)
            std::free(buffer);
    }

    void addRect(ElementT &sum, BufRefT pixel, int w, int h, AvgT mul) {
        if(!mul)
            return;
        for(; h > 0; --h, pixel += spch) {
            const PixelT *row = reinterpret_cast<const PixelT *>(pixel);
            for(int n = w; n > 0; --n, ++row) {
                const PixelT color = *row;
                sum.r += (color & 0xffU) * mul;
                sum.g += ((color >> 8) & 0xffU) * mul;
                sum.b += ((color >> 16) & 0xffU) * mul;
                sum.a += (color >> 24) * mul;
            }
        }
    }

    void addVert(ElementT &sum, BufRefT pixel, int h, AvgT mulA,
                 AvgT mulRgb) {
        if(!mulA)
            return;
        for(; h > 0; --h, pixel += spch) {
            const PixelT color = *reinterpret_cast<const PixelT *>(pixel);
            sum.r += (color & 0xffU) * mulRgb;
            sum.g += ((color >> 8) & 0xffU) * mulRgb;
            sum.b += ((color >> 16) & 0xffU) * mulRgb;
            sum.a += (color >> 24) * mulA;
        }
    }

    void addHorz(ElementT &sum, BufRefT pixel, int w, AvgT mulA,
                 AvgT mulRgb) {
        if(!mulA)
            return;
        const PixelT *row = reinterpret_cast<const PixelT *>(pixel);
        for(; w > 0; --w, ++row) {
            const PixelT color = *row;
            sum.r += (color & 0xffU) * mulRgb;
            sum.g += ((color >> 8) & 0xffU) * mulRgb;
            sum.b += ((color >> 16) & 0xffU) * mulRgb;
            sum.a += (color >> 24) * mulA;
        }
    }

    void addPoint(ElementT &sum, BufRefT pixel, AvgT mulA, AvgT mulRgb) {
        if(!mulA)
            return;
        const PixelT color = *reinterpret_cast<const PixelT *>(pixel);
        sum.r += (color & 0xffU) * mulRgb;
        sum.g += ((color >> 8) & 0xffU) * mulRgb;
        sum.b += ((color >> 16) & 0xffU) * mulRgb;
        sum.a += (color >> 24) * mulA;
    }

    iTJSDispatch2 *dst;
    RealT dx;
    RealT dy;
    RealT dw;
    RealT dh;
    long dtx;
    long dty;
    long dtw;
    long dth;
    long dsx;
    long dsy;
    long dex;
    long dey;

    iTJSDispatch2 *src;
    long sx;
    long sy;
    long sw;
    long sh;

    BufRefT ps;
    long siw;
    long sih;
    long spch;

    WrtRefT pd;
    long diw;
    long dih;
    long dpch;
};
NCB_ATTACH_FUNCTION(shrinkCopy, Layer, ShrinkCopy::layerShrinkCopy);

struct LimitedShrink : public ShrinkCopyLayerUtils {
    static tjs_error TJS_INTF_METHOD layerShrinkCopy(tTJSVariant *, tjs_int numparams,
                                                     tTJSVariant **param,
                                                     iTJSDispatch2 *dst) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;

        LimitedShrink instance(
            dst, param[0]->AsObjectNoAddRef(),
            static_cast<long>(param[1]->AsInteger()),
            numparams >= 3 ? static_cast<long>(param[2]->AsInteger()) : 0);

        if(!instance.check())
            return TJS_E_INVALIDPARAM;
        if(!instance.resize())
            return TJS_E_FAIL;

        instance.copy();
        return TJS_S_OK;
    }

    LimitedShrink(iTJSDispatch2 *dstLayer, iTJSDispatch2 *srcLayer,
                  long stepX, long stepY) :
        dst(dstLayer),
        src(srcLayer),
        stepx(stepX),
        stepy(stepY),
        xdiv(0),
        xrem(0),
        ps(nullptr),
        siw(0),
        sih(0),
        spch(0),
        pd(nullptr),
        diw(0),
        dih(0),
        dpch(0) {
        if(!stepy)
            stepy = stepx;
    }

    bool check() {
        return stepx > 0 && stepy > 0 &&
               GetLayerBufferAndSize(src, siw, sih, ps, spch) &&
               IsValidLayer(dst);
    }

    bool resize() {
        tTJSVariant newWidth(static_cast<tjs_int>((siw + stepx - 1) / stepx));
        tTJSVariant newHeight(static_cast<tjs_int>((sih + stepy - 1) / stepy));
        tTJSVariant *params[] = { &newWidth, &newHeight };
        return TJS_SUCCEEDED(dst->FuncCall(0, TJS_W("setImageSize"), nullptr,
                                           nullptr, 2, params, dst)) &&
               GetLayerBufferAndSize(dst, diw, dih, pd, dpch);
    }

    void copy() {
        xdiv = siw / stepx;
        xrem = siw - xdiv * stepx;

        if(stepy <= 1) {
            for(long y = 0; y < sih; ++y, pd += dpch, ps += spch)
                shrinkLineX(pd, ps);
            return;
        }

        const long bufferPitch = diw * 4;
        WrtRefT buffer = new UnitT[static_cast<size_t>(bufferPitch * stepy)];
        try {
            long div = sih / stepy;
            for(long len = div; len > 0; --len, pd += dpch) {
                for(long sub = stepy, offset = 0; sub > 0;
                    --sub, offset += bufferPitch, ps += spch) {
                    shrinkLineX(buffer + offset, ps);
                }
                shrinkLineY(pd, buffer, bufferPitch, stepy);
            }

            div *= stepy;
            if(div < sih) {
                const long yrem = sih - div;
                for(long sub = yrem, offset = 0; sub > 0;
                    --sub, offset += bufferPitch, ps += spch) {
                    shrinkLineX(buffer + offset, ps);
                }
                shrinkLineY(pd, buffer, bufferPitch, yrem);
            }
        } catch(...) {
            delete[] buffer;
            throw;
        }
        delete[] buffer;
    }

    static void ShrinkLine(WrtRefT &write, BufRefT &read, long len,
                           long shrink, long step, long tstep) {
        BufRefT tmp = read;
        switch(shrink) {
        case 1:
            for(; len > 0; --len, read += step) {
                *write++ = read[0];
                *write++ = read[1];
                *write++ = read[2];
                *write++ = 255;
            }
            break;
        default:
            for(; len > 0; --len, read += step) {
                PixelT r = 0;
                PixelT g = 0;
                PixelT b = 0;
                tmp = read;
                for(long sub = shrink; sub > 0; --sub, tmp += tstep) {
                    r += static_cast<PixelT>(tmp[0]);
                    g += static_cast<PixelT>(tmp[1]);
                    b += static_cast<PixelT>(tmp[2]);
                }
                *write++ = static_cast<UnitT>(r / shrink);
                *write++ = static_cast<UnitT>(g / shrink);
                *write++ = static_cast<UnitT>(b / shrink);
                *write++ = 255;
            }
            break;
        }
    }

    void shrinkLineX(WrtRefT write, BufRefT read) {
        ShrinkLine(write, read, xdiv, stepx, stepx * 4, 4);
        if(xrem > 0)
            ShrinkLine(write, read, 1, xrem, 0, 4);
    }

    void shrinkLineY(WrtRefT write, BufRefT read, long pitch, long shrink) {
        ShrinkLine(write, read, diw, shrink, 4, pitch);
    }

    iTJSDispatch2 *dst;
    iTJSDispatch2 *src;
    long stepx;
    long stepy;
    long xdiv;
    long xrem;

    BufRefT ps;
    long siw;
    long sih;
    long spch;

    WrtRefT pd;
    long diw;
    long dih;
    long dpch;
};
NCB_ATTACH_FUNCTION(shrinkCopyFast, Layer, LimitedShrink::layerShrinkCopy);
