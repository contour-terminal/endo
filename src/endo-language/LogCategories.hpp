// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/LogStore.hpp>

namespace endo::log
{

// Centralized log category accessors to avoid duplicate registration
logstore::Category& shellDebug();
logstore::Category& vmTrace();
logstore::Category& vmIR();
logstore::Category& parser();
logstore::Category& pipe();

// Force initialization of all log categories (call before --log-list)
void registerAllCategories();

} // namespace endo::log
