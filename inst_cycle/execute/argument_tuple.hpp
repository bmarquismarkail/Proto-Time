#ifndef arg_type_tupl
#define arg_type_tupl

template<class T>
struct arg_tuple_impl;

template<class F, class... Args>
struct arg_tuple_impl<F(Args...)>{
    static constexpr size_t num_args = sizeof...(Args);
    using type = std::conditional_t< (num_args > 1), std::tuple<std::tuple<Args...>>, 
        std::conditional_t<(num_args == 1), std::tuple<Args...>, std::tuple<>>>;
};
// Might need a catch-all for pointers, references, void and volitile, etc...
template<class F, class... Args>
struct arg_tuple_impl<F(*)(Args...)>{
    static constexpr size_t num_args = sizeof...(Args);
    using type = std::conditional_t< (num_args > 1), std::tuple<std::tuple<Args...>>, 
        std::conditional_t<(num_args == 1), std::tuple<Args...>, std::tuple<>>>;
};

template<class F, class... Fs>
struct argument_tuple{
    using type = decltype(std::tuple_cat(
        typename arg_tuple_impl<F>::type(), 
        typename arg_tuple_impl<Fs>::type()...));
};

//Helper Variable Template
template<class... Fs>
using argument_tuple_t = typename argument_tuple<Fs...>::type;

// Helper Function Template for std::tuple with Raw Function Pointers
template<class... Args>
auto generate_argument_tuple(std::tuple<Args...> t){
    return typename argument_tuple<Args...>::type{};
};

#endif