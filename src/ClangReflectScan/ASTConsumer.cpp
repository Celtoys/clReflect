
// TODO: Is it worth reflecting anonymous enumerations, given they can only be used to pass function parameters?
// TODO: unnamed parameters
// TODO: inheritance
// TODO: Parameter names no longer need to be unique
// TODO: Do we really need fully qualified names at this point, given the use of a multimap?
// TODO: Could make the hash key a pair of name/parent. This would require multiple lookups but it's linear only with scope depth
//
// Primitives that require full names:
//
//    * Functions: No. Nothing references functions in code that we are going to reflect.
//    * Namespaces: No. Nothing references namespaces.
//    * Enums: Yes. These are types and can be used as fields.
//    * Classes: Yes. They can be used as fields.
//    * Fields: No. Nothing references fields.
//
// A downside of having everything named is that usually anonymous entities need to be catered for. An example
// is function return values - they're not named and would usually be stored as a property of the function.
// In this case we have to think up some valid name that doesn't collide with other names and that hopefully
// won't cause a CRC collision, either.
//
// I suppose we could have a list of un-named primitives in a list that can't really be hashed in any way.
// The really important requirement is that these un-named primitives can parent themselves correctly. As a
// result, they can't become parents themselves.
//
// On top of that, we lose order so each parameter will need to keep track of its index for functions.
//

#include "ASTConsumer.h"
#include "Database.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCxx.h"
#include "clang/AST/DeclGroup.h"


namespace
{
	void Remove(std::string& str, const std::string& remove_str)
	{
		for (size_t i; (i = str.find(remove_str)) != std::string::npos; )
		{
			str.replace(i, remove_str.length(), "");
		}
	}


	bool MakeField(crdb::Database& db, clang::QualType qual_type, const char* param_name, crdb::Name parent_name, int index, crdb::Field& field)
	{
		// Get type info for the field
		clang::SplitQualType sqt = qual_type.split();
		const clang::Type* type = sqt.first;

		// Only handle one level of recursion for pointers and references
		crdb::Field::Modifier pass = crdb::Field::MODIFIER_VALUE;

		// Get pointee type info if this is a pointer
		if (const clang::PointerType* ptr_type = dyn_cast<clang::PointerType>(type))
		{
			pass = crdb::Field::MODIFIER_POINTER;
			qual_type = ptr_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Get pointee type info if this is a reference
		else if (const clang::LValueReferenceType* ref_type = dyn_cast<clang::LValueReferenceType>(type))
		{
			pass = crdb::Field::MODIFIER_REFERENCE;
			qual_type = ref_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Record the qualifiers before stripping them and generating the type name
		clang::Qualifiers qualifiers = clang::Qualifiers::fromFastMask(sqt.second);
		qual_type.removeLocalFastQualifiers();
		std::string type_name_str = qual_type.getAsString();

		// Is this a field that can be safely recorded?
		type = sqt.first;
		clang::Type::TypeClass tc = type->getTypeClass();
		if (tc != clang::Type::Builtin && tc != clang::Type::Enum && tc != clang::Type::Elaborated && tc != clang::Type::Record)
		{
			return false;
		}

		// Pull the class descriptions from the type name
		Remove(type_name_str, "enum ");
		Remove(type_name_str, "struct ");
		Remove(type_name_str, "class ");

		// Construct the field
		crdb::Name type_name = db.GetName(type_name_str.c_str());
		field = crdb::Field(db.GetName(param_name), parent_name, type_name, pass, qualifiers.hasConst(), index);
		return true;
	}


	void MakeFunction(crdb::Database& db, clang::NamedDecl* decl, crdb::Name function_name, crdb::Name parent_name, std::vector<crdb::Field>& parameters)
	{
		// Cast to a function
		clang::FunctionDecl* function_decl = dyn_cast<clang::FunctionDecl>(decl);
		assert(function_decl != 0 && "Failed to cast to function declaration");

		// Only add the function once
		if (!function_decl->isFirstDeclaration())
		{
			return;
		}

		// Parse the return type
		crdb::Field return_parameter;
		if (!MakeField(db, function_decl->getResultType(), 0, function_name, -1, return_parameter))
		{
			printf("WARNING: Unsupported return type for %s\n", function_name->second.c_str());
			return;
		}

		// Try to gather every parameter successfully before adding the function
		int index = parameters.size();
		for (clang::FunctionDecl::param_iterator i = function_decl->param_begin(); i != function_decl->param_end(); ++i)
		{
			clang::ParmVarDecl* param_decl = *i;

			// Collect a list of constructed parameters in case evaluating one of them fails
			crdb::Field parameter;
			if (!MakeField(db, param_decl->getType(),param_decl->getNameAsString().c_str(), function_name, index++, parameter))
			{
				printf("WARNING: Unsupported parameter type for %s\n", param_decl->getNameAsString().c_str());
				return;
			}
			parameters.push_back(parameter);
		}

		// Add the function
		printf("function %s\n", function_name->second.c_str());
		db.AddPrimitive(crdb::Function(function_name, parent_name));

		// Only add the return parameter if it's non-void
		if (return_parameter.type->second != "void")
		{
			printf("   Returns: %s%s%s\n",
				return_parameter.is_const ? "const " : "",
				return_parameter.type->second.c_str(),
				return_parameter.modifier == crdb::Field::MODIFIER_POINTER ? "*" : return_parameter.modifier == crdb::Field::MODIFIER_REFERENCE ? "&" : "");
			db.AddPrimitive(return_parameter);
		}
		else
		{
			printf("   Returns: void (not added)\n");
		}

		// Add the parameters
		for (std::vector<crdb::Field>::iterator i = parameters.begin(); i != parameters.end(); ++i)
		{
			printf("   %s%s%s %s\n",
				i->is_const ? "const " : "",
				i->type->second.c_str(),
				i->modifier == crdb::Field::MODIFIER_POINTER ? "*" : i->modifier == crdb::Field::MODIFIER_REFERENCE ? "&" : "",
				i->name->second.c_str());
			db.AddPrimitive(*i);
		}
	}
}


ASTConsumer::ASTConsumer(clang::ASTContext& context) : m_ASTContext(context)
{
}


ASTConsumer::~ASTConsumer()
{
}


void ASTConsumer::HandleTopLevelDecl(clang::DeclGroupRef d)
{
	// Root namespace
	crdb::Name parent_name = m_DB.GetNoName();

	// Iterate over every named declaration
	for (clang::DeclGroupRef::iterator i = d.begin(); i != d.end(); ++i)
	{
		clang::NamedDecl* named_decl = dyn_cast<clang::NamedDecl>(*i);
		if (named_decl == 0)
		{
			continue;
		}

		// Filter out unsupported decls at the global namespace level
		clang::Decl::Kind kind = named_decl->getKind();
		switch (kind)
		{
		case (clang::Decl::Namespace):
		case (clang::Decl::CXXRecord):
		case (clang::Decl::Function):
		case (clang::Decl::Enum):
			AddDecl(named_decl, parent_name);
		}
	}
}


void ASTConsumer::AddDecl(clang::NamedDecl* decl, const crdb::Name& parent_name)
{
	// Generate a name for the decl
	//crdb::Name name = m_DB.GetName(decl->getDeclName().getAsString().c_str());
	crdb::Name name = m_DB.GetName(decl->getQualifiedNameAsString().c_str());

	clang::Decl::Kind kind = decl->getKind();
	switch (kind)
	{
	case (clang::Decl::Namespace): AddNamespaceDecl(decl, name, parent_name); break;
	case (clang::Decl::CXXRecord): AddClassDecl(decl, name, parent_name); break;
	case (clang::Decl::Enum): AddEnumDecl(decl, name, parent_name); break;
	case (clang::Decl::Function): AddFunctionDecl(decl, name, parent_name); break;
	case (clang::Decl::CXXMethod): AddMethodDecl(decl, name, parent_name); break;
	case (clang::Decl::Field): AddFieldDecl(decl, name, parent_name); break;
	}
}


void ASTConsumer::AddNamespaceDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// TODO: Anonymous namespaces

	// If the same namespace gets opened multiple times then that simply results in recreation of the same record
	// Due to the database nature of storage during collection, everything within the namespace is maintained
	printf("namespace %s\n", name->second.c_str());
	m_DB.AddPrimitive(crdb::Namespace(name, parent_name));
	AddContainedDecls(decl, name);
}


void ASTConsumer::AddClassDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// Cast to a record (NOTE: CXXRecord is a temporary clang type and will change in future revisions)
	clang::CXXRecordDecl* record_decl = dyn_cast<clang::CXXRecordDecl>(decl);
	assert(record_decl != 0 && "Failed to cast to record declaration");

	// Ignore forward declarations
	if (record_decl->isDefinition() == false)
	{
		return;
	}

	// Can only inherit from one base class for now - offsets change based on derived type
	if (record_decl->getNumBases() > 1)
	{
		printf("WARNING: Class %s has too many bases\n", name->second.c_str());
		return;
	}

	// Parse any base classes
	crdb::Name base_name = m_DB.GetNoName();
	if (record_decl->getNumBases() > 0)
	{
		// Can't support virtual base classes - offsets change at runtime
		clang::CXXBaseSpecifier& base = *record_decl->bases_begin();
		if (base.isVirtual())
		{
			printf("WARNING: Class has an unsupported virtual base class\n", name->second.c_str());
			return;
		}

		// Parse the type name
		clang::QualType base_type = base.getType();
		std::string type_name_str = base_type.getAsString();
		Remove(type_name_str, "struct ");
		Remove(type_name_str, "class ");
		base_name = m_DB.GetName(type_name_str.c_str());
	}

	// Add to the database
	printf("class %s\n", name->second.c_str());
	if (base_name != m_DB.GetNoName())
	{
		printf("   BASE: %s\n", base_name->second.c_str());
	}
	m_DB.AddPrimitive(crdb::Class(name, parent_name, base_name));
	AddContainedDecls(decl, name);
}


void ASTConsumer::AddEnumDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// Cast to an enum
	clang::EnumDecl* enum_decl = dyn_cast<clang::EnumDecl>(decl);
	assert(enum_decl != 0 && "Failed to cast to enum declaration");

	// Add to the database
	printf("enum %s\n", name->second.c_str());
	m_DB.AddPrimitive(crdb::Enum(name, parent_name));

	// Iterate over all constants
	for (clang::EnumDecl::enumerator_iterator i = enum_decl->enumerator_begin(); i != enum_decl->enumerator_end(); ++i)
	{
		clang::EnumConstantDecl* constant_decl = *i;

		// Strip out the raw 64-bit value - the compiler will automatically modify any values
		// greater than 64-bits without having to worry about that here
		llvm::APSInt value = constant_decl->getInitVal();
		__int64 value_int = value.getRawData()[0];

		// Clang doesn't construct the enum name as a C++ compiler would see it so do that first
		// NOTE: May want to revisit this later
		std::string constant_name = constant_decl->getNameAsString();
		if (parent_name != m_DB.GetNoName())
		{
			constant_name = parent_name->second + "::" + constant_name;
		}

		// Add to the database
		m_DB.AddPrimitive(crdb::EnumConstant(m_DB.GetName(constant_name.c_str()), name, value_int));
		printf("   %s = 0x%x\n", constant_name.c_str(), value_int);
	}
}


void ASTConsumer::AddFunctionDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// Parse and add the function
	std::vector<crdb::Field> parameters;
	MakeFunction(m_DB, decl, name, parent_name, parameters);
}


void ASTConsumer::AddMethodDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// Cast to a method
	clang::CXXMethodDecl* method_decl = dyn_cast<clang::CXXMethodDecl>(decl);
	assert(method_decl != 0 && "Failed to cast to C++ method declaration");

	std::vector<crdb::Field> parameters;
	if (method_decl->isInstance())
	{
		// Parse the 'this' type, treating it as the first parameter to the method
		crdb::Field this_param;
		if (!MakeField(m_DB, method_decl->getThisType(m_ASTContext), "this", name, 0, this_param))
		{
			printf("WARNING: Unsupported 'this' type for %s\n", name->second.c_str());
			return;
		}
		parameters.push_back(this_param);
	}

	// Parse and add the method
	MakeFunction(m_DB, decl, name, parent_name, parameters);
}


void ASTConsumer::AddFieldDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name)
{
	// Cast to a field
	clang::FieldDecl* field_decl = dyn_cast<clang::FieldDecl>(decl);
	assert(field_decl != 0 && "Failed to cast to field declaration");

	// Parse and add the field
	crdb::Field field;
	if (!MakeField(m_DB, field_decl->getType(), field_decl->getNameAsString().c_str(), parent_name, 0, field))
	{
		printf("WARNING: Unsupported type for field %s\n", field_decl->getNameAsString().c_str());
		return;
	}

	printf("   Field: %s%s%s %s\n",
		field.is_const ? "const " : "",
		field.type->second.c_str(),
		field.modifier == crdb::Field::MODIFIER_POINTER ? "*" : field.modifier == crdb::Field::MODIFIER_REFERENCE ? "&" : "",
		field.name->second.c_str());
	m_DB.AddPrimitive(field);
}


void ASTConsumer::AddContainedDecls(clang::NamedDecl* decl, const crdb::Name& parent_name)
{
	// Iterate over every contained named declaration
	clang::DeclContext* decl_context = decl->castToDeclContext(decl);
	for (clang::DeclContext::decl_iterator i = decl_context->decls_begin(); i != decl_context->decls_end(); ++i)
	{
		clang::NamedDecl* named_decl = dyn_cast<clang::NamedDecl>(*i);
		if (named_decl != 0)
		{
			AddDecl(named_decl, parent_name);
		}
	}
}
