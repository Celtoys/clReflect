
#include <crcpp/crcpp.h>


// Reflect the entire namespace and implement each class
crcpp_reflect(TestClassImpl)
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

crcpp_impl_class(TestClassImpl::A, A)
crcpp_impl_class(TestClassImpl::B, B)


template <typename A0> void Call(const crcpp::Function* function, A0 a0)
{
	typedef void (*CallFunc)(A0);
	CallFunc call_func = (CallFunc)function->address;
	call_func(a0);
}


void TestConstructorDestructor(crcpp::Database& db)
{
	const crcpp::Class* ca = crcpp_get_type(db, TestClassImpl::A)->AsClass();
	const crcpp::Class* cb = crcpp_get_type(db, TestClassImpl::B)->AsClass();

	TestClassImpl::A* a = (TestClassImpl::A*)new char[sizeof(TestClassImpl::A)];
	TestClassImpl::B* b = (TestClassImpl::B*)new char[sizeof(TestClassImpl::B)];

	Call(ca->constructor, a);
	Call(cb->constructor, b);

	Call(ca->destructor, a);
	Call(cb->destructor, b);
	
	delete [] (char*) a;
	delete [] (char*) b;
}