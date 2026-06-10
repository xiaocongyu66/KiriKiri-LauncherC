#include "FlutterGameMenuBridge.h"

#include "WindowIntf.h"
#include "cocos2d/MainScene.h"
#include "MenuItemIntf.h"
#include "impl/MenuItemImpl.h"
#include "Application.h"
#include "Platform.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "platform/CCGLView.h"
#if defined(__ANDROID__)
#include <jni.h>
#include <cocos/platform/android/jni/JniHelper.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

iTJSDispatch2 *TVPGetMenuDispatch(tTVInteger hWnd);
tTJSNI_Window *TVPGetActiveWindow();

bool IsOverlayAction(const char *value, const char *expected) {
    return value && expected && std::strcmp(value, expected) == 0;
}

namespace {

thread_local std::string LastMenuJson;

void AppendJsonString(std::string &out, const std::string &value) {
    out.push_back('"');
    for(char ch : value) {
        switch(ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if(static_cast<unsigned char>(ch) < 0x20) {
                char buffer[7];
                snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                out += buffer;
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}

tTJSNI_MenuItem *GetActiveRootMenu() {
    iTJSDispatch2 *menuObject =
        TVPGetMenuDispatch((tjs_intptr_t)TVPGetActiveWindow());
    if(!menuObject)
        return nullptr;

    tTJSNI_MenuItem *menu = nullptr;
    menuObject->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                                      tTJSNC_MenuItem::ClassID,
                                      (iTJSNativeInstance **)&menu);
    if(!tTJSNI_BaseMenuItem::IsLiveInstance(menu))
        return nullptr;
    return menu;
}

void AppendMenuItemJson(std::string &out, tTJSNI_MenuItem *item,
                        const std::string &path) {
    ttstr caption;
    item->GetCaption(caption);
    const std::string title = caption.AsStdString();

    out += "{\"title\":";
    AppendJsonString(out, title);
    out += ",\"path\":";
    AppendJsonString(out, path);
    out += ",\"enabled\":";
    out += item->GetEnabled() ? "true" : "false";
    out += ",\"checked\":";
    out += item->GetChecked() ? "true" : "false";
    out += ",\"children\":[";

    bool first = true;
    const auto &children = item->GetChildren();
    for(size_t index = 0; index < children.size(); ++index) {
        auto *child = static_cast<tTJSNI_MenuItem *>(children.at(index));
        if(!tTJSNI_BaseMenuItem::IsLiveInstance(child))
            continue;
        ttstr childCaption;
        child->GetCaption(childCaption);
        if(childCaption.IsEmpty() || childCaption == TJS_W("+") ||
           childCaption == TJS_W("-"))
            continue;
        if(!first)
            out.push_back(',');
        first = false;
        AppendMenuItemJson(out, child, path.empty()
                                          ? std::to_string(index)
                                          : path + "/" + std::to_string(index));
    }
    out += "]}";
}

std::vector<int> ParsePath(const char *itemPathUtf8) {
    std::vector<int> result;
    if(!itemPathUtf8 || !*itemPathUtf8)
        return result;

    std::stringstream stream(itemPathUtf8);
    std::string segment;
    while(std::getline(stream, segment, '/')) {
        if(segment.empty()) {
            result.clear();
            return result;
        }
        char *end = nullptr;
        long value = std::strtol(segment.c_str(), &end, 10);
        if(*end != '\0' || value < 0) {
            result.clear();
            return result;
        }
        result.push_back(static_cast<int>(value));
    }
    return result;
}

tTJSNI_MenuItem *FindMenuItem(tTJSNI_MenuItem *root,
                              const std::vector<int> &path) {
    tTJSNI_MenuItem *current = root;
    for(int index : path) {
        if(!current || index >= static_cast<int>(current->GetChildren().size()))
            return nullptr;
        current = static_cast<tTJSNI_MenuItem *>(current->GetChildren().at(index));
        if(!tTJSNI_BaseMenuItem::IsLiveInstance(current))
            return nullptr;
    }
    return current;
}

void RunOnCocosThread(const std::function<void()> &task) {
    auto *director = cocos2d::Director::getInstance();
    auto *scheduler = director ? director->getScheduler() : nullptr;
    if(scheduler) {
        scheduler->performFunctionInCocosThread(task);
        return;
    }
    Application->PostUserMessage(task);
}

std::string BuildMainMenuJson() {
    tTJSNI_MenuItem *root = GetActiveRootMenu();
    if(!root)
        return "[]";

    std::string json = "[";
    bool first = true;
    const auto &children = root->GetChildren();
    for(size_t index = 0; index < children.size(); ++index) {
        auto *child = static_cast<tTJSNI_MenuItem *>(children.at(index));
        if(!tTJSNI_BaseMenuItem::IsLiveInstance(child))
            continue;
        ttstr caption;
        child->GetCaption(caption);
        if(caption.IsEmpty() || caption == TJS_W("+") || caption == TJS_W("-"))
            continue;
        if(!first)
            json.push_back(',');
        first = false;
        AppendMenuItemJson(json, child, std::to_string(index));
    }
    json.push_back(']');
    return json;
}

std::string BuildMainMenuJsonOnCocosThread() {
    auto *director = cocos2d::Director::getInstance();
    auto *scheduler = director ? director->getScheduler() : nullptr;
    if(!scheduler)
        return BuildMainMenuJson();

    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();
    scheduler->performFunctionInCocosThread([promise]() {
        promise->set_value(BuildMainMenuJson());
    });
    if(future.wait_for(std::chrono::milliseconds(750)) != std::future_status::ready)
        return "[]";
    return future.get();
}

} // namespace

bool TVPShowFlutterGameMainMenu() {
#if defined(__ANDROID__)
    cocos2d::JniMethodInfo methodInfo;
    if(!cocos2d::JniHelper::getStaticMethodInfo(
           methodInfo, "org/github/krkr2/MainActivity",
           "showFlutterGameMainMenu", "()Z")) {
        return false;
    }
    jboolean shown = methodInfo.env->CallStaticBooleanMethod(
        methodInfo.classID, methodInfo.methodID);
    methodInfo.env->DeleteLocalRef(methodInfo.classID);
    return shown == JNI_TRUE;
#endif
    return false;
}

extern "C" const char *KR2LauncherGetMainMenuJson() {
    LastMenuJson = BuildMainMenuJson();
    return LastMenuJson.c_str();
}

extern "C" int KR2LauncherActivateMenuItem(const char *itemPathUtf8) {
    const std::string path = itemPathUtf8 ? itemPathUtf8 : "";
    if(path.empty())
        return -1;

    RunOnCocosThread([path]() {
        tTJSNI_MenuItem *root = GetActiveRootMenu();
        if(!root)
            return;
        tTJSNI_MenuItem *item = FindMenuItem(root, ParsePath(path.c_str()));
        if(item)
            item->OnClick();
    });
    return 0;
}

extern "C" int KR2LauncherLaunchGame(const char *gamePathUtf8) {
    if(!gamePathUtf8 || !*gamePathUtf8)
        return -1;

    auto *scene = TVPMainScene::GetInstance();
    if(!scene)
        return -2;

    return scene->startupFrom(gamePathUtf8) ? 0 : -3;
}

extern "C" int KR2LauncherPerformOverlayAction(const char *actionNameUtf8) {
    if(!actionNameUtf8 || !*actionNameUtf8)
        return -1;

    auto *scene = TVPMainScene::GetInstance();
    if(!scene)
        return -2;

    if(IsOverlayAction(actionNameUtf8, "window-manager")) {
        RunOnCocosThread([]() {
            if(auto *scene = TVPMainScene::GetInstance())
                scene->showWindowManagerOverlay(true);
        });
        return 0;
    }

    if(IsOverlayAction(actionNameUtf8, "mouse-mode")) {
        const bool nextMode = !scene->isVirtualMouseMode();
        RunOnCocosThread([]() {
            if(auto *scene = TVPMainScene::GetInstance())
                scene->toggleVirtualMouseCursor();
        });
        return nextMode ? 1 : 0;
    }

    if(IsOverlayAction(actionNameUtf8, "keyboard")) {
        RunOnCocosThread([]() {
            auto *director = cocos2d::Director::getInstance();
            auto *view = director ? director->getOpenGLView() : nullptr;
            if(!view)
                return;
            cocos2d::Size screenSize = view->getFrameSize();
#if CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
            TVPShowIME(0, 0, screenSize.width, screenSize.height);
#else
            if(auto *scene = TVPMainScene::GetInstance())
                scene->attachWithIME();
#endif
        });
        return 0;
    }

    if(IsOverlayAction(actionNameUtf8, "exit")) {
        Application->PostUserMessage([]() {
            if(auto *window = TVPGetActiveWindow())
                window->Close();
        });
        return 0;
    }

    return -3;
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT jstring JNICALL
Java_org_github_krkr2_MainActivity_nativeGetMainMenuJson(JNIEnv *env,
                                                         jobject /*thiz*/) {
    const std::string json = BuildMainMenuJsonOnCocosThread();
    return env->NewStringUTF(json.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_org_github_krkr2_MainActivity_nativeActivateMenuItem(JNIEnv *env,
                                                          jobject /*thiz*/,
                                                          jstring path) {
    const char *chars = path ? env->GetStringUTFChars(path, nullptr) : nullptr;
    int result = KR2LauncherActivateMenuItem(chars ? chars : "");
    if(chars)
        env->ReleaseStringUTFChars(path, chars);
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_org_github_krkr2_MainActivity_nativePerformOverlayAction(JNIEnv *env,
                                                              jobject /*thiz*/,
                                                              jstring action) {
    const char *chars = action ? env->GetStringUTFChars(action, nullptr) : nullptr;
    int result = KR2LauncherPerformOverlayAction(chars ? chars : "");
    if(chars)
        env->ReleaseStringUTFChars(action, chars);
    return result;
}
#endif
