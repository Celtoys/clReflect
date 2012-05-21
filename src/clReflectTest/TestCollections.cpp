
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <vector>
#include <map>


clcpp_reflect(TestCollections)
namespace TestCollections
{
	struct Struct
	{
		//std::vector<int> x;
	};
}

clcpp_reflect(std::vector)
