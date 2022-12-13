// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>    // memset()
#include <functional> // hash<>
#include <iostream>
#include <optional>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>  // ntohl(), htonl()
    #include <netinet/in.h> // in_addr, in6_addr
#endif

namespace CoreVM::util
{

/**
 * IPv4 or IPv6 network address.
 */
class IPAddress
{
  public:
    enum class Family : int
    {
        V4 = AF_INET,
        V6 = AF_INET6,
    };

  private:
    Family _family = Family::V4;
    mutable char _cstr[INET6_ADDRSTRLEN] {};
    uint8_t _buf[sizeof(struct in6_addr)] {};

  public:
    IPAddress();
    explicit IPAddress(const in_addr* saddr);
    explicit IPAddress(const in6_addr* saddr);
    explicit IPAddress(const sockaddr_in* saddr);
    explicit IPAddress(const sockaddr_in6* saddr);
    explicit IPAddress(const std::string& text);
    IPAddress(int family, const void* addr);
    IPAddress(const std::string& text, Family v);

    IPAddress(IPAddress const&) = default;
    IPAddress& operator=(const IPAddress& value);

    void assign(const std::string& text);
    bool assign(const std::string& text, Family family);

    void clear();

    Family family() const;
    const void* data() const;
    size_t size() const;
    std::string str() const;
    const char* c_str() const;

    friend bool operator==(const IPAddress& a, const IPAddress& b);
    friend bool operator!=(const IPAddress& a, const IPAddress& b);
};

// {{{ impl
inline IPAddress::IPAddress()
{
    _family = Family::V4;
    _cstr[0] = '\0';
    memset(_buf, 0, sizeof(_buf));
}

inline IPAddress::IPAddress(const in_addr* saddr)
{
    _family = Family::V4;
    _cstr[0] = '\0';
    memcpy(_buf, saddr, sizeof(*saddr));
}

inline IPAddress::IPAddress(const in6_addr* saddr)
{
    _family = Family::V6;
    _cstr[0] = '\0';
    memcpy(_buf, saddr, sizeof(*saddr));
}

inline IPAddress::IPAddress(const sockaddr_in* saddr)
{
    _family = Family::V4;
    _cstr[0] = '\0';
    memcpy(_buf, &saddr->sin_addr, sizeof(saddr->sin_addr));
}

inline IPAddress::IPAddress(const sockaddr_in6* saddr)
{
    _family = Family::V6;
    _cstr[0] = '\0';
    memcpy(_buf, &saddr->sin6_addr, sizeof(saddr->sin6_addr));
}

// I suggest to use a very strict IP filter to prevent spoofing or injection
inline IPAddress::IPAddress(const std::string& text)
{
    // You should use regex to parse ipv6 :) ( http://home.deds.nl/~aeron/regex/ )
    if (text.find(':') != std::string::npos)
        assign(text, Family::V6);
    else
        assign(text, Family::V4);
}

inline IPAddress::IPAddress(int family, const void* addr)
{
    _family = static_cast<Family>(family);
    _cstr[0] = '\0';
    if (_family == Family::V6)
    {
        memcpy(_buf, addr, sizeof(sockaddr_in6::sin6_addr));
    }
    else
    {
        memcpy(_buf, addr, sizeof(sockaddr_in::sin_addr));
    }
}

inline IPAddress::IPAddress(const std::string& text, Family version)
{
    assign(text, version);
}

inline void IPAddress::assign(const std::string& text)
{
    // You should use regex to parse ipv6 :) ( http://home.deds.nl/~aeron/regex/ )
    if (text.find(':') != std::string::npos)
    {
        assign(text, Family::V6);
    }
    else
    {
        assign(text, Family::V4);
    }
}

inline IPAddress& IPAddress::operator=(const IPAddress& v)
{
    if (&v == this)
        return *this;

    _family = v._family;
#if defined(_WIN32) || defined(_WIN64)
    strncpy_s(_cstr, sizeof(_cstr), v._cstr, sizeof(v._cstr));
#else
    strncpy(_cstr, v._cstr, sizeof(_cstr));
#endif
    memcpy(_buf, v._buf, v.size());

    return *this;
}

inline bool IPAddress::assign(const std::string& text, Family family)
{
    _family = family;
    int rv = inet_pton(static_cast<int>(family), text.c_str(), _buf);
    if (rv <= 0)
    {
        if (rv < 0)
            perror("inet_pton");
        else
            fprintf(stderr, "IP address Not in presentation format: %s\n", text.c_str());

        _cstr[0] = 0;
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    strncpy_s(_cstr, sizeof(_cstr), text.c_str(), text.size());
#else
    strncpy(_cstr, text.c_str(), sizeof(_cstr));
#endif

    return true;
}

inline void IPAddress::clear()
{
    _family = Family::V4;
    _cstr[0] = 0;
    memset(_buf, 0, sizeof(_buf));
}

inline IPAddress::Family IPAddress::family() const
{
    return _family;
}

inline const void* IPAddress::data() const
{
    return _buf;
}

inline size_t IPAddress::size() const
{
    return _family == Family::V4 ? sizeof(in_addr) : sizeof(in6_addr);
}

inline std::string IPAddress::str() const
{
    return c_str();
}

inline const char* IPAddress::c_str() const
{
    if (*_cstr == '\0')
    {
        inet_ntop(static_cast<int>(_family), &_buf, _cstr, sizeof(_cstr));
    }
    return _cstr;
}

inline bool operator==(const IPAddress& a, const IPAddress& b)
{
    if (&a == &b)
        return true;

    return a.family() == b.family() && memcmp(a.data(), b.data(), a.size()) == 0;
}

inline bool operator!=(const IPAddress& a, const IPAddress& b)
{
    return !(a == b);
}

} // namespace CoreVM::util
// }}}

namespace std
{
template <>
struct hash<::CoreVM::util::IPAddress>
{
    size_t operator()(const ::CoreVM::util::IPAddress& v) const { return *(uint32_t*) (v.data()); }
};
} // namespace std

template <>
struct fmt::formatter<CoreVM::util::IPAddress>: formatter<std::string>
{
    using IPAddress = CoreVM::util::IPAddress;

    auto format(IPAddress const& v, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(v.str(), ctx);
    }
};

template <>
struct fmt::formatter<std::optional<CoreVM::util::IPAddress>>: formatter<std::string>
{
    using IPAddress = CoreVM::util::IPAddress;

    auto format(std::optional<IPAddress> const& v, format_context& ctx) -> format_context::iterator
    {
        if (v)
            return formatter<std::string>::format(v->str(), ctx);
        else
            return formatter<std::string>::format("NONE", ctx);
    }
};
