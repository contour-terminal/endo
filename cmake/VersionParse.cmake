# SPDX-License-Identifier: Apache-2.0
#
# Pure, side-effect-free parsing of `git describe` output into an Endo version.
#
# Kept separate from Version.cmake (which performs the actual git invocations) so
# the parsing logic can be unit-tested in isolation via `cmake -P` with mock
# inputs and no git dependency. See cmake/tests/TestVersionParse.cmake.

# Maximum value of an MSI ProductVersion build field. The Windows Installer
# compares only major.minor.build (each capped); a larger value is rejected by
# the WiX toolset or silently wraps, breaking upgrade detection.
set(ENDO_MSI_BUILD_FIELD_MAX 65535)

# Clamp a numeric build component to the MSI build-field limit, warning if it
# overflows.
# @param value     The candidate build number.
# @param out_var   Name of the variable to receive the clamped value.
function(endo_clamp_msi_build value out_var)
    if(value GREATER ${ENDO_MSI_BUILD_FIELD_MAX})
        message(WARNING
            "Endo: derived MSI build number ${value} exceeds ${ENDO_MSI_BUILD_FIELD_MAX}; "
            "clamping (Windows Installer ProductVersion build-field limit).")
        set(${out_var} "${ENDO_MSI_BUILD_FIELD_MAX}" PARENT_SCOPE)
    else()
        set(${out_var} "${value}" PARENT_SCOPE)
    endif()
endfunction()

# Parse the output of `git describe --tags --long --match "v*" --dirty` into a
# numeric version triple and a human-readable string.
#
# Accepted shape: v<major>.<minor>.<patch>[<suffix>]-<commits>-g<sha>[-dirty]
# where <suffix> is any optional pre-release/build metadata (e.g. "-rc1"); it is
# preserved in the human string but ignored for the numeric triple. The two-step
# match (peel the "-<commits>-g<sha>" trailer first, then parse the tag core)
# makes pre-release tags parse correctly instead of being silently dropped.
#
# Numbering:
#   * Exactly on a clean tag (commits == 0, not dirty) -> the clean tag version.
#   * Otherwise (development build, or dirty on a tag)  -> patch + commits folded
#     into the build field, so each commit yields a unique, increasing version
#     within the release line. (Releases always use the clean tag version and are
#     what users install; CI/dev MSIs are not distributed, so cross-tag ordering
#     is intentionally not guaranteed.)
#
# @param describe      The git-describe string to parse.
# @param out_ok        Receives TRUE if parsing succeeded, FALSE otherwise.
# @param out_numeric   Receives the numeric triple "major.minor.build".
# @param out_human     Receives the human-readable version string.
# @param out_source    Receives a short description of the version source.
function(endo_parse_git_describe describe out_ok out_numeric out_human out_source)
    # Step 1: peel the "-<commits>-g<sha>[-dirty]" describe trailer. Anchored at
    # the end so the greedy tag capture backtracks deterministically.
    if(NOT describe MATCHES "^(.+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        set(${out_ok} FALSE PARENT_SCOPE)
        return()
    endif()
    # Save the captures immediately: the next MATCHES clobbers CMAKE_MATCH_*.
    set(_tag "${CMAKE_MATCH_1}")
    set(_commits "${CMAKE_MATCH_2}")
    set(_sha "${CMAKE_MATCH_3}")
    set(_dirty "${CMAKE_MATCH_4}")

    # Step 2: parse the numeric core from the tag; anything after the patch is
    # optional pre-release/build metadata kept only in the human string.
    if(NOT _tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(.*)$")
        set(${out_ok} FALSE PARENT_SCOPE)
        return()
    endif()
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    set(_patch "${CMAKE_MATCH_3}")
    set(_suffix "${CMAKE_MATCH_4}")

    if(_commits EQUAL 0 AND _dirty STREQUAL "")
        # Exactly on a clean release tag.
        set(${out_numeric} "${_major}.${_minor}.${_patch}" PARENT_SCOPE)
        set(${out_human} "${_major}.${_minor}.${_patch}${_suffix}" PARENT_SCOPE)
        set(${out_source} "git tag" PARENT_SCOPE)
    else()
        # Development build N commits past the tag (or a dirty checkout exactly on
        # a tag, which folds to patch+0 == patch and so aliases the clean release
        # numerically — acceptable because a dirty build is local-only, never
        # released, and the "-dirty" marker is preserved in the human string).
        math(EXPR _build "${_patch} + ${_commits}")
        endo_clamp_msi_build("${_build}" _build)
        set(${out_numeric} "${_major}.${_minor}.${_build}" PARENT_SCOPE)
        set(${out_human}
            "${_major}.${_minor}.${_patch}${_suffix}-${_commits}-g${_sha}${_dirty}" PARENT_SCOPE)
        set(${out_source} "git describe (development build)" PARENT_SCOPE)
    endif()
    set(${out_ok} TRUE PARENT_SCOPE)
endfunction()
