# Endo Agent Library
# LLM provider abstraction layer and agent mode UX for multi-provider AI agent support.

add_library(endo-agent STATIC
    Types.hpp
    AgentMessages.hpp
    AgentManager.hpp
    AgentWorker.hpp AgentWorker.cpp
    providers/LlmProvider.hpp
    AgentConfig.hpp AgentConfig.cpp
    TerminalInput.hpp TerminalInput.cpp
    LoginCommand.hpp LoginCommand.cpp
    providers/ClaudeProvider.hpp providers/ClaudeProvider.cpp
    providers/OpenAiProvider.hpp providers/OpenAiProvider.cpp
    providers/GeminiProvider.hpp providers/GeminiProvider.cpp
    providers/ProviderFactory.hpp providers/ProviderFactory.cpp
    ConversationHistory.hpp ConversationHistory.cpp
    ConversationHistoryStore.hpp ConversationHistoryStore.cpp
    AgentHistoryProvider.hpp AgentHistoryProvider.cpp
    TokenEstimator.hpp TokenEstimator.cpp
    ConversationCompactor.hpp ConversationCompactor.cpp
    ProjectFileTree.hpp ProjectFileTree.cpp
    ProjectContextLoader.hpp ProjectContextLoader.cpp
    SystemPromptBuilder.hpp SystemPromptBuilder.cpp
    Plan.hpp
    AgentTracer.hpp AgentTracer.cpp
    TraceReplay.hpp TraceReplay.cpp
    AgentSession.hpp AgentSession.cpp
    PlanExecutor.hpp PlanExecutor.cpp
    AgentInputComponent.hpp AgentInputComponent.cpp
    AgentResponseRenderer.hpp AgentResponseRenderer.cpp
    SlashCommand.hpp
    SlashCommandRegistry.hpp SlashCommandRegistry.cpp
    SlashCommands.hpp SlashCommands.cpp
    SlashCommandCompleter.hpp SlashCommandCompleter.cpp
    FilePathCompleter.hpp FilePathCompleter.cpp
    tools/AgentTool.hpp
    tools/ToolRegistry.hpp tools/ToolRegistry.cpp
    tools/SubmitPlanTool.hpp tools/SubmitPlanTool.cpp
    tools/ReadFileTool.hpp tools/ReadFileTool.cpp
    tools/WriteFileTool.hpp tools/WriteFileTool.cpp
    tools/EditFileTool.hpp tools/EditFileTool.cpp
    tools/GlobTool.hpp tools/GlobTool.cpp
    tools/GrepTool.hpp tools/GrepTool.cpp
    tools/SearchTool.hpp tools/SearchTool.cpp
    tools/ShellExecuteTool.hpp tools/ShellExecuteTool.cpp
    tools/EndoExecuteTool.hpp tools/EndoExecuteTool.cpp
    tools/ExploreTool.hpp tools/ExploreTool.cpp
    tools/GitTool.hpp tools/GitTool.cpp
    tools/SaveMemoryTool.hpp tools/SaveMemoryTool.cpp
    tools/HtmlUtils.hpp tools/HtmlUtils.cpp
    tools/WebSearchTool.hpp tools/WebSearchTool.cpp
    tools/WebFetchTool.hpp tools/WebFetchTool.cpp
    tools/ListDirectoryTool.hpp tools/ListDirectoryTool.cpp
    tools/AskUserTool.hpp tools/AskUserTool.cpp
    mcp/McpError.hpp
    mcp/Transport.hpp
    mcp/StdioTransport.hpp mcp/StdioTransport.cpp
    mcp/JsonRpc.hpp mcp/JsonRpc.cpp
    mcp/McpClient.hpp mcp/McpClient.cpp
    mcp/ServerManager.hpp mcp/ServerManager.cpp
    mcp/McpToolAdapter.hpp mcp/McpToolAdapter.cpp
)

target_link_libraries(endo-agent PUBLIC
    endo-http
    endo-platform
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
    AgentWorker_test.cpp
    LoginCommand_test.cpp
    providers/ClaudeProvider_test.cpp
    providers/OpenAiProvider_test.cpp
    providers/GeminiProvider_test.cpp
    providers/ProviderFactory_test.cpp
    ConversationHistory_test.cpp
    ConversationHistoryStore_test.cpp
    AgentHistoryProvider_test.cpp
    TokenEstimator_test.cpp
    ConversationCompactor_test.cpp
    ProjectFileTree_test.cpp
    ProjectContextLoader_test.cpp
    SystemPromptBuilder_test.cpp
    AgentSession_test.cpp
    AgentTracer_test.cpp
    PlanExecutor_test.cpp
    tools/ToolRegistry_test.cpp
    tools/SubmitPlanTool_test.cpp
    tools/ReadFileTool_test.cpp
    tools/WriteFileTool_test.cpp
    tools/EditFileTool_test.cpp
    tools/GlobTool_test.cpp
    tools/GrepTool_test.cpp
    tools/SearchTool_test.cpp
    tools/ShellExecuteTool_test.cpp
    tools/EndoExecuteTool_test.cpp
    tools/ExploreTool_test.cpp
    tools/GitTool_test.cpp
    tools/SaveMemoryTool_test.cpp
    tools/HtmlUtils_test.cpp
    tools/WebSearchTool_test.cpp
    tools/WebFetchTool_test.cpp
    tools/ListDirectoryTool_test.cpp
    tools/AskUserTool_test.cpp
    SlashCommandRegistry_test.cpp
    SlashCommands_test.cpp
    SlashCommandCompleter_test.cpp
    FilePathCompleter_test.cpp
    mcp/JsonRpc_test.cpp
    mcp/McpClient_test.cpp
    mcp/ServerManager_test.cpp
    mcp/McpToolAdapter_test.cpp
)
target_link_libraries(test-endo-agent endo-agent Catch2::Catch2)
add_test(NAME test-endo-agent COMMAND test-endo-agent)

# Enable sanitizers
enable_sanitizers(endo-agent)
enable_sanitizers(test-endo-agent)
