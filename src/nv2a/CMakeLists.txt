# NV2A GPU Emulation Library
# Extracted from xemu (https://github.com/xemu-project/xemu)
#
# Register handlers: PMC, PBUS, PTIMER, PFB, PCRTC, PRAMDAC, PFIFO, PGRAPH
# MMIO interception via VEH (Vectored Exception Handling)
# Push buffer parsing and command dispatch

set(NV2A_SOURCES
    nv2a_core.c
    nv2a_pb_replay.c
    nv2a_mmio_hook.c
    nv2a_pb_test.c
)

if(XBOXRECOMP_GRAPHICS_BACKEND STREQUAL "d3d11")
    list(APPEND NV2A_SOURCES nv2a_pgraph_d3d11.c)
else()
    list(APPEND NV2A_SOURCES nv2a_pgraph_null.c)
endif()

add_library(xbox_nv2a STATIC ${NV2A_SOURCES})

target_include_directories(xbox_nv2a PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${XBOXRECOMP_PLATFORM_COMPAT_INCLUDE}
)

if(MSVC)
    target_compile_options(xbox_nv2a PRIVATE
        /W3
        /wd4244
        /wd4146
        /wd4267
    )
    target_compile_definitions(xbox_nv2a PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(xbox_nv2a PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)
endif()

if(WIN32)
    target_link_libraries(xbox_nv2a PRIVATE kernel32)
else()
    target_link_libraries(xbox_nv2a PRIVATE pthread)
endif()
