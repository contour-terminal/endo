# SPDX-License-Identifier: Apache-2.0
#
# The embedded macOS Info.plist, as one concern.

## @brief Embeds the generated Info.plist into @p target's __TEXT,__info_plist.
##
## endo is a CLI tool rather than an .app bundle, so MACOSX_BUNDLE and
## MACOSX_BUNDLE_INFO_PLIST do not apply -- there is no Contents/Info.plist to
## write. The linker is asked to create the section directly instead, which is
## the standard mechanism for a command-line tool and is what `otool -P` reads.
##
## `-current_version` / `-compatibility_version` are deliberately not used: ld64
## accepts them only when linking a dylib, and both write into LC_ID_DYLIB, which
## an executable does not have.
##
## The EndoInfo.plist.in template is read from @p target's own source directory,
## so this works from anywhere. A no-op off Apple platforms, so call sites need no
## guard of their own.
##
## @param target The executable to embed the plist into.
function(enable_macos_info_plist target)
    if(NOT APPLE)
        return()
    endif()
    endo_require_version_record(ENDO_VERSION "enable_macos_info_plist(${target})")

    get_target_property(_source_dir ${target} SOURCE_DIR)
    set(_plist "${CMAKE_CURRENT_BINARY_DIR}/EndoInfo.plist")
    configure_file("${_source_dir}/EndoInfo.plist.in" "${_plist}" @ONLY)

    # -Wl, splits on every comma, which is exactly how -sectcreate's four
    # arguments are delivered -- and exactly what a comma in the build path would
    # break, handing ld64 five garbage arguments. Spaces are fine (CMake
    # shell-quotes the whole token); commas are not, so refuse up front rather
    # than emit a link line that fails unreadably.
    if(_plist MATCHES ",")
        message(FATAL_ERROR
            "Endo: the build directory path contains a comma (${_plist}); the "
            "-Wl,-sectcreate link option cannot carry such a path. Build elsewhere.")
    endif()

    target_link_options(${target} PRIVATE "-Wl,-sectcreate,__TEXT,__info_plist,${_plist}")

    # A link option is not a dependency: without this, editing the plist (or
    # reconfiguring onto a new commit) leaves the already-linked executable
    # carrying the old one and nothing ever relinks. Honoured by the Ninja and
    # Makefile generators, which is all this project's presets use.
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${_plist}")
endfunction()
