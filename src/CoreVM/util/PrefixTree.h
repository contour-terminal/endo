// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <unordered_map>

namespace CoreVM::util
{

template <typename K, typename V>
class PrefixTree
{
  public:
    typedef K Key;
    typedef typename Key::value_type Elem;
    typedef V Value;

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

} // namespace CoreVM::util
