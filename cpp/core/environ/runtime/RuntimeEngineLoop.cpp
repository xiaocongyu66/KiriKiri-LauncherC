#include "RuntimeEngineLoop.h"

#include "Application.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "Platform.h"
#include "RenderManager.h"

#include <filesystem>

namespace {

std::string TVPRuntimePathParent(const std::string &path) {
    auto parsedPath = std::filesystem::u8path(path);
#ifdef _WIN32
    return parsedPath.parent_path().u8string();
#else
    return parsedPath.parent_path().string();
#endif
}

} // namespace

bool TVPRuntimeConfigureGameLaunch(
    const TVPRuntimeHostLaunchRequest &request) {
    if(!TVPCheckStartupPath(request.gamePath))
        return false;

    IndividualConfigManager::GetInstance()->UsePreferenceAt(
        request.preferenceRoot.empty()
            ? TVPRuntimePathParent(request.gamePath)
            : request.preferenceRoot);
    return true;
}

bool TVPRuntimeStartApplication(const std::string &path) {
    (void)::Application->StartApplication(path);
    return true;
}

void TVPRuntimeRunApplicationFrame(float deltaSeconds) {
    (void)deltaSeconds;
    ::Application->Run();
}

void TVPRuntimeRecycleFrameResources() {
    iTVPTexture2D::RecycleProcess();
}
