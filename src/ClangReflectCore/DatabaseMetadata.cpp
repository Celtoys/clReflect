
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

	DatabaseField type_fields[] =
	{
		DatabaseField(&crdb::Type::size),
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
		DatabaseField(&crdb::Field::offset),
		DatabaseField(&crdb::Field::parent_unique_id),
	};

	DatabaseField function_fields[] =
	{
		DatabaseField(&crdb::Function::unique_id),
	};

	DatabaseField template_type_fields[] =
	{
		DatabaseField(&crdb::TemplateType::parameter_types),
		DatabaseField(&crdb::TemplateType::parameter_ptrs),
	};

	DatabaseField class_fields[] =
	{
		DatabaseField(&crdb::Class::base_class),
	};

	DatabaseField int_attribute_fields[] =
	{
		DatabaseField(&crdb::IntAttribute::value),
	};

	DatabaseField float_attribute_fields[] =
	{
		DatabaseField(&crdb::FloatAttribute::value),
	};

	DatabaseField name_attribute_fields[] =
	{
		DatabaseField(&crdb::NameAttribute::value),
	};

	DatabaseField text_attribute_fields[] =
	{
		DatabaseField(&crdb::TextAttribute::value),
	};

	// Create the descriptions of each type
	m_PrimitiveType.Type<crdb::Primitive>().Fields(primitive_fields);
	m_TypeType.Type<crdb::Type>().Base(&m_PrimitiveType).Fields(type_fields);
	m_EnumConstantType.Type<crdb::EnumConstant>().Base(&m_PrimitiveType).Fields(enum_constant_fields);
	m_EnumType.Type<crdb::Enum>().Base(&m_TypeType);
	m_FieldType.Type<crdb::Field>().Base(&m_PrimitiveType).Fields(field_fields);
	m_FunctionType.Type<crdb::Function>().Base(&m_PrimitiveType).Fields(function_fields);
	m_ClassType.Type<crdb::Class>().Base(&m_TypeType).Fields(class_fields);
	m_TemplateType.Type<crdb::Template>().Base(&m_PrimitiveType);
	m_TemplateTypeType.Type<crdb::TemplateType>().Base(&m_TypeType).Fields(template_type_fields);
	m_NamespaceType.Type<crdb::Namespace>().Base(&m_PrimitiveType);

	// Create descriptions of each attribute type
	m_FlagAttributeType.Type<crdb::FlagAttribute>().Base(&m_PrimitiveType);
	m_IntAttributeType.Type<crdb::IntAttribute>().Base(&m_PrimitiveType).Fields(int_attribute_fields);
	m_FloatAttributeType.Type<crdb::FloatAttribute>().Base(&m_PrimitiveType).Fields(float_attribute_fields);
	m_NameAttributeType.Type<crdb::NameAttribute>().Base(&m_PrimitiveType).Fields(name_attribute_fields);
	m_TextAttributeType.Type<crdb::TextAttribute>().Base(&m_PrimitiveType).Fields(text_attribute_fields);
}