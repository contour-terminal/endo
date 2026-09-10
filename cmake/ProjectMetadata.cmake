# SPDX-License-Identifier: Apache-2.0
#
# Endo's product identity, declared once.
#
# Four consumers repeat these strings into four different metadata formats --
# CPack (cmake/Packaging.cmake), the Win32 VERSIONINFO resource
# (cmake/WindowsVersionResource.cmake), the macOS __TEXT,__info_plist section
# (cmake/MacOSInfoPlist.cmake) and the ELF .note.package note
# (cmake/ElfPackageMetadata.cmake). Declaring them here is what keeps
# "Endo Project" from being spelled four ways, and the homepage from drifting
# between the three CPack variables that already carry it.
#
# Two things are deliberately out of scope. Version numbers, which come from the
# record endo_get_version_information() builds (cmake/Version.cmake). And strings
# the C++ compiles in -- the docs URL and tagline in src/shell/HelpPrinter.cpp --
# which would have to travel through src/shell/EndoVersion.hpp.in to get here;
# worth doing if they drift, not worth a bridge today.

set(ENDO_PRODUCT_NAME "endo")
set(ENDO_PRODUCT_DISPLAY_NAME "Endo")
set(ENDO_PRODUCT_VENDOR "Endo Project")
set(ENDO_PRODUCT_CONTACT "Christian Parpart <christian@parpart.family>")
set(ENDO_PRODUCT_DESCRIPTION
    "A modern, cross-platform shell where functional programming meets everyday productivity")
set(ENDO_PRODUCT_HOMEPAGE "https://endo-lang.org/")
set(ENDO_PRODUCT_LICENSE_SPDX "Apache-2.0")

# Hardcoded rather than derived from string(TIMESTAMP): a copyright year that
# follows the wall clock makes two builds of the same commit differ. 2022 is the
# first commit in this repository.
set(ENDO_PRODUCT_COPYRIGHT "Copyright (C) 2022-2026 Christian Parpart")

# Reverse-DNS of ENDO_PRODUCT_HOMEPAGE. endo is a CLI tool rather than a bundle,
# so nothing enforces this -- it is what `otool -P` prints and what any future
# codesigning would read.
set(ENDO_PRODUCT_BUNDLE_ID "org.endo-lang.endo")
