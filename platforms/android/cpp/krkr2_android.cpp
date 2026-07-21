/* Include the SDL main definition header */
#include <jni.h>
#include <dlfcn.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "environ/ConfigManager/GlobalConfigManager.h"
#include "environ/Application.h"
#include "environ/NativeLog.h"
#include "environ/android/KrkrJniHelper.h"
#include "environ/android/AndroidUtils.h"
#include "environ/sdl/SDLGameManager.h"
#include "environ/sdl/SDLAndroidFlutterPresenter.h"
#include "environ/sdl/SDLPresentTypes.h"
#include "environ/sdl/SDLRuntimePresenter.h"
#include "environ/runtime/RuntimeEngineLoop.h"
#include "environ/runtime/RuntimeHost.h"
#include "environ/runtime/RuntimePresenter.h"
#include "common/FFmpegDecodeConfig.h"
#include "vkdefine.h"

/*******************************************************************************
                 Functions called by JNI
*******************************************************************************/
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <client/linux/handler/exception_handler.h>
#include <client/linux/handler/minidump_descriptor.h>

namespace TJS {
#if defined(ANDROID)
void TVPInstallKrkrHook();
bool TVPIsKrkrHookInstalled();
#endif
}

// std::string Android_GetDumpStoragePath();

namespace {
JavaVM *gAndroidJavaVM = nullptr;
jobject gAndroidApplicationContext = nullptr;
std::mutex gAndroidApplicationContextLock;

bool TVPAndroidEnsureSDLRenderContextCurrent(const char *stage);
}

jobject krkr_GetApplicationContext() {
    std::lock_guard<std::mutex> lock(gAndroidApplicationContextLock);
    return gAndroidApplicationContext;
}

static void krkr_SetApplicationContext(JNIEnv *env, jobject context) {
    if(!env)
        return;

    std::lock_guard<std::mutex> lock(gAndroidApplicationContextLock);
    if(gAndroidApplicationContext) {
        env->DeleteGlobalRef(gAndroidApplicationContext);
        gAndroidApplicationContext = nullptr;
    }
    if(context)
        gAndroidApplicationContext = env->NewGlobalRef(context);
}

static bool DumpCallback(const google_breakpad::MinidumpDescriptor &descriptor,
                         void *context, bool succeeded) {
    return succeeded;
}

extern bool TVPSystemUninitCalled;
void TVPSetUseFFmpegImageDecoder(bool enabled);

static bool DumpFilter(void *data) {
    // if trying exit system, ignore all exception
    return !TVPSystemUninitCalled;
}

class TVPAndroidSDLRuntimeHost final : public iTVPRuntimeHost {
public:
    const char *GetHostName() const override { return "android-sdl3"; }

    bool StartGame(const TVPRuntimeHostLaunchRequest &request) override {
        if(!TVPRuntimeConfigureGameLaunch(request))
            return false;
        if(!TVPAndroidEnsureSDLRenderContextCurrent(
               "android-sdl3-start-game")) {
            TVPNativeLogInfo("runtime-host",
                             "start skipped: EGL context not current");
            return false;
        }
        const bool started = TVPRuntimeStartApplication(request.gamePath);
        if(started) {
            TVPRuntimeSetScreenTakeoverEnabled(
                { true, "android-sdl3-start-game", kTVPSDLFixedGameSurfaceWidth,
                  kTVPSDLFixedGameSurfaceHeight, kTVPSDLFixedGameSurfaceWidth,
                  kTVPSDLFixedGameSurfaceHeight });
            ResetFrameClock();
        } else {
            TVPRuntimeSetScreenTakeoverEnabled(
                { false, "android-sdl3-start-game-failed", 0, 0, 0, 0 });
        }
        return started;
    }

    void RunFrame(float deltaSeconds) override {
        RunFrameTransaction(deltaSeconds, "android-sdl3-run-frame");
    }

    void RunFrameTick() {
        RunFrameTransaction(NextFrameDeltaSeconds(), "android-sdl3-frame-tick");
    }

    void ResetFrameClock() {
        std::lock_guard<std::mutex> lock(FrameClockMutex);
        LastFrameTime = std::chrono::steady_clock::time_point{};
    }

private:
    float NextFrameDeltaSeconds() {
        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        std::lock_guard<std::mutex> lock(FrameClockMutex);
        if(LastFrameTime == clock::time_point{}) {
            LastFrameTime = now;
            return 1.0f / 60.0f;
        }
        const std::chrono::duration<float> elapsed = now - LastFrameTime;
        LastFrameTime = now;
        return std::clamp(elapsed.count(), 0.0f, 0.25f);
    }

    void RunFrameTransaction(float deltaSeconds, const char *stage) {
        if(FrameInProgress.exchange(true, std::memory_order_acq_rel))
            return;
        struct FrameGuard {
            std::atomic_bool &Flag;
            ~FrameGuard() { Flag.store(false, std::memory_order_release); }
        } guard{ FrameInProgress };
        const char *frameStage = stage ? stage : "android-sdl3";
        if(!TVPAndroidEnsureSDLRenderContextCurrent(frameStage)) {
            TVPNativeLogInfo("runtime-host",
                             "frame skipped: EGL context not current");
            return;
        }
        TVPRuntimeRunApplicationFrame(deltaSeconds);
        TVPRuntimeRecycleFrameResources();
        TVPRuntimePumpScreenPresenter(frameStage);
    }

public:
    TVPRuntimeHostFrameMetrics GetFrameMetrics() override {
        TVPRuntimeHostFrameMetrics metrics;
        metrics.frameWidth = kTVPSDLFixedGameSurfaceWidth;
        metrics.frameHeight = kTVPSDLFixedGameSurfaceHeight;
        metrics.sceneWidth = kTVPSDLFixedGameSurfaceWidth;
        metrics.sceneHeight = kTVPSDLFixedGameSurfaceHeight;
        metrics.scale = 1.0f;
        return metrics;
    }

private:
    std::mutex FrameClockMutex;
    std::chrono::steady_clock::time_point LastFrameTime;
    std::atomic_bool FrameInProgress{ false };
};

static TVPAndroidSDLRuntimeHost gAndroidSDLRuntimeHost;
std::once_flag gAndroidSDLHostRegisterOnce;

static void TVPRegisterAndroidSDLRuntimeHost() {
    std::call_once(gAndroidSDLHostRegisterOnce, []() {
        TVPSetRuntimeHost(&gAndroidSDLRuntimeHost);
        TVPRegisterSDLRuntimePresenter();
        TVPNativeLogInfo("runtime-host", "android-sdl3 runtime host registered");
    });
}

std::once_flag gAndroidBaseInitOnce;
std::atomic_bool gAndroidBaseInitDone{false};
std::atomic_bool gAndroidSDLJniReady{false};

void TVPAndroidInitializeBase(JNIEnv *env, const char *source) {
    if(!env)
        return;

    std::call_once(gAndroidBaseInitOnce, [env, source]() {
        TVPInitializeNativeLogging();
        TVPAppendNativeFatalBreadcrumb("jni", "android native init enter");
        if(source && *source)
            TVPNativeLogInfo("jni", (std::string("native init source=") +
                                     source).c_str());

#if defined(ANDROID) && defined(KRKR2_ENABLE_TJS_DOBBY_HOOK)
        TJS::TVPInstallKrkrHook();
        try {
            spdlog::info("[hook] install requested, installed={}",
                         TJS::TVPIsKrkrHookInstalled() ? 1 : 0);
        } catch(...) {
        }
#endif

        JavaVM *vm{};
        env->GetJavaVM(&vm);
        gAndroidJavaVM = vm;
        krkr::JniHelper::setJavaVM(vm);
        void *handle = dlopen("libSDL3.so", RTLD_LAZY);
        if(handle) {
            typedef jint (*JNI_OnLoad)(JavaVM *, void *);
            void *sdl3Init = dlsym(handle, "JNI_OnLoad");
            const jint sdlVersion = sdl3Init
                ? ((JNI_OnLoad)sdl3Init)(vm, nullptr)
                : 0;
            if(!sdl3Init ||
               (sdlVersion != JNI_VERSION_1_4 &&
                sdlVersion != JNI_VERSION_1_6)) {
                spdlog::critical("invoke libSDL3.so JNI_OnLoad method failed");
                TVPAppendNativeFatalBreadcrumb(
                    "jni", "libSDL3.so JNI_OnLoad failed");
            } else {
                gAndroidSDLJniReady.store(true, std::memory_order_release);
                TVPAppendNativeFatalBreadcrumb("jni",
                                               "libSDL3.so JNI_OnLoad ok");
            }
        } else {
            spdlog::critical("load libSDL3.so failed");
            TVPAppendNativeFatalBreadcrumb("jni", "dlopen libSDL3.so failed");
        }

        gAndroidBaseInitDone.store(true, std::memory_order_release);
    });
}

void TVPAndroidInitializeSDLHost(JNIEnv *env, const char *source) {
    TVPAndroidInitializeBase(env, source);
    if(!gAndroidBaseInitDone.load(std::memory_order_acquire) ||
       !gAndroidSDLJniReady.load(std::memory_order_acquire))
        return;
    TVPRegisterAndroidSDLRuntimeHost();
}

void TVPAndroidInitializeLegacyHost(JNIEnv *env, const char *source) {
    // Legacy JNI entry kept only for KR2Activity bridge symbols. Android no
    // longer owns a Cocos host; always hand off to the SDL3 runtime host.
    TVPAndroidInitializeSDLHost(env, source);
    if(source && *source)
        TVPAppendNativeFatalBreadcrumb("jni", "legacy host -> sdl3");
}

namespace kr2android {
    extern std::condition_variable MessageBoxCond;
    extern std::mutex MessageBoxLock;
    extern int MsgBoxRet;
    extern std::string MessageBoxRetText;
} // namespace kr2android

void Android_PushEvents(const std::function<void()> &func);
using namespace kr2android;

namespace {
std::mutex gFlutterGameSurfaceLock;
ANativeWindow *gFlutterGameSurfaceWindow = nullptr;
int gFlutterGameSurfaceWidth = 0;
int gFlutterGameSurfaceHeight = 0;

struct TVPAndroidSDLRenderContextState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface pbuffer = EGL_NO_SURFACE;
    bool tried = false;
    bool ready = false;
    uint64_t makeCurrentCalls = 0;
    uint64_t failures = 0;
};

std::mutex gAndroidSDLRenderContextLock;
TVPAndroidSDLRenderContextState gAndroidSDLRenderContext;

bool ShowFlutterGameMainMenu(JNIEnv *env) {
    if(!env)
        return false;

    jclass cls = env->FindClass("org/github/krkr2/SdlRuntimeActivity");
    if(!cls) {
        env->ExceptionClear();
        return false;
    }

    jmethodID method =
        env->GetStaticMethodID(cls, "showFlutterGameMainMenu", "()Z");
    if(!method) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return false;
    }

    const bool shown = env->CallStaticBooleanMethod(cls, method) == JNI_TRUE;
    if(env->ExceptionCheck()) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        return false;
    }

    env->DeleteLocalRef(cls);
    return shown;
}

std::string FormatAndroidEGLError(const char *operation, EGLint error) {
    char message[128];
    std::snprintf(message, sizeof(message), "%s failed egl=0x%04x",
                  operation ? operation : "egl", static_cast<unsigned>(error));
    return message;
}

bool ChooseAndroidSDLRenderConfig(EGLDisplay display, EGLConfig *config) {
    if(!config)
        return false;
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint count = 0;
    if(eglChooseConfig(display, attrs, config, 1, &count) == EGL_TRUE &&
       count > 0 && *config)
        return true;

    const EGLint fallbackAttrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
        EGL_NONE,
    };
    return eglChooseConfig(display, fallbackAttrs, config, 1, &count) ==
        EGL_TRUE && count > 0 && *config;
}

void ResetAndroidSDLRenderContextLocked(bool terminateDisplay) {
    auto &state = gAndroidSDLRenderContext;
    if(state.display != EGL_NO_DISPLAY) {
        if(eglGetCurrentDisplay() == state.display &&
           eglGetCurrentContext() == state.context)
            eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
        if(state.pbuffer != EGL_NO_SURFACE)
            eglDestroySurface(state.display, state.pbuffer);
        if(state.context != EGL_NO_CONTEXT)
            eglDestroyContext(state.display, state.context);
        if(terminateDisplay)
            eglTerminate(state.display);
    }
    state.display = EGL_NO_DISPLAY;
    state.config = nullptr;
    state.context = EGL_NO_CONTEXT;
    state.pbuffer = EGL_NO_SURFACE;
    state.tried = false;
    state.ready = false;
}

bool CreateAndroidSDLRenderContextLocked(const char *stage) {
    auto &state = gAndroidSDLRenderContext;
    if(state.ready)
        return true;
    state.tried = true;

    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(state.display == EGL_NO_DISPLAY) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglGetDisplay", eglGetError())
                             .c_str());
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if(eglInitialize(state.display, &major, &minor) != EGL_TRUE) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglInitialize", eglGetError())
                             .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }
    if(eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglBindAPI", eglGetError())
                             .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }

    if(!ChooseAndroidSDLRenderConfig(state.display, &state.config)) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglChooseConfig", eglGetError())
                             .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }

    const EGLint context3Attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    state.context =
        eglCreateContext(state.display, state.config, EGL_NO_CONTEXT,
                         context3Attrs);
    int contextVersion = 3;
    if(state.context == EGL_NO_CONTEXT) {
        const EGLint context2Attrs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE,
        };
        state.context =
            eglCreateContext(state.display, state.config, EGL_NO_CONTEXT,
                             context2Attrs);
        contextVersion = 2;
    }
    if(state.context == EGL_NO_CONTEXT) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglCreateContext", eglGetError())
                             .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }

    const EGLint pbufferAttrs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE,
    };
    state.pbuffer =
        eglCreatePbufferSurface(state.display, state.config, pbufferAttrs);
    if(state.pbuffer == EGL_NO_SURFACE) {
        ++state.failures;
        TVPNativeLogInfo(
            "runtime-host",
            FormatAndroidEGLError("eglCreatePbufferSurface", eglGetError())
                .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }

    state.ready = true;
    char message[256];
    std::snprintf(message, sizeof(message),
                  "android-sdl3 EGL render context ready stage=%s "
                  "version=%d egl=%d.%d",
                  stage ? stage : "", contextVersion, major, minor);
    TVPNativeLogInfo("runtime-host", message);
    return true;
}

bool TVPAndroidEnsureSDLRenderContextCurrent(const char *stage) {
    std::lock_guard<std::mutex> lock(gAndroidSDLRenderContextLock);
    auto &state = gAndroidSDLRenderContext;
    if(!CreateAndroidSDLRenderContextLocked(stage))
        return false;

    if(eglGetCurrentDisplay() == state.display &&
       eglGetCurrentContext() == state.context &&
       eglGetCurrentSurface(EGL_DRAW) == state.pbuffer &&
       eglGetCurrentSurface(EGL_READ) == state.pbuffer)
        return true;

    if(eglMakeCurrent(state.display, state.pbuffer, state.pbuffer,
                      state.context) != EGL_TRUE) {
        ++state.failures;
        TVPNativeLogInfo("runtime-host",
                         FormatAndroidEGLError("eglMakeCurrent", eglGetError())
                             .c_str());
        ResetAndroidSDLRenderContextLocked(false);
        return false;
    }
    const uint64_t calls = ++state.makeCurrentCalls;
    if(calls <= 4 || calls == 8 || calls == 16 || (calls % 256) == 0) {
        char message[256];
        std::snprintf(message, sizeof(message),
                      "android-sdl3 EGL current #%llu stage=%s",
                      static_cast<unsigned long long>(calls),
                      stage ? stage : "");
        TVPNativeLogInfo("runtime-host", message);
    }
    return true;
}
} // namespace

extern "C" bool TVPAndroidShowFlutterGameMainMenu() {
    JavaVM *vm = gAndroidJavaVM;
    if(!vm)
        return false;

    JNIEnv *env = nullptr;
    bool shouldDetach = false;
    jint status = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if(status == JNI_EDETACHED) {
        if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
            return false;
        shouldDetach = true;
    } else if(status != JNI_OK) {
        return false;
    }

    const bool shown = ShowFlutterGameMainMenu(env);
    if(shouldDetach)
        vm->DetachCurrentThread();
    return shown;
}

std::string TVPShowFileSelector(const std::string &title,
                                const std::string &filename,
                                std::string initdir, bool issave) {
    char message[256];
    std::snprintf(message, sizeof(message),
                  "script file selector unavailable titleLen=%zu "
                  "filenameLen=%zu initdirLen=%zu save=%d",
                  title.size(), filename.size(), initdir.size(),
                  issave ? 1 : 0);
    TVPNativeLogInfo("android-file-selector", message);
    return {};
}

extern "C" ANativeWindow *TVPAndroidAcquireFlutterGameSurfaceWindow() {
    std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
    if(gFlutterGameSurfaceWindow)
        ANativeWindow_acquire(gFlutterGameSurfaceWindow);
    return gFlutterGameSurfaceWindow;
}

extern "C" void TVPAndroidReleaseFlutterGameSurfaceWindow(
    ANativeWindow *window) {
    if(window)
        ANativeWindow_release(window);
}

extern "C" void TVPAndroidGetFlutterGameSurfaceSize(int *width,
                                                     int *height) {
    std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
    const bool hasFixedSurface = gFlutterGameSurfaceWindow &&
        gFlutterGameSurfaceWidth > 0 && gFlutterGameSurfaceHeight > 0;
    if(width)
        *width = hasFixedSurface ? kTVPSDLFixedGameSurfaceWidth : 0;
    if(height)
        *height = hasFixedSurface ? kTVPSDLFixedGameSurfaceHeight : 0;
}

static std::string JStringToStdString(JNIEnv *env, jstring value) {
    if(!value)
        return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    if(!chars)
        return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

static jobjectArray MakeJavaStringArray(
    JNIEnv *env, const std::vector<std::string> &values) {
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray result =
        env->NewObjectArray(static_cast<jsize>(values.size()), stringClass,
                            nullptr);
    if(!result)
        return nullptr;
    for(size_t i = 0; i < values.size(); ++i) {
        jstring value = env->NewStringUTF(values[i].c_str());
        env->SetObjectArrayElement(result, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return result;
}

extern "C" {
JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeInitRuntime(JNIEnv *env, jclass) {
    TVPAndroidInitializeLegacyHost(env, "KR2Activity.nativeInitRuntime");
}

JNIEXPORT jboolean JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeInitRuntime(JNIEnv *env,
                                                             jclass) {
    TVPAndroidInitializeSDLHost(env,
                                "AndroidRuntimeBridge.nativeInitRuntime");
    return gAndroidBaseInitDone.load(std::memory_order_acquire) &&
            gAndroidSDLJniReady.load(std::memory_order_acquire) &&
            TVPGetRuntimeHost() == &gAndroidSDLRuntimeHost
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeStartGame(
    JNIEnv *env, jclass, jstring gamePath, jstring preferenceRoot) {
    TVPAndroidInitializeSDLHost(env, "AndroidRuntimeBridge.nativeStartGame");
    if(!gAndroidBaseInitDone.load(std::memory_order_acquire) ||
       !gAndroidSDLJniReady.load(std::memory_order_acquire) ||
       TVPGetRuntimeHost() != &gAndroidSDLRuntimeHost) {
        TVPNativeLogInfo("runtime-host",
                         "start rejected: SDL host not ready");
        return JNI_FALSE;
    }
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    TVPAndroidGetFlutterGameSurfaceSize(&surfaceWidth, &surfaceHeight);
    if(surfaceWidth <= 0 || surfaceHeight <= 0) {
        TVPNativeLogInfo("runtime-host",
                         "start rejected: game surface not ready");
        return JNI_FALSE;
    }
    TVPRuntimeHostLaunchRequest request;
    request.gamePath = JStringToStdString(env, gamePath);
    request.preferenceRoot = JStringToStdString(env, preferenceRoot);
    return TVPStartGameOnRuntimeHost(request, "android-runtime-bridge")
        ? JNI_TRUE
        : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeRunFrame(JNIEnv *, jclass) {
    if(TVPGetRuntimeHost() == &gAndroidSDLRuntimeHost) {
        gAndroidSDLRuntimeHost.RunFrameTick();
    } else if(iTVPRuntimeHost *host = TVPGetRuntimeHost()) {
        host->RunFrame(1.0f / 60.0f);
    } else {
        TVPNativeLogInfo("runtime-host", "frame skipped: no runtime host");
    }
}

JNIEXPORT jboolean JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativePumpPresenter(JNIEnv *,
                                                               jclass) {
    return TVPRuntimePumpScreenPresenter("android-runtime-bridge") ? JNI_TRUE
                                                                   : JNI_FALSE;
}

void Java_org_tvp_kirikiri2_KR2Activity_initDump(JNIEnv *env, jclass cls,
                                                 jstring path) {
    const char *pszPath = env->GetStringUTFChars(path, nullptr);
    if(pszPath && *pszPath) {
        std::string message = std::string("initDump path=") + pszPath;
        TVPAppendNativeFatalBreadcrumb("dump", message.c_str());
        static google_breakpad::MinidumpDescriptor descriptor(pszPath);
        static google_breakpad::ExceptionHandler eh(
            descriptor, DumpFilter, DumpCallback, nullptr, true, -1);
    }
    env->ReleaseStringUTFChars(path, pszPath);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_setUseFFmpegImageDecoder(JNIEnv *, jclass,
                                                            jboolean enabled) {
    TVPSetUseFFmpegImageDecoder(enabled == JNI_TRUE);
    try {
        spdlog::info("FFmpeg image decoder enabled={}",
                     enabled == JNI_TRUE ? 1 : 0);
    } catch(...) {
    }
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_setFFmpegDecodeMode(JNIEnv *, jclass,
                                                       jint mode) {
    TVPSetFFmpegDecodeMode(static_cast<int>(mode));
    try {
        spdlog::info("FFmpeg decode mode={}",
                     TVPFFmpegDecodeModeName(TVPGetFFmpegDecodeMode()));
    } catch(...) {
    }
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_configureFileLogging(JNIEnv *env, jclass,
                                                        jboolean enabled,
                                                        jstring path) {
    std::string logFilePath;
    if(path) {
        const char *chars = env->GetStringUTFChars(path, nullptr);
        if(chars) {
            logFilePath = chars;
            env->ReleaseStringUTFChars(path, chars);
        }
    }
    TVPConfigureNativeLogging(enabled == JNI_TRUE, logFilePath);
    std::string message = std::string("configureFileLogging enabled=") +
        (enabled == JNI_TRUE ? "1" : "0") + " path=" + logFilePath;
    TVPAppendNativeFatalBreadcrumb("log", message.c_str());
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeLifecycleEvent(JNIEnv *env, jclass,
                                                        jstring eventName,
                                                        jstring detail) {
    const std::string eventNameValue = JStringToStdString(env, eventName);
    const std::string detailValue = JStringToStdString(env, detail);
    if(eventNameValue.find("onResume") != std::string::npos ||
       eventNameValue.find("surfaceCreated") != std::string::npos)
        gAndroidSDLRuntimeHost.ResetFrameClock();
    TVPSDLRecordAndroidLifecycle(eventNameValue.c_str(), detailValue.c_str());
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeLifecycleEvent(
    JNIEnv *env, jclass, jstring eventName, jstring detail) {
    const std::string eventNameValue = JStringToStdString(env, eventName);
    const std::string detailValue = JStringToStdString(env, detail);
    if(eventNameValue.find("onResume") != std::string::npos ||
       eventNameValue.find("surfaceCreated") != std::string::npos)
        gAndroidSDLRuntimeHost.ResetFrameClock();
    TVPSDLRecordAndroidLifecycle(eventNameValue.c_str(), detailValue.c_str());
}

JNIEXPORT jboolean JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeLauncherLog(JNIEnv *env, jclass,
                                                     jstring message,
                                                     jstring throwableText) {
    std::string nativeMessage = JStringToStdString(env, message);
    const std::string throwableValue = JStringToStdString(env, throwableText);
    if(!throwableValue.empty()) {
        nativeMessage += "\n";
        nativeMessage += throwableValue;
    }
    TVPNativeLogInfo("launcher", nativeMessage.c_str());
    return JNI_TRUE;
}

void Java_org_tvp_kirikiri2_KR2Activity_onMessageBoxOK(JNIEnv *env, jclass cls,
                                                       jint nButton) {
    MsgBoxRet = nButton;
    MessageBoxCond.notify_one();
}

void Java_org_tvp_kirikiri2_KR2Activity_onMessageBoxText(JNIEnv *env,
                                                         jclass cls,
                                                         jstring text) {
    const char *pszText = env->GetStringUTFChars(text, nullptr);
    if(pszText && *pszText) {
        MessageBoxRetText = pszText;
    }
    env->ReleaseStringUTFChars(text, pszText);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeMessageBoxOK(JNIEnv *env, jclass cls,
                                                       jint nButton) {
    Java_org_tvp_kirikiri2_KR2Activity_onMessageBoxOK(env, cls, nButton);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeMessageBoxText(JNIEnv *env,
                                                         jclass cls,
                                                         jstring text) {
    Java_org_tvp_kirikiri2_KR2Activity_onMessageBoxText(env, cls, text);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesBegin(
    JNIEnv *env, jclass thiz, jint id, jfloat x, jfloat y) {
    (void)env;
    (void)thiz;
    // Legacy KR2Activity touch JNI retained for ABI compatibility. Active
    // Android input uses AndroidRuntimeBridge + TVPSDLQueueFlutterTouch*.
    TVPSDLRecordAndroidInput("touch-begin", 1, x, y, id, true);
    TVPSDLQueueFlutterTouchBegin(id, x, y);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesEnd(
    JNIEnv *env, jclass thiz, jint id, jfloat x, jfloat y) {
    (void)env;
    (void)thiz;
    TVPSDLRecordAndroidInput("touch-end", 1, x, y, id, false);
    TVPSDLQueueFlutterTouchEnd(id, x, y);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesMove(
    JNIEnv *env, jclass thiz, jintArray ids, jfloatArray xs, jfloatArray ys) {
    (void)thiz;
    if(!ids || !xs || !ys) {
        TVPSDLRecordAndroidInput("touch-move-empty", 0);
        TVPSDLQueueFlutterTouchMove(0, nullptr, nullptr, nullptr);
        return;
    }
    const jsize count =
        std::min(env->GetArrayLength(ids),
                 std::min(env->GetArrayLength(xs), env->GetArrayLength(ys)));
    if(count <= 0) {
        TVPSDLRecordAndroidInput("touch-move-empty", 0);
        TVPSDLQueueFlutterTouchMove(0, nullptr, nullptr, nullptr);
        return;
    }
    std::vector<jint> idValues(static_cast<size_t>(count));
    std::vector<jfloat> xValues(static_cast<size_t>(count));
    std::vector<jfloat> yValues(static_cast<size_t>(count));
    env->GetIntArrayRegion(ids, 0, count, idValues.data());
    env->GetFloatArrayRegion(xs, 0, count, xValues.data());
    env->GetFloatArrayRegion(ys, 0, count, yValues.data());
    TVPSDLRecordAndroidInput("touch-move", count, xValues[0], yValues[0],
                             idValues[0], true);
    TVPSDLQueueFlutterTouchMove(static_cast<int>(count), idValues.data(),
                                xValues.data(), yValues.data());
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesCancel(
    JNIEnv *env, jclass thiz, jintArray ids, jfloatArray xs, jfloatArray ys) {
    (void)thiz;
    if(!ids || !xs || !ys) {
        TVPSDLRecordAndroidInput("touch-cancel-empty", 0);
        TVPSDLQueueFlutterTouchCancel(0, nullptr, nullptr, nullptr);
        return;
    }
    const jsize count =
        std::min(env->GetArrayLength(ids),
                 std::min(env->GetArrayLength(xs), env->GetArrayLength(ys)));
    if(count <= 0) {
        TVPSDLRecordAndroidInput("touch-cancel-empty", 0);
        TVPSDLQueueFlutterTouchCancel(0, nullptr, nullptr, nullptr);
        return;
    }
    std::vector<jint> idValues(static_cast<size_t>(count));
    std::vector<jfloat> xValues(static_cast<size_t>(count));
    std::vector<jfloat> yValues(static_cast<size_t>(count));
    env->GetIntArrayRegion(ids, 0, count, idValues.data());
    env->GetFloatArrayRegion(xs, 0, count, xValues.data());
    env->GetFloatArrayRegion(ys, 0, count, yValues.data());
    TVPSDLRecordAndroidInput("touch-cancel", count, xValues[0], yValues[0],
                             idValues[0], false);
    TVPSDLQueueFlutterTouchCancel(static_cast<int>(count), idValues.data(),
                                  xValues.data(), yValues.data());
}

#define KEYCODE_BACK 0x04
#define KEYCODE_MENU 0x52
#define KEYCODE_DPAD_UP 0x13
#define KEYCODE_DPAD_DOWN 0x14
#define KEYCODE_DPAD_LEFT 0x15
#define KEYCODE_DPAD_RIGHT 0x16
#define KEYCODE_ENTER 0x42
#define KEYCODE_PLAY 0x7e
#define KEYCODE_DPAD_CENTER 0x17
#define KEYCODE_DEL 0x43

JNIEXPORT jboolean JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeKeyAction(
    JNIEnv *env, jclass cls, jint keyCode, jboolean isPress) {
    (void)cls;
    const bool pressed = isPress == JNI_TRUE;
    if(TVPSDLDispatchAndroidKeyAction(keyCode, pressed)) {
        TVPSDLRecordAndroidInput("key-direct", 0, 0.0f, 0.0f, keyCode,
                                 pressed);
        return JNI_TRUE;
    }
    if(TVPSDLHasScreenPresenterPresented() && keyCode == KEYCODE_MENU) {
        if(!pressed || ShowFlutterGameMainMenu(env)) {
            TVPSDLRecordAndroidInput("key-menu-flutter", 0, 0.0f, 0.0f,
                                     keyCode, pressed);
            return JNI_TRUE;
        }
    }
    TVPSDLRecordAndroidInput("key-takeover-drop", 0, 0.0f, 0.0f, keyCode,
                             pressed);
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeInsertText(
    JNIEnv *env, jclass cls, jstring text) {
    (void)cls;
    if(!text) {
        TVPSDLRecordAndroidInput("text-insert-null", 0);
        return;
    }
    const char *pszText = env->GetStringUTFChars(text, nullptr);
    if(pszText && *pszText) {
        std::string str = pszText;
        TVPSDLRecordAndroidInput("text-insert", 0, 0.0f, 0.0f,
                                 static_cast<int>(str.length()), true);
        (void)TVPSDLDispatchTextInput(str.c_str());
    }
    env->ReleaseStringUTFChars(text, pszText);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeDeleteBackward(
    JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    TVPSDLRecordAndroidInput("text-delete", 0, 0.0f, 0.0f, VK_BACK, false);
    (void)TVPSDLDispatchDeleteBackward();
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeCharInput(
    JNIEnv *env, jclass cls, jint keyCode) {
    (void)env;
    (void)cls;
    TVPSDLRecordAndroidInput("char-input", 0, 0.0f, 0.0f, keyCode, true);
    (void)TVPSDLDispatchCharInput(keyCode);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeCommitText(
    JNIEnv *env, jclass cls, jstring text, jint newCursorPosition) {
    (void)cls;
    (void)newCursorPosition;
    if(!text) {
        TVPSDLRecordAndroidInput("text-commit-null", 0);
        return;
    }
    const char *utftext = env->GetStringUTFChars(text, nullptr);
    if(!utftext) {
        TVPSDLRecordAndroidInput("text-commit-null", 0);
        return;
    }
    std::string str(utftext);
    env->ReleaseStringUTFChars(text, utftext);
    TVPSDLRecordAndroidInput("text-commit", 0, 0.0f, 0.0f,
                             static_cast<int>(str.length()), true);
    (void)TVPSDLDispatchTextInput(str.c_str());
}

JNIEXPORT jboolean JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeKeyAction(JNIEnv *env, jclass cls,
                                                    jint keyCode,
                                                    jboolean isPress) {
    return Java_org_tvp_kirikiri2_KR2Activity_nativeKeyAction(env, cls, keyCode,
                                                              isPress);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeCharInput(JNIEnv *env, jclass cls,
                                                    jint keyCode) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeCharInput(env, cls, keyCode);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeCommitText(JNIEnv *env, jclass cls,
                                                     jstring text,
                                                     jint newCursorPosition) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeCommitText(
        env, cls, text, newCursorPosition);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_NativeUiHost_nativeDeleteBackward(JNIEnv *env,
                                                         jclass cls) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeDeleteBackward(env, cls);
}

JNIEXPORT jboolean JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeGetHideSystemButton(JNIEnv *env,
                                                             jclass cls) {
    return GlobalConfigManager::GetInstance()->GetValue<bool>(
        "hide_android_sys_btn", false);
}

static float _mouseX, _mouseY;

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeHoverMoved(
    JNIEnv *env, jclass cls, jfloat x, jfloat y) {
    (void)env;
    (void)cls;
    _mouseX = x;
    _mouseY = y;
    TVPSDLRecordAndroidInput("hover-move", 1, x, y, 0, true);
    (void)TVPSDLDispatchAndroidHoverMove(x, y);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeMouseScrolled(
    JNIEnv *env, jclass cls, jfloat v) {
    (void)env;
    (void)cls;
    TVPSDLRecordAndroidInput("mouse-scroll", 0, _mouseX, v, 0, true);
    (void)TVPSDLDispatchAndroidMouseScroll(_mouseX, _mouseY, v);
}

JNIEXPORT jboolean JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeKeyAction(
    JNIEnv *env, jclass, jint keyCode, jboolean isPress) {
    return Java_org_tvp_kirikiri2_KR2Activity_nativeKeyAction(env, nullptr,
                                                              keyCode, isPress);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeHoverMoved(JNIEnv *env, jclass,
                                                            jfloat x, jfloat y) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeHoverMoved(env, nullptr, x, y);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeMouseScrolled(JNIEnv *env,
                                                               jclass,
                                                               jfloat scroll) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeMouseScrolled(env, nullptr, scroll);
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeOnLowMemory(JNIEnv *env, jclass cls) {
    TVPAppendNativeFatalBreadcrumb("memory", "nativeOnLowMemory");
    Android_PushEvents([]() {
        ::Application->OnLowMemory();
    });
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeOnLowMemory(JNIEnv *env,
                                                             jclass cls) {
    Java_org_tvp_kirikiri2_KR2Activity_nativeOnLowMemory(env, cls);
}

static void TVPAndroidSetGameSurface(JNIEnv *env, jobject surface, jint width,
                                     jint height, const char *source) {
    {
        std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
        if(gFlutterGameSurfaceWindow) {
            ANativeWindow_release(gFlutterGameSurfaceWindow);
            gFlutterGameSurfaceWindow = nullptr;
        }
        gFlutterGameSurfaceWidth = 0;
        gFlutterGameSurfaceHeight = 0;

        if(surface) {
            gFlutterGameSurfaceWindow = ANativeWindow_fromSurface(env, surface);
            if(gFlutterGameSurfaceWindow) {
                gFlutterGameSurfaceWidth = kTVPSDLFixedGameSurfaceWidth;
                gFlutterGameSurfaceHeight = kTVPSDLFixedGameSurfaceHeight;
                const int geometryResult = ANativeWindow_setBuffersGeometry(
                    gFlutterGameSurfaceWindow, gFlutterGameSurfaceWidth,
                    gFlutterGameSurfaceHeight, WINDOW_FORMAT_RGBA_8888);
                char message[192];
                std::snprintf(message, sizeof(message),
                              "%s set game surface window=%p size=%dx%d "
                              "requested=%dx%d geometry=%d",
                              source ? source : "unknown",
                              static_cast<void *>(gFlutterGameSurfaceWindow),
                              gFlutterGameSurfaceWidth,
                              gFlutterGameSurfaceHeight, width, height,
                              geometryResult);
                TVPNativeLogInfo("flutter-surface", message);
            } else {
                TVPNativeLogInfo("flutter-surface",
                                 "set game surface failed: null ANativeWindow");
            }
        } else {
            TVPNativeLogInfo("flutter-surface",
                             "set game surface: null surface");
        }
    }
    TVPSDLNotifyAndroidFlutterGameSurfaceChanged("set");
}

static void TVPAndroidResizeGameSurface(jint width, jint height,
                                        const char *source) {
    {
        std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
        int geometryResult = 0;
        if(gFlutterGameSurfaceWindow) {
            gFlutterGameSurfaceWidth = kTVPSDLFixedGameSurfaceWidth;
            gFlutterGameSurfaceHeight = kTVPSDLFixedGameSurfaceHeight;
            geometryResult = ANativeWindow_setBuffersGeometry(
                gFlutterGameSurfaceWindow, gFlutterGameSurfaceWidth,
                gFlutterGameSurfaceHeight, WINDOW_FORMAT_RGBA_8888);
        } else {
            gFlutterGameSurfaceWidth = 0;
            gFlutterGameSurfaceHeight = 0;
        }
        char message[160];
        std::snprintf(message, sizeof(message),
                      "%s resize game surface size=%dx%d requested=%dx%d geometry=%d",
                      source ? source : "unknown", gFlutterGameSurfaceWidth,
                      gFlutterGameSurfaceHeight, width, height,
                      geometryResult);
        TVPNativeLogInfo("flutter-surface", message);
    }
    TVPSDLNotifyAndroidFlutterGameSurfaceChanged("resize");
}

static void TVPAndroidDetachGameSurface(const char *source) {
    {
        std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
        if(gFlutterGameSurfaceWindow) {
            ANativeWindow_release(gFlutterGameSurfaceWindow);
            gFlutterGameSurfaceWindow = nullptr;
        }
        gFlutterGameSurfaceWidth = 0;
        gFlutterGameSurfaceHeight = 0;
        char message[96];
        std::snprintf(message, sizeof(message), "%s detach game surface",
                      source ? source : "unknown");
        TVPNativeLogInfo("flutter-surface", message);
    }
    TVPSDLNotifyAndroidFlutterGameSurfaceChanged("detach");
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeSetGameSurface(
    JNIEnv *env, jclass, jobject surface, jint width, jint height) {
    TVPAndroidSetGameSurface(env, surface, width, height,
                             "AndroidRuntimeBridge");
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeResizeGameSurface(
    JNIEnv *, jclass, jint width, jint height) {
    TVPAndroidResizeGameSurface(width, height, "AndroidRuntimeBridge");
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeDetachGameSurface(JNIEnv *,
                                                                   jclass) {
    TVPAndroidDetachGameSurface("AndroidRuntimeBridge");
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_flutter_1engine_1bridge_FlutterEngineBridgePlugin_nativeSetApplicationContext(
    JNIEnv *env, jobject, jobject context) {
    krkr_SetApplicationContext(env, context);
    TVPNativeLogInfo("flutter-context", context ? "set application context"
                                                : "clear application context");
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeSetApplicationContext(
    JNIEnv *env, jclass, jobject context) {
    krkr_SetApplicationContext(env, context);
    TVPNativeLogInfo("flutter-context", context ? "bridge set application context"
                                                : "bridge clear application context");
}

static jintArray TVPAndroidGetGameSurfaceMetrics(JNIEnv *env) {
    int presentedWidth = 0;
    int presentedHeight = 0;
    TVPSDLGetPresentedSurfaceSize(&presentedWidth, &presentedHeight);

    const TVPRuntimePresentFrameInfo frameInfo =
        TVPRuntimeGetPresentFrameInfo();
    const bool hasPostedFrameInfo = frameInfo.valid && presentedWidth > 0 &&
        presentedHeight > 0;
    const int contentWidth =
        hasPostedFrameInfo && frameInfo.textureWidth > 0
            ? frameInfo.textureWidth
            : 0;
    const int contentHeight =
        hasPostedFrameInfo && frameInfo.textureHeight > 0
            ? frameInfo.textureHeight
            : 0;

    int flutterWidth = 0;
    int flutterHeight = 0;
    {
        std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
        flutterWidth = gFlutterGameSurfaceWidth;
        flutterHeight = gFlutterGameSurfaceHeight;
    }

    jint values[10] = {presentedWidth,
                       presentedHeight,
                       flutterWidth,
                       flutterHeight,
                       contentWidth,
                       contentHeight,
                       hasPostedFrameInfo ? frameInfo.destRect.x : 0,
                       hasPostedFrameInfo ? frameInfo.destRect.y : 0,
                       hasPostedFrameInfo ? frameInfo.destRect.w : 0,
                       hasPostedFrameInfo ? frameInfo.destRect.h : 0};
    jintArray result = env->NewIntArray(10);
    if(result)
        env->SetIntArrayRegion(result, 0, 10, values);
    return result;
}

JNIEXPORT jintArray JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeGetGameSurfaceMetrics(
    JNIEnv *env, jclass) {
    return TVPAndroidGetGameSurfaceMetrics(env);
}

static jobjectArray TVPAndroidGetLoadingConsoleSnapshot(JNIEnv *env) {
    const TVPSDLLoadingConsoleSnapshot snapshot =
        TVPSDLGetLoadingConsoleSnapshot();
    std::vector<std::string> values;
    values.reserve(snapshot.lines.size() + 1);

    char meta[96];
    std::snprintf(meta, sizeof(meta), "%d\t%llu\t%llu",
                  snapshot.active ? 1 : 0,
                  static_cast<unsigned long long>(snapshot.session),
                  static_cast<unsigned long long>(snapshot.totalLines));
    values.emplace_back(meta);

    for(const TVPSDLLoadingConsoleLineSnapshot &line : snapshot.lines) {
        std::string value = line.important ? "1\t" : "0\t";
        value += line.message;
        values.emplace_back(value);
    }
    return MakeJavaStringArray(env, values);
}

JNIEXPORT jobjectArray JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeGetLoadingConsoleSnapshot(
    JNIEnv *env, jclass) {
    return TVPAndroidGetLoadingConsoleSnapshot(env);
}

static jobjectArray TVPAndroidGetRenderOverlayStats(JNIEnv *env) {
    const TVPSDLRenderOverlaySnapshot snapshot =
        TVPSDLGetRenderOverlaySnapshot();
    std::vector<std::string> values;
    values.reserve(10);
    values.emplace_back(snapshot.showFps ? "1" : "0");
    values.emplace_back(snapshot.available ? "1" : "0");

    char number[96];
    std::snprintf(number, sizeof(number), "%.3f", snapshot.fps);
    values.emplace_back(number);
    values.emplace_back(std::to_string(snapshot.drawCount));
    values.emplace_back(std::to_string(snapshot.videoMemoryBytes));
    values.emplace_back(std::to_string(snapshot.selfMemoryMb));
    values.emplace_back(std::to_string(snapshot.freeMemoryMb));
    values.emplace_back(std::to_string(snapshot.presentedFrames));
    values.emplace_back(std::to_string(snapshot.sequence));
    values.emplace_back(snapshot.rendererName);
    return MakeJavaStringArray(env, values);
}

JNIEXPORT jobjectArray JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeGetRenderOverlayStats(
    JNIEnv *env, jclass) {
    return TVPAndroidGetRenderOverlayStats(env);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeFlutterTouchesBegin(
    JNIEnv *env, jclass, jint id, jfloat x, jfloat y) {
    (void)env;
    TVPSDLQueueFlutterTouchBegin(id, x, y);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeFlutterTouchesEnd(
    JNIEnv *env, jclass, jint id, jfloat x, jfloat y) {
    (void)env;
    TVPSDLQueueFlutterTouchEnd(id, x, y);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeFlutterTouchesMove(
    JNIEnv *env, jclass, jintArray ids, jfloatArray xs, jfloatArray ys) {
    if(!ids || !xs || !ys) {
        TVPSDLQueueFlutterTouchMove(0, nullptr, nullptr, nullptr);
        return;
    }
    const jsize count =
        std::min(env->GetArrayLength(ids),
                 std::min(env->GetArrayLength(xs), env->GetArrayLength(ys)));
    std::vector<jint> idValues(static_cast<size_t>(count));
    std::vector<jfloat> xValues(static_cast<size_t>(count));
    std::vector<jfloat> yValues(static_cast<size_t>(count));
    if(count > 0) {
        env->GetIntArrayRegion(ids, 0, count, idValues.data());
        env->GetFloatArrayRegion(xs, 0, count, xValues.data());
        env->GetFloatArrayRegion(ys, 0, count, yValues.data());
    }
    TVPSDLQueueFlutterTouchMove(
        static_cast<int>(count), idValues.data(), xValues.data(),
        yValues.data());
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_AndroidRuntimeBridge_nativeFlutterTouchesCancel(
    JNIEnv *env, jclass, jintArray ids, jfloatArray xs, jfloatArray ys) {
    if(!ids || !xs || !ys) {
        TVPSDLQueueFlutterTouchCancel(0, nullptr, nullptr, nullptr);
        return;
    }
    const jsize count =
        std::min(env->GetArrayLength(ids),
                 std::min(env->GetArrayLength(xs), env->GetArrayLength(ys)));
    std::vector<jint> idValues(static_cast<size_t>(count));
    std::vector<jfloat> xValues(static_cast<size_t>(count));
    std::vector<jfloat> yValues(static_cast<size_t>(count));
    if(count > 0) {
        env->GetIntArrayRegion(ids, 0, count, idValues.data());
        env->GetFloatArrayRegion(xs, 0, count, xValues.data());
        env->GetFloatArrayRegion(ys, 0, count, yValues.data());
    }
    TVPSDLQueueFlutterTouchCancel(
        static_cast<int>(count), idValues.data(), xValues.data(),
        yValues.data());
}
}
