# Standalone Endo test runner for .endo test files.
# Executes test files with metadata directives (# expect:, # mode:, etc.)
# without requiring Catch2.
#
# NOTE: TestHelper.cpp is compiled as shared source into this target.
# It must NOT include any Catch2 headers.

if(ENDO_TESTING)
    add_executable(endo-test
        main.cpp
        TestFileParser.hpp TestFileParser.cpp
        TestExecutor.hpp TestExecutor.cpp
        TestReporter.hpp TestReporter.cpp
        # Reuse TestRuntime from endo-language (no Catch2 dependency)
        ${CMAKE_CURRENT_SOURCE_DIR}/../endo-language/TestHelper.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../endo-language/TestHelper.cpp
    )
    target_link_libraries(endo-test PRIVATE endo endo-platform endo-shell CoreVM crispy::core)
    target_include_directories(endo-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_definitions(endo-test PRIVATE
        ENDO_TEST_DIR="${CMAKE_SOURCE_DIR}/tests"
        ENDO_DEFINITIONS_DIR="${CMAKE_SOURCE_DIR}/data/definitions"
        ENDO_STDLIB_DIR="${CMAKE_SOURCE_DIR}/data/stdlib"
        ENDO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME endo-test COMMAND endo-test --dir "${CMAKE_SOURCE_DIR}/tests")
    add_test(NAME endo-examples COMMAND endo-test --dir "${CMAKE_SOURCE_DIR}/examples")
    add_test(NAME check-endo-formatting COMMAND $<TARGET_FILE:endo-bin> format --check --recursive "${CMAKE_SOURCE_DIR}/tests")
    set_tests_properties(check-endo-formatting PROPERTIES TIMEOUT 60)
    enable_sanitizers(endo-test)
endif()
