#include "SDLGameManager.h"

#include "NativeLog.h"
#include "Platform.h"

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

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
