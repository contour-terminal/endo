// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/util/IPAddress.h>

#include <fmt/format.h>

namespace CoreVM::util
{

/**
 * @brief CIDR network notation object.
 *
 * @see IPAddress
 */
class Cidr
{
  public:
    /**
     * @brief Initializes an empty cidr notation.
     *
     * e.g. 0.0.0.0/0
     */
    Cidr(): _ipaddr(), _prefix(0) {}

    /**
     * @brief Initializes this CIDR notation with given IP address and prefix.
     */
    Cidr(const char* ipaddress, size_t prefix): _ipaddr(ipaddress), _prefix(prefix) {}

    /**
     * @brief Initializes this CIDR notation with given IP address and prefix.
     */
    Cidr(const IPAddress& ipaddress, size_t prefix): _ipaddr(ipaddress), _prefix(prefix) {}

    /**
     * @brief Retrieves the address part of this CIDR notation.
     */
    const IPAddress& address() const { return _ipaddr; }

    /**
     * @brief Sets the address part of this CIDR notation.
     */
    bool setAddress(const std::string& text, IPAddress::Family family)
    {
        return _ipaddr.assign(text, family);
    }

    /**
     * @brief Retrieves the prefix part of this CIDR notation.
     */
    size_t prefix() const { return _prefix; }

    /**
     * @brief Sets the prefix part of this CIDR notation.
     */
    void setPrefix(size_t n) { _prefix = n; }

    /**
     * @brief Retrieves the string-form of this network in CIDR notation.
     */
    std::string str() const;

    /**
     * @brief test whether or not given IP address is inside the network.
     *
     * @retval true it is inside this network.
     * @retval false it is not inside this network.
     */
    bool contains(const IPAddress& ipaddr) const;

    /**
     * @brief compares 2 CIDR notations for equality.
     */
    friend bool operator==(const Cidr& a, const Cidr& b);

    /**
     * @brief compares 2 CIDR notations for inequality.
     */
    friend bool operator!=(const Cidr& a, const Cidr& b);

  private:
    IPAddress _ipaddr;
    size_t _prefix;
};

} // namespace CoreVM::util

namespace std
{

template <>
struct hash<CoreVM::util::Cidr>
{
    size_t operator()(const CoreVM::util::Cidr& v) const noexcept
    {
        // TODO: let it honor IPv6 better
        return static_cast<size_t>(*(uint32_t*) (v.address().data()) + v.prefix());
    }
};

} // namespace std

namespace fmt
{
template <>
struct formatter<CoreVM::util::Cidr>: formatter<std::string>
{
    auto format(CoreVM::util::Cidr const& value, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(value.str(), ctx);
    }
};
} // namespace fmt
