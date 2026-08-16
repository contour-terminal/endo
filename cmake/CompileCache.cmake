# Compiler-cache launcher selection.
#
# Three launchers are supported, in preference order:
#
#   1. fastcache-cc — the launcher from the fastcached project. Its entries are
#      portable across checkout paths, so a CI runner and a developer working
#      from different directories share cache hits. It must already be installed
#      and on PATH. It is configured purely through the environment and caches
#      nothing unless FASTCACHE_ADDR / FASTCACHE_SRCROOT / FASTCACHE_BUILDTREE
#      are all set; the address defaults to fastcached's own port,
#      127.0.0.1:6674, and the two roots are injected here via `cmake -E env`,
#      because CMake already knows them. Selecting it is conditional on a daemon
#      actually answering there — see the probe below.
#   2. sccache — the usual third-party launcher, used when fastcache-cc is
#      unavailable or unconfigured. Supports shared (Redis/S3/...) caches.
#   3. ccache — the classic local cache, used when neither of the above applies.
#
# Launchers are wired in as compiler launchers, so CPM-/FetchContent-fetched
# dependencies get cached too. If a launcher is already set (e.g. via the
# command line or a preset), it is left untouched.
#
# To disable entirely: -DUSE_COMPILER_CACHE=OFF.

option(USE_COMPILER_CACHE
       "Use a compiler-cache launcher when one is available (fastcache-cc when a daemon answers, else sccache, else ccache) [default: ON]"
       ON)

# Respect a launcher provided externally (command line, preset, toolchain).
# Check both C and CXX: a toolchain may set only one of them, and we must not
# override either (nor silently set the other alongside it).
if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER OR DEFINED CMAKE_C_COMPILER_LAUNCHER)
    message(STATUS "[cache] Compiler launcher already set externally "
                   "(C='${CMAKE_C_COMPILER_LAUNCHER}', CXX='${CMAKE_CXX_COMPILER_LAUNCHER}'); leaving it untouched.")
    # A build tree configured before this module existed carries the launcher of
    # the day in its cache, and would keep it forever without a word about why
    # the selection below never runs.
    if(DEFINED CACHE{CMAKE_CXX_COMPILER_LAUNCHER} OR DEFINED CACHE{CMAKE_C_COMPILER_LAUNCHER})
        message(STATUS "[cache] That value comes from the CMake cache (a -D, a preset, or an older configure); "
                       "reconfigure with --fresh to let this module choose instead.")
    endif()
    return()
endif()

find_program(FASTCACHE_CC fastcache-cc DOC "fastcache-cc tool path; needs a fastcached daemon to be used")
find_program(SCCACHE sccache DOC "sccache tool path")
find_program(CCACHE ccache DOC "ccache tool path")

# An address in the environment wins; otherwise fastcached's own port, which a
# stock daemon (and the service the installers register) listens on. An empty
# -DFASTCACHE_ADDR= opts out of fastcache-cc entirely.
set(_fc_addr_initial "$ENV{FASTCACHE_ADDR}")
if(_fc_addr_initial STREQUAL "")
    set(_fc_addr_initial "127.0.0.1:6674")
endif()
set(FASTCACHE_ADDR "${_fc_addr_initial}" CACHE STRING
    "host:port of the fastcached compile-cache daemon, 127.0.0.1:6674 by default (empty disables the fastcache-cc launcher)")

# How fastcache-cc is configured, in one place: the probe below must test the
# very environment the build will use, or it would vouch for a configuration
# nothing else runs.
set(_fc_fastcache_env
    "FASTCACHE_ADDR=${FASTCACHE_ADDR}"
    "FASTCACHE_SRCROOT=${CMAKE_SOURCE_DIR}"
    "FASTCACHE_BUILDTREE=${CMAKE_BINARY_DIR}")

# Ask fastcache-cc itself whether the cache works, by compiling one tiny
# translation unit through it with FASTCACHE_VERBOSE=1 and requiring a reported
# cache outcome. A launcher that cannot reach its daemon still compiles fine —
# it just runs the real compiler — so nothing but an end-to-end exchange tells
# "the cache works" apart from "every TU will silently pay a failed connect,
# with precompiled headers disabled for nothing and ccache passed over".
#
# The match is positive (HIT/MISS only): should the launcher's diagnostics ever
# be reworded, this reports unusable and the build falls back to the next
# launcher, which is the harmless direction to be wrong in.
#
# @param outVar Set to TRUE when the cache served the probe, FALSE otherwise.
# @param reasonVar Set to a short diagnostic when outVar is FALSE.
function(_fc_probe_fastcache_cc outVar reasonVar)
    set(${outVar} FALSE PARENT_SCOPE)

    set(_dir "${CMAKE_BINARY_DIR}/CMakeFiles/fastcache-probe")
    set(_src "${_dir}/probe.cpp")
    # Both the file and its content are fixed, so the probe itself is a cache
    # hit from the second configure onwards — which exercises FETCH rather than
    # just STORE, and costs less than the first run.
    file(WRITE "${_src}" "int fastcacheProbe() { return 0; }\n")

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(_args /nologo /c "${_src}" "/Fo${_dir}/probe.obj")
    else()
        set(_args -c "${_src}" -o "${_dir}/probe.o")
    endif()

    # NO_STATS keeps the probe out of `fastcache-cc --show-stats`, where it would
    # read as a build that never hits. TIMEOUT_MS bounds a daemon that accepts
    # the connection and then stalls; builds keep the launcher's own default.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                ${_fc_fastcache_env}
                "FASTCACHE_VERBOSE=1"
                "FASTCACHE_NO_STATS=1"
                "FASTCACHE_TIMEOUT_MS=2000"
                "${FASTCACHE_CC}" "${CMAKE_CXX_COMPILER}" ${_args}
        WORKING_DIRECTORY "${_dir}"
        RESULT_VARIABLE _rc
        OUTPUT_QUIET
        ERROR_VARIABLE _err)

    if(NOT _rc EQUAL 0)
        set(${reasonVar} "probe compile failed (${_rc})" PARENT_SCOPE)
    elseif(_err MATCHES "fastcache-cc: (HIT|MISS) key=")
        set(${outVar} TRUE PARENT_SCOPE)
        set(${reasonVar} "" PARENT_SCOPE)
    elseif(_err MATCHES "fastcache-cc: cache unavailable \\(([^)]*)\\)")
        set(${reasonVar} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${reasonVar} "no cache outcome reported" PARENT_SCOPE)
    endif()
endfunction()

# Candidate table, most-preferred first. Each row <id> is described by:
#   _fc_cache_<id>_label     human-readable name for the status message
#   _fc_cache_<id>_program   the found program (empty when not installed)
#   _fc_cache_<id>_requires  extra condition; the row is skipped when falsy
#   _fc_cache_<id>_env       NAME=VALUE pairs to inject around the invocation
#   _fc_cache_<id>_check     function deciding usability at configure time
#                            (empty when being installed is enough); called as
#                            <fn>(<outVar> <reasonVar>) and only for a row that
#                            already passed program and requires
#   _fc_cache_<id>_detail    extra words for the status message (empty for none)
# Supporting a fourth launcher is adding an id here plus its six variables.
set(_fc_cache_candidates fastcache_cc sccache ccache)

set(_fc_cache_fastcache_cc_label "fastcache-cc")
set(_fc_cache_fastcache_cc_program "${FASTCACHE_CC}")
set(_fc_cache_fastcache_cc_requires "${FASTCACHE_ADDR}")
set(_fc_cache_fastcache_cc_env ${_fc_fastcache_env})
set(_fc_cache_fastcache_cc_check _fc_probe_fastcache_cc)
set(_fc_cache_fastcache_cc_detail "at ${FASTCACHE_ADDR}")

set(_fc_cache_sccache_label "sccache")
set(_fc_cache_sccache_program "${SCCACHE}")
set(_fc_cache_sccache_requires ON)
set(_fc_cache_sccache_env "")
set(_fc_cache_sccache_check "")
set(_fc_cache_sccache_detail "")

set(_fc_cache_ccache_label "ccache")
set(_fc_cache_ccache_program "${CCACHE}")
set(_fc_cache_ccache_requires ON)
set(_fc_cache_ccache_env "")
set(_fc_cache_ccache_check "")
set(_fc_cache_ccache_detail "")

set(_fc_cache_chosen "")
set(_fc_cache_rejected "")
if(USE_COMPILER_CACHE)
    foreach(_id IN LISTS _fc_cache_candidates)
        if(NOT _fc_cache_${_id}_program OR NOT _fc_cache_${_id}_requires)
            continue()
        endif()
        if(_fc_cache_${_id}_check)
            cmake_language(CALL ${_fc_cache_${_id}_check} _fc_cache_usable _fc_cache_why_not)
            if(NOT _fc_cache_usable)
                # Remember why, so a fall-through to a slower launcher explains
                # itself rather than looking like the faster one was never there.
                list(APPEND _fc_cache_rejected "${_fc_cache_${_id}_label}: ${_fc_cache_why_not}")
                continue()
            endif()
        endif()
        set(_fc_cache_chosen "${_id}")
        break()
    endforeach()
endif()

# Say why a preferred launcher was passed over, whatever the outcome: falling
# through in silence looks exactly like it never being installed.
foreach(_rejection IN LISTS _fc_cache_rejected)
    message(STATUS "[cache] Not using ${_rejection}")
endforeach()

if(_fc_cache_chosen)
    set(_fc_cache_label "${_fc_cache_${_fc_cache_chosen}_label}")
    set(_fc_cache_program "${_fc_cache_${_fc_cache_chosen}_program}")

    # `cmake -E env NAME=VALUE ... <program>` is the only way to attach
    # environment to a compiler launcher; rows without env invoke the program
    # directly so they pay no extra process.
    if(_fc_cache_${_fc_cache_chosen}_env)
        set(_fc_cache_launcher
            "${CMAKE_COMMAND}" -E env
            ${_fc_cache_${_fc_cache_chosen}_env}
            "${_fc_cache_program}")
    else()
        set(_fc_cache_launcher "${_fc_cache_program}")
    endif()

    set(_fc_cache_detail "${_fc_cache_${_fc_cache_chosen}_detail}")
    if(_fc_cache_detail)
        string(PREPEND _fc_cache_detail " ")
    endif()
    message(STATUS "[cache] Enabling ${_fc_cache_label} (${_fc_cache_program})${_fc_cache_detail} "
                   "for C/C++ compilation")
    set(CMAKE_C_COMPILER_LAUNCHER ${_fc_cache_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_fc_cache_launcher})

    # None of the launchers reproduces anything but the object file on a cache
    # hit, so a precompiled header (a second, separately produced artefact)
    # cannot be served from cache.
    set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)

    # CMake's C++20 module support puts scanning flags on every compile line
    # (-fmodules-ts -fmodule-mapper=<per-object modmap> on GCC). A preprocess-only
    # run with those flags fails, so a launcher that derives its key by
    # preprocessing falls back on *every* translation unit. This project has no
    # module units, so the scan is pure overhead; turn it off while a launcher is
    # in use.
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

    # For the same reason none of them supports /Zi (shared PDB). Force MSVC to
    # embed debug info in .obj files (/Z7) via the modern CMake knob (CMP0141),
    # and also fix up any legacy /Zi already present in FLAGS_DEBUG /
    # FLAGS_RELWITHDEBINFO.
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
else()
    # Define the launchers as empty rather than leaving them unset. Fetched
    # dependencies bring their own cache modules that auto-enable ccache when the
    # launcher is merely *undefined* (libunicode's cmake/EnableCcache.cmake does
    # exactly that), which would quietly re-enable caching for their targets.
    # An empty definition is inert for us and keeps USE_COMPILER_CACHE=OFF honest.
    set(CMAKE_C_COMPILER_LAUNCHER "")
    set(CMAKE_CXX_COMPILER_LAUNCHER "")

    if(NOT USE_COMPILER_CACHE)
        message(STATUS "[cache] Compiler caching disabled by USE_COMPILER_CACHE=OFF")
    elseif(_fc_cache_rejected)
        message(STATUS "[cache] No other compiler-cache launcher found (sccache, ccache); caching disabled "
                       "(start a daemon with `fastcached` to cache through fastcache-cc)")
    else()
        message(STATUS "[cache] No compiler-cache launcher found (fastcache-cc, sccache, ccache); caching disabled")
    endif()
endif()
