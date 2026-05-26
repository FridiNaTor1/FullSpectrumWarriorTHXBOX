cmake_minimum_required(VERSION 3.20)
project(fsw_th_xbox_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(UNIX AND NOT APPLE)
    set(XBOXRECOMP_DEFAULT_GRAPHICS_BACKEND "vulkan")
    set(XBOXRECOMP_PLATFORM_COMPAT_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32_compat")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    find_package(Vulkan REQUIRED)
else()
    set(XBOXRECOMP_DEFAULT_GRAPHICS_BACKEND "d3d11")
    set(XBOXRECOMP_PLATFORM_COMPAT_INCLUDE "")
endif()

set(XBOXRECOMP_GRAPHICS_BACKEND "${XBOXRECOMP_DEFAULT_GRAPHICS_BACKEND}" CACHE STRING
    "Graphics backend to build: d3d11, vulkan, or null")
set_property(CACHE XBOXRECOMP_GRAPHICS_BACKEND PROPERTY STRINGS d3d11 vulkan null)

add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_subdirectory(src/kernel)
add_subdirectory(src/d3d)
add_subdirectory(src/audio)
add_subdirectory(src/apu)
add_subdirectory(src/nv2a)
add_subdirectory(src/input)

add_library(xboxrecomp INTERFACE)
target_link_libraries(xboxrecomp INTERFACE
    xbox_kernel
    xbox_d3d8
    xbox_dsound
    xbox_input
    xbox_apu
    xbox_nv2a
)

target_include_directories(xboxrecomp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_subdirectory(src/game)
