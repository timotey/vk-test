#ifndef HELPER_HPP_INCLUDED
#define HELPER_HPP_INCLUDED

#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>
#include <map>
#include <string_view>
#include <array>
#include <ranges>
#include <ranges>

template<class range_t = std::intmax_t>
class range{
	struct it_type{
		range_t val;
		range_t step;
		it_type& operator++() {
			val += step;
			return *this;
		}
		range_t operator * () {
			return val;
		}
		bool operator !=(it_type const& other) {
			return this->val != other.val;
		}
	} m_begin, m_end;
	public:
	range(range_t end):
		m_begin{.val = 0, .step = 1},
		m_end{.val = end, .step = 1} { }
	range(range_t beg, range_t end):
		m_begin{.val = beg, .step = (beg - end < 0 ? 1 : -1)},
		m_end{.val = end, .step = (beg - end < 0 ? 1 : -1)}
	{ }
	range(range_t beg, range_t end, range_t step):
		m_begin{.val = beg, .step = step},
		m_end{.val = end, .step = step}
	{ }
	it_type begin() const {
		return m_begin;
	}
	it_type end() const {
		return m_end;
	}
};

struct args{
	std::vector<std::string_view> positional;
	std::map<std::string_view, std::string_view> named;
	std::array<bool, 128> flags;
};

namespace utils{

template<bool c, class T>
using add_c = std::conditional_t<c, const T, T>;

template<bool C, class... Views> concept all_common_range  = (std::ranges::common_range       <add_c<C, Views>> && ... && true);
template<bool C, class... Views> concept all_range         = (std::ranges::range              <add_c<C, Views>> && ... && true);
template<bool C, class... Views> concept all_forward       = (std::ranges::forward_range      <add_c<C, Views>> && ... && true);
template<bool C, class... Views> concept all_bidirectional = (std::ranges::bidirectional_range<add_c<C, Views>> && ... && true);
template<bool C, class... Views> concept all_random_access = (std::ranges::random_access_range<add_c<C, Views>> && ... && true);

template<class... Views>
struct zip_view{
    std::tuple<Views...> vs;
    template<bool Constness, size_t... Is>
    class iterator{
        friend struct zip_view;
        static constexpr bool fwd = all_forward      <Constness, Views...>;
        static constexpr bool bidi= all_bidirectional<Constness, Views...>;
        static constexpr bool rnd = all_random_access<Constness, Views...>;
        //convenient type alias for a tuple type required by the standard
        template<bool b, template<class> class TypeTransformer>
        struct tuple {
            using type = std::tuple<typename TypeTransformer<Views>::type...>;
        };
        template<template<class> class TypeTransformer>
        struct tuple<true, TypeTransformer>{
            using type = std::tuple<typename TypeTransformer<Views>::type...>;
        };
        template<template<class> class TypeTransformer>
        using tuple_t = typename tuple<sizeof...(Views)==2, TypeTransformer>::type;

        template<class T> struct val_trans  { using type = std::ranges::range_value_t     <add_c<Constness, T>>; };
        template<class T> using  val_trans_t = typename val_trans<T>::type;
        template<class T> struct ref_trans  { using type = std::ranges::range_reference_t <add_c<Constness, T>>; };
        template<class T> using  ref_trans_t = typename  ref_trans<T>::type;
        template<class T> struct ptr_trans  { using type = std::ranges::range_value_t     <add_c<Constness, T>> *; };
        template<class T> using  ptr_trans_t = typename  ptr_trans<T>::type;
        template<class T> struct diff_trans { using type = std::ranges::range_difference_t<add_c<Constness, T>>; };
        template<class T> using  diff_trans_t= typename  diff_trans<T>::type;
        template<class T> struct iter_trans { using type = std::ranges::iterator_t        <add_c<Constness, T>>; };
        template<class T> using  iter_trans_t= typename  iter_trans<T>::type;

        std::tuple<iter_trans_t<Views>...> vals;
        iterator(iter_trans_t<Views>&&... vals):vals(vals...){}
    public:
        using iterator_concept =
            std::conditional_t<all_random_access<Constness, Views...>, std::random_access_iterator_tag,
            std::conditional_t<all_bidirectional<Constness, Views...>, std::bidirectional_iterator_tag,
            std::conditional_t<all_forward      <Constness, Views...>, std::forward_iterator_tag,
            std::input_iterator_tag>>>;
        using iterator_category = std::enable_if_t<all_forward<Constness, Views...>, std::input_iterator_tag>;
        using difference_type = std::common_type_t<diff_trans_t<Views>...>;
        using value_type = tuple_t<val_trans>; 
        using reference  = tuple_t<ref_trans>;
        using pointer    = tuple_t<ptr_trans>;

        /* input_iterator */
        reference operator*() const{
            return {*std::get<Is>(vals)...};
        }
        iterator& operator++(){
            (++std::get<Is>(vals),...);
            return *this;
        }
        iterator operator++(int){
            auto temp = *this;
            ++(*this);
            return temp;
        }

        /* forward_iterator */
        bool operator==(iterator const& other) const requires fwd{
            return ([&]{
                return std::get<Is>(vals) == std::get<Is>(other.vals);
            }() || ... || false);
        }
        bool operator!=(iterator const& other) const requires fwd{
            return !(*this == other);
        }

        /* bidirectional_iterator */
        iterator& operator--() requires bidi{
            return {--std::get<Is>(vals)...};
        }
        iterator operator--(int) requires bidi{
            auto temp = *this;
            --(*this);
            return temp;
        }

        /* random_access_iterator */
        iterator& operator+=(difference_type n) requires rnd{
            ([&]{std::get<Is>(vals)+=n;}(), ...);
            return *this;
        }
        iterator operator+(difference_type n) const requires rnd{
            return {std::get<Is>(vals)+n...};
        }
        friend iterator operator+(difference_type n, iterator const& i) requires rnd{
            return {n+std::get<Is>(i.vals)...};
        }
        iterator& operator-=(difference_type n) requires rnd{
            ([&]{std::get<Is>(vals)-=n;}(), ...);
            return *this;
        }
        iterator operator-(difference_type n) const requires rnd{
            return {std::get<Is>(vals)-n...};
        }
        friend iterator operator-(difference_type n, iterator const& i) requires rnd{
            return {n-std::get<Is>(i.vals)...};
        }
        reference operator[](difference_type n) const requires rnd{
            return {std::get<Is>(vals)[n]...};
        }
    };
    auto begin(){
        return [&]<size_t... Is>(std::index_sequence<Is...>){
            return iterator<false, Is...>{std::get<Is>(vs).begin()...};
        }(std::make_index_sequence<sizeof...(Views)>{});
    }
    auto begin() const requires all_range<true, Views...>{
        return [&]<size_t... Is>(std::index_sequence<Is...>){
            return iterator<false, Is...>{std::get<Is>(vs).begin()...};
        }(std::make_index_sequence<sizeof...(Views)>{});
    }
    auto end() requires all_common_range<false, Views...>{
        return [&]<size_t... Is>(std::index_sequence<Is...>){
            return iterator<false, Is...>{std::get<Is>(vs).end()...};
        }(std::make_index_sequence<sizeof...(Views)>{});
    }
    auto end() const requires all_common_range<true, Views...>{
        return [&]<size_t... Is>(std::index_sequence<Is...>){
            return iterator<false, Is...>{std::get<Is>(vs).end()...};
        }(std::make_index_sequence<sizeof...(Views)>{});
    }
};

template<class... Ts>
zip_view(std::tuple<Ts...>) -> zip_view<Ts...>;

inline constexpr auto zip = []<class... T>(T&&... vs){
    return zip_view<std::remove_reference_t<T>&...>{{std::forward<T>(vs)...}};
};

}

///
///@brief a function to parse c-style args to named, positional, and flags
///
///@param[in] argc count of arguments
///@param[in] argv argument values, their lifetime will not be manged by the args struct
///
args parse_args(int const argc, char const * const * const argv);

#endif // HELPER_HPP_INCLUDED
