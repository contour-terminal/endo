// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file DapTypes.hpp
/// @brief DAP (Debug Adapter Protocol) message structures.

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo::dap
{

/// Exception breakpoint filter descriptor advertised in capabilities.
struct ExceptionBreakpointsFilter
{
    std::string filterId;
    std::string label;
    std::string description;
    bool defaultValue = false;
};

/// Server capabilities advertised during initialize handshake.
struct Capabilities
{
    std::optional<bool> supportsConfigurationDoneRequest;
    std::optional<bool> supportsFunctionBreakpoints;
    std::optional<bool> supportsBreakpointLocationsRequest;
    std::optional<bool> supportsEvaluateForHovers;
    std::optional<bool> supportsVariableType;
    std::optional<bool> supportsConditionalBreakpoints;
    std::optional<bool> supportsHitConditionalBreakpoints;
    std::optional<bool> supportsLogPoints;
    std::optional<bool> supportsSteppingGranularity;
    std::optional<bool> supportsDisassembleRequest;
    std::optional<bool> supportsSetVariable;
    std::optional<bool> supportsExceptionInfoRequest;
    std::optional<bool> supportsLoadedSourcesRequest;
    std::optional<bool> supportsTerminateRequest;
    std::optional<bool> supportsRestartRequest;
    std::optional<bool> supportsCompletionsRequest;
    std::optional<bool> supportsInstructionBreakpoints;
    std::vector<ExceptionBreakpointsFilter> exceptionBreakpointFilters;
};

/// Arguments for the DAP 'initialize' request.
struct InitializeRequestArguments
{
    std::string clientID;
    std::string clientName;
    bool linesStartAt1 = true;
    bool columnsStartAt1 = true;
    std::string pathFormat = "path";
};

/// Arguments for the DAP 'launch' request.
struct LaunchRequestArguments
{
    std::string program;
    std::vector<std::string> args;
    bool stopOnEntry = false;
    bool noDebug = false;
};

/// DAP Source reference.
struct Source
{
    std::optional<std::string> name;
    std::optional<std::string> path;
};

/// A breakpoint requested by the client (line-based).
struct SourceBreakpoint
{
    int line = 0;
    std::optional<int> column;
    std::optional<std::string> condition;
    std::optional<std::string> hitCondition;
    std::optional<std::string> logMessage;
};

/// A breakpoint requested by the client (function name-based).
struct FunctionBreakpoint
{
    std::string name;
    std::optional<std::string> condition;
    std::optional<std::string> hitCondition;
};

/// A breakpoint response sent to the client.
struct Breakpoint
{
    int id = 0;
    bool verified = false;
    std::optional<std::string> message;
    std::optional<std::string> sourcePath;
    std::optional<int> line;
    std::optional<int> column;
    std::optional<int> endLine;
    std::optional<int> endColumn;
};

/// A possible breakpoint location returned by breakpointLocations request.
struct BreakpointLocation
{
    int line = 0;
    std::optional<int> column;
    std::optional<int> endLine;
    std::optional<int> endColumn;
};

/// A single stack frame in the call stack.
struct StackFrame
{
    int id = 0;
    std::string name;
    Source source;
    int line = 0;
    int column = 0;
    std::optional<std::string> instructionPointerReference;
};

/// A scope within a stack frame.
struct Scope
{
    std::string name;
    int variablesReference = 0;
    bool expensive = false;
};

/// A variable within a scope.
struct Variable
{
    std::string name;
    std::string value;
    std::string type;
    int variablesReference = 0;
};

/// Result of evaluating an expression.
struct EvaluateResult
{
    std::string result;
    std::string type;
    int variablesReference = 0;
};

/// A completion item for debug console autocomplete.
struct CompletionItem
{
    std::string label;
    std::string type; ///< "variable", "function", "keyword", etc.
};

/// An instruction breakpoint requested by address.
struct InstructionBreakpoint
{
    std::string instructionReference; ///< Hex-encoded address
    std::optional<int> offset;
    std::optional<std::string> condition;
    std::optional<std::string> hitCondition;
};

// --- nlohmann JSON serialization ---

inline void to_json(nlohmann::json& j, ExceptionBreakpointsFilter const& f)
{
    j = nlohmann::json {
        { "filter", f.filterId },
        { "label", f.label },
    };
    if (!f.description.empty())
        j["description"] = f.description;
    if (f.defaultValue)
        j["default"] = f.defaultValue;
}

inline void from_json(nlohmann::json const& j, ExceptionBreakpointsFilter& f)
{
    f.filterId = j.at("filter").get<std::string>();
    f.label = j.at("label").get<std::string>();
    if (j.contains("description"))
        f.description = j.at("description").get<std::string>();
    if (j.contains("default"))
        f.defaultValue = j.at("default").get<bool>();
}

inline void to_json(nlohmann::json& j, Capabilities const& c)
{
    j = nlohmann::json::object();
    if (c.supportsConfigurationDoneRequest.has_value())
        j["supportsConfigurationDoneRequest"] = *c.supportsConfigurationDoneRequest;
    if (c.supportsFunctionBreakpoints.has_value())
        j["supportsFunctionBreakpoints"] = *c.supportsFunctionBreakpoints;
    if (c.supportsBreakpointLocationsRequest.has_value())
        j["supportsBreakpointLocationsRequest"] = *c.supportsBreakpointLocationsRequest;
    if (c.supportsEvaluateForHovers.has_value())
        j["supportsEvaluateForHovers"] = *c.supportsEvaluateForHovers;
    if (c.supportsVariableType.has_value())
        j["supportsVariableType"] = *c.supportsVariableType;
    if (c.supportsConditionalBreakpoints.has_value())
        j["supportsConditionalBreakpoints"] = *c.supportsConditionalBreakpoints;
    if (c.supportsHitConditionalBreakpoints.has_value())
        j["supportsHitConditionalBreakpoints"] = *c.supportsHitConditionalBreakpoints;
    if (c.supportsLogPoints.has_value())
        j["supportsLogPoints"] = *c.supportsLogPoints;
    if (c.supportsSteppingGranularity.has_value())
        j["supportsSteppingGranularity"] = *c.supportsSteppingGranularity;
    if (c.supportsDisassembleRequest.has_value())
        j["supportsDisassembleRequest"] = *c.supportsDisassembleRequest;
    if (c.supportsSetVariable.has_value())
        j["supportsSetVariable"] = *c.supportsSetVariable;
    if (c.supportsExceptionInfoRequest.has_value())
        j["supportsExceptionInfoRequest"] = *c.supportsExceptionInfoRequest;
    if (c.supportsLoadedSourcesRequest.has_value())
        j["supportsLoadedSourcesRequest"] = *c.supportsLoadedSourcesRequest;
    if (c.supportsTerminateRequest.has_value())
        j["supportsTerminateRequest"] = *c.supportsTerminateRequest;
    if (c.supportsRestartRequest.has_value())
        j["supportsRestartRequest"] = *c.supportsRestartRequest;
    if (c.supportsCompletionsRequest.has_value())
        j["supportsCompletionsRequest"] = *c.supportsCompletionsRequest;
    if (c.supportsInstructionBreakpoints.has_value())
        j["supportsInstructionBreakpoints"] = *c.supportsInstructionBreakpoints;
    if (!c.exceptionBreakpointFilters.empty())
    {
        auto filters = nlohmann::json::array();
        for (auto const& f: c.exceptionBreakpointFilters)
        {
            nlohmann::json fj;
            to_json(fj, f);
            filters.push_back(std::move(fj));
        }
        j["exceptionBreakpointFilters"] = std::move(filters);
    }
}

inline void from_json(nlohmann::json const& j, Capabilities& c)
{
    if (j.contains("supportsConfigurationDoneRequest"))
        c.supportsConfigurationDoneRequest = j.at("supportsConfigurationDoneRequest").get<bool>();
    if (j.contains("supportsFunctionBreakpoints"))
        c.supportsFunctionBreakpoints = j.at("supportsFunctionBreakpoints").get<bool>();
    if (j.contains("supportsBreakpointLocationsRequest"))
        c.supportsBreakpointLocationsRequest = j.at("supportsBreakpointLocationsRequest").get<bool>();
    if (j.contains("supportsEvaluateForHovers"))
        c.supportsEvaluateForHovers = j.at("supportsEvaluateForHovers").get<bool>();
    if (j.contains("supportsVariableType"))
        c.supportsVariableType = j.at("supportsVariableType").get<bool>();
    if (j.contains("supportsConditionalBreakpoints"))
        c.supportsConditionalBreakpoints = j.at("supportsConditionalBreakpoints").get<bool>();
    if (j.contains("supportsHitConditionalBreakpoints"))
        c.supportsHitConditionalBreakpoints = j.at("supportsHitConditionalBreakpoints").get<bool>();
    if (j.contains("supportsLogPoints"))
        c.supportsLogPoints = j.at("supportsLogPoints").get<bool>();
    if (j.contains("supportsSteppingGranularity"))
        c.supportsSteppingGranularity = j.at("supportsSteppingGranularity").get<bool>();
    if (j.contains("supportsDisassembleRequest"))
        c.supportsDisassembleRequest = j.at("supportsDisassembleRequest").get<bool>();
    if (j.contains("supportsSetVariable"))
        c.supportsSetVariable = j.at("supportsSetVariable").get<bool>();
    if (j.contains("supportsExceptionInfoRequest"))
        c.supportsExceptionInfoRequest = j.at("supportsExceptionInfoRequest").get<bool>();
    if (j.contains("supportsLoadedSourcesRequest"))
        c.supportsLoadedSourcesRequest = j.at("supportsLoadedSourcesRequest").get<bool>();
    if (j.contains("supportsTerminateRequest"))
        c.supportsTerminateRequest = j.at("supportsTerminateRequest").get<bool>();
    if (j.contains("supportsRestartRequest"))
        c.supportsRestartRequest = j.at("supportsRestartRequest").get<bool>();
    if (j.contains("supportsCompletionsRequest"))
        c.supportsCompletionsRequest = j.at("supportsCompletionsRequest").get<bool>();
    if (j.contains("supportsInstructionBreakpoints"))
        c.supportsInstructionBreakpoints = j.at("supportsInstructionBreakpoints").get<bool>();
    if (j.contains("exceptionBreakpointFilters"))
        c.exceptionBreakpointFilters =
            j.at("exceptionBreakpointFilters").get<std::vector<ExceptionBreakpointsFilter>>();
}

inline void to_json(nlohmann::json& j, InitializeRequestArguments const& a)
{
    j = nlohmann::json { { "clientID", a.clientID },
                         { "clientName", a.clientName },
                         { "linesStartAt1", a.linesStartAt1 },
                         { "columnsStartAt1", a.columnsStartAt1 },
                         { "pathFormat", a.pathFormat } };
}

inline void from_json(nlohmann::json const& j, InitializeRequestArguments& a)
{
    if (j.contains("clientID"))
        a.clientID = j.at("clientID").get<std::string>();
    if (j.contains("clientName"))
        a.clientName = j.at("clientName").get<std::string>();
    if (j.contains("linesStartAt1"))
        a.linesStartAt1 = j.at("linesStartAt1").get<bool>();
    if (j.contains("columnsStartAt1"))
        a.columnsStartAt1 = j.at("columnsStartAt1").get<bool>();
    if (j.contains("pathFormat"))
        a.pathFormat = j.at("pathFormat").get<std::string>();
}

inline void to_json(nlohmann::json& j, LaunchRequestArguments const& a)
{
    j = nlohmann::json {
        { "program", a.program },
        { "args", a.args },
        { "stopOnEntry", a.stopOnEntry },
        { "noDebug", a.noDebug },
    };
}

inline void from_json(nlohmann::json const& j, LaunchRequestArguments& a)
{
    if (j.contains("program"))
        a.program = j.at("program").get<std::string>();
    if (j.contains("args"))
        a.args = j.at("args").get<std::vector<std::string>>();
    if (j.contains("stopOnEntry"))
        a.stopOnEntry = j.at("stopOnEntry").get<bool>();
    if (j.contains("noDebug"))
        a.noDebug = j.at("noDebug").get<bool>();
}

inline void to_json(nlohmann::json& j, Source const& s)
{
    j = nlohmann::json::object();
    if (s.name.has_value())
        j["name"] = *s.name;
    if (s.path.has_value())
        j["path"] = *s.path;
}

inline void from_json(nlohmann::json const& j, Source& s)
{
    if (j.contains("name"))
        s.name = j.at("name").get<std::string>();
    if (j.contains("path"))
        s.path = j.at("path").get<std::string>();
}

inline void to_json(nlohmann::json& j, SourceBreakpoint const& b)
{
    j = nlohmann::json { { "line", b.line } };
    if (b.column.has_value())
        j["column"] = *b.column;
    if (b.condition.has_value())
        j["condition"] = *b.condition;
    if (b.hitCondition.has_value())
        j["hitCondition"] = *b.hitCondition;
    if (b.logMessage.has_value())
        j["logMessage"] = *b.logMessage;
}

inline void from_json(nlohmann::json const& j, SourceBreakpoint& b)
{
    b.line = j.at("line").get<int>();
    if (j.contains("column"))
        b.column = j.at("column").get<int>();
    if (j.contains("condition"))
        b.condition = j.at("condition").get<std::string>();
    if (j.contains("hitCondition"))
        b.hitCondition = j.at("hitCondition").get<std::string>();
    if (j.contains("logMessage"))
        b.logMessage = j.at("logMessage").get<std::string>();
}

inline void to_json(nlohmann::json& j, FunctionBreakpoint const& b)
{
    j = nlohmann::json { { "name", b.name } };
    if (b.condition.has_value())
        j["condition"] = *b.condition;
    if (b.hitCondition.has_value())
        j["hitCondition"] = *b.hitCondition;
}

inline void from_json(nlohmann::json const& j, FunctionBreakpoint& b)
{
    b.name = j.at("name").get<std::string>();
    if (j.contains("condition"))
        b.condition = j.at("condition").get<std::string>();
    if (j.contains("hitCondition"))
        b.hitCondition = j.at("hitCondition").get<std::string>();
}

inline void to_json(nlohmann::json& j, Breakpoint const& b)
{
    j = nlohmann::json { { "id", b.id }, { "verified", b.verified } };
    if (b.message.has_value())
        j["message"] = *b.message;
    if (b.sourcePath.has_value())
        j["source"] = nlohmann::json { { "path", *b.sourcePath } };
    if (b.line.has_value())
        j["line"] = *b.line;
    if (b.column.has_value())
        j["column"] = *b.column;
    if (b.endLine.has_value())
        j["endLine"] = *b.endLine;
    if (b.endColumn.has_value())
        j["endColumn"] = *b.endColumn;
}

inline void from_json(nlohmann::json const& j, Breakpoint& b)
{
    b.id = j.at("id").get<int>();
    b.verified = j.at("verified").get<bool>();
    if (j.contains("message"))
        b.message = j.at("message").get<std::string>();
    if (j.contains("source") && j.at("source").contains("path"))
        b.sourcePath = j.at("source").at("path").get<std::string>();
    if (j.contains("line"))
        b.line = j.at("line").get<int>();
    if (j.contains("column"))
        b.column = j.at("column").get<int>();
    if (j.contains("endLine"))
        b.endLine = j.at("endLine").get<int>();
    if (j.contains("endColumn"))
        b.endColumn = j.at("endColumn").get<int>();
}

inline void to_json(nlohmann::json& j, BreakpointLocation const& loc)
{
    j = nlohmann::json { { "line", loc.line } };
    if (loc.column.has_value())
        j["column"] = *loc.column;
    if (loc.endLine.has_value())
        j["endLine"] = *loc.endLine;
    if (loc.endColumn.has_value())
        j["endColumn"] = *loc.endColumn;
}

inline void from_json(nlohmann::json const& j, BreakpointLocation& loc)
{
    loc.line = j.at("line").get<int>();
    if (j.contains("column"))
        loc.column = j.at("column").get<int>();
    if (j.contains("endLine"))
        loc.endLine = j.at("endLine").get<int>();
    if (j.contains("endColumn"))
        loc.endColumn = j.at("endColumn").get<int>();
}

inline void to_json(nlohmann::json& j, StackFrame const& f)
{
    j = nlohmann::json {
        { "id", f.id },
        { "name", f.name },
        { "line", f.line },
        { "column", f.column },
    };
    nlohmann::json src;
    to_json(src, f.source);
    j["source"] = std::move(src);
    if (f.instructionPointerReference.has_value())
        j["instructionPointerReference"] = *f.instructionPointerReference;
}

inline void from_json(nlohmann::json const& j, StackFrame& f)
{
    f.id = j.at("id").get<int>();
    f.name = j.at("name").get<std::string>();
    f.line = j.at("line").get<int>();
    f.column = j.value("column", 0);
    if (j.contains("source"))
        from_json(j.at("source"), f.source);
    if (j.contains("instructionPointerReference"))
        f.instructionPointerReference = j.at("instructionPointerReference").get<std::string>();
}

inline void to_json(nlohmann::json& j, Scope const& s)
{
    j = nlohmann::json {
        { "name", s.name },
        { "variablesReference", s.variablesReference },
        { "expensive", s.expensive },
    };
}

inline void from_json(nlohmann::json const& j, Scope& s)
{
    s.name = j.at("name").get<std::string>();
    s.variablesReference = j.at("variablesReference").get<int>();
    s.expensive = j.value("expensive", false);
}

inline void to_json(nlohmann::json& j, Variable const& v)
{
    j = nlohmann::json {
        { "name", v.name },
        { "value", v.value },
        { "variablesReference", v.variablesReference },
    };
    if (!v.type.empty())
        j["type"] = v.type;
}

inline void from_json(nlohmann::json const& j, Variable& v)
{
    v.name = j.at("name").get<std::string>();
    v.value = j.at("value").get<std::string>();
    v.variablesReference = j.value("variablesReference", 0);
    if (j.contains("type"))
        v.type = j.at("type").get<std::string>();
}

inline void to_json(nlohmann::json& j, EvaluateResult const& e)
{
    j = nlohmann::json {
        { "result", e.result },
        { "variablesReference", e.variablesReference },
    };
    if (!e.type.empty())
        j["type"] = e.type;
}

inline void from_json(nlohmann::json const& j, EvaluateResult& e)
{
    e.result = j.at("result").get<std::string>();
    e.variablesReference = j.value("variablesReference", 0);
    if (j.contains("type"))
        e.type = j.at("type").get<std::string>();
}

inline void to_json(nlohmann::json& j, CompletionItem const& c)
{
    j = nlohmann::json { { "label", c.label }, { "type", c.type } };
}

inline void from_json(nlohmann::json const& j, CompletionItem& c)
{
    c.label = j.at("label").get<std::string>();
    if (j.contains("type"))
        c.type = j.at("type").get<std::string>();
}

inline void to_json(nlohmann::json& j, InstructionBreakpoint const& b)
{
    j = nlohmann::json { { "instructionReference", b.instructionReference } };
    if (b.offset.has_value())
        j["offset"] = *b.offset;
    if (b.condition.has_value())
        j["condition"] = *b.condition;
    if (b.hitCondition.has_value())
        j["hitCondition"] = *b.hitCondition;
}

inline void from_json(nlohmann::json const& j, InstructionBreakpoint& b)
{
    b.instructionReference = j.at("instructionReference").get<std::string>();
    if (j.contains("offset"))
        b.offset = j.at("offset").get<int>();
    if (j.contains("condition"))
        b.condition = j.at("condition").get<std::string>();
    if (j.contains("hitCondition"))
        b.hitCondition = j.at("hitCondition").get<std::string>();
}

} // namespace endo::dap
