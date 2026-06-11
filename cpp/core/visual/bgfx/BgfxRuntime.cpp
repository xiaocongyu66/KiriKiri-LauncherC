#include "BgfxRuntime.h"

#include "DebugIntf.h"

#if defined(KIRIKIRI_HAS_BGFX)
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace TVPBgfx {
namespace {

std::mutex RuntimeMutex;
void *NativeWindow = nullptr;
bool Ready = false;
bool Requested = false;
uint32_t BackbufferWidth = 1;
uint32_t BackbufferHeight = 1;
uint32_t TextureUploadCount = 0;
uint32_t TextureUploadSkipCount = 0;
bool LoggedIntermediateTextureUploadDisabled = false;

#if defined(KIRIKIRI_HAS_BGFX)
constexpr uint16_t InvalidTextureHandle = bgfx::kInvalidHandle;
#else
constexpr uint16_t InvalidTextureHandle = UINT16_MAX;
#endif

uint32_t FrameUploadCount = 0;
uint16_t SoftwareFrameTexture = InvalidTextureHandle;
uint32_t SoftwareFrameWidth = 0;
uint32_t SoftwareFrameHeight = 0;
uint32_t RectBatchCount = 0;
uint32_t TriangleBatchCount = 0;

struct tTVPBgfxVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

std::vector<tTVPBgfxVertex> StagedTriangleVertices;
std::array<tTVPBgfxVertex, 6> StagedRectVertices;
std::array<tTVPBgfxVertex, 6> SoftwareFrameQuad;

#if defined(KIRIKIRI_HAS_BGFX)
bgfx::VertexLayout ScreenVertexLayout;
#endif

bool CopyAsRgba8(std::vector<uint8_t> &out, uint32_t width, uint32_t height,
                 const void *pixel, int pitch, int format) {
    if(!pixel || !width || !height || pitch <= 0)
        return false;

    const auto *source = static_cast<const uint8_t *>(pixel);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    out.resize(pixelCount * 4);

    switch(format) {
    case 4:
        for(uint32_t y = 0; y < height; ++y) {
            std::memcpy(out.data() + static_cast<size_t>(y) * width * 4,
                        source + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(width) * 4);
        }
        return true;
    case 3:
        for(uint32_t y = 0; y < height; ++y) {
            const uint8_t *line = source + static_cast<size_t>(y) * pitch;
            uint8_t *dest = out.data() + static_cast<size_t>(y) * width * 4;
            for(uint32_t x = 0; x < width; ++x) {
                dest[x * 4 + 0] = line[x * 3 + 0];
                dest[x * 4 + 1] = line[x * 3 + 1];
                dest[x * 4 + 2] = line[x * 3 + 2];
                dest[x * 4 + 3] = 0xff;
            }
        }
        return true;
    case 1:
        for(uint32_t y = 0; y < height; ++y) {
            const uint8_t *line = source + static_cast<size_t>(y) * pitch;
            uint8_t *dest = out.data() + static_cast<size_t>(y) * width * 4;
            for(uint32_t x = 0; x < width; ++x) {
                const uint8_t gray = line[x];
                dest[x * 4 + 0] = gray;
                dest[x * 4 + 1] = gray;
                dest[x * 4 + 2] = gray;
                dest[x * 4 + 3] = 0xff;
            }
        }
        return true;
    default:
        return false;
    }
}

bool ShouldStageTextureUpload(uint32_t width, uint32_t height) {
    if(!width || !height)
        return false;

    ++TextureUploadSkipCount;
    if(!LoggedIntermediateTextureUploadDisabled) {
        LoggedIntermediateTextureUploadDisabled = true;
        TVPAddLog(TJS_W("[renderer] bgfx intermediate texture upload disabled while software compositing is active; using final frame upload only."));
    }
    if(TextureUploadSkipCount <= 8 || TextureUploadSkipCount == 16 ||
       TextureUploadSkipCount == 32 || (TextureUploadSkipCount % 512) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx intermediate texture upload skipped #") +
                  ttstr(static_cast<int>(TextureUploadSkipCount)) + TJS_W(" ") +
                  ttstr(static_cast<int>(width)) + TJS_W("x") +
                  ttstr(static_cast<int>(height)));
    }
    return false;
}

bool InitializeVulkanLocked(uint32_t width, uint32_t height) {
    if(Ready)
        return true;

#if defined(KIRIKIRI_HAS_BGFX)
    if(!NativeWindow) {
        TVPAddLog(TJS_W("[renderer] bgfx Vulkan native window is not ready; delegating to the software path."));
        return false;
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::Vulkan;
    init.resolution.width = width ? width : BackbufferWidth;
    init.resolution.height = height ? height : BackbufferHeight;
    init.resolution.reset = BGFX_RESET_NONE;
    init.platformData.nwh = NativeWindow;

    Ready = bgfx::init(init);
    if(Ready) {
        ScreenVertexLayout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    }
    TVPAddLog(Ready
                  ? TJS_W("[renderer] bgfx Vulkan runtime initialized; TVP compositing migration is staged and currently delegates to the software path.")
                  : TJS_W("[renderer] bgfx Vulkan runtime initialization failed; delegating to the software path."));
    return Ready;
#else
    TVPAddLog(TJS_W("[renderer] bgfx Vulkan renderer selected; bgfx runtime is not compiled in and compositing delegates to the software path."));
    return false;
#endif
}

void StageSoftwareFrameQuadLocked(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    SoftwareFrameQuad = {
        tTVPBgfxVertex{ -1.0f, -1.0f, 0.0f, 0.0f },
        tTVPBgfxVertex{ 1.0f, -1.0f, 1.0f, 0.0f },
        tTVPBgfxVertex{ -1.0f, 1.0f, 0.0f, 1.0f },
        tTVPBgfxVertex{ 1.0f, -1.0f, 1.0f, 0.0f },
        tTVPBgfxVertex{ -1.0f, 1.0f, 0.0f, 1.0f },
        tTVPBgfxVertex{ 1.0f, 1.0f, 1.0f, 1.0f },
    };
}

} // namespace

void SetNativeWindow(void *nativeWindow) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    NativeWindow = nativeWindow;
    if(NativeWindow && Requested && !Ready)
        InitializeVulkanLocked(BackbufferWidth, BackbufferHeight);
}

void SetBackbufferSize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    BackbufferWidth = width ? width : 1;
    BackbufferHeight = height ? height : 1;
#if defined(KIRIKIRI_HAS_BGFX)
    if(Ready)
        bgfx::reset(BackbufferWidth, BackbufferHeight, BGFX_RESET_NONE);
#endif
}

bool InitializeVulkan(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    Requested = true;
    return InitializeVulkanLocked(width, height);
}

bool IsReady() {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    return Ready;
}

uint16_t CreateTexture2D(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
#if defined(KIRIKIRI_HAS_BGFX)
    if(!Ready)
        return InvalidTextureHandle;
    if(!ShouldStageTextureUpload(width, height))
        return InvalidTextureHandle;
    std::vector<uint8_t> rgba;
    if(!CopyAsRgba8(rgba, width, height, pixel, pitch, format))
        return InvalidTextureHandle;
    const bgfx::Memory *memory = bgfx::copy(rgba.data(),
                                           static_cast<uint32_t>(rgba.size()));
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
        static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)), false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, memory);
    if(bgfx::isValid(handle)) {
        ++TextureUploadCount;
        if(TextureUploadCount <= 8 || (TextureUploadCount % 256) == 0) {
            TVPAddLog(TJS_W("[renderer] bgfx texture upload #") +
                      ttstr(static_cast<int>(TextureUploadCount)) + TJS_W(" ") +
                      ttstr(static_cast<int>(width)) + TJS_W("x") +
                      ttstr(static_cast<int>(height)));
        }
        return handle.idx;
    }
#endif
    return InvalidTextureHandle;
}

void UpdateTexture2D(uint16_t handle, uint32_t width, uint32_t height,
                     const void *pixel, int pitch, int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    bgfx::TextureHandle texture{handle};
    if(!Ready || !bgfx::isValid(texture))
        return;
    if(!ShouldStageTextureUpload(width, height))
        return;
    std::vector<uint8_t> rgba;
    if(!CopyAsRgba8(rgba, width, height, pixel, pitch, format))
        return;
    const bgfx::Memory *memory = bgfx::copy(rgba.data(),
                                           static_cast<uint32_t>(rgba.size()));
    bgfx::updateTexture2D(texture, 0, 0, 0, 0,
                          static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
                          static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)),
                          memory);
#endif
}

void DestroyTexture2D(uint16_t handle) {
#if defined(KIRIKIRI_HAS_BGFX)
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    bgfx::TextureHandle texture{handle};
    if(Ready && bgfx::isValid(texture))
        bgfx::destroy(texture);
#endif
}

void UploadSoftwareFrame(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    if(!Ready || !pixel || !width || !height || pitch <= 0)
        return;

    std::vector<uint8_t> rgba;
    if(!CopyAsRgba8(rgba, width, height, pixel, pitch, format))
        return;

    if(SoftwareFrameTexture == InvalidTextureHandle ||
       SoftwareFrameWidth != width || SoftwareFrameHeight != height) {
        if(SoftwareFrameTexture != InvalidTextureHandle) {
            bgfx::TextureHandle oldTexture{SoftwareFrameTexture};
            if(bgfx::isValid(oldTexture))
                bgfx::destroy(oldTexture);
            SoftwareFrameTexture = InvalidTextureHandle;
        }

        const bgfx::Memory *memory = bgfx::copy(
            rgba.data(), static_cast<uint32_t>(rgba.size()));
        bgfx::TextureHandle texture = bgfx::createTexture2D(
            static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
            static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)),
            false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE, memory);
        if(!bgfx::isValid(texture))
            return;
        SoftwareFrameTexture = texture.idx;
        SoftwareFrameWidth = width;
        SoftwareFrameHeight = height;
    } else {
        bgfx::TextureHandle texture{SoftwareFrameTexture};
        if(!bgfx::isValid(texture))
            return;
        const bgfx::Memory *memory = bgfx::copy(
            rgba.data(), static_cast<uint32_t>(rgba.size()));
        bgfx::updateTexture2D(
            texture, 0, 0, 0, 0,
            static_cast<uint16_t>(std::min<uint32_t>(width, UINT16_MAX)),
            static_cast<uint16_t>(std::min<uint32_t>(height, UINT16_MAX)),
            memory);
    }

    StageSoftwareFrameQuadLocked(width, height);

    bgfx::touch(0);
    bgfx::frame();

    ++FrameUploadCount;
    if(FrameUploadCount <= 8 || FrameUploadCount == 16 ||
       FrameUploadCount == 32 || (FrameUploadCount % 256) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx software frame upload #") +
                  ttstr(static_cast<int>(FrameUploadCount)) + TJS_W(" ") +
                  ttstr(static_cast<int>(width)) + TJS_W("x") +
                  ttstr(static_cast<int>(height)));
    }
#else
    (void)width;
    (void)height;
    (void)pixel;
    (void)pitch;
    (void)format;
#endif
}

void StageRectBatch(const char *methodName, int targetLeft, int targetTop,
                    int targetWidth, int targetHeight, uint32_t textureCount) {
    if(targetWidth <= 0 || targetHeight <= 0)
        return;

    std::lock_guard<std::mutex> lock(RuntimeMutex);
    StagedRectVertices = {
        tTVPBgfxVertex{ -1.0f, -1.0f, 0.0f, 0.0f },
        tTVPBgfxVertex{ 1.0f, -1.0f, 1.0f, 0.0f },
        tTVPBgfxVertex{ -1.0f, 1.0f, 0.0f, 1.0f },
        tTVPBgfxVertex{ 1.0f, -1.0f, 1.0f, 0.0f },
        tTVPBgfxVertex{ -1.0f, 1.0f, 0.0f, 1.0f },
        tTVPBgfxVertex{ 1.0f, 1.0f, 1.0f, 1.0f },
    };

    ++RectBatchCount;
    if(RectBatchCount <= 8 || RectBatchCount == 16 ||
       RectBatchCount == 32 || (RectBatchCount % 256) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx rect batch staged #") +
                  ttstr(static_cast<int>(RectBatchCount)) + TJS_W(" rect=") +
                  ttstr(targetLeft) + TJS_W(",") + ttstr(targetTop) +
                  TJS_W(",") + ttstr(targetWidth) + TJS_W("x") +
                  ttstr(targetHeight) + TJS_W(" textures=") +
                  ttstr(static_cast<int>(textureCount)) + TJS_W(" method=") +
                  ttstr(methodName ? methodName : ""));
    }
}

void StageTriangleBatch(const char *methodName, uint32_t nTriangles,
                        int clipLeft, int clipTop, int clipWidth,
                        int clipHeight, const double *targetPointsXY) {
    if(!nTriangles || clipWidth <= 0 || clipHeight <= 0 || !targetPointsXY)
        return;

    const uint32_t pointCount = nTriangles * 3;
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    StagedTriangleVertices.resize(pointCount);
    const float clipWidthFloat = static_cast<float>(clipWidth);
    const float clipHeightFloat = static_cast<float>(clipHeight);
    const float clipLeftFloat = static_cast<float>(clipLeft);
    const float clipTopFloat = static_cast<float>(clipTop);
    for(uint32_t i = 0; i < pointCount; ++i) {
        const float x = static_cast<float>(targetPointsXY[i * 2 + 0]);
        const float y = static_cast<float>(targetPointsXY[i * 2 + 1]);
        StagedTriangleVertices[i].x =
            ((x - clipLeftFloat) / clipWidthFloat - 0.5f) * 2.0f;
        StagedTriangleVertices[i].y =
            ((y - clipTopFloat) / clipHeightFloat - 0.5f) * 2.0f;
        StagedTriangleVertices[i].u = (x - clipLeftFloat) / clipWidthFloat;
        StagedTriangleVertices[i].v = (y - clipTopFloat) / clipHeightFloat;
    }

    ++TriangleBatchCount;
    if(TriangleBatchCount <= 8 || TriangleBatchCount == 16 ||
       TriangleBatchCount == 32 || (TriangleBatchCount % 256) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx triangle batch staged #") +
                  ttstr(static_cast<int>(TriangleBatchCount)) +
                  TJS_W(" tris=") + ttstr(static_cast<int>(nTriangles)) +
                  TJS_W(" clip=") + ttstr(clipWidth) + TJS_W("x") +
                  ttstr(clipHeight) + TJS_W(" method=") +
                  ttstr(methodName ? methodName : ""));
    }
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
#if defined(KIRIKIRI_HAS_BGFX)
    if(Ready && SoftwareFrameTexture != InvalidTextureHandle) {
        bgfx::TextureHandle texture{SoftwareFrameTexture};
        if(bgfx::isValid(texture))
            bgfx::destroy(texture);
    }
    if(Ready)
        bgfx::shutdown();
#endif
    SoftwareFrameTexture = InvalidTextureHandle;
    SoftwareFrameWidth = 0;
    SoftwareFrameHeight = 0;
    TextureUploadCount = 0;
    TextureUploadSkipCount = 0;
    LoggedIntermediateTextureUploadDisabled = false;
    FrameUploadCount = 0;
    RectBatchCount = 0;
    TriangleBatchCount = 0;
    StagedTriangleVertices.clear();
    Ready = false;
    Requested = false;
}

} // namespace TVPBgfx
