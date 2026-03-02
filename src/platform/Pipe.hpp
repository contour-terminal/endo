// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <memory>

#include <platform/PlatformError.hpp>
#include <platform/Types.hpp>

namespace endo::platform
{

/// Abstract interface for platform-independent pipe operations.
///
/// Pipes provide unidirectional communication channels, typically used
/// for inter-process communication. This interface abstracts the platform-
/// specific details of pipe creation and management.
class Pipe
{
  public:
    virtual ~Pipe() = default;

    Pipe() = default;
    Pipe(Pipe const&) = delete;
    Pipe& operator=(Pipe const&) = delete;
    Pipe(Pipe&&) = default;
    Pipe& operator=(Pipe&&) = default;

    /// Returns the native handle for the read end of the pipe.
    [[nodiscard]] virtual NativeHandle reader() const noexcept = 0;

    /// Returns the native handle for the write end of the pipe.
    [[nodiscard]] virtual NativeHandle writer() const noexcept = 0;

    /// Releases ownership of the read handle and returns it.
    /// After calling this method, the read handle will not be closed by the pipe.
    [[nodiscard]] virtual NativeHandle releaseReader() noexcept = 0;

    /// Releases ownership of the write handle and returns it.
    /// After calling this method, the write handle will not be closed by the pipe.
    [[nodiscard]] virtual NativeHandle releaseWriter() noexcept = 0;

    /// Closes the read end of the pipe.
    virtual void closeReader() noexcept = 0;

    /// Closes the write end of the pipe.
    virtual void closeWriter() noexcept = 0;

    /// Checks if both ends of the pipe are valid.
    [[nodiscard]] virtual bool good() const noexcept = 0;
};

/// Creates a new pipe with platform-specific implementation.
///
/// @param flags Platform-specific flags for pipe creation (e.g., O_CLOEXEC on POSIX)
/// @return A unique pointer to the created pipe on success, or an error
[[nodiscard]] std::expected<std::unique_ptr<Pipe>, PlatformError> createPipe(unsigned flags = 0);

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::createPipe;
using endo::platform::Pipe;
} // namespace endo
