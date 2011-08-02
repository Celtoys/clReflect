
#pragma once


#include <string>
#include <vector>


//
// Very simple command-line argument storage and query
//
struct Arguments
{
	Arguments(int argc, const char* argv[])
	{
		// Copy from the command-line into local storage
		args.resize(argc);
		for (size_t i = 0; i < args.size(); i++)
		{
			args[i] = argv[i];
		}
	}

	size_t Count() const
	{
		return args.size();
	}

	size_t GetIndexOf(const std::string& arg, int occurrence = 0) const
	{
		// Linear search for a matching argument
		int found = 0;
		for (size_t i = 0; i < args.size(); i++)
		{
			if (args[i] == arg)
			{
				if (found++ == occurrence)
				{
					return i;
				}
			}
		}

		return -1;
	}

	bool Have(const std::string& arg) const
	{
		// Does the specific argument exist?
		return GetIndexOf(arg) != -1;
	}

	std::string GetProperty(const std::string& arg, int occurrence = 0) const
	{
		// Does the arg exist and does it have a value
		size_t index = GetIndexOf(arg, occurrence);
		if (index == -1 || index + 1 >= args.size())
		{
			return "";
		}

		return args[index + 1];
	}

	const std::string& operator [] (int index) const
	{
		return args[index];
	}

	std::vector<std::string> args;
};
