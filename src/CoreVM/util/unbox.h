// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iterator>

namespace CoreVM::util
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

} // namespace CoreVM::util
