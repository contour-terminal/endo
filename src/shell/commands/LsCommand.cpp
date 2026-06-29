// SPDX-License-Identifier: Apache-2.0
#include "LsCommand.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeDescriptor.hpp>

#include <ranges>

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
    for (const auto& file: std::ranges::reverse_view(files))
    {
        // Allocate a FileInfo record
        auto* record = runner.allocObject(CoreVM::BuiltinTypeId::FileInfo);
        record->setSlot(0, reinterpret_cast<uintptr_t>(runner.newString(file.name)));
        auto* sizeObj = endo::builtins::makeSizeFromBytes(&runner, file.size);
        record->setSlot(1, reinterpret_cast<uintptr_t>(sizeObj));
        auto* modeObj = endo::builtins::makeFileModeFromBits(&runner, file.mode);
        record->setSlot(2, reinterpret_cast<uintptr_t>(modeObj));
        auto* mtimeObj = endo::builtins::makeDateTimeFromEpoch(&runner, file.mtime);
        record->setSlot(3, reinterpret_cast<uintptr_t>(mtimeObj));
        record->setSlot(4, static_cast<uint64_t>(file.isDir ? 1 : 0));
        record->setSlot(5, static_cast<uint64_t>(file.isSymlink ? 1 : 0));
        // Reuse the shared empty-string sentinel for the common non-symlink case to avoid a
        // heap allocation + known-string insertion per directory entry on this listing path.
        record->setSlot(6,
                        reinterpret_cast<uintptr_t>(file.symlinkTarget.empty()
                                                        ? runner.emptyString()
                                                        : runner.newString(file.symlinkTarget)));

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
