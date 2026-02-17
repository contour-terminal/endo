add_library(tui
    Box.cpp
    Buffer.cpp
    Canvas.cpp
    Cell.cpp
    Component.cpp
    CompletionPopup.cpp
    Dialog.cpp
    HoverState.cpp
    InputField.cpp
    KeyBindings.cpp
    List.cpp
    LogPanel.cpp
    MarkdownRenderer.cpp
    Screen.cpp
    Sixel.cpp
    Spinner.cpp
    StatusBar.cpp
    StyledText.cpp
    Text.cpp
    Theme.cpp
    Tooltip.cpp
    VtParser.cpp
    completer/Completer.cpp
    completer/FuzzyMatch.cpp
    completer/SmartCaseMatch.cpp
)

# Platform-specific terminal implementations
if(WIN32)
    target_sources(tui PRIVATE
        platform/TerminalInputWin32.cpp
        platform/TerminalOutputWin32.cpp
        platform/TerminalWin32.cpp
    )
else()
    target_sources(tui PRIVATE
        platform/TerminalInput.cpp
        platform/TerminalOutput.cpp
        platform/Terminal.cpp
    )
endif()

target_include_directories(tui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(tui PUBLIC
    unicode::unicode
)

target_compile_features(tui PUBLIC cxx_std_23)

set_pedantic_compiler_warnings(tui)
enable_sanitizers(tui)

# Tests
add_executable(test-tui
    test_main.cpp
    InputField_test.cpp
    KeyBindings_test.cpp
    Renderer_test.cpp
    Completer_test.cpp
    CompletionPopup_test.cpp
    Screen_test.cpp
    SmartCaseMatch_test.cpp
    FuzzyMatch_test.cpp
    Unicode_test.cpp
    VtParser_test.cpp
)
target_link_libraries(test-tui tui Catch2::Catch2)
add_test(NAME test-tui COMMAND test-tui)
enable_sanitizers(test-tui)
