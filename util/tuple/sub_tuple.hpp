#include <tuple>

// Helper functions to make a sub-tuple out of a main tuple.
template <std::size_t Start, std::size_t End, std::size_t... Seq>
struct make_integer_sequence_helper {
    using type = typename make_integer_sequence_helper<Start + 1, End, Seq...,
                                                       Start>::type;
};

template <std::size_t End, std::size_t... Seq>
struct make_integer_sequence_helper<End, End, Seq...> {
    using type = std::integer_sequence<std::size_t, Seq...>;
};

template <std::size_t Start, std::size_t Len>
using make_integer_sequence =
    typename make_integer_sequence_helper<Start, Start + Len>::type;

template <typename Tuple, std::size_t... Indices>
inline constexpr auto sub_tuple(
    Tuple&& tuple, std::integer_sequence<std::size_t, Indices...>) {
    return std::make_tuple(std::get<Indices>(std::forward<Tuple>(tuple))...);
}