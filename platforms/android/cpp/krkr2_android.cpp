/* Include the SDL main definition header */
#include <memory>
#include <jni.h>
#include <dlfcn.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "environ/cocos2d/AppDelegate.h"
#include "environ/cocos2d/MainScene.h"
#include "environ/ConfigManager/GlobalConfigManager.h"
#include "environ/Application.h"
#include "environ/NativeLog.h"
#include "environ/android/AndroidUtils.h"
#include "environ/sdl/SDLGameManager.h"
#include "common/FFmpegDecodeConfig.h"
#include "vkdefine.h"

/*******************************************************************************
                 Functions called by JNI
*******************************************************************************/
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <condition_variable>
#include <mutex>
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

[[maybe_unused]] void cocos_android_app_init(JNIEnv *env) { // for cocos3.10+

    TVPInitializeNativeLogging();
    TVPAppendNativeFatalBreadcrumb("jni", "cocos_android_app_init enter");

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
    void *handle = dlopen("libSDL3.so", RTLD_LAZY);
    if(handle) {
        typedef jint (*JNI_OnLoad)(JavaVM *, void *);
        void *sdl3Init = dlsym(handle, "JNI_OnLoad");
        if(!sdl3Init ||
           ((JNI_OnLoad)sdl3Init)(vm, nullptr) != JNI_VERSION_1_4) {
            spdlog::critical("invoke libSDL3.so JNI_OnLoad method failed");
            TVPAppendNativeFatalBreadcrumb("jni",
                                           "libSDL3.so JNI_OnLoad failed");
        } else {
            TVPAppendNativeFatalBreadcrumb("jni",
                                           "libSDL3.so JNI_OnLoad ok");
        }
    } else {
        spdlog::critical("load libSDL3.so failed");
        TVPAppendNativeFatalBreadcrumb("jni", "dlopen libSDL3.so failed");
    }

    static std::unique_ptr<TVPAppDelegate> pAppDelegate =
        std::make_unique<TVPAppDelegate>();
    TVPAppendNativeFatalBreadcrumb("jni", "TVPAppDelegate created");
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

bool ShouldRouteLegacyInputToCocos() {
    return !TVPSDLIsScreenTakeoverEnabled();
}

bool ShowFlutterGameMainMenu(JNIEnv *env) {
    if(!env)
        return false;

    jclass cls = env->FindClass("org/github/krkr2/MainActivity");
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
    if(width)
        *width = gFlutterGameSurfaceWidth;
    if(height)
        *height = gFlutterGameSurfaceHeight;
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

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesBegin(
    JNIEnv *env, jclass thiz, jint id, jfloat x, jfloat y) {
    TVPSDLRecordAndroidInput("touch-begin", 1, x, y, id, true);
    if(!ShouldRouteLegacyInputToCocos())
        return;
    intptr_t idlong = id;
    Android_PushEvents([idlong, x, y]() {
        cocos2d::Director::getInstance()->getOpenGLView()->handleTouchesBegin(
            1, (intptr_t *)&idlong, (float *)&x, (float *)&y);
    });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesEnd(
    JNIEnv *env, jclass thiz, jint id, jfloat x, jfloat y) {
    TVPSDLRecordAndroidInput("touch-end", 1, x, y, id, false);
    if(!ShouldRouteLegacyInputToCocos())
        return;
    intptr_t idlong = id;
    Android_PushEvents([idlong, x, y]() {
        cocos2d::Director::getInstance()->getOpenGLView()->handleTouchesEnd(
            1, (intptr_t *)&idlong, (float *)&x, (float *)&y);
    });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesMove(
    JNIEnv *env, jclass thiz, jintArray ids, jfloatArray xs, jfloatArray ys) {
    int size = env->GetArrayLength(ids);
    if(size <= 0) {
        TVPSDLRecordAndroidInput("touch-move-empty", 0);
        return;
    }
    if(size == 1) {
        intptr_t idlong;
        jint id;
        jfloat x;
        jfloat y;
        env->GetIntArrayRegion(ids, 0, size, &id);
        env->GetFloatArrayRegion(xs, 0, size, &x);
        env->GetFloatArrayRegion(ys, 0, size, &y);
        idlong = id;
        TVPSDLRecordAndroidInput("touch-move", 1, x, y, id, true);
        if(!ShouldRouteLegacyInputToCocos())
            return;
        Android_PushEvents([idlong, x, y]() {
            cocos2d::Director::getInstance()
                ->getOpenGLView()
                ->handleTouchesMove(1, (intptr_t *)&idlong, (float *)&x,
                                    (float *)&y);
        });
        return;
    }

    jint id[size];
    std::vector<jfloat> x;
    x.resize(size);
    std::vector<jfloat> y;
    y.resize(size);

    env->GetIntArrayRegion(ids, 0, size, id);
    env->GetFloatArrayRegion(xs, 0, size, &x[0]);
    env->GetFloatArrayRegion(ys, 0, size, &y[0]);

    std::vector<intptr_t> idlong;
    idlong.resize(size);
    for(int i = 0; i < size; i++)
        idlong[i] = id[i];

    TVPSDLRecordAndroidInput("touch-move", size, x[0], y[0], id[0], true);
    if(!ShouldRouteLegacyInputToCocos())
        return;
    Android_PushEvents([idlong, x, y]() {
        cocos2d::Director::getInstance()->getOpenGLView()->handleTouchesMove(
            idlong.

            size(),
            (intptr_t

                 *)&idlong[0],
            (float *)&x[0], (float *)&y[0]);
    });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeTouchesCancel(
    JNIEnv *env, jclass thiz, jintArray ids, jfloatArray xs, jfloatArray ys) {
    int size = env->GetArrayLength(ids);
    if(size <= 0) {
        TVPSDLRecordAndroidInput("touch-cancel-empty", 0);
        return;
    }
    if(size == 1) {
        intptr_t idlong;
        jint id;
        jfloat x;
        jfloat y;
        env->GetIntArrayRegion(ids, 0, size, &id);
        env->GetFloatArrayRegion(xs, 0, size, &x);
        env->GetFloatArrayRegion(ys, 0, size, &y);
        idlong = id;
        TVPSDLRecordAndroidInput("touch-cancel", 1, x, y, id, false);
        if(!ShouldRouteLegacyInputToCocos())
            return;
        Android_PushEvents([idlong, x, y]() {
            cocos2d::Director::getInstance()
                ->getOpenGLView()
                ->handleTouchesCancel(1, (intptr_t *)&idlong, (float *)&x,
                                      (float *)&y);
        });
        return;
    }

    jint id[size];
    std::vector<jfloat> x;
    x.resize(size);
    std::vector<jfloat> y;
    y.resize(size);

    env->GetIntArrayRegion(ids, 0, size, id);
    env->GetFloatArrayRegion(xs, 0, size, &x[0]);
    env->GetFloatArrayRegion(ys, 0, size, &y[0]);

    std::vector<intptr_t> idlong;
    idlong.resize(size);
    for(int i = 0; i < size; i++)
        idlong[i] = id[i];

    TVPSDLRecordAndroidInput("touch-cancel", size, x[0], y[0], id[0], false);
    if(!ShouldRouteLegacyInputToCocos())
        return;
    Android_PushEvents([idlong, x, y]() {
        cocos2d::Director::getInstance()->getOpenGLView()->handleTouchesCancel(
            idlong.

            size(),
            (intptr_t

                 *)&idlong[0],
            (float *)&x[0], (float *)&y[0]);
    });
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
    const bool pressed = isPress == JNI_TRUE;
    if(TVPSDLDispatchAndroidKeyAction(keyCode, pressed)) {
        TVPSDLRecordAndroidInput("key-direct", 0, 0.0f, 0.0f, keyCode,
                                 pressed);
        return JNI_TRUE;
    }
    if(TVPSDLIsScreenTakeoverEnabled() && keyCode == KEYCODE_MENU) {
        if(!pressed || ShowFlutterGameMainMenu(env)) {
            TVPSDLRecordAndroidInput("key-menu-flutter", 0, 0.0f, 0.0f,
                                     keyCode, pressed);
            return JNI_TRUE;
        }
    }
    if(!ShouldRouteLegacyInputToCocos()) {
        TVPSDLRecordAndroidInput("key-takeover-drop", 0, 0.0f, 0.0f,
                                 keyCode, pressed);
        return JNI_TRUE;
    }

    cocos2d::EventKeyboard::KeyCode pKeyCode;
    switch(keyCode) {
        case KEYCODE_BACK:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_ESCAPE;
            break;
        case KEYCODE_MENU:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_MENU;
            break;
        case KEYCODE_DPAD_UP:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_DPAD_UP;
            break;
        case KEYCODE_DPAD_DOWN:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_DPAD_DOWN;
            break;
        case KEYCODE_DPAD_LEFT:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_DPAD_LEFT;
            break;
        case KEYCODE_DPAD_RIGHT:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_DPAD_RIGHT;
            break;
        case KEYCODE_ENTER:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_ENTER;
            break;
        case KEYCODE_PLAY:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_PLAY;
            break;
        case KEYCODE_DPAD_CENTER:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_DPAD_CENTER;
            break;
        case KEYCODE_DEL:
            pKeyCode = cocos2d::EventKeyboard::KeyCode::KEY_BACKSPACE;
            break;
        default:
            TVPSDLRecordAndroidInput("key-unhandled", 0, 0.0f, 0.0f, keyCode,
                                     pressed);
            return JNI_FALSE;
    }

    TVPSDLRecordAndroidInput("key", 0, 0.0f, 0.0f, keyCode, pressed);
    Android_PushEvents([pKeyCode, pressed]() {
        cocos2d::EventKeyboard event(pKeyCode, pressed);
        cocos2d::Director::getInstance()->getEventDispatcher()->dispatchEvent(
            &event);
    });
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeInsertText(
    JNIEnv *env, jclass cls, jstring text) {
    if(!text) {
        TVPSDLRecordAndroidInput("text-insert-null", 0);
        return;
    }
    const char *pszText = env->GetStringUTFChars(text, nullptr);
    if(pszText && *pszText) {
        std::string str = pszText;
        TVPSDLRecordAndroidInput("text-insert", 0, 0.0f, 0.0f,
                                 static_cast<int>(str.length()), true);
        if(TVPSDLDispatchTextInput(str.c_str()) ||
           !ShouldRouteLegacyInputToCocos()) {
            env->ReleaseStringUTFChars(text, pszText);
            return;
        }
        Android_PushEvents([str]() {
            cocos2d::IMEDispatcher::sharedDispatcher()->dispatchInsertText(
                str.

                c_str(),
                str

                    .

                length()

            );
        });
    }
    env->ReleaseStringUTFChars(text, pszText);
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeDeleteBackward(
    JNIEnv *env, jclass cls) {
    TVPSDLRecordAndroidInput("text-delete", 0, 0.0f, 0.0f, VK_BACK, false);
    if(TVPSDLDispatchDeleteBackward() || !ShouldRouteLegacyInputToCocos())
        return;
    Android_PushEvents([capture0 = cocos2d::IMEDispatcher::sharedDispatcher()] {
        capture0->

            dispatchDeleteBackward();
    });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeCharInput(
    JNIEnv *env, jclass cls, jint keyCode) {
    TVPSDLRecordAndroidInput("char-input", 0, 0.0f, 0.0f, keyCode, true);
    if(TVPSDLDispatchCharInput(keyCode) || !ShouldRouteLegacyInputToCocos())
        return;
    TVPMainScene *pScene = TVPMainScene::GetInstance();
    if(!pScene)
        return;
    pScene->getScheduler()->performFunctionInCocosThread(
        [keyCode] { TVPMainScene::onCharInput(keyCode); });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeCommitText(
    JNIEnv *env, jclass cls, jstring text, jint newCursorPosition) {
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
    if(TVPSDLDispatchTextInput(str.c_str()) || !ShouldRouteLegacyInputToCocos())
        return;
    TVPMainScene *pScene = TVPMainScene::GetInstance();
    if(!pScene)
        return;
    pScene->getScheduler()->performFunctionInCocosThread(
        [str] { TVPMainScene::onTextInput(str); });
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
    _mouseX = x;
    _mouseY = y;
    TVPSDLRecordAndroidInput("hover-move", 1, x, y, 0, true);
    if(TVPSDLDispatchAndroidHoverMove(x, y) || !ShouldRouteLegacyInputToCocos())
        return;
    Android_PushEvents([x, y]() {
        cocos2d::GLView *glview =
            cocos2d::Director::getInstance()->getOpenGLView();
        float _scaleX = glview->getScaleX(), _scaleY = glview->getScaleY();
        const cocos2d::Rect _viewPortRect = glview->getViewPortRect();

        float cursorX = (_mouseX - _viewPortRect.origin.x) / _scaleX;
        float cursorY =
            (_viewPortRect.origin.y + _viewPortRect.size.height - _mouseY) /
            _scaleY;

        cocos2d::EventMouse event(
            cocos2d::EventMouse::MouseEventType::MOUSE_MOVE);

        event.setCursorPosition(cursorX, cursorY);
        cocos2d::Director::getInstance()->getEventDispatcher()->dispatchEvent(
            &event);
    });
}

JNIEXPORT void JNICALL Java_org_tvp_kirikiri2_KR2Activity_nativeMouseScrolled(
    JNIEnv *env, jclass cls, jfloat v) {
    TVPSDLRecordAndroidInput("mouse-scroll", 0, _mouseX, v, 0, true);
    if(TVPSDLDispatchAndroidMouseScroll(_mouseX, _mouseY, v) ||
       !ShouldRouteLegacyInputToCocos())
        return;
    Android_PushEvents([v]() {
        cocos2d::GLView *glview =
            cocos2d::Director::getInstance()->getOpenGLView();
        float _scaleX = glview->getScaleX(), _scaleY = glview->getScaleY();
        const cocos2d::Rect _viewPortRect = glview->getViewPortRect();

        float cursorX = (_mouseX - _viewPortRect.origin.x) / _scaleX;
        float cursorY =
            (_viewPortRect.origin.y + _viewPortRect.size.height - _mouseY) /
            _scaleY;

        cocos2d::EventMouse event(
            cocos2d::EventMouse::MouseEventType::MOUSE_SCROLL);

        event.setScrollData(0, v);
        event.setCursorPosition(cursorX, cursorY);
        cocos2d::Director::getInstance()->getEventDispatcher()->dispatchEvent(
            &event);
    });
}

JNIEXPORT void JNICALL
Java_org_tvp_kirikiri2_KR2Activity_nativeOnLowMemory(JNIEnv *env, jclass cls) {
    TVPAppendNativeFatalBreadcrumb("memory", "nativeOnLowMemory");
    Android_PushEvents([]() {
        ::Application->

            OnLowMemory();
    });
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeSetGameSurface(JNIEnv *env,
                                                        jobject,
                                                        jobject surface,
                                                        jint width,
                                                        jint height) {
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
            gFlutterGameSurfaceWidth = width > 0 ? width : 0;
            gFlutterGameSurfaceHeight = height > 0 ? height : 0;
            ANativeWindow_setBuffersGeometry(gFlutterGameSurfaceWindow,
                                             gFlutterGameSurfaceWidth,
                                             gFlutterGameSurfaceHeight,
                                             WINDOW_FORMAT_RGBA_8888);
            char message[192];
            std::snprintf(message, sizeof(message),
                          "set game surface window=%p size=%dx%d",
                          static_cast<void *>(gFlutterGameSurfaceWindow),
                          gFlutterGameSurfaceWidth,
                          gFlutterGameSurfaceHeight);
            TVPNativeLogInfo("flutter-surface", message);
        } else {
            TVPNativeLogInfo("flutter-surface",
                             "set game surface failed: null ANativeWindow");
        }
    } else {
        TVPNativeLogInfo("flutter-surface", "set game surface: null surface");
    }
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeResizeGameSurface(JNIEnv *, jobject,
                                                           jint width,
                                                           jint height) {
    std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
    gFlutterGameSurfaceWidth = width > 0 ? width : 0;
    gFlutterGameSurfaceHeight = height > 0 ? height : 0;
    if(gFlutterGameSurfaceWindow) {
        ANativeWindow_setBuffersGeometry(gFlutterGameSurfaceWindow,
                                         gFlutterGameSurfaceWidth,
                                         gFlutterGameSurfaceHeight,
                                         WINDOW_FORMAT_RGBA_8888);
    }
    char message[160];
    std::snprintf(message, sizeof(message), "resize game surface size=%dx%d",
                  gFlutterGameSurfaceWidth, gFlutterGameSurfaceHeight);
    TVPNativeLogInfo("flutter-surface", message);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeDetachGameSurface(JNIEnv *, jobject) {
    std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
    if(gFlutterGameSurfaceWindow) {
        ANativeWindow_release(gFlutterGameSurfaceWindow);
        gFlutterGameSurfaceWindow = nullptr;
    }
    gFlutterGameSurfaceWidth = 0;
    gFlutterGameSurfaceHeight = 0;
    TVPNativeLogInfo("flutter-surface", "detach game surface");
}

JNIEXPORT jintArray JNICALL
Java_org_github_krkr2_MainActivity_nativeGetGameSurfaceMetrics(JNIEnv *env,
                                                               jobject) {
    int presentedWidth = 0;
    int presentedHeight = 0;
    TVPSDLGetPresentedSurfaceSize(&presentedWidth, &presentedHeight);

    int flutterWidth = 0;
    int flutterHeight = 0;
    {
        std::lock_guard<std::mutex> lock(gFlutterGameSurfaceLock);
        flutterWidth = gFlutterGameSurfaceWidth;
        flutterHeight = gFlutterGameSurfaceHeight;
    }

    jint values[4] = {presentedWidth, presentedHeight, flutterWidth,
                      flutterHeight};
    jintArray result = env->NewIntArray(4);
    if(result)
        env->SetIntArrayRegion(result, 0, 4, values);
    return result;
}

JNIEXPORT jobjectArray JNICALL
Java_org_github_krkr2_MainActivity_nativeGetLoadingConsoleSnapshot(
    JNIEnv *env, jobject) {
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
Java_org_github_krkr2_MainActivity_nativeGetRenderOverlayStats(JNIEnv *env,
                                                               jobject) {
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

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeFlutterTouchesBegin(JNIEnv *env,
                                                             jobject,
                                                             jint id,
                                                             jfloat x,
                                                             jfloat y) {
    (void)env;
    TVPSDLQueueFlutterTouchBegin(id, x, y);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeFlutterTouchesEnd(JNIEnv *env,
                                                           jobject, jint id,
                                                           jfloat x,
                                                           jfloat y) {
    (void)env;
    TVPSDLQueueFlutterTouchEnd(id, x, y);
}

JNIEXPORT void JNICALL
Java_org_github_krkr2_MainActivity_nativeFlutterTouchesMove(JNIEnv *env,
                                                            jobject,
                                                            jintArray ids,
                                                            jfloatArray xs,
                                                            jfloatArray ys) {
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
Java_org_github_krkr2_MainActivity_nativeFlutterTouchesCancel(JNIEnv *env,
                                                              jobject,
                                                              jintArray ids,
                                                              jfloatArray xs,
                                                              jfloatArray ys) {
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
