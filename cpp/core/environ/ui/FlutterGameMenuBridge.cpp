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
#include <functional>
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
    LastMenuJson = "[]";
    tTJSNI_MenuItem *root = GetActiveRootMenu();
    if(!root)
        return LastMenuJson.c_str();

    LastMenuJson = "[";
    bool first = true;
    const auto &children = root->GetChildren();
    for(size_t index = 0; index < children.size(); ++index) {
        auto *child = static_cast<tTJSNI_MenuItem *>(children.at(index));
        if(!tTJSNI_BaseMenuItem::IsLiveInstance(child))
            continue;
        if(!first)
            LastMenuJson.push_back(',');
        first = false;
        AppendMenuItemJson(LastMenuJson, child, std::to_string(index));
    }
    LastMenuJson.push_back(']');
    return LastMenuJson.c_str();
}

extern "C" int KR2LauncherActivateMenuItem(const char *itemPathUtf8) {
    tTJSNI_MenuItem *root = GetActiveRootMenu();
    if(!root)
        return -1;

    tTJSNI_MenuItem *item = FindMenuItem(root, ParsePath(itemPathUtf8));
    if(!item)
        return -2;

    item->OnClick();
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
