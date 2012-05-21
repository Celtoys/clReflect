
//
// ===============================================================================
// clReflect, FunctionCall.h - A basic function call API for calling reflected
// functions at runtime.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include "clcpp.h"


namespace clcpp
{
	//
	// This code will call reflected functions with signatures that you assume the functions to hold.
	// If the signature you assume is different to the actual signature then your program is likely to
	// become unstable or present a security risk. It's not recommended that you use these functions
	// in the general case and instead build your own function library which performs parameter checking.
	//
	// Note that you can use Koenig lookup to avoid having to specify the clcpp namespace when calling
	// these functions.
	//


	//
	// This class stores references to objects so that those references can be preserved and safely
	// passed through compiler-generated proxy functions. If you use CallFunction to call functions which
	// accept references as parameters, pass an object of this type instead.
	//
	template <typename TYPE>
	class RefParam
	{
	public:
		RefParam(TYPE& reference)
			: m_Reference(reference)
		{
		}

	private:
		TYPE& m_Reference;
	};


	//
	// Function adaptor for generating a RefParam when passing arguments by reference to CallFunction
	//
	template <typename TYPE>
	RefParam<TYPE> ByRef(TYPE& reference)
	{
		return RefParam<TYPE>(reference);
	}


	//
	// Call a function with no parameters and to return value.
	//
	inline void CallFunction(const Function* function)
	{
		typedef void (*CallFunc)();
		CallFunc call_func = (CallFunc)function->address;
		internal::Assert(call_func != 0);
		call_func();
	}


	//
	// Call a function with one parameter and no return value.
	//
	template <typename A0> inline void CallFunction(const Function* function, const A0& a0)
	{
		typedef void (*CallFunc)(A0);
		CallFunc call_func = (CallFunc)function->address;
		internal::Assert(call_func != 0);
		call_func(a0);
	}


	//
	// Call a function with two parameters and no return value.
	//
	template <typename A0, typename A1> inline void CallFunction(const Function* function, const A0& a0, const A1& a1)
	{
		typedef void (*CallFunc)(A0, A1);
		CallFunc call_func = (CallFunc)function->address;
		internal::Assert(call_func != 0);
		call_func(a0, a1);
	}
}