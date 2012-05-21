
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "DatabaseMetadata.h"


cldb::meta::DatabaseTypes::DatabaseTypes()
{
	// Create field descriptions for each database type that has some

	DatabaseField primitive_fields[] =
	{
		DatabaseField(&cldb::Primitive::kind),
		DatabaseField(&cldb::Primitive::name),
		DatabaseField(&cldb::Primitive::parent),
	};

	DatabaseField type_fields[] =
	{
		DatabaseField(&cldb::Type::size),
	};

	DatabaseField enum_constant_fields[] =
	{
		DatabaseField(&cldb::EnumConstant::value),
	};

	DatabaseField field_fields[] =
	{
		DatabaseField(&cldb::Field::type),
		DatabaseField(&cldb::Field::qualifier),
		DatabaseField(&cldb::Field::offset),
		DatabaseField(&cldb::Field::parent_unique_id),
	};

	DatabaseField function_fields[] =
	{
		DatabaseField(&cldb::Function::unique_id),
	};

	DatabaseField template_type_fields[] =
	{
		DatabaseField(&cldb::TemplateType::parameter_types),
		DatabaseField(&cldb::TemplateType::parameter_ptrs),
	};

	DatabaseField int_attribute_fields[] =
	{
		DatabaseField(&cldb::IntAttribute::value),
	};

	DatabaseField float_attribute_fields[] =
	{
		DatabaseField(&cldb::FloatAttribute::value),
	};

	DatabaseField primitive_attribute_fields[] =
	{
		DatabaseField(&cldb::PrimitiveAttribute::value),
	};

	DatabaseField text_attribute_fields[] =
	{
		DatabaseField(&cldb::TextAttribute::value),
	};

	DatabaseField container_info_fields[] =
	{
		DatabaseField(&cldb::ContainerInfo::name),
		DatabaseField(&cldb::ContainerInfo::read_iterator_type),
		DatabaseField(&cldb::ContainerInfo::write_iterator_type),
		DatabaseField(&cldb::ContainerInfo::flags),
		DatabaseField(&cldb::ContainerInfo::count),
	};

	DatabaseField inheritance_fields[] =
	{
		DatabaseField(&cldb::TypeInheritance::name),
		DatabaseField(&cldb::TypeInheritance::derived_type),
		DatabaseField(&cldb::TypeInheritance::base_type),
	};

	// Create the descriptions of each type
	m_PrimitiveType.Type<cldb::Primitive>().Fields(primitive_fields);
	m_TypeType.Type<cldb::Type>().Base(&m_PrimitiveType).Fields(type_fields);
	m_EnumConstantType.Type<cldb::EnumConstant>().Base(&m_PrimitiveType).Fields(enum_constant_fields);
	m_EnumType.Type<cldb::Enum>().Base(&m_TypeType);
	m_FieldType.Type<cldb::Field>().Base(&m_PrimitiveType).Fields(field_fields);
	m_FunctionType.Type<cldb::Function>().Base(&m_PrimitiveType).Fields(function_fields);
	m_ClassType.Type<cldb::Class>().Base(&m_TypeType);
	m_TemplateType.Type<cldb::Template>().Base(&m_PrimitiveType);
	m_TemplateTypeType.Type<cldb::TemplateType>().Base(&m_TypeType).Fields(template_type_fields);
	m_NamespaceType.Type<cldb::Namespace>().Base(&m_PrimitiveType);

	// Create descriptions of each attribute type
	m_FlagAttributeType.Type<cldb::FlagAttribute>().Base(&m_PrimitiveType);
	m_IntAttributeType.Type<cldb::IntAttribute>().Base(&m_PrimitiveType).Fields(int_attribute_fields);
	m_FloatAttributeType.Type<cldb::FloatAttribute>().Base(&m_PrimitiveType).Fields(float_attribute_fields);
	m_PrimitiveAttributeType.Type<cldb::PrimitiveAttribute>().Base(&m_PrimitiveType).Fields(primitive_attribute_fields);
	m_TextAttributeType.Type<cldb::TextAttribute>().Base(&m_PrimitiveType).Fields(text_attribute_fields);

	// Create descriptions of the container type
	m_ContainerInfoType.Type<cldb::ContainerInfo>().Fields(container_info_fields);

	// Create descriptions of inheritance
	m_InheritanceType.Type<cldb::TypeInheritance>().Fields(inheritance_fields);
}