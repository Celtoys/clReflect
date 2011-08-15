
#pragma once


#include "Database.h"


namespace crcpp
{
	//
	// This code will call reflected functions with signatures that you assume the functions to hold.
	// If the signature you assume is different to the actual signature then your program is likely to
	// become unstable or present a security risk. It's not recommended that you use these functions
	// in the general case and instead build your own function library which performs parameter checking.
	//
	// Note that you can use Koenig lookup to avoid having to specify the crcpp namespace when calling
	// these functions.
	//


	//
	// Call a function with no parameters and to return value.
	//
	inline void CallFunction(const Function* function)
	{
		typedef void (*CallFunc)();
		CallFunc call_func = (CallFunc)function->address;
		call_func();
	}


	//
	// Call a function with one parameter and no return value.
	//
	template <typename A0> inline void CallFunction(const Function* function, A0 a0)
	{
		typedef void (*CallFunc)(A0);
		CallFunc call_func = (CallFunc)function->address;
		call_func(a0);
	}
}