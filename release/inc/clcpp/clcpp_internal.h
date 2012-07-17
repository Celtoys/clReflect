
#include "clcpp.h"


namespace clcpp
{
	namespace internal
	{
		//
		// Point to the runtime addresses of the GetType family of functions so that
		// the values that they return can be patched at runtime.
		//
		struct GetTypeFunctions
		{
			unsigned int type_hash;
            clcpp::pointer_type get_typename_address;
            clcpp::pointer_type get_type_address;
		};


		//
		// Memory-mapped representation of the entire reflection database
		//
		struct DatabaseMem
		{
			DatabaseMem();

			// The address to subtract when rebasing function addresses
            clcpp::pointer_type function_base_address;

			// Raw allocation of all null-terminated name strings
			const char* name_text_data;

			// Mapping from hash to text string
			CArray<Name> names;

			// Ownership storage of all referenced primitives
			CArray<Type> types;
			CArray<EnumConstant> enum_constants;
			CArray<Enum> enums;
			CArray<Field> fields;
			CArray<Function> functions;
			CArray<Class> classes;
			CArray<Template> templates;
			CArray<TemplateType> template_types;
			CArray<Namespace> namespaces;

			// Raw allocation of all null-terminated text attribute strings
			const char* text_attribute_data;

			// Ownership storage of all attributes
			CArray<FlagAttribute> flag_attributes;
			CArray<IntAttribute> int_attributes;
			CArray<FloatAttribute> float_attributes;
			CArray<PrimitiveAttribute> primitive_attributes;
			CArray<TextAttribute> text_attributes;

			// A list of references to all types, enums and classes for potentially quicker
			// searches during serialisation
			CArray<const Type*> type_primitives;

			// A list of all GetType function addresses paired to their type
			CArray<GetTypeFunctions> get_type_functions;

			// A list of all registered containers
			CArray<ContainerInfo> container_infos;

			// The root namespace that allows you to reach every referenced primitive
			Namespace global_namespace;
		};


		//
		// Header for binary database file
		//
		struct DatabaseFileHeader
		{
			// Initialises the file header to the current supported version
			DatabaseFileHeader();

			// Signature and version numbers for verifying header integrity
			// TODO: add check to pervent loading a 64-bit database from 32-bit
			// runtime system, or vice versa
			unsigned int signature0;
			unsigned int signature1;
			unsigned int version;

			int nb_ptr_schemas;
			int nb_ptr_offsets;
			int nb_ptr_relocations;

			clcpp::size_type data_size;

			// TODO: CRC verify?
		};
	}
}