#pragma once

#include <string>

struct TVPRuntimeHostLaunchRequest {
    std::string gamePath;
    std::string preferenceRoot;
};

struct TVPRuntimeHostFrameMetrics {
    int frameWidth = 0;
    int frameHeight = 0;
    int sceneWidth = 0;
    int sceneHeight = 0;
    float scale = 1.0f;
};

enum class TVPRuntimeHostLaunchStatus {
    Started,
    EmptyPath,
    NoHost,
    RejectedByHost,
};

class iTVPRuntimeHost {
public:
    virtual ~iTVPRuntimeHost() = default;

    virtual const char *GetHostName() const = 0;
    virtual bool StartGame(const TVPRuntimeHostLaunchRequest &request) = 0;
    virtual void RunFrame(float deltaSeconds);
    virtual TVPRuntimeHostFrameMetrics GetFrameMetrics();
};

void TVPSetRuntimeHost(iTVPRuntimeHost *host);
iTVPRuntimeHost *TVPGetRuntimeHost();
const char *TVPGetRuntimeHostName();
TVPRuntimeHostLaunchStatus
TVPStartGameOnRuntimeHostDetailed(const TVPRuntimeHostLaunchRequest &request,
                                  const char *source = nullptr);
bool TVPStartGameOnRuntimeHost(const TVPRuntimeHostLaunchRequest &request,
                               const char *source = nullptr);
