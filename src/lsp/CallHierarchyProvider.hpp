// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Prepares a call hierarchy item for the function at the given position.
///
/// @param source The full document text
/// @param uri The document URI
/// @param position The cursor position
/// @return The CallHierarchyItem if a function is found at the position, otherwise empty
[[nodiscard]] std::vector<CallHierarchyItem> prepareCallHierarchy(std::string const& source,
                                                                  std::string const& uri,
                                                                  Position position);

/// Computes incoming calls to the given call hierarchy item.
///
/// @param source The full document text
/// @param uri The document URI
/// @param item The call hierarchy item to find callers for
/// @return A vector of incoming calls
[[nodiscard]] std::vector<CallHierarchyIncomingCall> computeIncomingCalls(std::string const& source,
                                                                          std::string const& uri,
                                                                          CallHierarchyItem const& item);

/// Computes outgoing calls from the given call hierarchy item.
///
/// @param source The full document text
/// @param uri The document URI
/// @param item The call hierarchy item to find callees for
/// @return A vector of outgoing calls
[[nodiscard]] std::vector<CallHierarchyOutgoingCall> computeOutgoingCalls(std::string const& source,
                                                                          std::string const& uri,
                                                                          CallHierarchyItem const& item);

} // namespace endo::lsp
