## Sets pedantic compiler warnings for a target.
##
## Every option below is scoped to $<COMPILE_LANGUAGE:CXX>. target_compile_options
## applies to EVERY language a target compiles, and a target carrying the Windows
## VERSIONINFO resource also compiles RC -- where rc.exe treats an unknown switch
## as a fatal RC1106. Nothing here means anything to a resource compiler.

include(CheckCXXCompilerFlag)

option(ENDO_WARNINGS_AS_ERRORS "Treat compiler warnings in endo's own targets as errors [default: ON]" ON)

# Warnings that are switched off, each for a reason that holds for the whole tree. A warning a
# compiler does not know is never passed: an unknown -Wno-* is itself a warning, and so an error
# under -Werror.
set(_endo_disabled_warning_flags "")

# Catch2's TEST_CASE and SECTION expand to __COUNTER__, which -Wpedantic reports as a C2y extension
# in every test that uses them, and every compiler endo builds with implements it.
check_cxx_compiler_flag(-Wc2y-extensions ENDO_HAS_WC2Y_EXTENSIONS)
if(ENDO_HAS_WC2Y_EXTENSIONS)
    list(APPEND _endo_disabled_warning_flags -Wno-c2y-extensions)
endif()

# Descriptor tables and option structs name only the fields that differ from the defaults their
# members declare; that is what designated initializers are for here. GCC, and Clang before 19
# (Emscripten 3.1.51 ships clang 18), report those under -Wmissing-field-initializers alone.
check_cxx_compiler_flag(-Wmissing-designated-field-initializers ENDO_HAS_WMISSING_DESIGNATED_FIELD_INITIALIZERS)
if(ENDO_HAS_WMISSING_DESIGNATED_FIELD_INITIALIZERS)
    list(APPEND _endo_disabled_warning_flags -Wno-missing-designated-field-initializers)
else()
    list(APPEND _endo_disabled_warning_flags -Wno-missing-field-initializers)
endif()

# `date +FORMAT` hands the user's format string to strftime(), which is the point of it.
check_cxx_compiler_flag(-Wformat-nonliteral ENDO_HAS_WFORMAT_NONLITERAL)
if(ENDO_HAS_WFORMAT_NONLITERAL)
    list(APPEND _endo_disabled_warning_flags -Wno-format-nonliteral)
endif()

# Clang's -Wshadow leaves out a constructor parameter shadowing the member it initializes, which
# GCC's -Wshadow reports; -Wshadow-all is the clang set that covers it.
check_cxx_compiler_flag(-Wshadow-all ENDO_HAS_WSHADOW_ALL)
if(ENDO_HAS_WSHADOW_ALL)
    set(_endo_shadow_warning -Wshadow-all)
else()
    set(_endo_shadow_warning -Wshadow)
endif()

function(set_pedantic_compiler_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # The base warning level goes first: a later group flag switches back on whatever an
        # earlier -Wno-* turned off. clang-cl interprets -Wall as MSVC's /Wall, which maps to
        # Clang's -Weverything; its /W4 is what maps to Clang's -Wall -Wextra.
        if(MSVC)
            set(_warnings /W4)
        else()
            set(_warnings -Wall)
        endif()
        list(APPEND _warnings
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            ${_endo_shadow_warning}
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            ${_endo_disabled_warning_flags}
        )
        if(MSVC)
            # Suppress backward-compatibility warnings irrelevant for a C++23 codebase.
            list(APPEND _warnings
                -Wno-c++98-compat-pedantic
                -Wno-pre-c++14-compat
                -Wno-pre-c++17-compat
                -Wno-pre-c++20-compat-pedantic
            )
        endif()
        if(ENDO_WARNINGS_AS_ERRORS)
            list(APPEND _warnings -Werror)
        endif()
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_warnings}>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(_warnings /W4 /utf-8)
        if(ENDO_WARNINGS_AS_ERRORS)
            list(APPEND _warnings /WX)
        endif()
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_warnings}>)
    endif()
endfunction()

## @brief Applies set_pedantic_compiler_warnings() to every target built from source at or below @p dir.
##
## A walk rather than a call per target, so a target added later cannot be left out. Third-party
## code is never below @p dir, so it keeps whatever warnings its own build chooses.
function(set_pedantic_compiler_warnings_below dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            set_pedantic_compiler_warnings(${_target})
        endif()
    endforeach()
    get_property(_children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        set_pedantic_compiler_warnings_below("${_child}")
    endforeach()
endfunction()
