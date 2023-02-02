#ifndef microcodefunc_h
#define microcodefunc_h

#pragma once
#include <type_traits>
#include <concepts>

// microcode_func
template<typename T>
struct microcode_func;

template<typename R, typename... Args>
struct microcode_func<R(Args...)> {
    using function_type = R(Args...);
    using return_type = R;

    function_type& func;

    microcode_func(function_type* f) : func(*f) {}

    return_type operator()(Args... args) {
        return func(std::forward<Args>(args)...);
    }
};

template<typename F>
microcode_func<std::remove_pointer_t<std::decay_t<F>>> make_microcode_func(F&& f) {
    return microcode_func<std::remove_pointer_t<std::decay_t<F>>>(std::forward<F>(f));
}

// Concept to ensure that all types are <microcode_func>s
template<typename T>
concept is_microcode_func = std::same_as<T, microcode_func<typename T::function_type>>;

#endif