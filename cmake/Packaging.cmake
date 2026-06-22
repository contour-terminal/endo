# SPDX-License-Identifier: Apache-2.0
# CPack packaging configuration for Endo Shell.

# Common metadata
set(CPACK_PACKAGE_NAME "endo")
set(CPACK_PACKAGE_VENDOR "Endo Project")
set(CPACK_PACKAGE_CONTACT "Christian Parpart <christian@parpart.family>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A modern, cross-platform shell where functional programming meets everyday productivity")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://endo-lang.org/")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# Platform-specific generator selection and configuration
if(WIN32)
    # WIX (.msi) is the single canonical Windows installer. NSIS was dropped:
    # it force-uninstalled before install, which required stopping every running
    # endo.exe. The WIX installer below upgrades without that constraint.
    set(CPACK_GENERATOR "WIX")

    # Install each version into its own directory under "Program Files\Endo\",
    # e.g. "Program Files\Endo\1.2.3\". This is the key to upgrading without
    # stopping running endo.exe processes: a new version's files land in a brand
    # new directory, never touching the locked binary the running process holds.
    # endo locates its data relative to the executable (see InstallPaths.cpp), so
    # a versioned prefix needs no code changes.
    # Use a forward slash as the separator: CPack writes this value verbatim into
    # the generated CPackConfig.cmake, and a literal backslash there would form an
    # invalid CMake string escape (e.g. "Endo\0.1.135" -> bad escape '\0'). The WIX
    # generator splits on '/' and still emits the nested "Endo\<version>\" tree.
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Endo/${PROJECT_VERSION}")

    # WIX (.msi installer)
    # Stable GUID for MSI upgrade detection — must never change once published.
    set(CPACK_WIX_UPGRADE_GUID "E8A5C7B2-3F1D-4A9E-B6D0-8C2F5E7A1B3D")

    # Custom product template: schedules the major upgrade AFTER the new files
    # are installed and defers removal of the in-use old binary to next reboot,
    # so upgrades never force the user to close running endo.exe processes.
    set(CPACK_WIX_TEMPLATE "${CMAKE_SOURCE_DIR}/cmake/wix-template.wxs")

    # Generate LICENSE.rtf from LICENSE.txt — WIX requires RTF format for the
    # license dialog.  Keep LICENSE.txt as the single source of truth.
    file(READ "${CMAKE_SOURCE_DIR}/LICENSE.txt" _license_text)
    string(REPLACE "\\" "\\\\" _license_text "${_license_text}")
    string(REPLACE "{" "\\{" _license_text "${_license_text}")
    string(REPLACE "}" "\\}" _license_text "${_license_text}")
    string(REPLACE "\n" "\\par\n" _license_text "${_license_text}")
    file(WRITE "${CMAKE_BINARY_DIR}/LICENSE.rtf"
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}}\\f0\\fs18\n"
        "${_license_text}"
        "}\n")
    set(CPACK_WIX_LICENSE_RTF "${CMAKE_BINARY_DIR}/LICENSE.rtf")

    # Add endo's bin directory to the system PATH on install/remove on uninstall.
    # The component is defined in wix-env-path.wxs.in (compiled as extra source);
    # the patch file injects the ComponentRef into the product feature.
    #
    # The PATH component GUID is derived deterministically from the version so
    # that each installed version owns a distinct PATH component. This lets an
    # upgrade cleanly add the new version's bin directory and remove the previous
    # one (Environment edits are registry-only and never locked), leaving the
    # newest version's bin first/only on PATH — so "endo" always resolves to the
    # latest install. Reusing one GUID across versions would violate MSI's
    # component rules (same GUID, differing keypath value) and break migration.
    string(UUID ENDO_PATH_COMPONENT_GUID
        NAMESPACE "${CPACK_WIX_UPGRADE_GUID}"
        NAME "endo-path-${PROJECT_VERSION}"
        TYPE SHA1 UPPER)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/wix-env-path.wxs.in"
                   "${CMAKE_BINARY_DIR}/wix-env-path.wxs" @ONLY)
    set(CPACK_WIX_EXTRA_SOURCES "${CMAKE_BINARY_DIR}/wix-env-path.wxs")
    set(CPACK_WIX_PATCH_FILE "${CMAKE_SOURCE_DIR}/cmake/wix-path-env.xml")

elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")

    set(CPACK_DMG_VOLUME_NAME "Endo ${PROJECT_VERSION}")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;RPM")

    # DEB
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "shells")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://endo-lang.org/")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # RPM
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
    set(CPACK_RPM_PACKAGE_GROUP "System Environment/Shells")
    set(CPACK_RPM_PACKAGE_URL "https://endo-lang.org/")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

else()
    set(CPACK_GENERATOR "TGZ")
endif()

# Bundle MSVC runtime DLLs (vcruntime140.dll, msvcp140.dll, etc.) so the
# installer works on machines without the Visual C++ Redistributable.
include(InstallRequiredSystemLibraries)

include(CPack)
