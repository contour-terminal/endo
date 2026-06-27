// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Portable C++23 coroutine generator.
///
/// Prefers the standard `std::generator` (P2502) where the standard library
/// provides it, and otherwise falls back to a minimal, self-contained
/// implementation built on the core `<coroutine>` header. The fallback exists
/// because some standard libraries (notably libc++ at the time of writing) ship
/// `<coroutine>` but not yet `<generator>`. Both spellings expose the same
/// `endo::Generator<T>` alias so call sites are identical across platforms.

// Define ENDO_GENERATOR_FORCE_FALLBACK to compile the self-contained fallback even
// when std::generator is available — used to exercise the fallback on platforms whose
// standard library ships <generator> (e.g. MSVC STL).
#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L && !defined(ENDO_GENERATOR_FORCE_FALLBACK)

    #include <generator>

namespace endo
{
/// Lazy, single-pass coroutine generator yielding values of type @c T.
template <typename T>
using Generator = std::generator<T>;
} // namespace endo

#else

    #include <coroutine>

    #if !defined(__cpp_impl_coroutine) || __cpp_impl_coroutine < 201902L
        #error "endo::Generator requires C++20 coroutine language support (__cpp_impl_coroutine)."
    #endif

    #include <exception>
    #include <iterator>
    #include <utility>

namespace endo
{

/// Lazy, single-pass coroutine generator yielding values of type @c T.
///
/// A coroutine returning `Generator<T>` produces values lazily via `co_yield`.
/// The generator is a move-only, input range: iterate it with a range-based for
/// loop, which resumes the coroutine for each element. Breaking out of the loop
/// destroys the (suspended) coroutine frame.
///
/// Lifetime invariant: each yielded value lives in the coroutine frame while the
/// coroutine is suspended at its `co_yield`, so the reference obtained from the
/// iterator is valid until the next increment. Yield owning values (not views
/// into a buffer that is overwritten after the yield).
///
/// @tparam T The element type produced by the generator.
template <typename T>
class Generator
{
  public:
    /// The coroutine promise that records the currently-yielded value and any
    /// exception escaping the coroutine body. The coroutine machinery refers to
    /// it through the mandatory `promise_type` alias below; the struct itself is
    /// named per the project convention.
    struct PromiseType
    {
        T const* value = nullptr;     ///< Points at the value live in the frame.
        std::exception_ptr exception; ///< Captured exception, rethrown to the consumer.

        /// @return The owning generator wrapping this coroutine.
        Generator get_return_object() noexcept
        {
            return Generator { std::coroutine_handle<PromiseType>::from_promise(*this) };
        }

        /// Start suspended so the first value is produced on the first iteration.
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }

        /// Suspend at the end so the handle stays valid for `done()` queries.
        [[nodiscard]] std::suspend_always final_suspend() const noexcept { return {}; }

        /// Records the address of the yielded value and suspends.
        std::suspend_always yield_value(T const& v) noexcept
        {
            value = std::addressof(v);
            return {};
        }

        void return_void() const noexcept {}

        /// Captures an exception escaping the coroutine body for later rethrow.
        void unhandled_exception() noexcept { exception = std::current_exception(); }

        /// This is a synchronous pull-range; `co_await` is not supported.
        void await_transform() = delete;
    };

    /// Name the coroutine machinery requires; the C++ standard looks up
    /// `Generator<T>::promise_type` to drive the coroutine.
    using promise_type = PromiseType;
    using handle_type = std::coroutine_handle<PromiseType>;

    /// Sentinel marking the end of the sequence.
    struct Sentinel
    {
    };

    /// Input iterator that resumes the coroutine on increment.
    class Iterator
    {
      public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = T const&;
        using pointer = T const*;

        Iterator() noexcept = default;

        explicit Iterator(handle_type handle) noexcept: _handle(handle) {}

        Iterator& operator++()
        {
            resume();
            return *this;
        }

        void operator++(int) { resume(); }

        [[nodiscard]] reference operator*() const noexcept { return *_handle.promise().value; }

        [[nodiscard]] pointer operator->() const noexcept { return _handle.promise().value; }

        [[nodiscard]] bool operator==(Sentinel /*end*/) const noexcept { return !_handle || _handle.done(); }

      private:
        void resume()
        {
            _handle.resume();
            if (_handle.done())
                rethrowIfFailed(_handle);
        }

        handle_type _handle {};
    };

    Generator() noexcept = default;

    explicit Generator(handle_type handle) noexcept: _handle(handle) {}

    Generator(Generator&& other) noexcept: _handle(std::exchange(other._handle, {})) {}

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other)
        {
            if (_handle)
                _handle.destroy();
            _handle = std::exchange(other._handle, {});
        }
        return *this;
    }

    Generator(Generator const&) = delete;
    Generator& operator=(Generator const&) = delete;

    ~Generator()
    {
        if (_handle)
            _handle.destroy();
    }

    /// Resumes to the first element and returns an iterator to it.
    [[nodiscard]] Iterator begin()
    {
        if (_handle)
        {
            _handle.resume();
            if (_handle.done())
                rethrowIfFailed(_handle);
        }
        return Iterator { _handle };
    }

    [[nodiscard]] Sentinel end() noexcept { return {}; }

  private:
    /// Rethrows an exception captured by the coroutine body, if any.
    static void rethrowIfFailed(handle_type handle)
    {
        if (auto& e = handle.promise().exception)
            std::rethrow_exception(e);
    }

    handle_type _handle {};
};

} // namespace endo

#endif
