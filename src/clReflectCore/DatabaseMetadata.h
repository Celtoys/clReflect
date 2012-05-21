
//
// ===============================================================================
// clReflect, DatabaseMetadata.h - Metadata describing the types in the offline
// Reflection Database, used for more automated serialisation.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include "Database.h"


#ifdef __GNUC__

	// The offsetof macro of g++ version does not work with pointer to members.
	//
	// First g++ would expand
	// offset(type, field)
	// macro into:
	// ((size_t)(&((type *)0)-> field))
	//
	// Note that with g++ whose version is later than 3.5, offsetof would actually
	// expand to __builtin_offsetof, the behaviour remains the same
	//
	// So if we feed in data like following(example taken from line 86
	// in DatabaseMetadata.h):
	//
	// offsetof(CONTAINER_TYPE, *member)
	// It would expand to:
	// ((size_t)(&((CONTAINER_TYPE *)0)-> *member))
	//
	// First, with default macro expansion rule, g++ would add a space between
	// "->" and "*member", causing "->*" operator to become two operators, thus
	// breaking our code
	// Second, even if we eliminate the space some how, according to c++ operator
	// precedence, & is first evaluated on ((CONTAINER_TYPE *)0), then the result
	// is evaluated on ->*member, which is the wrong order.
	//
	// Considering all these cases, we provide a custom offsetof macro here which
	// is compatible with pointer to member given our requirements.
	//
	// TODO: We could've moved this into clReflectCore, but this macro is used in
	// clReflectCore, clReflectExport as well as clReflectTest. And currently we do
	// not have a header in clReflectCore for containing this sort of util macros(like
	// the Core.h here). Anyway, we may move this when we have such a header.
	#define POINTER_OFFSETOF(type, field) ((size_t)(&(((type *)0)->##field)))

#else

	// For MSVC, we can just use official offsetof macro
	#define POINTER_OFFSETOF(type, field) offsetof(type, field)

#endif  /* __GNUC__ */


namespace cldb
{
	namespace meta
	{
		//
		// All possible database field types
		//
		enum FieldType
		{
			FIELD_TYPE_NONE,
			FIELD_TYPE_BASIC,
			FIELD_TYPE_NAME,
			FIELD_TYPE_STRING,
		};

		
		//
		// A map from the compile-time field type to descriptive metadata
		//
		template <typename TYPE> struct FieldTypeTraits
		{
			static const FieldType type = FIELD_TYPE_BASIC;
			static const int packed_size = sizeof(TYPE);
		};
		template <> struct FieldTypeTraits<cldb::Name>
		{
			static const FieldType type = FIELD_TYPE_NAME;
			static const int packed_size = sizeof(cldb::u32);
		};
		template <> struct FieldTypeTraits<std::string>
		{
			static const FieldType type = FIELD_TYPE_STRING;
			static const int packed_size = sizeof(cldb::u32);
		};


		//
		// Description of a field within a database type
		//
		struct DatabaseField
		{
			// Basic constructors
			DatabaseField() : type(FIELD_TYPE_NONE), offset(0), size(0) { }
			DatabaseField(FieldType t, int o, int s) : type(t), offset(o), size(s) { }

			// Automatically deduce all the required field information from a member pointer
			template <typename FIELD_TYPE, typename CONTAINER_TYPE>
			DatabaseField(FIELD_TYPE (CONTAINER_TYPE::*member))
			{
				type = FieldTypeTraits<FIELD_TYPE>::type;
				count = 1;
				offset = POINTER_OFFSETOF(CONTAINER_TYPE, *member);

				size = sizeof(FIELD_TYPE);
				packed_size = FieldTypeTraits<FIELD_TYPE>::packed_size;

				// This will only get calculated when the field is added to a type
				packed_offset = 0;
			}

			template <typename FIELD_TYPE, typename CONTAINER_TYPE, int N>
			DatabaseField(FIELD_TYPE (CONTAINER_TYPE::*member)[N])
			{
				type = FieldTypeTraits<FIELD_TYPE>::type;
				count = N;
				offset = POINTER_OFFSETOF(CONTAINER_TYPE, *member);
				size = sizeof(FIELD_TYPE);
				packed_size = FieldTypeTraits<FIELD_TYPE>::packed_size;

				// This will only get calculated when the field is added to a type
				packed_offset = 0;
			}

			FieldType type;

			// Count in case this is an array
			int count;

			// Offset and size within the containing type
			int offset;
			int size;

			// Offset and size when binary packed in memory
			int packed_offset;
			int packed_size;
		};


		//
		// Description of a database type and its fields with function chained initialisation
		//
		struct DatabaseType
		{
			// An empty type
			DatabaseType() : size(0), packed_size(0), base_type(0) { }

			// Set the type
			template <typename TYPE>
			DatabaseType& Type()
			{
				size = sizeof(TYPE);
				return *this;
			}

			// Set the base class
			DatabaseType& Base(DatabaseType* base)
			{
				assert(packed_size == 0 && "Must set base before fields");
				base_type = base;
				packed_size = base->packed_size;
				return *this;
			}

			// Set the fields
			template <int N>
			DatabaseType& Fields(const DatabaseField (&df) [N])
			{
				// Copy the field array into the vector while calculating packed size info
				fields.resize(N);
				for (int i = 0; i < N; i++)
				{
					fields[i] = df[i];
					fields[i].packed_offset = packed_size;
					packed_size += fields[i].packed_size * fields[i].count;
				}

				return *this;
			}

			// Native and binary packed size
			int size;
			int packed_size;

			DatabaseType* base_type;

			std::vector<DatabaseField> fields;
		};


		struct DatabaseTypes
		{
			DatabaseTypes();

            template <typename TYPE> const DatabaseType& GetType() const;

			// All type descriptions
			DatabaseType m_PrimitiveType;
			DatabaseType m_TypeType;
			DatabaseType m_EnumConstantType;
			DatabaseType m_EnumType;
			DatabaseType m_FieldType;
			DatabaseType m_FunctionType;
			DatabaseType m_ClassType;
			DatabaseType m_TemplateType;
			DatabaseType m_TemplateTypeType;
			DatabaseType m_NamespaceType;

			// All attribute type descriptions
			DatabaseType m_FlagAttributeType;
			DatabaseType m_IntAttributeType;
			DatabaseType m_FloatAttributeType;
			DatabaseType m_PrimitiveAttributeType;
			DatabaseType m_TextAttributeType;

			// Container type description
			DatabaseType m_ContainerInfoType;

			// Inheritance type description
			DatabaseType m_InheritanceType;
		};

        // A compile-time map to runtime data for each database type
        template <typename TYPE> inline const DatabaseType& DatabaseTypes::GetType() const { }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Type>() const { return m_TypeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<EnumConstant>() const { return m_EnumConstantType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Enum>() const { return m_EnumType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Field>() const { return m_FieldType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Function>() const { return m_FunctionType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Class>() const { return m_ClassType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Template>() const { return m_TemplateType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<TemplateType>() const { return m_TemplateTypeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<Namespace>() const { return m_NamespaceType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<FlagAttribute>() const { return m_FlagAttributeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<IntAttribute>() const { return m_IntAttributeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<FloatAttribute>() const { return m_FloatAttributeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<PrimitiveAttribute>() const { return m_PrimitiveAttributeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<TextAttribute>() const { return m_TextAttributeType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<ContainerInfo>() const { return m_ContainerInfoType; }
        template <> inline const DatabaseType& DatabaseTypes::GetType<TypeInheritance>() const { return m_InheritanceType; }

	}
}
