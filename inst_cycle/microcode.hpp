#ifndef microcode_h
#define microcode_h

#include <type_traits>
#include "execute/return_tuple.hpp"
#include "execute/argument_tuple.hpp"
#include "microcodeFunc.hpp"
#include "../util/function/invoke_tuple.hpp"

namespace BMMQ
{

	template <is_microcode_func... fs>
	struct microcode
	{
		using argument_type = argument_tuple<fs...>::type;
		using return_type = return_type_tuple<fs...>::type;
		using func_list = std::tuple<fs...>;

		func_list f_list;

		microcode(fs... funcs) : f_list(std::make_tuple(funcs...))
		{
		}

		template <std::size_t N>
		auto get()
		{
			return std::get<N>(f_list);
		}

		template <std::size_t N, std::size_t cur = 0, std::size_t R = 0>
		std::size_t get_return_index()
		{
			// The return type of the function must not be void
			static_assert(std::negation<
							  std::is_void<
								  typename std::tuple_element_t<N, func_list>::return_type>>::value,
						  "Attempting to get a return type of a function returning void.");
			// size checks
			static_assert(N < std::tuple_size_v<std::decay_t<func_list>>);
			static_assert(N < std::tuple_size_v<return_type>);
			if constexpr (cur == N)
			{
				return R;
			}
			else
			{
				if constexpr (std::is_void<std::decay_t<std::tuple_element<cur, func_list>>>::value)
				{
					return get_return_index<N, cur + 1, R>();
				}
				else
					return get_return_index<N, cur + 1, R + 1>();
			}
		}

		// create argument_type
		argument_type get_Args() { return argument_type(); }
		return_type get_ret() { return return_type(); }

		return_type invoke(argument_type &args) const
		{
			return_type results{};
			invoke_all(f_list, results, args);
			return results;
		}
	};

	// helper code to make microcodes
	template <is_microcode_func... FS>
	auto make_microcode(FS... fs)
	{
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