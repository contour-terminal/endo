// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/util.hpp>

#include <string>
namespace CoreVM
{

template class ConstantValue<int64_t, LiteralType::Number>;
template class ConstantValue<bool, LiteralType::Boolean>;
template class ConstantValue<std::string, LiteralType::String>;
template class ConstantValue<util::IPAddress, LiteralType::IPAddress>;
template class ConstantValue<util::Cidr, LiteralType::Cidr>;
template class ConstantValue<util::RegExp, LiteralType::RegExp>;

} // namespace CoreVM
