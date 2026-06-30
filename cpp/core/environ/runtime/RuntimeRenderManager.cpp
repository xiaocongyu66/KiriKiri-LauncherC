#include "RuntimeRenderManager.h"

#include <mutex>
#include <sstream>

namespace {

std::mutex gRuntimeRenderManagerMutex;
TVPRuntimeRenderManagerSnapshot gRuntimeRenderManagerSnapshot;

const char *FallbackString(const std::string &value, const char *fallback) {
    return value.empty() ? fallback : value.c_str();
}

void AppendModuleSummary(std::ostringstream &stream,
                         const TVPRuntimeRenderModuleInfo &module) {
    if(module.name.empty())
        return;
    stream << " " << module.type << "=" << module.name;
    if(module.active)
        stream << ":active";
    if(module.highPerformance)
        stream << ":fast";
    if(module.cpuCopyFree)
        stream << ":cpu-copy-free";
    if(module.usedBytes > 0) {
        stream << ":used=" << (module.usedBytes >> 20) << "MiB";
    }
    if(module.budgetBytes > 0) {
        stream << ":budget=" << (module.budgetBytes >> 20) << "MiB";
    }
}

} // namespace

void TVPRuntimeUpdateRenderManagerSnapshot(
    const TVPRuntimeRenderManagerSnapshot &snapshot) {
    std::lock_guard<std::mutex> lock(gRuntimeRenderManagerMutex);
    gRuntimeRenderManagerSnapshot = snapshot;
}

TVPRuntimeRenderManagerSnapshot TVPRuntimeGetRenderManagerSnapshot() {
    std::lock_guard<std::mutex> lock(gRuntimeRenderManagerMutex);
    return gRuntimeRenderManagerSnapshot;
}

std::string TVPRuntimeDescribeRenderManager() {
    TVPRuntimeRenderManagerSnapshot snapshot =
        TVPRuntimeGetRenderManagerSnapshot();
    std::ostringstream stream;
    stream << "pipeline=" << FallbackString(snapshot.pipelineName, "unknown");
    if(!snapshot.presenterName.empty())
        stream << " presenter=" << snapshot.presenterName;
    if(!snapshot.graphicsBackend.empty())
        stream << " backend=" << snapshot.graphicsBackend;
    if(snapshot.highPerformancePresenter)
        stream << " presenterFast=1";
    if(snapshot.cpuCopyFreePresenter)
        stream << " cpuCopyFree=1";
    for(const TVPRuntimeRenderModuleInfo &module : snapshot.modules)
        AppendModuleSummary(stream, module);
    return stream.str();
}
