#include "SDLGameManager.h"

#include "NativeLog.h"
#include "Platform.h"
#include "tjsCommHead.h"
#include "ComplexRect.h"
#include "ConfigManager/GlobalConfigManager.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "LayerBitmapIntf.h"
#include "RenderManager.h"
#include "SDLAndroidFlutterPresenter.h"
#include "SDLGpuBackend.h"
#include "SDLPresentTypes.h"
#include "SDLGpuTextureCache.h"
#include "SDLRuntimePresenter.h"
#include "SDLUIManager.h"
#include "SysInitIntf.h"
#include "runtime/RuntimePresenter.h"
#include "runtime/RuntimeRenderManager.h"
#include "WindowIntf.h"
#include "TVPWindow.h"
#include "vkdefine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

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
TVPSDLPresentRect ToPresentRect(const SDL_Rect &rect) {
    return TVPSDLPresentRect{ rect.x, rect.y, rect.w, rect.h };
}

SDL_Rect ToSDLRect(const TVPSDLPresentRect &rect) {
    return SDL_Rect{ rect.x, rect.y, rect.w, rect.h };
}

TVPSDLPresentRect FullPresentRect(int width, int height) {
    return TVPSDLPresentRect{ 0, 0, width, height };
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
    TVPRuntimeRenderManagerSnapshot renderSnapshot;
    renderSnapshot.pipelineName = pipelineName;
    renderSnapshot.presenterName = presenterName;
    renderSnapshot.graphicsBackend = PreferredGraphicsBackend();
    renderSnapshot.available = available;
    renderSnapshot.drawCount = drawCount;
    renderSnapshot.videoMemoryBytes = videoMemoryBytes;
    renderSnapshot.presentedFrames = presentedFrames;
#if defined(__ANDROID__)
    if(TVPSDLAndroidFlutterPresenterIsEGLHighPerformanceActive()) {
        renderSnapshot.highPerformancePresenter = true;
        renderSnapshot.cpuCopyFreePresenter = true;
    }
#endif
    renderSnapshot.modules.push_back(TVPRuntimeRenderModuleInfo{
        pipelineName.empty() ? "unknown" : pipelineName, "renderer", true,
        available && pipelineName != "Software", false, videoMemoryBytes, 0 });
    if(!presenterName.empty()) {
        renderSnapshot.modules.push_back(TVPRuntimeRenderModuleInfo{
            presenterName, "presenter", true,
            renderSnapshot.highPerformancePresenter,
            renderSnapshot.cpuCopyFreePresenter, 0, 0 });
    }
    if(!renderSnapshot.graphicsBackend.empty()) {
        renderSnapshot.modules.push_back(TVPRuntimeRenderModuleInfo{
            renderSnapshot.graphicsBackend, "backend", true,
            renderSnapshot.graphicsBackend == "vulkan" ||
                renderSnapshot.graphicsBackend == "gpuapi",
            false, 0, 0 });
    }
    TVPRuntimeUpdateRenderManagerSnapshot(renderSnapshot);

    std::ostringstream rendererInfo;
    rendererInfo << TVPRuntimeDescribeRenderManager();
    AppendSDLGpuOverlayInfo(rendererInfo);
#if defined(__ANDROID__)
    TVPSDLAndroidFlutterPresenterAppendEGLOverlayInfo(rendererInfo);
#endif
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
    const bool useSurfaceMirror =
        enabled && ShouldUseSDLSurfaceMirrorForTakeover();
    gSDLSurfaceMirrorConsumerActive.store(useSurfaceMirror,
                                          std::memory_order_relaxed);
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
                  "takeover enabled=%d requested=%d surfaceMirror=%d reason=%s "
                  "unsupported=%s frame=%dx%d scene=%dx%d events=%d video=%d "
                  "audio=%d ticks=%u",
                  enabled ? 1 : 0, requested ? 1 : 0,
                  useSurfaceMirror ? 1 : 0, reason ? reason : "",
                  supported ? "" : SDLScreenPresenterUnsupportedReason(),
                  frameWidth, frameHeight, sceneWidth, sceneHeight,
                  (initialized & SDL_INIT_EVENTS) ? 1 : 0,
                  (initialized & SDL_INIT_VIDEO) ? 1 : 0,
                  (initialized & SDL_INIT_AUDIO) ? 1 : 0,
                  static_cast<unsigned>(SDL_GetTicks()));
    LogSDLScreenPresenter(message);
    if(!useSurfaceMirror) {
        DropSDLSurfaceMirror(enabled ? "surface-mirror-disabled"
                                     : "takeover-disabled");
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
    if(TVPSDLAndroidFlutterPresenterGetPresentedSurfaceSize(width, height))
        return;
    std::lock_guard<std::mutex> lock(gSDLSurfaceMirrorMutex);
    if(width)
        *width = gSDLSurfaceMirrorState.width;
    if(height)
        *height = gSDLSurfaceMirrorState.height;
}

#if defined(__ANDROID__)
extern "C" void TVPSDLNotifyAndroidFlutterGameSurfaceChanged(
    const char *reason) {
    TVPSDLAndroidFlutterPresenterNotifySurfaceChanged(reason);
}
#endif

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
    if(takeoverActive &&
       TVPSDLAndroidFlutterPresenterConsumeForceFullFramePresent()) {
        updateRect = fullRect;
        hasDirty = !updateRect.is_empty();
        forceFullFramePresent = hasDirty;
    }
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
        const int directWidth = static_cast<int>(texture->GetWidth());
        const int directHeight = static_cast<int>(texture->GetHeight());
        TVPSDLTexturePresentPlan androidPlan;
        androidPlan.textureWidth = directWidth;
        androidPlan.textureHeight = directHeight;
        androidPlan.dirtyRect =
            TVPSDLPresentRect{ updateRect.left, updateRect.top,
                               updateRect.get_width(),
                               updateRect.get_height() };
        androidPlan.forceFullFrame = forceFullFramePresent;
        androidPlan.directPartialAllowed =
            TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled();
        SDL_Rect directRect = ToSDLRect(androidPlan.dirtyRect);
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
        androidPlan.dirtyRect = ToPresentRect(directRect);
        androidPlan.fallbackRect = androidPlan.directPartialAllowed
            ? androidPlan.dirtyRect
            : FullPresentRect(directWidth, directHeight);
        TVPSDLTexturePresentResult androidResult;
        TVPSDLAndroidFlutterPresenterTryPresentTexturePlan(
            texture, copyFormat, stage, androidPlan, androidResult);
        if(androidResult.Presented()) {
            uint64_t presented = 0;
            {
                std::lock_guard<std::mutex> lock(gSDLScreenPresenterMutex);
                presented = ++gSDLScreenPresenterState.presentedFrames;
            }
            TVPRuntimePresentFrameInfo frameInfo;
            frameInfo.sequence = presented;
            frameInfo.textureWidth = static_cast<int>(texture->GetWidth());
            frameInfo.textureHeight = static_cast<int>(texture->GetHeight());
            frameInfo.layerWidth = layerWidth;
            frameInfo.layerHeight = layerHeight;
            frameInfo.sourceRect = { androidResult.sourceRect.x,
                                     androidResult.sourceRect.y,
                                     androidResult.sourceRect.w,
                                     androidResult.sourceRect.h };
            frameInfo.destRect = frameInfo.sourceRect;
            frameInfo.fullFrame = androidResult.fullFrame;
            frameInfo.nativeGL = androidResult.nativeGL;
            frameInfo.cpuCopyFree = androidResult.cpuCopyFree;
            TVPRuntimeRecordPresentFrame(frameInfo);
            tTVPRect consumed;
            texture->ConsumeDirtyRect(consumed);
            const uint64_t presentSequence =
                gpuUploads > 0 ? gpuUploads : presented;
            if(ShouldLogScreenPresenter(presentSequence)) {
                char message[512];
                std::snprintf(
                    message, sizeof(message),
                    "present-texture-%s #%llu stage=%s tex=%p size=%ux%u "
                    "dirty=%d,%d,%dx%d gpuBytes=%llu converted=%d "
                    "takeover=1 fullFrame=%d",
                    TVPSDLAndroidFlutterPresenterPresentPathLogName(
                        androidResult.path),
                    static_cast<unsigned long long>(presentSequence),
                    stage ? stage : "", static_cast<void *>(texture),
                    texture->GetWidth(), texture->GetHeight(),
                    androidResult.sourceRect.x, androidResult.sourceRect.y,
                    androidResult.sourceRect.w, androidResult.sourceRect.h,
                    static_cast<unsigned long long>(gpuUploadBytes),
                    gpuConverted ? 1 : 0, androidResult.fullFrame ? 1 : 0);
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
        if(TVPSDLAndroidFlutterPresenterHasPresentedSurface())
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
    if(TVPSDLAndroidFlutterPresenterTryPresentSurface(
           surface, surfaceWidth, surfaceHeight, pitch, rect, stage)) {
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
