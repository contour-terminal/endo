// SPDX-License-Identifier: Apache-2.0
module;

#include <fmt/format.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>    // memset()
#include <functional> // hash<>
#include <iostream>
#include <optional>
#include <regex>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>  // ntohl(), htonl()
    #include <netinet/in.h> // in_addr, in6_addr
#endif

#include <memory>
#include <unordered_map>

export module CoreVM.util;

export namespace CoreVM::util
{

template <typename T>
class UnboxedRange
{
  public:
    using BoxedContainer = T;
    using BoxedIterator = typename BoxedContainer::iterator;
    using element_type = typename BoxedContainer::value_type::element_type;

    class iterator // NOLINT
    {              // {{{
      public:
        using difference_type = typename BoxedContainer::iterator::difference_type;
        using value_type = typename BoxedContainer::iterator::value_type::element_type;
        using pointer = typename BoxedContainer::iterator::value_type::element_type*;
        using reference = typename BoxedContainer::iterator::value_type::element_type&;
        using iterator_category = typename BoxedContainer::iterator::iterator_category;

        explicit iterator(BoxedIterator boxed): _it(boxed) {}

        [[nodiscard]] const element_type& operator->() const { return **_it; }
        [[nodiscard]] element_type& operator->() { return **_it; }

        [[nodiscard]] const element_type* operator*() const { return (*_it).get(); }
        [[nodiscard]] element_type* operator*() { return (*_it).get(); }

        iterator& operator++()
        {
            ++_it;
            return *this;
        }
        iterator& operator++(int)
        {
            ++_it;
            return *this;
        }

        [[nodiscard]] bool operator==(const iterator& other) const { return _it == other._it; }
        [[nodiscard]] bool operator!=(const iterator& other) const { return _it != other._it; }

      private:
        BoxedIterator _it;
    }; // }}}

    UnboxedRange(BoxedIterator begin, BoxedIterator end): _begin(begin), _end(end) {}
    explicit UnboxedRange(BoxedContainer& c): _begin(c.begin()), _end(c.end()) {}

    [[nodiscard]] iterator begin() const { return _begin; }
    [[nodiscard]] iterator end() const { return _end; }
    [[nodiscard]] size_t size() const
    {
        auto it = _begin;
        size_t count = 0;
        while (it != _end)
        {
            ++it;
            ++count;
        }
        return count;
    }

  private:
    iterator _begin;
    iterator _end;
};

/**
 * Unboxes boxed element types in containers.
 *
 * Good examples are:
 *
 * \code
 *    std::vector<std::unique_ptr<int>> numbers;
 *    // ...
 *    for (int number: unbox(numbers)) {
 *      // ... juse use number here, instead of number.get() or *number.
 *    };
 * \endcode
 */
template <typename BoxedContainer>
UnboxedRange<BoxedContainer> unbox(BoxedContainer& boxedContainer)
{
    return UnboxedRange<BoxedContainer>(boxedContainer);
}

template <typename K, typename V>
class SuffixTree
{
  public:
    using Key = K;
    using Elem = typename Key::value_type;
    using Value = V;

    SuffixTree();
    ~SuffixTree();

    void insert(const Key& key, const Value& value);
    bool lookup(const Key& key, Value* value) const;

  private:
    struct Node
    { // {{{
        Node* parent;
        Elem element;
        std::unordered_map<Elem, std::unique_ptr<Node>> children;
        Value value;

        Node(): parent(nullptr), element(), children(), value() {}
        Node(Node* p, Elem e): parent(p), element(e), children(), value() {}
    }; // }}}

    Node _root;

    Node* acquire(Elem el, Node* n);
};

template <typename K, typename V>
SuffixTree<K, V>::SuffixTree(): _root()
{
}

template <typename K, typename V>
SuffixTree<K, V>::~SuffixTree()
{
}

template <typename K, typename V>
void SuffixTree<K, V>::insert(const Key& key, const Value& value)
{
    Node* level = &_root;

    // insert reverse
    for (auto i = key.rbegin(), e = key.rend(); i != e; ++i)
        level = acquire(*i, level);

    level->value = value;
}

template <typename K, typename V>
typename SuffixTree<K, V>::Node* SuffixTree<K, V>::acquire(Elem elem, Node* n)
{
    auto i = n->children.find(elem);
    if (i != n->children.end())
        return i->second.get();

    n->children[elem] = std::make_unique<Node>(n, elem);
    return n->children[elem].get();
}

template <typename K, typename V>
bool SuffixTree<K, V>::lookup(const Key& key, Value* value) const
{
    const Node* level = &_root;

    for (auto i = key.rbegin(), e = key.rend(); i != e; ++i)
    {
        auto k = level->children.find(*i);
        if (k == level->children.end())
            break;

        level = k->second.get();
    }

    while (level && level->parent)
    {
        if (level->value)
        {
            *value = level->value;
            return true;
        }
        level = level->parent;
    }

    return false;
}


template <typename K, typename V>
class PrefixTree
{
  public:
    using Key = K;
    using Elem = typename Key::value_type;
    using Value = V;

    PrefixTree();
    ~PrefixTree();

    void insert(const Key& key, const Value& value);
    bool lookup(const Key& key, Value* value) const;

  private:
    struct Node
    { // {{{
        Node* parent;
        Elem element;
        std::unordered_map<Elem, std::unique_ptr<Node>> children;
        Value value;

        Node(): parent(nullptr), element(), children(), value() {}
        Node(Node* p, Elem e): parent(p), element(e), children(), value() {}
    }; // }}}

    Node _root;

    Node* acquire(Elem el, Node* n);
};

// {{{
template <typename K, typename V>
PrefixTree<K, V>::PrefixTree(): _root()
{
}

template <typename K, typename V>
PrefixTree<K, V>::~PrefixTree()
{
}

template <typename K, typename V>
void PrefixTree<K, V>::insert(const Key& key, const Value& value)
{
    Node* level = &_root;

    for (const auto& ke: key)
        level = acquire(ke, level);

    level->value = value;
}

template <typename K, typename V>
typename PrefixTree<K, V>::Node* PrefixTree<K, V>::acquire(Elem elem, Node* n)
{
    auto i = n->children.find(elem);
    if (i != n->children.end())
        return i->second.get();

    n->children[elem] = std::make_unique<Node>(n, elem);
    return n->children[elem].get();
}

template <typename K, typename V>
bool PrefixTree<K, V>::lookup(const Key& key, Value* value) const
{
    const Node* level = &_root;

    for (const auto& ke: key)
    {
        auto i = level->children.find(ke);
        if (i == level->children.end())
            break;

        level = i->second.get();
    }

    while (level && level->parent)
    {
        if (level->value)
        {
            *value = level->value;
            return true;
        }
        level = level->parent;
    }

    return false;
}
// }}}

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

class BufferRef;

class RegExp
{
  private:
    std::string _pattern;
    std::regex _re;

  public:
    using Result = std::smatch;

  public:
    explicit RegExp(std::string const& pattern);
    RegExp() = default;
    RegExp(RegExp const& v) = default;
    ~RegExp() = default;

    RegExp(RegExp&& v) noexcept = default;
    RegExp& operator=(RegExp&& v) noexcept = default;

    bool match(const std::string& target, Result* result = nullptr) const;

    [[nodiscard]] const std::string& pattern() const { return _pattern; }
    [[nodiscard]] const char* c_str() const;

    operator const std::string&() const { return _pattern; }

    friend bool operator==(const RegExp& a, const RegExp& b) { return a.pattern() == b.pattern(); }
    friend bool operator!=(const RegExp& a, const RegExp& b) { return a.pattern() != b.pattern(); }
    friend bool operator<=(const RegExp& a, const RegExp& b) { return a.pattern() <= b.pattern(); }
    friend bool operator>=(const RegExp& a, const RegExp& b) { return a.pattern() >= b.pattern(); }
    friend bool operator<(const RegExp& a, const RegExp& b) { return a.pattern() < b.pattern(); }
    friend bool operator>(const RegExp& a, const RegExp& b) { return a.pattern() > b.pattern(); }
};

class RegExpContext
{
  public:
    const RegExp::Result* regexMatch() const
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

    RegExp::Result* regexMatch()
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

  private:
    mutable std::unique_ptr<RegExp::Result> _regexMatch;
};

} // namespace CoreVM::util

export
{

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
}
