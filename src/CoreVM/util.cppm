module;

#include <cstdint>
#include <regex>
#include <CoreVM/util/IPAddress.h>

export module CoreVM:util;

export namespace CoreVM::util
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


}
