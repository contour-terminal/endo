// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <editor-protocol/JsonTransport.hpp>

#include <optional>
#include <ostream>
#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// RAII progress reporter that sends work done progress notifications.
///
/// Constructor sends `window/workDoneProgress/create` and `$/progress` begin.
/// `report()` sends progress reports. Destructor sends end notification.
class ProgressReporter
{
  public:
    /// Creates a progress reporter and sends begin notification.
    /// @param output The output stream for JSON-RPC messages
    /// @param token A unique progress token
    /// @param title The progress title
    ProgressReporter(std::ostream& output, std::string token, std::string title);

    /// Sends end notification.
    ~ProgressReporter();

    ProgressReporter(ProgressReporter const&) = delete;
    ProgressReporter& operator=(ProgressReporter const&) = delete;
    ProgressReporter(ProgressReporter&&) = delete;
    ProgressReporter& operator=(ProgressReporter&&) = delete;

    /// Sends a progress report.
    /// @param message The progress message
    /// @param percentage Optional percentage (0-100)
    void report(std::string const& message, std::optional<int> percentage = std::nullopt);

  private:
    std::ostream& _output;
    std::string _token;
};

} // namespace endo::lsp
