# Endo HTTP Client Library
# RAII wrapper around libcurl for HTTP requests.

add_library(endo-http STATIC
    HttpClient.hpp
    HttpClient.cpp
    LocalTcpListener.hpp
    LocalTcpListener.cpp
)

target_link_libraries(endo-http PUBLIC CURL::libcurl)
target_include_directories(endo-http PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Enable sanitizers and clang-tidy for the production library (always).
enable_sanitizers(endo-http)
enable_clang_tidy(endo-http)

# Unit tests
if(ENDO_TESTING)
    add_executable(test-endo-http
        test_main.cpp
        HttpClient_test.cpp
    )
    target_link_libraries(test-endo-http endo-http Catch2::Catch2)
    add_test(NAME test-endo-http COMMAND test-endo-http)
    enable_sanitizers(test-endo-http)
    enable_clang_tidy(test-endo-http)
endif()
