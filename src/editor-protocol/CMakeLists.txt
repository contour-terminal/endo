# Endo Editor Protocol Library
# Provides shared transport, types, and document management for editor protocol servers (LSP, DAP).

add_library(endo-editor-protocol STATIC
    JsonTransport.hpp
    JsonTransport.cpp
    EditorTypes.hpp
    DocumentStore.hpp
    DocumentStore.cpp
    StubRuntime.hpp
    TestHelpers.hpp
)
target_link_libraries(endo-editor-protocol PUBLIC endo nlohmann_json::nlohmann_json)
target_include_directories(endo-editor-protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
enable_sanitizers(endo-editor-protocol)
