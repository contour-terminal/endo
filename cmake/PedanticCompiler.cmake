## Sets pedantic compiler warnings for a target.
##
## Every option below is scoped to $<COMPILE_LANGUAGE:CXX>. target_compile_options
## applies to EVERY language a target compiles, and a target carrying the Windows
## VERSIONINFO resource also compiles RC -- where rc.exe treats an unknown switch
## as a fatal RC1106. Nothing here means anything to a resource compiler.

function(set_pedantic_compiler_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_warnings
            -Werror
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
        )
        if(MSVC)
            # clang-cl interprets -Wall as MSVC's /Wall, which maps to Clang's -Weverything.
            # Use /W4 instead, which correctly maps to Clang's -Wall -Wextra.
            list(APPEND _warnings /W4)
            # Suppress backward-compatibility warnings irrelevant for a C++23 codebase.
            list(APPEND _warnings
                -Wno-c++98-compat-pedantic
                -Wno-pre-c++14-compat
                -Wno-pre-c++17-compat
                -Wno-pre-c++20-compat-pedantic
            )
        else()
            list(APPEND _warnings -Wall)
        endif()
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_warnings}>)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/utf-8>)
    endif()
endfunction()
