add_library(tui
    Box.cpp
    Dialog.cpp
    InputField.cpp
    List.cpp
    LogPanel.cpp
    MarkdownRenderer.cpp
    Sixel.cpp
    Spinner.cpp
    StatusBar.cpp
    Terminal.cpp
    TerminalInput.cpp
    TerminalOutput.cpp
    Text.cpp
    Theme.cpp
    VtParser.cpp
)

target_include_directories(tui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(tui PUBLIC
    unicode::unicode
)

target_compile_features(tui PUBLIC cxx_std_23)

set_pedantic_compiler_warnings(tui)
enable_sanitizers(tui)
