#include "SDLGameManager.h"

#include "NativeLog.h"
#include "Platform.h"
#include "tjsCommHead.h"
#include "ComplexRect.h"
#include "LayerBitmapIntf.h"
#include "RenderManager.h"
#include "SDLUIManager.h"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {

std::once_flag gSDLRuntimeInitOnce;
bool gSDLRuntimeInitialized = false;
std::string gSDLRuntimeError;
std::atomic_uint64_t gSDLLifecycleEventSequence{0};
std::atomic_uint64_t gSDLInputEventSequence{0};
std::once_flag gSDLInputQueueInitOnce;
Uint32 gSDLInputQueueEventType = 0;
std::atomic_bool gSDLInputQueueReady{false};
std::atomic_uint64_t gSDLInputQueued{0};
std::atomic_uint64_t gSDLInputDrained{0};
std::atomic_uint64_t gSDLInputDropped{0};
std::atomic_uint64_t gSDLInputBatches{0};
std::atomic_uint64_t gSDLInputMaxBacklog{0};
std::atomic_uint64_t gSDLInputMaxAgeMs{0};
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

struct TVPSDLLoadingConsoleLine {
    std::string message;
    bool important = false;
};

struct TVPSDLLoadingConsoleState {
    bool active = false;
    uint64_t session = 0;
    uint64_t totalLines = 0;
    std::deque<TVPSDLLoadingConsoleLine> lines;
};

std::mutex gSDLLoadingConsoleMutex;
TVPSDLLoadingConsoleState gSDLLoadingConsoleState;

struct TVPSDLQueuedInputEvent {
    std::string eventName;
    int itemCount = 0;
    float x = 0.0f;
    float y = 0.0f;
    int code = 0;
    bool state = false;
    Uint32 ticks = 0;
    uint64_t sequence = 0;
};

std::string FormatVersion(const SDL_version &version) {
    std::ostringstream os;
    os << static_cast<int>(version.major) << "."
       << static_cast<int>(version.minor) << "."
       << static_cast<int>(version.patch);
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
        SDL_FreeSurface(gSDLSurfaceMirrorState.surface);
        gSDLSurfaceMirrorState.surface = nullptr;
    }

    gSDLSurfaceMirrorState.surface = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
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
                  SDL_GetPixelFormatName(
                      gSDLSurfaceMirrorState.surface->format->format));
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

uint64_t CalculateBacklog(uint64_t queued, uint64_t drained,
                          uint64_t dropped) {
    const uint64_t completed = drained + dropped;
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
        if(eventType == static_cast<Uint32>(-1)) {
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
                            float y, int code, bool state) {
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

    const uint64_t queuedCount =
        gSDLInputQueued.fetch_add(1, std::memory_order_relaxed) + 1;
    auto *queued = new TVPSDLQueuedInputEvent{
        eventName ? eventName : "", itemCount, x, y, code, state,
        SDL_GetTicks(), queuedCount,
    };

    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = gSDLInputQueueEventType;
    event.user.code = code;
    event.user.data1 = queued;

    if(SDL_PushEvent(&event) != 1) {
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
    const uint64_t backlog = CalculateBacklog(queuedCount, drained, dropped);
    UpdateAtomicMax(gSDLInputMaxBacklog, backlog);

    if(ShouldLogInputQueueEvent(queuedCount, eventName)) {
        char message[320];
        std::snprintf(message, sizeof(message),
                      "queued=%llu backlog=%llu drained=%llu dropped=%llu "
                      "event=%s count=%d x=%.2f y=%.2f code=%d state=%d",
                      static_cast<unsigned long long>(queuedCount),
                      static_cast<unsigned long long>(backlog),
                      static_cast<unsigned long long>(drained),
                      static_cast<unsigned long long>(dropped),
                      eventName ? eventName : "", itemCount, x, y, code,
                      state ? 1 : 0);
        LogSDLInputQueue(message);
    }
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
        if(SDL_InitSubSystem(SDL_INIT_EVENTS) == 0) {
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

    SDL_version compiled;
    SDL_VERSION(&compiled);
    SDL_version linked;
    SDL_GetVersion(&linked);

    info.compiledVersion = FormatVersion(compiled);
    info.linkedVersion = FormatVersion(linked);
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
    TVPSDLInitializeRuntime();
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
    if(IsSDLUITouchInput(eventName)) {
        TVPSDLUIRecordAndroidTouch(eventName, x, y,
                                   itemCount > 0 ? code : -1, state);
    }
    QueueAndroidInputEvent(eventName, itemCount, x, y, code, state);
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

void TVPSDLProcessAndroidInputQueue() {
    if(!gSDLInputQueueReady.load(std::memory_order_acquire))
        return;

    uint64_t drainedInBatch = 0;
    uint64_t droppedInBatch = 0;
    Uint32 maxAgeInBatch = 0;
    uint64_t lastSequence = 0;
    std::string lastEventName;

    SDL_Event event;
    while(SDL_PeepEvents(&event, 1, SDL_GETEVENT, gSDLInputQueueEventType,
                         gSDLInputQueueEventType) == 1) {
        auto *queued =
            static_cast<TVPSDLQueuedInputEvent *>(event.user.data1);
        if(!queued) {
            droppedInBatch++;
            continue;
        }

        const Uint32 age = SDL_GetTicks() - queued->ticks;
        if(age > maxAgeInBatch)
            maxAgeInBatch = age;
        lastEventName = queued->eventName;
        lastSequence = queued->sequence;
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
    const uint64_t batch =
        gSDLInputBatches.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t backlog = CalculateBacklog(
        gSDLInputQueued.load(std::memory_order_relaxed), drained, dropped);
    UpdateAtomicMax(gSDLInputMaxBacklog, backlog);
    UpdateAtomicMax(gSDLInputMaxAgeMs, maxAgeInBatch);

    if(ShouldLogInputQueueSequence(batch) || droppedInBatch > 0 ||
       maxAgeInBatch > 50) {
        char message[320];
        std::snprintf(
            message, sizeof(message),
            "batch=%llu items=%llu drained=%llu dropped=%llu backlog=%llu "
            "maxAgeMs=%u maxBacklog=%llu maxSeenAgeMs=%llu lastSeq=%llu last=%s",
            static_cast<unsigned long long>(batch),
            static_cast<unsigned long long>(drainedInBatch),
            static_cast<unsigned long long>(drained),
            static_cast<unsigned long long>(dropped),
            static_cast<unsigned long long>(backlog),
            static_cast<unsigned>(maxAgeInBatch),
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
    const bool surfaceCopied = copyReady &&
        CopyRegionToSDLSurfaceMirror(texture, clipRect, x, y, sourceWidth,
                                     sourceHeight, format, globalRegion,
                                     batchRegion, surfaceCopiedTotal,
                                     surfaceCopiedBytesTotal,
                                     surfaceSkippedTotal);
    const bool surfaceSkipped = copyReady && !surfaceCopied;

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
        "copyReadyBatch=%llu surfaceBatch=%llu surfaceSkipBatch=%llu "
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

    if(!ShouldLogBitmapCompletionBatch(batch) && outOfBounds == 0 &&
       surfaceSkipped == 0)
        return;

    const Uint32 initialized = SDL_WasInit(0);
    char message[384];
    std::snprintf(
        message, sizeof(message),
        "end batch=%llu manager=%p regions=%llu copyReady=%llu "
        "surfaceCopied=%llu surfaceSkipped=%llu glBacked=%llu "
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
