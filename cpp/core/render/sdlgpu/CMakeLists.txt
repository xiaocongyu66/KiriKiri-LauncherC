cmake_minimum_required(VERSION 3.28)
project(core_render_sdlgpu_module LANGUAGES CXX)

set(SDLGPU_RENDER_PATH ${CMAKE_CURRENT_LIST_DIR})

add_library(${PROJECT_NAME} STATIC
    ${SDLGPU_RENDER_PATH}/SDLGpuBackend.cpp
    ${SDLGPU_RENDER_PATH}/SDLGpuTvpAdapter.cpp
    ${SDLGPU_RENDER_PATH}/SDLGpuTextureCache.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${SDLGPU_RENDER_PATH}
    ${CMAKE_CURRENT_LIST_DIR}/../../visual
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    KRKR2_ENABLE_COCOS_HOST=0
)

find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC SDL3::SDL3 tjs2)
