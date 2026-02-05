## Adds static linking support to a target.

option(ENABLE_STATIC_LINKING "Build a statically linked executable" OFF)

## @brief Enables static linking for the given target when ENABLE_STATIC_LINKING is ON.
##
## Only supports Clang and GCC compilers. Emits a fatal error if both static
## linking and sanitizers are enabled, as sanitizers require shared runtime libraries.
function(enable_static_linking target)
    if(NOT ENABLE_STATIC_LINKING)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Static linking is only supported with Clang or GCC.")
    endif()

    if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
        message(FATAL_ERROR "Static linking is incompatible with sanitizers (ASAN, UBSAN, TSAN). "
                            "Sanitizers require shared runtime libraries.")
    endif()

    target_link_options(${target} PRIVATE -static)
endfunction()
