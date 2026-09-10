# SPDX-License-Identifier: Apache-2.0
#
# Windows version metadata -- the VS_VERSION_INFO resource and the application
# manifest -- as one concern.
#
# Both carry a version, so both live here: leaving the manifest behind was how
# its assemblyIdentity froze at 0.1.0.0 while the rest of the build moved on.

# The resource compiler. It sits at module scope rather than inside the function
# because enable_language() is rejected inside a function(), and out of the
# project() LANGUAGES list because it has to be conditional. This module is
# include()d at directory scope, which is all enable_language() requires.
#
# WIN32 rather than MSVC: the .rc is self-contained, so windres accepts it too and
# a MinGW build would get the resource as well. Enabling RC does not reach
# cmake/CompileCache.cmake -- that module only ever fronts C and CXX -- so no
# compiler-cache launcher is put in front of the resource compiler.
if(WIN32)
    enable_language(RC)
endif()

# Whether a version resource will actually be embedded. Exported so that the test
# which reads the metadata back out of the binary can skip on the same condition
# the stamping uses, rather than on an approximation of it.
get_property(_endo_enabled_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if(WIN32 AND "RC" IN_LIST _endo_enabled_languages)
    set(ENDO_HAS_VERSION_RESOURCE ON)
else()
    set(ENDO_HAS_VERSION_RESOURCE OFF)
endif()

## @brief Embeds Windows version metadata into @p target.
##
## Without it a Windows executable has no version metadata at all and every
## reader -- Explorer, `Get-Command`, a crash reporter -- reports the synthesised
## 0.0.0.0.
##
## Templates (EndoVersionInfo.rc.in, endo.manifest.in) are read from @p target's
## own source directory, so this works from anywhere. A no-op off Windows, so
## call sites need no guard of their own.
##
## @param target The executable to embed the metadata into.
function(enable_windows_version_resource target)
    if(NOT ENDO_HAS_VERSION_RESOURCE)
        return()
    endif()
    endo_require_version_record(ENDO_VERSION "enable_windows_version_resource(${target})")

    get_target_property(_source_dir ${target} SOURCE_DIR)

    # VS_FF_PRERELEASE (0x2) for anything that is not exactly a clean release
    # tag; VS_FF_PRIVATEBUILD (0x8) for a modified working tree.
    set(_flags 0)
    if(ENDO_VERSION_COMMITS GREATER 0 OR NOT ENDO_VERSION_PRERELEASE STREQUAL "")
        math(EXPR _flags "${_flags} | 0x2")
    endif()

    # VS_FF_PRIVATEBUILD without a PrivateBuild string is a malformed
    # VERSIONINFO, so the flag and the string are set together or not at all.
    set(ENDO_RC_PRIVATE_BUILD_VALUE "")
    if(ENDO_VERSION_DIRTY)
        math(EXPR _flags "${_flags} | 0x8")
        set(ENDO_RC_PRIVATE_BUILD_VALUE
            "            VALUE \"PrivateBuild\",     \"built from a modified working tree\"")
    endif()

    # Both spellings are whole numbers computed here rather than an expression in
    # the .rc: resource compilers differ in how much constant arithmetic they
    # accept, and that is not a thing to discover on someone else's toolchain.
    math(EXPR _flags_debug "${_flags} | 0x1") # VS_FF_DEBUG
    math(EXPR _flags "${_flags}" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR _flags_debug "${_flags_debug}" OUTPUT_FORMAT HEXADECIMAL)
    set(ENDO_RC_FILEFLAGS "${_flags}L")
    set(ENDO_RC_FILEFLAGS_DEBUG "${_flags_debug}L")

    # FILEVERSION and PRODUCTVERSION want the quad comma-separated, so it is
    # spelled once here rather than twice in the template.
    string(REPLACE "." "," ENDO_RC_VERSION_FIELDS "${ENDO_VERSION_QUAD}")

    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/EndoVersionInfo.rc")
    configure_file("${_source_dir}/EndoVersionInfo.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")

    # Scoped to RC: an unscoped definition would also land on every C++
    # translation unit, and the Debug bit is the one flag a multi-config
    # generator can still change after the .rc was configured.
    target_compile_definitions(${target} PRIVATE
        $<$<AND:$<COMPILE_LANGUAGE:RC>,$<CONFIG:Debug>>:ENDO_RC_DEBUG_BUILD>)

    # The manifest is only consumed by the MSVC-style linker, which embeds it via
    # /MANIFESTINPUT -- so a MinGW build gets the resource above but no manifest,
    # rather than a configured file nothing reads. Note the resource deliberately
    # carries no RT_MANIFEST entry of its own; two would collide.
    if(MSVC)
        set(_manifest "${CMAKE_CURRENT_BINARY_DIR}/endo.manifest")
        configure_file("${_source_dir}/endo.manifest.in" "${_manifest}" @ONLY)
        target_sources(${target} PRIVATE "${_manifest}")
    endif()
endfunction()
