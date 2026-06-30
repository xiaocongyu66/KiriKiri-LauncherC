#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TVPRuntimeRenderModuleInfo {
    std::string name;
    std::string type;
    bool active = false;
    bool highPerformance = false;
    bool cpuCopyFree = false;
    uint64_t usedBytes = 0;
    uint64_t budgetBytes = 0;
};

struct TVPRuntimeRenderManagerSnapshot {
    std::string pipelineName;
    std::string presenterName;
    std::string graphicsBackend;
    bool available = false;
    bool highPerformancePresenter = false;
    bool cpuCopyFreePresenter = false;
    unsigned int drawCount = 0;
    uint64_t videoMemoryBytes = 0;
    uint64_t presentedFrames = 0;
    std::vector<TVPRuntimeRenderModuleInfo> modules;
};

void TVPRuntimeUpdateRenderManagerSnapshot(
    const TVPRuntimeRenderManagerSnapshot &snapshot);
TVPRuntimeRenderManagerSnapshot TVPRuntimeGetRenderManagerSnapshot();
std::string TVPRuntimeDescribeRenderManager();
