cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Version)

# ENDO_VERSION is the record PREFIX, so the fields below are ENDO_VERSION_NUMERIC
# (what project()/CPack/MSI use), ENDO_VERSION_QUAD (the four-field form the
# binary's OS-level metadata reports) and ENDO_VERSION_HUMAN. See
# ENDO_VERSION_RECORD_FIELDS in cmake/VersionParse.cmake, and docs/releases.md for
# why the packaged and embedded versions differ.
endo_get_version_information(ENDO_VERSION)

project(endo VERSION "${ENDO_VERSION_NUMERIC}" LANGUAGES CXX)

include(GNUInstallDirs)
include(ProjectMetadata)


# setting defaults
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On macOS, ensure the SDK sysroot is set so Homebrew Clang (and clang-scan-deps)
# can find system headers like <time.h>.
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE _macos_sdk_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_macos_sdk_path)
        set(CMAKE_OSX_SYSROOT "${_macos_sdk_path}")
    endif()
endif()

# Homebrew Clang compiles against its own libc++ headers but links against the
# system libc++ by default, causing undefined symbols for newer APIs (e.g.
# __hash_memory).  Point the linker at Homebrew's libc++ instead.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    get_filename_component(_llvm_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_bin_dir}/.." ABSOLUTE)
    set(_llvm_libcxx "${_llvm_prefix}/lib/c++")
    if(EXISTS "${_llvm_libcxx}")
        # Use -nostdlib++ to prevent the Clang driver from implicitly linking
        # the system's libc++/libc++abi.  Then explicitly link Homebrew's
        # versions so that compile-time headers and runtime exception handling
        # use the same library (avoiding ABI mismatches in catch blocks).
        # Also link Homebrew's libunwind so that stack unwinding uses the same
        # runtime as the exception handling (required for catch blocks to work
        # when exceptions cross translation-unit boundaries).
        set(_llvm_libunwind "${_llvm_prefix}/lib/unwind")
        add_link_options(
            "-nostdlib++"
            "-L${_llvm_libcxx}"
            "-Wl,-rpath,${_llvm_libcxx}"
            "-lc++"
            "-lc++abi"
        )
        if(EXISTS "${_llvm_libunwind}")
            add_link_options(
                "-L${_llvm_libunwind}"
                "-Wl,-rpath,${_llvm_libunwind}"
                "-lunwind"
            )
            message(STATUS "Using Homebrew libunwind: ${_llvm_libunwind}")
        endif()
        message(STATUS "Using Homebrew libc++: ${_llvm_libcxx}")
    endif()

    # Prevent CMake from adding -isystem <SDK>/usr/include, which places the
    # macOS SDK's C headers (math.h, string.h, …) ahead of libc++'s own C
    # wrapper headers (cmath, cstring, …).  The SDK's math.h defines signbit,
    # isfinite, isnan as macros that break libc++'s std::signbit usage.
    # The compiler still finds SDK headers via -isysroot.
    if(CMAKE_OSX_SYSROOT)
        list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "${CMAKE_OSX_SYSROOT}/usr/include")
        list(APPEND CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "${CMAKE_OSX_SYSROOT}/usr/include")
    endif()

    # Disable libc++ availability annotations (e.g. std::from_chars for
    # floating-point being marked macOS 26.0+).  These annotations reflect the
    # *system* libc++ version, but we link against Homebrew's libc++ which
    # already provides these symbols.
    add_compile_definitions(_LIBCPP_DISABLE_AVAILABILITY)
endif()

if(WIN32)
    # Prevent <windows.h> from defining min/max macros that conflict with std::min/std::max
    add_definitions(-DNOMINMAX)
    # Exclude rarely-used Windows headers (prevents winsock.h/winsock2.h conflicts)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    # Target Windows 10+
    add_definitions(-D_WIN32_WINNT=0x0A00)
    # Use Unicode APIs
    add_definitions(-DUNICODE -D_UNICODE)
endif()


# The AI agent features are experimental, so they are opt-in: a default configure
# builds no LLM providers, no agent mode and no MCP client, and skips the llama.cpp
# dependency entirely. Official packages are built with this switched ON.
option(ENDO_ENABLE_AGENT
       "Build with experimental AI agent support (LLM providers, agent mode, MCP) [default: OFF]" OFF)
option(ENDO_ENABLE_WASM "Build the WebAssembly compilation backend (requires binaryen) [default: ON]" ON)

if(NOT DEFINED ENDO_TRACE_VM)
    option(ENDO_TRACE_VM "Enables debug output" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_VM ON)
    endif()
endif()

if(NOT DEFINED ENDO_TRACE_PARSER)
    option(ENDO_TRACE_PARSER "Enables debug output for Parser" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_PARSER ON)
    endif()
endif()


if(NOT DEFINED ENDO_TRACE_LEXER)
    option(ENDO_TRACE_LEXER "Enables debug output for Lexer" OFF)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        set(ENDO_TRACE_LEXER ON)
    endif()
endif()


# ENDO_TESTING must be defined before include(EndoThirdParties): the Catch2 fetch
# there is guarded by it, so the variable has to exist by the time it runs.
if(EMSCRIPTEN)
    set(ENDO_TESTING OFF)
else()
    option(ENDO_TESTING "Build unit tests [default: ON]" ON)
endif()

if(ENDO_TESTING)
    enable_testing()
    # Unit test for the git-describe version parser (pure CMake, no git needed).
    add_test(NAME version-parse
             COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/TestVersionParse.cmake")
endif()

# Set up the compiler-cache launcher before EndoThirdParties, so the sources CPM
# and FetchContent pull in get cached too. CompileCache.cmake prefers fastcache-cc
# when a cache answers (127.0.0.1:6674 unless FASTCACHE_ADDR says otherwise -- a
# stock fastcached, or a fastcache-compile-node's --listen-node), else ccache;
# sccache is only ever picked with -DALLOW_SCCACHE_FALLBACK=ON. It silently
# no-ops when none is usable or USE_COMPILER_CACHE=OFF.
include(CompileCache)
include(StaticLinking)
include(EndoThirdParties)
include(PedanticCompiler)
include(Sanitizers)
include(ClangTidy)

# core-cpp instruments none of its targets as a subproject: a parent that sanitizes its own code
# and not core-cpp's would mix instrumented and uninstrumented code, which is what makes TSan
# report races that are not there. So endo applies its own sanitizers to every core-cpp target.
get_property(_endo_core_cpp_targets GLOBAL PROPERTY CORE_CPP_TARGETS)
foreach(_endo_core_cpp_target IN LISTS _endo_core_cpp_targets)
    enable_sanitizers(${_endo_core_cpp_target})
endforeach()

# OS-level version metadata for the shipped executable: one module per mechanism,
# each exporting an enable_*(target) that is a no-op off its own platform, and
# each doing its platform's one-time setup (enabling the RC language, probing the
# linker) at include time. They are applied to endo-bin in
# src/shell/CMakeLists.txt alongside enable_sanitizers()/enable_clang_tidy().
include(WindowsVersionResource)
include(MacOSInfoPlist)
include(ElfPackageMetadata)

# Desktop integration on freedesktop platforms (icon theme, desktop entry), in the
# same shape: enable_freedesktop_integration(target), applied to endo-bin in
# src/shell/CMakeLists.txt.
include(FreedesktopIntegration)

if(NOT EMSCRIPTEN)
    check_static_system_requirements()
endif()

# WebAssembly compilation backend: requires the system binaryen library.
# Disabled under Emscripten (no cross-compiled binaryen) and under static
# linking (binaryen ships as a shared library only).
set(ENDO_HAS_WASM OFF)
if(ENDO_ENABLE_WASM AND NOT EMSCRIPTEN AND NOT ENABLE_STATIC_LINKING)
    find_package(Binaryen)
    if(Binaryen_FOUND)
        set(ENDO_HAS_WASM ON)
    endif()
endif()

if((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
endif()

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

# Enables STL container checker if not building a release.
if(NOT EMSCRIPTEN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_GLIBCXX_ASSERTIONS)
endif()

option(ENDO_BUILTIN_CRARHDUMP "Enable built-in crashdump reporter, say NO to use system wide crashdump collection like coredumpctl [default: ON]" ON)

# find_package imported targets are scoped to the directory that created them and its
# subdirectories, so Threads is found here, above add_subdirectory(src), where every
# subdirectory can see the target.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# No executable ctest starts may stop at a Windows dialog: every executable target gets the startup
# suppression and every test the environment that switches it on in a product binary. Deferred, so the
# walk sees every target and test the tree declares below. See cmake/WindowsDialogs.cmake.
include(WindowsDialogs)
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL endo_install_windows_dialog_suppression)

add_subdirectory(src)

# The proof that the suppression reaches an executable whose main() never asks for it: a canary that
# asserts, aborts, passes an invalid parameter and faults must EXIT each time, and a product-linked copy
# must keep its dialogs unless the test environment names the variable.
if(ENDO_TESTING AND WIN32)
    add_executable(endo-windows-dialog-canary src/testing/WindowsDialogCanary.cpp)
    add_executable(endo-windows-dialog-product-canary src/testing/WindowsDialogCanary.cpp)
    list(APPEND EndoWindowsDialogProductExecutables
        "endo-windows-dialog-product-canary|stands in for a product in windows-dialog-canary, which watches that variant both ways")
    add_test(NAME windows-dialog-canary
             COMMAND ${CMAKE_COMMAND}
                 -DCANARY=$<TARGET_FILE:endo-windows-dialog-canary>
                 -DPRODUCT_CANARY=$<TARGET_FILE:endo-windows-dialog-product-canary>
                 "-DCTEST=${CMAKE_CTEST_COMMAND}"
                 "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
                 -P "${CMAKE_SOURCE_DIR}/cmake/tests/TestWindowsDialogCanary.cmake")
    set_tests_properties(windows-dialog-canary PROPERTIES
        TIMEOUT 180
        FAIL_REGULAR_EXPRESSION "CMake Error")
endif()
EndoThirdPartiesSummary2()
message(STATUS "==============================================================================")
message(STATUS "          build flags")
message(STATUS "------------------------------------------------------------------------------")
message(STATUS "endo trace vm       ${ENDO_TRACE_VM}")
message(STATUS "endo trace parser   ${ENDO_TRACE_PARSER}")
message(STATUS "endo trace lexer    ${ENDO_TRACE_LEXER}")
message(STATUS "Testing             ${ENDO_TESTING}")
message(STATUS "Static linking      ${ENABLE_STATIC_LINKING}")
message(STATUS "WASM backend        ${ENDO_HAS_WASM}")
message(STATUS "Agent support       ${ENDO_ENABLE_AGENT}")
message(STATUS "Built in crashdump  ${ENDO_BUILTIN_CRARHDUMP}")
if(NOT EMSCRIPTEN)
    message(STATUS "Address Sanitizer   ${ENABLE_ASAN}")
    message(STATUS "UB Sanitizer        ${ENABLE_UBSAN}")
    message(STATUS "Thread Sanitizer    ${ENABLE_TSAN}")
endif()
message(STATUS "------------------------------------------------------------------------------")

include(Packaging)
