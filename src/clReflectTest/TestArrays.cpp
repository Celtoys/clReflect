
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <clcpp/Containers.h>
#include <stdio.h>


clcpp_reflect(TestArrays)
namespace TestArrays
{
	struct S
	{
		int x[3];
		float y[30];
	};
}


void TestArraysFunc(clcpp::Database& db)
{
	TestArrays::S s;

	const clcpp::Class* type = clcpp::GetType<TestArrays::S>()->AsClass();
	for (unsigned int i = 0; i < type->fields.size; i++)
	{
		const clcpp::Field* field = type->fields[i];

		clcpp::WriteIterator writer;
		writer.Initialise(field, (char*)&s + field->offset);
		for (unsigned int j = 0; j < writer.m_Count; j++)
		{
			void* value_ptr = writer.AddEmpty();

			if (writer.m_ValueType == clcpp::GetType<float>())
				*(float*)value_ptr = (float)j;
			if (writer.m_ValueType == clcpp::GetType<int>())
				*(int*)value_ptr = j;
		}

		clcpp::ReadIterator reader(field, (char*)&s + field->offset);
		for (unsigned int j = 0; j < reader.m_Count; j++)
		{
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();

			if (reader.m_ValueType == clcpp::GetType<float>())
				printf("%f\n", *(float*)kv.value);
			if (reader.m_ValueType == clcpp::GetType<int>())
				printf("%d\n", *(int*)kv.value);

			reader.MoveNext();
		}
	}
}