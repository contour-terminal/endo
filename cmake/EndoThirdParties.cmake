# EndoThirdParties.cmake
#
# This file handles all third-party dependencies for Endo Shell.
# It first tries to find packages on the system, and falls back to CPM if not found.
# When ENABLE_STATIC_LINKING is ON, all dependencies are built from source via CPM
# to ensure static libraries are available.

set(CPM_VERSION "0.40.8")
set(CPM_HASH_SUM "78ba32abdf798bc616bab7c73aac32a17bbd7b06ad9e26a6add69de8f3ae4791")

if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_VERSION}.cmake")
endif()

file(
  DOWNLOAD
  https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake
  ${CPM_DOWNLOAD_LOCATION}
  EXPECTED_HASH SHA256=${CPM_HASH_SUM}
)
include(${CPM_DOWNLOAD_LOCATION})

# Helper macro for displaying dependency status
macro(EndoThirdPartiesSummary2)
    message(STATUS "==============================================================================")
    message(STATUS "    Endo Shell ThirdParties")
    message(STATUS "------------------------------------------------------------------------------")
    if(NOT EMSCRIPTEN)
        message(STATUS "Catch2              ${THIRDPARTY_BUILTIN_Catch2}")
    endif()
    message(STATUS "GSL                 ${THIRDPARTY_BUILTIN_GSL}")
    if(NOT EMSCRIPTEN)
        message(STATUS "yaml-cpp            ${THIRDPARTY_BUILTIN_yaml_cpp}")
    endif()
    message(STATUS "libunicode          ${THIRDPARTY_BUILTIN_libunicode}")
    message(STATUS "boxed-cpp           ${THIRDPARTY_BUILTIN_boxed_cpp}")
    message(STATUS "reflection-cpp      ${THIRDPARTY_BUILTIN_reflection_cpp}")
    if(NOT EMSCRIPTEN)
        message(STATUS "nlohmann_json       ${THIRDPARTY_BUILTIN_nlohmann_json}")
        message(STATUS "CURL                ${THIRDPARTY_BUILTIN_CURL}")
        if(ENABLE_STATIC_LINKING)
            message(STATUS "mbedTLS             ${THIRDPARTY_BUILTIN_mbedtls}")
        endif()
    endif()
    message(STATUS "------------------------------------------------------------------------------")
endmacro()

# ==============================================================================
# Catch2 v3 - Unit testing framework
# ==============================================================================
if(NOT EMSCRIPTEN)
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
    # The global -D_UNICODE definition causes Catch2WithMain to define wmain()
    # instead of main(), leading to unresolved symbol errors on MSVC.
    # Undefine _UNICODE for Catch2WithMain so it provides the standard main().
    if(WIN32 AND TARGET Catch2WithMain)
        target_compile_options(Catch2WithMain PRIVATE /U_UNICODE)
    endif()
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
if(NOT EMSCRIPTEN)
    if(NOT ENABLE_STATIC_LINKING)
        find_package(yaml-cpp QUIET)
    endif()
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
                "BUILD_SHARED_LIBS OFF"
            EXCLUDE_FROM_ALL YES
        )
        set(THIRDPARTY_BUILTIN_yaml_cpp "CPM (v0.9.0, static)")
    endif()
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
        GIT_TAG v1.4.3
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_boxed_cpp "CPM (v1.4.3)")
endif()

# ==============================================================================
# libunicode - Unicode library
# ==============================================================================
if(NOT ENABLE_STATIC_LINKING)
    find_package(libunicode QUIET)
endif()
if(TARGET unicode::unicode OR TARGET unicode::core)
    set(THIRDPARTY_BUILTIN_libunicode "system package")
else()
    CPMAddPackage(
        NAME libunicode
        GITHUB_REPOSITORY contour-terminal/libunicode
        GIT_TAG v0.8.0
        OPTIONS
            "LIBUNICODE_TESTING OFF"
            "LIBUNICODE_BENCHMARK OFF"
            "LIBUNICODE_TOOLS OFF"
            "LIBUNICODE_EXAMPLES OFF"
            "BUILD_SHARED_LIBS OFF"
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_libunicode "CPM (v0.8.0, static)")
endif()

# ==============================================================================
# nlohmann/json - JSON library for LSP protocol
# ==============================================================================
if(NOT EMSCRIPTEN)
    find_package(nlohmann_json 3.11.0 QUIET)
    if(TARGET nlohmann_json::nlohmann_json)
        set(THIRDPARTY_BUILTIN_nlohmann_json "system package")
    else()
        CPMAddPackage(
            NAME nlohmann_json
            VERSION 3.11.3
            GITHUB_REPOSITORY nlohmann/json
            OPTIONS
                "JSON_BuildTests OFF"
            EXCLUDE_FROM_ALL YES
        )
        set(THIRDPARTY_BUILTIN_nlohmann_json "CPM (v3.11.3)")
    endif()
endif()

# ==============================================================================
# Static-only dependency: mbedTLS (TLS backend for CURL, avoids system OpenSSL)
# ==============================================================================
if(NOT EMSCRIPTEN)
    if(ENABLE_STATIC_LINKING)
        CPMAddPackage(
            NAME mbedtls
            VERSION 3.6.2
            GITHUB_REPOSITORY Mbed-TLS/mbedtls
            GIT_TAG mbedtls-3.6.2
            EXCLUDE_FROM_ALL YES
            OPTIONS
                "ENABLE_TESTING OFF"
                "ENABLE_PROGRAMS OFF"
                "USE_STATIC_MBEDTLS_LIBRARY ON"
                "USE_SHARED_MBEDTLS_LIBRARY OFF"
                "MBEDTLS_FATAL_WARNINGS OFF"
        )
        # Set variables that CURL's FindMbedTLS.cmake expects, since the CPM-built
        # targets aren't discoverable via find_package().
        if(mbedtls_ADDED)
            set(MBEDTLS_FOUND TRUE CACHE BOOL "" FORCE)
            set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
            set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
            set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
            set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
        endif()
        set(THIRDPARTY_BUILTIN_mbedtls "CPM (v3.6.2, static)")
    endif()
endif()

# ==============================================================================
# CURL - HTTP client library
# ==============================================================================
if(NOT EMSCRIPTEN)
    if(ENABLE_STATIC_LINKING)
        # Build CURL from source with mbedTLS backend, no optional deps.
        # Note: USE_LIBIDN2 has no CURL_ prefix in CURL 8.9.x.
        CPMAddPackage(
            NAME CURL
            GIT_TAG curl-8_9_1
            GITHUB_REPOSITORY curl/curl
            OPTIONS
                "BUILD_TESTING OFF"
                "BUILD_CURL_EXE OFF"
                "BUILD_SHARED_LIBS OFF"
                "CURL_USE_MBEDTLS ON"
                "CURL_USE_OPENSSL OFF"
                "CURL_USE_LIBSSH2 OFF"
                "USE_LIBIDN2 OFF"
                "CURL_USE_LIBPSL OFF"
                "CURL_DISABLE_LDAP ON"
                "CURL_DISABLE_LDAPS ON"
                "CURL_DISABLE_NTLM ON"
                "CURL_ZLIB OFF"
                "CURL_ENABLE_EXPORT_TARGET OFF"
                "HTTP_ONLY ON"
            EXCLUDE_FROM_ALL YES
        )
        # Ensure mbedTLS static libraries are linked transitively through CURL
        if(TARGET libcurl_static)
            target_link_libraries(libcurl_static INTERFACE mbedtls mbedx509 mbedcrypto)
        endif()
        set(THIRDPARTY_BUILTIN_CURL "CPM (v8.9.1, static, mbedTLS)")
    else()
        find_package(CURL QUIET)
        if(TARGET CURL::libcurl)
            set(THIRDPARTY_BUILTIN_CURL "system package")
        else()
            CPMAddPackage(
                NAME CURL
                GIT_TAG curl-8_9_1
                GITHUB_REPOSITORY curl/curl
                OPTIONS
                    "BUILD_TESTING OFF"
                    "BUILD_CURL_EXE OFF"
                    "BUILD_SHARED_LIBS OFF"
                EXCLUDE_FROM_ALL YES
            )
            set(THIRDPARTY_BUILTIN_CURL "CPM (v8.9.1)")
        endif()
    endif()
endif()

# ==============================================================================
# reflection-cpp - Required by crispy::core
# ==============================================================================
CPMAddPackage(
    NAME reflection-cpp
    GITHUB_REPOSITORY contour-terminal/reflection-cpp
    GIT_TAG v0.4.0
    EXCLUDE_FROM_ALL YES
)
set(THIRDPARTY_BUILTIN_reflection_cpp "CPM (v0.4.0)")
