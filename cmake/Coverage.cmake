## Adds code coverage support.

option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)

function(enable_coverage target)
    if(NOT ENABLE_COVERAGE)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_options(${target} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

# Locate the llvm-profdata and llvm-cov tools, preferring the version matching the compiler.
macro(_find_llvm_coverage_tools)
    if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
        # Extract compiler version major (e.g. "21" from clang++-21).
        string(REGEX MATCH "[0-9]+" _clang_major "${CMAKE_CXX_COMPILER_VERSION}")

        find_program(LLVM_PROFDATA NAMES llvm-profdata-${_clang_major} llvm-profdata)
        find_program(LLVM_COV NAMES llvm-cov-${_clang_major} llvm-cov)

        if(NOT LLVM_PROFDATA)
            message(WARNING "llvm-profdata not found; coverage target will not work")
        endif()
        if(NOT LLVM_COV)
            message(WARNING "llvm-cov not found; coverage target will not work")
        endif()
    endif()
endmacro()

function(add_coverage_target test_target)
    if(NOT ENABLE_COVERAGE)
        return()
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        _find_llvm_coverage_tools()

        add_custom_target(coverage
            # Run the test binary with profiling enabled.
            COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=coverage.profraw $<TARGET_FILE:${test_target}>
            # Merge raw profile data.
            COMMAND ${LLVM_PROFDATA} merge -sparse coverage.profraw -o coverage.profdata
            # Print a human-readable summary to the console.
            COMMAND ${LLVM_COV} report $<TARGET_FILE:${test_target}> -instr-profile=coverage.profdata
                -ignore-filename-regex=_deps/
                -ignore-filename-regex=/tests/
                -ignore-filename-regex=.cpm-cache/
            # Generate HTML report.
            COMMAND ${LLVM_COV} show $<TARGET_FILE:${test_target}> -instr-profile=coverage.profdata
                -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage
                -ignore-filename-regex=_deps/
                -ignore-filename-regex=/tests/
                -ignore-filename-regex=.cpm-cache/
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            DEPENDS ${test_target}
            COMMENT "Generating code coverage report"
        )
    endif()
endfunction()
