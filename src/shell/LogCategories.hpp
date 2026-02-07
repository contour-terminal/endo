// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/logstore.h>

namespace endo::log
{

// Centralized log category accessors to avoid duplicate registration
logstore::category& shellDebug();
logstore::category& vmTrace();
logstore::category& vmIR();
logstore::category& parser();
logstore::category& pipe();

// Force initialization of all log categories (call before --log-list)
void registerAllCategories();

} // namespace endo::log
