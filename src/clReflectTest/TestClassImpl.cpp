
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <clcpp/FunctionCall.h>


// Reflect the entire namespace and implement each class
clcpp_reflect(TestClassImpl)
namespace TestClassImpl
{
	class A
	{
	public:
		A()
		{
			x = 1;
			y = 2;
			z = 3;
		}

		int x, y, z;
	};

	struct B
	{
		B()
		{
			a = 1.5f;
			b = 2.5f;
			c = 3.5f;
		}

		float a, b, c;
	};
}

clcpp_impl_class(TestClassImpl::A)
clcpp_impl_class(TestClassImpl::B)

void TestConstructorDestructor(clcpp::Database& db)
{
	const clcpp::Class* ca = clcpp::GetType<TestClassImpl::A>()->AsClass();
	const clcpp::Class* cb = clcpp::GetType<TestClassImpl::B>()->AsClass();

	TestClassImpl::A* a = (TestClassImpl::A*)new char[sizeof(TestClassImpl::A)];
	TestClassImpl::B* b = (TestClassImpl::B*)new char[sizeof(TestClassImpl::B)];

	CallFunction(ca->constructor, a);
	CallFunction(cb->constructor, b);

	CallFunction(ca->destructor, a);
	CallFunction(cb->destructor, b);

	delete [] (char*) a;
	delete [] (char*) b;
}