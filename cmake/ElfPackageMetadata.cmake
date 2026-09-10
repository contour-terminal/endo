# SPDX-License-Identifier: Apache-2.0
#
# The systemd ELF Package Metadata note (.note.package), as one concern.
# Spec: https://systemd.io/ELF_PACKAGE_METADATA/ -- owner "FDO", note type
# 0xcafe1a7e, description a NUL-terminated compact JSON object.
#
# ELF has no VERSIONINFO equivalent; this note is the nearest thing to a
# standard, and systemd-coredump reads it straight out of a core dump to name
# the build that produced it. The note is SHF_ALLOC, so it survives `strip` and
# travels in the stripped .deb rather than disappearing into the .ddeb.

include(CheckLinkerFlag)

set(ENDO_PACKAGE_METADATA_TYPE "cmake" CACHE STRING
    "'type' field of the .note.package ELF note; a distro build passes deb/rpm/...")

# The note's `architecture` must use the packaging system's own spelling, which is
# not always uname's: a .deb says amd64 where CMAKE_SYSTEM_PROCESSOR says x86_64.
# One row per (packaging type, processor) pair that needs translating; anything
# not listed passes through unchanged, which is already right for rpm and for a
# plain cmake build.
set(ENDO_PACKAGE_ARCH_ALIASES
    "deb/x86_64/amd64"
    "deb/aarch64/arm64"
    "deb/armv7l/armhf"
)

# Translate @p processor into the spelling packaging type @p type uses.
# @param type      The .note.package `type` field.
# @param processor CMAKE_SYSTEM_PROCESSOR.
# @param out_var   Name of the variable to receive the architecture.
function(endo_package_metadata_architecture type processor out_var)
    foreach(_alias IN LISTS ENDO_PACKAGE_ARCH_ALIASES)
        string(REPLACE "/" ";" _parts "${_alias}")
        list(GET _parts 0 _alias_type)
        list(GET _parts 1 _alias_processor)
        list(GET _parts 2 _alias_arch)
        if(_alias_type STREQUAL "${type}" AND _alias_processor STREQUAL "${processor}")
            set(${out_var} "${_alias_arch}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "${processor}" PARENT_SCOPE)
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # -Xlinker, never -Wl, and never CMake's LINKER: prefix. Both of the latter
    # split their payload on every comma, and this option's value is JSON --
    # `-Wl,--package-metadata={"a":1,"b":2}` reaches ld as three arguments, two
    # of them garbage. -Xlinker passes exactly one argument through untouched.
    #
    # A probe rather than a version comparison decides: GNU ld grew this in
    # binutils 2.39 and LLVM lld in 15, gold never did, and mold at some release
    # this comment does not claim to know. Asking the linker in front of us is
    # both shorter and correct.
    check_linker_flag(CXX
        "-Xlinker;--package-metadata={\"type\":\"probe\",\"name\":\"probe\",\"version\":\"0\"}"
        ENDO_HAS_ELF_PACKAGE_METADATA)
    if(NOT ENDO_HAS_ELF_PACKAGE_METADATA)
        # Said out loud rather than skipped in silence: a missing .note.package
        # is invisible until someone tries to identify a core dump with it.
        message(STATUS
            "[Version] .note.package will NOT be emitted: this linker does not accept "
            "--package-metadata (needs GNU ld >= 2.39 or LLVM lld >= 15).")
    endif()
endif()

## @brief Emits a systemd ELF package-metadata note into @p target.
##
## A no-op off Linux and when the linker does not support the option, so call
## sites need no guard of their own.
##
## @param target The executable to annotate.
function(enable_elf_package_metadata target)
    # CMAKE_SYSTEM_NAME rather than UNIX: Emscripten sets UNIX, and wasm-ld has
    # no ELF notes to emit. This mirrors cmake/Packaging.cmake's own elseif.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()
    if(NOT ENDO_HAS_ELF_PACKAGE_METADATA)
        return()
    endif()

    endo_require_version_record(ENDO_VERSION "enable_elf_package_metadata(${target})")

    endo_package_metadata_architecture(
        "${ENDO_PACKAGE_METADATA_TYPE}" "${CMAKE_SYSTEM_PROCESSOR}" _arch)

    # Compact -- no spaces anywhere -- so the object stays one shell word however
    # CMake and Ninja quote it. JSON has no ';' and this object has no '$', so
    # there is nothing here for CMake's list escaping to mangle either.
    set(_json "{\"type\":\"${ENDO_PACKAGE_METADATA_TYPE}\"")
    string(APPEND _json ",\"name\":\"${ENDO_PRODUCT_NAME}\"")
    string(APPEND _json ",\"version\":\"${ENDO_VERSION_HUMAN}\"")
    string(APPEND _json ",\"architecture\":\"${_arch}\"}")

    target_link_options(${target} PRIVATE "-Xlinker" "--package-metadata=${_json}")
    message(STATUS "[Version] ${target}: .note.package = ${_json}")
endfunction()
