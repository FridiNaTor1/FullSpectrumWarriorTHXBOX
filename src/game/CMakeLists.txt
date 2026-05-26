file(GLOB_RECURSE GAME_SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

set(RECOMP_SUPPORT_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp/recomp_manual.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gen/recomp_dispatch.c"
)

list(REMOVE_ITEM GAME_SOURCE_FILES ${RECOMP_SUPPORT_FILES})

add_executable(fsw_th_recomp
    ${RECOMP_SUPPORT_FILES}
    ${GAME_SOURCE_FILES}
)

target_include_directories(fsw_th_recomp PRIVATE
    "${CMAKE_SOURCE_DIR}/templates/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gen"
)

target_link_libraries(fsw_th_recomp PRIVATE xboxrecomp m)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fsw_th_recomp PRIVATE
        -Wno-unused-variable
        -Wno-unused-label
        -Wno-unused-but-set-variable
        -Wno-maybe-uninitialized
    )
    set_source_files_properties(${GAME_SOURCE_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/recomp/gen/recomp_dispatch.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif()

set_target_properties(fsw_th_recomp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
