#include "helper.hpp"

args parse_args(int const argc, char const * const * const argv)
{
	args args;
	bool positional_only = false;
	for (auto& i : args.flags)
		i = false;
	for (auto const i : range(argc))
	{
		std::string_view curr = argv[i];
		if ((curr[0] == '-') && !positional_only)
		{
			if (curr.size() == 1)
			{
				positional_only = true;
			}
			else if (curr[1] == '-')
			{
				curr.remove_prefix(2);
				if (!curr.size())
				{
					positional_only = true;
				}
				else if(auto const idx = curr.find('='); idx == curr.npos)
				{
					args.named[curr] = "";
				}
				else
				{
					args.named[curr.substr(0, idx)] = curr.substr(idx+1);
				}
			}
			else
			{
				curr.remove_prefix(1);
				for (auto const j : curr)
				{
					args.flags[static_cast<std::size_t>(j)] = true;
				}
			}
		}
		else
		{
			args.positional.push_back(curr);
		}	
	}
	return args;
}
