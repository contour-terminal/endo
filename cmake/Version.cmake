# SPDX-License-Identifier: Apache-2.0
# CMake function to extract version triple and full version string from the source code repository.
#
# The following locations are checked in order:
# 1.) /version.txt file
# 2.) Git tags matching pattern v* (e.g., v1.2.34)
# 3.) .git directory with branch and SHA info as fallback
#
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
        # unique, monotonically increasing version. Format: v<maj>.<min>.<patch>-<n>-g<sha>
        # where <n> is the number of commits since the most recent v* tag.
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --long --match "v*" --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_RESULT
        )
        if(GIT_RESULT EQUAL 0 AND
           GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
            set(_ver_major "${CMAKE_MATCH_1}")
            set(_ver_minor "${CMAKE_MATCH_2}")
            set(_ver_patch "${CMAKE_MATCH_3}")
            set(_ver_commits "${CMAKE_MATCH_4}")
            set(_ver_sha "${CMAKE_MATCH_5}")
            set(_ver_dirty "${CMAKE_MATCH_6}")
            if(_ver_commits EQUAL 0 AND _ver_dirty STREQUAL "")
                # Exactly on a release tag.
                set(THE_VERSION "${_ver_major}.${_ver_minor}.${_ver_patch}")
                set(THE_VERSION_STRING "${_ver_major}.${_ver_minor}.${_ver_patch}")
                set(THE_SOURCE "git tag")
                message(STATUS "Successfully retrieved version '${THE_VERSION}' from Git tag.")
            else()
                # Development build N commits past the tag. Fold the commit distance
                # into the patch component so the numeric version is unique and
                # monotonically increasing per commit. This is required so the MSI
                # installer treats each CI build as an upgrade and so per-version
                # install directories are distinct. The human-readable string keeps
                # the short SHA so an installed build maps back to a commit.
                math(EXPR _ver_build "${_ver_patch} + ${_ver_commits}")
                set(THE_VERSION "${_ver_major}.${_ver_minor}.${_ver_build}")
                set(THE_VERSION_STRING
                    "${_ver_major}.${_ver_minor}.${_ver_patch}-${_ver_commits}-g${_ver_sha}${_ver_dirty}")
                set(THE_SOURCE "git describe (development build)")
                message(STATUS "Derived development version '${THE_VERSION}' (${THE_VERSION_STRING}).")
            endif()
        else()
            # No matching v* tag reachable (e.g. shallow clone without tags). Fall
            # back to the total commit count so CI builds remain unique and ordered.
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
                set(THE_VERSION "0.0.${GIT_COMMIT_COUNT}")
                set(THE_VERSION_STRING "0.0.0-${GIT_COMMIT_COUNT}-g${GIT_SHORT_SHA}")
                set(THE_SOURCE "git commit count (no v* tag found)")
                message(STATUS "No v* tag found; derived version '${THE_VERSION}' from commit count.")
            else()
                message(STATUS "Info: No suitable Git tag (e.g., 'v1.2.34') found.")
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
