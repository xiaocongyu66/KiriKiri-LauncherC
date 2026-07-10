cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>   # 对于 MSVC 兼容性
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(ANDROID)
    set(VCPKG_TARGET_ANDROID ON)
    set(ENV{ANDROID_NDK_HOME} "$ENV{ANDROID_NDK}")
    add_compile_options(-Wno-inconsistent-missing-override)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/hook/Dobby)
    add_library(dobby STATIC IMPORTED)
    set_target_properties(dobby PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/hook/Dobby/libraries/${ANDROID_ABI}/libdobby.a)
    # Legacy TJS2 code (tTJSVariantString::GetLength / operator const char* /
    # FixLength / ...) guards against being called with `this == nullptr`
    # via `if(!this) return ...;`. Modern Clang treats that as UB and
    # deletes the check under -O2, so calls on a null ttstr crash inside
    # the engine (e.g. Storages.getPlacedPath(nil) → SIGSEGV at
    # GetLength+0 reading offset 0x3c). Tell the compiler to keep those
    # null checks.
    add_compile_options(
        -fno-delete-null-pointer-checks
        -Wno-tautological-undefined-compare
        -Wno-undefined-bool-conversion
    )
    include(cmake/vcpkg_android.cmake)
else()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

set(APP_NAME krkr2)

project(${APP_NAME})

option(ENABLE_TESTS "enable tests execute build(exclude android)" ON)
option(BUILD_TOOLS "build tools execute build(exclude android ios)" ON)
set(KRKR2_ENABLE_COCOS_HOST_DEFAULT ON)
if(ANDROID)
    set(KRKR2_ENABLE_COCOS_HOST_DEFAULT OFF)
endif()
option(KRKR2_ENABLE_COCOS_HOST "Build the legacy Cocos runtime host" ${KRKR2_ENABLE_COCOS_HOST_DEFAULT})

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "/MP" "/utf-8")
    add_link_options("/ignore:4099" "/INCREMENTAL" "/DEBUG:FASTLINK")
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ANDROID)
    add_library(${PROJECT_NAME} SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/cpp/krkr2_android.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/cpp/LauncherNativeScanner.cpp
    )
    find_package(unofficial-breakpad CONFIG REQUIRED)
    find_package(SDL3 CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        unofficial::breakpad::libbreakpad_client
        SDL3::SDL3
        log
        android
        EGL
        GLESv2
        GLESv3
        GLESv1_CM
        vulkan
        OpenSLES
    )
elseif(LINUX)
    add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/main.cpp)
elseif(WINDOWS)
    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/game.rc
    )
elseif(IOS)
#        list(APPEND GAME_HEADER
#                proj.ios_mac/ios/AppController.h
#                proj.ios_mac/ios/RootViewController.h
#        )
#        set(APP_UI_RES
#                proj.ios_mac/ios/LaunchScreen.storyboard
#                proj.ios_mac/ios/LaunchScreenBackground.png
#                proj.ios_mac/ios/Images.xcassets
#        )
#        list(APPEND GAME_SOURCE
#                proj.ios_mac/ios/main.m
#                proj.ios_mac/ios/AppController.mm
#                proj.ios_mac/ios/RootViewController.mm
#                proj.ios_mac/ios/Prefix.pch
#                ${APP_UI_RES}
#        )
elseif(MACOS)
    set(APP_UI_RES
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Icon.icns
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Info.plist
    )
    add_executable(${PROJECT_NAME}
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Prefix.pch
            ${APP_UI_RES}
    )
endif()

if(KRKR2_ENABLE_COCOS_HOST)
    target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2CORE_PATH}/environ/cocos2d)
endif()

find_package(TBB CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC TBB::tbb)

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})

# For Android/Linux keep plugin archives under whole-archive so file-scope NCB
# static constructors are preserved. Static-library dependencies are not folded
# into krkr2plugin itself, so plugin sub-archives with their own registrars must
# be listed here too; otherwise modules such as layerExDraw.dll are never
# registered even though krkr2plugin links the target.
if(ANDROID OR LINUX)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        -Wl,--whole-archive
        krkr2plugin
        motionplayer
        psbfile
        layerExDraw
        psdfile
        fstat
        kagparserex
        json
        -Wl,--no-whole-archive
        $<$<BOOL:${ANDROID}>:dobby>
    )
elseif(APPLE)
    # Apple ld: -force_load takes an absolute path; let CMake resolve
    # the static library target locations at generate time.
    target_link_libraries(${PROJECT_NAME} PUBLIC
        "-Wl,-force_load,$<TARGET_FILE:krkr2plugin>"
        "-Wl,-force_load,$<TARGET_FILE:krkr2core>"
        "-Wl,-force_load,$<TARGET_FILE:motionplayer>"
        "-Wl,-force_load,$<TARGET_FILE:psbfile>"
        "-Wl,-force_load,$<TARGET_FILE:layerExDraw>"
        "-Wl,-force_load,$<TARGET_FILE:psdfile>"
        "-Wl,-force_load,$<TARGET_FILE:fstat>"
        "-Wl,-force_load,$<TARGET_FILE:json>"
    )
elseif(MSVC)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        krkr2plugin krkr2core
    )
    target_link_options(${PROJECT_NAME} PUBLIC
        /WHOLEARCHIVE:krkr2plugin
        /WHOLEARCHIVE:krkr2core
        /WHOLEARCHIVE:motionplayer
        /WHOLEARCHIVE:psbfile
        /WHOLEARCHIVE:layerExDraw
        /WHOLEARCHIVE:psdfile
        /WHOLEARCHIVE:fstat
        /WHOLEARCHIVE:json
    )
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC
        krkr2plugin krkr2core
    )
endif()

## resources
if(NOT ANDROID AND KRKR2_ENABLE_COCOS_HOST)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CocosBuildHelpers.cmake")

    set(GAME_RES_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/ui/cocos-studio")

    if(WINDOWS OR APPLE)
        cocos_mark_multi_resources(common_res_files RES_TO "Resources" FOLDERS ${GAME_RES_FOLDER})
    endif()

    # mark app resources
    setup_cocos_app_config(${APP_NAME})

    if(APPLE)
        set_target_properties(${APP_NAME} PROPERTIES RESOURCE "${APP_UI_RES}")
        target_sources(${APP_NAME} PRIVATE ${common_res_files})
        if(MACOSX)
            set_target_properties(${APP_NAME} PROPERTIES
                    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/apple/macos/Info.plist"
            )
        elseif(IOS)
#            cocos_pak_xcode(${APP_NAME} INFO_PLIST "iOSBundleInfo.plist.in")
#            set_xcode_property(${APP_NAME} ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
#            set_xcode_property(${APP_NAME} DEVELOPMENT_TEAM "")
#            set_xcode_property(${APP_NAME} CODE_SIGN_IDENTITY "iPhone Developer")
        endif()
    elseif(WINDOWS)
        target_sources(${PROJECT_NAME} PRIVATE ${common_res_files})
        cocos_copy_target_dll(${APP_NAME})
    endif()

    if(LINUX OR WINDOWS)
        cocos_get_resource_path(APP_RES_DIR ${APP_NAME})
        cocos_copy_target_res(${APP_NAME} LINK_TO ${APP_RES_DIR} FOLDERS ${GAME_RES_FOLDER})
    endif()
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID))
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_TOOLS AND NOT (IOS OR ANDROID))
    add_subdirectory(tools)
endif()
