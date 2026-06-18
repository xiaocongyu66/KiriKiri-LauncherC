#include "RuntimeHost.h"

#include <atomic>

namespace {

std::atomic<iTVPRuntimeHost *> gRuntimeHost{ nullptr };

} // namespace

void iTVPRuntimeHost::RunFrame(float deltaSeconds) { (void)deltaSeconds; }

TVPRuntimeHostFrameMetrics iTVPRuntimeHost::GetFrameMetrics() {
    return {};
}

void TVPSetRuntimeHost(iTVPRuntimeHost *host) {
    gRuntimeHost.store(host, std::memory_order_release);
}

iTVPRuntimeHost *TVPGetRuntimeHost() {
    return gRuntimeHost.load(std::memory_order_acquire);
}

const char *TVPGetRuntimeHostName() {
    iTVPRuntimeHost *host = TVPGetRuntimeHost();
    return host ? host->GetHostName() : "none";
}
