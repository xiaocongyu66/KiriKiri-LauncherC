option(KIRIKIRI_ENABLE_BGFX "Build optional bgfx renderer runtime" OFF)
set(KIRIKIRI_BGFX_ROOT "" CACHE PATH "Path to bgfx source tree")
set(KIRIKIRI_BIMG_ROOT "" CACHE PATH "Path to bimg source tree")
set(KIRIKIRI_BX_ROOT "" CACHE PATH "Path to bx source tree")

function(kiriki_configure_bgfx target_name)
    if(NOT KIRIKIRI_ENABLE_BGFX)
        return()
    endif()

    if(NOT KIRIKIRI_BGFX_ROOT OR NOT KIRIKIRI_BIMG_ROOT OR NOT KIRIKIRI_BX_ROOT)
        message(FATAL_ERROR "KIRIKIRI_ENABLE_BGFX requires KIRIKIRI_BGFX_ROOT, KIRIKIRI_BIMG_ROOT and KIRIKIRI_BX_ROOT")
    endif()

    foreach(path_var KIRIKIRI_BGFX_ROOT KIRIKIRI_BIMG_ROOT KIRIKIRI_BX_ROOT)
        if(NOT EXISTS "${${path_var}}")
            message(FATAL_ERROR "${path_var} does not exist: ${${path_var}}")
        endif()
    endforeach()

    add_library(kiriki_bx STATIC "${KIRIKIRI_BX_ROOT}/src/amalgamated.cpp")
    target_include_directories(kiriki_bx PUBLIC "${KIRIKIRI_BX_ROOT}/include" "${KIRIKIRI_BX_ROOT}/3rdparty")
    target_compile_definitions(kiriki_bx PUBLIC BX_CONFIG_DEBUG=0)

    add_library(kiriki_bimg STATIC
        "${KIRIKIRI_BIMG_ROOT}/src/image.cpp"
        "${KIRIKIRI_BIMG_ROOT}/src/image_decode.cpp"
        "${KIRIKIRI_BIMG_ROOT}/src/image_encode.cpp"
        "${KIRIKIRI_BIMG_ROOT}/src/image_gnf.cpp"
    )
    target_include_directories(kiriki_bimg PUBLIC
        "${KIRIKIRI_BIMG_ROOT}/include"
        "${KIRIKIRI_BIMG_ROOT}/3rdparty"
        "${KIRIKIRI_BX_ROOT}/include"
        "${KIRIKIRI_BX_ROOT}/3rdparty"
    )
    target_link_libraries(kiriki_bimg PUBLIC kiriki_bx)

    add_library(kiriki_bgfx STATIC "${KIRIKIRI_BGFX_ROOT}/src/amalgamated.cpp")
    target_include_directories(kiriki_bgfx PUBLIC
        "${KIRIKIRI_BGFX_ROOT}/include"
        "${KIRIKIRI_BGFX_ROOT}/3rdparty"
        "${KIRIKIRI_BGFX_ROOT}/3rdparty/khronos"
        "${KIRIKIRI_BGFX_ROOT}/src"
        "${KIRIKIRI_BIMG_ROOT}/include"
        "${KIRIKIRI_BIMG_ROOT}/3rdparty"
        "${KIRIKIRI_BX_ROOT}/include"
        "${KIRIKIRI_BX_ROOT}/3rdparty"
    )
    target_compile_definitions(kiriki_bgfx PUBLIC
        BGFX_CONFIG_RENDERER_VULKAN=1
        BGFX_CONFIG_RENDERER_OPENGL=1
        BGFX_CONFIG_RENDERER_OPENGLES=1
        BGFX_CONFIG_RENDERER_NOOP=1
        BGFX_CONFIG_MULTITHREADED=1
    )
    target_link_libraries(kiriki_bgfx PUBLIC kiriki_bimg kiriki_bx)
    if(ANDROID)
        target_link_libraries(kiriki_bgfx PUBLIC android log vulkan EGL GLESv3)
    elseif(APPLE)
        find_library(METAL_LIBRARY Metal)
        find_library(QUARTZCORE_LIBRARY QuartzCore)
        target_link_libraries(kiriki_bgfx PUBLIC ${METAL_LIBRARY} ${QUARTZCORE_LIBRARY})
    elseif(UNIX)
        target_link_libraries(kiriki_bgfx PUBLIC dl pthread)
    endif()

    target_compile_definitions(${target_name} PRIVATE KIRIKIRI_HAS_BGFX=1)
    target_link_libraries(${target_name} PRIVATE kiriki_bgfx)
endfunction()
