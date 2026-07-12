# Finds the Binaryen library (C API) for WebAssembly code generation.
#
# Binaryen ships no pkg-config or CMake package configuration, and some
# distributions (e.g. Fedora) install the shared library into a private
# directory such as /usr/lib64/binaryen that is not part of the default
# linker search path. This module handles both cases.
#
# Defines the imported target `Binaryen::Binaryen` on success, plus:
#   Binaryen_FOUND        - TRUE if header and library were found
#   Binaryen_INCLUDE_DIR  - directory containing binaryen-c.h
#   Binaryen_LIBRARY      - full path to the binaryen library

find_path(Binaryen_INCLUDE_DIR binaryen-c.h)
find_library(Binaryen_LIBRARY NAMES binaryen PATH_SUFFIXES binaryen)

# binaryen-c.h carries no version macro; probe for the newest API entry point
# the backend needs so that a too-old system binaryen cleanly reports
# "not found" (disabling the backend) instead of breaking the build.
if(Binaryen_INCLUDE_DIR AND Binaryen_LIBRARY)
    include(CheckSymbolExists)
    set(CMAKE_REQUIRED_INCLUDES "${Binaryen_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${Binaryen_LIBRARY}")
    check_symbol_exists(BinaryenFeatureBulkMemoryOpt "binaryen-c.h" Binaryen_HAS_REQUIRED_API)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Binaryen REQUIRED_VARS Binaryen_LIBRARY Binaryen_INCLUDE_DIR Binaryen_HAS_REQUIRED_API)

if(Binaryen_FOUND AND NOT TARGET Binaryen::Binaryen)
    add_library(Binaryen::Binaryen UNKNOWN IMPORTED)
    set_target_properties(Binaryen::Binaryen PROPERTIES
        IMPORTED_LOCATION "${Binaryen_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Binaryen_INCLUDE_DIR}")
endif()

mark_as_advanced(Binaryen_INCLUDE_DIR Binaryen_LIBRARY)
