#include "SDLAndroidFlutterPresenter.h"

#include "NativeLog.h"
#if defined(__ANDROID__)
#include "RenderDiagLog.h"
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#endif

#if defined(__ANDROID__)
extern "C" __attribute__((weak)) ANativeWindow *
TVPAndroidAcquireFlutterGameSurfaceWindow();
extern "C" __attribute__((weak)) void
TVPAndroidReleaseFlutterGameSurfaceWindow(ANativeWindow *window);
extern "C" __attribute__((weak)) void
TVPAndroidGetFlutterGameSurfaceSize(int *width, int *height);
#endif

namespace {

std::atomic_int gPresentedSurfaceWidth{0};
std::atomic_int gPresentedSurfaceHeight{0};
std::atomic_bool gForceFullFramePresent{false};
std::atomic_bool gExternalPresenterPostedFrame{false};
std::atomic_uint64_t gFlutterSurfacePresented{0};
std::atomic_uint64_t gFlutterSurfaceFailures{0};
std::atomic_uint64_t gFlutterSurfaceUnavailable{0};

#if defined(__ANDROID__)
constexpr uint64_t kAndroidEGLAutoDisableFailureLimit = 8;

struct TVPAndroidEGLSurfacePresenterState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
    ANativeWindow *window = nullptr;
    int width = 0;
    int height = 0;
    GLuint program = 0;
    GLint positionAttrib = -1;
    GLint texCoordAttrib = -1;
    GLint textureUniform = -1;
    GLint uvRectUniform = -1;
    GLint flipYUniform = -1;
    GLuint vertexBuffer = 0;
    GLuint uploadTexture = 0;
    int uploadWidth = 0;
    int uploadHeight = 0;
    const iTVPTexture2D *uploadSourceTexture = nullptr;
    std::vector<uint8_t> uploadScratch;
    uint64_t attempts = 0;
    uint64_t presented = 0;
    uint64_t nativePresents = 0;
    uint64_t failures = 0;
    uint64_t unavailable = 0;
    uint64_t recreates = 0;
    uint64_t contextResets = 0;
    uint64_t softwareUploads = 0;
    uint64_t dirtyMarks = 0;
    uint64_t dirtyOverwrites = 0;
    uint64_t cleanSwapChecks = 0;
    uint64_t swapAttempts = 0;
    uint64_t missingSurfaceDirtyDrops = 0;
    bool frameDirty = false;
    uint64_t pendingDirtySerial = 0;
    bool pendingNativeGL = false;
    int pendingWidth = 0;
    int pendingHeight = 0;
    iTVPTexture2D *pendingDirtyTexture = nullptr;
    bool fatal = false;
    bool autoDisabled = false;
    bool lastPresentNativeGL = false;
    bool surfaceHasContent = false;
    bool swapIntervalZeroSet = false;
    std::string fatalReason;
    std::string autoDisabledReason;
};

std::mutex gSDLAndroidEGLPresenterMutex;
TVPAndroidEGLSurfacePresenterState gSDLAndroidEGLPresenterState;
std::once_flag gSDLAndroidEGLPresenterFlagOnce;
bool gSDLAndroidEGLPresenterEnabled = false;
std::once_flag gSDLAndroidEGLFlipYFlagOnce;
bool gSDLAndroidEGLFlipY = false;
std::once_flag gSDLAndroidEGLSoftwareUploadFlagOnce;
bool gSDLAndroidEGLSoftwareUploadEnabled = false;
std::once_flag gSDLAndroidEGLSaveGLStateFlagOnce;
bool gSDLAndroidEGLSaveGLState = false;
std::once_flag gSDLAndroidEGLSwapIntervalZeroFlagOnce;
bool gSDLAndroidEGLSwapIntervalZero = false;
#endif

bool ShouldLogScreenPresenter(uint64_t sequence) {
    return sequence <= 8 || sequence == 16 || sequence == 32 ||
        sequence == 64 || sequence == 128 || (sequence % 256) == 0;
}

void MarkExternalPresenterPostedFrame() {
    gExternalPresenterPostedFrame.store(true, std::memory_order_release);
}

void ClearRememberedPresentedSurface() {
    gPresentedSurfaceWidth.store(0, std::memory_order_relaxed);
    gPresentedSurfaceHeight.store(0, std::memory_order_relaxed);
    gExternalPresenterPostedFrame.store(false, std::memory_order_release);
    gForceFullFramePresent.store(true, std::memory_order_relaxed);
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

SDL_Rect ComputeAndroidAspectViewport(int contentWidth, int contentHeight,
                                      int outputWidth, int outputHeight) {
    if(contentWidth <= 0 || contentHeight <= 0 || outputWidth <= 0 ||
       outputHeight <= 0)
        return FullAndroidSurfaceRect(outputWidth > 0 ? outputWidth : 0,
                                      outputHeight > 0 ? outputHeight : 0);

    int viewportWidth = outputWidth;
    int viewportHeight = outputHeight;
    if(static_cast<int64_t>(contentWidth) * outputHeight >
       static_cast<int64_t>(outputWidth) * contentHeight) {
        viewportHeight = static_cast<int>(
            (static_cast<int64_t>(outputWidth) * contentHeight) /
            contentWidth);
        if(viewportHeight <= 0)
            viewportHeight = 1;
    } else if(static_cast<int64_t>(contentWidth) * outputHeight <
              static_cast<int64_t>(outputWidth) * contentHeight) {
        viewportWidth = static_cast<int>(
            (static_cast<int64_t>(outputHeight) * contentWidth) /
            contentHeight);
        if(viewportWidth <= 0)
            viewportWidth = 1;
    }
    return SDL_Rect{ (outputWidth - viewportWidth) / 2,
                     (outputHeight - viewportHeight) / 2, viewportWidth,
                     viewportHeight };
}

void ForceAndroidBufferRowOpaque(Uint8 *dst, int pixels) {
    if(!dst || pixels <= 0)
        return;
    for(int x = 0; x < pixels; ++x)
        dst[static_cast<size_t>(x) * 4 + 3] = 0xff;
}

void CopyAndroidPixelOpaque(Uint8 *dst, const Uint8 *src) {
    std::memcpy(dst, src, 4);
    dst[3] = 0xff;
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
            ForceAndroidBufferRowOpaque(dst, rect.w);
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
            CopyAndroidPixelOpaque(dst + static_cast<size_t>(x) * 4,
                                   src + static_cast<size_t>(srcX) * 4);
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
            ForceAndroidBufferRowOpaque(dst, rect.w);
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
            CopyAndroidPixelOpaque(dst + static_cast<size_t>(x) * 4,
                                   src + static_cast<size_t>(srcX) * 4);
        }
    }
    return true;
}

void FillAndroidBufferBlack(ANativeWindow_Buffer &buffer) {
    if(!buffer.bits)
        return;
    auto *dstBase = static_cast<Uint8 *>(buffer.bits);
    const int dstWidth = buffer.width > 0 ? buffer.width : 0;
    const int dstHeight = buffer.height > 0 ? buffer.height : 0;
    const int dstPitch = buffer.stride * 4;
    if(dstWidth <= 0 || dstHeight <= 0 || dstPitch <= 0)
        return;
    for(int y = 0; y < dstHeight; ++y) {
        auto *dst = dstBase + static_cast<size_t>(y) * dstPitch;
        for(int x = 0; x < dstWidth; ++x) {
            auto *pixel = dst + static_cast<size_t>(x) * 4;
            pixel[0] = 0;
            pixel[1] = 0;
            pixel[2] = 0;
            pixel[3] = 0xff;
        }
    }
}

bool CopyTextureToAndroidBufferViewport(iTVPTexture2D *texture,
                                        int sourceWidth, int sourceHeight,
                                        int outputWidth, int outputHeight,
                                        ANativeWindow_Buffer &buffer) {
    if(!texture || sourceWidth <= 0 || sourceHeight <= 0 ||
       outputWidth <= 0 || outputHeight <= 0 || !buffer.bits)
        return false;

    if(buffer.width == sourceWidth && buffer.height == sourceHeight) {
        return CopyTextureToAndroidBuffer(
            texture, sourceWidth, sourceHeight,
            FullAndroidSurfaceRect(sourceWidth, sourceHeight), buffer);
    }

    FillAndroidBufferBlack(buffer);
    const int dstWidth = buffer.width > 0 ? buffer.width : outputWidth;
    const int dstHeight = buffer.height > 0 ? buffer.height : outputHeight;
    const SDL_Rect viewport =
        ComputeAndroidAspectViewport(sourceWidth, sourceHeight, dstWidth,
                                     dstHeight);
    if(viewport.w <= 0 || viewport.h <= 0)
        return false;

    auto *dstBase = static_cast<Uint8 *>(buffer.bits);
    const int dstPitch = buffer.stride * 4;
    for(int y = 0; y < viewport.h; ++y) {
        const int srcY =
            static_cast<int>((static_cast<int64_t>(y) * sourceHeight) /
                             viewport.h);
        const auto *src = static_cast<const Uint8 *>(
            texture->GetScanLineForRead(srcY));
        if(!src)
            return false;
        auto *dst = dstBase +
            static_cast<size_t>(viewport.y + y) * dstPitch +
            static_cast<size_t>(viewport.x) * 4;
        for(int x = 0; x < viewport.w; ++x) {
            const int srcX =
                static_cast<int>((static_cast<int64_t>(x) * sourceWidth) /
                                 viewport.w);
            CopyAndroidPixelOpaque(dst + static_cast<size_t>(x) * 4,
                                   src + static_cast<size_t>(srcX) * 4);
        }
    }
    return true;
}

void CopySurfaceToAndroidBufferViewport(SDL_Surface *surface, int sourceWidth,
                                        int sourceHeight, int pitch,
                                        int outputWidth, int outputHeight,
                                        ANativeWindow_Buffer &buffer) {
    if(!surface || !surface->pixels || sourceWidth <= 0 || sourceHeight <= 0 ||
       pitch <= 0 || outputWidth <= 0 || outputHeight <= 0 || !buffer.bits)
        return;

    if(buffer.width == sourceWidth && buffer.height == sourceHeight) {
        CopySurfaceToAndroidBuffer(
            surface, sourceWidth, sourceHeight, pitch,
            FullAndroidSurfaceRect(sourceWidth, sourceHeight), buffer);
        return;
    }

    FillAndroidBufferBlack(buffer);
    const int dstWidth = buffer.width > 0 ? buffer.width : outputWidth;
    const int dstHeight = buffer.height > 0 ? buffer.height : outputHeight;
    const SDL_Rect viewport =
        ComputeAndroidAspectViewport(sourceWidth, sourceHeight, dstWidth,
                                     dstHeight);
    if(viewport.w <= 0 || viewport.h <= 0)
        return;

    auto *dstBase = static_cast<Uint8 *>(buffer.bits);
    const auto *srcBase = static_cast<const Uint8 *>(surface->pixels);
    const int dstPitch = buffer.stride * 4;
    for(int y = 0; y < viewport.h; ++y) {
        const int srcY =
            static_cast<int>((static_cast<int64_t>(y) * sourceHeight) /
                             viewport.h);
        const auto *src = srcBase + static_cast<size_t>(srcY) * pitch;
        auto *dst = dstBase +
            static_cast<size_t>(viewport.y + y) * dstPitch +
            static_cast<size_t>(viewport.x) * 4;
        for(int x = 0; x < viewport.w; ++x) {
            const int srcX =
                static_cast<int>((static_cast<int64_t>(x) * sourceWidth) /
                                 viewport.w);
            CopyAndroidPixelOpaque(dst + static_cast<size_t>(x) * 4,
                                   src + static_cast<size_t>(srcX) * 4);
        }
    }
}
#endif

#if defined(__ANDROID__)
struct TVPAndroidGLAttribSnapshot {
    GLuint index = 0;
    bool valid = false;
    GLint enabled = 0;
    GLint size = 0;
    GLint type = 0;
    GLint normalized = 0;
    GLint stride = 0;
    GLint buffer = 0;
    void *pointer = nullptr;
};

struct TVPAndroidGLStateSnapshot {
    GLint framebuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };
    GLint scissorBox[4] = { 0, 0, 0, 0 };
    GLint activeTexture = GL_TEXTURE0;
    GLint texture0Binding = 0;
    GLint arrayBuffer = 0;
    GLint elementArrayBuffer = 0;
    GLint program = 0;
    GLint blendEquationRGB = GL_FUNC_ADD;
    GLint blendEquationAlpha = GL_FUNC_ADD;
    GLint blendSrcRGB = GL_ONE;
    GLint blendDstRGB = GL_ZERO;
    GLint blendSrcAlpha = GL_ONE;
    GLint blendDstAlpha = GL_ZERO;
    GLint cullFaceMode = GL_BACK;
    GLint frontFace = GL_CCW;
    GLint unpackAlignment = 4;
#if defined(GL_UNPACK_ROW_LENGTH)
    GLint unpackRowLength = 0;
#endif
    GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    GLboolean depthMask = GL_TRUE;
    GLboolean blend = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    TVPAndroidGLAttribSnapshot attribs[2];
};

void LogSDLAndroidEGLPresenter(const char *message) {
    TVPNativeLogInfo("android-egl-presenter", message ? message : "");
}

bool IsAndroidEGLSurfacePresenterEnabled() {
    std::call_once(gSDLAndroidEGLPresenterFlagOnce, []() {
        if(IsTruthyEnv("KRKR2_DISABLE_ANDROID_EGL_SURFACE_PRESENT")) {
            gSDLAndroidEGLPresenterEnabled = false;
            return;
        }

        const char *legacyEnable =
            SDL_getenv("KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT");
        if(legacyEnable &&
           !IsTruthyEnv("KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT")) {
            gSDLAndroidEGLPresenterEnabled = false;
            return;
        }

        gSDLAndroidEGLPresenterEnabled = true;
    });
    return gSDLAndroidEGLPresenterEnabled;
}

bool IsAndroidEGLSurfacePresenterForcedEnabled() {
    return IsTruthyEnv("KRKR2_ENABLE_ANDROID_EGL_SURFACE_PRESENT");
}

bool IsAndroidEGLSurfaceFlipYEnabled() {
    std::call_once(gSDLAndroidEGLFlipYFlagOnce, []() {
        const char *value = SDL_getenv("KRKR2_ANDROID_EGL_SURFACE_FLIP_Y");
        gSDLAndroidEGLFlipY =
            value ? IsTruthyEnv("KRKR2_ANDROID_EGL_SURFACE_FLIP_Y") : false;
    });
    return gSDLAndroidEGLFlipY;
}

bool IsAndroidEGLSoftwareUploadEnabled() {
    std::call_once(gSDLAndroidEGLSoftwareUploadFlagOnce, []() {
        const char *legacy =
            SDL_getenv("KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE");
        if(IsTruthyEnv("KRKR2_DISABLE_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE")) {
            gSDLAndroidEGLSoftwareUploadEnabled = false;
        } else if(legacy) {
            gSDLAndroidEGLSoftwareUploadEnabled =
                IsTruthyEnv("KRKR2_ANDROID_EGL_SURFACE_UPLOAD_SOFTWARE");
        } else {
            gSDLAndroidEGLSoftwareUploadEnabled = true;
        }
    });
    return gSDLAndroidEGLSoftwareUploadEnabled;
}

bool IsAndroidEGLSaveGLStateEnabled() {
    std::call_once(gSDLAndroidEGLSaveGLStateFlagOnce, []() {
        if(IsTruthyEnv("KRKR2_DISABLE_ANDROID_EGL_SAVE_GL_STATE")) {
            gSDLAndroidEGLSaveGLState = false;
            return;
        }
        const char *value = SDL_getenv("KRKR2_ANDROID_EGL_SAVE_GL_STATE");
        gSDLAndroidEGLSaveGLState =
            value ? IsTruthyEnv("KRKR2_ANDROID_EGL_SAVE_GL_STATE") : true;
    });
    return gSDLAndroidEGLSaveGLState;
}

bool IsAndroidEGLSwapIntervalZeroEnabled() {
    std::call_once(gSDLAndroidEGLSwapIntervalZeroFlagOnce, []() {
        if(IsTruthyEnv("KRKR2_DISABLE_ANDROID_EGL_SWAP_INTERVAL_ZERO")) {
            gSDLAndroidEGLSwapIntervalZero = false;
            return;
        }
        const char *value = SDL_getenv("KRKR2_ANDROID_EGL_SWAP_INTERVAL_ZERO");
        if(value) {
            gSDLAndroidEGLSwapIntervalZero =
                IsTruthyEnv("KRKR2_ANDROID_EGL_SWAP_INTERVAL_ZERO");
        } else if(SDL_getenv("KRKR2_ENABLE_ANDROID_EGL_SWAP_INTERVAL_ZERO")) {
            gSDLAndroidEGLSwapIntervalZero =
                IsTruthyEnv("KRKR2_ENABLE_ANDROID_EGL_SWAP_INTERVAL_ZERO");
        } else {
            gSDLAndroidEGLSwapIntervalZero = false;
        }
    });
    return gSDLAndroidEGLSwapIntervalZero;
}

#if defined(GL_UNPACK_ROW_LENGTH)
bool AndroidGLStringHasToken(const char *text, const char *token) {
    if(!text || !token || !*token)
        return false;
    const size_t tokenLength = std::strlen(token);
    const char *cursor = text;
    while((cursor = std::strstr(cursor, token)) != nullptr) {
        const bool leftBoundary = cursor == text || cursor[-1] == ' ';
        const char right = cursor[tokenLength];
        const bool rightBoundary = right == '\0' || right == ' ';
        if(leftBoundary && rightBoundary)
            return true;
        cursor += tokenLength;
    }
    return false;
}

bool IsAndroidGLUnpackRowLengthSupported() {
    static std::once_flag flag;
    static bool supported = false;
    std::call_once(flag, []() {
        const char *version =
            reinterpret_cast<const char *>(glGetString(GL_VERSION));
        const char *extensions =
            reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
        supported =
            (version && (std::strstr(version, "OpenGL ES 3.") ||
                         std::strstr(version, "OpenGL ES 4."))) ||
            AndroidGLStringHasToken(extensions, "GL_EXT_unpack_subimage");
    });
    return supported;
}
#endif

TVPSDLPresentRect ToPresentRect(const SDL_Rect &rect) {
    return TVPSDLPresentRect{ rect.x, rect.y, rect.w, rect.h };
}

SDL_Rect ToSDLRect(const TVPSDLPresentRect &rect) {
    return SDL_Rect{ rect.x, rect.y, rect.w, rect.h };
}

TVPSDLPresentRect FullPresentRect(int width, int height) {
    return TVPSDLPresentRect{ 0, 0, width, height };
}

const char *AndroidEGLErrorName(EGLint error) {
    switch(error) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "EGL_UNKNOWN";
    }
}

std::string AndroidEGLFormatError(const char *stage, EGLint error) {
    char message[160];
    std::snprintf(message, sizeof(message), "%s: %s(0x%x)",
                  stage ? stage : "egl", AndroidEGLErrorName(error), error);
    return message;
}

void LogAndroidEGLFailureLocked(const char *stage, const std::string &reason) {
    auto &state = gSDLAndroidEGLPresenterState;
    const uint64_t failures = ++state.failures;
    if(ShouldLogScreenPresenter(failures)) {
        char message[384];
        std::snprintf(message, sizeof(message),
                      "failure #%llu stage=%s reason=%s",
                      static_cast<unsigned long long>(failures),
                      stage ? stage : "", reason.c_str());
        LogSDLAndroidEGLPresenter(message);
    }
    if(!state.autoDisabled && state.presented == 0 &&
       failures >= kAndroidEGLAutoDisableFailureLimit &&
       !IsAndroidEGLSurfacePresenterForcedEnabled()) {
        state.autoDisabled = true;
        state.autoDisabledReason = reason;
        char message[384];
        std::snprintf(
            message, sizeof(message),
            "auto-disabled failures=%llu stage=%s reason=%s fallback=flutter-cpu",
            static_cast<unsigned long long>(failures), stage ? stage : "",
            reason.c_str());
        LogSDLAndroidEGLPresenter(message);
    }
}

void SaveAndroidGLAttrib(GLuint index, TVPAndroidGLAttribSnapshot &snapshot) {
    snapshot.index = index;
    snapshot.valid = true;
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                        &snapshot.enabled);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &snapshot.size);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &snapshot.type);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                        &snapshot.normalized);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &snapshot.stride);
    glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                        &snapshot.buffer);
    glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                              &snapshot.pointer);
}

void RestoreAndroidGLAttrib(const TVPAndroidGLAttribSnapshot &snapshot) {
    if(!snapshot.valid)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(snapshot.buffer));
    glVertexAttribPointer(snapshot.index, snapshot.size,
                          static_cast<GLenum>(snapshot.type),
                          static_cast<GLboolean>(snapshot.normalized),
                          snapshot.stride, snapshot.pointer);
    if(snapshot.enabled)
        glEnableVertexAttribArray(snapshot.index);
    else
        glDisableVertexAttribArray(snapshot.index);
}

TVPAndroidGLStateSnapshot SaveAndroidGLState(GLint positionAttrib,
                                             GLint texCoordAttrib) {
    TVPAndroidGLStateSnapshot snapshot;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &snapshot.framebuffer);
    glGetIntegerv(GL_VIEWPORT, snapshot.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, snapshot.scissorBox);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &snapshot.activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &snapshot.texture0Binding);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &snapshot.arrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
                  &snapshot.elementArrayBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &snapshot.program);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &snapshot.blendEquationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &snapshot.blendEquationAlpha);
    glGetIntegerv(GL_BLEND_SRC_RGB, &snapshot.blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &snapshot.blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &snapshot.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &snapshot.blendDstAlpha);
    glGetIntegerv(GL_CULL_FACE_MODE, &snapshot.cullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &snapshot.frontFace);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &snapshot.unpackAlignment);
#if defined(GL_UNPACK_ROW_LENGTH)
    if(IsAndroidGLUnpackRowLengthSupported())
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &snapshot.unpackRowLength);
#endif
    glGetFloatv(GL_COLOR_CLEAR_VALUE, snapshot.clearColor);
    glGetBooleanv(GL_COLOR_WRITEMASK, snapshot.colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &snapshot.depthMask);
    snapshot.blend = glIsEnabled(GL_BLEND);
    snapshot.depthTest = glIsEnabled(GL_DEPTH_TEST);
    snapshot.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    snapshot.cullFace = glIsEnabled(GL_CULL_FACE);
    snapshot.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    if(positionAttrib >= 0)
        SaveAndroidGLAttrib(static_cast<GLuint>(positionAttrib),
                            snapshot.attribs[0]);
    if(texCoordAttrib >= 0 && texCoordAttrib != positionAttrib)
        SaveAndroidGLAttrib(static_cast<GLuint>(texCoordAttrib),
                            snapshot.attribs[1]);
    return snapshot;
}

void RestoreAndroidGLState(const TVPAndroidGLStateSnapshot &snapshot) {
    RestoreAndroidGLAttrib(snapshot.attribs[0]);
    RestoreAndroidGLAttrib(snapshot.attribs[1]);
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(snapshot.arrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLuint>(snapshot.elementArrayBuffer));
    glUseProgram(static_cast<GLuint>(snapshot.program));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,
                  static_cast<GLuint>(snapshot.texture0Binding));
    glActiveTexture(static_cast<GLenum>(snapshot.activeTexture));
    glBlendEquationSeparate(static_cast<GLenum>(snapshot.blendEquationRGB),
                            static_cast<GLenum>(snapshot.blendEquationAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(snapshot.blendSrcRGB),
                        static_cast<GLenum>(snapshot.blendDstRGB),
                        static_cast<GLenum>(snapshot.blendSrcAlpha),
                        static_cast<GLenum>(snapshot.blendDstAlpha));
    glCullFace(static_cast<GLenum>(snapshot.cullFaceMode));
    glFrontFace(static_cast<GLenum>(snapshot.frontFace));
    glPixelStorei(GL_UNPACK_ALIGNMENT, snapshot.unpackAlignment);
#if defined(GL_UNPACK_ROW_LENGTH)
    if(IsAndroidGLUnpackRowLengthSupported())
        glPixelStorei(GL_UNPACK_ROW_LENGTH, snapshot.unpackRowLength);
#endif
    glClearColor(snapshot.clearColor[0], snapshot.clearColor[1],
                 snapshot.clearColor[2], snapshot.clearColor[3]);
    glColorMask(snapshot.colorMask[0], snapshot.colorMask[1],
                snapshot.colorMask[2], snapshot.colorMask[3]);
    glDepthMask(snapshot.depthMask);
    if(snapshot.blend)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    if(snapshot.depthTest)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    if(snapshot.scissorTest)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    if(snapshot.cullFace)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    if(snapshot.stencilTest)
        glEnable(GL_STENCIL_TEST);
    else
        glDisable(GL_STENCIL_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,
                      static_cast<GLuint>(snapshot.framebuffer));
    glViewport(snapshot.viewport[0], snapshot.viewport[1], snapshot.viewport[2],
               snapshot.viewport[3]);
    glScissor(snapshot.scissorBox[0], snapshot.scissorBox[1],
              snapshot.scissorBox[2], snapshot.scissorBox[3]);
}

bool RestoreAndroidEGLCurrentLocked(
    EGLDisplay display, EGLSurface drawSurface, EGLSurface readSurface,
    EGLContext context, const TVPAndroidGLStateSnapshot *glState,
    const char *stage) {
    if(eglMakeCurrent(display, drawSurface, readSurface, context) == EGL_TRUE) {
        if(glState)
            RestoreAndroidGLState(*glState);
        return true;
    }
    const std::string reason =
        AndroidEGLFormatError("eglMakeCurrent(restore)", eglGetError());
    LogAndroidEGLFailureLocked(stage, reason);
    gSDLAndroidEGLPresenterState.fatal = true;
    gSDLAndroidEGLPresenterState.fatalReason = reason;
    return false;
}

bool CopyTextureRegionToAndroidEGLScratch(iTVPTexture2D *texture,
                                          TVPTextureFormat::e format,
                                          int width, int height,
                                          const SDL_Rect &rect,
                                          std::vector<uint8_t> &scratch) {
    if(!texture || format != TVPTextureFormat::RGBA || width <= 0 ||
       height <= 0 || rect.x < 0 || rect.y < 0 || rect.w <= 0 ||
       rect.h <= 0 || rect.x + rect.w > width || rect.y + rect.h > height)
        return false;
    const size_t rowBytes = static_cast<size_t>(rect.w) * 4;
    scratch.resize(rowBytes * static_cast<size_t>(rect.h));
    for(int row = 0; row < rect.h; ++row) {
        const auto *line = static_cast<const uint8_t *>(
            texture->GetScanLineForRead(rect.y + row));
        if(!line)
            return false;
        line += static_cast<size_t>(rect.x) * 4;
        std::memcpy(scratch.data() + rowBytes * static_cast<size_t>(row),
                    line, rowBytes);
    }
    return true;
}

bool GetTextureRegionUploadPointer(iTVPTexture2D *texture,
                                   TVPTextureFormat::e format, int width,
                                   int height, const SDL_Rect &rect,
                                   const uint8_t *&pixels, int &pitch) {
    pixels = nullptr;
    pitch = 0;
    if(!texture || format != TVPTextureFormat::RGBA || width <= 0 ||
       height <= 0 || rect.x < 0 || rect.y < 0 || rect.w <= 0 ||
       rect.h <= 0 || rect.x + rect.w > width || rect.y + rect.h > height)
        return false;

    const int sourcePitch = texture->GetPitch();
    const int rowBytes = rect.w * 4;
    if(sourcePitch < width * 4 || sourcePitch < rowBytes)
        return false;

    if(sourcePitch != rowBytes) {
#if defined(GL_UNPACK_ROW_LENGTH)
        if(!IsAndroidGLUnpackRowLengthSupported() || (sourcePitch % 4) != 0)
            return false;
#else
        return false;
#endif
    }

    const auto *line = static_cast<const uint8_t *>(
        texture->GetScanLineForRead(rect.y));
    if(!line)
        return false;
    pixels = line + static_cast<size_t>(rect.x) * 4;
    pitch = sourcePitch;
    return true;
}

GLuint CompileAndroidEGLShaderLocked(GLenum type, const char *source,
                                     const char *stage) {
    GLuint shader = glCreateShader(type);
    if(!shader) {
        LogAndroidEGLFailureLocked(stage, "glCreateShader returned 0");
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if(compiled == GL_TRUE)
        return shader;

    char info[512] = {};
    glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
    glDeleteShader(shader);
    std::string reason = "shader compile failed: ";
    reason += info[0] ? info : "(no log)";
    LogAndroidEGLFailureLocked(stage, reason);
    gSDLAndroidEGLPresenterState.fatal = true;
    gSDLAndroidEGLPresenterState.fatalReason = reason;
    return 0;
}

bool EnsureAndroidEGLProgramLocked(const char *stage) {
    auto &state = gSDLAndroidEGLPresenterState;
    if(state.program)
        return true;

    static const char *kVertexShader =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "uniform vec4 uUvRect;\n"
        "uniform float uFlipY;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "  float v = uFlipY > 0.5 ? (1.0 - aTexCoord.y) : aTexCoord.y;\n"
        "  vTexCoord = vec2(mix(uUvRect.x, uUvRect.z, aTexCoord.x),\n"
        "                   mix(uUvRect.y, uUvRect.w, v));\n"
        "}\n";
    static const char *kFragmentShader =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "uniform sampler2D uTexture;\n"
        "void main() {\n"
        "  vec4 color = texture2D(uTexture, vTexCoord);\n"
        "  gl_FragColor = vec4(color.rgb, 1.0);\n"
        "}\n";

    GLuint vertex = CompileAndroidEGLShaderLocked(GL_VERTEX_SHADER,
                                                  kVertexShader, stage);
    if(!vertex)
        return false;
    GLuint fragment = CompileAndroidEGLShaderLocked(GL_FRAGMENT_SHADER,
                                                    kFragmentShader, stage);
    if(!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    GLuint program = glCreateProgram();
    if(!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        LogAndroidEGLFailureLocked(stage, "glCreateProgram returned 0");
        state.fatal = true;
        state.fatalReason = "glCreateProgram returned 0";
        return false;
    }

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aTexCoord");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if(linked != GL_TRUE) {
        char info[512] = {};
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        glDeleteProgram(program);
        std::string reason = "program link failed: ";
        reason += info[0] ? info : "(no log)";
        LogAndroidEGLFailureLocked(stage, reason);
        state.fatal = true;
        state.fatalReason = reason;
        return false;
    }

    state.program = program;
    state.positionAttrib = glGetAttribLocation(program, "aPosition");
    state.texCoordAttrib = glGetAttribLocation(program, "aTexCoord");
    state.textureUniform = glGetUniformLocation(program, "uTexture");
    state.uvRectUniform = glGetUniformLocation(program, "uUvRect");
    state.flipYUniform = glGetUniformLocation(program, "uFlipY");
    if(state.positionAttrib < 0 || state.texCoordAttrib < 0) {
        glDeleteProgram(state.program);
        state.program = 0;
        std::string reason = "program missing required attributes";
        LogAndroidEGLFailureLocked(stage, reason);
        state.fatal = true;
        state.fatalReason = reason;
        return false;
    }

    if(!state.vertexBuffer)
        glGenBuffers(1, &state.vertexBuffer);
    if(!state.vertexBuffer) {
        LogAndroidEGLFailureLocked(stage, "glGenBuffers returned 0");
        state.fatal = true;
        state.fatalReason = "glGenBuffers returned 0";
        return false;
    }

    static const GLfloat kVertices[] = {
        -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  1.0f,  1.0f, 0.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, state.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    char message[192];
    std::snprintf(message, sizeof(message),
                  "program ready stage=%s program=%u attribs=%d,%d",
                  stage ? stage : "", state.program, state.positionAttrib,
                  state.texCoordAttrib);
    LogSDLAndroidEGLPresenter(message);
    return true;
}

EGLConfig FindAndroidEGLConfig(EGLDisplay display, EGLSurface currentSurface) {
    EGLint configId = 0;
    if(display != EGL_NO_DISPLAY && currentSurface != EGL_NO_SURFACE &&
       eglQuerySurface(display, currentSurface, EGL_CONFIG_ID, &configId) &&
       configId > 0) {
        EGLint configCount = 0;
        if(eglGetConfigs(display, nullptr, 0, &configCount) &&
           configCount > 0) {
            std::vector<EGLConfig> configs(static_cast<size_t>(configCount));
            EGLint returned = 0;
            if(eglGetConfigs(display, configs.data(), configCount, &returned)) {
                for(EGLint index = 0; index < returned; ++index) {
                    EGLint candidateId = 0;
                    if(eglGetConfigAttrib(display, configs[index],
                                          EGL_CONFIG_ID, &candidateId) &&
                       candidateId == configId)
                        return configs[index];
                }
            }
        }
    }

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if(eglChooseConfig(display, configAttribs, &config, 1, &configCount) &&
       configCount > 0)
        return config;
    return nullptr;
}

bool CanDeleteAndroidEGLGLResourcesLocked() {
    const auto &state = gSDLAndroidEGLPresenterState;
    return state.display != EGL_NO_DISPLAY && state.context != EGL_NO_CONTEXT &&
        eglGetCurrentDisplay() == state.display &&
        eglGetCurrentContext() == state.context;
}

void DestroyAndroidEGLWindowSurfaceLocked(const char *reason) {
    auto &state = gSDLAndroidEGLPresenterState;
    if(state.surface != EGL_NO_SURFACE && state.display != EGL_NO_DISPLAY) {
        const EGLDisplay currentDisplay = eglGetCurrentDisplay();
        const EGLSurface currentDraw = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface currentRead = eglGetCurrentSurface(EGL_READ);
        if(currentDisplay == state.display &&
           (currentDraw == state.surface || currentRead == state.surface)) {
            eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
        }
        eglDestroySurface(state.display, state.surface);
        state.surface = EGL_NO_SURFACE;
    }
    if(state.window) {
        ANativeWindow_release(state.window);
        state.window = nullptr;
    }
    state.width = 0;
    state.height = 0;
    state.surfaceHasContent = false;
    state.swapIntervalZeroSet = false;
    state.frameDirty = false;
    state.pendingNativeGL = false;
    state.pendingWidth = 0;
    state.pendingHeight = 0;
    state.pendingDirtyTexture = nullptr;
    if(reason && *reason) {
        char message[192];
        std::snprintf(message, sizeof(message), "surface dropped reason=%s",
                      reason);
        LogSDLAndroidEGLPresenter(message);
    }
}

void ResetAndroidEGLContextResourcesLocked(const char *reason) {
    auto &state = gSDLAndroidEGLPresenterState;
    DestroyAndroidEGLWindowSurfaceLocked(reason);
    if(CanDeleteAndroidEGLGLResourcesLocked()) {
        if(state.uploadTexture)
            glDeleteTextures(1, &state.uploadTexture);
        if(state.vertexBuffer)
            glDeleteBuffers(1, &state.vertexBuffer);
        if(state.program)
            glDeleteProgram(state.program);
    }
    state.display = EGL_NO_DISPLAY;
    state.context = EGL_NO_CONTEXT;
    state.config = nullptr;
    state.program = 0;
    state.positionAttrib = -1;
    state.texCoordAttrib = -1;
    state.textureUniform = -1;
    state.uvRectUniform = -1;
    state.flipYUniform = -1;
    state.vertexBuffer = 0;
    state.uploadTexture = 0;
    state.uploadWidth = 0;
    state.uploadHeight = 0;
    state.uploadSourceTexture = nullptr;
    state.swapIntervalZeroSet = false;
    state.uploadScratch.clear();
    state.frameDirty = false;
    state.pendingNativeGL = false;
    state.pendingWidth = 0;
    state.pendingHeight = 0;
    state.pendingDirtyTexture = nullptr;
}

bool EnsureAndroidEGLUploadTextureLocked(int width, int height,
                                         const SDL_Rect &rect,
                                         const uint8_t *pixels,
                                         int pitch,
                                         const char *stage) {
    auto &state = gSDLAndroidEGLPresenterState;
    if(!pixels || width <= 0 || height <= 0 || rect.x < 0 || rect.y < 0 ||
       rect.w <= 0 || rect.h <= 0 || rect.x + rect.w > width ||
       rect.y + rect.h > height)
        return false;
    const int rowBytes = rect.w * 4;
    if(pitch < rowBytes)
        return false;
    if(!state.uploadTexture) {
        glGenTextures(1, &state.uploadTexture);
        if(!state.uploadTexture) {
            LogAndroidEGLFailureLocked(stage, "glGenTextures returned 0");
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, state.uploadTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, state.uploadTexture);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#if defined(GL_UNPACK_ROW_LENGTH)
    const bool useRowLength =
        pitch != rowBytes && IsAndroidGLUnpackRowLengthSupported();
    if(useRowLength)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
#else
    const bool useRowLength = false;
#endif
    if(state.uploadWidth != width || state.uploadHeight != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        state.uploadWidth = width;
        state.uploadHeight = height;
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.w, rect.h,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#if defined(GL_UNPACK_ROW_LENGTH)
    if(useRowLength)
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
    (void)useRowLength;
#endif
    ++state.softwareUploads;
    if(IsTruthyEnv("KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS"))
        return glGetError() == GL_NO_ERROR;
    return true;
}

bool EnsureAndroidEGLSurfacePresenterLocked(ANativeWindow *window, int width,
                                            int height, const char *stage) {
    auto &state = gSDLAndroidEGLPresenterState;
    if(!window || width <= 0 || height <= 0)
        return false;

    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface currentDraw = eglGetCurrentSurface(EGL_DRAW);
    if(display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT ||
       currentDraw == EGL_NO_SURFACE) {
        LogAndroidEGLFailureLocked(
            stage,
            "no current EGL display/context/surface on presenter thread");
        return false;
    }

    if(state.display != display || state.context != context) {
        ++state.contextResets;
        ResetAndroidEGLContextResourcesLocked("context-change");
        state.display = display;
        state.context = context;
        state.config = FindAndroidEGLConfig(display, currentDraw);
        if(!state.config) {
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglChooseConfig", eglGetError()));
            return false;
        }
    }

    const bool recreateSurface = state.surface == EGL_NO_SURFACE ||
        state.window != window || state.width != width ||
        state.height != height;
    if(recreateSurface) {
        ++state.recreates;
        DestroyAndroidEGLWindowSurfaceLocked("recreate");
        ANativeWindow_setBuffersGeometry(window, width, height,
                                         WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_acquire(window);
        state.window = window;
        state.width = width;
        state.height = height;
        const EGLint surfaceAttribs[] = { EGL_NONE };
        state.surface = eglCreateWindowSurface(display, state.config, window,
                                               surfaceAttribs);
        if(state.surface == EGL_NO_SURFACE) {
            ANativeWindow_release(state.window);
            state.window = nullptr;
            state.width = 0;
            state.height = 0;
            LogAndroidEGLFailureLocked(
                stage,
                AndroidEGLFormatError("eglCreateWindowSurface", eglGetError()));
            return false;
        }
        state.surfaceHasContent = false;
        char message[256];
        std::snprintf(message, sizeof(message),
                      "surface ready #%llu stage=%s window=%p size=%dx%d "
                      "fullFrame=1",
                      static_cast<unsigned long long>(state.recreates),
                      stage ? stage : "", static_cast<void *>(window),
                      width, height);
        LogSDLAndroidEGLPresenter(message);
    }

    return true;
}

void CleanupAndroidEGLBlitStateLocked(
    const TVPAndroidEGLSurfacePresenterState &state) {
    if(state.positionAttrib >= 0)
        glDisableVertexAttribArray(static_cast<GLuint>(state.positionAttrib));
    if(state.texCoordAttrib >= 0 &&
       state.texCoordAttrib != state.positionAttrib)
        glDisableVertexAttribArray(static_cast<GLuint>(state.texCoordAttrib));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

uint64_t MarkAndroidEGLFrameDirtyLocked(
    TVPAndroidEGLSurfacePresenterState &state, iTVPTexture2D *texture,
    bool nativeGL, int width, int height, bool *overwroteDirty,
    uint64_t *previousDirtySerial, uint64_t *dirtyOverwriteCount) {
    const bool wasDirty = state.frameDirty;
    const uint64_t previousSerial = state.pendingDirtySerial;
    const uint64_t serial = ++state.dirtyMarks;
    uint64_t overwrites = state.dirtyOverwrites;
    if(wasDirty)
        overwrites = ++state.dirtyOverwrites;
    state.frameDirty = true;
    state.pendingDirtySerial = serial;
    state.pendingNativeGL = nativeGL;
    state.pendingWidth = width;
    state.pendingHeight = height;
    state.pendingDirtyTexture = texture;
    if(overwroteDirty)
        *overwroteDirty = wasDirty;
    if(previousDirtySerial)
        *previousDirtySerial = previousSerial;
    if(dirtyOverwriteCount)
        *dirtyOverwriteCount = overwrites;
    return serial;
}

bool TryPresentAndroidEGLSurfaceTexture(iTVPTexture2D *texture,
                                        TVPTextureFormat::e format,
                                        int surfaceWidth, int surfaceHeight,
                                        int outputWidth, int outputHeight,
                                        const SDL_Rect &rect,
                                        const char *stage,
                                        bool *usedFullFrame) {
    if(!IsAndroidEGLSurfacePresenterEnabled())
        return false;
    if(!texture || surfaceWidth <= 0 || surfaceHeight <= 0 ||
       format != TVPTextureFormat::RGBA || rect.w <= 0 || rect.h <= 0)
        return false;
    outputWidth = kTVPSDLFixedGameSurfaceWidth;
    outputHeight = kTVPSDLFixedGameSurfaceHeight;

    const GLuint nativeTexture =
        static_cast<GLuint>(texture->GetNativeGLTextureId());
    const bool softwareUpload =
        nativeTexture == 0 && IsAndroidEGLSoftwareUploadEnabled();
    if(nativeTexture == 0 && !softwareUpload) {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        const uint64_t unavailable =
            ++gSDLAndroidEGLPresenterState.unavailable;
        if(ShouldLogScreenPresenter(unavailable)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "unavailable #%llu stage=%s reason=no-native-gl "
                          "surface=%dx%d",
                          static_cast<unsigned long long>(unavailable),
                          stage ? stage : "", surfaceWidth, surfaceHeight);
            LogSDLAndroidEGLPresenter(message);
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        if(gSDLAndroidEGLPresenterState.autoDisabled)
            return false;
    }

    if(!TVPAndroidAcquireFlutterGameSurfaceWindow ||
       !TVPAndroidReleaseFlutterGameSurfaceWindow ||
       !TVPAndroidGetFlutterGameSurfaceSize)
        return false;
    ANativeWindow *window = TVPAndroidAcquireFlutterGameSurfaceWindow();
    if(!window) {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        const uint64_t unavailable =
            ++gSDLAndroidEGLPresenterState.unavailable;
        if(ShouldLogScreenPresenter(unavailable)) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "unavailable #%llu stage=%s reason=no-window "
                          "surface=%dx%d",
                          static_cast<unsigned long long>(unavailable),
                          stage ? stage : "", surfaceWidth, surfaceHeight);
            LogSDLAndroidEGLPresenter(message);
        }
        return false;
    }

    bool presented = false;
    bool copiedSoftware = false;
    bool fullFramePresent = true;
    SDL_Rect softwareUploadRect = rect;
    uint64_t queuedCount = 0;
    uint64_t softwareUploadCount = 0;
    float uvScaleU = 1.0f;
    float uvScaleV = 1.0f;
    bool flipY = false;
    SDL_Rect viewport = FullAndroidSurfaceRect(outputWidth, outputHeight);
    const uint8_t *softwareUploadPixels = nullptr;
    int softwareUploadPitch = 0;
    uint64_t dirtySerial = 0;
    bool dirtyOverwritten = false;
    uint64_t previousDirtySerial = 0;
    uint64_t dirtyOverwriteCount = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        auto &state = gSDLAndroidEGLPresenterState;
        queuedCount = ++state.attempts;
        if(state.autoDisabled) {
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
        if(state.fatal) {
            LogAndroidEGLFailureLocked(stage, state.fatalReason);
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }

        if(nativeTexture != 0) {
            state.uploadSourceTexture = nullptr;
            TVPGetRenderManager()->PrepareTextureForExternalPresenter(texture);
            const tjs_uint internalWidth = texture->GetInternalWidth();
            const tjs_uint internalHeight = texture->GetInternalHeight();
            if(internalWidth > 0 && internalHeight > 0) {
                uvScaleU =
                    static_cast<float>(surfaceWidth) /
                    static_cast<float>(internalWidth);
                uvScaleV =
                    static_cast<float>(surfaceHeight) /
                    static_cast<float>(internalHeight);
            }
        }

        if(softwareUpload) {
            // The EGL path blits the source texture as a complete frame; keep
            // the software upload texture complete too.
            const bool needsFullUpload =
                fullFramePresent ||
                state.uploadSourceTexture != texture ||
                state.uploadWidth != surfaceWidth ||
                state.uploadHeight != surfaceHeight;
            if(needsFullUpload)
                softwareUploadRect =
                    FullAndroidSurfaceRect(surfaceWidth, surfaceHeight);
        }

        const EGLDisplay previousDisplay = eglGetCurrentDisplay();
        const EGLContext previousContext = eglGetCurrentContext();
        const EGLSurface previousDraw = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface previousRead = eglGetCurrentSurface(EGL_READ);

        if(!EnsureAndroidEGLSurfacePresenterLocked(window, outputWidth,
                                                   outputHeight, stage)) {
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }

        const bool samePresenterContext =
            previousContext != EGL_NO_CONTEXT &&
            previousContext == state.context;
        const bool savePresenterGLState =
            samePresenterContext && IsAndroidEGLSaveGLStateEnabled();
        TVPAndroidGLStateSnapshot glState;
        if(savePresenterGLState)
            glState = SaveAndroidGLState(0, 1);

        if(!eglMakeCurrent(state.display, state.surface, state.surface,
                           state.context)) {
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglMakeCurrent", eglGetError()));
            if(savePresenterGLState)
                RestoreAndroidGLState(glState);
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
        if(!EnsureAndroidEGLProgramLocked(stage)) {
            RestoreAndroidEGLCurrentLocked(
                previousDisplay, previousDraw, previousRead, previousContext,
                savePresenterGLState ? &glState : nullptr, stage);
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
        if(!state.swapIntervalZeroSet &&
           IsAndroidEGLSwapIntervalZeroEnabled()) {
            eglSwapInterval(state.display, 0);
            state.swapIntervalZeroSet = true;
        }
        GLuint sourceTexture = nativeTexture;
        if(softwareUpload) {
            if(!GetTextureRegionUploadPointer(
                   texture, format, surfaceWidth, surfaceHeight,
                   softwareUploadRect, softwareUploadPixels,
                   softwareUploadPitch)) {
                if(!CopyTextureRegionToAndroidEGLScratch(
                       texture, format, surfaceWidth, surfaceHeight,
                       softwareUploadRect, state.uploadScratch)) {
                    LogAndroidEGLFailureLocked(
                        stage, "software texture copy failed");
                    RestoreAndroidEGLCurrentLocked(
                        previousDisplay, previousDraw, previousRead,
                        previousContext,
                        savePresenterGLState ? &glState : nullptr, stage);
                    TVPAndroidReleaseFlutterGameSurfaceWindow(window);
                    return false;
                }
                softwareUploadPixels = state.uploadScratch.data();
                softwareUploadPitch = softwareUploadRect.w * 4;
            }
            if(!EnsureAndroidEGLUploadTextureLocked(
                   surfaceWidth, surfaceHeight, softwareUploadRect,
                   softwareUploadPixels, softwareUploadPitch, stage)) {
                LogAndroidEGLFailureLocked(
                    stage, "software texture upload failed");
                RestoreAndroidEGLCurrentLocked(
                    previousDisplay, previousDraw, previousRead,
                    previousContext,
                    savePresenterGLState ? &glState : nullptr, stage);
                TVPAndroidReleaseFlutterGameSurfaceWindow(window);
                return false;
            }
            sourceTexture = state.uploadTexture;
            state.uploadSourceTexture = texture;
            copiedSoftware = true;
            softwareUploadCount = state.softwareUploads;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, outputWidth, outputHeight);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        viewport = ComputeAndroidAspectViewport(surfaceWidth, surfaceHeight,
                                                outputWidth, outputHeight);
        glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
        glUseProgram(state.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceTexture);
        glUniform1i(state.textureUniform, 0);
        glUniform4f(state.uvRectUniform, 0.0f, 0.0f, uvScaleU, uvScaleV);
        flipY = IsAndroidEGLSurfaceFlipYEnabled();
        glUniform1f(state.flipYUniform, flipY ? 1.0f : 0.0f);
        glBindBuffer(GL_ARRAY_BUFFER, state.vertexBuffer);
        glEnableVertexAttribArray(static_cast<GLuint>(state.positionAttrib));
        glVertexAttribPointer(static_cast<GLuint>(state.positionAttrib), 2,
                              GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              reinterpret_cast<void *>(0));
        glEnableVertexAttribArray(static_cast<GLuint>(state.texCoordAttrib));
        glVertexAttribPointer(
            static_cast<GLuint>(state.texCoordAttrib), 2, GL_FLOAT, GL_FALSE,
            4 * sizeof(GLfloat),
            reinterpret_cast<void *>(2 * sizeof(GLfloat)));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CleanupAndroidEGLBlitStateLocked(state);

        GLenum glError = GL_NO_ERROR;
        if(IsTruthyEnv("KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS"))
            glError = glGetError();
        if(glError == GL_NO_ERROR)
            glFlush();

        if(glError != GL_NO_ERROR) {
            char reason[160];
            std::snprintf(reason, sizeof(reason), "gl draw failed: 0x%x",
                          glError);
            LogAndroidEGLFailureLocked(stage, reason);
            RestoreAndroidEGLCurrentLocked(
                previousDisplay, previousDraw, previousRead, previousContext,
                savePresenterGLState ? &glState : nullptr, stage);
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }

        // Swap while the external SurfaceTexture producer is still current.
        // Restoring the engine surface before eglSwapBuffers is unsafe on many
        // Android drivers and yields black or half-stale frames.
        const EGLBoolean swapped =
            eglSwapBuffers(state.display, state.surface);
        const EGLint swapError =
            swapped == EGL_TRUE ? EGL_SUCCESS : eglGetError();

        const bool restored = RestoreAndroidEGLCurrentLocked(
            previousDisplay, previousDraw, previousRead, previousContext,
            savePresenterGLState ? &glState : nullptr, stage);
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);

        if(swapped != EGL_TRUE) {
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglSwapBuffers", swapError));
            DestroyAndroidEGLWindowSurfaceLocked("immediate-swap-failed");
            return false;
        }

        if(!restored) {
            LogAndroidEGLFailureLocked(
                stage,
                "external swap posted but EGL restore failed");
        }

        // Consume any previously deferred dirty mark so later frame-end drains
        // do not re-swap an already-posted buffer.
        if(state.frameDirty) {
            dirtyOverwritten = true;
            previousDirtySerial = state.pendingDirtySerial;
            dirtyOverwriteCount = ++state.dirtyOverwrites;
        }
        dirtySerial = ++state.dirtyMarks;
        state.frameDirty = false;
        state.pendingDirtySerial = 0;
        state.pendingNativeGL = false;
        state.pendingWidth = 0;
        state.pendingHeight = 0;
        state.pendingDirtyTexture = nullptr;
        state.surfaceHasContent = true;
        state.lastPresentNativeGL = nativeTexture != 0;
        if(nativeTexture != 0)
            ++state.nativePresents;
        ++state.presented;
        presented = true;
    }

    if(usedFullFrame)
        *usedFullFrame = fullFramePresent;

    if(presented) {
        MarkExternalPresenterPostedFrame();
        TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(
            kTVPSDLFixedGameSurfaceWidth, kTVPSDLFixedGameSurfaceHeight);
        tTVPRect consumed;
        texture->ConsumeDirtyRect(consumed);
    }

    if(dirtyOverwritten && ShouldLogScreenPresenter(dirtyOverwriteCount)) {
        char message[320];
        std::snprintf(message, sizeof(message),
                      "sync-overwrite-android-egl #%llu stage=%s "
                      "oldDirty=%llu newDirty=%llu queued=%llu "
                      "nativeGL=%u surface=%dx%d output=%dx%d",
                      static_cast<unsigned long long>(dirtyOverwriteCount),
                      stage ? stage : "",
                      static_cast<unsigned long long>(previousDirtySerial),
                      static_cast<unsigned long long>(dirtySerial),
                      static_cast<unsigned long long>(queuedCount),
                      nativeTexture, surfaceWidth, surfaceHeight, outputWidth,
                      outputHeight);
        LogSDLAndroidEGLPresenter(message);
    }

    // Dense early-frame present logs for color/upload diagnosis.
    if(presented &&
       (queuedCount <= 32 || (queuedCount & 0x3F) == 0 ||
        ShouldLogScreenPresenter(queuedCount))) {
        char message[768];
        const int intW = texture ? (int)texture->GetInternalWidth() : 0;
        const int intH = texture ? (int)texture->GetInternalHeight() : 0;
        std::snprintf(message, sizeof(message),
                      "present-android-egl #%llu stage=%s texture=%dx%d "
                      "internal=%dx%d fmt=%d dirtySerial=%llu overwrite=%d "
                      "oldDirty=%llu output=%dx%d viewport=%d,%d,%dx%d "
                      "rect=%d,%d,%dx%d upload=%d,%d,%dx%d nativeGL=%u "
                      "softwareUpload=%d "
                      "softwareUploads=%llu uv=%.4f,%.4f flipY=%d "
                      "fullFrame=%d immediateSwap=1",
                      static_cast<unsigned long long>(queuedCount),
                      stage ? stage : "", surfaceWidth, surfaceHeight, intW,
                      intH, (int)format,
                      static_cast<unsigned long long>(dirtySerial),
                      dirtyOverwritten ? 1 : 0,
                      static_cast<unsigned long long>(previousDirtySerial),
                      outputWidth, outputHeight, viewport.x, viewport.y,
                      viewport.w, viewport.h, rect.x, rect.y, rect.w, rect.h,
                      softwareUploadRect.x, softwareUploadRect.y,
                      softwareUploadRect.w, softwareUploadRect.h, nativeTexture,
                      copiedSoftware ? 1 : 0,
                      static_cast<unsigned long long>(softwareUploadCount),
                      uvScaleU, uvScaleV, flipY ? 1 : 0,
                      fullFramePresent ? 1 : 0);
        LogSDLAndroidEGLPresenter(message);

        // Detailed color sample. Prefer software upload CPU bits.
        // Native-GL readback is expensive (glReadPixels); only do it for
        // early frames / every 256th present to catch color-swap bugs.
        const void *bits = nullptr;
        int pitch = 0;
        if(copiedSoftware && softwareUploadPixels) {
            bits = softwareUploadPixels;
            pitch = softwareUploadPitch > 0 ? softwareUploadPitch
                                            : surfaceWidth * 4;
        } else if(texture &&
                  (queuedCount <= 16 || (queuedCount & 0xFF) == 0)) {
            bits = texture->GetScanLineForRead(0);
            pitch = texture->GetPitch();
        } else if(texture) {
            pitch = texture->GetPitch();
        }
        kr2diag::LogPresent(stage, nativeTexture, (int)format, surfaceWidth,
                            surfaceHeight, intW, intH, uvScaleU, uvScaleV,
                            flipY ? 1 : 0, copiedSoftware ? 1 : 0,
                            fullFramePresent ? 1 : 0, bits, pitch);
        kr2diag::LogTexture(
            "present-src", nativeTexture, (int)format, surfaceWidth,
            surfaceHeight, intW, intH, pitch,
            texture && texture->IsOpaque() ? 1 : 0,
            texture && texture->IsStatic() ? 1 : 0, bits);
    }
    return presented;
}

bool SwapAndroidEGLSurfacePresenterIfDirty(const char *stage) {
    if(!IsAndroidEGLSurfacePresenterEnabled())
        return false;

    uint64_t presentedCount = 0;
    int presentedWidth = 0;
    int presentedHeight = 0;
    bool pendingNativeGL = false;
    iTVPTexture2D *dirtyTexture = nullptr;
    uint64_t dirtySerial = 0;
    uint64_t dirtyMarks = 0;
    uint64_t dirtyOverwrites = 0;
    uint64_t swapAttempt = 0;
    uint64_t nativePresents = 0;
    {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        auto &state = gSDLAndroidEGLPresenterState;
        if(!state.frameDirty) {
            const uint64_t cleanChecks = ++state.cleanSwapChecks;
            if(IsTruthyEnv("KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS") &&
               ShouldLogScreenPresenter(cleanChecks)) {
                char message[256];
                std::snprintf(message, sizeof(message),
                              "swap-skip-clean #%llu stage=%s presented=%llu "
                              "dirtyMarks=%llu dirtyOverwrites=%llu "
                              "surfaceReady=%d",
                              static_cast<unsigned long long>(cleanChecks),
                              stage ? stage : "",
                              static_cast<unsigned long long>(state.presented),
                              static_cast<unsigned long long>(state.dirtyMarks),
                              static_cast<unsigned long long>(
                                  state.dirtyOverwrites),
                              state.surface != EGL_NO_SURFACE ? 1 : 0);
                LogSDLAndroidEGLPresenter(message);
            }
            return false;
        }

        if(state.display == EGL_NO_DISPLAY || state.context == EGL_NO_CONTEXT ||
           state.surface == EGL_NO_SURFACE) {
            const uint64_t dirtyDrop = ++state.missingSurfaceDirtyDrops;
            dirtySerial = state.pendingDirtySerial;
            state.frameDirty = false;
            state.pendingDirtySerial = 0;
            state.pendingNativeGL = false;
            state.pendingWidth = 0;
            state.pendingHeight = 0;
            state.pendingDirtyTexture = nullptr;
            LogAndroidEGLFailureLocked(stage, "dirty frame without EGL surface");
            if(ShouldLogScreenPresenter(dirtyDrop)) {
                char message[256];
                std::snprintf(message, sizeof(message),
                              "swap-drop-no-surface #%llu stage=%s "
                              "dirtySerial=%llu",
                              static_cast<unsigned long long>(dirtyDrop),
                              stage ? stage : "",
                              static_cast<unsigned long long>(dirtySerial));
                LogSDLAndroidEGLPresenter(message);
            }
            return false;
        }
        swapAttempt = ++state.swapAttempts;
        dirtySerial = state.pendingDirtySerial;
        dirtyMarks = state.dirtyMarks;
        dirtyOverwrites = state.dirtyOverwrites;

        const EGLDisplay previousDisplay = eglGetCurrentDisplay();
        const EGLContext previousContext = eglGetCurrentContext();
        const EGLSurface previousDraw = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface previousRead = eglGetCurrentSurface(EGL_READ);

        if(!eglMakeCurrent(state.display, state.surface, state.surface,
                           state.context)) {
            state.frameDirty = false;
            state.pendingDirtySerial = 0;
            state.pendingNativeGL = false;
            state.pendingWidth = 0;
            state.pendingHeight = 0;
            state.pendingDirtyTexture = nullptr;
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglMakeCurrent(swap)",
                                             eglGetError()));
            DestroyAndroidEGLWindowSurfaceLocked("swap-make-current-failed");
            return false;
        }

        const EGLBoolean swapped = eglSwapBuffers(state.display, state.surface);
        const EGLint swapError =
            swapped == EGL_TRUE ? EGL_SUCCESS : eglGetError();

        bool restored = true;
        if(previousDisplay != EGL_NO_DISPLAY &&
           previousContext != EGL_NO_CONTEXT) {
            restored = RestoreAndroidEGLCurrentLocked(
                previousDisplay, previousDraw, previousRead, previousContext,
                nullptr, stage);
        }

        state.frameDirty = false;
        if(swapped != EGL_TRUE) {
            state.pendingDirtySerial = 0;
            state.pendingNativeGL = false;
            state.pendingWidth = 0;
            state.pendingHeight = 0;
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglSwapBuffers", swapError));
            DestroyAndroidEGLWindowSurfaceLocked("swap-failed");
            return false;
        }

        presentedWidth = state.pendingWidth;
        presentedHeight = state.pendingHeight;
        pendingNativeGL = state.pendingNativeGL;
        dirtyTexture = state.pendingDirtyTexture;
        state.pendingNativeGL = false;
        state.pendingDirtySerial = 0;
        state.pendingWidth = 0;
        state.pendingHeight = 0;
        state.pendingDirtyTexture = nullptr;
        presentedCount = ++state.presented;
        state.lastPresentNativeGL = pendingNativeGL;
        if(pendingNativeGL)
            ++state.nativePresents;
        nativePresents = state.nativePresents;
        state.surfaceHasContent = true;
        if(!restored)
            LogAndroidEGLFailureLocked(
                stage, "external swap posted but EGL restore failed");
    }

    TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(presentedWidth,
                                                              presentedHeight);
    if(dirtyTexture) {
        tTVPRect consumed;
        dirtyTexture->ConsumeDirtyRect(consumed);
    }
    MarkExternalPresenterPostedFrame();
    if(ShouldLogScreenPresenter(presentedCount)) {
        char message[384];
        std::snprintf(message, sizeof(message),
                      "swap-android-egl #%llu stage=%s surface=%dx%d "
                      "dirtySerial=%llu swapAttempt=%llu dirtyMarks=%llu "
                      "dirtyOverwrites=%llu nativeGL=%d nativePresents=%llu",
                      static_cast<unsigned long long>(presentedCount),
                      stage ? stage : "", presentedWidth, presentedHeight,
                      static_cast<unsigned long long>(dirtySerial),
                      static_cast<unsigned long long>(swapAttempt),
                      static_cast<unsigned long long>(dirtyMarks),
                      static_cast<unsigned long long>(dirtyOverwrites),
                      pendingNativeGL ? 1 : 0,
                      static_cast<unsigned long long>(nativePresents));
        LogSDLAndroidEGLPresenter(message);
    }
    return true;
}

const char *PresentPathLogName(TVPSDLPresentPath path) {
    switch(path) {
        case TVPSDLPresentPath::AndroidEGL:
            return "egl";
        case TVPSDLPresentPath::AndroidFlutterDirect:
            return "direct";
        case TVPSDLPresentPath::SDLWindowSurface:
            return "sdl";
        case TVPSDLPresentPath::None:
        default:
            return "none";
    }
}

bool TryPresentAndroidTexturePlan(iTVPTexture2D *texture,
                                  TVPTextureFormat::e format,
                                  const char *stage,
                                  const TVPSDLTexturePresentPlan &plan,
                                  TVPSDLTexturePresentResult &result) {
    if(!texture || plan.textureWidth <= 0 || plan.textureHeight <= 0 ||
       plan.dirtyRect.IsEmpty())
        return false;

    const SDL_Rect dirtyRect = ToSDLRect(plan.dirtyRect);
    const int outputWidth = kTVPSDLFixedGameSurfaceWidth;
    const int outputHeight = kTVPSDLFixedGameSurfaceHeight;
    bool eglFullFrame = true;
    if(TryPresentAndroidEGLSurfaceTexture(texture, format, plan.textureWidth,
                                          plan.textureHeight, outputWidth,
                                          outputHeight, dirtyRect, stage,
                                          &eglFullFrame)) {
        result.path = TVPSDLPresentPath::AndroidEGL;
        result.sourceRect = eglFullFrame
            ? FullPresentRect(plan.textureWidth, plan.textureHeight)
            : plan.dirtyRect;
        const SDL_Rect viewport = ComputeAndroidAspectViewport(
            plan.textureWidth, plan.textureHeight, outputWidth, outputHeight);
        result.destRect = ToPresentRect(viewport);
        result.fullFrame = eglFullFrame;
        result.nativeGL = texture->GetNativeGLTextureId() != 0;
        result.cpuCopyFree = result.nativeGL;
        // Immediate eglSwapBuffers already posted the SurfaceTexture producer
        // buffer while the external surface was current.
        result.deferredSwap = false;
        return true;
    }

    if(plan.allowFallback && TVPSDLAndroidFlutterPresenterTryPresentTexture(
           texture, format, plan.textureWidth, plan.textureHeight,
           ToSDLRect(plan.fallbackRect), stage)) {
        result.path = TVPSDLPresentPath::AndroidFlutterDirect;
        result.sourceRect = plan.fallbackRect;
        const SDL_Rect viewport = ComputeAndroidAspectViewport(
            plan.textureWidth, plan.textureHeight, outputWidth, outputHeight);
        result.destRect = ToPresentRect(viewport);
        result.fullFrame = result.sourceRect.IsFullFrame(plan.textureWidth,
                                                         plan.textureHeight);
        result.nativeGL = texture->GetNativeGLTextureId() != 0;
        result.cpuCopyFree = false;
        return true;
    }

    return false;
}
#endif


} // namespace

extern "C" bool TVPSDLAndroidConsumeExternalPresenterPostedFrame() {
    return gExternalPresenterPostedFrame.exchange(false,
                                                  std::memory_order_acq_rel);
}

bool TVPSDLAndroidFlutterPresenterSwapIfDirty(const char *stage) {
#if defined(__ANDROID__)
    return SwapAndroidEGLSurfacePresenterIfDirty(stage ? stage : "sdl-drain");
#else
    (void)stage;
    return false;
#endif
}

extern "C" bool TVPSDLAndroidSwapExternalPresenterIfDirty() {
    return TVPSDLAndroidFlutterPresenterSwapIfDirty("TVPForceSwapBuffer");
}

const char *TVPSDLAndroidFlutterPresenterPresentPathLogName(
    TVPSDLPresentPath path) {
#if defined(__ANDROID__)
    return PresentPathLogName(path);
#else
    switch(path) {
        case TVPSDLPresentPath::AndroidEGL:
            return "egl";
        case TVPSDLPresentPath::AndroidFlutterDirect:
            return "direct";
        case TVPSDLPresentPath::SDLWindowSurface:
            return "sdl";
        case TVPSDLPresentPath::None:
        default:
            return "none";
    }
#endif
}

bool TVPSDLAndroidFlutterPresenterTryPresentTexturePlan(
    iTVPTexture2D *texture, TVPTextureFormat::e format, const char *stage,
    const TVPSDLTexturePresentPlan &plan,
    TVPSDLTexturePresentResult &result) {
#if defined(__ANDROID__)
    return TryPresentAndroidTexturePlan(texture, format, stage, plan, result);
#else
    (void)texture;
    (void)format;
    (void)stage;
    (void)plan;
    result = TVPSDLTexturePresentResult{};
    return false;
#endif
}

void TVPSDLAndroidFlutterPresenterAppendEGLOverlayInfo(
    std::ostream &rendererInfo) {
#if defined(__ANDROID__)
    if(!IsAndroidEGLSurfacePresenterEnabled())
        return;

    auto clipOverlayString = [](std::string value, size_t limit) {
        for(char &ch : value) {
            if(ch == '\r' || ch == '\n' || ch == '\t')
                ch = ' ';
        }
        if(value.size() > limit) {
            value.resize(limit);
            value += "...";
        }
        return value;
    };

    std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
    const auto &state = gSDLAndroidEGLPresenterState;
    rendererInfo << " androidEgl=";
    if(state.fatal) {
        rendererInfo << "fatal";
        if(!state.fatalReason.empty()) {
            rendererInfo << " reason="
                         << clipOverlayString(state.fatalReason, 72);
        }
        return;
    }
    if(state.autoDisabled) {
        rendererInfo << "autoDisabled";
        if(!state.autoDisabledReason.empty()) {
            rendererInfo << " reason="
                         << clipOverlayString(state.autoDisabledReason, 72);
        }
        return;
    }
    if(state.presented > 0)
        rendererInfo << "presented=" << state.presented;
    else if(state.attempts > 0)
        rendererInfo << "attempts=" << state.attempts;
    else
        rendererInfo << "pending";
    if(state.softwareUploads > 0)
        rendererInfo << " uploads=" << state.softwareUploads;
    if(state.nativePresents > 0)
        rendererInfo << " native=" << state.nativePresents;
    if(state.failures > 0)
        rendererInfo << " failures=" << state.failures;
    if(state.unavailable > 0 && state.presented == 0)
        rendererInfo << " unavailable=" << state.unavailable;
    if(state.contextResets > 0)
        rendererInfo << " contextResets=" << state.contextResets;
#else
    (void)rendererInfo;
#endif
}

bool TVPSDLAndroidFlutterPresenterIsEGLHighPerformanceActive() {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
    const auto &eglState = gSDLAndroidEGLPresenterState;
    return eglState.presented > 0 && !eglState.autoDisabled &&
        !eglState.fatal;
#else
    return false;
#endif
}

bool TVPSDLAndroidFlutterPresenterIsEGLCpuCopyFreeActive() {
#if defined(__ANDROID__)
    std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
    const auto &eglState = gSDLAndroidEGLPresenterState;
    return eglState.presented > 0 && eglState.lastPresentNativeGL &&
        !eglState.autoDisabled && !eglState.fatal;
#else
    return false;
#endif
}

void TVPSDLAndroidFlutterPresenterNotifySurfaceChanged(const char *reason) {
    TVPSDLAndroidFlutterPresenterMarkSurfaceChanged();
    ClearRememberedPresentedSurface();
#if defined(__ANDROID__)
    if(!IsAndroidEGLSurfacePresenterEnabled())
        return;
    std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
    if(gSDLAndroidEGLPresenterState.autoDisabled) {
        gSDLAndroidEGLPresenterState.autoDisabled = false;
        gSDLAndroidEGLPresenterState.autoDisabledReason.clear();
        gSDLAndroidEGLPresenterState.failures = 0;
    }
    DestroyAndroidEGLWindowSurfaceLocked(reason ? reason : "surface-changed");
#else
    (void)reason;
#endif
}


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
#if defined(__ANDROID__)
    width = kTVPSDLFixedGameSurfaceWidth;
    height = kTVPSDLFixedGameSurfaceHeight;
#endif

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
    return false;
}

extern "C" bool TVPSDLAndroidIsExternalPresenterActive() {
#if defined(__ANDROID__)
    {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        const auto &eglState = gSDLAndroidEGLPresenterState;
        if(eglState.frameDirty ||
           (eglState.presented > 0 && eglState.surface != EGL_NO_SURFACE &&
            eglState.width > 0 && eglState.height > 0 &&
            eglState.surfaceHasContent))
            return true;
    }
#endif
    return TVPSDLAndroidFlutterPresenterHasPresentedSurface() ||
        gExternalPresenterPostedFrame.load(std::memory_order_acquire);
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

    if(!TVPAndroidAcquireFlutterGameSurfaceWindow ||
       !TVPAndroidReleaseFlutterGameSurfaceWindow ||
       !TVPAndroidGetFlutterGameSurfaceSize)
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
    const int outputWidth = kTVPSDLFixedGameSurfaceWidth;
    const int outputHeight = kTVPSDLFixedGameSurfaceHeight;

    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if(windowWidth != outputWidth || windowHeight != outputHeight) {
        const int geometryResult =
            ANativeWindow_setBuffersGeometry(window, outputWidth,
                                             outputHeight,
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
                              flutterWidth, flutterHeight, outputWidth,
                              outputHeight, geometryResult);
                LogSDLScreenPresenter(message);
            }
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
    }

    SDL_Rect presentRect = rect;
    if(!TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled())
        presentRect = FullAndroidSurfaceRect(outputWidth, outputHeight);

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

    const bool copied = CopyTextureToAndroidBufferViewport(
        texture, surfaceWidth, surfaceHeight, outputWidth, outputHeight,
        buffer);
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

    TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(outputWidth,
                                                              outputHeight);
    MarkExternalPresenterPostedFrame();
    const uint64_t presented = ++gFlutterSurfacePresented;
    if(ShouldLogScreenPresenter(presented)) {
        const int glBacked = texture->GetNativeGLTextureId() != 0 ? 1 : 0;
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-direct #%llu stage=%s surface=%dx%d "
                      "output=%dx%d flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d glBacked=%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      outputWidth, outputHeight, flutterWidth, flutterHeight,
                      buffer.width, buffer.height, buffer.stride, buffer.format,
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

    if(!TVPAndroidAcquireFlutterGameSurfaceWindow ||
       !TVPAndroidReleaseFlutterGameSurfaceWindow ||
       !TVPAndroidGetFlutterGameSurfaceSize)
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
    const int outputWidth = kTVPSDLFixedGameSurfaceWidth;
    const int outputHeight = kTVPSDLFixedGameSurfaceHeight;

    const int windowWidth = ANativeWindow_getWidth(window);
    const int windowHeight = ANativeWindow_getHeight(window);
    if(windowWidth != outputWidth || windowHeight != outputHeight) {
        const int geometryResult =
            ANativeWindow_setBuffersGeometry(window, outputWidth,
                                             outputHeight,
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
                              flutterWidth, flutterHeight, outputWidth,
                              outputHeight, geometryResult);
                LogSDLScreenPresenter(message);
            }
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
    }

    SDL_Rect presentRect = rect;
    if(!TVPSDLAndroidFlutterPresenterIsDirectPartialPresentEnabled())
        presentRect = FullAndroidSurfaceRect(outputWidth, outputHeight);

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

    CopySurfaceToAndroidBufferViewport(surface, surfaceWidth, surfaceHeight,
                                       pitch, outputWidth, outputHeight,
                                       buffer);
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
    TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(outputWidth,
                                                              outputHeight);
    MarkExternalPresenterPostedFrame();
    if(ShouldLogScreenPresenter(presented)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-flutter-surface #%llu stage=%s surface=%dx%d "
                      "output=%dx%d flutter=%dx%d buffer=%dx%d stride=%d format=%d "
                      "rect=%d,%d,%dx%d",
                      static_cast<unsigned long long>(presented),
                      stage ? stage : "", surfaceWidth, surfaceHeight,
                      outputWidth, outputHeight, flutterWidth, flutterHeight,
                      buffer.width, buffer.height, buffer.stride, buffer.format,
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
