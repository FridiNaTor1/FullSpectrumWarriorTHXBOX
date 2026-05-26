# D3D8 → D3D11 Compatibility Layer
# Implements Xbox D3D8 interface backed by Direct3D 11

if(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "d3d11")
    add_library(xbox_d3d8 STATIC
        d3d8_device.c
        d3d8_resources.c
        d3d8_shaders.c
        d3d8_states.c
        d3d8_combiners.c
        d3d8_vsh.c
    )
elseif(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "vulkan")
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    set(XBOXRECOMP_VULKAN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${XBOXRECOMP_VULKAN_SHADER_DIR}")
    set(XBOXRECOMP_VULKAN_SHADERS
        "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.vert.spv"
        "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.frag.spv"
    )
    add_custom_command(
        OUTPUT "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.vert.spv"
        COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/d3d8_rhw.vert" -o "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.vert.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/d3d8_rhw.vert"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.frag.spv"
        COMMAND "${GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/d3d8_rhw.frag" -o "${XBOXRECOMP_VULKAN_SHADER_DIR}/d3d8_rhw.frag.spv"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/d3d8_rhw.frag"
        VERBATIM
    )
    add_custom_target(xbox_d3d8_vulkan_shaders DEPENDS ${XBOXRECOMP_VULKAN_SHADERS})
    add_library(xbox_d3d8 STATIC
        d3d8_null.c
        d3d8_vulkan_host.c
    )
    add_dependencies(xbox_d3d8 xbox_d3d8_vulkan_shaders)
else()
    add_library(xbox_d3d8 STATIC
        d3d8_null.c
    )
endif()

target_include_directories(xbox_d3d8 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${XBOXRECOMP_PLATFORM_COMPAT_INCLUDE}
)

if(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "d3d11")
    target_link_libraries(xbox_d3d8 PUBLIC d3d11 dxgi dxguid d3dcompiler)
elseif(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "vulkan")
    target_link_libraries(xbox_d3d8 PUBLIC PkgConfig::SDL2 Vulkan::Vulkan)
    target_compile_definitions(xbox_d3d8 PUBLIC
        XBOXRECOMP_VULKAN_GRAPHICS=1
        XBOXRECOMP_VK_SHADER_DIR="${XBOXRECOMP_VULKAN_SHADER_DIR}"
    )
else()
    target_compile_definitions(xbox_d3d8 PUBLIC XBOXRECOMP_NULL_GRAPHICS=1)
endif()

if(MSVC)
    target_compile_options(xbox_d3d8 PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_d3d8 PRIVATE -Wall -Wextra -O2)
endif()
