add_library(tui
    SgrBuilder.hpp SgrBuilder.cpp
    Box.cpp
    Buffer.cpp
    Canvas.cpp
    Cell.cpp
    CommandPalettePopup.cpp
    CommandRegistry.cpp
    FuzzyPickerPopup.cpp
    Component.cpp
    CompletionPopup.cpp
    Dialog.cpp
    GenericSyntaxHighlighter.cpp
    HoverState.cpp
    ImageLoader.cpp
    QuestionComponent.cpp
    InputField.cpp
    KeyBindings.cpp
    List.cpp
    LogPanel.cpp
    MarkdownRenderer.cpp
    MarkdownTable.cpp
    MockTerminalOutput.cpp
    Screen.cpp
    SemanticBlockClient.cpp
    Sixel.cpp
    Spinner.cpp
    StbImageImpl.cpp
    StatusBar.cpp
    StyledText.cpp
    Text.cpp
    Theme.cpp
    Tooltip.cpp
    TreeTableView.cpp
    Unicode.cpp
    VtParser.cpp
    completer/Completer.cpp
    completer/FuzzyMatch.cpp
    completer/SmartCaseMatch.cpp
    platform/TerminalShared.cpp
    runtime/TuiRuntime.cpp
)

# Platform-specific terminal implementations
if(WIN32)
    target_sources(tui PRIVATE
        platform/TerminalInputWin32.cpp
        platform/TerminalOutputWin32.cpp
        platform/TerminalWin32.cpp
        runtime/platform/TerminalEventSourceWin32.cpp
    )
else()
    target_sources(tui PRIVATE
        platform/TerminalInput.cpp
        platform/TerminalOutput.cpp
        platform/Terminal.cpp
        runtime/platform/TerminalEventSourcePosix.cpp
    )
endif()

target_include_directories(tui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(tui PUBLIC
    unicode::unicode
    endo-platform
    endo-coro
    stb_image
)

target_compile_features(tui PUBLIC cxx_std_23)

set_pedantic_compiler_warnings(tui)
enable_sanitizers(tui)
enable_clang_tidy(tui)

# Disable sanitizers for third-party stb code (known UBSan false positives in stb_image_resize2)
set_source_files_properties(StbImageImpl.cpp PROPERTIES COMPILE_OPTIONS "-fno-sanitize=undefined")

# Tests
if(ENDO_TESTING)
    add_executable(test-tui
        test_main.cpp
        CommandPalettePopup_test.cpp
        CommandRegistry_test.cpp
        GenericSyntaxHighlighter_test.cpp
        InputField_test.cpp
        KeyBindings_test.cpp
        Renderer_test.cpp
        Completer_test.cpp
        CompletionPopup_test.cpp
        FuzzyPickerPopup_test.cpp
        PopupKeyDispatch_test.cpp
        ScrollableSelection_test.cpp
        Screen_test.cpp
        SmartCaseMatch_test.cpp
        FuzzyMatch_test.cpp
        MarkdownTable_test.cpp
        MarkdownRenderer_test.cpp
        QuestionComponent_test.cpp
        StyledText_test.cpp
        TreeTableView_test.cpp
        Unicode_test.cpp
        VtParser_test.cpp
        ImageLoader_test.cpp
        SemanticBlockClient_test.cpp
        runtime/TuiRuntime_test.cpp
        runtime/Modal_test.cpp
    )
    target_link_libraries(test-tui tui Catch2::Catch2)
    add_test(NAME test-tui COMMAND test-tui)
    enable_sanitizers(test-tui)
    enable_clang_tidy(test-tui)
endif()
