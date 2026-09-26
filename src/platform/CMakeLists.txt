# Endo Platform Library
# Provides platform-agnostic abstractions for OS operations (processes, pipes,
# environment, signals, file/process listing).

add_library(endo-platform STATIC
    PosixCompat.hpp
    WaitResult.hpp
    Process.hpp
    Process.cpp
    Pipe.hpp
    Pipe.cpp
    ProcessProvider.hpp
    ProcessProvider.cpp
    ProjectFileTree.hpp
    ProjectFileTree.cpp
    InstallPaths.hpp
    InstallPaths.cpp
    testing/MockProcessManager.hpp
    testing/MockPipe.hpp
    testing/MockProcessProvider.hpp
)

# Platform-specific implementations
if(WIN32)
    target_sources(endo-platform PRIVATE
        windows/WindowsProcess.cpp
        windows/WindowsPipe.cpp
        windows/WindowsProcessProvider.hpp
        windows/WindowsProcessProvider.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(endo-platform PRIVATE
        linux/LinuxProcessProvider.hpp
        linux/LinuxProcessProvider.cpp
    )
elseif(APPLE)
    target_sources(endo-platform PRIVATE
        darwin/DarwinProcessProvider.hpp
        darwin/DarwinProcessProvider.cpp
    )
else()
    # Generic POSIX fallback (FreeBSD, etc.)
    target_sources(endo-platform PRIVATE
        linux/LinuxProcessProvider.hpp
        linux/LinuxProcessProvider.cpp
    )
endif()

target_include_directories(endo-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(endo-platform PUBLIC core::platform core::cli)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(endo-platform PUBLIC Threads::Threads)
endif()

enable_sanitizers(endo-platform)
enable_clang_tidy(endo-platform)

# Tests
if(ENDO_TESTING)
    add_executable(test-endo-platform
        test_main.cpp
        InterruptThrottle_test.cpp
        Process_test.cpp
        Pipe_test.cpp
        ProjectFileTree_test.cpp
        LinuxProcessProvider_test.cpp
        DarwinProcessProvider_test.cpp
    )

    target_include_directories(test-endo-platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test-endo-platform endo-platform core::testing Catch2::Catch2)
    add_test(NAME test-endo-platform COMMAND test-endo-platform)
    enable_sanitizers(test-endo-platform)
    enable_clang_tidy(test-endo-platform)
endif()
