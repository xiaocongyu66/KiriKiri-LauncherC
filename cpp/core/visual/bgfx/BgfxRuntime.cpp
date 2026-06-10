#include "BgfxRuntime.h"

#include "DebugIntf.h"

#if defined(KIRIKIRI_HAS_BGFX)
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#endif

#include <mutex>

namespace TVPBgfx {
namespace {

std::mutex RuntimeMutex;
void *NativeWindow = nullptr;
bool Ready = false;
bool Requested = false;
uint32_t BackbufferWidth = 1;
uint32_t BackbufferHeight = 1;

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
