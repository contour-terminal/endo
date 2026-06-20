# Endo Coroutine Primitives Library
# TUI-agnostic C++23 coroutine building blocks: Task<T>, whenAll, awaitable
# concepts, and std::stop_token-based cancellation. Header-only, so this is an
# INTERFACE library; the headers are compiled (and linted/sanitized) through the
# test-coro executable below.

add_library(endo-coro INTERFACE)

target_sources(endo-coro INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES
        Task.hpp
        WhenAll.hpp
        Awaitable.hpp
        Cancellation.hpp
)

target_include_directories(endo-coro INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(endo-coro INTERFACE cxx_std_23)

# Tests
if(ENDO_TESTING)
    add_executable(test-coro
        test_main.cpp
        Task_test.cpp
    )

    target_include_directories(test-coro PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test-coro endo-coro Catch2::Catch2)
    add_test(NAME test-coro COMMAND test-coro)
    enable_sanitizers(test-coro)
    enable_clang_tidy(test-coro)
endif()
