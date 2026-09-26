// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/log/LogStore.hpp>

namespace endo::log
{

// Centralized log category accessors to avoid duplicate registration
core::log::Category& shellDebug();
core::log::Category& vmTrace();
core::log::Category& vmIR();
core::log::Category& parser();
core::log::Category& pipe();

// Force initialization of all log categories (call before --log-list)
void registerAllCategories();

} // namespace endo::log
