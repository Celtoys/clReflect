
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/FieldVisitor.h>
#include <clcpp/clcpp.h>
#include <clcpp/Containers.h>


namespace
{
	void VisitTemplateTypeFields(char* object, const clcpp::TemplateType* template_type, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type);
	void VisitClassFields(char* object, const clcpp::Class* class_type, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type);


	void VisitField(char* object, const clcpp::Type* type, const clcpp::Qualifier& qualifier, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type)
	{
		// Chain to callback for pointers - no deep following
		if (qualifier.op == clcpp::Qualifier::POINTER)
		{
			visitor.Visit(object, type, qualifier);
			return;
		}

		// Dispatch based on kind
		switch (type->kind)
		{
		case clcpp::Primitive::KIND_TYPE:
		case clcpp::Primitive::KIND_ENUM:
			if (visit_type == clutl::VFT_All)
				visitor.Visit(object, type, qualifier);
			break;

		case clcpp::Primitive::KIND_CLASS:
			VisitClassFields(object, type->AsClass(), visitor, visit_type);
			break;

		case clcpp::Primitive::KIND_TEMPLATE_TYPE:
			VisitTemplateTypeFields(object, type->AsTemplateType(), visitor, visit_type);
			break;

		default:
			clcpp::internal::Assert(false && "Invalid primitive kind for type");
		}
	}


	void VisitContainerFields(clcpp::ReadIterator& reader, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type)
	{
		// Visit each entry in the container - keys are discarded
		clcpp::Qualifier qualifer(reader.m_ValueIsPtr ? clcpp::Qualifier::POINTER : clcpp::Qualifier::VALUE, false);
		for (unsigned int i = 0; i < reader.m_Count; i++)
		{
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();
			VisitField((char*)kv.value, reader.m_ValueType, qualifer, visitor, visit_type);
			reader.MoveNext();
		}
	}


	void VisitTemplateTypeFields(char* object, const clcpp::TemplateType* template_type, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type)
	{
		// Visit the template type container if there are any entries
		if (template_type->ci != 0)
		{
			clcpp::ReadIterator reader(template_type, object);
			if (reader.m_Count != 0)
				VisitContainerFields(reader, visitor, visit_type);
			return;
		}

		// Template types have no fields; just bases
		for (unsigned int i = 0; i < template_type->base_types.size; i++)
			VisitField(object, template_type->base_types[i], clcpp::Qualifier(), visitor, visit_type);
	}


	void VisitClassFields(char* object, const clcpp::Class* class_type, const clutl::IFieldVisitor& visitor, clutl::VisitFieldType visit_type)
	{
		// Visit all fields in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		for (unsigned int i = 0; i < fields.size; i++)
		{
			const clcpp::Field* field = fields[i];

			// Visit the field container if there are any entries
			if (field->ci != 0)
			{
				clcpp::ReadIterator reader(field, object + field->offset);
				if (reader.m_Count != 0)
					VisitContainerFields(reader, visitor, visit_type);
				continue;
			}

			VisitField(object + field->offset, field->type, field->qualifier, visitor, visit_type);
		}

		// Visit the base types at the same offset
		for (unsigned int i = 0; i < class_type->base_types.size; i++)
			VisitField(object, class_type->base_types[i], clcpp::Qualifier(), visitor, visit_type);
	}
}


void clutl::VisitFields(void* object, const clcpp::Type* type, const IFieldVisitor& visitor, VisitFieldType visit_type)
{
	VisitField((char*)object, type, clcpp::Qualifier(), visitor, visit_type);
}
