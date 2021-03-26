
//
// ===============================================================================
// clReflect, FieldVisitor.h - Abstracts walking data structures via field members
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>


namespace clutl
{
	enum VisitFieldType
	{
		VFT_All,
		VFT_Pointers
	};


	// Implement this interface to receive a callback on each visited field.
	// This includes visits to objects within containers.
	// When visiting entries in a container, the 'field' pointer will point to the container field
	// itself and will have a different type to that pointed to by 'type'. The 'type' pointer will
	// be the type of the objects in the container.
	struct IFieldVisitor
	{
		virtual void Visit(void* object, const clcpp::Field* field, const clcpp::Type* type, const clcpp::Qualifier& qualifier) const = 0;
	};


	// Shallow visitation of all fields in an object, including the entries of any containers, any
	// base classes and nested data types.
	CLCPP_API void VisitFields(
		void* object,
		const clcpp::Type* type,
		const IFieldVisitor& visitor,
		VisitFieldType visit_type,
		unsigned int stop_flags);
}