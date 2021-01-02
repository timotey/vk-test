#include <vector>
#include <map>
#include <string_view>
#include <array>

template<class range_t = std::intmax_t>
class range
{
	struct it_type
	{
		range_t val;
		range_t step;
		it_type& operator++()
		{
			val += step;
			return *this;
		}
		range_t operator * ()
		{
			return val;
		}
		bool operator !=(it_type const& other)
		{
			return this->val != other.val;
		}
	} m_begin, m_end;
	public:
	range(range_t end):
		m_begin{.val = 0, .step = 1},
		m_end{.val = end, .step = 1}
	{
	}
	range(range_t beg, range_t end):
		m_begin{.val = beg, .step = (beg - end < 0 ? 1 : -1)},
		m_end{.val = end, .step = (beg - end < 0 ? 1 : -1)}
	{
	}
	range(range_t beg, range_t end, range_t step):
		m_begin{.val = beg, .step = step},
		m_end{.val = end, .step = step}
	{
	}
	it_type begin() const
	{
		return m_begin;
	}
	it_type end() const
	{
		return m_end;
	}
};

struct args
{
	std::vector<std::string_view> positional;
	std::map<std::string_view, std::string_view> named;
	std::array<bool, 128> flags;
};

///
///@brief a function to parse c-style args to named, positional, and flags
///
///@param[in] argc count of arguments
///@param[in] argv argument values, their lifetime will not be manged by the args struct
///
args parse_args(int const argc, char const * const * const argv);
