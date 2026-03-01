// SPDX-License-Identifier: Apache-2.0
//
// Umbrella header — includes all CoreVM public headers.
// Consumers that only need a subset should include specific headers instead.
//
#pragma once

#include <CoreVM/CoreTypes.hpp>
#include <CoreVM/Diagnostics.hpp>
#include <CoreVM/Formatters.hpp>
#include <CoreVM/RuntimeConfig.hpp>
#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/TargetCodeGenerator.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/ir/BasicBlock.hpp>
#include <CoreVM/ir/IRProgram.hpp>
#include <CoreVM/ir/Instructions.hpp>
#include <CoreVM/ir/Value.hpp>
#include <CoreVM/transform/Passes.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/util.hpp>
#include <CoreVM/vm/NativeCallback.hpp>
#include <CoreVM/vm/Program.hpp>
#include <CoreVM/vm/Runner.hpp>
#include <CoreVM/vm/Runtime.hpp>
