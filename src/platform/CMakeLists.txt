# Endo Platform Library
# Provides platform-agnostic abstractions for OS operations (processes, pipes,
# environment, signals, file/process listing).

add_library(endo-platform STATIC
    PlatformError.hpp
    Types.hpp
    WaitResult.hpp
    Wakeup.hpp
    MessageQueue.hpp
    Process.hpp
    Process.cpp
    Pipe.hpp
    Pipe.cpp
    EnvironmentProvider.hpp
    UserPaths.hpp
    PathUtils.hpp
    PathUtils.cpp
    ProcessProvider.hpp
    ProcessProvider.cpp
    FileSystem.hpp
    NativeFileSystem.hpp
    NativeFileSystem.cpp
    FileInfoProvider.hpp
    GlobMatch.hpp
    GlobMatch.cpp
    ProjectFileTree.hpp
    ProjectFileTree.cpp
    SignalHandler.hpp
    SignalHandler.cpp
    InstallPaths.hpp
    InstallPaths.cpp
    testing/TestEnvironmentProvider.hpp
    testing/MockProcessManager.hpp
    testing/MockPipe.hpp
    testing/MockProcessProvider.hpp
    testing/MockFileInfoProvider.hpp
    testing/InMemoryFileSystem.hpp
    testing/InMemoryFileSystem.cpp
)

# Platform-specific implementations
if(WIN32)
    target_sources(endo-platform PRIVATE
        windows/WindowsProcess.cpp
        windows/WindowsPipe.cpp
        windows/WindowsWakeup.cpp
        windows/WindowsEnvironmentProvider.hpp
        windows/WindowsEnvironmentProvider.cpp
        windows/WindowsProcessProvider.hpp
        windows/WindowsProcessProvider.cpp
        windows/WindowsFileInfoProvider.hpp
        windows/WindowsFileInfoProvider.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(endo-platform PRIVATE
        linux/LinuxWakeup.cpp
        posix/PosixEnvironmentProvider.hpp
        posix/PosixEnvironmentProvider.cpp
        linux/LinuxProcessProvider.hpp
        linux/LinuxProcessProvider.cpp
        linux/LinuxFileInfoProvider.hpp
        linux/LinuxFileInfoProvider.cpp
    )
elseif(APPLE)
    target_sources(endo-platform PRIVATE
        posix/PosixWakeup.cpp
        posix/PosixEnvironmentProvider.hpp
        posix/PosixEnvironmentProvider.cpp
        darwin/DarwinProcessProvider.hpp
        darwin/DarwinProcessProvider.cpp
        linux/LinuxFileInfoProvider.hpp
        linux/LinuxFileInfoProvider.cpp
    )
else()
    # Generic POSIX fallback (FreeBSD, etc.)
    target_sources(endo-platform PRIVATE
        posix/PosixWakeup.cpp
        posix/PosixEnvironmentProvider.hpp
        posix/PosixEnvironmentProvider.cpp
        linux/LinuxProcessProvider.hpp
        linux/LinuxProcessProvider.cpp
        linux/LinuxFileInfoProvider.hpp
        linux/LinuxFileInfoProvider.cpp
    )
endif()

target_include_directories(endo-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(endo-platform PUBLIC crispy::core Threads::Threads)
else()
    target_link_libraries(endo-platform PUBLIC crispy::core)
endif()

enable_sanitizers(endo-platform)
enable_clang_tidy(endo-platform)

# Tests
if(ENDO_TESTING)
    add_executable(test-endo-platform
        test_main.cpp
        FileSystem_test.cpp
        Generator_test.cpp
        InterruptThrottle_test.cpp
        PlatformError_test.cpp
        Process_test.cpp
        Pipe_test.cpp
        EnvironmentProvider_test.cpp
        FileInfoProvider_test.cpp
        Wakeup_test.cpp
        MessageQueue_test.cpp
        ProjectFileTree_test.cpp
        WindowsPlatform_test.cpp
        LinuxProcessProvider_test.cpp
        DarwinProcessProvider_test.cpp
    )

    target_include_directories(test-endo-platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test-endo-platform endo-platform Catch2::Catch2)
    add_test(NAME test-endo-platform COMMAND test-endo-platform)
    enable_sanitizers(test-endo-platform)
    enable_clang_tidy(test-endo-platform)
endif()
