# SPDX-License-Identifier: Apache-2.0
#
# Unit test for the version parsers in cmake/VersionParse.cmake.
# Pure CMake, no git required. Run via:
#   cmake -P cmake/tests/TestVersionParse.cmake
#
# Data-driven on two axes. Each table row is "<input>|<expectations>", where
# <expectations> is a space-separated list of <FIELD>=<value> assertions naming
# fields of the version record; a row asserts only the fields it mentions, so a
# new record field means a new token on the rows that care -- not an edit to
# every row, and not a new positional column. The sentinel "FAIL" in place of the
# expectation list means the input must be rejected. And each SUITE is a row too,
# naming the parser under test, so covering a third parser is one more line
# rather than a second copy of the loop.
#
# Values may contain neither a space (the assertion separator) nor a "|" (the
# field separator). No describe string or version component ever does.

# Required: a -P script starts with no policy baseline, and with CMP0007 unset the
# list() commands drop empty elements. The empty-input row below splits to ";FAIL",
# whose first element is empty, so list(GET _parts 1 ...) would read past the end.
cmake_minimum_required(VERSION 3.30 FATAL_ERROR)

include("${CMAKE_CURRENT_LIST_DIR}/../VersionParse.cmake")

# `git describe` inputs.
set(_describe_cases
    # clean release tag
    "v0.1.0-0-gabc1234|OK=TRUE NUMERIC=0.1.0 HUMAN=0.1.0 QUAD=0.1.0.0 MAJOR=0 MINOR=1 PATCH=0 COMMITS=0 SHA=abc1234 DIRTY=FALSE PRERELEASE="
    # the shape this repository actually builds from
    "v0.1.0-312-gfcf09f3e|NUMERIC=0.1.312 QUAD=0.1.0.312 HUMAN=0.1.0-312-gfcf09f3e DIRTY=FALSE"
    # development build: patch + commits folded into NUMERIC, QUAD stays unfolded
    "v1.2.3-5-gabc1234|NUMERIC=1.2.8 QUAD=1.2.3.5 MAJOR=1 MINOR=2 PATCH=3 COMMITS=5"
    # dirty on a tag: folds to patch+0 (documented alias), but DIRTY is reported
    "v1.2.3-0-gabc1234-dirty|NUMERIC=1.2.3 QUAD=1.2.3.0 DIRTY=TRUE"
    # pre-release suffix tolerated: kept out of NUMERIC, reported in PRERELEASE
    "v1.2.3-rc1-5-gabc1234|NUMERIC=1.2.8 QUAD=1.2.3.5 PRERELEASE=-rc1"
    # dotted pre-release, multi-digit components
    "v10.20.30-rc.2-7-gdeadbee|NUMERIC=10.20.37 QUAD=10.20.30.7 PRERELEASE=-rc.2"
    # both 16-bit fields clamp independently -- the folded build AND the commit
    # count that becomes the fourth VERSIONINFO field. The true count survives in
    # HUMAN, which is what makes clamping acceptable.
    "v1.2.0-70000-gabc1234|NUMERIC=1.2.65535 QUAD=1.2.0.65535 COMMITS=65535 HUMAN=1.2.0-70000-gabc1234"
    # zero version
    "v0.0.0-0-g0000000|NUMERIC=0.0.0 QUAD=0.0.0.0"
    "garbage|FAIL"                # not a describe string
    "v1.2-5-gabc1234|FAIL"        # tag missing the patch component
    "|FAIL"                       # empty input
)

# version.txt contents (the Debian / tarball path).
#
# NUMERIC must stay the UNFOLDED tag triple here: the published .deb is versioned
# 0.1.0, and a refactor that "unified" this path with the describe path would move
# it to 0.1.312 without a single other test going red. This table is that test.
set(_version_file_cases
    "v0.1.0-312-gfcf09f3e|OK=TRUE NUMERIC=0.1.0 QUAD=0.1.0.312 MAJOR=0 MINOR=1 PATCH=0 COMMITS=312 SHA=fcf09f3e"
    "v1.2.3-rc1-5-gabc1234|NUMERIC=1.2.3 QUAD=1.2.3.5 PRERELEASE=-rc1"
    # a bare triple: the describe parser rejects it, components fall back to it
    "0.1.0|NUMERIC=0.1.0 QUAD=0.1.0.0 MAJOR=0 MINOR=1 PATCH=0 COMMITS=0 SHA= DIRTY=FALSE"
    # package-deb.sh's git-less fallback
    "0.0.0|NUMERIC=0.0.0 QUAD=0.0.0.0"
    "not-a-version|FAIL"
    ".1.0|FAIL"                   # a missing major must be rejected, not yield ".1.0"
    "|FAIL"
)

# One row per parser: "<label>/<function>/<table variable>".
set(_suites
    "describe/endo_parse_git_describe/_describe_cases"
    "version.txt/endo_parse_version_file/_version_file_cases"
)

# Check one parsed record against one row's expectations. A failed assertion is
# reported with message(SEND_ERROR), which is what makes `cmake -P` exit non-zero
# -- there is no separate failure counter to keep in step.
# @param label        What is under test, for failure messages.
# @param input        The row's input.
# @param prefix       The record prefix the parser wrote.
# @param expectations The row's assertion list, or "FAIL".
function(check_version_record label input prefix expectations)
    if(expectations STREQUAL "FAIL")
        if(${prefix}_OK)
            message(SEND_ERROR "FAIL: ${label} '${input}' expected rejection but parsed.")
        endif()
        return()
    endif()

    if(NOT ${prefix}_OK)
        message(SEND_ERROR "FAIL: ${label} '${input}' was rejected but should parse.")
        return()
    endif()

    string(REPLACE " " ";" _assertions "${expectations}")
    foreach(_assertion IN LISTS _assertions)
        if(NOT _assertion MATCHES "^([A-Z_]+)=(.*)$")
            message(FATAL_ERROR "${label}: malformed expectation '${_assertion}' in row '${input}'.")
        endif()
        set(_key "${CMAKE_MATCH_1}")
        set(_want "${CMAKE_MATCH_2}")

        # A misspelled field name must fail loudly rather than assert nothing --
        # that is the one way a name-addressed table can quietly stop testing.
        if(NOT _key IN_LIST ENDO_VERSION_RECORD_FIELDS)
            message(FATAL_ERROR "${label}: '${_key}' is not a version-record field (row '${input}').")
        endif()

        if(NOT "${${prefix}_${_key}}" STREQUAL "${_want}")
            message(SEND_ERROR
                "FAIL: ${label} '${input}' ${_key}: expected '${_want}' but got '${${prefix}_${_key}}'.")
        endif()
    endforeach()
endfunction()

set(_total 0)
foreach(_suite IN LISTS _suites)
    string(REPLACE "/" ";" _suite_parts "${_suite}")
    list(GET _suite_parts 0 _label)
    list(GET _suite_parts 1 _parser)
    list(GET _suite_parts 2 _table)

    foreach(_case IN LISTS ${_table})
        string(REPLACE "|" ";" _parts "${_case}")
        list(GET _parts 0 _input)
        list(GET _parts 1 _expectations)
        cmake_language(CALL ${_parser} "${_input}" _record)
        check_version_record("${_label}" "${_input}" _record "${_expectations}")
        math(EXPR _total "${_total} + 1")
    endforeach()
endforeach()

# A rejected parse must leave no field of an earlier successful parse behind: the
# record is reused under one prefix by cmake/Version.cmake's fallback chain.
endo_parse_git_describe("v9.9.9-9-gfeedbee" _reused)
endo_parse_git_describe("garbage" _reused)
if(NOT _reused_MAJOR STREQUAL "")
    message(SEND_ERROR "FAIL: a rejected parse left MAJOR='${_reused_MAJOR}' from the previous one.")
endif()
math(EXPR _total "${_total} + 1")

message(STATUS "version-parse: ${_total} cases checked.")
