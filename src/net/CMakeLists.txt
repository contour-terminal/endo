# Endo Async Network Library
# Coroutine-native sockets/listeners driven by the TUI runtime's reactor:
# ISocket/IListener return endo::coro::Task<IoResult>, with non-blocking I/O that
# parks the calling coroutine on waitReadable/waitWritable rather than blocking a
# thread. Backs the httpServe builtin and is the foundation for future async
# network consumers.

add_library(endo-net STATIC
    IoResult.hpp
    ISocket.hpp
    IListener.hpp
    Sockets.hpp
    testing/InMemoryTransport.hpp
    testing/InMemoryTransport.cpp
)

# Platform-specific socket implementations.
if(WIN32)
    target_sources(endo-net PRIVATE
        windows/WindowsSocket.hpp
        windows/WindowsSocket.cpp
        windows/WindowsListener.hpp
        windows/WindowsListener.cpp
        windows/SocketsWin32.cpp
    )
else()
    target_sources(endo-net PRIVATE
        posix/PosixSocket.hpp
        posix/PosixSocket.cpp
        posix/PosixListener.hpp
        posix/PosixListener.cpp
        posix/SocketsPosix.cpp
    )
endif()

target_include_directories(endo-net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(endo-net PUBLIC endo-coro endo-platform tui)
if(WIN32)
    target_link_libraries(endo-net PUBLIC ws2_32)
endif()

enable_sanitizers(endo-net)
enable_clang_tidy(endo-net)

# Tests
if(ENDO_TESTING)
    add_executable(test-endo-net
        test_main.cpp
        Socket_test.cpp
    )

    target_include_directories(test-endo-net PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test-endo-net endo-net Catch2::Catch2)
    add_test(NAME test-endo-net COMMAND test-endo-net)
    enable_sanitizers(test-endo-net)
    enable_clang_tidy(test-endo-net)
endif()
