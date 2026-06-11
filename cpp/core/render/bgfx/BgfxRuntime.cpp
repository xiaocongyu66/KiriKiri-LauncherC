#include "BgfxRuntime.h"

#include "BgfxTextureStore.h"

#include "DebugIntf.h"

#if defined(KIRIKIRI_HAS_BGFX)
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#endif

#include <algorithm>
#include <array>
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
#if defined(KIRIKIRI_HAS_BGFX)
constexpr uint16_t InvalidTextureHandle = bgfx::kInvalidHandle;
#else
constexpr uint16_t InvalidTextureHandle = UINT16_MAX;
#endif

uint16_t SoftwareFrameTexture = InvalidTextureHandle;
uint32_t SoftwareFrameWidth = 0;
uint32_t SoftwareFrameHeight = 0;
uint32_t SoftwareFrameSkipCount = 0;
bool LoggedSoftwareFrameUploadDisabled = false;
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

#if defined(KIRIKIRI_HAS_BGFX)
bgfx::VertexLayout ScreenVertexLayout;
#endif

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
    if(!Ready)
        return InvalidTextureHandle;
    return CreateManagedTexture2D(width, height, pixel, pitch, format);
}

uint16_t CreateEmptyTexture2D(uint32_t width, uint32_t height, int format) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    if(!Ready)
        return InvalidTextureHandle;
    return CreateEmptyManagedTexture2D(width, height, format);
}

void UpdateTexture2D(uint16_t handle, uint32_t width, uint32_t height,
                     const void *pixel, int pitch, int format) {
    UpdateTexture2DRect(handle, width, height, 0, 0, width, height, pixel,
                        pitch, format);
}

void UpdateTexture2DRect(uint16_t handle, uint32_t textureWidth,
                         uint32_t textureHeight, uint32_t x, uint32_t y,
                         uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    if(!Ready)
        return;
    UpdateManagedTexture2DRect(handle, textureWidth, textureHeight, x, y, width,
                               height, pixel, pitch, format);
}

void DestroyTexture2D(uint16_t handle) {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    if(!Ready)
        return;
    DestroyManagedTexture2D(handle);
}

uint64_t GetManagedTextureBytes() {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    return GetManagedTextureStoreBytes();
}

uint64_t GetManagedTextureBudgetBytes() {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    return GetManagedTextureStoreBudgetBytes();
}

void UploadSoftwareFrame(uint32_t width, uint32_t height, const void *pixel,
                         int pitch, int format) {
#if defined(KIRIKIRI_HAS_BGFX)
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    if(!Ready || !pixel || !width || !height || pitch <= 0)
        return;

    ++SoftwareFrameSkipCount;
    if(!LoggedSoftwareFrameUploadDisabled) {
        LoggedSoftwareFrameUploadDisabled = true;
        TVPAddLog(TJS_W("[renderer] bgfx software frame upload disabled while SDL/software presentation is active; avoiding per-frame full-surface copies."));
    }
    if(SoftwareFrameSkipCount <= 8 || SoftwareFrameSkipCount == 16 ||
       SoftwareFrameSkipCount == 32 || (SoftwareFrameSkipCount % 512) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx software frame upload skipped #") +
                  ttstr(static_cast<int>(SoftwareFrameSkipCount)) + TJS_W(" ") +
                  ttstr(static_cast<int>(width)) + TJS_W("x") +
                  ttstr(static_cast<int>(height)));
    }
    if(SoftwareFrameTexture != InvalidTextureHandle) {
        bgfx::TextureHandle oldTexture{SoftwareFrameTexture};
        if(bgfx::isValid(oldTexture))
            bgfx::destroy(oldTexture);
        SoftwareFrameTexture = InvalidTextureHandle;
        SoftwareFrameWidth = 0;
        SoftwareFrameHeight = 0;
    }
    (void)format;
    return;
#else
    (void)width;
    (void)height;
    (void)pixel;
    (void)pitch;
    (void)format;
#endif
}

void SubmitRectBatch(const RectBatchCommand &command) {
    if(command.targetWidth <= 0 || command.targetHeight <= 0)
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
                  ttstr(command.targetLeft) + TJS_W(",") +
                  ttstr(command.targetTop) + TJS_W(",") +
                  ttstr(command.targetWidth) + TJS_W("x") +
                  ttstr(command.targetHeight) + TJS_W(" textures=") +
                  ttstr(static_cast<int>(command.textureCount)) +
                  TJS_W(" method=") +
                  ttstr(command.methodName ? command.methodName : ""));
    }
}

void StageRectBatch(const char *methodName, int targetLeft, int targetTop,
                    int targetWidth, int targetHeight, uint32_t textureCount) {
    SubmitRectBatch(RectBatchCommand{ methodName, targetLeft, targetTop,
                                      targetWidth, targetHeight,
                                      textureCount });
}

void SubmitTriangleBatch(const TriangleBatchCommand &command) {
    if(!command.triangleCount || command.clipWidth <= 0 ||
       command.clipHeight <= 0 || !command.targetPointsXY)
        return;

    const uint32_t pointCount = command.triangleCount * 3;
    std::lock_guard<std::mutex> lock(RuntimeMutex);
    StagedTriangleVertices.resize(pointCount);
    const float clipWidthFloat = static_cast<float>(command.clipWidth);
    const float clipHeightFloat = static_cast<float>(command.clipHeight);
    const float clipLeftFloat = static_cast<float>(command.clipLeft);
    const float clipTopFloat = static_cast<float>(command.clipTop);
    for(uint32_t index = 0; index < pointCount; ++index) {
        const float x = static_cast<float>(command.targetPointsXY[index * 2 + 0]);
        const float y = static_cast<float>(command.targetPointsXY[index * 2 + 1]);
        StagedTriangleVertices[index].x =
            ((x - clipLeftFloat) / clipWidthFloat - 0.5f) * 2.0f;
        StagedTriangleVertices[index].y =
            ((y - clipTopFloat) / clipHeightFloat - 0.5f) * 2.0f;
        StagedTriangleVertices[index].u = (x - clipLeftFloat) / clipWidthFloat;
        StagedTriangleVertices[index].v = (y - clipTopFloat) / clipHeightFloat;
    }

    ++TriangleBatchCount;
    if(TriangleBatchCount <= 8 || TriangleBatchCount == 16 ||
       TriangleBatchCount == 32 || (TriangleBatchCount % 256) == 0) {
        TVPAddLog(TJS_W("[renderer] bgfx triangle batch staged #") +
                  ttstr(static_cast<int>(TriangleBatchCount)) +
                  TJS_W(" tris=") +
                  ttstr(static_cast<int>(command.triangleCount)) +
                  TJS_W(" clip=") + ttstr(command.clipWidth) + TJS_W("x") +
                  ttstr(command.clipHeight) + TJS_W(" method=") +
                  ttstr(command.methodName ? command.methodName : ""));
    }
}

void StageTriangleBatch(const char *methodName, uint32_t nTriangles,
                        int clipLeft, int clipTop, int clipWidth,
                        int clipHeight, const double *targetPointsXY) {
    SubmitTriangleBatch(TriangleBatchCommand{ methodName, nTriangles, clipLeft,
                                              clipTop, clipWidth, clipHeight,
                                              targetPointsXY });
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
    ResetManagedTextureStore();
    SoftwareFrameSkipCount = 0;
    LoggedSoftwareFrameUploadDisabled = false;
    RectBatchCount = 0;
    TriangleBatchCount = 0;
    StagedTriangleVertices.clear();
    Ready = false;
    Requested = false;
}

} // namespace TVPBgfx
