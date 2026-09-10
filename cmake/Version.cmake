# SPDX-License-Identifier: Apache-2.0
# CMake function to derive Endo's version record from the source repository.
#
# The following locations are checked in order:
# 1.) /version.txt file
# 2.) Git tags matching pattern v* (e.g., v1.2.34)
# 3.) .git directory with branch and SHA info as fallback
#
include("${CMAKE_CURRENT_LIST_DIR}/VersionParse.cmake")

## @brief Derive Endo's version record from the best available source.
##
## Every source fills the SAME record (see ENDO_VERSION_RECORD_FIELDS in
## VersionParse.cmake), so no consumer ever has to branch on which one answered
## -- that is the point of having a record rather than loose strings. NUMERIC is
## what project()/CPack/MSI use; the unfolded MAJOR/MINOR/PATCH/COMMITS and QUAD
## are what the OS-level metadata stamped into the binary uses.
##
## Each source shapes its input into something a VersionParse.cmake parser
## understands rather than assembling a record by hand, so the numbering policy
## and the 16-bit clamping are spelled exactly once.
##
## @param out_prefix Variable-name prefix receiving the record, e.g.
##                   `endo_get_version_information(ENDO_VERSION)` yields
##                   ENDO_VERSION_NUMERIC, ENDO_VERSION_QUAD, and so on.
function(endo_get_version_information out_prefix)
    endo_clear_version_record()

    if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
        # 1.) /version.txt file
        file(READ "${CMAKE_SOURCE_DIR}/version.txt" _version_text)
        string(STRIP "${_version_text}" _version_text)
        endo_parse_version_file("${_version_text}" _file)
        if(_file_OK)
            endo_adopt_version_record(_file)
            set(_SOURCE "${CMAKE_SOURCE_DIR}/version.txt")
        else()
            message(WARNING
                "Endo: version.txt ('${_version_text}') is not X.Y.Z-shaped; ignoring it.")
        endif()
    else()
        find_package(Git QUIET)
    endif()

    if(NOT _OK AND GIT_FOUND)
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
        if(GIT_RESULT EQUAL 0)
            endo_parse_git_describe("${GIT_DESCRIBE}" _git)
            if(_git_OK)
                endo_adopt_version_record(_git)
                message(STATUS "Derived version '${_NUMERIC}' (${_HUMAN}) from ${_SOURCE}.")
            endif()
        endif()

        if(NOT _OK)
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
                # Shaped as a describe against an imaginary v0.0.0 tag rather than
                # assembled field by field: the count then lands in the build field
                # of NUMERIC (0.0.<count>, as this path has always produced) and in
                # COMMITS -- never in PATCH -- and it is clamped by the same code
                # as every other derived component.
                endo_parse_git_describe("v0.0.0-${GIT_COMMIT_COUNT}-g${GIT_SHORT_SHA}" _count)
                endo_adopt_version_record(_count)
                set(_SOURCE "git commit count (no v* tag found)")
                message(WARNING
                    "Endo: no parseable v* tag reachable; derived degraded version "
                    "'${_NUMERIC}' from commit count. Tag a release "
                    "(v<MAJOR>.<MINOR>.<PATCH>) or fetch tags "
                    "(git fetch --tags / actions/checkout fetch-depth: 0).")
            else()
                message(WARNING
                    "Endo: no suitable Git tag (e.g. 'v1.2.34') found and commit count unavailable.")
            endif()
        endif()
    endif()

    if(NOT _OK)
        endo_parse_version_file("0.0.0" _default)
        endo_adopt_version_record(_default)
        set(_SOURCE "default fallback")
        message(STATUS "Warning: No version.txt or matching git tag found. Defaulting to ${_NUMERIC}.")
    endif()

    message(STATUS "[Version] version source: ${_SOURCE}")
    message(STATUS "[Version] version triple: ${_NUMERIC}")
    message(STATUS "[Version] version string: ${_HUMAN}")
    message(STATUS "[Version] version quad:   ${_QUAD} (dirty: ${_DIRTY})")

    endo_publish_version_record(${out_prefix})
endfunction()
