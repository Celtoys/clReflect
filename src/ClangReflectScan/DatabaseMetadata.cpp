
#include "DatabaseMetadata.h"


crdb::meta::DatabaseTypes::DatabaseTypes()
{
	// Create field descriptions for each database type that has some

	DatabaseField primitive_fields[] =
	{
		DatabaseField(&crdb::Primitive::kind),
		DatabaseField(&crdb::Primitive::name),
		DatabaseField(&crdb::Primitive::parent),
	};

	DatabaseField class_fields[] =
	{
		DatabaseField(&crdb::Class::base_class),
	};

	DatabaseField enum_constant_fields[] =
	{
		DatabaseField(&crdb::EnumConstant::value),
	};

	DatabaseField field_fields[] =
	{
		DatabaseField(&crdb::Field::type),
		DatabaseField(&crdb::Field::modifier),
		DatabaseField(&crdb::Field::is_const),
		DatabaseField(&crdb::Field::index),
	};

	// Create the descriptions of each type
	m_PrimitiveType.Type<crdb::Primitive>().Fields(primitive_fields);
	m_NamespaceType.Type<crdb::Namespace>().Base(&m_PrimitiveType);
	m_TypeType.Type<crdb::Type>().Base(&m_PrimitiveType);
	m_ClassType.Type<crdb::Class>().Base(&m_TypeType).Fields(class_fields);
	m_EnumType.Type<crdb::Enum>().Base(&m_TypeType);
	m_EnumConstantType.Type<crdb::EnumConstant>().Base(&m_PrimitiveType).Fields(enum_constant_fields);
	m_FunctionType.Type<crdb::Function>().Base(&m_PrimitiveType);
	m_FieldType.Type<crdb::Field>().Base(&m_PrimitiveType).Fields(field_fields);
}