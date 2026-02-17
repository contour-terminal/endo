# Endo Agent Library
# LLM provider abstraction layer and agent mode UX for multi-provider AI agent support.

add_library(endo-agent STATIC
    Types.hpp
    LlmProvider.hpp
    AgentConfig.hpp AgentConfig.cpp
    ClaudeProvider.hpp ClaudeProvider.cpp
    OpenAiProvider.hpp OpenAiProvider.cpp
    GeminiProvider.hpp GeminiProvider.cpp
    ProviderFactory.hpp ProviderFactory.cpp
    ConversationHistory.hpp ConversationHistory.cpp
    TokenEstimator.hpp TokenEstimator.cpp
    ConversationCompactor.hpp ConversationCompactor.cpp
    ProjectFileTree.hpp ProjectFileTree.cpp
    ProjectContextLoader.hpp ProjectContextLoader.cpp
    SystemPromptBuilder.hpp SystemPromptBuilder.cpp
    AgentSession.hpp AgentSession.cpp
    AgentInputComponent.hpp AgentInputComponent.cpp
    AgentResponseRenderer.hpp AgentResponseRenderer.cpp
    tools/AgentTool.hpp
    tools/ToolRegistry.hpp tools/ToolRegistry.cpp
    tools/ReadFileTool.hpp tools/ReadFileTool.cpp
    tools/WriteFileTool.hpp tools/WriteFileTool.cpp
    tools/EditFileTool.hpp tools/EditFileTool.cpp
    tools/GlobTool.hpp tools/GlobTool.cpp
    tools/GrepTool.hpp tools/GrepTool.cpp
    tools/ShellExecuteTool.hpp tools/ShellExecuteTool.cpp
    tools/GitTool.hpp tools/GitTool.cpp
)

target_link_libraries(endo-agent PUBLIC
    endo-http
    tui
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
    ConversationHistory_test.cpp
    TokenEstimator_test.cpp
    ConversationCompactor_test.cpp
    ProjectFileTree_test.cpp
    ProjectContextLoader_test.cpp
    SystemPromptBuilder_test.cpp
    AgentSession_test.cpp
    tools/ToolRegistry_test.cpp
    tools/ReadFileTool_test.cpp
    tools/WriteFileTool_test.cpp
    tools/EditFileTool_test.cpp
    tools/GlobTool_test.cpp
    tools/GrepTool_test.cpp
    tools/ShellExecuteTool_test.cpp
    tools/GitTool_test.cpp
)
target_link_libraries(test-endo-agent endo-agent Catch2::Catch2)
add_test(NAME test-endo-agent COMMAND test-endo-agent)

# Enable sanitizers
enable_sanitizers(endo-agent)
enable_sanitizers(test-endo-agent)
