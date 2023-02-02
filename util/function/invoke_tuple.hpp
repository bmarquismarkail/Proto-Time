#include <utility>
//*******************************************************************************
// Helper Function to invoke depending on return type, might not be needed.
template <typename F, typename... Args>
std::invoke_result_t<F, Args...> invoke_helper(F& f,
                                               std::tuple<Args...>& args) {
    if constexpr (std::is_void_v<std::invoke_result_t<F, Args...>>) {
        std::apply(f, args);
    } else {
        return std::apply(f, args);
    }
}

template <class FuncList, std::size_t funcInd = 0, std::size_t retInd = 0,
          std::size_t argInd = 0>
constexpr void invoke_all(FuncList func_list, auto return_list,
                          auto argument_list) {
    using func_t =
        std::remove_pointer_t<typename std::tuple_element_t<funcInd, FuncList>>;
    constexpr size_t f_arity = arity_v<func_t>;
    constexpr bool ret_void = invocable_returns_void<func_t>::value;

    constexpr std::size_t nextFuncInd = funcInd + 1;
    constexpr std::size_t nextRetInd = retInd + (ret_void ? 0 : 1);
    constexpr std::size_t nextArgInd = argInd + f_arity;

    auto arg_sequence = make_integer_sequence<argInd, f_arity>{};
    auto arg_tuple = tuple_selector(argument_list, arg_sequence);
    // check if the function returns void
    if constexpr (ret_void) {
        invoke_helper(std::get<funcInd>(func_list), arg_tuple);
    } else {
        std::get<retInd>(return_list) =
            invoke_helper(std::get<funcInd>(func_list), arg_tuple);
    }
    if constexpr (nextFuncInd < std::tuple_size_v<FuncList>) {
        invoke_all<FuncList, nextFuncInd, nextRetInd, nextArgInd>(
            func_list, return_list, argument_list);
    }
}