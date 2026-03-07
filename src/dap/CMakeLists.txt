# Endo DAP Server Library
# Provides Debug Adapter Protocol support for Endo shell scripts.
# Reuses the endo language library for parsing, IR generation, and execution.

add_library(endo-dap STATIC
    # DAP protocol types
    DapTypes.hpp

    # Breakpoint management
    BreakpointManager.hpp
    BreakpointManager.cpp

    # Condition evaluation
    ConditionEvaluator.hpp
    ConditionEvaluator.cpp

    # Debug session
    DebugSession.hpp
    DebugSession.cpp

    # Server core
    DapServer.hpp
    DapServer.cpp
)

target_link_libraries(endo-dap PUBLIC endo-editor-protocol endo nlohmann_json::nlohmann_json CoreVM)
target_include_directories(endo-dap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Tests
add_executable(test-endo-dap
    test_main.cpp
    DapServer_test.cpp
)
target_link_libraries(test-endo-dap endo-dap Catch2::Catch2)
add_test(NAME test-endo-dap COMMAND test-endo-dap)

# Enable sanitizers and clang-tidy for all targets
enable_sanitizers(endo-dap)
enable_clang_tidy(endo-dap)
enable_sanitizers(test-endo-dap)
enable_clang_tidy(test-endo-dap)
