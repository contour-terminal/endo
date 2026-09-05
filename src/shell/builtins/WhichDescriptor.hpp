// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file WhichDescriptor.hpp
/// @brief Single source of truth for the `which` builtin's flags and argument shape.

#include <shell/builtins/InlineCommandDescriptor.hpp>

namespace endo
{

/// @brief Returns the command metadata for `which`.
///
/// This drives three consumers from one declaration: the completion spec
/// (createWhichSpec()), the rendered `which --help` text (generateInlineHelp()), and the
/// flag parsing in Shell::builtinWhich(). Teaching `which` a new flag is one new row in
/// the option table behind this function.
///
/// @note Deliberately *not* part of Shell::inlineCommandDescriptors(). `which` is
///       dispatched as a parser-level builtin (ast::BuiltinWhichStmt), so this descriptor
///       carries no noStdinFn/withStdinFn — and InlineCommandDescriptor::execute()
///       dereferences noStdinFn unconditionally. Keeping it out of the table means
///       Shell::findInlineBuiltin() can never reach it.
///
/// @return Reference to the static descriptor; valid for the program's lifetime.
[[nodiscard]] InlineCommandDescriptor const& whichDescriptor();

} // namespace endo
