// SPDX-License-Identifier: Apache-2.0
#include "LsCommand.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

namespace endo
{

LsCommand::LsCommand(FileInfoProvider const& provider, std::string path):
    _provider(provider), _path(std::move(path))
{
}

uint16_t LsCommand::outputTypeId() const
{
    return CoreVM::BuiltinTypeId::FileInfo;
}

CoreVM::TypedObject* LsCommand::execute(CoreVM::Runner& runner) const
{
    auto const files = _provider.listDirectory(_path);

    // Start with Nil (empty list)
    auto* list = runner.allocObject(CoreVM::BuiltinTypeId::List);
    list->tag = 0; // Nil

    // Build cons-cell list right-to-left so the result is in original order
    for (auto it = files.rbegin(); it != files.rend(); ++it)
    {
        auto const& file = *it;

        // Allocate a FileInfo record
        auto* record = runner.allocObject(CoreVM::BuiltinTypeId::FileInfo);
        record->setSlot(0, reinterpret_cast<uintptr_t>(runner.newString(file.name)));
        auto* sizeObj = endo::builtins::makeSizeFromBytes(&runner, file.size);
        record->setSlot(1, reinterpret_cast<uintptr_t>(sizeObj));
        record->setSlot(2, static_cast<uint64_t>(file.mode));
        auto* mtimeObj = endo::builtins::makeDateTimeFromEpoch(&runner, file.mtime);
        record->setSlot(3, reinterpret_cast<uintptr_t>(mtimeObj));
        record->setSlot(4, static_cast<uint64_t>(file.isDir ? 1 : 0));

        // Cons this record onto the list
        auto* cons = runner.allocObject(CoreVM::BuiltinTypeId::List);
        cons->tag = 1; // Cons
        cons->setSlot(0, reinterpret_cast<uintptr_t>(record));
        cons->setSlot(1, reinterpret_cast<uintptr_t>(list));
        list = cons;
    }

    return list;
}

} // namespace endo
