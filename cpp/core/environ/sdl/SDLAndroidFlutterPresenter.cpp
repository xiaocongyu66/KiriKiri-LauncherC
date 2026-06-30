#include "SDLAndroidFlutterPresenter.h"

#include "NativeLog.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(__ANDROID__)
#include <android/native_window.h>
#endif

#if defined(__ANDROID__)
extern "C" ANativeWindow *TVPAndroidAcquireFlutterGameSurfaceWindow();
extern "C" void TVPAndroidReleaseFlutterGameSurfaceWindow(
    ANativeWindow *window);
extern "C" void TVPAndroidGetFlutterGameSurfaceSize(int *width, int *height);
#endif

namespace {

std::atomic_int gPresentedSurfaceWidth{0};
std::atomic_int gPresentedSurfaceHeight{0};
std::atomic_bool gForceFullFramePresent{false};
std::atomic_uint64_t gFlutterSurfacePresented{0};
std::atomic_uint64_t gFlutterSurfaceFailures{0};
std::atomic_uint64_t gFlutterSurfaceUnavailable{0};

#if defined(__ANDROID__)
std::once_flag gDirectPartialPresentFlagOnce;
bool gDirectPartialPresentEnabled = false;
#endif

bool ShouldLogScreenPresenter(uint64_t sequence) {
    return sequence <= 8 || sequence == 16 || sequence == 32 ||
        sequence == 64 || sequence == 128 || (sequence % 256) == 0;
}

void LogSDLScreenPresenter(const char *message) {
    TVPNativeLogInfo("sdl-screen", message ? message : "");
}

bool IsTruthyEnv(const char *name) {
    const char *value = SDL_getenv(name);
    return value && std::strcmp(value, "0") != 0 &&
        std::strcmp(value, "false") != 0 &&
        std::strcmp(value, "FALSE") != 0;
}

#if defined(__ANDROID__)
SDL_Rect FullAndroidSurfaceRect(int surfaceWidth, int surfaceHeight) {
    return SDL_Rect{ 0, 0, surfaceWidth, surfaceHeight };
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
#endif

} // namespace

bool TVPSDLAndroidFlutterPresenterGetPresentedSurfaceSize(int *width,
                                                          int *height) {
    const int presentedWidth =
        gPresentedSurfaceWidth.load(std::memory_order_relaxed);
    const int presentedHeight =
        gPresentedSurfaceHeight.load(std::memory_order_relaxed);
    if(presentedWidth <= 0 || presentedHeight <= 0)
        return false;
    if(width)
        *width = presentedWidth;
    if(height)
        *height = presentedHeight;
    return true;
}

bool TVPSDLAndroidFlutterPresenterHasPresentedSurface() {
    return gPresentedSurfaceWidth.load(std::memory_order_relaxed) > 0 &&
        gPresentedSurfaceHeight.load(std::memory_order_relaxed) > 0;
}

void TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(int width,
                                                               int height) {
    if(width <= 0 || height <= 0)
        return;

    const int previousWidth =
        gPresentedSurfaceWidth.exchange(width, std::memory_order_relaxed);
    const int previousHeight =
        gPresentedSurfaceHeight.exchange(height, std::memory_order_relaxed);
    if((previousWidth > 0 && previousWidth != width) ||
       (previousHeight > 0 && previousHeight != height)) {
        TVPSDLAndroidFlutterPresenterMarkSurfaceChanged();
    }
}

void TVPSDLAndroidFlutterPresenterMarkSurfaceChanged() {
    gForceFullFramePresent.store(true, std::memory_order_relaxed);
}

bool TVPSDLAndroidFlutterPresenterConsumeForceFullFramePresent() {
    return gForceFullFramePresent.exchange(false, std::memory_order_relaxed);
}

bool TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled() {
#if defined(__ANDROID__)
    std::call_once(gDirectPartialPresentFlagOnce, []() {
        gDirectPartialPresentEnabled =
            IsTruthyEnv("KRKR2_ANDROID_DIRECT_PARTIAL_PRESENT");
    });
    return gDirectPartialPresentEnabled;
#else
    return false;
#endif
}

bool TVPSDLAndroidFlutterPresenterTryPresentTexture(iTVPTexture2D *texture,
                                                    TVPTextureFormat::e format,
                                                    int surfaceWidth,
                                                    int surfaceHeight,
                                                    const SDL_Rect &rect,
                                                    const char *stage) {
#if defined(__ANDROID__)
    if(!texture || surfaceWidth <= 0 || surfaceHeight <= 0 ||
       format != TVPTextureFormat::RGBA)
        return false;

    ANativeWindow *window = TVPAndroidAcquireFlutterGameSurfaceWindow();
    if(!window) {
        const uint64_t unavailable = ++gFlutterSurfaceUnavailable;
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
            const uint64_t failed = ++gFlutterSurfaceFailures;
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

    SDL_Rect presentRect = rect;
    if(!TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled())
        presentRect = FullAndroidSurfaceRect(surfaceWidth, surfaceHeight);

    ARect dirty;
    dirty.left = presentRect.x;
    dirty.top = presentRect.y;
    dirty.right = presentRect.x + presentRect.w;
    dirty.bottom = presentRect.y + presentRect.h;

    ANativeWindow_Buffer buffer{};
    const int lockResult = ANativeWindow_lock(window, &buffer, &dirty);
    if(lockResult != 0 || !buffer.bits) {
        const uint64_t failed = ++gFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "flutter-texture lock failed #%llu stage=%s "
                          "surface=%dx%d rect=%d,%d,%dx%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          presentRect.x, presentRect.y, presentRect.w,
                          presentRect.h, lockResult);
            LogSDLScreenPresenter(message);
        }
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);
        return false;
    }

    const bool copied = CopyTextureToAndroidBuffer(
        texture, surfaceWidth, surfaceHeight, presentRect, buffer);
    const int unlockResult = ANativeWindow_unlockAndPost(window);
    TVPAndroidReleaseFlutterGameSurfaceWindow(window);
    if(!copied || unlockResult != 0) {
        const uint64_t failed = ++gFlutterSurfaceFailures;
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

    TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(surfaceWidth,
                                                              surfaceHeight);
    const uint64_t presented = ++gFlutterSurfacePresented;
    if(ShouldLogScreenPresenter(presented)) {
        const int glBacked = texture->GetNativeGLTextureId() != 0 ? 1 : 0;
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-direct #%llu stage=%s surface=%dx%d "
                      "flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d glBacked=%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      flutterWidth, flutterHeight, buffer.width,
                      buffer.height, buffer.stride, buffer.format,
                      presentRect.x, presentRect.y, presentRect.w,
                      presentRect.h, glBacked);
        LogSDLScreenPresenter(message);
    }
    return true;
#else
    (void)texture;
    (void)format;
    (void)surfaceWidth;
    (void)surfaceHeight;
    (void)rect;
    (void)stage;
    return false;
#endif
}

bool TVPSDLAndroidFlutterPresenterTryPresentSurface(SDL_Surface *surface,
                                                    int surfaceWidth,
                                                    int surfaceHeight,
                                                    int pitch,
                                                    const SDL_Rect &rect,
                                                    const char *stage) {
#if defined(__ANDROID__)
    if(!surface || !surface->pixels || surfaceWidth <= 0 ||
       surfaceHeight <= 0 || pitch <= 0)
        return false;

    ANativeWindow *window = TVPAndroidAcquireFlutterGameSurfaceWindow();
    if(!window) {
        const uint64_t unavailable = ++gFlutterSurfaceUnavailable;
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
            const uint64_t failed = ++gFlutterSurfaceFailures;
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

    SDL_Rect presentRect = rect;
    if(!TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled())
        presentRect = FullAndroidSurfaceRect(surfaceWidth, surfaceHeight);

    ARect dirty;
    dirty.left = presentRect.x;
    dirty.top = presentRect.y;
    dirty.right = presentRect.x + presentRect.w;
    dirty.bottom = presentRect.y + presentRect.h;

    ANativeWindow_Buffer buffer{};
    const int lockResult = ANativeWindow_lock(window, &buffer, &dirty);
    if(lockResult != 0 || !buffer.bits) {
        const uint64_t failed = ++gFlutterSurfaceFailures;
        if(ShouldLogScreenPresenter(failed)) {
            char message[384];
            std::snprintf(message, sizeof(message),
                          "flutter-surface lock failed #%llu stage=%s "
                          "surface=%dx%d rect=%d,%d,%dx%d result=%d",
                          static_cast<unsigned long long>(failed),
                          stage ? stage : "", surfaceWidth, surfaceHeight,
                          presentRect.x, presentRect.y, presentRect.w,
                          presentRect.h, lockResult);
            LogSDLScreenPresenter(message);
        }
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);
        return false;
    }

    CopySurfaceToAndroidBuffer(surface, surfaceWidth, surfaceHeight, pitch,
                               presentRect, buffer);
    const int unlockResult = ANativeWindow_unlockAndPost(window);
    TVPAndroidReleaseFlutterGameSurfaceWindow(window);
    if(unlockResult != 0) {
        const uint64_t failed = ++gFlutterSurfaceFailures;
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

    const uint64_t presented = ++gFlutterSurfacePresented;
    TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(surfaceWidth,
                                                              surfaceHeight);
    if(ShouldLogScreenPresenter(presented)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-surface #%llu stage=%s surface=%dx%d "
                      "flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      flutterWidth, flutterHeight, buffer.width,
                      buffer.height, buffer.stride, buffer.format,
                      presentRect.x, presentRect.y, presentRect.w,
                      presentRect.h);
        LogSDLScreenPresenter(message);
    }
    return true;
#else
    (void)surface;
    (void)surfaceWidth;
    (void)surfaceHeight;
    (void)pitch;
    (void)rect;
    (void)stage;
    return false;
#endif
}
