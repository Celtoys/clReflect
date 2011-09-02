
#include <clcpp/clcpp.h>
#include <clutl/Containers.h>
#include <clutl/SerialiseVersionedBinary.h>


clcpp_reflect(Stuff)
namespace Stuff
{
	enum SomeEnum
	{
		VAL_A = 23,
		VAL_B = 51,
		VAL_C = 25,
	};

	enum NoInit { NO_INIT };


	struct BaseStruct
	{
		BaseStruct()
			: be(VAL_C), v0(0), v1(1)
		{
		}
		BaseStruct(NoInit) { }
		SomeEnum be;
		double v0;
		float v1;
	};

	
	struct NestedStruct
	{
		NestedStruct()
			: a(1), b(2), c(3), d(4), e(5), f(6), g(7), h(8), i(9)
		{
		}
		NestedStruct(NoInit) { }
		short a, b, c;
		char d, e, f;
		int g, h, i;
	};


	struct TestStruct : public BaseStruct
	{
		TestStruct()
			: x(1), y(2), z(3), w(4), e(VAL_B)
		{
		}
		TestStruct(NoInit) : BaseStruct(NO_INIT), n(NO_INIT) { }
		int x;
		float y;
		char z;
		double w;
		SomeEnum e;
		NestedStruct n;
	};
};


void TestSerialise(clcpp::Database& db)
{
	clutl::DataBuffer buffer(1024);

	int offset = (int)&(((Stuff::TestStruct*)0)->x);
	const clcpp::Class* cptr = clcpp_get_type(db, Stuff::TestStruct)->AsClass();
	const clcpp::Field* fptr = clcpp::FindPrimitive(cptr->fields, db.GetName("Stuff::TestStruct::x").hash);

	Stuff::TestStruct src;
	clutl::SaveVersionedBinary(buffer, &src, clcpp_get_type(db, Stuff::TestStruct));
	buffer.Reset();
	Stuff::TestStruct dest(Stuff::NO_INIT);
	clutl::LoadVersionedBinary(buffer, &dest, clcpp_get_type(db, Stuff::TestStruct));
}
