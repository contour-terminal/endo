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

    auto* list = runner.makeNilList(CoreVM::LiteralType::Object);

    // Build cons-cell list right-to-left so the result is in original order
    for (const auto& file: std::ranges::reverse_view(files))
    {
        // The provider's absolute path travels with the record, so consumers can address the
        // entry after the listing has been passed around, filtered, or concatenated.
        auto* record = endo::builtins::makeFileInfoRecord(&runner,
                                                          { .name = file.name,
                                                            .path = file.path,
                                                            .symlinkTarget = file.symlinkTarget,
                                                            .size = file.size,
                                                            .mode = file.mode,
                                                            .mtime = file.mtime,
                                                            .isDir = file.isDir,
                                                            .isSymlink = file.isSymlink });

        list = runner.makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
    }

    return list;
}

} // namespace endo
