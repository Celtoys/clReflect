
#include <clcpp/clcpp.h>


struct NonReflectedStruct
{
};


clcpp_reflect(TestTemplates)
namespace TestTemplates
{
	// Some structures for using as template arguments
	struct ParamStruct { };
	struct ParamClass { };
	enum ParamEnum { };

	// A basic template with one argument and a single specialisation
	template <typename TYPE> struct BasicTemplate { };
	template <> struct BasicTemplate<int> { };

	// A template with two arguments, a partial specialisation and full specialisation
	template <typename A, typename B> struct MultiSpecTemplate { };
	template <typename A> struct MultiSpecTemplate<A, char> { };
	template <> struct MultiSpecTemplate<short, long> { };

	template <typename TYPE> struct FwdDeclTemplate;
	template <typename TYPE> struct FwdDeclTemplate { };

	// Trigger a few warnings
	template <int INT> struct InvalidIntArgTemplate { };
	template <typename A, typename B, typename C, typename D, typename E> struct TooManyArgsTemplate { };


	struct Fields
	{
		// Just ensure these are usable
		ParamStruct a;
		ParamClass b;
		ParamEnum c;

		// Builtin, struct, class, enum, pointers and not
		BasicTemplate<int> BasicTemplateInt;
		BasicTemplate<int*> BasicTemplateIntPtr;
		BasicTemplate<ParamStruct> BasicTemplateStruct;
		BasicTemplate<ParamStruct*> BasicTemplateStructPtr;
		BasicTemplate<ParamClass> BasicTemplateClass;
		BasicTemplate<ParamClass*> BasicTemplateClassPtr;
		BasicTemplate<ParamEnum> BasicTemplateEnum;
		BasicTemplate<ParamEnum*> BasicTemplateEnumPtr;

		// Other template types as arguments
		BasicTemplate<BasicTemplate<int> > BasicTemplateBasicTemplateInt;
		BasicTemplate<BasicTemplate<int>*> BasicTemplateBasicTemplateIntPtr;

		// Multi-parameter templates
		MultiSpecTemplate<int, int*> MultiSpecTemplateIntIntPtr;
		MultiSpecTemplate<ParamStruct, ParamStruct*> MultiSpecTemplateParamStructParamStructPtr;
		MultiSpecTemplate<ParamClass, ParamClass*> MultiSpecTemplateParamClassParamClassPtr;
		MultiSpecTemplate<ParamEnum, ParamEnum*> MultiSpecTemplateParamEnumParamEnumPtr;
		MultiSpecTemplate<BasicTemplate<int>, BasicTemplate<int>*> MultiSpecTemplateBasicTemplateBasicTemplateIntBasicTemplateBasicTemplateIntPtr;

		// Duplicate uses of template types
		BasicTemplate<int> BasicTemplateIntA;
		BasicTemplate<int> BasicTemplateIntB;
		BasicTemplate<ParamStruct> BasicTemplateStructParamA;
		BasicTemplate<ParamStruct> BasicTemplateStructParamB;
		BasicTemplate<ParamClass> BasicTemplateClassParamA;
		BasicTemplate<ParamClass> BasicTemplateClassParamB;
		BasicTemplate<ParamEnum> BasicTemplateEnumParamA;
		BasicTemplate<ParamEnum> BasicTemplateEnumParamB;

		BasicTemplate<NonReflectedStruct> BasicTemplateNonReflectedStruct;
	};
}
