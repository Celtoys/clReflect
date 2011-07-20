
//
// Metadata describing the database types in the crdb namespace for serialisation use.
//

#pragma once


#include "Database.h"


namespace crdb
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
			FIELD_TYPE_NAME
		};

		
		//
		// A map from the compile-time field type to descriptive metadata
		//
		template <typename TYPE> struct FieldTypeTraits
		{
			static const FieldType type = FIELD_TYPE_BASIC;
			static const int packed_size = sizeof(TYPE);
		};
		template <> struct FieldTypeTraits<crdb::Name>
		{
			static const FieldType type = FIELD_TYPE_NAME;
			static const int packed_size = sizeof(crdb::u32);
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
				offset = offsetof(CONTAINER_TYPE, *member);
				size = sizeof(FIELD_TYPE);
				packed_size = FieldTypeTraits<FIELD_TYPE>::packed_size;

				// This will only get calculated when the field is added to a type
				packed_offset = 0;
			}

			FieldType type;

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
					packed_size += fields[i].packed_size;
				}

				// Sum the packed size with all base packed sizes
				for (DatabaseType* base = base_type; base; base = base->base_type)
				{
					packed_size += base->packed_size;
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

			// A compile-time map to runtime data for each database type
			template <typename TYPE> const DatabaseType& GetType() const { }
			template <> const DatabaseType& GetType<Namespace>() const { return m_NamespaceType; }
			template <> const DatabaseType& GetType<Type>() const { return m_TypeType; }
			template <> const DatabaseType& GetType<Class>() const { return m_ClassType; }
			template <> const DatabaseType& GetType<Enum>() const { return m_EnumType; }
			template <> const DatabaseType& GetType<EnumConstant>() const { return m_EnumConstantType; }
			template <> const DatabaseType& GetType<Function>() const { return m_FunctionType; }
			template <> const DatabaseType& GetType<Field>() const { return m_FieldType; }

			// All type descriptions
			DatabaseType m_PrimitiveType;
			DatabaseType m_NamespaceType;
			DatabaseType m_TypeType;
			DatabaseType m_ClassType;
			DatabaseType m_EnumType;
			DatabaseType m_EnumConstantType;
			DatabaseType m_FunctionType;
			DatabaseType m_FieldType;
		};
	}
}