// SPDX-License-Identifier: Apache-2.0
#include "ProgressReporter.hpp"

namespace endo::lsp
{

using namespace endo::editor_protocol;

ProgressReporter::ProgressReporter(std::ostream& output, std::string token, std::string title):
    _output(output), _token(std::move(token))
{
    // Send window/workDoneProgress/create request
    // Note: This is a server→client request, but we send it as a notification for simplicity
    // since we don't need to wait for the response in the current implementation.
    writeMessage(_output,
                 makeNotification("window/workDoneProgress/create", nlohmann::json { { "token", _token } }));

    // Send begin
    auto begin = WorkDoneProgressBegin { .title = std::move(title) };
    writeMessage(_output,
                 makeNotification("$/progress", nlohmann::json { { "token", _token }, { "value", begin } }));
}

ProgressReporter::~ProgressReporter()
{
    auto end = WorkDoneProgressEnd {};
    writeMessage(_output,
                 makeNotification("$/progress", nlohmann::json { { "token", _token }, { "value", end } }));
}

void ProgressReporter::report(std::string const& message, std::optional<int> percentage)
{
    auto report = WorkDoneProgressReport { .message = message, .percentage = percentage };
    writeMessage(_output,
                 makeNotification("$/progress", nlohmann::json { { "token", _token }, { "value", report } }));
}

} // namespace endo::lsp
