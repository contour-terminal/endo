# Endo Agent Library
# LLM provider abstraction layer for multi-provider AI agent support.

add_library(endo-agent STATIC
    Types.hpp
    LlmProvider.hpp
    AgentConfig.hpp AgentConfig.cpp
    ClaudeProvider.hpp ClaudeProvider.cpp
    OpenAiProvider.hpp OpenAiProvider.cpp
    GeminiProvider.hpp GeminiProvider.cpp
    ProviderFactory.hpp ProviderFactory.cpp
)

target_link_libraries(endo-agent PUBLIC
    endo-http
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    crispy::core
)
target_include_directories(endo-agent PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Unit tests
add_executable(test-endo-agent
    test_main.cpp
    Types_test.cpp
    AgentConfig_test.cpp
    ClaudeProvider_test.cpp
    OpenAiProvider_test.cpp
    GeminiProvider_test.cpp
    ProviderFactory_test.cpp
)
target_link_libraries(test-endo-agent endo-agent Catch2::Catch2)
add_test(NAME test-endo-agent COMMAND test-endo-agent)

# Enable sanitizers
enable_sanitizers(endo-agent)
enable_sanitizers(test-endo-agent)
