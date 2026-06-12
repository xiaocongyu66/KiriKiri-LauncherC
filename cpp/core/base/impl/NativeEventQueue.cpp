#include "tjsCommHead.h"
#include "NativeEventQueue.h"
#include "Application.h"
#include "NativeLog.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>

class NativeEventQueueState {
public:
    std::atomic_bool active{ false };
};

namespace {
    struct PendingNativeEvent {
        NativeEventQueueImplement *queue;
        std::shared_ptr<NativeEventQueueState> state;
        NativeEvent event;
    };

    struct NativeEventFilterContext {
        NativeEventQueueImplement *queue;
        int msg;
        uint64_t removed;
    };

    std::once_flag gNativeEventQueueInitOnce;
    Uint32 gNativeEventQueueEventType = 0;
    std::atomic_bool gNativeEventQueueReady{ false };
    std::atomic_uint64_t gNativeEventPosted{ 0 };
    std::atomic_uint64_t gNativeEventDispatched{ 0 };
    std::atomic_uint64_t gNativeEventDropped{ 0 };
    std::atomic_uint64_t gNativeEventBatches{ 0 };

    bool ShouldLogSequence(uint64_t sequence) {
        return sequence <= 8 || (sequence % 256) == 0;
    }

    void LogNativeEventQueue(const char *message) {
        TVPNativeLogInfo("sdl-eventqueue", message ? message : "");
    }

    void LogNativeEventQueueF(const char *fmt, uint64_t a = 0,
                              uint64_t b = 0, uint64_t c = 0) {
        char message[256];
        std::snprintf(message, sizeof(message), fmt,
                      static_cast<unsigned long long>(a),
                      static_cast<unsigned long long>(b),
                      static_cast<unsigned long long>(c));
        LogNativeEventQueue(message);
    }

    bool EnsureNativeEventQueue() {
        std::call_once(gNativeEventQueueInitOnce, []() {
            if(SDL_WasInit(SDL_INIT_EVENTS) == 0 &&
               !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
                char message[256];
                std::snprintf(message, sizeof(message),
                              "init events failed: %s", SDL_GetError());
                LogNativeEventQueue(message);
                return;
            }

            const Uint32 eventType = SDL_RegisterEvents(1);
            if(eventType == 0) {
                char message[256];
                std::snprintf(message, sizeof(message),
                              "register custom event failed: %s",
                              SDL_GetError());
                LogNativeEventQueue(message);
                return;
            }

            gNativeEventQueueEventType = eventType;
            gNativeEventQueueReady.store(true, std::memory_order_release);

            char message[128];
            std::snprintf(message, sizeof(message),
                          "ready custom_event_type=%u",
                          static_cast<unsigned>(eventType));
            LogNativeEventQueue(message);
        });

        return gNativeEventQueueReady.load(std::memory_order_acquire);
    }

    void PostFallbackApplicationMessage(NativeEventQueueImplement *queue,
                                        const NativeEvent &ev) {
        if(!Application)
            return;
        Application->PostUserMessage(
            [queue, ev]() {
                NativeEvent event = ev;
                queue->Dispatch(event);
            },
            queue, static_cast<int>(ev.Message));
    }

    bool FilterNativeEvent(void *userdata, SDL_Event *event) {
        auto *context = static_cast<NativeEventFilterContext *>(userdata);
        if(!context || event->type != gNativeEventQueueEventType)
            return true;

        auto *pending =
            static_cast<PendingNativeEvent *>(event->user.data1);
        if(!pending)
            return false;

        if(pending->queue == context->queue &&
           (!context->msg ||
            pending->event.Message == static_cast<unsigned int>(context->msg))) {
            delete pending;
            context->removed++;
            return false;
        }
        return true;
    }
} // namespace

NativeEventQueueImplement::NativeEventQueueImplement() :
    state_(std::make_shared<NativeEventQueueState>()) {
}

NativeEventQueueImplement::~NativeEventQueueImplement() { Deallocate(); }

void NativeEventQueueImplement::Allocate() {
    state_->active.store(true, std::memory_order_release);
    EnsureNativeEventQueue();
}

void NativeEventQueueImplement::Deallocate() {
    state_->active.store(false, std::memory_order_release);
    Clear();
}

void NativeEventQueueImplement::PostEvent(const NativeEvent &ev) {
    if(!state_->active.load(std::memory_order_acquire))
        return;

    if(!EnsureNativeEventQueue()) {
        LogNativeEventQueue("SDL queue unavailable, using Application fallback");
        PostFallbackApplicationMessage(this, ev);
        return;
    }

    auto *pending = new PendingNativeEvent{ this, state_, ev };
    SDL_Event event;
    SDL_memset(&event, 0, sizeof(event));
    event.type = gNativeEventQueueEventType;
    event.user.code = static_cast<Sint32>(ev.Message);
    event.user.data1 = pending;

    if(!SDL_PushEvent(&event)) {
        delete pending;
        char message[256];
        std::snprintf(message, sizeof(message),
                      "push failed message=%u error=%s", ev.Message,
                      SDL_GetError());
        LogNativeEventQueue(message);
        PostFallbackApplicationMessage(this, ev);
        return;
    }

    const uint64_t posted =
        gNativeEventPosted.fetch_add(1, std::memory_order_relaxed) + 1;
    if(ShouldLogSequence(posted)) {
        LogNativeEventQueueF("posted=%llu dispatched=%llu dropped=%llu",
                             posted,
                             gNativeEventDispatched.load(
                                 std::memory_order_relaxed),
                             gNativeEventDropped.load(
                                 std::memory_order_relaxed));
    }
}

void NativeEventQueueImplement::Clear(int msg) {
    if(Application) {
        Application->FilterUserMessage(
            [this, msg](
                std::vector<std::tuple<void *, int, tTVPApplication::tMsg>>
                    &lst) {
                for(auto it = lst.begin(); it != lst.end();) {
                    if(std::get<0>(*it) == this &&
                       (!msg || std::get<1>(*it) == msg)) {
                        it = lst.erase(it);
                    } else {
                        ++it;
                    }
                }
            });
    }

    if(!gNativeEventQueueReady.load(std::memory_order_acquire))
        return;

    NativeEventFilterContext context{ this, msg, 0 };
    SDL_FilterEvents(FilterNativeEvent, &context);
    if(context.removed > 0) {
        LogNativeEventQueueF("cleared=%llu msg=%llu dropped=%llu",
                             context.removed, static_cast<uint64_t>(msg),
                             gNativeEventDropped.load(
                                 std::memory_order_relaxed));
    }
}

void TVPProcessNativeEventQueue() {
    if(!EnsureNativeEventQueue())
        return;

    uint64_t dispatchedInBatch = 0;
    uint64_t droppedInBatch = 0;

    SDL_Event event;
    while(SDL_PeepEvents(&event, 1, SDL_GETEVENT,
                         gNativeEventQueueEventType,
                         gNativeEventQueueEventType) == 1) {
        auto *pending =
            static_cast<PendingNativeEvent *>(event.user.data1);
        if(!pending) {
            droppedInBatch++;
            continue;
        }

        std::unique_ptr<PendingNativeEvent> holder(pending);
        if(!pending->state ||
           !pending->state->active.load(std::memory_order_acquire) ||
           !pending->queue) {
            droppedInBatch++;
            continue;
        }

        NativeEvent nativeEvent = pending->event;
        pending->queue->Dispatch(nativeEvent);
        dispatchedInBatch++;
    }

    if(dispatchedInBatch == 0 && droppedInBatch == 0)
        return;

    const uint64_t dispatched =
        gNativeEventDispatched.fetch_add(dispatchedInBatch,
                                         std::memory_order_relaxed) +
        dispatchedInBatch;
    const uint64_t dropped =
        gNativeEventDropped.fetch_add(droppedInBatch,
                                      std::memory_order_relaxed) +
        droppedInBatch;
    const uint64_t batch =
        gNativeEventBatches.fetch_add(1, std::memory_order_relaxed) + 1;

    if(ShouldLogSequence(batch) || droppedInBatch > 0) {
        LogNativeEventQueueF("batch=%llu dispatched=%llu dropped=%llu",
                             batch, dispatched, dropped);
    }
}
