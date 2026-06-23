cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Version)
GetVersionInformation(ENDO_VERSION ENDO_VERSION_STRING)

project(endo VERSION "${ENDO_VERSION}" LANGUAGES CXX)

include(GNUInstallDirs)


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


option(ENDO_ENABLE_AGENT "Build with AI agent support (LLM providers, agent mode, MCP)" ON)

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

include(EnableCcache)
include(StaticLinking)
include(EndoThirdParties)
include(PedanticCompiler)
include(Sanitizers)
include(ClangTidy)

if(NOT EMSCRIPTEN)
    check_static_system_requirements()
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

# crispy is vendored from contour — pass through its testing flag
# (ENDO_TESTING is defined earlier, before include(EndoThirdParties).)
set(CONTOUR_TESTING ${ENDO_TESTING})

option(ENDO_BUILTIN_CRARHDUMP "Enable built-in crashdump reporter, say NO to use system wide crashdump collection like coredumpctl [default: ON]" ON)

# Apply Emscripten compatibility patches to vendored sources.
# crispy is symlinked from contour, so we use sed for in-place patching
# (GNU patch does not follow symlinked directories).
if(EMSCRIPTEN)
    set(_BUFOBJ_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/crispy/BufferObject.h")
    if(EXISTS "${_BUFOBJ_PATH}")
        file(REAL_PATH "${_BUFOBJ_PATH}" _BUFOBJ_H)
        file(READ "${_BUFOBJ_H}" _BUFOBJ_CONTENT)
        string(FIND "${_BUFOBJ_CONTENT}" "buffer_object_ptr<T> buffer_object<T>::create(" _PATCH_POS)
        if(NOT _PATCH_POS EQUAL -1)
            string(REPLACE
                "buffer_object_ptr<T> buffer_object<T>::create(size_t capacity, buffer_object_release<T> release)"
                "auto buffer_object<T>::create(size_t capacity, buffer_object_release<T> release) -> buffer_object_ptr<T>"
                _BUFOBJ_CONTENT "${_BUFOBJ_CONTENT}")
            file(WRITE "${_BUFOBJ_H}" "${_BUFOBJ_CONTENT}")
            message(STATUS "Applied Emscripten patch to BufferObject.h (trailing return type)")
        else()
            message(STATUS "BufferObject.h already patched or signature changed")
        endif()
    endif()
endif()

add_subdirectory(src)
EndoThirdPartiesSummary2()
message(STATUS "==============================================================================")
message(STATUS "          build flags")
message(STATUS "------------------------------------------------------------------------------")
message(STATUS "endo trace vm       ${ENDO_TRACE_VM}")
message(STATUS "endo trace parser   ${ENDO_TRACE_PARSER}")
message(STATUS "endo trace lexer    ${ENDO_TRACE_LEXER}")
message(STATUS "Testing             ${ENDO_TESTING}")
message(STATUS "Static linking      ${ENABLE_STATIC_LINKING}")
message(STATUS "Built in crashdump  ${ENDO_BUILTIN_CRARHDUMP}")
if(NOT EMSCRIPTEN)
    message(STATUS "Address Sanitizer   ${ENABLE_ASAN}")
    message(STATUS "UB Sanitizer        ${ENABLE_UBSAN}")
    message(STATUS "Thread Sanitizer    ${ENABLE_TSAN}")
endif()
message(STATUS "------------------------------------------------------------------------------")

include(Packaging)
