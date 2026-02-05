# This directory structure is being created by `scripts/install-deps.sh`
# and is used to inject all the dependencies the operating system's
# package manager did not provide (not found or too old version).

include(CPM)

set(EndoThirdParties_SRCDIR ${PROJECT_SOURCE_DIR}/_deps/sources)
if(EXISTS "${EndoThirdParties_SRCDIR}/CMakeLists.txt")
    message(STATUS "Embedding 3rdparty libraries: ${EndoThirdParties_SRCDIR}")
    add_subdirectory(${EndoThirdParties_SRCDIR})
else()
    message(STATUS "No 3rdparty libraries found at ${EndoThirdParties_SRCDIR}")
endif()

set(LIST EndoThirdParties)
macro(Thirdparty_Include_If_MIssing _TARGET _PACKAGE_NAME)
    if(${_PACKAGE_NAME} STREQUAL "")
        set(${_PACKAGE_NAME} ${_TARGET})
    endif()
    if (NOT TARGET ${_TARGET})
        find_package(${_PACKAGE_NAME} REQUIRED)
        list(APPEND EndoThirdParties ${_TARGET}_SYSDEP)
        set(THIRDPARTY_BUILTIN_${_TARGET} "system package")
    else()
        list(APPEND EndoThirdParties ${_TARGET}_EMBED)
        set(THIRDPARTY_BUILTIN_${_TARGET} "embedded")
    endif()
endmacro()

# TODO make me working
macro(EndoThirdPartiesSummary)
    message(STATUS "==============================================================================")
    message(STATUS "    Endo ThirdParties")
    message(STATUS "------------------------------------------------------------------------------")
    foreach(TP ${EndoThirdParties})
        message(STATUS "${TP}\t\t${THIRDPARTY_BUILTIN_${TP}}")
    endforeach()
endmacro()

# Now, conditionally find all dependencies that were not included above
# via find_package, usually system installed packages.

# Try to find Catch2 v3 from the system, otherwise use CPM
find_package(Catch2 3 QUIET)
if(TARGET Catch2::Catch2)
    set(THIRDPARTY_BUILTIN_Catch2 "system package")
else()
    CPMAddPackage(
        NAME Catch2
        VERSION 3.8.0
        GITHUB_REPOSITORY catchorg/Catch2
        EXCLUDE_FROM_ALL YES
    )
    set(THIRDPARTY_BUILTIN_Catch2 "CPM (v3.8.0)")
endif()

# Try to find GSL from the system, otherwise use CPM
if(TARGET GSL)
    set(THIRDPARTY_BUILTIN_GSL "embedded")
else()
    if(WIN32)
        find_package(Microsoft.GSL CONFIG QUIET)
    else()
        find_package(Microsoft.GSL QUIET)
    endif()
    if(TARGET Microsoft.GSL::GSL)
        set(THIRDPARTY_BUILTIN_GSL "system package")
    else()
        CPMAddPackage(
            NAME GSL
            VERSION 4.1.0
            GITHUB_REPOSITORY microsoft/GSL
            EXCLUDE_FROM_ALL YES
        )
        set(THIRDPARTY_BUILTIN_GSL "CPM (v4.1.0)")
    endif()
endif()

if (TARGET range-v3)
    set(THIRDPARTY_BUILTIN_range_v3 "embedded")
else()
    find_package(range-v3 REQUIRED)
    set(THIRDPARTY_BUILTIN_range_v3 "system package")
endif()

if (TARGET yaml-cpp)
    set(THIRDPARTY_BUILTIN_yaml_cpp "embedded")
else()
    find_package(yaml-cpp REQUIRED)
    set(THIRDPARTY_BUILTIN_yaml_cpp "system package")
endif()

EndoThirdParties_Embed_libunicode()

if (TARGET unicode::core)
    set(THIRDPARTY_BUILTIN_unicode_core "embedded")
else()
    find_package(libunicode REQUIRED)
    set(THIRDPARTY_BUILTIN_unicode_core "system package")
endif()

EndoThirdParties_Embed_boxed_cpp()
set(THIRDPARTY_BUILDIN_boxed_cpp "embedded")

# reflection-cpp is required by crispy::core
CPMAddPackage(
    NAME reflection-cpp
    GITHUB_REPOSITORY contour-terminal/reflection-cpp
    GIT_TAG master
    EXCLUDE_FROM_ALL YES
)
set(THIRDPARTY_BUILTIN_reflection_cpp "CPM")

macro(EndoThirdPartiesSummary2)
    message(STATUS "==============================================================================")
    message(STATUS "    Endo Shell ThirdParties")
    message(STATUS "------------------------------------------------------------------------------")
    message(STATUS "Catch2              ${THIRDPARTY_BUILTIN_Catch2}")
    message(STATUS "GSL                 ${THIRDPARTY_BUILTIN_GSL}")
    message(STATUS "range-v3            ${THIRDPARTY_BUILTIN_range_v3}")
    message(STATUS "unicode::core       ${THIRDPARTY_BUILTIN_unicode_core}")
    message(STATUS "yaml-cpp            ${THIRDPARTY_BUILTIN_yaml_cpp}")
    message(STATUS "boxed-cpp           ${THIRDPARTY_BUILDIN_boxed_cpp}")
    message(STATUS "------------------------------------------------------------------------------")
endmacro()
