## Adds static linking support to a target.

option(ENABLE_STATIC_LINKING "Build a statically linked executable" OFF)

include(CheckCXXSourceCompiles)

## @brief Checks that the system has the required static libraries installed.
##
## Verifies that a trivial C++ program can be linked with -static. If this fails,
## it means the system is missing static C/C++ runtime libraries (e.g. glibc-static,
## libstdc++-static on Fedora, or libc6-dev on Ubuntu/Debian).
function(check_static_system_requirements)
    if(NOT ENABLE_STATIC_LINKING)
        return()
    endif()

    set(CMAKE_REQUIRED_FLAGS "-static")
    check_cxx_source_compiles("int main() { return 0; }" STATIC_LINKING_WORKS)
    unset(CMAKE_REQUIRED_FLAGS)

    if(NOT STATIC_LINKING_WORKS)
        message(FATAL_ERROR
            "Static linking failed: system is missing static C/C++ runtime libraries.\n"
            "  Fedora/RHEL: sudo dnf install glibc-static libstdc++-static\n"
            "  Ubuntu/Debian: sudo apt install libc6-dev\n"
            "  Arch: Static libs are included in base-devel\n"
        )
    endif()

    message(STATUS "Static linking: system requirements satisfied")
endfunction()

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
