# SPDX-License-Identifier: Apache-2.0
# CMake function to extract version triple and full version string from the source code repository.
#
# The following locations are checked in order:
# 1.) /version.txt file
# 2.) Git tags matching pattern v* (e.g., v1.2.34)
# 3.) .git directory with branch and SHA info as fallback
#
include("${CMAKE_CURRENT_LIST_DIR}/VersionParse.cmake")

function(GetVersionInformation VersionTripleVar VersionStringVar)
    find_package(Git QUIET)

    if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
        # 1.) /version.txt file
        file(READ "${CMAKE_SOURCE_DIR}/version.txt" version_text)
        string(STRIP "${version_text}" version_text)
        string(REGEX MATCH "^v?([0-9]*\\.[0-9]+\\.[0-9]+).*$" _ ${version_text})
        set(THE_VERSION ${CMAKE_MATCH_1})
        set(THE_VERSION_STRING "${version_text}")
        set(THE_SOURCE "${CMAKE_SOURCE_DIR}/version.txt")
    elseif(GIT_FOUND)
        # Use a long describe so untagged (development / CI) builds still get a
        # version that increases per commit within a release line. Format:
        # v<maj>.<min>.<patch>[-<pre>]-<n>-g<sha>[-dirty], where <n> is the number
        # of commits since the most recent v* tag. Releases use the clean tag
        # version (what users install); the folded development version is only for
        # CI artifacts, which are not distributed. Parsing lives in
        # endo_parse_git_describe() (cmake/VersionParse.cmake) so it can be tested.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --long --match "v*" --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_RESULT
        )
        set(_parsed_ok FALSE)
        if(GIT_RESULT EQUAL 0)
            endo_parse_git_describe("${GIT_DESCRIBE}"
                _parsed_ok THE_VERSION THE_VERSION_STRING THE_SOURCE)
        endif()
        if(_parsed_ok)
            message(STATUS "Derived version '${THE_VERSION}' (${THE_VERSION_STRING}) from ${THE_SOURCE}.")
        else()
            # No parseable v* tag reachable (e.g. shallow clone without tags, or a
            # tag that is not v<MAJOR>.<MINOR>.<PATCH>-shaped). Fall back to the
            # total commit count so CI builds remain unique and ordered. This is a
            # degraded, non-semver version, so warn loudly rather than degrade
            # silently.
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_COUNT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE GIT_COUNT_RESULT
            )
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_SHORT_SHA
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(GIT_COUNT_RESULT EQUAL 0 AND NOT "${GIT_COMMIT_COUNT}" STREQUAL "")
                endo_clamp_msi_build("${GIT_COMMIT_COUNT}" _count_build)
                set(THE_VERSION "0.0.${_count_build}")
                set(THE_VERSION_STRING "0.0.0-${GIT_COMMIT_COUNT}-g${GIT_SHORT_SHA}")
                set(THE_SOURCE "git commit count (no v* tag found)")
                message(WARNING
                    "Endo: no parseable v* tag reachable; derived degraded version "
                    "'${THE_VERSION}' from commit count. Tag a release "
                    "(v<MAJOR>.<MINOR>.<PATCH>) or fetch tags "
                    "(git fetch --tags / actions/checkout fetch-depth: 0).")
            else()
                message(WARNING
                    "Endo: no suitable Git tag (e.g. 'v1.2.34') found and commit count unavailable.")
            endif()
        endif()
    endif()

    if("${THE_VERSION}" STREQUAL "" OR "${THE_VERSION_STRING}" STREQUAL "")
        set(THE_VERSION "0.0.0")
        set(THE_VERSION_STRING "0.0.0")
        set(THE_SOURCE "default fallback")
        message(STATUS "Warning: No version.txt or matching git tag found. Defaulting to ${THE_VERSION}.")
    endif()

    message(STATUS "[Version] version source: ${THE_SOURCE}")
    message(STATUS "[Version] version triple: ${THE_VERSION}")
    message(STATUS "[Version] version string: ${THE_VERSION_STRING}")

    set(${VersionTripleVar} "${THE_VERSION}" PARENT_SCOPE)
    set(${VersionStringVar} "${THE_VERSION_STRING}" PARENT_SCOPE)
endfunction()
