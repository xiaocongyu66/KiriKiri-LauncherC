#pragma once

// Shared render/texture/color diagnostics for Android CG debug.
// Logs go through KR2RenderProbeWriteF (enabled when native file logging is on).

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
extern "C" void KR2RenderProbeWriteF(const char *fmt, ...);
#endif

namespace kr2diag {

inline const char *TexFormatName(int format) {
    // Matches TVPTextureFormat::e numeric values (Gray=1, RGB=3, RGBA=4).
    switch(format) {
        case 1:
            return "Gray";
        case 3:
            return "RGB";
        case 4:
            return "RGBA";
        case 0:
            return "None";
        default:
            return "Other";
    }
}

inline const char *GLPixFmtName(unsigned pixfmt) {
    switch(pixfmt) {
        case 0x1908:
            return "GL_RGBA";
        case 0x1907:
            return "GL_RGB";
        case 0x1909:
            return "GL_LUMINANCE";
        case 0x80E1:
            return "GL_BGRA_EXT";
        default:
            return "GL_?";
    }
}

// Krkr 32bpp pixel layout on little-endian: 0xAARRGGBB in memory as B,G,R,A
// when read as bytes, but as tjs_uint32: (A<<24)|(R<<16)|(G<<8)|B in some
// paths, or host-endian ARGB. Sample as raw uint32 and report both
// channel interpretations for color-swap diagnosis.
struct ColorSample {
    unsigned a = 0, r = 0, g = 0, b = 0;
    unsigned raw = 0;
};

inline ColorSample DecodeARGB(std::uint32_t p) {
    ColorSample c;
    c.raw = p;
    c.a = (p >> 24) & 0xff;
    c.r = (p >> 16) & 0xff;
    c.g = (p >> 8) & 0xff;
    c.b = p & 0xff;
    return c;
}

// Sample 5 points: TL TR center BL BR from a 32bpp buffer.
// pitchBytes is row stride in bytes. Returns count of valid samples written
// into out[5].
inline int Sample5RGBA(const void *bits, int width, int height, int pitchBytes,
                       ColorSample out[5]) {
    for(int i = 0; i < 5; ++i)
        out[i] = ColorSample{};
    if(!bits || width <= 0 || height <= 0 || pitchBytes < width * 4)
        return 0;
    const auto *base = static_cast<const std::uint8_t *>(bits);
    auto at = [&](int x, int y) -> ColorSample {
        if(x < 0)
            x = 0;
        if(y < 0)
            y = 0;
        if(x >= width)
            x = width - 1;
        if(y >= height)
            y = height - 1;
        const auto *row = base + static_cast<std::size_t>(y) * pitchBytes;
        std::uint32_t p = 0;
        std::memcpy(&p, row + static_cast<std::size_t>(x) * 4, 4);
        return DecodeARGB(p);
    };
    const int mx = width > 1 ? width / 2 : 0;
    const int my = height > 1 ? height / 2 : 0;
    out[0] = at(0, 0);
    out[1] = at(width - 1, 0);
    out[2] = at(mx, my);
    out[3] = at(0, height - 1);
    out[4] = at(width - 1, height - 1);
    return 5;
}

// Compact color dump: "TL=AARRGGBB TR=... C=... BL=... BR=..."
inline void Format5Colors(char *buf, std::size_t buflen, const ColorSample s[5]) {
    if(!buf || buflen == 0)
        return;
    std::snprintf(buf, buflen,
                  "TL=%02X%02X%02X%02X TR=%02X%02X%02X%02X C=%02X%02X%02X%02X "
                  "BL=%02X%02X%02X%02X BR=%02X%02X%02X%02X",
                  s[0].a, s[0].r, s[0].g, s[0].b, s[1].a, s[1].r, s[1].g,
                  s[1].b, s[2].a, s[2].r, s[2].g, s[2].b, s[3].a, s[3].r,
                  s[3].g, s[3].b, s[4].a, s[4].r, s[4].g, s[4].b);
}

// Occupancy stats over an 8x8 grid.
struct GridStats {
    unsigned samples = 0;
    unsigned clear = 0; // a==0
    unsigned semi = 0; // 0<a<255
    unsigned opaque = 0; // a==255
    unsigned rgbNZ = 0; // rgb != 0
    unsigned maxA = 0;
    unsigned minA = 255;
    // Channel means over non-clear samples (scaled 0-255).
    unsigned meanR = 0, meanG = 0, meanB = 0, meanA = 0;
};

inline GridStats SampleGridStats(const void *bits, int width, int height,
                                 int pitchBytes) {
    GridStats st;
    if(!bits || width <= 0 || height <= 0 || pitchBytes < width * 4)
        return st;
    const auto *base = static_cast<const std::uint8_t *>(bits);
    unsigned sumR = 0, sumG = 0, sumB = 0, sumA = 0, nColor = 0;
    st.minA = 255;
    for(int yi = 0; yi < 8; ++yi) {
        const int y = (height <= 1) ? 0 : (yi * (height - 1)) / 7;
        const auto *row = base + static_cast<std::size_t>(y) * pitchBytes;
        for(int xi = 0; xi < 8; ++xi) {
            const int x = (width <= 1) ? 0 : (xi * (width - 1)) / 7;
            std::uint32_t p = 0;
            std::memcpy(&p, row + static_cast<std::size_t>(x) * 4, 4);
            const unsigned a = (p >> 24) & 0xff;
            const unsigned r = (p >> 16) & 0xff;
            const unsigned g = (p >> 8) & 0xff;
            const unsigned b = p & 0xff;
            ++st.samples;
            if(a == 0)
                ++st.clear;
            else if(a == 255)
                ++st.opaque;
            else
                ++st.semi;
            if((p & 0x00ffffff) != 0)
                ++st.rgbNZ;
            if(a > st.maxA)
                st.maxA = a;
            if(a < st.minA)
                st.minA = a;
            if(a > 0) {
                sumR += r;
                sumG += g;
                sumB += b;
                sumA += a;
                ++nColor;
            }
        }
    }
    if(nColor > 0) {
        st.meanR = sumR / nColor;
        st.meanG = sumG / nColor;
        st.meanB = sumB / nColor;
        st.meanA = sumA / nColor;
    } else {
        st.minA = 0;
    }
    return st;
}

#if defined(__ANDROID__)
inline void LogTextureUpload(const char *stage, unsigned texId, int format,
                             int logicalW, int logicalH, int internalW,
                             int internalH, int pitch, int x, int y, int w,
                             int h, unsigned glFmt, const void *pixels) {
    ColorSample s[5];
    char colors[160] = "no-pixels";
    GridStats st{};
    if(pixels && w > 0 && h > 0 && pitch >= w * 4) {
        Sample5RGBA(pixels, w, h, pitch, s);
        Format5Colors(colors, sizeof(colors), s);
        st = SampleGridStats(pixels, w, h, pitch);
    }
    KR2RenderProbeWriteF(
        "[tex-upload] stage=%s glTex=%u fmt=%s(%d) glPix=%s logical=%dx%d "
        "internal=%dx%d pitch=%d rect=%d,%d,%dx%d "
        "grid(n=%u clear=%u semi=%u opaque=%u rgbNZ=%u aMin=%u aMax=%u "
        "meanRGBA=%u,%u,%u,%u) colors{%s}",
        stage ? stage : "?", texId, TexFormatName(format), format,
        GLPixFmtName(glFmt), logicalW, logicalH, internalW, internalH, pitch, x,
        y, w, h, st.samples, st.clear, st.semi, st.opaque, st.rgbNZ, st.minA,
        st.maxA, st.meanR, st.meanG, st.meanB, st.meanA, colors);
}

inline void LogImageLoad(const char *stage, const char *name, int cacheHit,
                         int viaTexture, int w, int h, int pitch, int bpp,
                         int opaque, const void *bits) {
    ColorSample s[5];
    char colors[160] = "no-bits";
    GridStats st{};
    if(bits && w > 0 && h > 0 && pitch >= w * 4 && bpp >= 32) {
        Sample5RGBA(bits, w, h, pitch, s);
        Format5Colors(colors, sizeof(colors), s);
        st = SampleGridStats(bits, w, h, pitch);
    }
    KR2RenderProbeWriteF(
        "[img-load] stage=%s name='%s' cacheHit=%d viaTex=%d size=%dx%d "
        "pitch=%d bpp=%d opaqueFlag=%d "
        "grid(n=%u clear=%u semi=%u opaque=%u rgbNZ=%u aMin=%u aMax=%u "
        "meanRGBA=%u,%u,%u,%u) colors{%s}",
        stage ? stage : "?", name ? name : "?", cacheHit, viaTexture, w, h,
        pitch, bpp, opaque, st.samples, st.clear, st.semi, st.opaque, st.rgbNZ,
        st.minA, st.maxA, st.meanR, st.meanG, st.meanB, st.meanA, colors);
}

inline void LogComposite(const char *stage, const char *layerName, int destX,
                         int destY, int srcX, int srcY, int srcW, int srcH,
                         const char *drawType, int opacity, int hda, int met,
                         const void *srcBits, int srcPitch, int srcFullW,
                         int srcFullH, const void *dstBits, int dstPitch,
                         int dstFullW, int dstFullH) {
    ColorSample ss[5], ds[5];
    char sc[160] = "no-src", dc[160] = "no-dst";
    if(srcBits && srcW > 0 && srcH > 0 && srcPitch >= srcFullW * 4) {
        // Sample from full source bitmap corners of the blit rect origin.
        Sample5RGBA(static_cast<const std::uint8_t *>(srcBits) +
                        static_cast<std::size_t>(srcY) * srcPitch +
                        static_cast<std::size_t>(srcX) * 4,
                    srcW, srcH, srcPitch, ss);
        Format5Colors(sc, sizeof(sc), ss);
    }
    if(dstBits && dstFullW > 0 && dstFullH > 0 && dstPitch >= dstFullW * 4) {
        Sample5RGBA(dstBits, dstFullW, dstFullH, dstPitch, ds);
        Format5Colors(dc, sizeof(dc), ds);
    }
    KR2RenderProbeWriteF(
        "[composite] stage=%s layer='%s' dest=%d,%d src=%d,%d,%dx%d "
        "type=%s opa=%d hda=%d met=%d srcFull=%dx%d dstFull=%dx%d "
        "srcColors{%s} dstColors{%s}",
        stage ? stage : "?", layerName ? layerName : "?", destX, destY, srcX,
        srcY, srcW, srcH, drawType ? drawType : "?", opacity, hda, met,
        srcFullW, srcFullH, dstFullW, dstFullH, sc, dc);
}

inline void LogPresent(const char *stage, unsigned nativeGL, int format,
                       int texW, int texH, int intW, int intH, float uvU,
                       float uvV, int flipY, int softwareUpload, int fullFrame,
                       const void *bits, int pitch) {
    ColorSample s[5];
    char colors[160] = "native-gl-no-cpu-bits";
    GridStats st{};
    if(bits && texW > 0 && texH > 0 && pitch >= texW * 4) {
        Sample5RGBA(bits, texW, texH, pitch, s);
        Format5Colors(colors, sizeof(colors), s);
        st = SampleGridStats(bits, texW, texH, pitch);
    }
    KR2RenderProbeWriteF(
        "[present-color] stage=%s nativeGL=%u fmt=%s tex=%dx%d internal=%dx%d "
        "uv=%.4f,%.4f flipY=%d softwareUpload=%d fullFrame=%d "
        "grid(n=%u clear=%u semi=%u opaque=%u rgbNZ=%u meanRGBA=%u,%u,%u,%u) "
        "colors{%s}",
        stage ? stage : "?", nativeGL, TexFormatName(format), texW, texH, intW,
        intH, uvU, uvV, flipY, softwareUpload, fullFrame, st.samples, st.clear,
        st.semi, st.opaque, st.rgbNZ, st.meanR, st.meanG, st.meanB, st.meanA,
        colors);
}

// Texture identity / metadata (no pixel sample).
inline void LogTexture(const char *stage, unsigned texId, int format,
                       int logicalW, int logicalH, int internalW,
                       int internalH, int pitch, int opaque, int isStatic,
                       const void *bits) {
    ColorSample s[5];
    char colors[160] = "no-bits";
    GridStats st{};
    if(bits && logicalW > 0 && logicalH > 0 && pitch >= logicalW * 4) {
        Sample5RGBA(bits, logicalW, logicalH, pitch, s);
        Format5Colors(colors, sizeof(colors), s);
        st = SampleGridStats(bits, logicalW, logicalH, pitch);
    }
    KR2RenderProbeWriteF(
        "[texture] stage=%s glTex=%u fmt=%s(%d) logical=%dx%d internal=%dx%d "
        "pitch=%d opaque=%d static=%d "
        "grid(n=%u clear=%u semi=%u opaque=%u rgbNZ=%u aMin=%u aMax=%u "
        "meanRGBA=%u,%u,%u,%u) colors{%s}",
        stage ? stage : "?", texId, TexFormatName(format), format, logicalW,
        logicalH, internalW, internalH, pitch, opaque, isStatic, st.samples,
        st.clear, st.semi, st.opaque, st.rgbNZ, st.minA, st.maxA, st.meanR,
        st.meanG, st.meanB, st.meanA, colors);
}

// Color-only dump for any buffer (merge result, temp layer, etc.).
inline void LogColorBuffer(const char *stage, const char *name, int w, int h,
                           int pitch, const void *bits) {
    ColorSample s[5];
    char colors[160] = "no-bits";
    GridStats st{};
    if(bits && w > 0 && h > 0 && pitch >= w * 4) {
        Sample5RGBA(bits, w, h, pitch, s);
        Format5Colors(colors, sizeof(colors), s);
        st = SampleGridStats(bits, w, h, pitch);
    }
    KR2RenderProbeWriteF(
        "[color] stage=%s name='%s' size=%dx%d pitch=%d "
        "grid(n=%u clear=%u semi=%u opaque=%u rgbNZ=%u aMin=%u aMax=%u "
        "meanRGBA=%u,%u,%u,%u) colors{%s}",
        stage ? stage : "?", name ? name : "?", w, h, pitch, st.samples,
        st.clear, st.semi, st.opaque, st.rgbNZ, st.minA, st.maxA, st.meanR,
        st.meanG, st.meanB, st.meanA, colors);
}
#endif // __ANDROID__

} // namespace kr2diag
