#ifndef return_type_tupl
#define return_type_tupl

 #include <tuple>
 #include <type_traits>

template<typename T>
struct return_type_tuple_impl;

template<typename T, typename... Args>
struct return_type_tuple_impl<T(Args...)>{
    using type = std::conditional_t<
    std::is_void_v<std::invoke_result_t<T, Args...>>, 
    std::tuple<>, 
    std::tuple<std::invoke_result_t<T, Args...>>>;
};

// Might need a catch-all for pointers, references, void and volitile, etc...
template<class F, class... Args>
struct return_type_tuple_impl<F(*)(Args...)> : return_type_tuple_impl<F(Args...){};

template<class F, class... Fs>
struct return_type_tuple{
    using type = decltype(std::tuple_cat(
        typename return_type_tuple_impl<F>::type(), 
        typename return_type_tuple_impl<Fs>::type()...));
};

#endif