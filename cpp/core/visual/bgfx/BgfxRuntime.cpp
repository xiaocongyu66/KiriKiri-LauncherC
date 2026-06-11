#include "BgfxRuntime.h"

#include "DebugIntf.h"

#if defined(KIRIKIRI_HAS_BGFX)
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#endif

#include <algorithm>
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

#if defined(KIRIKIRI_HAS_BGFX)
constexpr uint16_t InvalidTextureHandle = bgfx::kInvalidHandle;
#else
constexpr uint16_t InvalidTextureHandle = UINT16_MAX;
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
#if defined(KIRIKIRI_HAS_BGFX)
    if(!Ready)
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

void Shutdown() {
    std::lock_guard<std::mutex> lock(RuntimeMutex);
#if defined(KIRIKIRI_HAS_BGFX)
    if(Ready)
        bgfx::shutdown();
#endif
    Ready = false;
    Requested = false;
}

} // namespace TVPBgfx
