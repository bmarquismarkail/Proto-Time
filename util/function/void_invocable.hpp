//*******************************************************************************
// metafunction to check for invokables returning void
template <typename F>
struct void_invocable;

template <typename F, typename... Args>
struct void_invocable<F(Args...)> {
    static constexpr bool value = std::is_same_v<F, void>;
};