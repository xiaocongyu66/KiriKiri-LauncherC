cmake_minimum_required(VERSION 3.19)
project(motionplayer)
add_library(${PROJECT_NAME} STATIC)
target_sources(${PROJECT_NAME} PRIVATE
    main.cpp
    EmotePlayer.cpp
    PlayerCore.cpp
    PlayerFrameProgress.cpp
    PlayerResource.cpp
    PlayerMotionLoad.cpp
    PlayerVariable.cpp
    PlayerTimeline.cpp
    PlayerLayerQuery.cpp
    PlayerDrawDispatch.cpp
    PlayerRender.cpp
    PlayerRenderInternal.cpp
    PlayerRenderItems.cpp
    PlayerRenderExecute.cpp
    PlayerRenderTargets.cpp
    PlayerUpdateLayers.cpp
    PlayerUpdateLayerEval.cpp
    PlayerUpdateGeometry.cpp
    PlayerUpdateChildMotion.cpp
    PlayerUpdateParticles.cpp
    PlayerUpdateAnchor.cpp
    D3DAdaptor.cpp
    ResourceManager.cpp
    SourceCache.cpp
    RuntimeSupport.cpp
    NodeTree.cpp
    MotionNodeBridge.cpp
    MotionTraceWeb.cpp
    PrivateMotionGLL.cpp
    SeparateLayerAdaptor.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)

# motionplayer was ported from kirikiroid2-web which uses spdlog/fmt heavily
# (see Player.h, EmotePlayer.h, RuntimeSupport.h). krkr2plugin links
# spdlog/fmt PRIVATE so we have to re-find and link them here too —
# otherwise the upstream <spdlog/spdlog.h> / <spdlog/fmt/fmt.h> includes
# won't resolve when this library compiles its sources.
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog fmt::fmt)
