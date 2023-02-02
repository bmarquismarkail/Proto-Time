#include <cstddef>

// Wrapper to determine the number or arguments in a function
template <class... C>
struct arity;

template <class F, class... Args>
struct arity<F(Args...)> {
    static constexpr std::size_t value = sizeof...(Args);
};

template <class C>
inline constexpr std::size_t arity_v = arity<C>::value;