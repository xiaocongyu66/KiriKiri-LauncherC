cmake_minimum_required(VERSION 3.28)
project(krkr2core)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

set(KRKR2_ENABLE_COCOS_HOST_DEFAULT ON)
if(ANDROID)
    set(KRKR2_ENABLE_COCOS_HOST_DEFAULT OFF)
endif()
option(KRKR2_ENABLE_COCOS_HOST "Build the legacy Cocos runtime host" ${KRKR2_ENABLE_COCOS_HOST_DEFAULT})

add_library(${PROJECT_NAME} INTERFACE)

find_package(mimalloc CONFIG REQUIRED)

add_subdirectory(tjs2)
add_subdirectory(base)
add_subdirectory(environ)
add_subdirectory(extension)
add_subdirectory(plugin)
add_subdirectory(movie)
add_subdirectory(sound)
add_subdirectory(render/sdlgpu)
add_subdirectory(visual)
add_subdirectory(utils)

target_link_libraries(${PROJECT_NAME} INTERFACE
        tjs2
        core_base_module
        core_environ_module
        core_extension_module
        core_plugin_module
        core_movie_module
        core_sound_module
        core_render_sdlgpu_module
        core_visual_module
        core_utils_module
        mimalloc
)

target_include_directories(${PROJECT_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} INTERFACE
    -DTJS_TEXT_OUT_CRLF
    -D__STDC_CONSTANT_MACROS
    -DUSE_UNICODE_FSTRING
    $<$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>:KRKR2_ENABLE_COCOS_HOST=1>
    $<$<NOT:$<BOOL:${KRKR2_ENABLE_COCOS_HOST}>>:KRKR2_ENABLE_COCOS_HOST=0>
)

if(ANDROID)
    target_link_libraries(${PROJECT_NAME} INTERFACE
        log
        android
        EGL
        GLESv2
        GLESv3
        GLESv1_CM
        vulkan
        OpenSLES
    )
endif()

if(KRKR2_ENABLE_COCOS_HOST)
    find_package(cocos2dx CONFIG REQUIRED)
    include("${CMAKE_SOURCE_DIR}/cmake/FixCocos2dxImportedTargets.cmake")
    fix_cocos2dx_android_cpufeatures_include()

    target_link_libraries(${PROJECT_NAME} INTERFACE
        cocos2dx::cocos2d
        $<$<BOOL:${ANDROID}>:$<LINK_LIBRARY:WHOLE_ARCHIVE,cocos2dx::cpp_android_spec>>
    )
endif()
