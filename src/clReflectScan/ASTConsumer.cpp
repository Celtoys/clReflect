
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

// TODO: Parameter names no longer need to be unique
//
// Primitives that require full names:
//
//    * Functions: No. Nothing references functions in code that we are going to reflect.
//    * Namespaces: No. Nothing references namespaces.
//    * Enums: Yes. These are types and can be used as fields.
//    * Classes: Yes. They can be used as fields.
//    * Fields: No. Nothing references fields.
//

#include "ASTConsumer.h"
#include "ReflectionSpecs.h"
#include "AttributeParser.h"

#include <clcpp/Core.h>

#include <clReflectCore/Logging.h>

// clang\ast\decltemplate.h(1484) : warning C4345: behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized
#pragma warning(disable:4345)

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCxx.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/TemplateName.h>
#include <clang/Basic/SourceManager.h>


namespace
{
	void Remove(std::string& str, const std::string& remove_str)
	{
		for (size_t i; (i = str.find(remove_str)) != std::string::npos; )
			str.replace(i, remove_str.length(), "");
	}


	struct ParameterInfo
	{
		std::string type_name;
		cldb::Qualifier qualifer;
	};


	bool GetParameterInfo(cldb::Database& db, const ReflectionSpecs& specs, clang::ASTContext& ctx, clang::QualType qual_type, ParameterInfo& info)
	{
		// Get type info for the parameter
		clang::SplitQualType sqt = qual_type.split();
		const clang::Type* type = sqt.first;

		// If this is a typedef, get the aliased type
		if (type->getTypeClass() == clang::Type::Typedef)
		{
			qual_type = qual_type.getCanonicalType();
			sqt = qual_type.split();
			type = sqt.first;
		}

		// Only handle one level of recursion for pointers and references

		// Get pointee type info if this is a pointer
		if (const clang::PointerType* ptr_type = dyn_cast<clang::PointerType>(type))
		{
			info.qualifer.op = cldb::Qualifier::POINTER;
			qual_type = ptr_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Get pointee type info if this is a reference
		else if (const clang::LValueReferenceType* ref_type = dyn_cast<clang::LValueReferenceType>(type))
		{
			info.qualifer.op = cldb::Qualifier::REFERENCE;
			qual_type = ref_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Record the qualifiers before stripping them and generating the type name
		clang::Qualifiers qualifiers = clang::Qualifiers::fromFastMask(sqt.second);
		qual_type.removeLocalFastQualifiers();
		info.type_name = qual_type.getAsString(ctx.getLangOptions());
		info.qualifer.is_const = qualifiers.hasConst();

		// Is this a field that can be safely recorded?
		type = sqt.first;
		clang::Type::TypeClass tc = type->getTypeClass();
		switch (tc)
		{
		case (clang::Type::Builtin):
		case (clang::Type::Enum):
		case (clang::Type::Elaborated):
		case (clang::Type::Record):
		case (clang::Type::TemplateSpecialization):
			break;
		default:
			return false;
		}

		// Is this a template specialisation?
		const clang::CXXRecordDecl* type_decl = type->getAsCXXRecordDecl();
		if (type_decl && type_decl->getTemplateSpecializationKind() != clang::TSK_Undeclared)
		{
			const clang::ClassTemplateSpecializationDecl* cts_decl = dyn_cast<clang::ClassTemplateSpecializationDecl>(type_decl);
			assert(cts_decl && "Couldn't cast to template specialisation decl");

			// Get the template being specialised and see if it's marked for reflection
			// The template definition needs to be in scope for specialisations to occur. This implies
			// that the reflection spec must also be in scope.
			const clang::ClassTemplateDecl* template_decl = cts_decl->getSpecializedTemplate();
			info.type_name = template_decl->getQualifiedNameAsString();
			if (!specs.IsReflected(info.type_name))
				return false;

			// Parent the instance to its declaring template
			cldb::Name parent_name = db.GetName(info.type_name.c_str());

			// Prepare for adding template arguments to the type name
			info.type_name += "<";

			// Get access to the template argument list
			ParameterInfo template_args[cldb::TemplateType::MAX_NB_ARGS];
			const clang::TemplateArgumentList& list = cts_decl->getTemplateArgs();
			if (list.size() >= cldb::TemplateType::MAX_NB_ARGS)
				return false;

			for (unsigned int i = 0; i < list.size(); i++)
			{
				// Only support type arguments
				const clang::TemplateArgument& arg = list[i];
				if (arg.getKind() != clang::TemplateArgument::Type)
					return false;

				// Recursively parse the template argument to get some parameter info
				if (!GetParameterInfo(db, specs, ctx, arg.getAsType(), template_args[i]))
					return false;

				// References currently not supported
				if (template_args[i].qualifer.op == cldb::Qualifier::REFERENCE)
					return false;

				// Concatenate the arguments in the type name
				if (i)
					info.type_name += ",";
				info.type_name += template_args[i].type_name;
				if (template_args[i].qualifer.op == cldb::Qualifier::POINTER)
					info.type_name += "*";
			}

			info.type_name += ">";

			// Create the referenced template type on demand if it doesn't exist
			if (db.GetFirstPrimitive<cldb::TemplateType>(info.type_name.c_str()) == 0)
			{
				cldb::Name type_name = db.GetName(info.type_name.c_str());
				cldb::TemplateType type(type_name, parent_name);

				// Populate the template argument list
				for (unsigned int i = 0; i < list.size(); i++)
				{
					type.parameter_types[i] = db.GetName(template_args[i].type_name.c_str());
					type.parameter_ptrs[i] = template_args[i].qualifer.op == cldb::Qualifier::POINTER;
				}

				db.AddPrimitive(type);
			}
		}

		// Pull the class descriptions from the type name
		Remove(info.type_name, "enum ");
		Remove(info.type_name, "struct ");
		Remove(info.type_name, "class ");

		// Has the type itself been marked for reflection?
		// TODO: Remove this and offload to clexport - can't know at this point if a type is reflected
		// as it might be forward-declared!
		// If this is a field then the definition needs to be in scope if it's not a pointer or reference.
		// This implies that the reflection spec for the field type should also be in scope so this check is valid
		// in this case.
		if (tc != clang::Type::Builtin && !specs.IsReflected(info.type_name))
		{
			return false;
		}

		return true;
	}


	bool MakeField(cldb::Database& db, const ReflectionSpecs& specs, clang::ASTContext& ctx, clang::QualType qual_type, const char* param_name, cldb::Name parent_name, int index, cldb::Field& field)
	{
		ParameterInfo info;
		if (!GetParameterInfo(db, specs, ctx, qual_type, info))
		{
			return false;
		}

		// Construct the field
		cldb::Name type_name = db.GetName(info.type_name.c_str());
		field = cldb::Field(db.GetName(param_name), parent_name, type_name, info.qualifer, index);
		return true;
	}


	void MakeFunction(cldb::Database& db, const ReflectionSpecs& specs, clang::ASTContext& ctx, clang::NamedDecl* decl, cldb::Name function_name, cldb::Name parent_name, std::vector<cldb::Field>& parameters)
	{
		// Cast to a function
		clang::FunctionDecl* function_decl = dyn_cast<clang::FunctionDecl>(decl);
		assert(function_decl != 0 && "Failed to cast to function declaration");

		// Only add the function once
		if (!function_decl->isFirstDeclaration())
		{
			return;
		}

		// Parse the return type - named as a reserved keyword so it won't clash with user symbols
		cldb::Field return_parameter;
		if (!MakeField(db, specs, ctx, function_decl->getResultType(), "return", function_name, -1, return_parameter))
		{
			LOG(ast, WARNING, "Unsupported/unreflected return type for '%s' - skipping reflection\n", function_name.text.c_str());
			return;
		}

		// Try to gather every parameter successfully before adding the function
		int index = parameters.size();
		for (clang::FunctionDecl::param_iterator i = function_decl->param_begin(); i != function_decl->param_end(); ++i)
		{
			clang::ParmVarDecl* param_decl = *i;

			// Check for unnamed parameters
			if (param_decl->getNameAsString() == "")
			{
				LOG(ast, WARNING, "Unnamed function parameters not supported - skipping reflection of '%s'\n", function_name.text.c_str());
				return;
			}

			// Collect a list of constructed parameters in case evaluating one of them fails
			cldb::Field parameter;
			if (!MakeField(db, specs, ctx, param_decl->getType(), param_decl->getNameAsString().c_str(), function_name, index++, parameter))
			{
				LOG(ast, WARNING, "Unsupported/unreflected parameter type for '%s' - skipping reflection of '%s'\n", param_decl->getNameAsString().c_str(), function_name.text.c_str());
				return;
			}
			parameters.push_back(parameter);
		}

		// Generate a hash unique to this function among other functions of the same name
		// This is so that its parameters/return code can re-parent themselves correctly
		cldb::Field* return_parameter_ptr = 0;
		if (return_parameter.type.text != "void")
		{
			return_parameter_ptr = &return_parameter;
		}
		cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(return_parameter_ptr, parameters);

		// Parent each parameter to the function
		return_parameter.parent_unique_id = unique_id;
		for (size_t i = 0; i < parameters.size(); i++)
		{
			parameters[i].parent_unique_id = unique_id;
		}

		// Add the function
		LOG(ast, INFO, "function %s\n", function_name.text.c_str());
		db.AddPrimitive(cldb::Function(function_name, parent_name, unique_id));

		LOG_PUSH_INDENT(ast);

		// Only add the return parameter if it's non-void
		if (return_parameter.type.text != "void")
		{
			LOG(ast, INFO, "Returns: %s%s%s\n",
				return_parameter.qualifier.is_const ? "const " : "",
				return_parameter.type.text.c_str(),
				return_parameter.qualifier.op == cldb::Qualifier::POINTER ? "*" : return_parameter.qualifier.op == cldb::Qualifier::REFERENCE ? "&" : "");
			db.AddPrimitive(return_parameter);
		}
		else
		{
			LOG(ast, INFO, "Returns: void (not added)\n");
		}

		// Add the parameters
		for (std::vector<cldb::Field>::iterator i = parameters.begin(); i != parameters.end(); ++i)
		{
			LOG(ast, INFO, "%s%s%s %s\n",
				i->qualifier.is_const ? "const " : "",
				i->type.text.c_str(),
				i->qualifier.op == cldb::Qualifier::POINTER ? "*" : i->qualifier.op == cldb::Qualifier::REFERENCE ? "&" : "",
				i->name.text.c_str());
			db.AddPrimitive(*i);
		}

		LOG_POP_INDENT(ast);
	}


	template <typename TYPE>
	void ProcessAttribute(cldb::Database& db, TYPE* attribute, bool add)
	{
		if (add)
		{
			// Only add the attribute if its unique
			const cldb::PrimitiveStore<TYPE>& store = db.GetPrimitiveStore<TYPE>();
			cldb::PrimitiveStore<TYPE>::const_iterator i = store.find(attribute->name.hash);
			if (i == store.end() || !i->second.Equals(*attribute))
			{
				LOG(ast, INFO, "attribute %s\n", attribute->name.text.c_str());
				db.AddPrimitive(*attribute);
			}
		}

		// Delete as the specified type to catch the correct destructor
		delete attribute;
	}


	bool ParseAttributes(cldb::Database& db, clang::SourceManager& srcmgr, clang::NamedDecl* decl, cldb::Name parent)
	{
		// Reflection attributes are stored as clang annotation attributes
		clang::specific_attr_iterator<clang::AnnotateAttr> i = decl->specific_attr_begin<clang::AnnotateAttr>();
		if (i == decl->specific_attr_end<clang::AnnotateAttr>())
		{
			return true;
		}

		// Get the annotation text
		clang::AnnotateAttr* attribute = *i;
		llvm::StringRef attribute_text = attribute->getAnnotation();

		// Figure out what operations to apply to the attributes
		if (attribute_text.startswith("attr:"))
		{
			attribute_text = attribute_text.substr(sizeof("attr"));
		}

		// Decipher the source location of the attribute for error reporting
		clang::SourceLocation location = attribute->getLocation();
		clang::PresumedLoc presumed_loc = srcmgr.getPresumedLoc(location);
		const char* filename = presumed_loc.getFilename();
		int line = presumed_loc.getLine();

		// Parse all attributes in the text
		std::vector<cldb::Attribute*> attributes = ::ParseAttributes(db, attribute_text.str().c_str(), filename, line);

		// Determine if the attributes are being added or whether their memory is just being deleted
		// TODO: Need to evaluate whether this works on parent primitives, too.
		bool add = true;
		static unsigned int noreflect_hash = clcpp::internal::HashNameString("noreflect");
		if (attributes.size() && attributes[0]->name.hash == noreflect_hash)
			add = false;

		for (size_t i = 0; i < attributes.size(); i++)
		{
			cldb::Attribute* attribute = attributes[i];

			// 'noreflect' must be the first attribute
			if (i && attribute->name.hash == noreflect_hash)
				LOG(ast, WARNING, "'noreflect' attribute unexpected and ignored");

			// Add the attributes to the database, parented to the calling declaration
			attribute->parent = parent;
			switch (attribute->kind)
			{
			case (cldb::Primitive::KIND_FLAG_ATTRIBUTE):
				ProcessAttribute(db, (cldb::FlagAttribute*)attribute, add);
				break;
			case (cldb::Primitive::KIND_INT_ATTRIBUTE):
				ProcessAttribute(db, (cldb::IntAttribute*)attribute, add);
				break;
			case (cldb::Primitive::KIND_FLOAT_ATTRIBUTE):
				ProcessAttribute(db, (cldb::FloatAttribute*)attribute, add);
				break;
			case (cldb::Primitive::KIND_NAME_ATTRIBUTE):
				ProcessAttribute(db, (cldb::NameAttribute*)attribute, add);
				break;
			case (cldb::Primitive::KIND_TEXT_ATTRIBUTE):
				ProcessAttribute(db, (cldb::TextAttribute*)attribute, add);
				break;
			}
		}

		return add;
	}
}


ASTConsumer::ASTConsumer(clang::ASTContext& context, cldb::Database& db, const ReflectionSpecs& rspecs, const std::string& ast_log)
	: m_ASTContext(context)
	, m_DB(db)
	, m_ReflectionSpecs(rspecs)
{
	LOG_TO_STDOUT(ast, WARNING);
	LOG_TO_STDOUT(ast, ERROR);

	if (ast_log != "")
	{
		LOG_TO_FILE(ast, ALL, ast_log.c_str());
	}
}


void ASTConsumer::WalkTranlationUnit(clang::TranslationUnitDecl* tu_decl)
{
	// Root namespace
	cldb::Name parent_name;

	// Iterate over every named declaration
	for (clang::DeclContext::decl_iterator i = tu_decl->decls_begin(); i != tu_decl->decls_end(); ++i)
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
			AddDecl(named_decl, parent_name, 0);
		}
	}
}


void ASTConsumer::AddDecl(clang::NamedDecl* decl, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout)
{
	// Skip decls with errors and those marked by the Reflection Spec pass to ignore
	if (decl->isInvalidDecl())
	{
		return;
	}

	// Has this decl been marked for reflection?
	std::string name_str = decl->getQualifiedNameAsString();
	if (!m_ReflectionSpecs.IsReflected(name_str.c_str()))
	{
		return;
	}

	// Generate a name for the decl
	cldb::Name name = m_DB.GetName(name_str.c_str());

	// Gather all attributes associated with this primitive
	if (!ParseAttributes(m_DB, m_ASTContext.getSourceManager(), decl, name))
		return;

	clang::Decl::Kind kind = decl->getKind();
	switch (kind)
	{
	case (clang::Decl::Namespace): AddNamespaceDecl(decl, name, parent_name); break;
	case (clang::Decl::CXXRecord): AddClassDecl(decl, name, parent_name); break;
	case (clang::Decl::Enum): AddEnumDecl(decl, name, parent_name); break;
	case (clang::Decl::Function): AddFunctionDecl(decl, name, parent_name); break;
	case (clang::Decl::CXXMethod): AddMethodDecl(decl, name, parent_name); break;
	case (clang::Decl::Field): AddFieldDecl(decl, name, parent_name, layout); break;
	case (clang::Decl::ClassTemplate): AddClassTemplateDecl(decl, name, parent_name); break;
	}
}


void ASTConsumer::AddNamespaceDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
{
	// Only add the namespace if it doesn't exist yet
	if (m_DB.GetFirstPrimitive<cldb::Namespace>(name.text.c_str()) == 0)
	{
		m_DB.AddPrimitive(cldb::Namespace(name, parent_name));
		LOG(ast, INFO, "namespace %s\n", name.text.c_str());
	}

	// Add everything within the namespace
	AddContainedDecls(decl, name, 0);
}


void ASTConsumer::AddClassDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
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
		LOG(ast, WARNING, "Class '%s' has too many bases\n", name.text.c_str());
		return;
	}

	// Parse any base classes
	cldb::Name base_name;
	if (record_decl->getNumBases() > 0)
	{
		// Can't support virtual base classes - offsets change at runtime
		clang::CXXBaseSpecifier& base = *record_decl->bases_begin();
		if (base.isVirtual())
		{
			LOG(ast, WARNING, "Class '%s' has an unsupported virtual base class\n", name.text.c_str());
			return;
		}

		// Parse the type name
		clang::QualType base_type = base.getType();
		std::string type_name_str = base_type.getAsString(m_ASTContext.getLangOptions());
		Remove(type_name_str, "struct ");
		Remove(type_name_str, "class ");

		// Check it's valid
		if (!m_ReflectionSpecs.IsReflected(type_name_str))
		{
			LOG(ast, WARNING, "Base class '%s' of '%s' is not reflected so skipping\n", type_name_str.c_str(), name.text.c_str());
			return;
		}
		base_name = m_DB.GetName(type_name_str.c_str());
	}

	// Add to the database
	LOG(ast, INFO, "class %s", name.text.c_str());
	if (base_name != cldb::Name())
	{
		LOG(ast, INFO, " : %s", base_name.text.c_str());
	}
	LOG_NEWLINE(ast);
	const clang::ASTRecordLayout& layout = m_ASTContext.getASTRecordLayout(record_decl);
	cldb::u32 size = layout.getSize().getQuantity();
	m_DB.AddPrimitive(cldb::Class(name, parent_name, base_name, size));
	AddContainedDecls(decl, name, &layout);
}


void ASTConsumer::AddEnumDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
{
	// Note that by unnamed enums are not explicitly discarded here. This is because they don't generally
	// get this far because you can't can't reference them in reflection specs.

	// Cast to an enum
	clang::EnumDecl* enum_decl = dyn_cast<clang::EnumDecl>(decl);
	assert(enum_decl != 0 && "Failed to cast to enum declaration");

	// Add to the database
	LOG(ast, INFO, "enum %s\n", name.text.c_str());
	m_DB.AddPrimitive(cldb::Enum(name, parent_name));

	LOG_PUSH_INDENT(ast);

	// Iterate over all constants
	for (clang::EnumDecl::enumerator_iterator i = enum_decl->enumerator_begin(); i != enum_decl->enumerator_end(); ++i)
	{
		clang::EnumConstantDecl* constant_decl = *i;

		// Strip out the raw 64-bit value - the compiler will automatically modify any values
		// greater than 64-bits without having to worry about that here
		llvm::APSInt value = constant_decl->getInitVal();
		int value_int = (int)value.getRawData()[0];

		// Clang doesn't construct the enum name as a C++ compiler would see it so do that first
		// NOTE: May want to revisit this later
		std::string constant_name = constant_decl->getNameAsString();
		if (parent_name != cldb::Name())
		{
			constant_name = parent_name.text + "::" + constant_name;
		}

		// Add to the database
		m_DB.AddPrimitive(cldb::EnumConstant(m_DB.GetName(constant_name.c_str()), name, value_int));
		LOG(ast, INFO, "   %s = 0x%x\n", constant_name.c_str(), value_int);
	}

	LOG_POP_INDENT(ast);
}


void ASTConsumer::AddFunctionDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
{
	// Parse and add the function
	std::vector<cldb::Field> parameters;
	MakeFunction(m_DB, m_ReflectionSpecs, m_ASTContext, decl, name, parent_name, parameters);
}


void ASTConsumer::AddMethodDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
{
	// Cast to a method
	clang::CXXMethodDecl* method_decl = dyn_cast<clang::CXXMethodDecl>(decl);
	assert(method_decl != 0 && "Failed to cast to C++ method declaration");

	// Ignore overloaded operators for now
	if (method_decl->isOverloadedOperator())
		return;

	std::vector<cldb::Field> parameters;
	if (method_decl->isInstance())
	{
		// Parse the 'this' type, treating it as the first parameter to the method
		cldb::Field this_param;
		if (!MakeField(m_DB, m_ReflectionSpecs, m_ASTContext, method_decl->getThisType(m_ASTContext), "this", name, 0, this_param))
		{
			LOG(ast, WARNING, "Unsupported/unreflected 'this' type for '%s'\n", name.text.c_str());
			return;
		}
		parameters.push_back(this_param);
	}

	// Parse and add the method
	MakeFunction(m_DB, m_ReflectionSpecs, m_ASTContext, decl, name, parent_name, parameters);
}


void ASTConsumer::AddFieldDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout)
{
	// Cast to a field
	clang::FieldDecl* field_decl = dyn_cast<clang::FieldDecl>(decl);
	assert(field_decl != 0 && "Failed to cast to field declaration");

	// Parse and add the field
	cldb::Field field;
	cldb::u32 offset = layout->getFieldOffset(field_decl->getFieldIndex()) / 8;
	std::string field_name = field_decl->getQualifiedNameAsString();
	if (!MakeField(m_DB, m_ReflectionSpecs, m_ASTContext, field_decl->getType(), field_name.c_str(), parent_name, offset, field))
	{
		LOG(ast, WARNING, "Unsupported/unreflected type for field '%s' in '%s'\n", field_name.c_str(), parent_name.text.c_str());
		return;
	}

	LOG(ast, INFO, "Field: %s%s%s %s\n",
		field.qualifier.is_const ? "const " : "",
		field.type.text.c_str(),
		field.qualifier.op == cldb::Qualifier::POINTER ? "*" : field.qualifier.op == cldb::Qualifier::REFERENCE ? "&" : "",
		field.name.text.c_str());
	m_DB.AddPrimitive(field);
}


void ASTConsumer::AddClassTemplateDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name)
{
	// Cast to class template decl
	clang::ClassTemplateDecl* template_decl = dyn_cast<clang::ClassTemplateDecl>(decl);
	assert(template_decl != 0 && "Failed to cast template declaration");

	// Only add the template if it doesn't exist yet
	if (m_DB.GetFirstPrimitive<cldb::Template>(name.text.c_str()) == 0)
	{
		// First check that the argument count is valid
		const clang::TemplateParameterList* parameters = template_decl->getTemplateParameters();
		if (parameters->size() > cldb::TemplateType::MAX_NB_ARGS)
		{
			LOG(ast, WARNING, "Too many template arguments for '%s'\n", name.text.c_str());
			return;
		}

		// Then verify that each argument is of the correct type
		for (clang::TemplateParameterList::const_iterator i = parameters->begin(); i != parameters->end(); ++i)
		{
			if (dyn_cast<clang::TemplateTypeParmDecl>(*i) == 0)
			{
				LOG(ast, WARNING, "Unsupported template argument type for '%s'\n", name.text.c_str());
				return;
			}
		}

		m_DB.AddPrimitive(cldb::Template(name, parent_name));
		LOG(ast, INFO, "template %s\n", name.text.c_str());
	}
}


void ASTConsumer::AddContainedDecls(clang::NamedDecl* decl, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout)
{
	LOG_PUSH_INDENT(ast)

	// Iterate over every contained named declaration
	clang::DeclContext* decl_context = decl->castToDeclContext(decl);
	for (clang::DeclContext::decl_iterator i = decl_context->decls_begin(); i != decl_context->decls_end(); ++i)
	{
		clang::NamedDecl* named_decl = dyn_cast<clang::NamedDecl>(*i);
		if (named_decl != 0)
		{
			AddDecl(named_decl, parent_name, layout);
		}
	}

	LOG_POP_INDENT(ast)
}
