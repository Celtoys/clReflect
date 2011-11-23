
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
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
		DatabaseField(&cldb::Type::base_type),
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

	DatabaseField name_attribute_fields[] =
	{
		DatabaseField(&cldb::NameAttribute::value),
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
	m_NameAttributeType.Type<cldb::NameAttribute>().Base(&m_PrimitiveType).Fields(name_attribute_fields);
	m_TextAttributeType.Type<cldb::TextAttribute>().Base(&m_PrimitiveType).Fields(text_attribute_fields);

	// Create descriptions of the container type
	m_ContainerInfoType.Type<cldb::ContainerInfo>().Fields(container_info_fields);
}