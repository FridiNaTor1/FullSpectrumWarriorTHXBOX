# Xbox Input Compatibility Layer

if(WIN32)
    add_library(xbox_input STATIC
        xinput_device.c
    )
else()
    add_library(xbox_input STATIC
        xinput_device_sdl.c
    )
endif()

target_include_directories(xbox_input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${XBOXRECOMP_PLATFORM_COMPAT_INCLUDE}
)

if(WIN32)
    target_link_libraries(xbox_input PUBLIC xinput)
else()
    target_link_libraries(xbox_input PUBLIC PkgConfig::SDL2)
    target_compile_definitions(xbox_input PUBLIC XBOXRECOMP_SDL_INPUT=1)
endif()

if(MSVC)
    target_compile_options(xbox_input PRIVATE /W4 /O2)
else()
    target_compile_options(xbox_input PRIVATE -Wall -Wextra -O2)
endif()
