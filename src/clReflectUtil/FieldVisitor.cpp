
#include <clutl/FieldVisitor.h>
#include <clcpp/clcpp.h>
#include <clcpp/Containers.h>


namespace
{
	void VisitTemplateTypeFields(char* object, const clcpp::TemplateType* template_type, const clutl::FieldDelegate& visitor);
	void VisitClassFields(char* object, const clcpp::Class* class_type, const clutl::FieldDelegate& visitor);


	void VisitField(char* object, const clcpp::Type* type, const clcpp::Qualifier& qualifier, const clutl::FieldDelegate& visitor)
	{
		// Chain to callback for pointers - no deep following
		if (qualifier.op == clcpp::Qualifier::POINTER)
		{
			visitor(object, type, qualifier);
			return;
		}

		// Dispatch based on kind
		switch (type->kind)
		{
		case clcpp::Primitive::KIND_TYPE:
		case clcpp::Primitive::KIND_ENUM:
			visitor(object, type, qualifier);
			break;

		case clcpp::Primitive::KIND_CLASS:
			VisitClassFields(object, type->AsClass(), visitor);
			break;

		case clcpp::Primitive::KIND_TEMPLATE_TYPE:
			VisitTemplateTypeFields(object, type->AsTemplateType(), visitor);
			break;

		default:
			clcpp::internal::Assert(false && "Invalid primitive kind for type");
		}
	}


	void VisitContainerFields(clcpp::ReadIterator& reader, const clutl::FieldDelegate& visitor)
	{
		// Visit each entry in the container - keys are discarded
		clcpp::Qualifier qualifer(reader.m_ValueIsPtr ? clcpp::Qualifier::POINTER : clcpp::Qualifier::VALUE, false);
		for (unsigned int i = 0; i < reader.m_Count; i++)
		{
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();
			VisitField((char*)kv.value, reader.m_ValueType, qualifer, visitor);
			reader.MoveNext();
		}
	}


	void VisitTemplateTypeFields(char* object, const clcpp::TemplateType* template_type, const clutl::FieldDelegate& visitor)
	{
		// Visit the template type container if there are any entries
		if (template_type->ci != 0)
		{
			clcpp::ReadIterator reader(template_type, object);
			if (reader.m_Count != 0)
				VisitContainerFields(reader, visitor);
			return;
		}

		// Template types have no fields; just bases
		for (int i = 0; i < template_type->base_types.size(); i++)
		{
			VisitField(object, template_type->base_types[i], clcpp::Qualifier(), visitor);
		}
	}


	void VisitClassFields(char* object, const clcpp::Class* class_type, const clutl::FieldDelegate& visitor)
	{
		// Visit all fields in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		for (int i = 0; i < fields.size(); i++)
		{
			const clcpp::Field* field = fields[i];

			// Visit the field container if there are any entries
			if (field->ci != 0)
			{
				clcpp::ReadIterator reader(field, object + field->offset);
				if (reader.m_Count != 0)
					VisitContainerFields(reader, visitor);
				continue;
			}

			VisitField(object + field->offset, field->type, field->qualifier, visitor);
		}

		// Visit the base types at the same offset
		for (int i = 0; i < class_type->base_types.size(); i++)
		{
			VisitField(object, class_type->base_types[i], clcpp::Qualifier(), visitor);
		}
	}
}


void clutl::VisitFields(void* object, const clcpp::Type* type, const FieldDelegate& visitor)
{
	VisitField((char*)object, type, clcpp::Qualifier(), visitor);
}
