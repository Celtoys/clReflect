
//
// ===============================================================================
// clReflect, FieldVisitor.h - Abstracts walking data structures via field members
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


namespace clcpp
{
	struct Qualifier;
	struct Type;
	struct Class;
}


namespace clutl
{
	enum VisitFieldType
	{
		VFT_All,
		VFT_Pointers
	};


	// Implement this interface to receive a callback on each visited field
	struct IFieldVisitor
	{
		virtual void Visit(void* object, const clcpp::Type* type, const clcpp::Qualifier& qualifier) const = 0;
	};


	// Shallow visitation of all fields in an object, including the entries of any containers, any
	// base classes and nested data types.
	void VisitFields(void* object, const clcpp::Type* type, const IFieldVisitor& visitor, VisitFieldType visit_type);
}