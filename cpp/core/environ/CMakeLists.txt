cmake_minimum_required(VERSION 3.19)
project(core_environ_module LANGUAGES CXX)

set(KRKR2_ENABLE_COCOS_HOST_DEFAULT ON)
if(ANDROID)
    set(KRKR2_ENABLE_COCOS_HOST_DEFAULT OFF)
endif()
option(KRKR2_ENABLE_COCOS_HOST "Build the legacy Cocos runtime host" ${KRKR2_ENABLE_COCOS_HOST_DEFAULT})

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
    $<$<NOT:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>>:${ENVIRON_PATH}/sdl/SDLRuntimeWindowHost.cpp>
    ${ENVIRON_PATH}/sdl/SDLUIManager.cpp
    ${ENVIRON_PATH}/sdl/tvpsdl.cpp

    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:${ENVIRON_PATH}/ui/BaseForm.cpp>
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:${ENVIRON_PATH}/ui/ConsoleWindow.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/DebugViewLayerForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/FileSelectorForm.cpp>
    ${ENVIRON_PATH}/ui/FlutterGameMenuBridge.cpp
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/GameMainMenu.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/GlobalPreferenceForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/IndividualPreferenceForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/MainFileSelectorForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/InGameMenuForm.cpp>
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:${ENVIRON_PATH}/ui/MessageBox.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/PreferenceForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/SelectListForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/SimpleMediaFilePlayer.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/TipsHelpForm.cpp>
    $<$<AND:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>,$<NOT:$<BOOL:${ANDROID}>>>:${ENVIRON_PATH}/ui/XP3RepackForm.cpp>
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:${ENVIRON_PATH}/ui/extension/ActionExtension.cpp>
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:${ENVIRON_PATH}/ui/extension/UIExtension.cpp>

    ${ENVIRON_PATH}/win32/SystemControl.cpp
#    ${ENVIRON_PATH}/win32/CompatibleNativeFuncs.cpp
#    ${ENVIRON_PATH}/win32/ConfigFormUnit.cpp
#    ${ENVIRON_PATH}/win32/DetectCPU.cpp
#    ${ENVIRON_PATH}/win32/EmergencyExit.cpp
#    ${ENVIRON_PATH}/win32/HitWindow.cpp
#    ${ENVIRON_PATH}/win32/MouseCursor.cpp
#    ${ENVIRON_PATH}/win32/my_HintWindow.cpp
#    ${ENVIRON_PATH}/win32/Platform.cpp
#    ${ENVIRON_PATH}/win32/TouchPoint.cpp
#    ${ENVIRON_PATH}/win32/TVPWindow.cpp
#    ${ENVIRON_PATH}/win32/VersionFormUnit.cpp
#    ${ENVIRON_PATH}/win32/WindowFormUnit.cpp
#    ${ENVIRON_PATH}/win32/WindowsUtil.cpp
)

if(KRKR2_ENABLE_COCOS_HOST)
    list(APPEND ENVIRON_SOURCE_FILES
        ${ENVIRON_PATH}/cocos2d/CocosRuntimeHost.cpp
        ${ENVIRON_PATH}/cocos2d/AppDelegate.cpp
        ${ENVIRON_PATH}/cocos2d/CCKeyCodeConv.cpp
        ${ENVIRON_PATH}/cocos2d/CustomFileUtils.cpp
        ${ENVIRON_PATH}/cocos2d/MainScene.cpp
        ${ENVIRON_PATH}/cocos2d/YUVSprite.cpp
    )
endif()

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
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:KRKR2_ENABLE_COCOS_HOST=1>
    $<$<NOT:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>>:KRKR2_ENABLE_COCOS_HOST=0>
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

if(KRKR2_ENABLE_COCOS_HOST)
    find_package(cocos2dx CONFIG REQUIRED)
    include("${CMAKE_SOURCE_DIR}/cmake/FixCocos2dxImportedTargets.cmake")
    fix_cocos2dx_android_cpufeatures_include()
    target_link_libraries(${PROJECT_NAME} PRIVATE cocos2dx::cocos2d)
endif()
