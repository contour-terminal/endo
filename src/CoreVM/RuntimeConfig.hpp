// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace CoreVM
{

/// Configuration options for runtime behavior.
///
/// These settings control debugging, error handling, type checking, and
/// garbage collection behavior. They can be modified before execution
/// but should not be changed during execution.
struct RuntimeConfig
{
    /// Controls how the ? operator reports error propagation.
    enum class ErrorPropagation : uint8_t
    {
        Silent,  ///< Just propagate, no logging
        Trace,   ///< Log propagation path (function, location)
        Verbose, ///< Log with values and full context
    };

    /// Error propagation behavior for the ? operator.
    /// Default: Trace - provides useful debugging info without excessive output.
    ErrorPropagation errorPropagation = ErrorPropagation::Trace;

    /// Whether to perform runtime type checks on object operations.
    /// When enabled, operations like ? will verify the object is the expected type.
    /// Default: true - catches type errors early, slight performance cost.
    bool typeChecksEnabled = true;

    /// Whether the backup garbage collector is enabled.
    /// The GC handles reference cycles that can't be collected by refcounting alone.
    /// Default: true - recommended for long-running sessions (interactive shells).
    bool gcEnabled = true;

    /// Number of object allocations between GC cycles.
    /// Lower values mean more frequent GC (less memory, more CPU).
    /// Higher values mean less frequent GC (more memory, less CPU).
    /// Default: 10000 - reasonable balance for interactive use.
    size_t gcThreshold = 10000;

    /// Whether to run GC when the object pool exceeds this many bytes.
    /// This provides a memory-based trigger in addition to allocation count.
    /// Default: 10MB
    size_t gcMemoryThreshold = 10 * 1024 * 1024;

    /// Whether debug mode is enabled.
    /// In debug mode, additional validation and logging is performed.
    /// Default: false
    bool debugMode = false;

    /// Maximum recursion depth for function calls.
    /// Prevents stack overflow from infinite recursion.
    /// Default: 1000
    size_t maxRecursionDepth = 1000;

    /// Creates a default configuration suitable for interactive use.
    static RuntimeConfig defaultConfig() { return RuntimeConfig {}; }

    /// Creates a configuration optimized for performance (less safety checks).
    static RuntimeConfig performanceConfig()
    {
        RuntimeConfig config;
        config.errorPropagation = ErrorPropagation::Silent;
        config.typeChecksEnabled = false;
        config.gcThreshold = 50000;
        config.debugMode = false;
        return config;
    }

    /// Creates a configuration with maximum debugging enabled.
    static RuntimeConfig debugConfig()
    {
        RuntimeConfig config;
        config.errorPropagation = ErrorPropagation::Verbose;
        config.typeChecksEnabled = true;
        config.gcThreshold = 1000; // More frequent GC to catch issues
        config.debugMode = true;
        return config;
    }
};

} // namespace CoreVM
