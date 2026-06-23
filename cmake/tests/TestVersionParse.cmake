# SPDX-License-Identifier: Apache-2.0
#
# Unit test for endo_parse_git_describe() in cmake/VersionParse.cmake.
# Pure CMake, no git required. Run via:
#   cmake -P cmake/tests/TestVersionParse.cmake
#
# Data-driven: each row is "<describe>|<expected-numeric>". The sentinel value
# "FAIL" means parsing is expected to be rejected (out_ok == FALSE).

include("${CMAKE_CURRENT_LIST_DIR}/../VersionParse.cmake")

set(_cases
    "v0.1.0-0-gabc1234|0.1.0"             # clean release tag
    "v1.2.3-5-gabc1234|1.2.8"             # development build: patch + commits
    "v1.2.3-0-gabc1234-dirty|1.2.3"       # dirty on a tag: folds to patch+0 (documented alias)
    "v1.2.3-rc1-5-gabc1234|1.2.8"         # pre-release suffix tolerated (kept only in human string)
    "v10.20.30-rc.2-7-gdeadbee|10.20.37"  # dotted pre-release, multi-digit components
    "v1.2.0-70000-gabc1234|1.2.65535"     # MSI build-field overflow clamped
    "v0.0.0-0-g0000000|0.0.0"             # zero version
    "garbage|FAIL"                        # not a describe string
    "v1.2-5-gabc1234|FAIL"                # tag missing the patch component
    "|FAIL"                               # empty input
)

set(_failures 0)
foreach(_case IN LISTS _cases)
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _describe)
    list(GET _parts 1 _expected)

    endo_parse_git_describe("${_describe}" _ok _numeric _human _source)

    if(_expected STREQUAL "FAIL")
        if(_ok)
            message(SEND_ERROR "FAIL: '${_describe}' expected rejection but parsed to '${_numeric}'.")
            math(EXPR _failures "${_failures} + 1")
        endif()
    elseif(NOT _ok)
        message(SEND_ERROR "FAIL: '${_describe}' expected '${_expected}' but was rejected.")
        math(EXPR _failures "${_failures} + 1")
    elseif(NOT _numeric STREQUAL _expected)
        message(SEND_ERROR "FAIL: '${_describe}' expected '${_expected}' but got '${_numeric}'.")
        math(EXPR _failures "${_failures} + 1")
    endif()
endforeach()

list(LENGTH _cases _total)
if(_failures GREATER 0)
    message(FATAL_ERROR "version-parse: ${_failures}/${_total} case(s) failed.")
endif()
message(STATUS "version-parse: all ${_total} cases passed.")
