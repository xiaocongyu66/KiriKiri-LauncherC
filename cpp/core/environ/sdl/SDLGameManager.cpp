#include "SDLGameManager.h"

#include "NativeLog.h"
#include "Platform.h"
#include "tjsCommHead.h"
#include "ComplexRect.h"
#include "ConfigManager/GlobalConfigManager.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "LayerBitmapIntf.h"
#include "RenderManager.h"
#include "SDLGpuBackend.h"
#include "SDLGpuTextureCache.h"
#include "SDLUIManager.h"
#include "SysInitIntf.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "vkdefine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#if defined(__ANDROID__)
#include <android/native_window.h>
#endif

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

#if defined(__ANDROID__)
extern "C" ANativeWindow *TVPAndroidAcquireFlutterGameSurfaceWindow();
extern "C" void TVPAndroidReleaseFlutterGameSurfaceWindow(
    ANativeWindow *window);
extern "C" void TVPAndroidGetFlutterGameSurfaceSize(int *width, int *height);
#endif

namespace {

std::once_flag gSDLRuntimeInitOnce;
bool gSDLRuntimeInitialized = false;
std::string gSDLRuntimeError;
std::atomic_uint64_t gSDLLifecycleEventSequence{0};
std::atomic_uint64_t gSDLInputEventSequence{0};
std::once_flag gSDLInputQueueInitOnce;
Uint32 gSDLInputQueueEventType = 0;
std::mutex gSDLInputQueueMutex;
std::atomic_bool gSDLInputQueueReady{false};
std::atomic_uint64_t gSDLInputQueued{0};
std::atomic_uint64_t gSDLInputDrained{0};
std::atomic_uint64_t gSDLInputDropped{0};
std::atomic_uint64_t gSDLInputCoalesced{0};
std::atomic_uint64_t gSDLInputBatches{0};
std::atomic_uint64_t gSDLInputMaxBacklog{0};
std::atomic_uint64_t gSDLInputMaxAgeMs{0};
std::atomic_bool gSDLInputPaused{false};
constexpr uint64_t kSDLStaleDirectTouchAgeMs = 250;
constexpr uint64_t kSDLDirectTouchMoveBacklogLimit = 24;
constexpr int kAndroidKeyBack = 0x04;
constexpr int kAndroidKeyMenu = 0x52;
constexpr int kAndroidKeyDpadUp = 0x13;
constexpr int kAndroidKeyDpadDown = 0x14;
constexpr int kAndroidKeyDpadLeft = 0x15;
constexpr int kAndroidKeyDpadRight = 0x16;
constexpr int kAndroidKeyEnter = 0x42;
constexpr int kAndroidKeyPlay = 0x7e;
constexpr int kAndroidKeyDpadCenter = 0x17;
constexpr int kAndroidKeyDel = 0x43;
std::atomic_uint64_t gSDLRenderFrameSequence{0};
std::atomic_uint64_t gSDLRenderTextureChanges{0};
std::atomic_uint64_t gSDLPresenterFrameSequence{0};
std::atomic_uint64_t gSDLPresenterTextureChanges{0};
std::atomic_uint64_t gSDLPresenterCpuProbeAttempts{0};
std::atomic_uint64_t gSDLPresenterCpuAccessible{0};
std::atomic_uint64_t gSDLAndroidFlutterSurfacePresented{0};
std::atomic_uint64_t gSDLAndroidFlutterSurfaceFailures{0};
std::atomic_uint64_t gSDLAndroidFlutterSurfaceUnavailable{0};
std::atomic_uint64_t gSDLBitmapCompletionBatchSequence{0};
std::atomic_uint64_t gSDLBitmapCompletionRegionSequence{0};

struct TVPSDLPresenterProbeState {
    const void *texture = nullptr;
    int width = 0;
    int height = 0;
    int internalWidth = 0;
    int internalHeight = 0;
    int format = 0;
    int pitch = 0;
    unsigned int glTexture = 0;
};

std::mutex gSDLPresenterProbeMutex;
TVPSDLPresenterProbeState gSDLPresenterProbeState;

struct TVPSDLBitmapCompletionState {
    bool active = false;
    uint64_t batch = 0;
    uint64_t regions = 0;
    uint64_t copyReady = 0;
    uint64_t surfaceCopied = 0;
    uint64_t surfaceSkipped = 0;
    uint64_t glBacked = 0;
    uint64_t outOfBounds = 0;
    const void *manager = nullptr;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int destWidth = 0;
    int destHeight = 0;
    bool hasUnion = false;
    tTVPRect unionRect;
};

std::mutex gSDLBitmapCompletionMutex;
TVPSDLBitmapCompletionState gSDLBitmapCompletionState;

struct TVPSDLSurfaceMirrorState {
    SDL_Surface *surface = nullptr;
    int width = 0;
    int height = 0;
    uint64_t creates = 0;
    uint64_t copiedRegions = 0;
    uint64_t copiedBytes = 0;
    uint64_t skippedUnsupported = 0;
    uint64_t failedCopies = 0;
    bool hasUpdate = false;
    tTVPRect updateRect;
};

std::mutex gSDLSurfaceMirrorMutex;
TVPSDLSurfaceMirrorState gSDLSurfaceMirrorState;
std::atomic_int gSDLPresentedSurfaceWidth{0};
std::atomic_int gSDLPresentedSurfaceHeight{0};
std::atomic_bool gSDLSurfaceMirrorConsumerActive{false};
std::once_flag gSDLShadowUploadFlagOnce;
bool gSDLShadowUploadEnabled = false;

struct TVPSDLScreenPresenterState {
    bool takeoverEnabled = false;
    int frameWidth = 0;
    int frameHeight = 0;
    int sceneWidth = 0;
    int sceneHeight = 0;
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Surface *windowSurface = nullptr;
    SDL_Texture *texture = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    bool videoInitTried = false;
    bool videoReady = false;
    bool windowFailed = false;
    bool rendererFailed = false;
    bool hybridWindowDeferred = false;
    uint64_t pumpAttempts = 0;
    uint64_t presentedFrames = 0;
    uint64_t failedPumps = 0;
    uint64_t noSurfacePumps = 0;
    uint64_t deferredPumps = 0;
};

std::mutex gSDLScreenPresenterMutex;
TVPSDLScreenPresenterState gSDLScreenPresenterState;

struct TVPSDLGpuPresenterState {
    krkr::render::sdlgpu::Backend backend;
    krkr::render::sdlgpu::tvp::TextureCache textureCache;
    bool initializeTried = false;
    bool ready = false;
    bool unavailable = false;
    std::string preferredDriver;
    std::string availableDrivers;
    std::string unavailableReason;
    uint64_t attempts = 0;
    uint64_t uploads = 0;
    uint64_t skippedDirect = 0;
    uint64_t failures = 0;
};

std::mutex gSDLGpuPresenterMutex;
TVPSDLGpuPresenterState gSDLGpuPresenterState;

struct TVPSDLLoadingConsoleLine {
    std::string message;
    bool important = false;
};

struct TVPSDLLoadingConsoleState {
    bool active = false;
    uint64_t session = 0;
    uint64_t totalLines = 0;
    Uint64 retainUntilTicks = 0;
    std::deque<TVPSDLLoadingConsoleLine> lines;
};

std::mutex gSDLLoadingConsoleMutex;
TVPSDLLoadingConsoleState gSDLLoadingConsoleState;

struct TVPSDLRenderOverlayState {
    bool showFps = false;
    bool available = false;
    double fps = 0.0;
    unsigned int drawCount = 0;
    uint64_t videoMemoryBytes = 0;
    int selfMemoryMb = 0;
    int freeMemoryMb = 0;
    uint64_t presentedFrames = 0;
    uint64_t sequence = 0;
    std::string rendererName;
    float previousDeltaTime = 0.016f;
    float accumulatedDeltaTime = 0.0f;
};

std::mutex gSDLRenderOverlayMutex;
TVPSDLRenderOverlayState gSDLRenderOverlayState;

struct TVPSDLQueuedInputEvent {
    std::string eventName;
    int itemCount = 0;
    float x = 0.0f;
    float y = 0.0f;
    int code = 0;
    bool state = false;
    bool dispatchToTVP = false;
    Uint64 ticks = 0;
    uint64_t sequence = 0;
};

struct TVPSDLDirectTouchState {
    bool active = false;
    bool moved = false;
    bool mouseDownSent = false;
    int pointerId = -1;
    float startX = 0.0f;
    float startY = 0.0f;
    float lastX = 0.0f;
    float lastY = 0.0f;
    Uint64 downTicks = 0;
};

TVPSDLDirectTouchState gSDLDirectTouchState;

std::string FormatVersion(int version) {
    std::ostringstream os;
    os << SDL_VERSIONNUM_MAJOR(version) << "."
       << SDL_VERSIONNUM_MINOR(version) << "."
       << SDL_VERSIONNUM_MICRO(version);
    return os.str();
}

std::string SafeSDLString(const char *value) {
    return value ? std::string(value) : std::string();
}

void LaunchLog(const TVPSDLGameLaunchCallbacks &callbacks,
               const std::string &message) {
    if(callbacks.log) {
        callbacks.log(message);
    }
    try {
        spdlog::info("[sdl-launch] {}", message);
    } catch(...) {
    }
}

void LogSDLRuntime(const TVPSDLGameLaunchCallbacks &callbacks) {
    const bool initialized = TVPSDLInitializeRuntime();
    const TVPSDLRuntimeInfo info = TVPSDLGetRuntimeInfo();

    LaunchLog(callbacks,
              "SDL runtime compiled=" + info.compiledVersion + " linked=" +
                  info.linkedVersion + " platform=" + info.platform +
                  " events=" + (info.eventsReady ? "1" : "0") + " video=" +
                  (info.videoReady ? "1" : "0") + " audio=" +
                  (info.audioReady ? "1" : "0"));

    if(!info.revision.empty()) {
        LaunchLog(callbacks, "SDL revision: " + info.revision);
    }
    if(!info.videoDriver.empty()) {
        LaunchLog(callbacks, "SDL video driver: " + info.videoDriver);
    }
    if(!info.audioDriver.empty()) {
        LaunchLog(callbacks, "SDL audio driver: " + info.audioDriver);
    }
    if(!initialized && !gSDLRuntimeError.empty()) {
        LaunchLog(callbacks, "SDL runtime init failed: " + gSDLRuntimeError);
    }
}

bool IsHighFrequencyInput(const char *eventName) {
    if(!eventName)
        return false;
    return std::strcmp(eventName, "touch-move") == 0 ||
        std::strcmp(eventName, "hover-move") == 0;
}

bool IsSDLUITouchInput(const char *eventName) {
    if(!eventName)
        return false;
    return std::strcmp(eventName, "touch-begin") == 0 ||
        std::strcmp(eventName, "touch-end") == 0 ||
        std::strcmp(eventName, "touch-move") == 0 ||
        std::strcmp(eventName, "touch-cancel") == 0 ||
        std::strcmp(eventName, "touch-cancel-empty") == 0;
}

bool IsSDLDirectTouchInput(const char *eventName, bool dispatchToTVP) {
    return dispatchToTVP && IsSDLUITouchInput(eventName);
}

bool IsSDLDirectTouchMoveInput(const char *eventName, bool dispatchToTVP) {
    return dispatchToTVP && eventName &&
        std::strcmp(eventName, "touch-move") == 0;
}

bool IsAndroidLifecyclePauseEvent(const char *eventName) {
    return eventName && (std::strstr(eventName, "onPause") ||
                         std::strstr(eventName, "onStop") ||
                         std::strstr(eventName, "onDestroy"));
}

bool IsAndroidLifecycleResumeEvent(const char *eventName) {
    return eventName && (std::strstr(eventName, "onCreate") ||
                         std::strstr(eventName, "onResume"));
}

bool ShouldLogInputEvent(uint64_t sequence, const char *eventName) {
    if(!IsHighFrequencyInput(eventName))
        return true;
    return sequence <= 16 || (sequence & (sequence - 1)) == 0 ||
        (sequence % 512) == 0;
}

bool ShouldLogInputQueueSequence(uint64_t sequence) {
    return sequence <= 8 || (sequence % 256) == 0;
}

bool ShouldLogInputQueueEvent(uint64_t sequence, const char *eventName) {
    if(!IsHighFrequencyInput(eventName))
        return true;
    return sequence <= 16 || (sequence % 512) == 0;
}

bool ShouldLogPresenterFrame(uint64_t frame) {
    return frame <= 8 || frame == 16 || frame == 32 || frame == 64 ||
        frame == 128 || (frame % 256) == 0;
}

bool ShouldLogBitmapCompletionBatch(uint64_t batch) {
    return batch <= 8 || batch == 16 || batch == 32 || batch == 64 ||
        (batch % 256) == 0;
}

bool ShouldLogBitmapCompletionRegion(uint64_t globalRegion,
                                     uint64_t batchRegion) {
    (void)batchRegion;
    return globalRegion <= 16 ||
        globalRegion == 32 || globalRegion == 64 || globalRegion == 128 ||
        (globalRegion % 512) == 0;
}

bool ShouldLogSurfaceMirrorCopy(uint64_t copiedRegions) {
    return copiedRegions <= 8 || copiedRegions == 16 ||
        copiedRegions == 32 || copiedRegions == 64 ||
        copiedRegions == 128 || (copiedRegions % 256) == 0;
}

bool ShouldLogScreenPresenter(uint64_t sequence) {
    return sequence <= 8 || sequence == 16 || sequence == 32 ||
        sequence == 64 || sequence == 128 || (sequence % 256) == 0;
}

void LogSDLScreenPresenter(const char *message);
bool IsSDLGpuShadowUploadEnabled();

bool IsTruthyEnv(const char *name) {
    const char *value = SDL_getenv(name);
    return value && std::strcmp(value, "0") != 0 &&
        std::strcmp(value, "false") != 0 &&
        std::strcmp(value, "FALSE") != 0;
}

bool IsSDLScreenPresenterDisabled() {
    return IsTruthyEnv("KRKR2_DISABLE_SDL_SCREEN_TAKEOVER") ||
        IsTruthyEnv("KRKR2_FORCE_COCOS_RENDER");
}

std::string ReadCommandLineString(const tjs_char *name) {
    tTJSVariant value;
    if(TVPGetCommandLine(name, &value))
        return ttstr(value).AsStdString();
    return std::string();
}

std::string NormalizeGraphicsBackendName(std::string backend) {
    for(char &ch : backend) {
        if(ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    if(backend == "vulkan" || backend == "vk")
        return "vulkan";
    if(backend == "opengl" || backend == "gles" || backend == "opengles")
        return "opengl";
    if(backend == "gpuapi" || backend == "sdlgpu" ||
       backend == "sdl_gpu" || backend == "gpu")
        return "gpuapi";
    return std::string();
}

std::string PreferredGraphicsBackend() {
    std::string backend = ReadCommandLineString(TJS_W("graphics_backend"));
    if(backend.empty()) {
        backend = ReadCommandLineString(TJS_W("angle_backend"));
    }
    if(backend.empty()) {
        backend = IndividualConfigManager::GetInstance()->GetValue<std::string>(
            "graphics_backend", "");
    }
    if(backend.empty()) {
        backend = IndividualConfigManager::GetInstance()->GetValue<std::string>(
            "angle_backend", "");
    }
    backend = NormalizeGraphicsBackendName(backend);
    if(!backend.empty())
        return backend;

    std::string legacyRenderer = ReadCommandLineString(TJS_W("renderer"));
    if(legacyRenderer.empty()) {
        legacyRenderer =
            IndividualConfigManager::GetInstance()->GetValue<std::string>(
                "renderer", "software");
    }
    legacyRenderer = NormalizeGraphicsBackendName(legacyRenderer);
    if(legacyRenderer == "vulkan")
        return legacyRenderer;
    return "opengl";
}

std::string PreferredSDLRendererDriver() {
    const char *forced = SDL_getenv("KRKR2_SDL_RENDER_DRIVER");
    if(forced && *forced)
        return forced;

    const std::string backend = PreferredGraphicsBackend();
    if(backend == "gpuapi")
        return "gpu,vulkan,opengles2,opengl";
    if(backend == "vulkan")
        return "vulkan,opengles2,opengl,gpu";
    return "opengles2,opengl,vulkan,gpu";
}

std::string AvailableSDLGpuDrivers() {
    const int driverCount = SDL_GetNumGPUDrivers();
    if(driverCount <= 0)
        return "(none)";

    std::string drivers;
    for(int index = 0; index < driverCount; ++index) {
        const char *driver = SDL_GetGPUDriver(index);
        if(!driver || !*driver)
            continue;
        if(!drivers.empty())
            drivers += ",";
        drivers += driver;
    }
    return drivers.empty() ? "(none)" : drivers;
}

std::string PreferredSDLGpuDriver() {
    const char *forced = SDL_getenv("KRKR2_SDL_GPU_DRIVER");
    if(forced && *forced)
        return forced;
    return std::string();
}

std::string ClipOverlayString(std::string value, size_t limit) {
    for(char &ch : value) {
        if(ch == '\r' || ch == '\n' || ch == '\t')
            ch = ' ';
    }
    if(value.size() > limit) {
        value.resize(limit);
        value += "...";
    }
    return value;
}

void LogSDLScreenPresenter(const char *message) {
    TVPNativeLogInfo("sdl-screen", message ? message : "");
}

void LogSDLGpuPresenter(const char *message) {
    TVPNativeLogInfo("sdl-gpu-presenter", message ? message : "");
}

#if defined(__ANDROID__)
void RememberPresentedSurfaceSize(int width, int height) {
    if(width > 0 && height > 0) {
        gSDLPresentedSurfaceWidth.store(width, std::memory_order_relaxed);
        gSDLPresentedSurfaceHeight.store(height, std::memory_order_relaxed);
    }
}

void CopySurfaceToAndroidBuffer(SDL_Surface *surface, int surfaceWidth,
                                int surfaceHeight, int pitch,
                                const SDL_Rect &rect,
                                ANativeWindow_Buffer &buffer) {
    auto *dstBase = static_cast<Uint8 *>(buffer.bits);
    const auto *srcBase = static_cast<const Uint8 *>(surface->pixels);
    const int dstPitch = buffer.stride * 4;

    if(buffer.width == surfaceWidth && buffer.height == surfaceHeight) {
        for(int row = 0; row < rect.h; ++row) {
            const auto *src = srcBase +
                static_cast<size_t>(rect.y + row) * pitch +
                static_cast<size_t>(rect.x) * 4;
            auto *dst = dstBase +
                static_cast<size_t>(rect.y + row) * dstPitch +
                static_cast<size_t>(rect.x) * 4;
            std::memcpy(dst, src, static_cast<size_t>(rect.w) * 4);
        }
        return;
    }

    const int dstWidth = buffer.width > 0 ? buffer.width : surfaceWidth;
    const int dstHeight = buffer.height > 0 ? buffer.height : surfaceHeight;
    for(int y = 0; y < dstHeight; ++y) {
        const int srcY =
            static_cast<int>((static_cast<int64_t>(y) * surfaceHeight) /
                             dstHeight);
        const auto *src = srcBase + static_cast<size_t>(srcY) * pitch;
        auto *dst = dstBase + static_cast<size_t>(y) * dstPitch;
        for(int x = 0; x < dstWidth; ++x) {
            const int srcX =
                static_cast<int>((static_cast<int64_t>(x) * surfaceWidth) /
                                 dstWidth);
            std::memcpy(dst + static_cast<size_t>(x) * 4,
                        src + static_cast<size_t>(srcX) * 4, 4);
        }
    }
}

bool CopyTextureToAndroidBuffer(iTVPTexture2D *texture, int surfaceWidth,
                                int surfaceHeight, const SDL_Rect &rect,
                                ANativeWindow_Buffer &buffer) {
    if(!texture || surfaceWidth <= 0 || surfaceHeight <= 0 ||
       rect.w <= 0 || rect.h <= 0 || !buffer.bits)
        return false;

    auto *dstBase = static_cast<Uint8 *>(buffer.bits);
    const int dstPitch = buffer.stride * 4;

    if(buffer.width == surfaceWidth && buffer.height == surfaceHeight) {
        for(int row = 0; row < rect.h; ++row) {
            const auto *src = static_cast<const Uint8 *>(
                texture->GetScanLineForRead(rect.y + row));
            if(!src)
                return false;
            src += static_cast<size_t>(rect.x) * 4;
            auto *dst = dstBase +
                static_cast<size_t>(rect.y + row) * dstPitch +
                static_cast<size_t>(rect.x) * 4;
            std::memcpy(dst, src, static_cast<size_t>(rect.w) * 4);
        }
        return true;
    }

    const int dstWidth = buffer.width > 0 ? buffer.width : surfaceWidth;
    const int dstHeight = buffer.height > 0 ? buffer.height : surfaceHeight;
    for(int y = 0; y < dstHeight; ++y) {
        const int srcY =
            static_cast<int>((static_cast<int64_t>(y) * surfaceHeight) /
                             dstHeight);
        const auto *src = static_cast<const Uint8 *>(
            texture->GetScanLineForRead(srcY));
        if(!src)
            return false;
        auto *dst = dstBase + static_cast<size_t>(y) * dstPitch;
        for(int x = 0; x < dstWidth; ++x) {
            const int srcX =
                static_cast<int>((static_cast<int64_t>(x) * surfaceWidth) /
                                 dstWidth);
            std::memcpy(dst + static_cast<size_t>(x) * 4,
                        src + static_cast<size_t>(srcX) * 4, 4);
        }
    }
    return true;
}

bool TryPresentAndroidFlutterTexture(iTVPTexture2D *texture,
                                     TVPTextureFormat::e format,
                                     int surfaceWidth, int surfaceHeight,
                                     const SDL_Rect &rect,
                                     const char *stage) {
    if(!texture || surfaceWidth <= 0 || surfaceHeight <= 0 ||
       format != TVPTextureFormat::RGBA)
        return false;

    ANativeWindow *window = TVPAndroidAcquireFlutterGameSurfaceWindow();
    if(!window) {
        const uint64_t unavailable =
            ++gSDLAndroidFlutterSurfaceUnavailable;
        if(ShouldLogScreenPresenter(unavailable)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "flutter-texture unavailable #%llu stage=%s "
                          "surface=%dx%d",
                          static_cast<unsigned long long>(unavailable),
                          stage ? stage : "", surfaceWidth, surfaceHeight);
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    int flutterWidth = 0;
    int flutterHeight = 0;
    TVPAndroidGetFlutterGameSurfaceSize(&flutterWidth, &flutterHeight);

    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if(windowWidth != surfaceWidth || windowHeight != surfaceHeight) {
        const int geometryResult =
            ANativeWindow_setBuffersGeometry(window, surfaceWidth,
                                             surfaceHeight,
                                             WINDOW_FORMAT_RGBA_8888);
        if(geometryResult != 0) {
            const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
            if(ShouldLogScreenPresenter(failed)) {
                char message[384];
                std::snprintf(message, sizeof(message),
                              "flutter-texture geometry failed #%llu "
                              "stage=%s from=%dx%d flutter=%dx%d to=%dx%d "
                              "result=%d",
                              static_cast<unsigned long long>(failed),
                              stage ? stage : "", windowWidth, windowHeight,
                              flutterWidth, flutterHeight, surfaceWidth,
                              surfaceHeight, geometryResult);
                LogSDLScreenPresenter(message);
            }
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
    }

    ARect dirty;
    dirty.left = rect.x;
    dirty.top = rect.y;
    dirty.right = rect.x + rect.w;
    dirty.bottom = rect.y + rect.h;

    ANativeWindow_Buffer buffer{};
    const int lockResult = ANativeWindow_lock(window, &buffer, &dirty);
    if(lockResult != 0 || !buffer.bits) {
        const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "flutter-texture lock failed #%llu stage=%s "
                          "surface=%dx%d rect=%d,%d,%dx%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          rect.x, rect.y, rect.w, rect.h, lockResult);
            LogSDLScreenPresenter(message);
        }
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);
        return false;
    }

    const bool copied = CopyTextureToAndroidBuffer(
        texture, surfaceWidth, surfaceHeight, rect, buffer);
    const int unlockResult = ANativeWindow_unlockAndPost(window);
    TVPAndroidReleaseFlutterGameSurfaceWindow(window);
    if(!copied || unlockResult != 0) {
        const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "flutter-texture post failed #%llu stage=%s "
                          "surface=%dx%d copied=%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          copied ? 1 : 0, unlockResult);
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    RememberPresentedSurfaceSize(surfaceWidth, surfaceHeight);
    const uint64_t presented = ++gSDLAndroidFlutterSurfacePresented;
    if(ShouldLogScreenPresenter(presented)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-direct #%llu stage=%s surface=%dx%d "
                      "flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      flutterWidth, flutterHeight, buffer.width,
                      buffer.height, buffer.stride, buffer.format, rect.x,
                      rect.y, rect.w, rect.h);
        LogSDLScreenPresenter(message);
    }
    return true;
}

bool TryPresentAndroidFlutterSurface(SDL_Surface *surface, int surfaceWidth,
                                     int surfaceHeight, int pitch,
                                     const SDL_Rect &rect,
                                     const char *stage) {
    if(!surface || !surface->pixels || surfaceWidth <= 0 ||
       surfaceHeight <= 0 || pitch <= 0)
        return false;

    ANativeWindow *window = TVPAndroidAcquireFlutterGameSurfaceWindow();
    if(!window) {
        const uint64_t unavailable =
            ++gSDLAndroidFlutterSurfaceUnavailable;
        if(ShouldLogScreenPresenter(unavailable)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "flutter-surface unavailable #%llu stage=%s "
                          "surface=%dx%d",
                          static_cast<unsigned long long>(unavailable),
                          stage ? stage : "", surfaceWidth, surfaceHeight);
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    int flutterWidth = 0;
    int flutterHeight = 0;
    TVPAndroidGetFlutterGameSurfaceSize(&flutterWidth, &flutterHeight);

    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if(windowWidth != surfaceWidth || windowHeight != surfaceHeight) {
        const int geometryResult =
            ANativeWindow_setBuffersGeometry(window, surfaceWidth,
                                             surfaceHeight,
                                             WINDOW_FORMAT_RGBA_8888);
        if(geometryResult != 0) {
            const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
            if(ShouldLogScreenPresenter(failed)) {
                char message[384];
                std::snprintf(message, sizeof(message),
                              "flutter-surface geometry failed #%llu "
                              "stage=%s from=%dx%d flutter=%dx%d to=%dx%d "
                              "result=%d",
                              static_cast<unsigned long long>(failed),
                              stage ? stage : "", windowWidth, windowHeight,
                              flutterWidth, flutterHeight, surfaceWidth,
                              surfaceHeight, geometryResult);
                LogSDLScreenPresenter(message);
            }
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
    }

    ARect dirty;
    dirty.left = rect.x;
    dirty.top = rect.y;
    dirty.right = rect.x + rect.w;
    dirty.bottom = rect.y + rect.h;

    ANativeWindow_Buffer buffer{};
    const int lockResult = ANativeWindow_lock(window, &buffer, &dirty);
    if(lockResult != 0 || !buffer.bits) {
        const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "flutter-surface lock failed #%llu stage=%s "
                          "surface=%dx%d rect=%d,%d,%dx%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          rect.x, rect.y, rect.w, rect.h, lockResult);
            LogSDLScreenPresenter(message);
        }
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);
        return false;
    }

    CopySurfaceToAndroidBuffer(surface, surfaceWidth, surfaceHeight, pitch,
                               rect, buffer);
    const int unlockResult = ANativeWindow_unlockAndPost(window);
    TVPAndroidReleaseFlutterGameSurfaceWindow(window);
    if(unlockResult != 0) {
        const uint64_t failed = ++gSDLAndroidFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[320];
            std::snprintf(message, sizeof(message),
                          "flutter-surface post failed #%llu stage=%s "
                          "surface=%dx%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          unlockResult);
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    const uint64_t presented = ++gSDLAndroidFlutterSurfacePresented;
    RememberPresentedSurfaceSize(surfaceWidth, surfaceHeight);
    if(ShouldLogScreenPresenter(presented)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-surface #%llu stage=%s surface=%dx%d "
                      "flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      flutterWidth, flutterHeight, buffer.width,
                      buffer.height, buffer.stride, buffer.format, rect.x,
                      rect.y, rect.w, rect.h);
        LogSDLScreenPresenter(message);
    }
    return true;
}
#endif

bool EnsureSDLGpuPresenterLocked(const char *stage) {
    if(gSDLGpuPresenterState.ready)
        return true;
    if(gSDLGpuPresenterState.initializeTried) {
        if(!gSDLGpuPresenterState.unavailable) {
            gSDLGpuPresenterState.unavailable = true;
            gSDLGpuPresenterState.unavailableReason =
                "SDL_GPU presenter initialization already failed";
        }
        return false;
    }

    gSDLGpuPresenterState.initializeTried = true;
    TVPSDLInitializeRuntime();

    gSDLGpuPresenterState.preferredDriver = PreferredSDLGpuDriver();
    gSDLGpuPresenterState.availableDrivers = AvailableSDLGpuDrivers();
    {
        char message[384];
        std::snprintf(
            message, sizeof(message),
            "backend init begin stage=%s preferred=%s available=%s",
            stage ? stage : "",
            gSDLGpuPresenterState.preferredDriver.empty()
                ? "(auto)"
                : gSDLGpuPresenterState.preferredDriver.c_str(),
            gSDLGpuPresenterState.availableDrivers.c_str());
        LogSDLGpuPresenter(message);
    }

    if((SDL_WasInit(0) & SDL_INIT_VIDEO) == 0 &&
       !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        gSDLGpuPresenterState.unavailable = true;
        gSDLGpuPresenterState.unavailableReason = "SDL_INIT_VIDEO failed";
        if(const char *error = SDL_GetError(); error && *error) {
            gSDLGpuPresenterState.unavailableReason += ": ";
            gSDLGpuPresenterState.unavailableReason += error;
        }
        char message[320];
        std::snprintf(message, sizeof(message),
                      "video init failed stage=%s error=%s",
                      stage ? stage : "", SDL_GetError());
        LogSDLGpuPresenter(message);
        return false;
    }

    if(!gSDLGpuPresenterState.backend.Initialize(
           gSDLGpuPresenterState.preferredDriver.empty()
               ? nullptr
               : gSDLGpuPresenterState.preferredDriver.c_str(),
           false)) {
        gSDLGpuPresenterState.unavailable = true;
        gSDLGpuPresenterState.unavailableReason =
            gSDLGpuPresenterState.backend.LastError();
        char message[384];
        std::snprintf(message, sizeof(message),
                      "backend init failed stage=%s error=%s",
                      stage ? stage : "",
                      gSDLGpuPresenterState.unavailableReason.c_str());
        LogSDLGpuPresenter(message);
        return false;
    }

    gSDLGpuPresenterState.textureCache.SetBackend(
        &gSDLGpuPresenterState.backend);
    gSDLGpuPresenterState.ready = true;
    gSDLGpuPresenterState.unavailable = false;
    gSDLGpuPresenterState.unavailableReason.clear();

    char message[256];
    std::snprintf(message, sizeof(message),
                  "backend ready stage=%s driver=%s",
                  stage ? stage : "",
                  gSDLGpuPresenterState.backend.DriverName());
    LogSDLGpuPresenter(message);
    return true;
}

void AppendSDLGpuOverlayInfo(std::ostringstream &rendererInfo) {
    if(!IsSDLGpuShadowUploadEnabled())
        return;

    std::lock_guard<std::mutex> lock(gSDLGpuPresenterMutex);
    rendererInfo << " sdlgpu=";
    if(gSDLGpuPresenterState.ready) {
        const char *driver = gSDLGpuPresenterState.backend.DriverName();
        rendererInfo << (driver && *driver ? driver : "ready");
        rendererInfo << " uploads=" << gSDLGpuPresenterState.uploads;
        if(gSDLGpuPresenterState.skippedDirect > 0)
            rendererInfo << " skippedDirect="
                         << gSDLGpuPresenterState.skippedDirect;
        if(gSDLGpuPresenterState.failures > 0)
            rendererInfo << " failures=" << gSDLGpuPresenterState.failures;
        return;
    }

    if(gSDLGpuPresenterState.unavailable) {
        rendererInfo << "unavailable";
        if(!gSDLGpuPresenterState.preferredDriver.empty()) {
            rendererInfo << " preferred="
                         << gSDLGpuPresenterState.preferredDriver;
        }
        if(!gSDLGpuPresenterState.availableDrivers.empty()) {
            rendererInfo << " available="
                         << gSDLGpuPresenterState.availableDrivers;
        }
        if(!gSDLGpuPresenterState.unavailableReason.empty()) {
            rendererInfo << " reason="
                         << ClipOverlayString(
                                gSDLGpuPresenterState.unavailableReason, 96);
        }
        return;
    }

    rendererInfo << (gSDLGpuPresenterState.initializeTried ? "initializing"
                                                           : "pending");
}

bool IsSDLScreenPresenterWindowSupported() {
    return !IsSDLScreenPresenterDisabled();
}

const char *SDLScreenPresenterUnsupportedReason() {
    return IsSDLScreenPresenterDisabled() ? "disabled-by-env" : "";
}

bool IsSDLSurfaceMirrorConsumerActive() {
    return gSDLSurfaceMirrorConsumerActive.load(std::memory_order_relaxed);
}

bool IsExplicitSDLGpuShadowUploadEnabled() {
    std::call_once(gSDLShadowUploadFlagOnce, []() {
        const char *value = SDL_getenv("KRKR2_ENABLE_SDL_GPU_SHADOW_UPLOAD");
        gSDLShadowUploadEnabled =
            value && std::strcmp(value, "0") != 0 &&
            std::strcmp(value, "false") != 0 &&
            std::strcmp(value, "FALSE") != 0;
    });
    return gSDLShadowUploadEnabled;
}

bool IsSDLGpuShadowUploadEnabled() {
    return IsExplicitSDLGpuShadowUploadEnabled() ||
        PreferredGraphicsBackend() == "gpuapi";
}

bool ShouldRunSDLGpuShadowUpload(bool takeoverActive) {
#if defined(__ANDROID__)
    if(takeoverActive && PreferredGraphicsBackend() == "gpuapi" &&
       !IsExplicitSDLGpuShadowUploadEnabled()) {
        return false;
    }
#else
    (void)takeoverActive;
#endif
    return IsSDLGpuShadowUploadEnabled();
}

bool IsSDLRenderDiagnosticsActive() {
    return IsTruthyEnv("KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS") ||
        IsSDLGpuShadowUploadEnabled();
}

void DestroySDLSurfaceMirrorLocked() {
    if(gSDLSurfaceMirrorState.surface) {
        SDL_DestroySurface(gSDLSurfaceMirrorState.surface);
        gSDLSurfaceMirrorState.surface = nullptr;
    }
    gSDLSurfaceMirrorState.width = 0;
    gSDLSurfaceMirrorState.height = 0;
    gSDLSurfaceMirrorState.hasUpdate = false;
    gSDLSurfaceMirrorState.updateRect.clear();
}

void DropSDLSurfaceMirror(const char *reason) {
    std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
    if(!gSDLSurfaceMirrorState.surface)
        return;

    char message[256];
    std::snprintf(message, sizeof(message),
                  "drop reason=%s copiedRegions=%llu copiedBytes=%llu",
                  reason ? reason : "",
                  static_cast<unsigned long long>(
                      gSDLSurfaceMirrorState.copiedRegions),
                  static_cast<unsigned long long>(
                      gSDLSurfaceMirrorState.copiedBytes));
    DestroySDLSurfaceMirrorLocked();
    TVPNativeLogInfo("sdl-surface", message);
}

void DestroySDLScreenTextureLocked() {
    if(gSDLScreenPresenterState.texture) {
        SDL_DestroyTexture(gSDLScreenPresenterState.texture);
        gSDLScreenPresenterState.texture = nullptr;
    }
    gSDLScreenPresenterState.textureWidth = 0;
    gSDLScreenPresenterState.textureHeight = 0;
}

void DestroySDLScreenPresenterLocked() {
    DestroySDLScreenTextureLocked();
    gSDLScreenPresenterState.windowSurface = nullptr;
    if(gSDLScreenPresenterState.renderer) {
        SDL_DestroyRenderer(gSDLScreenPresenterState.renderer);
        gSDLScreenPresenterState.renderer = nullptr;
    }
    if(gSDLScreenPresenterState.window) {
        SDL_DestroyWindow(gSDLScreenPresenterState.window);
        gSDLScreenPresenterState.window = nullptr;
    }
    gSDLScreenPresenterState.windowFailed = false;
    gSDLScreenPresenterState.rendererFailed = false;
    gSDLScreenPresenterState.hybridWindowDeferred = false;
}

bool EnsureSDLScreenPresenterLocked(int surfaceWidth, int surfaceHeight,
                                    const char *stage) {
    if(!IsSDLScreenPresenterWindowSupported()) {
        gSDLScreenPresenterState.hybridWindowDeferred = true;
        const uint64_t deferred = ++gSDLScreenPresenterState.deferredPumps;
        if(ShouldLogScreenPresenter(deferred)) {
            const Uint32 initialized = SDL_WasInit(0);
            char message[512];
            std::snprintf(
                message, sizeof(message),
                "window creation deferred #%llu stage=%s reason=%s "
                "surface=%dx%d frame=%dx%d scene=%dx%d events=%d video=%d "
                "audio=%d",
                static_cast<unsigned long long>(deferred), stage ? stage : "",
                SDLScreenPresenterUnsupportedReason(), surfaceWidth,
                surfaceHeight, gSDLScreenPresenterState.frameWidth,
                gSDLScreenPresenterState.frameHeight,
                gSDLScreenPresenterState.sceneWidth,
                gSDLScreenPresenterState.sceneHeight,
                (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                (initialized & SDL_INIT_AUDIO) ? 1 : 0);
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    TVPSDLInitializeRuntime();

    if(!gSDLScreenPresenterState.videoInitTried) {
        gSDLScreenPresenterState.videoInitTried = true;
        if(SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            gSDLScreenPresenterState.videoReady = true;
            const char *driver = SDL_GetCurrentVideoDriver();
            char message[384];
            std::snprintf(message, sizeof(message),
                          "video ready stage=%s driver=%s frame=%dx%d "
                          "scene=%dx%d",
                          stage ? stage : "", driver ? driver : "",
                          gSDLScreenPresenterState.frameWidth,
                          gSDLScreenPresenterState.frameHeight,
                          gSDLScreenPresenterState.sceneWidth,
                          gSDLScreenPresenterState.sceneHeight);
            LogSDLScreenPresenter(message);
        } else {
            gSDLScreenPresenterState.videoReady = false;
            char message[384];
            std::snprintf(message, sizeof(message),
                          "video init failed stage=%s error=%s events=%d "
                          "video=%d",
                          stage ? stage : "", SDL_GetError(),
                          (SDL_WasInit(0) & SDL_INIT_EVENTS) ? 1 : 0,
                          (SDL_WasInit(0) & SDL_INIT_VIDEO) ? 1 : 0);
            LogSDLScreenPresenter(message);
            return false;
        }
    }

    if(!gSDLScreenPresenterState.videoReady)
        return false;

    int windowWidth = gSDLScreenPresenterState.frameWidth > 0
        ? gSDLScreenPresenterState.frameWidth
        : surfaceWidth;
    int windowHeight = gSDLScreenPresenterState.frameHeight > 0
        ? gSDLScreenPresenterState.frameHeight
        : surfaceHeight;
    SDL_WindowFlags windowFlags = 0;
#if defined(__ANDROID__)
    windowFlags |= SDL_WINDOW_RESIZABLE;
    windowFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    windowFlags |= SDL_WINDOW_FULLSCREEN;
    windowWidth = 0;
    windowHeight = 0;
#endif

    if(!gSDLScreenPresenterState.window &&
       !gSDLScreenPresenterState.windowFailed) {
        gSDLScreenPresenterState.window = SDL_CreateWindow(
            "KiriKiri SDL Presenter", windowWidth, windowHeight, windowFlags);
        if(!gSDLScreenPresenterState.window) {
            gSDLScreenPresenterState.windowFailed = true;
            char message[384];
            std::snprintf(message, sizeof(message),
                          "window create failed stage=%s size=%dx%d "
                          "surface=%dx%d error=%s",
                          stage ? stage : "", windowWidth, windowHeight,
                          surfaceWidth, surfaceHeight, SDL_GetError());
            LogSDLScreenPresenter(message);
            return false;
        }
        char message[320];
        std::snprintf(message, sizeof(message),
                      "window created stage=%s window=%p size=%dx%d "
                      "surface=%dx%d",
                      stage ? stage : "",
                      static_cast<void *>(gSDLScreenPresenterState.window),
                      windowWidth, windowHeight, surfaceWidth, surfaceHeight);
        LogSDLScreenPresenter(message);
    }

    if(!gSDLScreenPresenterState.window)
        return false;

    if(!gSDLScreenPresenterState.renderer &&
       !gSDLScreenPresenterState.rendererFailed &&
       !gSDLScreenPresenterState.windowSurface) {
        const std::string graphicsBackend = PreferredGraphicsBackend();
        const std::string rendererDriver = PreferredSDLRendererDriver();
        gSDLScreenPresenterState.renderer =
            SDL_CreateRenderer(gSDLScreenPresenterState.window,
                               rendererDriver.empty() ? nullptr
                                                      : rendererDriver.c_str());
        if(!gSDLScreenPresenterState.renderer) {
            char rendererError[256];
            std::snprintf(rendererError, sizeof(rendererError), "%s",
                          SDL_GetError());
            gSDLScreenPresenterState.windowSurface =
                SDL_GetWindowSurface(gSDLScreenPresenterState.window);
            if(!gSDLScreenPresenterState.windowSurface) {
                gSDLScreenPresenterState.rendererFailed = true;
                char message[512];
                std::snprintf(message, sizeof(message),
                              "renderer/window-surface failed stage=%s "
                              "rendererError=%s surfaceError=%s",
                              stage ? stage : "", rendererError,
                              SDL_GetError());
                LogSDLScreenPresenter(message);
                return false;
            }
            char message[384];
            std::snprintf(message, sizeof(message),
                          "renderer failed, using window surface stage=%s "
                          "backend=%s driver=%s rendererError=%s windowSurface=%p "
                          "size=%dx%d pitch=%d",
                          stage ? stage : "", graphicsBackend.c_str(),
                          rendererDriver.c_str(), rendererError,
                          static_cast<void *>(
                              gSDLScreenPresenterState.windowSurface),
                          gSDLScreenPresenterState.windowSurface->w,
                          gSDLScreenPresenterState.windowSurface->h,
                          gSDLScreenPresenterState.windowSurface->pitch);
            LogSDLScreenPresenter(message);
            return true;
        }
        SDL_SetRenderVSync(gSDLScreenPresenterState.renderer, 1);
        SDL_SetRenderLogicalPresentation(gSDLScreenPresenterState.renderer,
                                         surfaceWidth, surfaceHeight,
                                         SDL_LOGICAL_PRESENTATION_STRETCH);
        const char *actualDriver =
            SDL_GetRendererName(gSDLScreenPresenterState.renderer);
        char message[320];
        std::snprintf(message, sizeof(message),
                      "renderer created stage=%s renderer=%p backend=%s "
                      "driver=%s actual=%s logical=%dx%d",
                      stage ? stage : "",
                      static_cast<void *>(gSDLScreenPresenterState.renderer),
                      graphicsBackend.c_str(),
                      rendererDriver.c_str(),
                      actualDriver ? actualDriver : "",
                      surfaceWidth, surfaceHeight);
        LogSDLScreenPresenter(message);
    }

    if(!gSDLScreenPresenterState.renderer)
        return gSDLScreenPresenterState.windowSurface != nullptr;

    if(gSDLScreenPresenterState.windowSurface)
        return false;

    if(!gSDLScreenPresenterState.texture ||
       gSDLScreenPresenterState.textureWidth != surfaceWidth ||
       gSDLScreenPresenterState.textureHeight != surfaceHeight) {
        DestroySDLScreenTextureLocked();
        gSDLScreenPresenterState.texture = SDL_CreateTexture(
            gSDLScreenPresenterState.renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING, surfaceWidth, surfaceHeight);
        if(!gSDLScreenPresenterState.texture) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "texture create failed stage=%s size=%dx%d "
                          "error=%s",
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          SDL_GetError());
            LogSDLScreenPresenter(message);
            return false;
        }
        gSDLScreenPresenterState.textureWidth = surfaceWidth;
        gSDLScreenPresenterState.textureHeight = surfaceHeight;
        SDL_SetTextureScaleMode(gSDLScreenPresenterState.texture,
                                SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(gSDLScreenPresenterState.texture,
                                SDL_BLENDMODE_NONE);
        SDL_SetRenderLogicalPresentation(gSDLScreenPresenterState.renderer,
                                         surfaceWidth, surfaceHeight,
                                         SDL_LOGICAL_PRESENTATION_STRETCH);
        char message[320];
        std::snprintf(message, sizeof(message),
                      "texture created stage=%s texture=%p size=%dx%d",
                      stage ? stage : "",
                      static_cast<void *>(gSDLScreenPresenterState.texture),
                      surfaceWidth, surfaceHeight);
        LogSDLScreenPresenter(message);
    }

    return gSDLScreenPresenterState.texture != nullptr;
}

const char *TextureFormatName(TVPTextureFormat::e format) {
    switch(format) {
        case TVPTextureFormat::None:
            return "None";
        case TVPTextureFormat::Gray:
            return "Gray";
        case TVPTextureFormat::RGB:
            return "RGB";
        case TVPTextureFormat::RGBA:
            return "RGBA";
        case TVPTextureFormat::Compressed:
            return "Compressed";
        case TVPTextureFormat::CompressedEnd:
            return "CompressedEnd";
    }
    return "Unknown";
}

bool IsDirectCpuProbeFormat(TVPTextureFormat::e format) {
    return format == TVPTextureFormat::Gray ||
        format == TVPTextureFormat::RGB ||
        format == TVPTextureFormat::RGBA;
}

int TextureBytesPerPixel(TVPTextureFormat::e format) {
    switch(format) {
        case TVPTextureFormat::Gray:
            return 1;
        case TVPTextureFormat::RGB:
            return 3;
        case TVPTextureFormat::RGBA:
            return 4;
        case TVPTextureFormat::None:
        case TVPTextureFormat::Compressed:
        case TVPTextureFormat::CompressedEnd:
            break;
    }
    return 0;
}

bool IsBitmapCompletionInBounds(int x, int y, const tTVPRect &clipRect,
                                int sourceWidth, int sourceHeight,
                                int bitmapWidth, int bitmapHeight) {
    return x >= 0 && y >= 0 &&
        x + clipRect.get_width() <= sourceWidth &&
        y + clipRect.get_height() <= sourceHeight &&
        clipRect.left >= 0 && clipRect.top >= 0 &&
        clipRect.right <= bitmapWidth && clipRect.bottom <= bitmapHeight;
}

bool EnsureSDLSurfaceMirrorLocked(int width, int height) {
    if(width <= 0 || height <= 0)
        return false;
    if(gSDLSurfaceMirrorState.surface &&
       gSDLSurfaceMirrorState.width == width &&
       gSDLSurfaceMirrorState.height == height)
        return true;

    if(gSDLSurfaceMirrorState.surface) {
        SDL_DestroySurface(gSDLSurfaceMirrorState.surface);
        gSDLSurfaceMirrorState.surface = nullptr;
    }

    gSDLSurfaceMirrorState.surface =
        SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if(!gSDLSurfaceMirrorState.surface) {
        char message[256];
        std::snprintf(message, sizeof(message),
                      "create failed size=%dx%d error=%s", width, height,
                      SDL_GetError());
        TVPNativeLogInfo("sdl-surface", message);
        gSDLSurfaceMirrorState.width = 0;
        gSDLSurfaceMirrorState.height = 0;
        return false;
    }

    gSDLSurfaceMirrorState.width = width;
    gSDLSurfaceMirrorState.height = height;
    gSDLSurfaceMirrorState.creates++;
    gSDLSurfaceMirrorState.hasUpdate = false;
    gSDLSurfaceMirrorState.updateRect.clear();

    char message[256];
    std::snprintf(message, sizeof(message),
                  "create #%llu surface=%p size=%dx%d pitch=%d format=%s",
                  static_cast<unsigned long long>(
                      gSDLSurfaceMirrorState.creates),
                  static_cast<void *>(gSDLSurfaceMirrorState.surface), width,
                  height, gSDLSurfaceMirrorState.surface->pitch,
                  SDL_GetPixelFormatName(gSDLSurfaceMirrorState.surface->format));
    TVPNativeLogInfo("sdl-surface", message);
    return true;
}

bool CopyRegionToSDLSurfaceMirror(iTVPTexture2D *texture,
                                  const tTVPRect &clipRect, int x, int y,
                                  int sourceWidth, int sourceHeight,
                                  TVPTextureFormat::e format,
                                  uint64_t globalRegion,
                                  uint64_t batchRegion,
                                  uint64_t &copiedTotal,
                                  uint64_t &copiedBytesTotal,
                                  uint64_t &skippedTotal) {
    copiedTotal = 0;
    copiedBytesTotal = 0;
    skippedTotal = 0;

    if(!texture)
        return false;

    const int bytesPerPixel = TextureBytesPerPixel(format);
    if(format != TVPTextureFormat::RGBA || bytesPerPixel != 4) {
        std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
        skippedTotal = ++gSDLSurfaceMirrorState.skippedUnsupported;
        if(skippedTotal <= 8 || (skippedTotal % 128) == 0) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "skip unsupported total=%llu global=%llu "
                          "format=%s(%d) bpp=%d",
                          static_cast<unsigned long long>(skippedTotal),
                          static_cast<unsigned long long>(globalRegion),
                          TextureFormatName(format), static_cast<int>(format),
                          bytesPerPixel);
            TVPNativeLogInfo("sdl-surface", message);
        }
        return false;
    }

    const int copyWidth = clipRect.get_width();
    const int copyHeight = clipRect.get_height();
    if(copyWidth <= 0 || copyHeight <= 0)
        return false;

    const int mirrorWidth = sourceWidth > 0 ? sourceWidth
                                            : static_cast<int>(texture->GetWidth());
    const int mirrorHeight = sourceHeight > 0
        ? sourceHeight
        : static_cast<int>(texture->GetHeight());

    std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
    if(!EnsureSDLSurfaceMirrorLocked(mirrorWidth, mirrorHeight)) {
        gSDLSurfaceMirrorState.failedCopies++;
        return false;
    }

    SDL_Surface *surface = gSDLSurfaceMirrorState.surface;
    bool locked = false;
    if(SDL_MUSTLOCK(surface)) {
        if(SDL_LockSurface(surface) != 0) {
            gSDLSurfaceMirrorState.failedCopies++;
            char message[256];
            std::snprintf(message, sizeof(message),
                          "lock failed global=%llu error=%s",
                          static_cast<unsigned long long>(globalRegion),
                          SDL_GetError());
            TVPNativeLogInfo("sdl-surface", message);
            return false;
        }
        locked = true;
    }

    bool copied = false;
    std::string failureReason;
    try {
        for(int row = 0; row < copyHeight; ++row) {
            const auto *src = static_cast<const tjs_uint8 *>(
                texture->GetScanLineForRead(clipRect.top + row));
            if(!src)
                throw std::runtime_error("scanline unavailable");
            src += clipRect.left * bytesPerPixel;
            auto *dst = static_cast<tjs_uint8 *>(surface->pixels) +
                surface->pitch * (y + row) + x * 4;
            SDL_memcpy(dst, src, static_cast<size_t>(copyWidth) * 4);
        }
        copied = true;
    } catch(const std::exception &e) {
        failureReason = e.what();
    } catch(...) {
        failureReason = "unknown exception";
    }

    if(!copied) {
        const uint64_t failed = ++gSDLSurfaceMirrorState.failedCopies;
        if(failed <= 8 || (failed % 128) == 0) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "copy failed total=%llu global=%llu batchRegion=%llu "
                          "dst=%d,%d clip=%d,%d,%dx%d mirror=%dx%d "
                          "pitch=%d reason=%s",
                          static_cast<unsigned long long>(failed),
                          static_cast<unsigned long long>(globalRegion),
                          static_cast<unsigned long long>(batchRegion), x, y,
                          clipRect.left, clipRect.top, copyWidth, copyHeight,
                          gSDLSurfaceMirrorState.width,
                          gSDLSurfaceMirrorState.height, surface->pitch,
                          failureReason.c_str());
            TVPNativeLogInfo("sdl-surface", message);
        }
    }

    if(locked)
        SDL_UnlockSurface(surface);

    if(!copied)
        return false;

    tTVPRect updateRect(x, y, x + copyWidth, y + copyHeight);
    if(gSDLSurfaceMirrorState.hasUpdate) {
        gSDLSurfaceMirrorState.updateRect.do_union(updateRect);
    } else {
        gSDLSurfaceMirrorState.updateRect = updateRect;
        gSDLSurfaceMirrorState.hasUpdate = true;
    }

    gSDLSurfaceMirrorState.copiedRegions++;
    gSDLSurfaceMirrorState.copiedBytes +=
        static_cast<uint64_t>(copyWidth) * copyHeight * 4;
    copiedTotal = gSDLSurfaceMirrorState.copiedRegions;
    copiedBytesTotal = gSDLSurfaceMirrorState.copiedBytes;

    if(ShouldLogSurfaceMirrorCopy(copiedTotal)) {
        const tTVPRect &ur = gSDLSurfaceMirrorState.updateRect;
        char message[384];
        std::snprintf(
            message, sizeof(message),
            "copy total=%llu global=%llu batchRegion=%llu dst=%d,%d "
            "size=%dx%d mirror=%dx%d pitch=%d bytesTotal=%llu "
            "update=%d,%d,%dx%d failed=%llu",
            static_cast<unsigned long long>(copiedTotal),
            static_cast<unsigned long long>(globalRegion),
            static_cast<unsigned long long>(batchRegion), x, y, copyWidth,
            copyHeight, gSDLSurfaceMirrorState.width,
            gSDLSurfaceMirrorState.height, surface->pitch,
            static_cast<unsigned long long>(copiedBytesTotal),
            gSDLSurfaceMirrorState.hasUpdate ? ur.left : 0,
            gSDLSurfaceMirrorState.hasUpdate ? ur.top : 0,
            gSDLSurfaceMirrorState.hasUpdate ? ur.get_width() : 0,
            gSDLSurfaceMirrorState.hasUpdate ? ur.get_height() : 0,
            static_cast<unsigned long long>(
                gSDLSurfaceMirrorState.failedCopies));
        TVPNativeLogInfo("sdl-surface", message);
    }

    return true;
}

uint64_t CalculateBacklog(uint64_t queued, uint64_t drained, uint64_t dropped,
                          uint64_t coalesced) {
    const uint64_t completed = drained + dropped + coalesced;
    return queued > completed ? queued - completed : 0;
}

void UpdateAtomicMax(std::atomic_uint64_t &target, uint64_t value) {
    uint64_t previous = target.load(std::memory_order_relaxed);
    while(value > previous &&
          !target.compare_exchange_weak(previous, value,
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
    }
}

void LogSDLInputQueue(const char *message) {
    TVPNativeLogInfo("sdl-inputqueue", message ? message : "");
}

void LogSDLInputQueueF(const char *fmt, uint64_t a = 0, uint64_t b = 0,
                       uint64_t c = 0) {
    char message[256];
    std::snprintf(message, sizeof(message), fmt,
                  static_cast<unsigned long long>(a),
                  static_cast<unsigned long long>(b),
                  static_cast<unsigned long long>(c));
    LogSDLInputQueue(message);
}

bool EnsureSDLInputQueue() {
    std::call_once(gSDLInputQueueInitOnce, []() {
        if(!TVPSDLInitializeRuntime()) {
            std::string message = "init events failed";
            if(!gSDLRuntimeError.empty()) {
                message += ": " + gSDLRuntimeError;
            }
            LogSDLInputQueue(message.c_str());
            return;
        }

        const Uint32 eventType = SDL_RegisterEvents(1);
        if(eventType == 0) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "register custom event failed: %s",
                          SDL_GetError());
            LogSDLInputQueue(message);
            return;
        }

        gSDLInputQueueEventType = eventType;
        gSDLInputQueueReady.store(true, std::memory_order_release);

        char message[128];
        std::snprintf(message, sizeof(message),
                      "ready custom_event_type=%u",
                      static_cast<unsigned>(eventType));
        LogSDLInputQueue(message);
    });

    return gSDLInputQueueReady.load(std::memory_order_acquire);
}

void QueueAndroidInputEvent(const char *eventName, int itemCount, float x,
                            float y, int code, bool state,
                            bool dispatchToTVP) {
    if(gSDLInputPaused.load(std::memory_order_acquire) &&
       IsSDLUITouchInput(eventName)) {
        const uint64_t dropped =
            gSDLInputDropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if(ShouldLogInputQueueEvent(dropped, eventName)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "paused-drop dropped=%llu event=%s count=%d "
                          "x=%.2f y=%.2f code=%d",
                          static_cast<unsigned long long>(dropped),
                          eventName ? eventName : "", itemCount, x, y,
                          code);
            LogSDLInputQueue(message);
        }
        return;
    }

    if(IsSDLDirectTouchInput(eventName, dispatchToTVP) &&
       !TVPSDLHasScreenPresenterPresented()) {
        const uint64_t queuedCount =
            gSDLInputQueued.fetch_add(1, std::memory_order_relaxed) + 1;
        const uint64_t dropped =
            gSDLInputDropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if(ShouldLogInputQueueEvent(queuedCount, eventName)) {
            char message[320];
            std::snprintf(
                message, sizeof(message),
                "prepresent-drop queued=%llu dropped=%llu event=%s count=%d "
                "x=%.2f y=%.2f code=%d state=%d",
                static_cast<unsigned long long>(queuedCount),
                static_cast<unsigned long long>(dropped),
                eventName ? eventName : "", itemCount, x, y, code,
                state ? 1 : 0);
            LogSDLInputQueue(message);
        }
        return;
    }

    if(!EnsureSDLInputQueue()) {
        const uint64_t dropped =
            gSDLInputDropped.fetch_add(1, std::memory_order_relaxed) + 1;
        if(ShouldLogInputQueueSequence(dropped)) {
            LogSDLInputQueueF("queue-unavailable dropped=%llu drained=%llu queued=%llu",
                              dropped,
                              gSDLInputDrained.load(std::memory_order_relaxed),
                              gSDLInputQueued.load(std::memory_order_relaxed));
        }
        return;
    }

    if(IsSDLDirectTouchMoveInput(eventName, dispatchToTVP)) {
        const uint64_t queuedBefore =
            gSDLInputQueued.load(std::memory_order_relaxed);
        const uint64_t drained =
            gSDLInputDrained.load(std::memory_order_relaxed);
        const uint64_t droppedBefore =
            gSDLInputDropped.load(std::memory_order_relaxed);
        const uint64_t coalesced =
            gSDLInputCoalesced.load(std::memory_order_relaxed);
        const uint64_t backlog = CalculateBacklog(
            queuedBefore, drained, droppedBefore, coalesced);
        if(backlog >= kSDLDirectTouchMoveBacklogLimit) {
            const uint64_t queuedCount =
                gSDLInputQueued.fetch_add(1, std::memory_order_relaxed) + 1;
            const uint64_t dropped =
                gSDLInputDropped.fetch_add(1, std::memory_order_relaxed) + 1;
            UpdateAtomicMax(gSDLInputMaxBacklog, backlog);
            if(ShouldLogInputQueueSequence(queuedCount)) {
                char message[320];
                std::snprintf(
                    message, sizeof(message),
                    "move-backpressure-drop queued=%llu backlog=%llu "
                    "limit=%llu drained=%llu dropped=%llu x=%.2f y=%.2f "
                    "code=%d",
                    static_cast<unsigned long long>(queuedCount),
                    static_cast<unsigned long long>(backlog),
                    static_cast<unsigned long long>(
                        kSDLDirectTouchMoveBacklogLimit),
                    static_cast<unsigned long long>(drained),
                    static_cast<unsigned long long>(dropped), x, y, code);
                LogSDLInputQueue(message);
            }
            return;
        }
    }

    const uint64_t queuedCount =
        gSDLInputQueued.fetch_add(1, std::memory_order_relaxed) + 1;
    auto *queued = new TVPSDLQueuedInputEvent{
        eventName ? eventName : "", itemCount, x, y, code, state,
        dispatchToTVP, SDL_GetTicks(), queuedCount,
    };

    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = gSDLInputQueueEventType;
    event.user.code = code;
    event.user.data1 = queued;

    if(!SDL_PushEvent(&event)) {
        delete queued;
        const uint64_t dropped =
            gSDLInputDropped.fetch_add(1, std::memory_order_relaxed) + 1;
        char message[256];
        std::snprintf(message, sizeof(message),
                      "push failed event=%s dropped=%llu error=%s",
                      eventName ? eventName : "",
                      static_cast<unsigned long long>(dropped),
                      SDL_GetError());
        LogSDLInputQueue(message);
        return;
    }

    const uint64_t drained = gSDLInputDrained.load(std::memory_order_relaxed);
    const uint64_t dropped = gSDLInputDropped.load(std::memory_order_relaxed);
    const uint64_t coalesced =
        gSDLInputCoalesced.load(std::memory_order_relaxed);
    const uint64_t backlog =
        CalculateBacklog(queuedCount, drained, dropped, coalesced);
    UpdateAtomicMax(gSDLInputMaxBacklog, backlog);

    if(ShouldLogInputQueueEvent(queuedCount, eventName)) {
        char message[320];
        std::snprintf(message, sizeof(message),
                      "queued=%llu backlog=%llu drained=%llu dropped=%llu "
                      "coalesced=%llu event=%s count=%d x=%.2f y=%.2f "
                      "code=%d state=%d",
                      static_cast<unsigned long long>(queuedCount),
                      static_cast<unsigned long long>(backlog),
                      static_cast<unsigned long long>(drained),
                      static_cast<unsigned long long>(dropped),
                      static_cast<unsigned long long>(coalesced),
                      eventName ? eventName : "", itemCount, x, y, code,
                      state ? 1 : 0);
        LogSDLInputQueue(message);
    }
}

tjs_int RoundInputCoord(float value) {
    return static_cast<tjs_int>(std::lround(value));
}

float ClampInputCoord(float value, int limit) {
    if(limit <= 0)
        return value;
    if(value < 0.0f)
        return 0.0f;
    const float maxValue = static_cast<float>(limit - 1);
    if(value > maxValue)
        return maxValue;
    return value;
}

void ClampFlutterTouchToPresentedSurface(float &x, float &y) {
    int width = gSDLPresentedSurfaceWidth.load(std::memory_order_relaxed);
    int height = gSDLPresentedSurfaceHeight.load(std::memory_order_relaxed);
    if(width <= 0 || height <= 0) {
        std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
        width = gSDLSurfaceMirrorState.width;
        height = gSDLSurfaceMirrorState.height;
    }
#if defined(__ANDROID__)
    if(width <= 0 || height <= 0)
        TVPAndroidGetFlutterGameSurfaceSize(&width, &height);
#endif
    x = ClampInputCoord(x, width);
    y = ClampInputCoord(y, height);
}

void UpdateWindowCursor(tTJSNI_Window *window, tjs_int x, tjs_int y) {
    if(!window)
        return;
    if(auto *form = window->GetForm()) {
        form->SetCursorPos(x, y);
    }
}

void PostSDLDirectMouseMove(tTJSNI_Window *window, float x, float y,
                            tjs_uint32 shift, bool discardable) {
    if(!window)
        return;
    const tjs_int ix = RoundInputCoord(x);
    const tjs_int iy = RoundInputCoord(y);
    UpdateWindowCursor(window, ix, iy);
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(window, ix, iy, shift),
                      discardable ? TVP_EPT_DISCARDABLE : TVP_EPT_POST);
}

void PostSDLDirectMouseDown(tTJSNI_Window *window, float x, float y) {
    if(!window)
        return;
    const tjs_int ix = RoundInputCoord(x);
    const tjs_int iy = RoundInputCoord(y);
    UpdateWindowCursor(window, ix, iy);
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
        window, ix, iy, mbLeft, TVP_SS_LEFT));
}

void PostSDLDirectMouseUp(tTJSNI_Window *window, float x, float y) {
    if(!window)
        return;
    const tjs_int ix = RoundInputCoord(x);
    const tjs_int iy = RoundInputCoord(y);
    UpdateWindowCursor(window, ix, iy);
    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(window, ix, iy, mbLeft, 0));
}

void PostSDLDirectClick(tTJSNI_Window *window, float x, float y) {
    if(!window)
        return;
    const tjs_int ix = RoundInputCoord(x);
    const tjs_int iy = RoundInputCoord(y);
    UpdateWindowCursor(window, ix, iy);
    TVPPostInputEvent(new tTVPOnClickInputEvent(window, ix, iy));
}

bool CanDispatchDirectTVPInput() {
    return TVPSDLIsScreenTakeoverEnabled() &&
        TVPSDLHasScreenPresenterPresented() && TVPMainWindow;
}

bool DecodeNextUtf8Codepoint(const unsigned char *&cursor,
                             const unsigned char *end, uint32_t &codepoint) {
    if(cursor >= end)
        return false;

    const unsigned char first = *cursor++;
    if(first < 0x80) {
        codepoint = first;
        return true;
    }

    int extra = 0;
    uint32_t value = 0;
    if((first & 0xe0) == 0xc0) {
        extra = 1;
        value = first & 0x1f;
    } else if((first & 0xf0) == 0xe0) {
        extra = 2;
        value = first & 0x0f;
    } else if((first & 0xf8) == 0xf0) {
        extra = 3;
        value = first & 0x07;
    } else {
        codepoint = 0xfffd;
        return true;
    }

    if(end - cursor < extra) {
        cursor = end;
        codepoint = 0xfffd;
        return true;
    }

    for(int index = 0; index < extra; ++index) {
        const unsigned char next = *cursor;
        if((next & 0xc0) != 0x80) {
            codepoint = 0xfffd;
            return true;
        }
        cursor++;
        value = (value << 6) | (next & 0x3f);
    }

    if((extra == 1 && value < 0x80) || (extra == 2 && value < 0x800) ||
       (extra == 3 && value < 0x10000) || value > 0x10ffff ||
       (value >= 0xd800 && value <= 0xdfff)) {
        codepoint = 0xfffd;
        return true;
    }

    codepoint = value;
    return true;
}

std::vector<tjs_char> DecodeUtf8ToTJSChars(const char *text) {
    std::vector<tjs_char> chars;
    if(!text || !*text)
        return chars;

    const auto *cursor = reinterpret_cast<const unsigned char *>(text);
    const auto *end = cursor + std::strlen(text);
    while(cursor < end) {
        uint32_t codepoint = 0;
        if(!DecodeNextUtf8Codepoint(cursor, end, codepoint))
            break;
        if(codepoint == 0)
            continue;
        if(codepoint <= 0xffff) {
            chars.push_back(static_cast<tjs_char>(codepoint));
            continue;
        }
        codepoint -= 0x10000;
        chars.push_back(static_cast<tjs_char>(0xd800 + (codepoint >> 10)));
        chars.push_back(static_cast<tjs_char>(0xdc00 + (codepoint & 0x3ff)));
    }
    return chars;
}

bool PostSDLDirectKeyDown(tTJSNI_Window *window, tjs_uint key) {
    if(!window)
        return false;
    TVPPostInputEvent(new tTVPOnKeyDownInputEvent(window, key, 0));
    return true;
}

bool PostSDLDirectKeyUp(tTJSNI_Window *window, tjs_uint key) {
    if(!window)
        return false;
    TVPPostInputEvent(new tTVPOnKeyUpInputEvent(window, key, 0));
    return true;
}

bool PostSDLDirectKeyPress(tTJSNI_Window *window, tjs_char key) {
    if(!window || key == 0)
        return false;
    TVPPostInputEvent(new tTVPOnKeyPressInputEvent(window, key));
    return true;
}

bool PostSDLDirectMouseWheel(tTJSNI_Window *window, float x, float y,
                             float scroll) {
    if(!window || scroll == 0.0f)
        return false;
    const tjs_int ix = RoundInputCoord(x);
    const tjs_int iy = RoundInputCoord(y);
    UpdateWindowCursor(window, ix, iy);
    const tjs_int delta = scroll > 0.0f ? -120 : 120;
    TVPPostInputEvent(
        new tTVPOnMouseWheelInputEvent(window, 0, delta, ix, iy));
    return true;
}

bool MapAndroidKeyCodeToTVPKey(int keyCode, tjs_uint &key) {
    switch(keyCode) {
        case kAndroidKeyBack:
            key = VK_ESCAPE;
            return true;
        case kAndroidKeyDpadUp:
            key = VK_UP;
            return true;
        case kAndroidKeyDpadDown:
            key = VK_DOWN;
            return true;
        case kAndroidKeyDpadLeft:
            key = VK_LEFT;
            return true;
        case kAndroidKeyDpadRight:
            key = VK_RIGHT;
            return true;
        case kAndroidKeyEnter:
        case kAndroidKeyDpadCenter:
            key = VK_RETURN;
            return true;
        case kAndroidKeyDel:
            key = VK_BACK;
            return true;
        case kAndroidKeyMenu:
        case kAndroidKeyPlay:
        default:
            key = 0;
            return false;
    }
}

void MapAndroidViewCoordToPresentedSurface(float &x, float &y) {
    int surfaceWidth = gSDLPresentedSurfaceWidth.load(std::memory_order_relaxed);
    int surfaceHeight =
        gSDLPresentedSurfaceHeight.load(std::memory_order_relaxed);
    int frameWidth = 0;
    int frameHeight = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
        frameWidth = gSDLScreenPresenterState.frameWidth;
        frameHeight = gSDLScreenPresenterState.frameHeight;
    }
    if(surfaceWidth <= 0 || surfaceHeight <= 0) {
        std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
        surfaceWidth = gSDLSurfaceMirrorState.width;
        surfaceHeight = gSDLSurfaceMirrorState.height;
    }
#if defined(__ANDROID__)
    if(surfaceWidth <= 0 || surfaceHeight <= 0)
        TVPAndroidGetFlutterGameSurfaceSize(&surfaceWidth, &surfaceHeight);
#endif
    if(frameWidth > 0 && frameHeight > 0 && surfaceWidth > 0 &&
       surfaceHeight > 0) {
        x = x * static_cast<float>(surfaceWidth) /
            static_cast<float>(frameWidth);
        y = y * static_cast<float>(surfaceHeight) /
            static_cast<float>(frameHeight);
    }
    x = ClampInputCoord(x, surfaceWidth);
    y = ClampInputCoord(y, surfaceHeight);
}

void ResetSDLDirectTouch() {
    gSDLDirectTouchState = {};
    gSDLDirectTouchState.pointerId = -1;
}

void CancelSDLDirectTouchForPointer(int pointerId) {
    if(gSDLDirectTouchState.active &&
       (pointerId < 0 || gSDLDirectTouchState.pointerId == pointerId)) {
        ResetSDLDirectTouch();
    }
}

void DropQueuedAndroidInputEvents(const char *reason) {
    ResetSDLDirectTouch();
    if(!gSDLInputQueueReady.load(std::memory_order_acquire))
        return;

    uint64_t droppedInBatch = 0;
    SDL_Event event;
    std::lock_guard<std::mutex> lock(gSDLInputQueueMutex);
    while(SDL_PeepEvents(&event, 1, SDL_GETEVENT, gSDLInputQueueEventType,
                         gSDLInputQueueEventType) == 1) {
        auto *queued =
            static_cast<TVPSDLQueuedInputEvent *>(event.user.data1);
        delete queued;
        droppedInBatch++;
    }

    if(droppedInBatch == 0)
        return;

    const uint64_t dropped =
        gSDLInputDropped.fetch_add(droppedInBatch,
                                   std::memory_order_relaxed) +
        droppedInBatch;
    const uint64_t backlog = CalculateBacklog(
        gSDLInputQueued.load(std::memory_order_relaxed),
        gSDLInputDrained.load(std::memory_order_relaxed), dropped,
        gSDLInputCoalesced.load(std::memory_order_relaxed));
    char message[320];
    std::snprintf(message, sizeof(message),
                  "lifecycle-drop reason=%s items=%llu dropped=%llu "
                  "backlog=%llu",
                  reason ? reason : "",
                  static_cast<unsigned long long>(droppedInBatch),
                  static_cast<unsigned long long>(dropped),
                  static_cast<unsigned long long>(backlog));
    LogSDLInputQueue(message);
}

void DispatchSDLDirectTouchEvent(const TVPSDLQueuedInputEvent &queued) {
    if(!queued.dispatchToTVP)
        return;

    auto *window = TVPMainWindow;
    if(!window)
        return;

    constexpr float moveThreshold = 24.0f;
    constexpr float moveThresholdSq = moveThreshold * moveThreshold;
    constexpr Uint64 holdToDragMs = 150;
    const Uint64 nowTicks = SDL_GetTicks();

    if(queued.eventName == "touch-begin") {
        gSDLDirectTouchState.active = true;
        gSDLDirectTouchState.moved = false;
        gSDLDirectTouchState.mouseDownSent = false;
        gSDLDirectTouchState.pointerId = queued.code;
        gSDLDirectTouchState.startX = queued.x;
        gSDLDirectTouchState.startY = queued.y;
        gSDLDirectTouchState.lastX = queued.x;
        gSDLDirectTouchState.lastY = queued.y;
        gSDLDirectTouchState.downTicks = nowTicks;
        return;
    }

    if(queued.eventName == "touch-cancel" ||
       queued.eventName == "touch-cancel-empty") {
        if(gSDLDirectTouchState.active &&
           (queued.code < 0 || gSDLDirectTouchState.pointerId == queued.code)) {
            if(gSDLDirectTouchState.mouseDownSent) {
                PostSDLDirectMouseUp(window, gSDLDirectTouchState.lastX,
                                     gSDLDirectTouchState.lastY);
            }
            ResetSDLDirectTouch();
        }
        return;
    }

    if(!gSDLDirectTouchState.active ||
       gSDLDirectTouchState.pointerId != queued.code)
        return;

    if(queued.eventName == "touch-move") {
        gSDLDirectTouchState.lastX = queued.x;
        gSDLDirectTouchState.lastY = queued.y;

        const float dx = queued.x - gSDLDirectTouchState.startX;
        const float dy = queued.y - gSDLDirectTouchState.startY;
        const bool shouldStartDrag =
            gSDLDirectTouchState.mouseDownSent ||
            (dx * dx + dy * dy > moveThresholdSq) ||
            (nowTicks - gSDLDirectTouchState.downTicks > holdToDragMs);

        if(!shouldStartDrag)
            return;

        if(!gSDLDirectTouchState.mouseDownSent) {
            PostSDLDirectMouseMove(window, gSDLDirectTouchState.startX,
                                   gSDLDirectTouchState.startY, 0, false);
            PostSDLDirectMouseDown(window, gSDLDirectTouchState.startX,
                                   gSDLDirectTouchState.startY);
            gSDLDirectTouchState.mouseDownSent = true;
        }

        gSDLDirectTouchState.moved = true;
        PostSDLDirectMouseMove(window, queued.x, queued.y, TVP_SS_LEFT, true);
        return;
    }

    if(queued.eventName == "touch-end") {
        if(gSDLDirectTouchState.mouseDownSent) {
            PostSDLDirectMouseUp(window, queued.x, queued.y);
        } else {
            PostSDLDirectMouseMove(window, queued.x, queued.y, 0, false);
            PostSDLDirectMouseDown(window, gSDLDirectTouchState.startX,
                                   gSDLDirectTouchState.startY);
            PostSDLDirectClick(window, queued.x, queued.y);
            PostSDLDirectMouseUp(window, queued.x, queued.y);
        }
        ResetSDLDirectTouch();
        return;
    }
}

bool IsQueuedDirectTouchMove(const TVPSDLQueuedInputEvent *queued, int code) {
    return queued &&
        IsSDLDirectTouchMoveInput(queued->eventName.c_str(),
                                  queued->dispatchToTVP) &&
        queued->code == code;
}

TVPSDLQueuedInputEvent *CoalescePendingDirectTouchMoves(
    TVPSDLQueuedInputEvent *queued,
    std::deque<TVPSDLQueuedInputEvent *> &pending,
    uint64_t &coalescedInBatch) {
    if(!IsQueuedDirectTouchMove(queued, queued ? queued->code : -1))
        return queued;

    while(!pending.empty()) {
        auto *nextQueued = pending.front();
        if(!IsQueuedDirectTouchMove(nextQueued, queued->code))
            return queued;

        pending.pop_front();
        delete queued;
        queued = nextQueued;
        coalescedInBatch++;
    }

    return queued;
}

} // namespace

bool TVPSDLInitializeRuntime() {
    std::call_once(gSDLRuntimeInitOnce, []() {
        SDL_SetMainReady();
#if defined(SDL_HINT_TOUCH_MOUSE_EVENTS)
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
#endif
#if defined(SDL_HINT_MOUSE_TOUCH_EVENTS)
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
        if(SDL_InitSubSystem(SDL_INIT_EVENTS)) {
            gSDLRuntimeInitialized = true;
        } else {
            gSDLRuntimeInitialized = false;
            gSDLRuntimeError = SafeSDLString(SDL_GetError());
        }
    });
    return gSDLRuntimeInitialized;
}

TVPSDLRuntimeInfo TVPSDLGetRuntimeInfo() {
    TVPSDLRuntimeInfo info;

    info.compiledVersion = FormatVersion(SDL_VERSION);
    info.linkedVersion = FormatVersion(SDL_GetVersion());
    info.revision = SafeSDLString(SDL_GetRevision());
    info.platform = SafeSDLString(SDL_GetPlatform());
    info.videoDriver = SafeSDLString(SDL_GetCurrentVideoDriver());
    info.audioDriver = SafeSDLString(SDL_GetCurrentAudioDriver());

    const Uint32 initialized = SDL_WasInit(0);
    info.eventsReady = (initialized & SDL_INIT_EVENTS) != 0;
    info.videoReady = (initialized & SDL_INIT_VIDEO) != 0;
    info.audioReady = (initialized & SDL_INIT_AUDIO) != 0;
    return info;
}

void TVPSDLRecordAndroidLifecycle(const char *eventName, const char *detail) {
    if(IsAndroidLifecycleResumeEvent(eventName)) {
        const bool wasPaused =
            gSDLInputPaused.exchange(false, std::memory_order_acq_rel);
        if(wasPaused)
            LogSDLInputQueue("lifecycle-resume input enabled");
    } else if(IsAndroidLifecyclePauseEvent(eventName)) {
        gSDLInputPaused.store(true, std::memory_order_release);
        DropQueuedAndroidInputEvents(eventName);
    }

    const uint64_t sequence =
        gSDLLifecycleEventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const Uint32 initialized = SDL_WasInit(0);

    char message[384];
    std::snprintf(
        message, sizeof(message),
        "#%llu event=%s detail=%s events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        eventName ? eventName : "", detail ? detail : "",
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-lifecycle", message);
}

void TVPSDLRecordAndroidInput(const char *eventName, int itemCount, float x,
                              float y, int code, bool state) {
    TVPSDLInitializeRuntime();
    if(!gSDLInputPaused.load(std::memory_order_acquire) &&
       IsSDLUITouchInput(eventName)) {
        TVPSDLUIRecordAndroidTouch(eventName, x, y,
                                   itemCount > 0 ? code : -1, state);
    }
    QueueAndroidInputEvent(eventName, itemCount, x, y, code, state, false);
    const uint64_t sequence =
        gSDLInputEventSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if(!ShouldLogInputEvent(sequence, eventName))
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[256];
    std::snprintf(
        message, sizeof(message),
        "#%llu event=%s count=%d x=%.2f y=%.2f code=%d state=%d events=%d ticks=%u",
        static_cast<unsigned long long>(sequence),
        eventName ? eventName : "", itemCount, x, y, code, state ? 1 : 0,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-input", message);
}

void TVPSDLQueueFlutterTouchBegin(int id, float x, float y) {
    TVPSDLInitializeRuntime();
    ClampFlutterTouchToPresentedSurface(x, y);
    QueueAndroidInputEvent("touch-begin", 1, x, y, id, true, true);
}

void TVPSDLQueueFlutterTouchEnd(int id, float x, float y) {
    TVPSDLInitializeRuntime();
    ClampFlutterTouchToPresentedSurface(x, y);
    QueueAndroidInputEvent("touch-end", 1, x, y, id, false, true);
}

void TVPSDLQueueFlutterTouchMove(int count, const int *ids, const float *xs,
                                 const float *ys) {
    TVPSDLInitializeRuntime();
    if(count <= 0 || !ids || !xs || !ys) {
        QueueAndroidInputEvent("touch-move-empty", 0, 0.0f, 0.0f, -1,
                               false, false);
        return;
    }
    float x = xs[0];
    float y = ys[0];
    ClampFlutterTouchToPresentedSurface(x, y);
    QueueAndroidInputEvent("touch-move", count, x, y, ids[0], true, true);
}

void TVPSDLQueueFlutterTouchCancel(int count, const int *ids, const float *xs,
                                   const float *ys) {
    TVPSDLInitializeRuntime();
    if(count <= 0 || !ids || !xs || !ys) {
        QueueAndroidInputEvent("touch-cancel-empty", 0, 0.0f, 0.0f, -1,
                               false, true);
        return;
    }
    float x = xs[0];
    float y = ys[0];
    ClampFlutterTouchToPresentedSurface(x, y);
    QueueAndroidInputEvent("touch-cancel", count, x, y, ids[0], false, true);
}

bool TVPSDLDispatchCharInput(int keyCode) {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;
    return PostSDLDirectKeyPress(TVPMainWindow, static_cast<tjs_char>(keyCode));
}

bool TVPSDLDispatchTextInput(const char *text) {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;

    const std::vector<tjs_char> chars = DecodeUtf8ToTJSChars(text);
    if(chars.empty())
        return false;

    for(const tjs_char key : chars)
        PostSDLDirectKeyPress(TVPMainWindow, key);
    return true;
}

bool TVPSDLDispatchDeleteBackward() {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;
    const bool down = PostSDLDirectKeyDown(TVPMainWindow, VK_BACK);
    const bool up = PostSDLDirectKeyUp(TVPMainWindow, VK_BACK);
    return down && up;
}

bool TVPSDLDispatchAndroidKeyAction(int keyCode, bool isPress) {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;

    tjs_uint key = 0;
    if(!MapAndroidKeyCodeToTVPKey(keyCode, key))
        return false;
    return isPress ? PostSDLDirectKeyDown(TVPMainWindow, key)
                   : PostSDLDirectKeyUp(TVPMainWindow, key);
}

bool TVPSDLDispatchAndroidHoverMove(float x, float y) {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;
    MapAndroidViewCoordToPresentedSurface(x, y);
    PostSDLDirectMouseMove(TVPMainWindow, x, y, 0, true);
    return true;
}

bool TVPSDLDispatchAndroidMouseScroll(float x, float y, float scroll) {
    TVPSDLInitializeRuntime();
    if(!CanDispatchDirectTVPInput())
        return false;
    MapAndroidViewCoordToPresentedSurface(x, y);
    return PostSDLDirectMouseWheel(TVPMainWindow, x, y, scroll);
}

void TVPSDLProcessAndroidInputQueue() {
    if(!gSDLInputQueueReady.load(std::memory_order_acquire))
        return;

    std::lock_guard<std::mutex> lock(gSDLInputQueueMutex);
    uint64_t drainedInBatch = 0;
    uint64_t droppedInBatch = 0;
    uint64_t coalescedInBatch = 0;
    uint64_t maxAgeInBatch = 0;
    uint64_t lastSequence = 0;
    std::string lastEventName;
    std::deque<TVPSDLQueuedInputEvent *> pending;

    SDL_Event event;
    while(SDL_PeepEvents(&event, 1, SDL_GETEVENT, gSDLInputQueueEventType,
                         gSDLInputQueueEventType) == 1) {
        auto *queued =
            static_cast<TVPSDLQueuedInputEvent *>(event.user.data1);
        if(!queued) {
            droppedInBatch++;
            continue;
        }
        pending.push_back(queued);
    }

    while(!pending.empty()) {
        auto *queued = pending.front();
        pending.pop_front();
        queued = CoalescePendingDirectTouchMoves(queued, pending,
                                                 coalescedInBatch);

        const uint64_t age = SDL_GetTicks() - queued->ticks;
        if(age > maxAgeInBatch)
            maxAgeInBatch = age;
        lastEventName = queued->eventName;
        lastSequence = queued->sequence;
        const bool directMove = IsSDLDirectTouchMoveInput(
            queued->eventName.c_str(), queued->dispatchToTVP);
        const bool dropDirectTouch =
            directMove &&
            (!TVPSDLHasScreenPresenterPresented() ||
             age > kSDLStaleDirectTouchAgeMs);
        if(dropDirectTouch) {
            CancelSDLDirectTouchForPointer(queued->code);
            delete queued;
            droppedInBatch++;
            continue;
        }
        DispatchSDLDirectTouchEvent(*queued);
        delete queued;
        drainedInBatch++;
    }

    if(drainedInBatch == 0 && droppedInBatch == 0)
        return;

    const uint64_t drained =
        gSDLInputDrained.fetch_add(drainedInBatch,
                                   std::memory_order_relaxed) +
        drainedInBatch;
    const uint64_t dropped =
        gSDLInputDropped.fetch_add(droppedInBatch,
                                   std::memory_order_relaxed) +
        droppedInBatch;
    const uint64_t coalesced =
        gSDLInputCoalesced.fetch_add(coalescedInBatch,
                                     std::memory_order_relaxed) +
        coalescedInBatch;
    const uint64_t batch =
        gSDLInputBatches.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t backlog = CalculateBacklog(
        gSDLInputQueued.load(std::memory_order_relaxed), drained, dropped,
        coalesced);
    UpdateAtomicMax(gSDLInputMaxBacklog, backlog);
    UpdateAtomicMax(gSDLInputMaxAgeMs, maxAgeInBatch);

    if(ShouldLogInputQueueSequence(batch) || droppedInBatch > 0 ||
       coalescedInBatch > 0 || maxAgeInBatch > 50) {
        char message[384];
        std::snprintf(
            message, sizeof(message),
            "batch=%llu items=%llu drained=%llu dropped=%llu "
            "coalesced=%llu backlog=%llu maxAgeMs=%llu maxBacklog=%llu "
            "maxSeenAgeMs=%llu lastSeq=%llu last=%s",
            static_cast<unsigned long long>(batch),
            static_cast<unsigned long long>(drainedInBatch),
            static_cast<unsigned long long>(drained),
            static_cast<unsigned long long>(dropped),
            static_cast<unsigned long long>(coalesced),
            static_cast<unsigned long long>(backlog),
            static_cast<unsigned long long>(maxAgeInBatch),
            static_cast<unsigned long long>(
                gSDLInputMaxBacklog.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                gSDLInputMaxAgeMs.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(lastSequence),
            lastEventName.c_str());
        LogSDLInputQueue(message);
    }
}

void TVPSDLRecordRenderFrame(int layerWidth, int layerHeight,
                             int internalWidth, int internalHeight,
                             bool textureChanged, const void *sourceTexture,
                             const void *currentTexture,
                             const void *newTexture) {
    if(!IsSDLRenderDiagnosticsActive())
        return;

    TVPSDLInitializeRuntime();
    const uint64_t frame =
        gSDLRenderFrameSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t changes = textureChanged
        ? gSDLRenderTextureChanges.fetch_add(1, std::memory_order_relaxed) + 1
        : gSDLRenderTextureChanges.load(std::memory_order_relaxed);

    if(frame > 6 && !textureChanged && (frame % 256) != 0)
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(
        message, sizeof(message),
        "frame=%llu changed=%d changes=%llu layer=%dx%d internal=%dx%d "
        "src=%p current=%p next=%p events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(frame), textureChanged ? 1 : 0,
        static_cast<unsigned long long>(changes), layerWidth, layerHeight,
        internalWidth, internalHeight, sourceTexture, currentTexture,
        newTexture, (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-renderprobe", message);
}

void TVPSDLRecordPresenterFrame(iTVPTexture2D *texture, const char *stage,
                                int layerWidth, int layerHeight) {
    if(!IsSDLRenderDiagnosticsActive())
        return;

    TVPSDLInitializeRuntime();
    const uint64_t frame =
        gSDLPresenterFrameSequence.fetch_add(1, std::memory_order_relaxed) + 1;

    if(!texture) {
        if(ShouldLogPresenterFrame(frame)) {
            char message[192];
            std::snprintf(message, sizeof(message),
                          "frame=%llu stage=%s texture=null layer=%dx%d",
                          static_cast<unsigned long long>(frame),
                          stage ? stage : "", layerWidth, layerHeight);
            TVPNativeLogInfo("sdl-presenter", message);
        }
        return;
    }

    bool metadataOk = true;
    int width = 0;
    int height = 0;
    int internalWidth = 0;
    int internalHeight = 0;
    int pitch = 0;
    unsigned int glTexture = 0;
    TVPTextureFormat::e format = TVPTextureFormat::None;

    try {
        width = static_cast<int>(texture->GetWidth());
        height = static_cast<int>(texture->GetHeight());
        internalWidth = static_cast<int>(texture->GetInternalWidth());
        internalHeight = static_cast<int>(texture->GetInternalHeight());
        format = texture->GetFormat();
        pitch = static_cast<int>(texture->GetPitch());
        glTexture = texture->GetNativeGLTextureId();
    } catch(...) {
        metadataOk = false;
    }

    bool textureChanged = false;
    {
        std::lock_guard<std::mutex> lock(gSDLPresenterProbeMutex);
        textureChanged =
            gSDLPresenterProbeState.texture != texture ||
            gSDLPresenterProbeState.width != width ||
            gSDLPresenterProbeState.height != height ||
            gSDLPresenterProbeState.internalWidth != internalWidth ||
            gSDLPresenterProbeState.internalHeight != internalHeight ||
            gSDLPresenterProbeState.format != static_cast<int>(format) ||
            gSDLPresenterProbeState.pitch != pitch ||
            gSDLPresenterProbeState.glTexture != glTexture;
        if(textureChanged) {
            gSDLPresenterProbeState.texture = texture;
            gSDLPresenterProbeState.width = width;
            gSDLPresenterProbeState.height = height;
            gSDLPresenterProbeState.internalWidth = internalWidth;
            gSDLPresenterProbeState.internalHeight = internalHeight;
            gSDLPresenterProbeState.format = static_cast<int>(format);
            gSDLPresenterProbeState.pitch = pitch;
            gSDLPresenterProbeState.glTexture = glTexture;
        }
    }

    const uint64_t changes = textureChanged
        ? gSDLPresenterTextureChanges.fetch_add(1,
                                                std::memory_order_relaxed) +
              1
        : gSDLPresenterTextureChanges.load(std::memory_order_relaxed);

    if(!ShouldLogPresenterFrame(frame) && !textureChanged && metadataOk)
        return;

    const bool glBacked = glTexture != 0;
    bool cpuProbeAttempted = false;
    bool cpuAccessible = false;
    bool cpuProbeFailed = false;
    const void *line0 = nullptr;

    if(metadataOk && !glBacked && height > 0 &&
       IsDirectCpuProbeFormat(format)) {
        cpuProbeAttempted = true;
        gSDLPresenterCpuProbeAttempts.fetch_add(1, std::memory_order_relaxed);
        try {
            line0 = texture->GetScanLineForRead(0);
            cpuAccessible = line0 != nullptr;
            if(cpuAccessible) {
                gSDLPresenterCpuAccessible.fetch_add(
                    1, std::memory_order_relaxed);
            }
        } catch(...) {
            cpuProbeFailed = true;
        }
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[640];
    std::snprintf(
        message, sizeof(message),
        "frame=%llu stage=%s changed=%d changes=%llu tex=%p layer=%dx%d "
        "size=%dx%d internal=%dx%d format=%s(%d) pitch=%d gl=%u "
        "cpuAttempt=%d cpuOk=%d cpuFail=%d cpuAttempts=%llu cpuOkTotal=%llu "
        "line0=%p metadataOk=%d events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(frame), stage ? stage : "",
        textureChanged ? 1 : 0, static_cast<unsigned long long>(changes),
        static_cast<void *>(texture), layerWidth, layerHeight, width, height,
        internalWidth, internalHeight, TextureFormatName(format),
        static_cast<int>(format), pitch, glTexture, cpuProbeAttempted ? 1 : 0,
        cpuAccessible ? 1 : 0, cpuProbeFailed ? 1 : 0,
        static_cast<unsigned long long>(
            gSDLPresenterCpuProbeAttempts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            gSDLPresenterCpuAccessible.load(std::memory_order_relaxed)),
        line0, metadataOk ? 1 : 0,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-presenter", message);
}

void TVPSDLRecordBitmapCompletionStart(iTVPLayerManager *manager,
                                       int sourceWidth, int sourceHeight,
                                       int destWidth, int destHeight) {
    if(!IsSDLRenderDiagnosticsActive())
        return;

    TVPSDLInitializeRuntime();
    const uint64_t batch =
        gSDLBitmapCompletionBatchSequence.fetch_add(
            1, std::memory_order_relaxed) +
        1;

    {
        std::lock_guard<std::mutex> lock(gSDLBitmapCompletionMutex);
        gSDLBitmapCompletionState.active = true;
        gSDLBitmapCompletionState.batch = batch;
        gSDLBitmapCompletionState.regions = 0;
        gSDLBitmapCompletionState.copyReady = 0;
        gSDLBitmapCompletionState.surfaceCopied = 0;
        gSDLBitmapCompletionState.surfaceSkipped = 0;
        gSDLBitmapCompletionState.glBacked = 0;
        gSDLBitmapCompletionState.outOfBounds = 0;
        gSDLBitmapCompletionState.manager = manager;
        gSDLBitmapCompletionState.sourceWidth = sourceWidth;
        gSDLBitmapCompletionState.sourceHeight = sourceHeight;
        gSDLBitmapCompletionState.destWidth = destWidth;
        gSDLBitmapCompletionState.destHeight = destHeight;
        gSDLBitmapCompletionState.hasUnion = false;
        gSDLBitmapCompletionState.unionRect.clear();
    }

    if(ShouldLogBitmapCompletionBatch(batch)) {
        const Uint32 initialized = SDL_WasInit(0);
        char message[256];
        std::snprintf(
            message, sizeof(message),
            "start batch=%llu manager=%p src=%dx%d dest=%dx%d events=%d "
            "video=%d audio=%d ticks=%u",
            static_cast<unsigned long long>(batch),
            static_cast<void *>(manager), sourceWidth, sourceHeight, destWidth,
            destHeight,
            (initialized & SDL_INIT_EVENTS) ? 1 : 0,
            (initialized & SDL_INIT_VIDEO) ? 1 : 0,
            (initialized & SDL_INIT_AUDIO) ? 1 : 0,
            static_cast<unsigned>(SDL_GetTicks()));
        TVPNativeLogInfo("sdl-bitmap", message);
    }
}

void TVPSDLRecordBitmapCompletionRegion(iTVPLayerManager *manager, int x,
                                        int y, tTVPBaseTexture *bitmap,
                                        const tTVPRect &clipRect, int type,
                                        int opacity, int sourceWidth,
                                        int sourceHeight) {
    if(!IsSDLRenderDiagnosticsActive())
        return;

    TVPSDLInitializeRuntime();
    const uint64_t globalRegion =
        gSDLBitmapCompletionRegionSequence.fetch_add(
            1, std::memory_order_relaxed) +
        1;

    int bitmapWidth = 0;
    int bitmapHeight = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    int internalWidth = 0;
    int internalHeight = 0;
    int pitch = 0;
    unsigned int glTexture = 0;
    TVPTextureFormat::e format = TVPTextureFormat::None;
    iTVPTexture2D *texture = nullptr;
    bool metadataOk = true;

    if(bitmap) {
        try {
            bitmapWidth = static_cast<int>(bitmap->GetWidth());
            bitmapHeight = static_cast<int>(bitmap->GetHeight());
            texture = bitmap->GetTexture();
            if(texture) {
                textureWidth = static_cast<int>(texture->GetWidth());
                textureHeight = static_cast<int>(texture->GetHeight());
                internalWidth =
                    static_cast<int>(texture->GetInternalWidth());
                internalHeight =
                    static_cast<int>(texture->GetInternalHeight());
                format = texture->GetFormat();
                pitch = static_cast<int>(texture->GetPitch());
                glTexture = texture->GetNativeGLTextureId();
            }
        } catch(...) {
            metadataOk = false;
        }
    }

    const bool inBounds =
        metadataOk && bitmap != nullptr &&
        IsBitmapCompletionInBounds(x, y, clipRect, sourceWidth, sourceHeight,
                                   bitmapWidth, bitmapHeight);
    const bool glBacked = glTexture != 0;
    const bool copyReady =
        inBounds && texture && !glBacked && IsDirectCpuProbeFormat(format);

    uint64_t batch = 0;
    uint64_t batchRegion = 0;
    uint64_t batchCopyReady = 0;
    uint64_t batchSurfaceCopied = 0;
    uint64_t batchSurfaceSkipped = 0;
    uint64_t batchGlBacked = 0;
    uint64_t batchOutOfBounds = 0;
    bool shouldLog = false;
    tTVPRect unionRect;
    bool hasUnion = false;

    {
        std::lock_guard<std::mutex> lock(gSDLBitmapCompletionMutex);
        if(!gSDLBitmapCompletionState.active ||
           gSDLBitmapCompletionState.manager != manager) {
            gSDLBitmapCompletionState.active = true;
            gSDLBitmapCompletionState.batch = 0;
            gSDLBitmapCompletionState.regions = 0;
            gSDLBitmapCompletionState.copyReady = 0;
            gSDLBitmapCompletionState.surfaceCopied = 0;
            gSDLBitmapCompletionState.surfaceSkipped = 0;
            gSDLBitmapCompletionState.glBacked = 0;
            gSDLBitmapCompletionState.outOfBounds = 0;
            gSDLBitmapCompletionState.manager = manager;
            gSDLBitmapCompletionState.sourceWidth = sourceWidth;
            gSDLBitmapCompletionState.sourceHeight = sourceHeight;
            gSDLBitmapCompletionState.destWidth = 0;
            gSDLBitmapCompletionState.destHeight = 0;
            gSDLBitmapCompletionState.hasUnion = false;
            gSDLBitmapCompletionState.unionRect.clear();
        }

        gSDLBitmapCompletionState.regions++;
        if(copyReady)
            gSDLBitmapCompletionState.copyReady++;
        if(glBacked)
            gSDLBitmapCompletionState.glBacked++;
        if(!inBounds)
            gSDLBitmapCompletionState.outOfBounds++;

        tTVPRect dstRect(x, y, x + clipRect.get_width(),
                         y + clipRect.get_height());
        if(!dstRect.is_empty()) {
            if(gSDLBitmapCompletionState.hasUnion) {
                gSDLBitmapCompletionState.unionRect.do_union(dstRect);
            } else {
                gSDLBitmapCompletionState.unionRect = dstRect;
                gSDLBitmapCompletionState.hasUnion = true;
            }
        }

        batch = gSDLBitmapCompletionState.batch;
        batchRegion = gSDLBitmapCompletionState.regions;
        batchCopyReady = gSDLBitmapCompletionState.copyReady;
        batchSurfaceCopied = gSDLBitmapCompletionState.surfaceCopied;
        batchSurfaceSkipped = gSDLBitmapCompletionState.surfaceSkipped;
        batchGlBacked = gSDLBitmapCompletionState.glBacked;
        batchOutOfBounds = gSDLBitmapCompletionState.outOfBounds;
        unionRect = gSDLBitmapCompletionState.unionRect;
        hasUnion = gSDLBitmapCompletionState.hasUnion;
    }

    uint64_t surfaceCopiedTotal = 0;
    uint64_t surfaceCopiedBytesTotal = 0;
    uint64_t surfaceSkippedTotal = 0;
    const bool surfaceMirrorWanted = IsSDLSurfaceMirrorConsumerActive();
    const bool surfaceCopied = surfaceMirrorWanted && copyReady &&
        CopyRegionToSDLSurfaceMirror(texture, clipRect, x, y, sourceWidth,
                                     sourceHeight, format, globalRegion,
                                     batchRegion, surfaceCopiedTotal,
                                     surfaceCopiedBytesTotal,
                                     surfaceSkippedTotal);
    const bool surfaceSkipped =
        surfaceMirrorWanted && copyReady && !surfaceCopied;

    {
        std::lock_guard<std::mutex> lock(gSDLBitmapCompletionMutex);
        if(gSDLBitmapCompletionState.manager == manager) {
            if(surfaceCopied)
                gSDLBitmapCompletionState.surfaceCopied++;
            if(surfaceSkipped)
                gSDLBitmapCompletionState.surfaceSkipped++;

            batchSurfaceCopied = gSDLBitmapCompletionState.surfaceCopied;
            batchSurfaceSkipped = gSDLBitmapCompletionState.surfaceSkipped;
            batchCopyReady = gSDLBitmapCompletionState.copyReady;
            batchGlBacked = gSDLBitmapCompletionState.glBacked;
            batchOutOfBounds = gSDLBitmapCompletionState.outOfBounds;
            unionRect = gSDLBitmapCompletionState.unionRect;
            hasUnion = gSDLBitmapCompletionState.hasUnion;
        }
        shouldLog = ShouldLogBitmapCompletionRegion(globalRegion, batchRegion) ||
            !inBounds || surfaceSkipped;
    }

    if(!shouldLog)
        return;

    char message[768];
    std::snprintf(
        message, sizeof(message),
        "region global=%llu batch=%llu batchRegion=%llu manager=%p dst=%d,%d "
        "clip=%d,%d,%dx%d src=%dx%d bmp=%p bmpSize=%dx%d tex=%p "
        "texSize=%dx%d internal=%dx%d format=%s(%d) pitch=%d gl=%u "
        "type=%d opacity=%d inBounds=%d copyReady=%d surfaceCopied=%d "
        "copyReadyBatch=%llu surfaceBatch=%llu "
        "surfaceSkipBatch=%llu "
        "surfaceTotal=%llu surfaceBytes=%llu glBatch=%llu outBatch=%llu "
        "union=%d,%d,%dx%d metadataOk=%d",
        static_cast<unsigned long long>(globalRegion),
        static_cast<unsigned long long>(batch),
        static_cast<unsigned long long>(batchRegion),
        static_cast<void *>(manager), x, y, clipRect.left, clipRect.top,
        clipRect.get_width(), clipRect.get_height(), sourceWidth,
        sourceHeight, static_cast<void *>(bitmap), bitmapWidth, bitmapHeight,
        static_cast<void *>(texture), textureWidth, textureHeight,
        internalWidth, internalHeight, TextureFormatName(format),
        static_cast<int>(format), pitch, glTexture, type, opacity,
        inBounds ? 1 : 0,
        copyReady ? 1 : 0,
        surfaceCopied ? 1 : 0,
        static_cast<unsigned long long>(batchCopyReady),
        static_cast<unsigned long long>(batchSurfaceCopied),
        static_cast<unsigned long long>(batchSurfaceSkipped),
        static_cast<unsigned long long>(surfaceCopiedTotal),
        static_cast<unsigned long long>(surfaceCopiedBytesTotal),
        static_cast<unsigned long long>(batchGlBacked),
        static_cast<unsigned long long>(batchOutOfBounds),
        hasUnion ? unionRect.left : 0, hasUnion ? unionRect.top : 0,
        hasUnion ? unionRect.get_width() : 0,
        hasUnion ? unionRect.get_height() : 0, metadataOk ? 1 : 0);
    TVPNativeLogInfo("sdl-bitmap", message);
}

void TVPSDLRecordBitmapCompletionEnd(iTVPLayerManager *manager,
                                     int sourceWidth, int sourceHeight) {
    if(!IsSDLRenderDiagnosticsActive())
        return;

    TVPSDLInitializeRuntime();

    uint64_t batch = 0;
    uint64_t regions = 0;
    uint64_t copyReady = 0;
    uint64_t surfaceCopied = 0;
    uint64_t surfaceSkipped = 0;
    uint64_t glBacked = 0;
    uint64_t outOfBounds = 0;
    int startSourceWidth = 0;
    int startSourceHeight = 0;
    int destWidth = 0;
    int destHeight = 0;
    tTVPRect unionRect;
    bool hasUnion = false;

    {
        std::lock_guard<std::mutex> lock(gSDLBitmapCompletionMutex);
        batch = gSDLBitmapCompletionState.batch;
        regions = gSDLBitmapCompletionState.regions;
        copyReady = gSDLBitmapCompletionState.copyReady;
        surfaceCopied = gSDLBitmapCompletionState.surfaceCopied;
        surfaceSkipped = gSDLBitmapCompletionState.surfaceSkipped;
        glBacked = gSDLBitmapCompletionState.glBacked;
        outOfBounds = gSDLBitmapCompletionState.outOfBounds;
        startSourceWidth = gSDLBitmapCompletionState.sourceWidth;
        startSourceHeight = gSDLBitmapCompletionState.sourceHeight;
        destWidth = gSDLBitmapCompletionState.destWidth;
        destHeight = gSDLBitmapCompletionState.destHeight;
        unionRect = gSDLBitmapCompletionState.unionRect;
        hasUnion = gSDLBitmapCompletionState.hasUnion;
        gSDLBitmapCompletionState.active = false;
    }

    if(TVPSDLIsScreenTakeoverEnabled()) {
        TVPSDLPumpScreenPresenter("bitmap-end");
    }

    if(!ShouldLogBitmapCompletionBatch(batch) && outOfBounds == 0 &&
       surfaceSkipped == 0)
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(
        message, sizeof(message),
        "end batch=%llu manager=%p regions=%llu copyReady=%llu "
        "surfaceCopied=%llu surfaceSkipped=%llu "
        "glBacked=%llu "
        "outOfBounds=%llu src=%dx%d endSrc=%dx%d dest=%dx%d "
        "union=%d,%d,%dx%d events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(batch), static_cast<void *>(manager),
        static_cast<unsigned long long>(regions),
        static_cast<unsigned long long>(copyReady),
        static_cast<unsigned long long>(surfaceCopied),
        static_cast<unsigned long long>(surfaceSkipped),
        static_cast<unsigned long long>(glBacked),
        static_cast<unsigned long long>(outOfBounds),
        startSourceWidth, startSourceHeight, sourceWidth, sourceHeight,
        destWidth, destHeight, hasUnion ? unionRect.left : 0,
        hasUnion ? unionRect.top : 0,
        hasUnion ? unionRect.get_width() : 0,
        hasUnion ? unionRect.get_height() : 0,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-bitmap", message);
}

void TVPSDLRecordLoadingConsoleShow(const char *path, int frameWidth,
                                    int frameHeight, int sceneWidth,
                                    int sceneHeight, float scale) {
    TVPSDLInitializeRuntime();
    uint64_t session = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLLoadingConsoleMutex);
        gSDLLoadingConsoleState.active = true;
        gSDLLoadingConsoleState.session++;
        gSDLLoadingConsoleState.totalLines = 0;
        gSDLLoadingConsoleState.retainUntilTicks = 0;
        gSDLLoadingConsoleState.lines.clear();
        session = gSDLLoadingConsoleState.session;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[512];
    std::snprintf(
        message, sizeof(message),
        "show session=%llu frame=%dx%d scene=%dx%d scale=%.3f path=%s "
        "events=%d video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(session), frameWidth, frameHeight,
        sceneWidth, sceneHeight, scale, path ? path : "",
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-loading", message);
}

void TVPSDLRecordLoadingConsoleLine(const char *message, bool important) {
    TVPSDLInitializeRuntime();
    if(!message)
        message = "";

    uint64_t session = 0;
    uint64_t totalLines = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLLoadingConsoleMutex);
        if(!gSDLLoadingConsoleState.active) {
            gSDLLoadingConsoleState.active = true;
            gSDLLoadingConsoleState.session++;
        }
        session = gSDLLoadingConsoleState.session;
        totalLines = ++gSDLLoadingConsoleState.totalLines;
        if(gSDLLoadingConsoleState.lines.size() >= 64)
            gSDLLoadingConsoleState.lines.pop_front();
        gSDLLoadingConsoleState.lines.push_back(
            TVPSDLLoadingConsoleLine{ message, important });
    }

    if(totalLines <= 12 || important || (totalLines % 128) == 0) {
        char logLine[640];
        std::snprintf(logLine, sizeof(logLine),
                      "line session=%llu line=%llu color=%s text=%s",
                      static_cast<unsigned long long>(session),
                      static_cast<unsigned long long>(totalLines),
                      important ? "yellow" : "gray", message);
        TVPNativeLogInfo("sdl-loading", logLine);
    }
}

void TVPSDLRecordLoadingConsoleHide(const char *reason) {
    TVPSDLInitializeRuntime();
    uint64_t session = 0;
    uint64_t totalLines = 0;
    size_t retained = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLLoadingConsoleMutex);
        session = gSDLLoadingConsoleState.session;
        totalLines = gSDLLoadingConsoleState.totalLines;
        retained = gSDLLoadingConsoleState.lines.size();
        gSDLLoadingConsoleState.active = false;
        gSDLLoadingConsoleState.retainUntilTicks =
            static_cast<Uint64>(SDL_GetTicks()) + 2500;
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[256];
    std::snprintf(
        message, sizeof(message),
        "hide session=%llu reason=%s lines=%llu retained=%zu events=%d "
        "video=%d audio=%d ticks=%u",
        static_cast<unsigned long long>(session), reason ? reason : "",
        static_cast<unsigned long long>(totalLines), retained,
        (initialized & SDL_INIT_EVENTS) ? 1 : 0,
        (initialized & SDL_INIT_VIDEO) ? 1 : 0,
        (initialized & SDL_INIT_AUDIO) ? 1 : 0,
        static_cast<unsigned>(SDL_GetTicks()));
    TVPNativeLogInfo("sdl-loading", message);
}

TVPSDLLoadingConsoleSnapshot TVPSDLGetLoadingConsoleSnapshot() {
    TVPSDLLoadingConsoleSnapshot snapshot;
    std::lock_guard<std::mutex> lock(gSDLLoadingConsoleMutex);
    snapshot.active = gSDLLoadingConsoleState.active ||
        static_cast<Uint64>(SDL_GetTicks()) <=
            gSDLLoadingConsoleState.retainUntilTicks;
    snapshot.session = gSDLLoadingConsoleState.session;
    snapshot.totalLines = gSDLLoadingConsoleState.totalLines;
    snapshot.lines.reserve(gSDLLoadingConsoleState.lines.size());
    for(const TVPSDLLoadingConsoleLine &line : gSDLLoadingConsoleState.lines) {
        snapshot.lines.push_back(
            TVPSDLLoadingConsoleLineSnapshot{ line.message, line.important });
    }
    return snapshot;
}

void TVPSDLRecordRenderOverlayFrame(float deltaSeconds) {
    const bool showFps =
        GlobalConfigManager::GetInstance()->GetValue<bool>("showfps", false);
    if(!showFps) {
        std::lock_guard<std::mutex> lock(gSDLRenderOverlayMutex);
        gSDLRenderOverlayState.showFps = false;
        gSDLRenderOverlayState.available = false;
        return;
    }

    if(deltaSeconds <= 0.0f)
        deltaSeconds = 0.016f;
    if(deltaSeconds > 1.0f)
        deltaSeconds = 1.0f;

    unsigned int drawCount = 0;
    uint64_t videoMemoryBytes = 0;
    bool available = false;
    std::string pipelineName;
    if(iTVPRenderManager *manager = TVPGetRenderManager()) {
        available = manager->GetRenderStat(drawCount, videoMemoryBytes);
        if(const char *name = manager->GetName())
            pipelineName = name;
    }

    uint64_t presentedFrames = 0;
    std::string presenterName;
    {
        std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
        presentedFrames = gSDLScreenPresenterState.presentedFrames;
        if(gSDLScreenPresenterState.renderer) {
            if(const char *name =
                   SDL_GetRendererName(gSDLScreenPresenterState.renderer))
                presenterName = name;
        } else if(gSDLScreenPresenterState.windowSurface) {
            presenterName = "window-surface";
        }
    }
    const std::string graphicsBackend = PreferredGraphicsBackend();
    std::ostringstream rendererInfo;
    rendererInfo << "pipeline="
                 << (pipelineName.empty() ? "unknown" : pipelineName);
    if(!presenterName.empty())
        rendererInfo << " presenter=" << presenterName;
    if(!graphicsBackend.empty())
        rendererInfo << " backend=" << graphicsBackend;
    AppendSDLGpuOverlayInfo(rendererInfo);
    const std::string rendererName = rendererInfo.str();

    std::lock_guard<std::mutex> lock(gSDLRenderOverlayMutex);
    gSDLRenderOverlayState.showFps = true;
    gSDLRenderOverlayState.available = available;
    gSDLRenderOverlayState.accumulatedDeltaTime += deltaSeconds;
    const float filteredDeltaTime =
        deltaSeconds * 0.10f +
        (1.0f - 0.10f) * gSDLRenderOverlayState.previousDeltaTime;
    gSDLRenderOverlayState.previousDeltaTime = filteredDeltaTime;

    if(gSDLRenderOverlayState.accumulatedDeltaTime > 0.1f ||
       gSDLRenderOverlayState.sequence == 0) {
        gSDLRenderOverlayState.fps =
            filteredDeltaTime > 0.0f ? 1.0 / filteredDeltaTime : 0.0;
        gSDLRenderOverlayState.drawCount = drawCount;
        gSDLRenderOverlayState.videoMemoryBytes = videoMemoryBytes;
        gSDLRenderOverlayState.selfMemoryMb = TVPGetSelfUsedMemory();
        gSDLRenderOverlayState.freeMemoryMb = TVPGetSystemFreeMemory();
        gSDLRenderOverlayState.presentedFrames = presentedFrames;
        gSDLRenderOverlayState.rendererName = rendererName;
        gSDLRenderOverlayState.sequence++;
        gSDLRenderOverlayState.accumulatedDeltaTime = 0.0f;
    }
}

TVPSDLRenderOverlaySnapshot TVPSDLGetRenderOverlaySnapshot() {
    TVPSDLRenderOverlaySnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(gSDLRenderOverlayMutex);
        snapshot.showFps = gSDLRenderOverlayState.showFps;
        snapshot.available = gSDLRenderOverlayState.available;
        snapshot.fps = gSDLRenderOverlayState.fps;
        snapshot.drawCount = gSDLRenderOverlayState.drawCount;
        snapshot.videoMemoryBytes = gSDLRenderOverlayState.videoMemoryBytes;
        snapshot.selfMemoryMb = gSDLRenderOverlayState.selfMemoryMb;
        snapshot.freeMemoryMb = gSDLRenderOverlayState.freeMemoryMb;
        snapshot.presentedFrames = gSDLRenderOverlayState.presentedFrames;
        snapshot.sequence = gSDLRenderOverlayState.sequence;
        snapshot.rendererName = gSDLRenderOverlayState.rendererName;
    }

    snapshot.showFps =
        GlobalConfigManager::GetInstance()->GetValue<bool>("showfps", false);
    if(!snapshot.showFps)
        snapshot.available = false;
    return snapshot;
}

void TVPSDLSetScreenTakeoverEnabled(bool enabled, const char *reason,
                                    int frameWidth, int frameHeight,
                                    int sceneWidth, int sceneHeight) {
    TVPSDLInitializeRuntime();
    const bool requested = enabled;
    const bool supported = IsSDLScreenPresenterWindowSupported();
    enabled = enabled && supported;
    gSDLSurfaceMirrorConsumerActive.store(enabled, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
        gSDLScreenPresenterState.takeoverEnabled = enabled;
        gSDLScreenPresenterState.frameWidth = frameWidth;
        gSDLScreenPresenterState.frameHeight = frameHeight;
        gSDLScreenPresenterState.sceneWidth = sceneWidth;
        gSDLScreenPresenterState.sceneHeight = sceneHeight;
        if(!enabled) {
            DestroySDLScreenPresenterLocked();
        }
    }

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(message, sizeof(message),
                  "takeover enabled=%d requested=%d reason=%s unsupported=%s "
                  "frame=%dx%d scene=%dx%d events=%d video=%d audio=%d ticks=%u",
                  enabled ? 1 : 0, requested ? 1 : 0, reason ? reason : "",
                  supported ? "" : SDLScreenPresenterUnsupportedReason(),
                  frameWidth, frameHeight, sceneWidth, sceneHeight,
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLScreenPresenter(message);
    if(!enabled) {
        DropSDLSurfaceMirror("takeover-disabled");
    }
}

bool TVPSDLIsScreenTakeoverSupported() {
    return IsSDLScreenPresenterWindowSupported();
}

bool TVPSDLIsScreenTakeoverEnabled() {
    std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
    return gSDLScreenPresenterState.takeoverEnabled;
}

bool TVPSDLHasScreenPresenterPresented() {
    std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
    return gSDLScreenPresenterState.presentedFrames > 0;
}

extern "C" void TVPSDLGetPresentedSurfaceSize(int *width, int *height) {
    const int presentedWidth =
        gSDLPresentedSurfaceWidth.load(std::memory_order_relaxed);
    const int presentedHeight =
        gSDLPresentedSurfaceHeight.load(std::memory_order_relaxed);
    if(presentedWidth > 0 && presentedHeight > 0) {
        if(width)
            *width = presentedWidth;
        if(height)
            *height = presentedHeight;
        return;
    }
    std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
    if(width)
        *width = gSDLSurfaceMirrorState.width;
    if(height)
        *height = gSDLSurfaceMirrorState.height;
}

bool TVPSDLIsRenderDiagnosticsEnabled() {
    return IsSDLRenderDiagnosticsActive();
}

bool TVPSDLTryPresentTexture(iTVPTexture2D *texture, const char *stage,
                             int layerWidth, int layerHeight) {
    if(!texture)
        return false;

    const bool takeoverActive =
        TVPSDLIsScreenTakeoverEnabled() && TVPSDLIsScreenTakeoverSupported();
    const bool sdlGpuRequested = IsSDLGpuShadowUploadEnabled();
    const bool shadowUpload = ShouldRunSDLGpuShadowUpload(takeoverActive);
    if(!takeoverActive && !sdlGpuRequested)
        return false;

    TVPSDLInitializeRuntime();
    TVPSDLRecordPresenterFrame(texture, stage, layerWidth, layerHeight);

    tTVPRect updateRect;
    const tTVPRect fullRect(0, 0, static_cast<tjs_int>(texture->GetWidth()),
                            static_cast<tjs_int>(texture->GetHeight()));
    const bool presenterAlreadyPresented = TVPSDLHasScreenPresenterPresented();
    const bool glBackedTexture = texture->GetNativeGLTextureId() != 0;
    bool hasDirty = texture->PeekDirtyRect(updateRect);
    if(hasDirty)
        updateRect.clip(fullRect);
    bool forceFullFramePresent = false;
    const bool needsTakeoverFullFrame =
        takeoverActive && (!presenterAlreadyPresented || glBackedTexture);
    if(!hasDirty && needsTakeoverFullFrame) {
        updateRect = fullRect;
        hasDirty = !updateRect.is_empty();
        forceFullFramePresent = hasDirty;
    }

    if(!hasDirty)
        return takeoverActive && presenterAlreadyPresented;
    if(updateRect.is_empty())
        return takeoverActive && presenterAlreadyPresented;

    TVPTextureFormat::e copyFormat = texture->GetPixelDataFormat();
    if(copyFormat == TVPTextureFormat::None)
        copyFormat = texture->GetFormat();

    if(forceFullFramePresent && glBackedTexture)
        texture->InvalidatePixelCache();

    bool gpuUploaded = false;
    bool gpuConverted = false;
    uint64_t gpuUploadBytes = 0;
    uint64_t gpuUploads = 0;
    uint64_t gpuFailures = 0;
    uint64_t gpuAttempts = 0;
    std::string gpuError;
    bool gpuFailureLogged = false;
    bool gpuUnavailable = false;

    if(sdlGpuRequested && !shadowUpload) {
        std::lock_guard<std::mutex> lock(gSDLGpuPresenterMutex);
        if(gSDLGpuPresenterState.unavailable) {
            gpuUnavailable = true;
            gpuError = gSDLGpuPresenterState.unavailableReason;
        } else if(EnsureSDLGpuPresenterLocked(stage)) {
            const uint64_t skipped = ++gSDLGpuPresenterState.skippedDirect;
            if(ShouldLogScreenPresenter(skipped)) {
                char message[384];
                std::snprintf(
                    message, sizeof(message),
                    "shadow-upload skipped #%llu stage=%s reason=%s "
                    "driver=%s",
                    static_cast<unsigned long long>(skipped),
                    stage ? stage : "", "android-direct-flutter-presenter",
                    gSDLGpuPresenterState.backend.DriverName());
                LogSDLGpuPresenter(message);
            }
        }
    }

    if(shadowUpload) {
        bool knownTexture = false;
        std::lock_guard<std::mutex> lock(gSDLGpuPresenterMutex);
        if(gSDLGpuPresenterState.unavailable) {
            gpuUnavailable = true;
            gpuError = gSDLGpuPresenterState.unavailableReason;
            gpuAttempts = gSDLGpuPresenterState.attempts;
            gpuFailures = gSDLGpuPresenterState.failures;
        } else {
            knownTexture = gSDLGpuPresenterState.textureCache.Contains(texture);
            gpuAttempts = ++gSDLGpuPresenterState.attempts;
        }
        if(!gpuUnavailable && EnsureSDLGpuPresenterLocked(stage)) {
            const tTVPRect *sourceRect =
                knownTexture && hasDirty ? &updateRect : nullptr;
            auto result = gSDLGpuPresenterState.textureCache.Upsert(
                *texture, sourceRect, "tvp-window-presenter");
            if(result.uploaded) {
                gpuUploaded = true;
                gpuConverted = result.converted;
                gpuUploadBytes = result.uploadBytes;
                gpuUploads = ++gSDLGpuPresenterState.uploads;
            } else {
                gpuError = result.error;
                gpuFailures = ++gSDLGpuPresenterState.failures;
            }
        } else if(!gpuUnavailable) {
            gpuError = "SDL_GPU presenter backend is unavailable";
            gpuFailures = ++gSDLGpuPresenterState.failures;
        }
    }

    if(shadowUpload && !gpuUploaded && takeoverActive && !gpuUnavailable) {
        gpuFailureLogged = true;
        if(ShouldLogScreenPresenter(gpuFailures)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "upload failed #%llu attempt=%llu stage=%s tex=%p "
                          "dirty=%d,%d,%dx%d error=%s",
                          static_cast<unsigned long long>(gpuFailures),
                          static_cast<unsigned long long>(gpuAttempts),
                          stage ? stage : "", static_cast<void *>(texture),
                          updateRect.left, updateRect.top,
                          updateRect.get_width(), updateRect.get_height(),
                          gpuError.c_str());
            LogSDLGpuPresenter(message);
        }
    }

#if defined(__ANDROID__)
    if(takeoverActive) {
        SDL_Rect directRect;
        directRect.x = updateRect.left;
        directRect.y = updateRect.top;
        directRect.w = updateRect.get_width();
        directRect.h = updateRect.get_height();
        const int directWidth = static_cast<int>(texture->GetWidth());
        const int directHeight = static_cast<int>(texture->GetHeight());
        if(directRect.x < 0) {
            directRect.w += directRect.x;
            directRect.x = 0;
        }
        if(directRect.y < 0) {
            directRect.h += directRect.y;
            directRect.y = 0;
        }
        if(directRect.x + directRect.w > directWidth)
            directRect.w = directWidth - directRect.x;
        if(directRect.y + directRect.h > directHeight)
            directRect.h = directHeight - directRect.y;
        if(directRect.w > 0 && directRect.h > 0 &&
           TryPresentAndroidFlutterTexture(texture, copyFormat, directWidth,
                                           directHeight, directRect, stage)) {
            uint64_t presented = 0;
            {
                std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
                presented = ++gSDLScreenPresenterState.presentedFrames;
            }
            tTVPRect consumed;
            texture->ConsumeDirtyRect(consumed);
            const uint64_t presentSequence =
                gpuUploads > 0 ? gpuUploads : presented;
            if(ShouldLogScreenPresenter(presentSequence)) {
                char message[512];
                std::snprintf(
                    message, sizeof(message),
                    "present-texture-direct #%llu stage=%s tex=%p size=%ux%u "
                    "dirty=%d,%d,%dx%d gpuBytes=%llu converted=%d "
                    "takeover=1 fullFrame=%d",
                    static_cast<unsigned long long>(presentSequence),
                    stage ? stage : "", static_cast<void *>(texture),
                    texture->GetWidth(), texture->GetHeight(), updateRect.left,
                    updateRect.top, updateRect.get_width(),
                    updateRect.get_height(),
                    static_cast<unsigned long long>(gpuUploadBytes),
                    gpuConverted ? 1 : 0, forceFullFramePresent ? 1 : 0);
                LogSDLGpuPresenter(message);
            }
            return true;
        }
    }
#endif

    if(shadowUpload && !gpuUploaded) {
        if(!gpuUnavailable && !gpuFailureLogged &&
           ShouldLogScreenPresenter(gpuFailures)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "upload failed #%llu attempt=%llu stage=%s tex=%p "
                          "dirty=%d,%d,%dx%d error=%s",
                          static_cast<unsigned long long>(gpuFailures),
                          static_cast<unsigned long long>(gpuAttempts),
                          stage ? stage : "", static_cast<void *>(texture),
                          updateRect.left, updateRect.top,
                          updateRect.get_width(), updateRect.get_height(),
                          gpuError.c_str());
            LogSDLGpuPresenter(message);
        }
        if(!takeoverActive)
            return false;
    }

    if(!takeoverActive) {
        if(ShouldLogScreenPresenter(gpuUploads)) {
            char message[512];
            std::snprintf(message, sizeof(message),
                          "shadow-upload #%llu stage=%s tex=%p size=%ux%u "
                          "dirty=%d,%d,%dx%d gpuBytes=%llu converted=%d "
                          "takeover=0 fullFrame=%d",
                          static_cast<unsigned long long>(gpuUploads),
                          stage ? stage : "", static_cast<void *>(texture),
                          texture->GetWidth(), texture->GetHeight(),
                          updateRect.left, updateRect.top,
                          updateRect.get_width(), updateRect.get_height(),
                          static_cast<unsigned long long>(gpuUploadBytes),
                          gpuConverted ? 1 : 0,
                          forceFullFramePresent ? 1 : 0);
            LogSDLGpuPresenter(message);
        }
        return false;
    }

    uint64_t surfaceCopiedTotal = 0;
    uint64_t surfaceCopiedBytes = 0;
    uint64_t surfaceSkippedTotal = 0;
    const bool surfaceCopied = CopyRegionToSDLSurfaceMirror(
        texture, updateRect, updateRect.left, updateRect.top,
        static_cast<int>(texture->GetWidth()),
        static_cast<int>(texture->GetHeight()), copyFormat, gpuUploads,
        gpuAttempts, surfaceCopiedTotal, surfaceCopiedBytes,
        surfaceSkippedTotal);
    if(!surfaceCopied)
        return false;

    const bool pumped = TVPSDLPumpScreenPresenter(stage);
    if(!pumped)
        return false;

    tTVPRect consumed;
    texture->ConsumeDirtyRect(consumed);

    const uint64_t presentSequence =
        gpuUploads > 0 ? gpuUploads : surfaceCopiedTotal;
    if(ShouldLogScreenPresenter(presentSequence)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-texture #%llu stage=%s tex=%p size=%ux%u "
                      "dirty=%d,%d,%dx%d gpuBytes=%llu converted=%d "
                      "surfaceRegions=%llu surfaceBytes=%llu takeover=1 "
                      "fullFrame=%d",
                      static_cast<unsigned long long>(presentSequence),
                      stage ? stage : "", static_cast<void *>(texture),
                      texture->GetWidth(), texture->GetHeight(),
                      updateRect.left, updateRect.top,
                      updateRect.get_width(), updateRect.get_height(),
                      static_cast<unsigned long long>(gpuUploadBytes),
                      gpuConverted ? 1 : 0,
                      static_cast<unsigned long long>(surfaceCopiedTotal),
                      static_cast<unsigned long long>(surfaceCopiedBytes),
                      forceFullFramePresent ? 1 : 0);
        LogSDLGpuPresenter(message);
    }
    return true;
}

bool TVPSDLPresentHostWindowTexture(tTJSNI_BaseWindow *window,
                                    iTVPTexture2D *texture, const char *stage,
                                    int layerWidth, int layerHeight) {
    if(!TVPSDLTryPresentTexture(texture, stage, layerWidth, layerHeight))
        return false;

    if(!window || !texture)
        return true;

    iTVPDrawDevice *drawDevice = window->GetDrawDevice();
    if(!drawDevice)
        return true;

    int surfaceWidth = 0;
    int surfaceHeight = 0;
    TVPSDLGetPresentedSurfaceSize(&surfaceWidth, &surfaceHeight);
    if(surfaceWidth <= 0)
        surfaceWidth = static_cast<int>(texture->GetWidth());
    if(surfaceHeight <= 0)
        surfaceHeight = static_cast<int>(texture->GetHeight());
    if(surfaceWidth <= 0 || surfaceHeight <= 0)
        return true;

    const tTVPRect dest(0, 0, surfaceWidth, surfaceHeight);
    drawDevice->SetDestRectangle(dest);
    drawDevice->SetClipRectangle(dest);
    drawDevice->SetWindowSize(surfaceWidth, surfaceHeight);
    return true;
}

bool TVPSDLPumpScreenPresenter(const char *stage) {
    {
        std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
        if(!gSDLScreenPresenterState.takeoverEnabled ||
           !IsSDLScreenPresenterWindowSupported())
            return false;
        gSDLScreenPresenterState.pumpAttempts++;
    }

    SDL_Surface *surface = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int pitch = 0;
    tTVPRect updateRect;
    bool hasUpdate = false;
    uint64_t copiedRegions = 0;
    uint64_t copiedBytes = 0;

    std::lock_guard<std::mutex> surfaceLock(gSDLSurfaceMirrorMutex);
    surface = gSDLSurfaceMirrorState.surface;
    surfaceWidth = gSDLSurfaceMirrorState.width;
    surfaceHeight = gSDLSurfaceMirrorState.height;
    hasUpdate = gSDLSurfaceMirrorState.hasUpdate;
    updateRect = gSDLSurfaceMirrorState.updateRect;
    copiedRegions = gSDLSurfaceMirrorState.copiedRegions;
    copiedBytes = gSDLSurfaceMirrorState.copiedBytes;
    if(surface)
        pitch = surface->pitch;

    if(!surface || surfaceWidth <= 0 || surfaceHeight <= 0) {
        if(gSDLPresentedSurfaceWidth.load(std::memory_order_relaxed) > 0 &&
           gSDLPresentedSurfaceHeight.load(std::memory_order_relaxed) > 0)
            return false;
        uint64_t noSurface = 0;
        {
            std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
            noSurface = ++gSDLScreenPresenterState.noSurfacePumps;
        }
        if(ShouldLogScreenPresenter(noSurface)) {
            char message[320];
            std::snprintf(message, sizeof(message),
                          "pump no-surface #%llu stage=%s copiedRegions=%llu "
                          "copiedBytes=%llu",
                          static_cast<unsigned long long>(noSurface),
                          stage ? stage : "",
                          static_cast<unsigned long long>(copiedRegions),
                          static_cast<unsigned long long>(copiedBytes));
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    if(!hasUpdate) {
        return false;
    }

    SDL_Rect rect;
    rect.x = updateRect.left;
    rect.y = updateRect.top;
    rect.w = updateRect.get_width();
    rect.h = updateRect.get_height();
    if(rect.x < 0) {
        rect.w += rect.x;
        rect.x = 0;
    }
    if(rect.y < 0) {
        rect.h += rect.y;
        rect.y = 0;
    }
    if(rect.x + rect.w > surfaceWidth)
        rect.w = surfaceWidth - rect.x;
    if(rect.y + rect.h > surfaceHeight)
        rect.h = surfaceHeight - rect.y;
    if(rect.w <= 0 || rect.h <= 0) {
        return false;
    }

#if defined(__ANDROID__)
    if(TryPresentAndroidFlutterSurface(surface, surfaceWidth, surfaceHeight,
                                       pitch, rect, stage)) {
        gSDLSurfaceMirrorState.hasUpdate = false;
        uint64_t presented = 0;
        {
            std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
            presented = ++gSDLScreenPresenterState.presentedFrames;
        }
        if(ShouldLogScreenPresenter(presented)) {
            char message[384];
            std::snprintf(
                message, sizeof(message),
                "present-flutter-texture #%llu stage=%s surface=%dx%d "
                "pitch=%d rect=%d,%d,%dx%d copiedRegions=%llu "
                "copiedBytes=%llu",
                static_cast<unsigned long long>(presented),
                stage ? stage : "", surfaceWidth, surfaceHeight, pitch,
                rect.x, rect.y, rect.w, rect.h,
                static_cast<unsigned long long>(copiedRegions),
                static_cast<unsigned long long>(copiedBytes));
            LogSDLScreenPresenter(message);
        }
        return true;
    }

    uint64_t failed = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
        failed = ++gSDLScreenPresenterState.failedPumps;
    }
    if(ShouldLogScreenPresenter(failed)) {
        char message[384];
        std::snprintf(message, sizeof(message),
                      "pump flutter-surface failed #%llu stage=%s "
                      "surface=%dx%d update=%d,%d,%dx%d copiedRegions=%llu",
                      static_cast<unsigned long long>(failed),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      updateRect.left, updateRect.top,
                      updateRect.get_width(), updateRect.get_height(),
                      static_cast<unsigned long long>(copiedRegions));
        LogSDLScreenPresenter(message);
    }
    return false;
#endif

    std::lock_guard<std::mutex> presenterLock(gSDLScreenPresenterMutex);
    if(!gSDLScreenPresenterState.takeoverEnabled)
        return false;
    if(!EnsureSDLScreenPresenterLocked(surfaceWidth, surfaceHeight, stage)) {
        const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "pump failed #%llu stage=%s surface=%dx%d "
                          "update=%d,%d,%dx%d copiedRegions=%llu",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          updateRect.left, updateRect.top,
                          updateRect.get_width(), updateRect.get_height(),
                          static_cast<unsigned long long>(copiedRegions));
            LogSDLScreenPresenter(message);
        }
        return false;
    }

    if(!gSDLScreenPresenterState.renderer &&
       gSDLScreenPresenterState.window &&
       gSDLScreenPresenterState.windowSurface) {
        SDL_Rect dstRect = rect;
        bool updateResult = false;
        if(gSDLScreenPresenterState.windowSurface->w == surfaceWidth &&
           gSDLScreenPresenterState.windowSurface->h == surfaceHeight) {
            if(!SDL_BlitSurface(surface, &rect,
                                gSDLScreenPresenterState.windowSurface,
                                &dstRect)) {
                const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
                char message[384];
                std::snprintf(message, sizeof(message),
                              "window-surface blit failed #%llu stage=%s "
                              "rect=%d,%d,%dx%d error=%s",
                              static_cast<unsigned long long>(failed),
                              stage ? stage : "", rect.x, rect.y, rect.w,
                              rect.h, SDL_GetError());
                LogSDLScreenPresenter(message);
                return false;
            }
            updateResult = SDL_UpdateWindowSurfaceRects(
                gSDLScreenPresenterState.window, &dstRect, 1);
        } else {
            if(!SDL_BlitSurfaceScaled(surface, nullptr,
                                      gSDLScreenPresenterState.windowSurface,
                                      nullptr, SDL_SCALEMODE_LINEAR)) {
                const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
                char message[384];
                std::snprintf(message, sizeof(message),
                              "window-surface scaled blit failed #%llu "
                              "stage=%s src=%dx%d dst=%dx%d error=%s",
                              static_cast<unsigned long long>(failed),
                              stage ? stage : "", surfaceWidth, surfaceHeight,
                              gSDLScreenPresenterState.windowSurface->w,
                              gSDLScreenPresenterState.windowSurface->h,
                              SDL_GetError());
                LogSDLScreenPresenter(message);
                return false;
            }
            updateResult =
                SDL_UpdateWindowSurface(gSDLScreenPresenterState.window);
        }
        if(!updateResult) {
            const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
            char message[384];
            std::snprintf(message, sizeof(message),
                          "window-surface update failed #%llu stage=%s "
                          "error=%s",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", SDL_GetError());
            LogSDLScreenPresenter(message);
            return false;
        }
        gSDLSurfaceMirrorState.hasUpdate = false;
        const uint64_t presented = ++gSDLScreenPresenterState.presentedFrames;
        if(ShouldLogScreenPresenter(presented)) {
            char message[512];
            std::snprintf(
                message, sizeof(message),
                "present-window-surface #%llu stage=%s surface=%dx%d "
                "windowSurface=%dx%d pitch=%d rect=%d,%d,%dx%d "
                "copiedRegions=%llu copiedBytes=%llu",
                static_cast<unsigned long long>(presented),
                stage ? stage : "", surfaceWidth, surfaceHeight,
                gSDLScreenPresenterState.windowSurface->w,
                gSDLScreenPresenterState.windowSurface->h,
                gSDLScreenPresenterState.windowSurface->pitch, rect.x, rect.y,
                rect.w, rect.h,
                static_cast<unsigned long long>(copiedRegions),
                static_cast<unsigned long long>(copiedBytes));
            LogSDLScreenPresenter(message);
        }
        return true;
    }

    const auto *updatePixels = static_cast<const Uint8 *>(surface->pixels) +
        static_cast<size_t>(rect.y) * pitch + static_cast<size_t>(rect.x) * 4;
    if(!SDL_UpdateTexture(gSDLScreenPresenterState.texture, &rect,
                          updatePixels, pitch)) {
        const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
        char message[384];
        std::snprintf(message, sizeof(message),
                      "update texture failed #%llu stage=%s surface=%dx%d "
                      "pitch=%d rect=%d,%d,%dx%d error=%s",
                      static_cast<unsigned long long>(failed),
                      stage ? stage : "", surfaceWidth, surfaceHeight, pitch,
                      rect.x, rect.y, rect.w, rect.h,
                      SDL_GetError());
        LogSDLScreenPresenter(message);
        return false;
    }

    SDL_SetRenderDrawColor(gSDLScreenPresenterState.renderer, 0, 0, 0, 255);
    if(!SDL_RenderClear(gSDLScreenPresenterState.renderer)) {
        const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
        char message[384];
        std::snprintf(message, sizeof(message),
                      "render clear failed #%llu stage=%s error=%s",
                      static_cast<unsigned long long>(failed),
                      stage ? stage : "", SDL_GetError());
        LogSDLScreenPresenter(message);
        return false;
    }
    if(!SDL_RenderTexture(gSDLScreenPresenterState.renderer,
                          gSDLScreenPresenterState.texture, nullptr,
                          nullptr)) {
        const uint64_t failed = ++gSDLScreenPresenterState.failedPumps;
        char message[384];
        std::snprintf(message, sizeof(message),
                      "render copy failed #%llu stage=%s full=%dx%d "
                      "dirty=%d,%d,%dx%d "
                      "error=%s",
                      static_cast<unsigned long long>(failed),
                      stage ? stage : "", surfaceWidth, surfaceHeight, rect.x,
                      rect.y, rect.w, rect.h, SDL_GetError());
        LogSDLScreenPresenter(message);
        return false;
    }
    SDL_RenderPresent(gSDLScreenPresenterState.renderer);
    gSDLSurfaceMirrorState.hasUpdate = false;

    const uint64_t presented = ++gSDLScreenPresenterState.presentedFrames;
    if(ShouldLogScreenPresenter(presented)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present #%llu stage=%s surface=%dx%d pitch=%d "
                      "rect=%d,%d,%dx%d copiedRegions=%llu copiedBytes=%llu "
                      "renderer=%p texture=%p",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight, pitch,
                      rect.x, rect.y, rect.w, rect.h,
                      static_cast<unsigned long long>(copiedRegions),
                      static_cast<unsigned long long>(copiedBytes),
                      static_cast<void *>(gSDLScreenPresenterState.renderer),
                      static_cast<void *>(gSDLScreenPresenterState.texture));
        LogSDLScreenPresenter(message);
    }
    return true;
}

TVPSDLGameLaunchResult
TVPSDLRunGameLaunch(const TVPSDLGameLaunchCallbacks &callbacks) {
    LaunchLog(callbacks, "run game launch begin");
    LogSDLRuntime(callbacks);

    if(callbacks.initializePreferences) {
        LaunchLog(callbacks, "initialize preferences begin");
        callbacks.initializePreferences();
        LaunchLog(callbacks, "initialize preferences done");
    }

    // Keep this before direct startupFrom(). On Android it registers the
    // per-frame event dispatcher used by async TJS storage/events.
    const bool startupArgHandled = TVPCheckStartupArg();
    LaunchLog(callbacks,
              std::string("startup arg handled=") +
                  (startupArgHandled ? "1" : "0"));

    const std::string gameDir = TVPGetLaunchGameDir();
    const std::string launchPath = TVPGetLaunchGamePath();
    if(!launchPath.empty()) {
        LaunchLog(callbacks, "platform launch path: " + launchPath);
        if(!gameDir.empty()) {
            LaunchLog(callbacks, "platform game dir: " + gameDir);
        }
        if(callbacks.startupFrom && callbacks.startupFrom(launchPath, gameDir)) {
            LaunchLog(callbacks, "run game launch result=Started");
            return TVPSDLGameLaunchResult::Started;
        }
        LaunchLog(callbacks, "platform launch path failed; falling back");
    }

    if(startupArgHandled) {
        LaunchLog(callbacks, "startup argument handled by platform");
        LaunchLog(callbacks, "run game launch result=StartupArgHandled");
        return TVPSDLGameLaunchResult::StartupArgHandled;
    }

    if(callbacks.showFileSelector) {
        LaunchLog(callbacks, "show file selector fallback");
        callbacks.showFileSelector();
        LaunchLog(callbacks, "run game launch result=FileSelectorShown");
        return TVPSDLGameLaunchResult::FileSelectorShown;
    }

    LaunchLog(callbacks, "run game launch result=NoFallback");
    return TVPSDLGameLaunchResult::NoFallback;
}
