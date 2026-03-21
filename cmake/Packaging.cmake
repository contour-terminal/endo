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
    set(CPACK_GENERATOR "NSIS;WIX")

    # NSIS (.exe installer)
    set(CPACK_NSIS_PACKAGE_NAME "Endo Shell")
    set(CPACK_NSIS_DISPLAY_NAME "Endo Shell ${PROJECT_VERSION}")
    set(CPACK_NSIS_HELP_LINK "https://endo-lang.org/")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/contour-terminal/endo")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Endo")

    # WIX (.msi installer)
    # Stable GUID for MSI upgrade detection — must never change once published.
    set(CPACK_WIX_UPGRADE_GUID "E8A5C7B2-3F1D-4A9E-B6D0-8C2F5E7A1B3D")

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
    # The component is defined in wix-env-path.wxs (compiled as extra source);
    # the patch file injects the ComponentRef into the product feature.
    set(CPACK_WIX_EXTRA_SOURCES "${CMAKE_SOURCE_DIR}/cmake/wix-env-path.wxs")
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

# Platform-specific installer customization (PATH modification, etc.)
# Uses a separate file to bypass CPackConfig.cmake serialization issues
# with embedded quotes and backslashes in NSIS commands.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/CPackOptions.cmake")

# Bundle MSVC runtime DLLs (vcruntime140.dll, msvcp140.dll, etc.) so the
# installer works on machines without the Visual C++ Redistributable.
include(InstallRequiredSystemLibraries)

include(CPack)
