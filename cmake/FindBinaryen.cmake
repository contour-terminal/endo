# Finds the Binaryen library (C API) for WebAssembly code generation.
#
# Binaryen ships no pkg-config or CMake package configuration, and some
# distributions (e.g. Fedora) install the shared library into a private
# directory such as /usr/lib64/binaryen that is not part of the default
# linker search path. This module handles both cases.
#
# Defines the imported target `Binaryen::Binaryen` on success, plus:
#   Binaryen_FOUND         - TRUE if header and library were found
#   Binaryen_INCLUDE_DIR   - directory containing binaryen-c.h
#   Binaryen_LIBRARY       - full path to the binaryen library
#   Binaryen_INSTALL_RPATH - directory to add to the RPATH of installed
#                            executables, or empty when the dynamic loader
#                            searches the library's directory by default

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

# When the library sits in a private directory (see above), the dynamic loader
# will not find it at runtime: libbinaryen.so carries no versioned SONAME and no
# entry in /etc/ld.so.conf.d.  Distributions solve this by putting that
# directory on the RPATH of their own binaries (Fedora's wasm-opt, for example,
# records RUNPATH=$ORIGIN/../lib64/binaryen), and consumers must do the same.
#
# CMake gives the *build tree* such an RPATH automatically, but drops it on
# install, so only installed executables are affected.  Report the directory
# that installed consumers need to record, and report nothing when binaryen is
# in a default loader directory -- there, an RPATH would be dead weight.
set(Binaryen_INSTALL_RPATH "")
if(Binaryen_FOUND)
    get_filename_component(_binaryen_library_dir "${Binaryen_LIBRARY}" DIRECTORY)
    if(NOT _binaryen_library_dir IN_LIST CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES)
        set(Binaryen_INSTALL_RPATH "${_binaryen_library_dir}")
    endif()
    unset(_binaryen_library_dir)
endif()

mark_as_advanced(Binaryen_INCLUDE_DIR Binaryen_LIBRARY)
