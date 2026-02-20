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
    ProcessProvider.hpp
    FileInfoProvider.hpp
    SignalHandler.hpp
    SignalHandler.cpp
    testing/TestEnvironmentProvider.hpp
    testing/MockProcessManager.hpp
    testing/MockPipe.hpp
    testing/MockProcessProvider.hpp
    testing/MockFileInfoProvider.hpp
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
else()
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
target_link_libraries(endo-platform PUBLIC crispy::core)

enable_sanitizers(endo-platform)

# Tests
add_executable(test-endo-platform
    test_main.cpp
    PlatformError_test.cpp
    Process_test.cpp
    Pipe_test.cpp
    EnvironmentProvider_test.cpp
    Wakeup_test.cpp
    MessageQueue_test.cpp
)

target_include_directories(test-endo-platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test-endo-platform endo-platform Catch2::Catch2)
add_test(NAME test-endo-platform COMMAND test-endo-platform)
enable_sanitizers(test-endo-platform)
