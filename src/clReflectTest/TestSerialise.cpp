
#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>


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


	struct DerivedStruct : public BaseStruct
	{
		DerivedStruct()
			: x(1), y(2), z(3), w(4), e(VAL_B)
		{
		}
		DerivedStruct(NoInit) : BaseStruct(NO_INIT), n(NO_INIT) { }
		int x;
		float y;
		char z;
		double w;
		SomeEnum e;
		NestedStruct n;
	};
};


#include <stdio.h>


void TestSerialise(clcpp::Database& db)
{
	clutl::WriteBuffer write_buffer;

	Stuff::DerivedStruct src;
	clutl::SaveVersionedBinary(write_buffer, &src, clcpp::GetType<Stuff::DerivedStruct>());
	clutl::ReadBuffer read_buffer(write_buffer);
	Stuff::DerivedStruct dest(Stuff::NO_INIT);
	clutl::LoadVersionedBinary(read_buffer, &dest, clcpp::GetType<Stuff::DerivedStruct>());
}
