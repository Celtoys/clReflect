
//
// ===============================================================================
// clReflect, Arguments.h - Basic command-line parsing.
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

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
