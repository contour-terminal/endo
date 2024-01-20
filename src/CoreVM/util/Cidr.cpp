// SPDX-License-Identifier: Apache-2.0
module;

#include <fmt/format.h>

#include <cstdint>

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>  // ntohl(), htonl()
    #include <netinet/in.h> // in_addr, in6_addr
#endif

module CoreVM.util;

namespace CoreVM::util
{

std::string Cidr::str() const
{
    char result[INET6_ADDRSTRLEN + 32];

    inet_ntop(static_cast<int>(_ipaddr.family()), _ipaddr.data(), result, sizeof(result));

    size_t n = strlen(result);
    snprintf(result + n, sizeof(result) - n, "/%zu", _prefix);

    return result;
}

bool Cidr::contains(const IPAddress& ipaddr) const
{
    if (ipaddr.family() != address().family())
        return false;

    // IPv4
    if (ipaddr.family() == IPAddress::Family::V4)
    {
        uint32_t ip = *(uint32_t*) ipaddr.data();
        uint32_t subnet = *(uint32_t*) address().data();
        uint32_t match = ip & (0xFFFFFFFF >> (32 - prefix()));

        return match == subnet;
    }

    // IPv6
    int bits = prefix();
    const uint32_t* words = (const uint32_t*) address().data();
    const uint32_t* input = (const uint32_t*) ipaddr.data();
    while (bits >= 32)
    {
        uint32_t match = *words & 0xFFFFFFFF;
        if (match != *input)
            return false;

        words++;
        input++;
        bits -= 32;
    }

    uint32_t match = *words & 0xFFFFFFFF >> (32 - bits);
    if (match != *input)
        return false;

    return true;
}

bool operator==(const Cidr& a, const Cidr& b)
{
    if (&a == &b)
        return true;

    return a._prefix == b._prefix && a._ipaddr == b._ipaddr;
}

bool operator!=(const Cidr& a, const Cidr& b)
{
    return !(a == b);
}

} // namespace CoreVM::util
