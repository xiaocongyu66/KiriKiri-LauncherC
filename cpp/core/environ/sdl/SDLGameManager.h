#pragma once

#include <functional>
#include <string>

struct TVPSDLRuntimeInfo {
    std::string compiledVersion;
    std::string linkedVersion;
    std::string revision;
    std::string platform;
    std::string videoDriver;
    std::string audioDriver;
    bool eventsReady = false;
    bool videoReady = false;
    bool audioReady = false;
};

struct TVPSDLGameLaunchCallbacks {
    std::function<void()> initializePreferences;
    std::function<bool(const std::string &path, const std::string &gameDir)>
        startupFrom;
    std::function<void()> showFileSelector;
    std::function<void(const std::string &message)> log;
};

enum class TVPSDLGameLaunchResult {
    Started,
    StartupArgHandled,
    FileSelectorShown,
    NoFallback,
};

bool TVPSDLInitializeRuntime();
TVPSDLRuntimeInfo TVPSDLGetRuntimeInfo();
void TVPSDLRecordAndroidInput(const char *eventName, int itemCount,
                              float x = 0.0f, float y = 0.0f, int code = 0,
                              bool state = false);

TVPSDLGameLaunchResult
TVPSDLRunGameLaunch(const TVPSDLGameLaunchCallbacks &callbacks);
