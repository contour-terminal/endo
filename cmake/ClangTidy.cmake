## Enables clang-tidy integration.

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

function(enable_clang_tidy target)
    if(NOT ENABLE_CLANG_TIDY)
        return()
    endif()

    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CLANG_TIDY_CMD "${CLANG_TIDY_EXE}")

        # When using Clang with GCC's libstdc++, clang-tidy's internal parser may not
        # find the same GCC toolchain as the compiler. Detect the GCC install directory
        # from the compiler and pass it explicitly to clang-tidy.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-search-dirs
                OUTPUT_VARIABLE _search_dirs
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            # Extract the GCC installation directory from Clang's verbose output
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -v -E -x c++ /dev/null
                OUTPUT_QUIET
                ERROR_VARIABLE _clang_verbose
                ERROR_STRIP_TRAILING_WHITESPACE
            )
            string(REGEX MATCH "Selected GCC installation: ([^\n]+)" _gcc_match "${_clang_verbose}")
            if(CMAKE_MATCH_1)
                list(APPEND CLANG_TIDY_CMD "--extra-arg=--gcc-install-dir=${CMAKE_MATCH_1}")
                message(STATUS "clang-tidy: using GCC install dir ${CMAKE_MATCH_1}")
            endif()
        endif()

        set_target_properties(${target} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_CMD}"
        )
        message(STATUS "clang-tidy enabled for ${target}: ${CLANG_TIDY_CMD}")
    else()
        message(WARNING "clang-tidy requested but not found")
    endif()
endfunction()
