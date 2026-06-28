#include "RuntimeHost.h"

#include "NativeLog.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {

std::atomic<iTVPRuntimeHost *> gRuntimeHost{ nullptr };
std::atomic_uint64_t gRuntimeHostLaunchSequence{ 0 };

const char *SafeString(const char *value, const char *fallback) {
    return value && *value ? value : fallback;
}

const char *RuntimeHostLaunchStatusName(TVPRuntimeHostLaunchStatus status) {
    switch(status) {
        case TVPRuntimeHostLaunchStatus::Started:
            return "started";
        case TVPRuntimeHostLaunchStatus::EmptyPath:
            return "rejected-empty-path";
        case TVPRuntimeHostLaunchStatus::NoHost:
            return "rejected-no-host";
        case TVPRuntimeHostLaunchStatus::RejectedByHost:
        default:
            return "rejected-by-host";
    }
}

void LogRuntimeHostLaunch(uint64_t sequence, const char *source,
                          const char *hostName, const std::string &path,
                          const std::string &preferenceRoot,
                          TVPRuntimeHostLaunchStatus status) {
    char message[768];
    std::snprintf(message, sizeof(message),
                  "launch #%llu source=%s host=%s result=%s path=%s "
                  "preferenceRoot=%s",
                  static_cast<unsigned long long>(sequence),
                  SafeString(source, "unknown"),
                  SafeString(hostName, "none"),
                  RuntimeHostLaunchStatusName(status),
                  path.c_str(),
                  preferenceRoot.empty() ? "(auto)" : preferenceRoot.c_str());
    TVPNativeLogInfo("runtime-host", message);
}

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

TVPRuntimeHostLaunchStatus
TVPStartGameOnRuntimeHostDetailed(const TVPRuntimeHostLaunchRequest &request,
                                  const char *source) {
    const uint64_t sequence = ++gRuntimeHostLaunchSequence;
    iTVPRuntimeHost *host = TVPGetRuntimeHost();
    const char *hostName = host ? host->GetHostName() : nullptr;
    if(request.gamePath.empty()) {
        const TVPRuntimeHostLaunchStatus status =
            TVPRuntimeHostLaunchStatus::EmptyPath;
        LogRuntimeHostLaunch(sequence, source, hostName, request.gamePath,
                             request.preferenceRoot, status);
        return status;
    }
    if(!host) {
        const TVPRuntimeHostLaunchStatus status =
            TVPRuntimeHostLaunchStatus::NoHost;
        LogRuntimeHostLaunch(sequence, source, hostName, request.gamePath,
                             request.preferenceRoot, status);
        return status;
    }

    const bool started = host->StartGame(request);
    const TVPRuntimeHostLaunchStatus status =
        started ? TVPRuntimeHostLaunchStatus::Started
                : TVPRuntimeHostLaunchStatus::RejectedByHost;
    LogRuntimeHostLaunch(sequence, source, hostName, request.gamePath,
                         request.preferenceRoot, status);
    return status;
}

bool TVPStartGameOnRuntimeHost(const TVPRuntimeHostLaunchRequest &request,
                               const char *source) {
    return TVPStartGameOnRuntimeHostDetailed(request, source) ==
        TVPRuntimeHostLaunchStatus::Started;
}
