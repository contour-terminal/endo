# Compiler caching.
#
# Endo supports two compiler caches, selected automatically based on what is
# installed and which options are enabled:
#
#   * sccache — preferred when USE_SCCACHE is ON and the binary is on PATH.
#     Supports local and shared (Redis/S3/...) caches and works well on CI.
#   * ccache  — used as a fallback when ENABLE_CCACHE is ON and sccache is not
#     selected.
#
# Both are wired in as compiler launchers, so CPM-/FetchContent-fetched
# dependencies get cached too. If a launcher is already set (e.g. via the
# command line or a preset), it is left untouched.
#
# To disable: -DUSE_SCCACHE=OFF and/or -DENABLE_CCACHE=OFF.

option(USE_SCCACHE "Use sccache as the compiler launcher when available [default: ON]" ON)
option(ENABLE_CCACHE "Enable compiler cache (ccache) when sccache is unavailable [default: ON]" ON)

# Respect a launcher provided externally (command line, preset, toolchain).
if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "[cache] Compiler launcher already set externally: ${CMAKE_CXX_COMPILER_LAUNCHER}")
    return()
endif()

# Prefer sccache.
if(USE_SCCACHE)
    find_program(SCCACHE sccache DOC "sccache tool path; set USE_SCCACHE=OFF to disable")
    if(SCCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE} CACHE STRING "C compiler launcher")
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE} CACHE STRING "C++ compiler launcher")
        # sccache cannot cache precompiled-header compilations, so disable PCH.
        set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
        # sccache does not support /Zi (shared PDB). Force MSVC to embed debug
        # info in .obj files (/Z7) via the modern CMake knob (CMP0141), and fix
        # up any legacy /Zi already present in the debug/relwithdebinfo flags.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
            set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
            set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
            foreach(_var
                    CMAKE_CXX_FLAGS_DEBUG
                    CMAKE_C_FLAGS_DEBUG
                    CMAKE_CXX_FLAGS_RELWITHDEBINFO
                    CMAKE_C_FLAGS_RELWITHDEBINFO)
                string(REGEX REPLACE "([-/])Zi" "\\1Z7" ${_var} "${${_var}}")
            endforeach()
        endif()
        message(STATUS "[cache] Enabled sccache: ${SCCACHE}")
        return()
    endif()
    message(STATUS "[cache] sccache not found; falling back to ccache.")
endif()

# Fall back to ccache.
if(ENABLE_CCACHE)
    find_program(CCACHE ccache DOC "ccache tool path; set ENABLE_CCACHE=OFF to disable")
    if(CCACHE)
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE} CACHE STRING "C compiler launcher")
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE} CACHE STRING "C++ compiler launcher")
        if(COMMAND cotire)
            # Change ccache config to meet cotire requirements.
            set(ENV{CCACHE_SLOPPINESS} pch_defines,time_macros)
        endif()
        message(STATUS "[cache] Enabled ccache: ${CCACHE}")
        return()
    endif()
    message(STATUS "[cache] ccache not found.")
endif()

message(STATUS "[cache] No compiler cache enabled.")
