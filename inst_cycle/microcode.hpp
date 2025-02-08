#ifndef microcode_h
#define microcode_h

#include <tuple>

namespace BMMQ {
  // Concept to check if a function is invocable with a given set of arguments
  template <typename F, typename... Args>
  concept Invocable = requires(F f, Args... args) {
      { f(std::forward<Args>(args)...) };
  };

  // Concept to ensure all Fs... are invocable with Args...
  template <typename... Fs, typename... Args>
  concept AllInvocable = (Invocable<Fs, Args...> && ...);

  template <typename... Fs>
  struct microcode {
      using func_list = std::tuple<Fs*...>;  // Store function pointers
      func_list functions;

      microcode(Fs&... funcs) : functions(std::make_tuple(&funcs...)) {}

      template <std::size_t N, typename... Args>
          requires Invocable<std::tuple_element_t<N, func_list>, Args...>
      auto invoke(Args... args) {
          return (*std::get<N>(functions))(std::forward<Args>(args)...);
      }
  };

    // helper code to make microcodes
    template <AllInvocable... FS>
    auto make_microcode(FS... fs) {
      return microcode<FS...>(fs...);
    }

    // Concept to ensure that all types are <microcode>s
    template <typename T>
    concept is_microcode = requires {
                   typename T::argument_type;
                   typename T::return_type;
   };
}
#endif
