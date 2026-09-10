# SPDX-License-Identifier: Apache-2.0
#
# Pure, side-effect-free parsing of Endo's version sources into a version record.
#
# Kept separate from Version.cmake (which performs the actual git invocations) so
# the parsing logic can be unit-tested in isolation via `cmake -P` with mock
# inputs and no git dependency. See cmake/tests/TestVersionParse.cmake.

# The clamp for a version component that lands in a 16-bit field: the MSI
# ProductVersion build field and every field of a Windows VERSIONINFO
# FILEVERSION/PRODUCTVERSION. One constant, because it is one limit.
#
# Only the derived components are clamped -- the commit distance and the folded
# build number. A tag's own MAJOR/MINOR/PATCH pass through untouched: a project
# that tags v65536.0.0 has a different problem than this file can solve.
set(ENDO_VERSION_FIELD_MAX 65535)

# The version record: the complete set of fields every parser below produces.
# One list drives clearing, copying, publishing and (in the unit test) assertion,
# so a new field is one entry here rather than an edit to every parser, every
# consumer and every test row.
#
#   OK          TRUE when the input parsed.
#   NUMERIC     "major.minor.build" -- the triple project()/CPack/MSI consume.
#               How `build` is derived is per-source policy, not a fixed rule.
#   HUMAN       Human-readable version string.
#   SOURCE      Short description of where the version came from.
#   MAJOR       The tag's major, never folded.
#   MINOR       The tag's minor, never folded.
#   PATCH       The tag's patch, never folded.
#   COMMITS     Commits since the tag; 0 exactly on a tag. Clamped, because it
#               lands in a 16-bit VERSIONINFO field. The true count survives in
#               HUMAN.
#   QUAD        "major.minor.patch.commits" -- the four-field form the OS-level
#               binary metadata reports. Composed here, where the components are
#               known, rather than by each of its consumers.
#   SHA         Short commit sha, without the describe's leading "g".
#   DIRTY       TRUE when the working tree was modified.
#   PRERELEASE  The tag's pre-release/build suffix ("-rc1"), or "".
set(ENDO_VERSION_RECORD_FIELDS
    OK NUMERIC HUMAN SOURCE MAJOR MINOR PATCH COMMITS QUAD SHA DIRTY PRERELEASE)

# Reset every record field to its "nothing parsed" value.
#
# The parsers build their record in these bare _<FIELD> locals and write them
# only on a path that also sets _OK, so a rejected parse publishes nothing but
# these defaults.
#
# Macros rather than functions: these all have to touch the *calling function's*
# scope, and a nested function() would put PARENT_SCOPE one level short.
macro(endo_clear_version_record)
    foreach(_record_field IN LISTS ENDO_VERSION_RECORD_FIELDS)
        set(_${_record_field} "")
    endforeach()
    set(_OK FALSE)
    set(_DIRTY FALSE)
    set(_COMMITS 0)
endmacro()

# Publish the record built up in the local _<FIELD> variables under @p prefix.
macro(endo_publish_version_record prefix)
    foreach(_record_field IN LISTS ENDO_VERSION_RECORD_FIELDS)
        set(${prefix}_${_record_field} "${_${_record_field}}" PARENT_SCOPE)
    endforeach()
endmacro()

# Adopt a record another parser published under @p prefix as this scope's own
# local _<FIELD> variables -- the inverse of endo_publish_version_record().
macro(endo_adopt_version_record prefix)
    foreach(_record_field IN LISTS ENDO_VERSION_RECORD_FIELDS)
        set(_${_record_field} "${${prefix}_${_record_field}}")
    endforeach()
endmacro()

# Publish an empty record under @p prefix and return from the calling function.
# A macro so that the return() leaves the *parser*, keeping its guard clauses
# flat instead of nesting the whole body inside them.
macro(endo_reject_version_record prefix)
    endo_clear_version_record()
    endo_publish_version_record(${prefix})
    return()
endmacro()

## @brief Fails the configure unless a parsed version record exists under @p prefix.
##
## The mechanisms that stamp version metadata onto a binary are useless without
## one, and a missing record means they were called before
## endo_get_version_information() -- an ordering mistake worth naming rather than
## silently embedding an empty version.
## @param prefix  The record prefix to check.
## @param context What is asking, for the error message.
macro(endo_require_version_record prefix context)
    if(NOT ${prefix}_OK)
        message(FATAL_ERROR
            "${context}: no version record under '${prefix}'. "
            "endo_get_version_information() must run before this is called.")
    endif()
endmacro()

# Clamp a numeric version component to the 16-bit field limit, warning if it
# overflows.
# @param value    The candidate number.
# @param what     What the number is, for the warning text.
# @param out_var  Name of the variable to receive the clamped value.
function(endo_clamp_version_field value what out_var)
    if(value GREATER ${ENDO_VERSION_FIELD_MAX})
        message(WARNING
            "Endo: derived ${what} ${value} exceeds ${ENDO_VERSION_FIELD_MAX}; clamping "
            "(16-bit version-field limit: MSI ProductVersion and Win32 VERSIONINFO alike).")
        set(${out_var} "${ENDO_VERSION_FIELD_MAX}" PARENT_SCOPE)
    else()
        set(${out_var} "${value}" PARENT_SCOPE)
    endif()
endfunction()

# Parse the output of `git describe --tags --long --match "v*" --dirty` into a
# version record.
#
# Accepted shape: v<major>.<minor>.<patch>[<suffix>]-<commits>-g<sha>[-dirty]
# where <suffix> is any optional pre-release/build metadata (e.g. "-rc1"); it is
# reported separately and ignored for the numeric triple. The two-step match
# (peel the "-<commits>-g<sha>" trailer first, then parse the tag core) makes
# pre-release tags parse correctly instead of being silently dropped.
#
# NUMERIC policy for this source:
#   * Exactly on a clean tag (commits == 0, not dirty) -> the clean tag version.
#   * Otherwise (development build, or dirty on a tag)  -> patch + commits folded
#     into the build field, so each commit yields a unique, increasing version
#     within the release line. (Releases always use the clean tag version and are
#     what users install; CI/dev MSIs are not distributed, so cross-tag ordering
#     is intentionally not guaranteed.)
#
# The components and QUAD are reported unfolded regardless, because the OS-level
# metadata stamped into the binary wants the tag's own MAJOR.MINOR.PATCH plus the
# commit distance in a fourth field -- one regex feeding both shapes rather than
# a second parser for the second consumer.
#
# @param describe    The git-describe string to parse.
# @param out_prefix  Variable-name prefix. Every field named in
#                    ENDO_VERSION_RECORD_FIELDS is set in the caller's scope, on
#                    success and on rejection alike, so a caller reusing one
#                    prefix for two parses can never read the first parse's
#                    leftovers.
function(endo_parse_git_describe describe out_prefix)
    endo_clear_version_record()

    # Step 1: peel the "-<commits>-g<sha>[-dirty]" describe trailer. Anchored at
    # the end so the greedy tag capture backtracks deterministically.
    if(NOT describe MATCHES "^(.+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        endo_reject_version_record(${out_prefix})
    endif()
    # Save the captures immediately: the next MATCHES clobbers CMAKE_MATCH_*.
    set(_tag "${CMAKE_MATCH_1}")
    set(_commit_count "${CMAKE_MATCH_2}")
    set(_short_sha "${CMAKE_MATCH_3}")
    set(_dirty_marker "${CMAKE_MATCH_4}")

    # Step 2: parse the numeric core from the tag; anything after the patch is
    # optional pre-release/build metadata kept only in the human string.
    if(NOT _tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(.*)$")
        endo_reject_version_record(${out_prefix})
    endif()

    set(_OK TRUE)
    set(_MAJOR "${CMAKE_MATCH_1}")
    set(_MINOR "${CMAKE_MATCH_2}")
    set(_PATCH "${CMAKE_MATCH_3}")
    set(_PRERELEASE "${CMAKE_MATCH_4}")
    set(_SHA "${_short_sha}")
    if(NOT _dirty_marker STREQUAL "")
        set(_DIRTY TRUE)
    endif()
    endo_clamp_version_field("${_commit_count}" "commit count since tag" _COMMITS)
    set(_QUAD "${_MAJOR}.${_MINOR}.${_PATCH}.${_COMMITS}")

    if(_commit_count EQUAL 0 AND NOT _DIRTY)
        # Exactly on a clean release tag.
        set(_NUMERIC "${_MAJOR}.${_MINOR}.${_PATCH}")
        set(_HUMAN "${_MAJOR}.${_MINOR}.${_PATCH}${_PRERELEASE}")
        set(_SOURCE "git tag")
    else()
        # Development build N commits past the tag (or a dirty checkout exactly
        # on a tag, which folds to patch+0 == patch and so aliases the clean
        # release numerically -- acceptable because a dirty build is local-only,
        # never released, and the "-dirty" marker is preserved in the human
        # string).
        math(EXPR _build "${_PATCH} + ${_commit_count}")
        endo_clamp_version_field("${_build}" "MSI ProductVersion build field" _build)
        set(_NUMERIC "${_MAJOR}.${_MINOR}.${_build}")
        set(_HUMAN
            "${_MAJOR}.${_MINOR}.${_PATCH}${_PRERELEASE}-${_commit_count}-g${_short_sha}${_dirty_marker}")
        set(_SOURCE "git describe (development build)")
    endif()

    endo_publish_version_record(${out_prefix})
endfunction()

# Interpret the contents of ${CMAKE_SOURCE_DIR}/version.txt into the same record.
#
# The file is written by scripts/package-deb.sh and packaging/deb/Dockerfile and
# holds a FULL describe string ("v0.1.0-312-gfcf09f3e"), because the Debian build
# context excludes .git. So the describe parser does the work, and this function
# overrides only the fields whose policy differs on this path -- above all
# NUMERIC, which stays the UNFOLDED tag triple (0.1.0). That is what this path
# has always produced and what the published .deb is versioned; adopting the
# describe parser's folded value would silently move the .deb from 0.1.0 to
# 0.1.312.
#
# A bare "X.Y.Z" that the describe parser rejects (the git-less fallback in
# package-deb.sh) still parses, with zero commits.
#
# @param text        The stripped contents of version.txt.
# @param out_prefix  As for endo_parse_git_describe().
function(endo_parse_version_file text out_prefix)
    endo_clear_version_record()

    endo_parse_git_describe("${text}" _describe)
    if(_describe_OK)
        endo_adopt_version_record(_describe)
    # "[0-9]+" for the major, not the historical "[0-9]*": the latter accepted
    # ".1.0" and produced the triple ".1.0", which fails obscurely much later
    # inside project(VERSION).
    elseif(text MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+).*$")
        set(_OK TRUE)
        set(_MAJOR "${CMAKE_MATCH_1}")
        set(_MINOR "${CMAKE_MATCH_2}")
        set(_PATCH "${CMAKE_MATCH_3}")
        set(_QUAD "${_MAJOR}.${_MINOR}.${_PATCH}.${_COMMITS}")
    else()
        endo_reject_version_record(${out_prefix})
    endif()

    set(_NUMERIC "${_MAJOR}.${_MINOR}.${_PATCH}")
    set(_HUMAN "${text}")
    set(_SOURCE "version.txt")

    endo_publish_version_record(${out_prefix})
endfunction()
