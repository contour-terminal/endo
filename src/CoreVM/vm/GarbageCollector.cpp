// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/vm/GarbageCollector.hpp>

#include <vector>

namespace CoreVM
{

GarbageCollector::GarbageCollector(ObjectPool& pool): _pool(pool)
{
}

size_t GarbageCollector::collect(std::span<TypedObject* const> roots)
{
    _marked.clear();
    mark(roots);
    auto const collected = sweep();
    _marked.clear();
    return collected;
}

void GarbageCollector::mark(std::span<TypedObject* const> roots)
{
    std::vector<TypedObject*> worklist;

    for (auto* root: roots)
    {
        if (root && !_marked.contains(root))
        {
            _marked.insert(root);
            worklist.push_back(root);
        }
    }

    while (!worklist.empty())
    {
        auto* obj = worklist.back();
        worklist.pop_back();

        visitChildObjects(
            *obj,
            [this](void const* ptr) { return _pool.owns(ptr); },
            [this, &worklist](TypedObject* child) {
                if (!_marked.contains(child))
                {
                    _marked.insert(child);
                    worklist.push_back(child);
                }
            });
    }
}

size_t GarbageCollector::sweep()
{
    std::vector<TypedObject*> toCollect;

    _pool.forEachLiveObject([&](TypedObject& obj) {
        if (!_marked.contains(&obj))
            toCollect.push_back(&obj);
    });

    for (auto* obj: toCollect)
    {
        // Decrement refcounts of marked children (they survive, but lose
        // the reference from the collected parent).
        visitChildObjects(
            *obj,
            [this](void const* ptr) { return _pool.owns(ptr); },
            [this](TypedObject* child) {
                if (_marked.contains(child))
                    (void) releaseObject(child); // just decrement RC, don't cascade
            });

        _pool.deallocate(obj);
    }

    return toCollect.size();
}

} // namespace CoreVM
