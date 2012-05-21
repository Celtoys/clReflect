
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>


clcpp_reflect(TestTypedefs)
namespace TestTypedefs
{
	class AliasedType
	{
	};

	template <typename TYPE>
	class AliasedTemplate
	{
	};

	typedef AliasedType Typedef;
	typedef AliasedType* TypedefPtr;
	typedef AliasedType& TypedefRef;
	typedef const AliasedType* ConstTypedefPtr;
	typedef const AliasedType& ConstTypedefRef;

	typedef AliasedTemplate<int> TypedefTemplate;
	typedef AliasedTemplate<int>* TypedefTemplatePtr;
	typedef AliasedTemplate<int>& TypedefTemplateRef;
	typedef const AliasedTemplate<int>* ConstTypedefTemplatePtr;
	typedef const AliasedTemplate<int>& ConstTypedefTemplateRef;

	struct Container
	{
		typedef AliasedType Typedef;
		typedef AliasedType* TypedefPtr;
		typedef AliasedType& TypedefRef;
		typedef const AliasedType* ConstTypedefPtr;
		typedef const AliasedType& ConstTypedefRef;

		typedef AliasedTemplate<int> TypedefTemplate;
		typedef AliasedTemplate<int>* TypedefTemplatePtr;
		typedef AliasedTemplate<int>& TypedefTemplateRef;
		typedef const AliasedTemplate<int>* ConstTypedefTemplatePtr;
		typedef const AliasedTemplate<int>& ConstTypedefTemplateRef;
	};


	struct Fields
	{
		Typedef a;
		TypedefPtr b;
		TypedefRef c;
		ConstTypedefPtr d;
		ConstTypedefRef e;

		TypedefTemplate f;
		TypedefTemplatePtr g;
		TypedefTemplateRef h;
		ConstTypedefTemplatePtr i;
		ConstTypedefTemplateRef j;
	};
}


void TestTypedefsFunc(clcpp::Database& db)
{
}