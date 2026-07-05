#pragma once

#include "RuntimeHost.h"

bool TVPRuntimeConfigureGameLaunch(
    const TVPRuntimeHostLaunchRequest &request);
bool TVPRuntimeStartApplication(const std::string &path);
void TVPRuntimeRunApplicationFrame(float deltaSeconds);
void TVPRuntimeRecycleFrameResources();
