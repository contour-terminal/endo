# EndoThirdParties.cmake
#
# This file handles all third-party dependencies for Endo Shell.
# It first tries to find packages on the system, and falls back to CPM if not found.
# This removes the need for the scripts/install-deps.sh script.

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.8/CPM.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
  EXPECTED_HASH SHA256=78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# Helper macro for displaying dependency status
macro(EndoThirdPartiesSummary2)
    message(STATUS "==============================================================================")
    message(STATUS "    Endo Shell ThirdParties")
    message(STATUS "------------------------------------------------------------------------------")
    message(STATUS "Catch2              ${THIRDPARTY_BUILTIN_Catch2}")
    message(STATUS "GSL                 ${THIRDPARTY_BUILTIN_GSL}")
    message(STATUS "yaml-cpp            ${THIRDPARTY_BUILTIN_yaml_cpp}")
    message(STATUS "libunicode          ${THIRDPARTY_BUILTIN_libunicode}")
    message(STATUS "boxed-cpp           ${THIRDPARTY_BUILTIN_boxed_cpp}")
    message(STATUS "reflection-cpp      ${THIRDPARTY_BUILTIN_reflection_cpp}")
    message(STATUS "------------------------------------------------------------------------------")
endmacro()

# ==============================================================================
# Catch2 v3 - Unit testing framework
# ==============================================================================
find_package(Catch2 3 QUIET)
if(TARGET Catch2::Catch2)
    set(THIRDPARTY_BUILTIN_Catch2 "system package")
else()
    CPMAddPackage(
        NAME Catch2
        VERSION 3.8.0
        GITHUB_REPOSITORY catchorg/Catch2
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_Catch2 "CPM (v3.8.0)")
endif()

# ==============================================================================
# Microsoft GSL - Guidelines Support Library
# ==============================================================================
if(WIN32)
    find_package(Microsoft.GSL CONFIG QUIET)
else()
    find_package(Microsoft.GSL QUIET)
endif()
if(TARGET Microsoft.GSL::GSL)
    set(THIRDPARTY_BUILTIN_GSL "system package")
else()
    CPMAddPackage(
        NAME GSL
        VERSION 4.1.0
        GITHUB_REPOSITORY microsoft/GSL
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_GSL "CPM (v4.1.0)")
endif()

# ==============================================================================
# yaml-cpp - YAML parser and emitter
# ==============================================================================
find_package(yaml-cpp QUIET)
if(TARGET yaml-cpp::yaml-cpp OR TARGET yaml-cpp)
    set(THIRDPARTY_BUILTIN_yaml_cpp "system package")
else()
    CPMAddPackage(
        NAME yaml-cpp
        GIT_TAG yaml-cpp-0.9.0
        GITHUB_REPOSITORY jbeder/yaml-cpp
        OPTIONS
            "YAML_CPP_BUILD_TESTS OFF"
            "YAML_CPP_BUILD_TOOLS OFF"
            "YAML_CPP_BUILD_CONTRIB OFF"
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_yaml_cpp "CPM (v0.9.0)")
endif()

# ==============================================================================
# boxed-cpp - Type-safe wrapper library
# ==============================================================================
find_package(boxed-cpp QUIET)
if(TARGET boxed-cpp::boxed-cpp)
    set(THIRDPARTY_BUILTIN_boxed_cpp "system package")
else()
    CPMAddPackage(
        NAME boxed-cpp
        GITHUB_REPOSITORY contour-terminal/boxed-cpp
        GIT_TAG master
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_boxed_cpp "CPM (master)")
endif()

# ==============================================================================
# libunicode - Unicode library
# ==============================================================================
find_package(libunicode QUIET)
if(TARGET unicode::unicode OR TARGET unicode::core)
    set(THIRDPARTY_BUILTIN_libunicode "system package")
else()
    CPMAddPackage(
        NAME libunicode
        GITHUB_REPOSITORY contour-terminal/libunicode
        GIT_TAG master
        OPTIONS
            "LIBUNICODE_TESTING OFF"
            "LIBUNICODE_BENCHMARK OFF"
            "LIBUNICODE_TOOLS OFF"
            "LIBUNICODE_EXAMPLES OFF"
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_libunicode "CPM (master)")
endif()

# ==============================================================================
# reflection-cpp - Required by crispy::core
# ==============================================================================
CPMAddPackage(
    NAME reflection-cpp
    GITHUB_REPOSITORY contour-terminal/reflection-cpp
    GIT_TAG master
    EXCLUDE_FROM_ALL YES
)
set(THIRDPARTY_BUILTIN_reflection_cpp "CPM (master)")
