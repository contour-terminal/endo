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
        # Try to get the latest annotated tag (e.g., v1.2.34)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "v*"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_RESULT
        )
        if(GIT_RESULT EQUAL 0 AND NOT "${GIT_TAG}" STREQUAL "")
            string(REGEX REPLACE "^v" "" VERSION_FROM_GIT "${GIT_TAG}")
            message(STATUS "Successfully retrieved version '${VERSION_FROM_GIT}' from Git tag.")
            set(THE_VERSION "${VERSION_FROM_GIT}")
            set(THE_VERSION_STRING "${VERSION_FROM_GIT}")
            set(THE_SOURCE "git tag")
        else()
            message(STATUS "Info: No suitable Git tag (e.g., 'v1.2.34') found.")
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
