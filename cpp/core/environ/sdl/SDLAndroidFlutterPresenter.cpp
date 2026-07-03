#include "SDLAndroidFlutterPresenter.h"

#include "NativeLog.h"

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

#if defined(__ANDROID__) && !defined(EGL_SWAP_BEHAVIOR_PRESERVED_BIT)
#define EGL_SWAP_BEHAVIOR_PRESERVED_BIT 0x0400
#endif
#if defined(__ANDROID__) && !defined(EGL_SWAP_BEHAVIOR)
#define EGL_SWAP_BEHAVIOR 0x3093
#endif
#if defined(__ANDROID__) && !defined(EGL_BUFFER_PRESERVED)
#define EGL_BUFFER_PRESERVED 0x3094
#endif
#if defined(__ANDROID__) && !defined(EGL_BUFFER_DESTROYED)
#define EGL_BUFFER_DESTROYED 0x3095
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
    GLint uvScaleUniform = -1;
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
    bool fatal = false;
    bool autoDisabled = false;
    bool lastPresentNativeGL = false;
    bool preserveSwapBehavior = false;
    bool surfaceHasContent = false;
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
    GLint activeTexture = GL_TEXTURE0;
    GLint texture0Binding = 0;
    GLint arrayBuffer = 0;
    GLint program = 0;
    GLint unpackAlignment = 4;
#if defined(GL_UNPACK_ROW_LENGTH)
    GLint unpackRowLength = 0;
#endif
    GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLboolean blend = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
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
            value ? IsTruthyEnv("KRKR2_ANDROID_EGL_SURFACE_FLIP_Y") : true;
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
    glGetIntegerv(GL_ACTIVE_TEXTURE, &snapshot.activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &snapshot.texture0Binding);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &snapshot.arrayBuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &snapshot.program);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &snapshot.unpackAlignment);
#if defined(GL_UNPACK_ROW_LENGTH)
    if(IsAndroidGLUnpackRowLengthSupported())
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &snapshot.unpackRowLength);
#endif
    glGetFloatv(GL_COLOR_CLEAR_VALUE, snapshot.clearColor);
    snapshot.blend = glIsEnabled(GL_BLEND);
    snapshot.depthTest = glIsEnabled(GL_DEPTH_TEST);
    snapshot.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
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
    glUseProgram(static_cast<GLuint>(snapshot.program));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,
                  static_cast<GLuint>(snapshot.texture0Binding));
    glActiveTexture(static_cast<GLenum>(snapshot.activeTexture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, snapshot.unpackAlignment);
#if defined(GL_UNPACK_ROW_LENGTH)
    if(IsAndroidGLUnpackRowLengthSupported())
        glPixelStorei(GL_UNPACK_ROW_LENGTH, snapshot.unpackRowLength);
#endif
    glClearColor(snapshot.clearColor[0], snapshot.clearColor[1],
                 snapshot.clearColor[2], snapshot.clearColor[3]);
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
    glBindFramebuffer(GL_FRAMEBUFFER,
                      static_cast<GLuint>(snapshot.framebuffer));
    glViewport(snapshot.viewport[0], snapshot.viewport[1], snapshot.viewport[2],
               snapshot.viewport[3]);
}

bool RestoreAndroidEGLCurrentAndGLStateLocked(
    EGLDisplay display, EGLSurface drawSurface, EGLSurface readSurface,
    EGLContext context, const TVPAndroidGLStateSnapshot &glState,
    const char *stage) {
    if(eglMakeCurrent(display, drawSurface, readSurface, context) == EGL_TRUE) {
        RestoreAndroidGLState(glState);
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
        "uniform vec2 uUvScale;\n"
        "uniform float uFlipY;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "  float v = uFlipY > 0.5 ? (1.0 - aTexCoord.y) : aTexCoord.y;\n"
        "  vTexCoord = vec2(aTexCoord.x * uUvScale.x, v * uUvScale.y);\n"
        "}\n";
    static const char *kFragmentShader =
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "uniform sampler2D uTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTexture, vTexCoord);\n"
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
    state.uvScaleUniform = glGetUniformLocation(program, "uUvScale");
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
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
        -1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  1.0f,  1.0f, 1.0f,
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

bool AndroidEGLConfigSupportsPreservedSwap(EGLDisplay display,
                                           EGLConfig config) {
    if(display == EGL_NO_DISPLAY || !config)
        return false;
    EGLint surfaceType = 0;
    return eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE,
                              &surfaceType) == EGL_TRUE &&
        (surfaceType & EGL_SWAP_BEHAVIOR_PRESERVED_BIT) != 0;
}

bool IsFullAndroidSurfaceRect(const SDL_Rect &rect, int width, int height) {
    return rect.x <= 0 && rect.y <= 0 && rect.w >= width && rect.h >= height;
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
    state.preserveSwapBehavior = false;
    state.surfaceHasContent = false;
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
    state.uvScaleUniform = -1;
    state.flipYUniform = -1;
    state.vertexBuffer = 0;
    state.uploadTexture = 0;
    state.uploadWidth = 0;
    state.uploadHeight = 0;
    state.uploadSourceTexture = nullptr;
    state.uploadScratch.clear();
}

bool EnsureAndroidEGLUploadTextureLocked(int width, int height,
                                         const SDL_Rect &rect,
                                         const uint8_t *pixels,
                                         const char *stage) {
    auto &state = gSDLAndroidEGLPresenterState;
    if(!pixels || width <= 0 || height <= 0 || rect.x < 0 || rect.y < 0 ||
       rect.w <= 0 || rect.h <= 0 || rect.x + rect.w > width ||
       rect.y + rect.h > height)
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
    if(state.uploadWidth == width && state.uploadHeight == height) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.w, rect.h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels);
        state.uploadWidth = width;
        state.uploadHeight = height;
    }
    ++state.softwareUploads;
    return glGetError() == GL_NO_ERROR;
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
        state.preserveSwapBehavior = false;
        state.surfaceHasContent = false;
        if(AndroidEGLConfigSupportsPreservedSwap(display, state.config) &&
           eglSurfaceAttrib(display, state.surface, EGL_SWAP_BEHAVIOR,
                            EGL_BUFFER_PRESERVED) == EGL_TRUE) {
            EGLint swapBehavior = EGL_BUFFER_DESTROYED;
            if(eglQuerySurface(display, state.surface, EGL_SWAP_BEHAVIOR,
                               &swapBehavior) == EGL_TRUE) {
                state.preserveSwapBehavior =
                    swapBehavior == EGL_BUFFER_PRESERVED;
            }
        }
        char message[256];
        std::snprintf(message, sizeof(message),
                      "surface ready #%llu stage=%s window=%p size=%dx%d "
                      "preserve=%d",
                      static_cast<unsigned long long>(state.recreates),
                      stage ? stage : "", static_cast<void *>(window),
                      width, height, state.preserveSwapBehavior ? 1 : 0);
        LogSDLAndroidEGLPresenter(message);
    }

    if(!EnsureAndroidEGLProgramLocked(stage))
        return false;

    return true;
}

bool TryPresentAndroidEGLSurfaceTexture(iTVPTexture2D *texture,
                                        TVPTextureFormat::e format,
                                        int surfaceWidth, int surfaceHeight,
                                        const SDL_Rect &rect,
                                        const char *stage,
                                        bool *usedFullFrame) {
    if(!IsAndroidEGLSurfacePresenterEnabled())
        return false;
    if(!texture || surfaceWidth <= 0 || surfaceHeight <= 0 ||
       format != TVPTextureFormat::RGBA || rect.w <= 0 || rect.h <= 0)
        return false;

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
    uint64_t presentedCount = 0;
    uint64_t softwareUploadCount = 0;
    float uvScaleU = 1.0f;
    float uvScaleV = 1.0f;
    bool flipY = false;
    {
        std::lock_guard<std::mutex> lock(gSDLAndroidEGLPresenterMutex);
        auto &state = gSDLAndroidEGLPresenterState;
        ++state.attempts;
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
            const bool needsFullUpload =
                state.uploadSourceTexture != texture ||
                state.uploadWidth != surfaceWidth ||
                state.uploadHeight != surfaceHeight;
            if(needsFullUpload)
                softwareUploadRect =
                    FullAndroidSurfaceRect(surfaceWidth, surfaceHeight);
            if(!CopyTextureRegionToAndroidEGLScratch(
                   texture, format, surfaceWidth, surfaceHeight,
                   softwareUploadRect, state.uploadScratch)) {
                LogAndroidEGLFailureLocked(stage,
                                           "software texture copy failed");
                TVPAndroidReleaseFlutterGameSurfaceWindow(window);
                return false;
            }
        }

        const EGLDisplay previousDisplay = eglGetCurrentDisplay();
        const EGLContext previousContext = eglGetCurrentContext();
        const EGLSurface previousDraw = eglGetCurrentSurface(EGL_DRAW);
        const EGLSurface previousRead = eglGetCurrentSurface(EGL_READ);

        if(!EnsureAndroidEGLSurfacePresenterLocked(window, surfaceWidth,
                                                   surfaceHeight, stage)) {
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }

        TVPAndroidGLStateSnapshot glState =
            SaveAndroidGLState(state.positionAttrib, state.texCoordAttrib);

        if(!eglMakeCurrent(state.display, state.surface, state.surface,
                           state.context)) {
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglMakeCurrent", eglGetError()));
            RestoreAndroidGLState(glState);
            TVPAndroidReleaseFlutterGameSurfaceWindow(window);
            return false;
        }
        eglSwapInterval(state.display, 0);

        GLuint sourceTexture = nativeTexture;
        if(softwareUpload) {
            if(!EnsureAndroidEGLUploadTextureLocked(
                   surfaceWidth, surfaceHeight, softwareUploadRect,
                   state.uploadScratch.data(), stage)) {
                LogAndroidEGLFailureLocked(
                    stage, "software texture upload failed");
                RestoreAndroidEGLCurrentAndGLStateLocked(
                    previousDisplay, previousDraw, previousRead,
                    previousContext, glState, stage);
                TVPAndroidReleaseFlutterGameSurfaceWindow(window);
                return false;
            }
            sourceTexture = state.uploadTexture;
            state.uploadSourceTexture = texture;
            copiedSoftware = true;
            softwareUploadCount = state.softwareUploads;
        }

        const bool canPartialPresent =
            state.preserveSwapBehavior && state.surfaceHasContent &&
            !IsFullAndroidSurfaceRect(rect, surfaceWidth, surfaceHeight);
        fullFramePresent = !canPartialPresent;
        SDL_Rect presentRect = fullFramePresent
            ? FullAndroidSurfaceRect(surfaceWidth, surfaceHeight)
            : rect;

        glViewport(0, 0, surfaceWidth, surfaceHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        if(fullFramePresent) {
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glEnable(GL_SCISSOR_TEST);
            glScissor(presentRect.x,
                      surfaceHeight - presentRect.y - presentRect.h,
                      presentRect.w, presentRect.h);
        }
        glUseProgram(state.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sourceTexture);
        if(copiedSoftware) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glUniform1i(state.textureUniform, 0);
        glUniform2f(state.uvScaleUniform, uvScaleU, uvScaleV);
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
        glFlush();

        const GLenum glError = glGetError();
        const EGLBoolean swapped = glError == GL_NO_ERROR
            ? eglSwapBuffers(state.display, state.surface)
            : EGL_FALSE;
        const EGLint swapError =
            swapped == EGL_TRUE ? EGL_SUCCESS : eglGetError();

        const bool restored = RestoreAndroidEGLCurrentAndGLStateLocked(
            previousDisplay, previousDraw, previousRead, previousContext,
            glState, stage);
        TVPAndroidReleaseFlutterGameSurfaceWindow(window);

        if(!restored)
            return swapped == EGL_TRUE && glError == GL_NO_ERROR;
        if(glError != GL_NO_ERROR) {
            char reason[160];
            std::snprintf(reason, sizeof(reason), "gl draw failed: 0x%x",
                          glError);
            LogAndroidEGLFailureLocked(stage, reason);
            return false;
        }
        if(swapped != EGL_TRUE) {
            LogAndroidEGLFailureLocked(
                stage, AndroidEGLFormatError("eglSwapBuffers", swapError));
            DestroyAndroidEGLWindowSurfaceLocked("swap-failed");
            return false;
        }

        TVPSDLAndroidFlutterPresenterRememberPresentedSurfaceSize(
            surfaceWidth, surfaceHeight);
        presentedCount = ++state.presented;
        state.lastPresentNativeGL = nativeTexture != 0;
        if(nativeTexture != 0)
            ++state.nativePresents;
        state.surfaceHasContent = true;
        presented = true;
    }

    if(usedFullFrame)
        *usedFullFrame = fullFramePresent;

    if(presented && IsTruthyEnv("KRKR2_ENABLE_SDL_RENDER_DIAGNOSTICS") &&
       ShouldLogScreenPresenter(presentedCount)) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "present-android-egl #%llu stage=%s surface=%dx%d "
                      "rect=%d,%d,%dx%d upload=%d,%d,%dx%d nativeGL=%u "
                      "softwareUpload=%d "
                      "softwareUploads=%llu uv=%.4f,%.4f flipY=%d "
                      "fullFrame=%d",
                      static_cast<unsigned long long>(presentedCount),
                      stage ? stage : "", surfaceWidth, surfaceHeight, rect.x,
                      rect.y, rect.w, rect.h, softwareUploadRect.x,
                      softwareUploadRect.y, softwareUploadRect.w,
                      softwareUploadRect.h, nativeTexture,
                      copiedSoftware ? 1 : 0,
                      static_cast<unsigned long long>(softwareUploadCount),
                      uvScaleU, uvScaleV, flipY ? 1 : 0,
                      fullFramePresent ? 1 : 0);
        LogSDLAndroidEGLPresenter(message);
    }
    return presented;
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
    bool eglFullFrame = true;
    if(TryPresentAndroidEGLSurfaceTexture(texture, format, plan.textureWidth,
                                          plan.textureHeight, dirtyRect, stage,
                                          &eglFullFrame)) {
        result.path = TVPSDLPresentPath::AndroidEGL;
        result.sourceRect = eglFullFrame
            ? FullPresentRect(plan.textureWidth, plan.textureHeight)
            : plan.dirtyRect;
        result.fullFrame = eglFullFrame;
        result.nativeGL = texture->GetNativeGLTextureId() != 0;
        result.cpuCopyFree = result.nativeGL;
        return true;
    }

    if(TVPSDLAndroidFlutterPresenterTryPresentTexture(
           texture, format, plan.textureWidth, plan.textureHeight,
           ToSDLRect(plan.fallbackRect), stage)) {
        result.path = TVPSDLPresentPath::AndroidFlutterDirect;
        result.sourceRect = plan.fallbackRect;
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
