cmake_minimum_required(VERSION 3.19)
project(core_environ_module LANGUAGES CXX)

# Cocos host removed; always use SDL/Flutter runtime path.
set(KRKR2_ENABLE_COCOS_HOST OFF CACHE BOOL
    "Legacy Cocos runtime host (removed; always OFF)" FORCE)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(ENVIRON_PATH ${CMAKE_CURRENT_LIST_DIR})
set(ENVIRON_SOURCE_FILES
    ${ENVIRON_PATH}/Application.cpp
    ${ENVIRON_PATH}/DetectCPU.cpp
    ${ENVIRON_PATH}/DumpSend.cpp
    ${ENVIRON_PATH}/NativeLog.cpp
    ${ENVIRON_PATH}/XP3ArchiveRepack.cpp
    ${ENVIRON_PATH}/runtime/RuntimeHost.cpp
    ${ENVIRON_PATH}/runtime/RuntimeEngineLoop.cpp
    ${ENVIRON_PATH}/runtime/RuntimePresenter.cpp
    ${ENVIRON_PATH}/runtime/RuntimeRenderManager.cpp

    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android/AndroidUtils.cpp>
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android/KrkrJniHelper.cpp>
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/linux/Platform.cpp>
    
    $<$<BOOL:${LINUX}>:${ENVIRON_PATH}/linux/Platform.cpp>

    $<$<BOOL:${WINDOWS}>:${ENVIRON_PATH}/win32/Platform.cpp>

    $<$<BOOL:${MACOS}>:${ENVIRON_PATH}/apple/macos/platform.mm>

    ${ENVIRON_PATH}/ConfigManager/GlobalConfigManager.cpp
    ${ENVIRON_PATH}/ConfigManager/ConfigFileIO.cpp
    ${ENVIRON_PATH}/ConfigManager/IndividualConfigManager.cpp
    ${ENVIRON_PATH}/ConfigManager/LocaleConfigManager.cpp
    ${ENVIRON_PATH}/ConfigManager/PreferenceDefaults.cpp
    ${ENVIRON_PATH}/sdl/SDLGameManager.cpp
    ${ENVIRON_PATH}/sdl/SDLAndroidFlutterPresenter.cpp
    ${ENVIRON_PATH}/sdl/SDLRuntimePresenter.cpp
    ${ENVIRON_PATH}/sdl/SDLRuntimeWindowHost.cpp
    ${ENVIRON_PATH}/sdl/SDLUIManager.cpp
    ${ENVIRON_PATH}/sdl/tvpsdl.cpp

    # Flutter overlay menu bridge (no Cocos UI forms).
    ${ENVIRON_PATH}/ui/FlutterGameMenuBridge.cpp

    ${ENVIRON_PATH}/win32/SystemControl.cpp
)

set(ENVIRON_HEADERS_DIR
    ${ENVIRON_PATH}/
    ${ENVIRON_PATH}/win32
    $<$<BOOL:${ANDROID}>:${ENVIRON_PATH}/android>
    ${ENVIRON_PATH}/sdl
    ${CMAKE_CURRENT_LIST_DIR}/../render/sdlgpu
)

add_library(${PROJECT_NAME} STATIC ${ENVIRON_SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${ENVIRON_HEADERS_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC tjs2 PRIVATE core_base_module core_visual_module core_sound_module core_plugin_module core_movie_module core_utils_module core_render_sdlgpu_module)
target_compile_definitions(${PROJECT_NAME} PUBLIC
    KRKR2_ENABLE_COCOS_HOST=0
)

find_package(7zip CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3
        tinyxml2::tinyxml2

        7zip::7zip
        minizip
)
