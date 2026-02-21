# Endo Agent Library
# LLM provider abstraction layer and agent mode UX for multi-provider AI agent support.

add_library(endo-agent STATIC
    Types.hpp Types.cpp
    Plan.hpp
    AgentConfig.hpp AgentConfig.cpp

    # Auth
    auth/OAuthFlow.hpp auth/OAuthFlow.cpp
    auth/OAuthCallbackServer.hpp auth/OAuthCallbackServer.cpp
    auth/TerminalInput.hpp auth/TerminalInput.cpp
    auth/LoginCommand.hpp auth/LoginCommand.cpp

    # Conversation
    conversation/ConversationHistory.hpp conversation/ConversationHistory.cpp
    conversation/ConversationHistoryStore.hpp conversation/ConversationHistoryStore.cpp
    conversation/TokenEstimator.hpp conversation/TokenEstimator.cpp
    conversation/ConversationCompactor.hpp conversation/ConversationCompactor.cpp

    # Context
    context/FileReferenceExpander.hpp context/FileReferenceExpander.cpp
    context/ProjectFileTree.hpp context/ProjectFileTree.cpp
    context/ProjectContextLoader.hpp context/ProjectContextLoader.cpp
    context/SystemPromptBuilder.hpp context/SystemPromptBuilder.cpp

    # Commands
    commands/SlashCommand.hpp
    commands/SlashCommandRegistry.hpp commands/SlashCommandRegistry.cpp
    commands/SlashCommands.hpp commands/SlashCommands.cpp
    commands/SlashCommandCompleter.hpp commands/SlashCommandCompleter.cpp
    commands/FilePathCompleter.hpp commands/FilePathCompleter.cpp
    commands/AgentHistoryProvider.hpp commands/AgentHistoryProvider.cpp

    # Session
    session/AgentMessages.hpp
    session/AgentManager.hpp
    session/AgentWorker.hpp session/AgentWorker.cpp
    session/AgentSession.hpp session/AgentSession.cpp
    session/PlanExecutor.hpp session/PlanExecutor.cpp

    # Tracing
    tracing/AgentTracer.hpp tracing/AgentTracer.cpp
    tracing/TraceReplay.hpp tracing/TraceReplay.cpp

    # UI
    ui/AgentInputComponent.hpp ui/AgentInputComponent.cpp
    ui/AgentResponseRenderer.hpp ui/AgentResponseRenderer.cpp

    # Providers
    providers/LlmProvider.hpp
    providers/ClaudeProvider.hpp providers/ClaudeProvider.cpp
    providers/OpenAiProvider.hpp providers/OpenAiProvider.cpp
    providers/GeminiProvider.hpp providers/GeminiProvider.cpp
    providers/ProviderFactory.hpp providers/ProviderFactory.cpp

    # Tools
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
    tools/DiffRenderer.hpp tools/DiffRenderer.cpp

    # MCP
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

    # Auth tests
    auth/OAuthFlow_test.cpp
    auth/OAuthCallbackServer_test.cpp
    auth/LoginCommand_test.cpp

    # Conversation tests
    conversation/ConversationHistory_test.cpp
    conversation/ConversationHistoryStore_test.cpp
    conversation/TokenEstimator_test.cpp
    conversation/ConversationCompactor_test.cpp

    # Context tests
    context/FileReferenceExpander_test.cpp
    context/ProjectFileTree_test.cpp
    context/ProjectContextLoader_test.cpp
    context/SystemPromptBuilder_test.cpp

    # Commands tests
    commands/SlashCommandRegistry_test.cpp
    commands/SlashCommands_test.cpp
    commands/SlashCommandCompleter_test.cpp
    commands/FilePathCompleter_test.cpp
    commands/AgentHistoryProvider_test.cpp

    # Session tests
    session/AgentWorker_test.cpp
    session/AgentSession_test.cpp
    session/PlanExecutor_test.cpp

    # Tracing tests
    tracing/AgentTracer_test.cpp

    # Provider tests
    providers/ProviderModels_test.cpp
    providers/ClaudeProvider_test.cpp
    providers/OpenAiProvider_test.cpp
    providers/GeminiProvider_test.cpp
    providers/ProviderFactory_test.cpp

    # Tool tests
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
    tools/DiffRenderer_test.cpp

    # MCP tests
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
