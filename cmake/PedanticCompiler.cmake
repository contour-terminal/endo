## Sets pedantic compiler warnings for a target.

function(set_pedantic_compiler_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
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
            target_compile_options(${target} PRIVATE /W4)
            # Suppress backward-compatibility warnings irrelevant for a C++23 codebase.
            target_compile_options(${target} PRIVATE
                -Wno-c++98-compat-pedantic
                -Wno-pre-c++14-compat
                -Wno-pre-c++17-compat
                -Wno-pre-c++20-compat-pedantic
            )
        else()
            target_compile_options(${target} PRIVATE -Wall)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4 /WX)
    endif()
endfunction()
