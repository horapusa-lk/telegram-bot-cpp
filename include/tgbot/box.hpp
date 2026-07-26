#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace tgbot {

/// @brief Deep-copying single-value indirection used to break reference cycles
/// in the generated API types while keeping value semantics.
///
/// Some Telegram types are recursive (a Message may contain another Message as
/// @c reply_to_message), and @c std::optional requires a complete type.  Fields
/// whose type belongs to the same strongly-connected component of the type
/// graph as their owner are therefore stored as
/// @c std::optional<tgbot::Box<T>>.  A @c Box behaves like the contained value:
/// it is deep-copied on copy, compared by value, and dereferenced with
/// @c * / @c ->.
///
/// A moved-from @c Box is empty; the only valid operations on it are
/// assignment and destruction.
///
/// @tparam T the boxed type; may be incomplete at declaration time.
template <typename T>
class Box {
public:
    /// Value-initializes the contained @c T.
    Box() : ptr_(new T()) {}

    /// Boxes a copy (or move) of @p value.
    ///
    /// Constrained to exactly @c T: the guard uses only @c std::decay /
    /// @c std::is_same, which never require @c T to be complete.  An
    /// unconstrained @c Box(T) would make every copy/conversion trait of
    /// @c Box instantiate @c T's converting constructors — for
    /// @c Box<std::variant<...>> fields that means completing the variant's
    /// alternatives inside their own definitions (libstdc++ rejects this).
    template <typename U, typename = std::enable_if_t<std::is_same_v<std::decay_t<U>, T>>>
    Box(U&& value) : ptr_(new T(std::forward<U>(value))) {}

    /// Deep copy: allocates a new @c T copied from @p other.
    Box(const Box& other) : ptr_(new T(*other.ptr_)) {}

    /// Move: steals the allocation, leaving @p other empty.
    Box(Box&& other) noexcept = default;

    /// Deep copy assignment.
    Box& operator=(const Box& other) {
        if (this != &other) {
            ptr_ = std::unique_ptr<T>(new T(*other.ptr_));
        }
        return *this;
    }

    /// Move assignment; leaves @p other empty.
    Box& operator=(Box&& other) noexcept = default;

    ~Box() = default;

    /// Dereferences to the contained value.
    T& operator*() noexcept { return *ptr_; }
    /// Dereferences to the contained value.
    const T& operator*() const noexcept { return *ptr_; }
    /// Member access into the contained value.
    T* operator->() noexcept { return ptr_.get(); }
    /// Member access into the contained value.
    const T* operator->() const noexcept { return ptr_.get(); }

    /// Compares the contained values.
    friend bool operator==(const Box& lhs, const Box& rhs) { return *lhs == *rhs; }

private:
    std::unique_ptr<T> ptr_;
};

}  // namespace tgbot
