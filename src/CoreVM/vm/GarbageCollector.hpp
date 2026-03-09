// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/vm/ObjectPool.hpp>

#include <cstddef>
#include <span>
#include <unordered_set>

namespace CoreVM
{

/// Mark-and-sweep cycle collector for TypedObject instances.
///
/// Works as a backup to reference counting: detects and collects cyclic garbage
/// that refcounting alone cannot reclaim. The collector is triggered periodically
/// based on allocation count and the presence of mutation suspects.
///
/// THREAD-SAFETY: Designed for per-Runner (per-thread) use. No internal locking.
class GarbageCollector
{
  public:
    /// @param pool The object pool to sweep.
    explicit GarbageCollector(ObjectPool& pool);

    /// Perform a full mark-and-sweep collection.
    /// @param roots Live root objects (from stack, globals, call frames).
    /// @return Number of objects collected.
    size_t collect(std::span<TypedObject* const> roots);

  private:
    /// Iteratively mark all objects reachable from roots.
    void mark(std::span<TypedObject* const> roots);

    /// Sweep unmarked objects: release their marked children's refcounts, then deallocate.
    /// @return Number of objects collected.
    size_t sweep();

    ObjectPool& _pool;
    std::unordered_set<TypedObject*> _marked;
};

} // namespace CoreVM
