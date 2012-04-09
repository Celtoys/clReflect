
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

#include "ASTConsumer.h"
#include "ReflectionSpecs.h"
#include "AttributeParser.h"

#include <clcpp/Core.h>

#include <clReflectCore/Logging.h>
#include <clReflectCore/FileUtils.h>

// clang\ast\decltemplate.h(1484) : warning C4345: behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized
#pragma warning(disable:4345)

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCxx.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/TemplateName.h>
#include <clang/Basic/SourceManager.h>

#include <stdarg.h>


namespace
{
	// Untyped flags for the MakeField function, as opposed to a few bools
	enum
	{
		MF_CHECK_TYPE_IS_REFLECTED = 1,
	};


	// Helper for constructing printf-formatted strings immediately and passing them between functions
	struct va
	{
		va(const char* format, ...)
		{
			char buffer[512];
			va_list args;
			va_start(args, format);
#if defined (CLCPP_USING_MSVC)
			vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
			vsnprintf(buffer, sizeof(buffer), format, args);
#endif	// CLCPP_USING_MSVC
			va_end(args);
			text = buffer;
		}

		operator const std::string& () const
		{
			return text;
		}

		std::string text;
	};


	struct Status
	{
		static Status Warn(const std::string& message)
		{
			Status status;
			status.messages = message;
			return status;
		}

		static Status JoinWarn(const Status& older, const std::string& message)
		{
			if (older.IsSilentFail())
				return older;

			// Add the message before concatenating with the older ones
			Status status;
			status.messages = message + "; " + older.messages;
			return status;
		}

		static Status SilentFail()
		{
			Status status;
			status.messages = "SILENT FAIL";
			return status;
		}

		bool HasWarnings() const
		{
			return messages != "";
		}

		bool IsSilentFail() const
		{
			return messages == "SILENT FAIL";
		}

		void Print(clang::SourceLocation location, const clang::SourceManager& srcmgr, const std::string& message)
		{
			// Don't do anything on a silent fail
			if (IsSilentFail())
				return;

			// Get text for source location
			clang::PresumedLoc presumed_loc = srcmgr.getPresumedLoc(location);
			const char* filename = presumed_loc.getFilename();
			int line = presumed_loc.getLine();

			// Print immediate warning
			LOG(warnings, INFO, "%s(%d) : warning - %s; %s\n", filename, line, message.c_str(), messages.c_str());
		}

		std::string messages;
	};


	void Remove(std::string& str, const std::string& remove_str)
	{
		for (size_t i; (i = str.find(remove_str)) != std::string::npos; )
			str.replace(i, remove_str.length(), "");
	}


	struct ParameterInfo
	{
		ParameterInfo() : array_count(0) { }
		std::string type_name;
		cldb::Qualifier qualifer;
		cldb::u32 array_count;
	};


	Status GetParameterInfo(ASTConsumer& consumer, clang::QualType qual_type, ParameterInfo& info, int flags);
	Status ParseTemplateSpecialisation(ASTConsumer& consumer, const clang::Type* type, std::string& type_name_str);


	Status ParseBaseClass(ASTConsumer& consumer, cldb::Name derived_type_name, const clang::CXXBaseSpecifier& base, cldb::Name& base_name)
	{
		// Parse the type name
		base_name = cldb::Name();
		clang::QualType base_qual_type = base.getType();
		const clang::Type* base_type = base_qual_type.split().first;
		std::string type_name_str = base_qual_type.getAsString(consumer.GetASTContext().getLangOptions());
		Remove(type_name_str, "struct ");
		Remove(type_name_str, "class ");

		// Can't support virtual base classes - offsets change at runtime
		if (base.isVirtual())
			return Status::Warn(va("Class '%s' is an unsupported virtual base class", type_name_str.c_str()));

		// Discover any new template types
		Status status = ParseTemplateSpecialisation(consumer, base_type, type_name_str);
		if (status.HasWarnings())
			return status;

		cldb::Database& db = consumer.GetDB();
		base_name = db.GetName(type_name_str.c_str());
		db.AddTypeInheritance(derived_type_name, base_name);
		return Status();
	}


	Status ParseTemplateSpecialisation(ASTConsumer& consumer, const clang::ClassTemplateSpecializationDecl* cts_decl, std::string& type_name_str)
	{
		// Get the template being specialised and see if it's marked for reflection
		// The template definition needs to be in scope for specialisations to occur. This implies
		// that the reflection spec must also be in scope.
		const clang::ClassTemplateDecl* template_decl = cts_decl->getSpecializedTemplate();
		type_name_str = template_decl->getQualifiedNameAsString(consumer.GetPrintingPolicy());

		// Parent the instance to its declaring template
		cldb::Database& db = consumer.GetDB();
		cldb::Name parent_name = db.GetName(type_name_str.c_str());

		// Prepare for adding template arguments to the type name
		type_name_str += "<";

		// Get access to the template argument list
		ParameterInfo template_args[cldb::TemplateType::MAX_NB_ARGS];
		const clang::TemplateArgumentList& list = cts_decl->getTemplateArgs();
		if (list.size() >= cldb::TemplateType::MAX_NB_ARGS)
			return Status::Warn(va("Only %d template arguments are supported; template has %d", cldb::TemplateType::MAX_NB_ARGS, list.size()));

		for (unsigned int i = 0; i < list.size(); i++)
		{
			// Only support type arguments
			const clang::TemplateArgument& arg = list[i];
			if (arg.getKind() != clang::TemplateArgument::Type)
				return Status::Warn(va("Unsupported non-type template parameter %d", i + 1));

			// Recursively parse the template argument to get some parameter info
			Status status = GetParameterInfo(consumer, arg.getAsType(), template_args[i], false);
			if (status.HasWarnings())
				return Status::JoinWarn(status, va("Unsupported template parameter type %d", i + 1));

			// References currently not supported
			if (template_args[i].qualifer.op == cldb::Qualifier::REFERENCE)
				return Status::Warn(va("Unsupported reference type as template parameter %d", i + 1));

			// Can't reflect array template parameters
			if (template_args[i].array_count)
				return Status::Warn(va("Unsupported array template parameter %d", i + 1));

			// Concatenate the arguments in the type name
			if (i)
				type_name_str += ",";
			type_name_str += template_args[i].type_name;
			if (template_args[i].qualifer.op == cldb::Qualifier::POINTER)
				type_name_str += "*";
		}

		type_name_str += ">";

		// Create the referenced template type on demand if it doesn't exist
		if (db.GetFirstPrimitive<cldb::TemplateType>(type_name_str.c_str()) == 0)
		{
			cldb::Name type_name = db.GetName(type_name_str.c_str());

			// Try to parse the base classes
			std::vector<cldb::Name> base_names;
			for (clang::CXXRecordDecl::base_class_const_iterator base_it = cts_decl->bases_begin(); base_it != cts_decl->bases_end(); base_it++)
			{
				cldb::Name base_name;
				Status status = ParseBaseClass(consumer, type_name, *base_it, base_name);
				if (status.HasWarnings())
					return Status::JoinWarn(status, "Failure to create template type due to invalid base class");
				base_names.push_back(base_name);
			}

			cldb::TemplateType type(type_name, parent_name);

			// Populate the template argument list
			for (unsigned int i = 0; i < list.size(); i++)
			{
				type.parameter_types[i] = db.GetName(template_args[i].type_name.c_str());
				type.parameter_ptrs[i] = template_args[i].qualifer.op == cldb::Qualifier::POINTER;
			}

			// Log the creation of this new instance
			LOG(ast, INFO, "class %s", type_name_str.c_str());
			for (size_t i = 0; i < base_names.size(); i++)
				LOG_APPEND(ast, INFO, (i == 0) ? " : %s" : ", %s", base_names[i].text.c_str());
			LOG_NEWLINE(ast);

			db.AddPrimitive(type);
		}

		return Status();
	}


	Status ParseTemplateSpecialisation(ASTConsumer& consumer, const clang::Type* type, std::string& type_name_str)
	{
		if (const clang::CXXRecordDecl* type_decl = type->getAsCXXRecordDecl())
		{
			// Don't attempt to parse declarations that contain this as it will be fully defined after
			// a merge operation. clang will try its best not to instantiate a template when it doesn't have too.
			// In such situations, parsing the specialisation is not possible.
			clang::Type::TypeClass tc = type->getTypeClass();
			if (tc == clang::Type::TemplateSpecialization && type_decl->getTemplateSpecializationKind() == clang::TSK_Undeclared)
				return Status::SilentFail();

			if (type_decl->getTemplateSpecializationKind() != clang::TSK_Undeclared)
			{
				const clang::ClassTemplateSpecializationDecl* cts_decl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(type_decl);
				assert(cts_decl && "Couldn't cast to template specialisation decl");

				// Parse template specialisation parameters
				Status status = ParseTemplateSpecialisation(consumer, cts_decl, type_name_str);
				if (status.HasWarnings())
					return Status::JoinWarn(status, va("Couldn't parse template specialisation parameter '%s'", type_name_str.c_str()));
			}
		}

		return Status();
	}


	Status GetParameterInfo(ASTConsumer& consumer, clang::QualType qual_type, ParameterInfo& info, int flags)
	{
		// Get type info for the parameter
		clang::SplitQualType sqt = qual_type.split();
		const clang::Type* type = sqt.first;

		// If this is an array of constant size, strip the size from the type and store it in the parameter info
		if (const clang::ConstantArrayType* array_type = llvm::dyn_cast<clang::ConstantArrayType>(type))
		{
			uint64_t size = *array_type->getSize().getRawData();
			if (size > UINT_MAX)
				return Status::Warn(va("Array size too big (%d)", size));
			info.array_count = (cldb::u32)size;
			qual_type = array_type->getElementType();
			sqt = qual_type.split();
			type = sqt.first;
		}

		// If this is a typedef, get the aliased type
		if (type->getTypeClass() == clang::Type::Typedef)
		{
			qual_type = qual_type.getCanonicalType();
			sqt = qual_type.split();
			type = sqt.first;
		}

		// Only handle one level of recursion for pointers and references

		// Get pointee type info if this is a pointer
		if (const clang::PointerType* ptr_type = llvm::dyn_cast<clang::PointerType>(type))
		{
			info.qualifer.op = cldb::Qualifier::POINTER;
			qual_type = ptr_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Get pointee type info if this is a reference
		else if (const clang::LValueReferenceType* ref_type = llvm::dyn_cast<clang::LValueReferenceType>(type))
		{
			info.qualifer.op = cldb::Qualifier::REFERENCE;
			qual_type = ref_type->getPointeeType();
			sqt = qual_type.split();
		}

		// Record the qualifiers before stripping them and generating the type name
		clang::Qualifiers qualifiers = clang::Qualifiers::fromFastMask(sqt.second);
		qual_type.removeLocalFastQualifiers();
		info.type_name = qual_type.getAsString(consumer.GetASTContext().getLangOptions());
		info.qualifer.is_const = qualifiers.hasConst();

		// Is this a field that can be safely recorded?
		type = sqt.first;
		clang::Type::TypeClass tc = type->getTypeClass();
		switch (tc)
		{
		case clang::Type::TemplateSpecialization:
		case clang::Type::Builtin:
		case clang::Type::Enum:
		case clang::Type::Elaborated:
		case clang::Type::Record:
			break;
		default:
			return Status::Warn("Type class is unknown");
		}

		// Discover any new template types
		Status status = ParseTemplateSpecialisation(consumer, type, info.type_name);
		if (status.HasWarnings())
			return status;

		// Pull the class descriptions from the type name
		Remove(info.type_name, "enum ");
		Remove(info.type_name, "struct ");
		Remove(info.type_name, "class ");

		return Status();
	}




	Status MakeField(ASTConsumer& consumer, clang::QualType qual_type, const char* param_name, const std::string& parent_name, int index, cldb::Field& field, int flags)
	{
		ParameterInfo info;
		Status status = GetParameterInfo(consumer, qual_type, info, flags);
		if (status.HasWarnings())
			return Status::JoinWarn(status, va("Failure to make field '%s'", param_name));

		// Construct the field
		cldb::Database& db = consumer.GetDB();
		cldb::Name type_name = db.GetName(info.type_name.c_str());
		field = cldb::Field(
			db.GetName(param_name),
			db.GetName(parent_name.c_str()),
			type_name, info.qualifer, index);

		// Add a container info for this field if it's a constant array
		if (info.array_count)
		{
			std::string full_name = parent_name + "::" + param_name;
			cldb::ContainerInfo ci;
			ci.name = db.GetName(full_name.c_str());
			ci.flags = cldb::ContainerInfo::IS_C_ARRAY;
			ci.count = info.array_count;
			db.AddPrimitive(ci);
		}

		return Status();
	}


	template <typename TYPE>
	void AddAttribute(cldb::Database& db, TYPE* attribute)
	{
		// Only add the attribute if its unique
		const cldb::DBMap<TYPE>& store = db.GetDBMap<TYPE>();
		typename cldb::DBMap<TYPE>::const_iterator i = store.find(attribute->name.hash);
		if (i == store.end() || !i->second.Equals(*attribute))
		{
			LOG(ast, INFO, "attribute %s\n", attribute->name.text.c_str());
			db.AddPrimitive(*attribute);
		}
	}


	enum ParseAttributesResult
	{
		PAR_Normal,
		PAR_Reflect,
		PAR_ReflectPartial,
		PAR_NoReflect,
	};


	ParseAttributesResult ParseAttributes(ASTConsumer& consumer, clang::NamedDecl* decl, const std::string& parent, bool allow_reflect)
	{
		ParseAttributesResult result = PAR_Normal;

		cldb::Database& db = consumer.GetDB();
		const clang::SourceManager& srcmgr = consumer.GetASTContext().getSourceManager();

		// See what the reflection specs have to say (namespaces can't have attributes)
		const ReflectionSpecs& specs = consumer.GetReflectionSpecs();
		ReflectionSpecType spec_type = specs.Get(parent);
		switch (spec_type)
		{
		case (RST_Full): result = PAR_Reflect; break;
		case (RST_Partial): result = PAR_ReflectPartial; break;
		}

		// Reflection attributes are stored as clang annotation attributes
		clang::specific_attr_iterator<clang::AnnotateAttr> i = decl->specific_attr_begin<clang::AnnotateAttr>();
		if (i == decl->specific_attr_end<clang::AnnotateAttr>())
			return result;

		// Get the annotation text
		clang::AnnotateAttr* attribute = *i;
		llvm::StringRef attribute_text = attribute->getAnnotation();

		// Figure out what operations to apply to the attributes
		if (attribute_text.startswith("attr:"))
			attribute_text = attribute_text.substr(sizeof("attr"));

		// Decipher the source location of the attribute for error reporting
		clang::SourceLocation location = attribute->getLocation();
		clang::PresumedLoc presumed_loc = srcmgr.getPresumedLoc(location);
		const char* filename = presumed_loc.getFilename();
		int line = presumed_loc.getLine();

		// Parse all attributes in the text
		std::vector<cldb::Attribute*> attributes = ::ParseAttributes(db, attribute_text.str().c_str(), filename, line);

		// Look for a reflection spec as the first attribute
		size_t attr_search_start = 0;
		static unsigned int reflect_hash = clcpp::internal::HashNameString("reflect");
		static unsigned int reflect_part_hash = clcpp::internal::HashNameString("reflect_part");
		static unsigned int noreflect_hash = clcpp::internal::HashNameString("noreflect");
		if (attributes.size())
		{
			unsigned int name_hash = attributes[0]->name.hash;
			if (name_hash == reflect_hash)
				result = PAR_Reflect;
			else if (name_hash == reflect_part_hash)
				result = PAR_ReflectPartial;
			else if (name_hash == noreflect_hash)
				result = PAR_NoReflect;

			// Start adding attributes after any reflection specs
			// Their existence is implied by the presence of the primitives they describe
			if (result != PAR_Normal)
				attr_search_start = 1;
		}

		// Determine whether the attributes themselves need reflecting
		if (allow_reflect || result != PAR_NoReflect)
		{
			for (size_t i = attr_search_start; i < attributes.size(); i++)
			{
				cldb::Attribute* attribute = attributes[i];

				if (result != PAR_Normal)
				{
					// Check that no attribute after the initial one contains a reflection spec
					unsigned int name_hash = attributes[i]->name.hash;
					if (name_hash == reflect_hash || name_hash == reflect_part_hash || name_hash == noreflect_hash)
						Status().Print(location, srcmgr, va("'%s' attribute unexpected and ignored", attributes[i]->name.text.c_str()));
				}

				// Add the attributes to the database, parented to the calling declaration
				attribute->parent = db.GetName(parent.c_str());
				switch (attribute->kind)
				{
				case (cldb::Primitive::KIND_FLAG_ATTRIBUTE):
					AddAttribute(db, (cldb::FlagAttribute*)attribute);
					break;
				case (cldb::Primitive::KIND_INT_ATTRIBUTE):
					AddAttribute(db, (cldb::IntAttribute*)attribute);
					break;
				case (cldb::Primitive::KIND_FLOAT_ATTRIBUTE):
					AddAttribute(db, (cldb::FloatAttribute*)attribute);
					break;
				case (cldb::Primitive::KIND_PRIMITIVE_ATTRIBUTE):
					AddAttribute(db, (cldb::PrimitiveAttribute*)attribute);
					break;
				case (cldb::Primitive::KIND_TEXT_ATTRIBUTE):
					AddAttribute(db, (cldb::TextAttribute*)attribute);
					break;
				}
			}
		}

		// Delete the allocated attributes
		for (size_t i = 0; i < attributes.size(); i++)
			delete attributes[i];

		return result;
	}
}


ASTConsumer::ASTConsumer(clang::ASTContext& context, cldb::Database& db, const ReflectionSpecs& rspecs, const std::string& ast_log)
	: m_ASTContext(context)
	, m_DB(db)
	, m_ReflectionSpecs(rspecs)
	, m_PrintingPolicy(0)
	, m_AllowReflect(false)
{
	LOG_TO_STDOUT(warnings, INFO);

	if (ast_log != "")
		LOG_TO_FILE(ast, ALL, ast_log.c_str());

	m_PrintingPolicy = new clang::PrintingPolicy(m_ASTContext.getLangOptions());
}


ASTConsumer::~ASTConsumer()
{
	delete m_PrintingPolicy;
}


void ASTConsumer::WalkTranlationUnit(clang::TranslationUnitDecl* tu_decl)
{
	// Root namespace
	cldb::Name parent_name;

	// Iterate over every named declaration
	for (clang::DeclContext::decl_iterator i = tu_decl->decls_begin(); i != tu_decl->decls_end(); ++i)
	{
		clang::NamedDecl* named_decl = llvm::dyn_cast<clang::NamedDecl>(*i);
		if (named_decl == 0)
			continue;

		// Filter out unsupported decls at the global namespace level
		clang::Decl::Kind kind = named_decl->getKind();
		switch (kind)
		{
		case (clang::Decl::Namespace):
		case (clang::Decl::CXXRecord):
		case (clang::Decl::Function):
		case (clang::Decl::Enum):
			AddDecl(named_decl, "", 0);
		}
	}
}


void ASTConsumer::AddDecl(clang::NamedDecl* decl, const std::string& parent_name, const clang::ASTRecordLayout* layout)
{
	// Skip decls with errors and those marked by the Reflection Spec pass to ignore
	if (decl->isInvalidDecl())
		return;

	// Gather all attributes associated with this primitive
	std::string name = decl->getQualifiedNameAsString(*m_PrintingPolicy);
	ParseAttributesResult result = ParseAttributes(*this, decl, name, m_AllowReflect);

	// Return immediately if 'noreflect' is specified, ignoring all children
	if (result == PAR_NoReflect)
		return;

	// If 'reflect' is specified, backup the allow reflect state and set it to true for this
	// declaration and all of its children.
	int old_allow_reflect = -1;
	if (result == PAR_Reflect)
	{
		old_allow_reflect = m_AllowReflect;
		m_AllowReflect = true;
	}

	// Reflect only if the allow reflect state has been inherited or the 'reflect_part'
	// attribute is specified
	if (m_AllowReflect || result == PAR_ReflectPartial)
	{
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

	// Restore any previously changed allow reflect state
	if (old_allow_reflect != -1)
		m_AllowReflect = old_allow_reflect != 0;

	// if m_ReflectPrimitives was changed, restore it
}


void ASTConsumer::AddNamespaceDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Only add the namespace if it doesn't exist yet
	if (m_DB.GetFirstPrimitive<cldb::Namespace>(name.c_str()) == 0)
	{
		m_DB.AddPrimitive(cldb::Namespace(m_DB.GetName(name.c_str()), m_DB.GetName(parent_name.c_str())));
		LOG(ast, INFO, "namespace %s\n", name.c_str());
	}

	// Add everything within the namespace
	AddContainedDecls(decl, name, 0);
}

void ASTConsumer::AddClassDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Cast to a record (NOTE: CXXRecord is a temporary clang type and will change in future revisions)
	clang::CXXRecordDecl* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
	assert(record_decl != 0 && "Failed to cast to record declaration");

	// Ignore forward declarations
	if (record_decl->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly)
		return;


	if (record_decl->getNumVBases())
	{
		Status().Print(record_decl->getLocation(), m_ASTContext.getSourceManager(), va("Class '%s' has an unsupported virtual base class", name.c_str()));
		return;
	}

	cldb::Name type_name = m_DB.GetName(name.c_str());

	// Parse base classes
	std::vector<cldb::Name> base_names;
	if (record_decl->getNumBases())
	{
		for (clang::CXXRecordDecl::base_class_const_iterator base_it = record_decl->bases_begin(); base_it != record_decl->bases_end(); base_it++)
		{
			cldb::Name base_name;
			Status status = ParseBaseClass(*this, type_name, *base_it, base_name);
			if (status.HasWarnings())
			{
				status.Print(record_decl->getLocation(), m_ASTContext.getSourceManager(), va("Failed to reflect class '%s'", name.c_str()));
				return;
			}

			// If the base class is valid, then add the inheritance relationship
			m_DB.AddTypeInheritance(type_name, base_name);
			base_names.push_back(base_name);
		}
	}

	if (record_decl->isAnonymousStructOrUnion())
	{
		// Add declarations to the parent
		const clang::ASTRecordLayout& layout = m_ASTContext.getASTRecordLayout(record_decl);
		AddContainedDecls(decl, parent_name, &layout);
	}
	else
	{
		// Add to the database
		LOG(ast, INFO, "class %s", name.c_str());
		for (size_t i = 0; i < base_names.size(); i++)
			LOG_APPEND(ast, INFO, (i == 0) ? " : %s" : ", %s", base_names[i].text.c_str());
		LOG_NEWLINE(ast);
		const clang::ASTRecordLayout& layout = m_ASTContext.getASTRecordLayout(record_decl);
		cldb::u32 size = layout.getSize().getQuantity();
		m_DB.AddPrimitive(cldb::Class(
			m_DB.GetName(name.c_str()),
			m_DB.GetName(parent_name.c_str()),
			size));
		AddContainedDecls(decl, name, &layout);
	}
}


void ASTConsumer::AddEnumDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Note that by unnamed enums are not explicitly discarded here. This is because they don't generally
	// get this far because you can't can't reference them in reflection specs.

	// Cast to an enum
	clang::EnumDecl* enum_decl = llvm::dyn_cast<clang::EnumDecl>(decl);
	assert(enum_decl != 0 && "Failed to cast to enum declaration");

	// Add to the database
	LOG(ast, INFO, "enum %s\n", name.c_str());
	m_DB.AddPrimitive(cldb::Enum(m_DB.GetName(name.c_str()), m_DB.GetName(parent_name.c_str())));

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
		if (parent_name != "")
			constant_name = parent_name + "::" + constant_name;

		// Add to the database
		m_DB.AddPrimitive(cldb::EnumConstant(
			m_DB.GetName(constant_name.c_str()),
			m_DB.GetName(name.c_str()), value_int));
		LOG(ast, INFO, "   %s = 0x%x\n", constant_name.c_str(), value_int);
	}

	LOG_POP_INDENT(ast);
}


void ASTConsumer::AddFunctionDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Parse and add the function
	std::vector<cldb::Field> parameters;
	MakeFunction(decl, name, parent_name, parameters);
}


void ASTConsumer::AddMethodDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Cast to a method
	clang::CXXMethodDecl* method_decl = llvm::dyn_cast<clang::CXXMethodDecl>(decl);
	assert(method_decl != 0 && "Failed to cast to C++ method declaration");

	// Ignore overloaded operators for now
	if (method_decl->isOverloadedOperator())
		return;

	std::vector<cldb::Field> parameters;
	if (method_decl->isInstance())
	{
		// Parse the 'this' type, treating it as the first parameter to the method
		cldb::Field this_param;
		Status status = MakeField(*this, method_decl->getThisType(m_ASTContext), "this", name, 0, this_param, MF_CHECK_TYPE_IS_REFLECTED);
		if (status.HasWarnings())
		{
			status.Print(method_decl->getLocation(), m_ASTContext.getSourceManager(), va("Failed to reflect method '%s' due to invalid 'this' type", name.c_str()));
			return;
		}
		parameters.push_back(this_param);
	}

	// Parse and add the method
	MakeFunction(decl, name, parent_name, parameters);
}


void ASTConsumer::AddFieldDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name, const clang::ASTRecordLayout* layout)
{
	// Cast to a field
	clang::FieldDecl* field_decl = llvm::dyn_cast<clang::FieldDecl>(decl);
	assert(field_decl != 0 && "Failed to cast to field declaration");

	// These are implicitly generated by clang so skip them
	if (field_decl->isAnonymousStructOrUnion())
		return;

	// Parse and add the field
	cldb::Field field;
	cldb::u32 offset = layout->getFieldOffset(field_decl->getFieldIndex()) / 8;
	std::string field_name = field_decl->getName().str();
	Status status = MakeField(*this, field_decl->getType(), field_name.c_str(), parent_name, offset, field, MF_CHECK_TYPE_IS_REFLECTED);
	if (status.HasWarnings())
	{
		status.Print(field_decl->getLocation(), m_ASTContext.getSourceManager(), va("Failed to reflect field in '%s'", parent_name.c_str()));
		return;
	}

	LOG(ast, INFO, "Field: %s%s%s %s\n",
		field.qualifier.is_const ? "const " : "",
		field.type.text.c_str(),
		field.qualifier.op == cldb::Qualifier::POINTER ? "*" : field.qualifier.op == cldb::Qualifier::REFERENCE ? "&" : "",
		field.name.text.c_str());
	m_DB.AddPrimitive(field);
}


void ASTConsumer::AddClassTemplateDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name)
{
	// Cast to class template decl
	clang::ClassTemplateDecl* template_decl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl);
	assert(template_decl != 0 && "Failed to cast template declaration");

	// Only add the template if it doesn't exist yet
	if (m_DB.GetFirstPrimitive<cldb::Template>(name.c_str()) == 0)
	{
		// First check that the argument count is valid
		const clang::TemplateParameterList* parameters = template_decl->getTemplateParameters();
		if (parameters->size() > cldb::TemplateType::MAX_NB_ARGS)
		{
			Status().Print(template_decl->getLocation(), m_ASTContext.getSourceManager(), va("Too many template arguments for '%s'", name.c_str()));
			return;
		}

		// Then verify that each argument is of the correct type
		for (clang::TemplateParameterList::const_iterator i = parameters->begin(); i != parameters->end(); ++i)
		{
			if (llvm::dyn_cast<clang::TemplateTypeParmDecl>(*i) == 0)
			{
				Status().Print(template_decl->getLocation(), m_ASTContext.getSourceManager(), va("Unsupported template argument type for '%s'", name.c_str()));
				return;
			}
		}

		m_DB.AddPrimitive(cldb::Template(
			m_DB.GetName(name.c_str()),
			m_DB.GetName(parent_name.c_str())));
		LOG(ast, INFO, "template %s\n", name.c_str());
	}
}


void ASTConsumer::AddContainedDecls(clang::NamedDecl* decl, const std::string& parent_name, const clang::ASTRecordLayout* layout)
{
	LOG_PUSH_INDENT(ast)

	// Iterate over every contained named declaration
	clang::DeclContext* decl_context = decl->castToDeclContext(decl);
	for (clang::DeclContext::decl_iterator i = decl_context->decls_begin(); i != decl_context->decls_end(); ++i)
	{
		clang::NamedDecl* named_decl = llvm::dyn_cast<clang::NamedDecl>(*i);
		if (named_decl != 0)
			AddDecl(named_decl, parent_name, layout);
	}

	LOG_POP_INDENT(ast)
}


void ASTConsumer::MakeFunction(clang::NamedDecl* decl, const std::string& function_name, const std::string& parent_name, std::vector<cldb::Field>& parameters)
{
	// Cast to a function
	clang::FunctionDecl* function_decl = llvm::dyn_cast<clang::FunctionDecl>(decl);
	assert(function_decl != 0 && "Failed to cast to function declaration");

	// Only add the function once
	if (!function_decl->isFirstDeclaration())
		return;

	// Parse the return type - named as a reserved keyword so it won't clash with user symbols
	cldb::Field return_parameter;
	Status status = MakeField(*this, function_decl->getResultType(), "return", function_name, -1, return_parameter, 0);
	if (status.HasWarnings())
	{
		status.Print(function_decl->getLocation(), m_ASTContext.getSourceManager(), va("Failed to reflect function '%s' due to invalid return type", function_name.c_str()));
		return;
	}

	// Try to gather every parameter successfully before adding the function
	int index = parameters.size();
	for (clang::FunctionDecl::param_iterator i = function_decl->param_begin(); i != function_decl->param_end(); ++i)
	{
		clang::ParmVarDecl* param_decl = *i;

		// Check for unnamed parameters
		llvm::StringRef param_name = param_decl->getName();
		if (param_name.empty())
		{
			Status().Print(function_decl->getLocation(), m_ASTContext.getSourceManager(), va("Unnamed function parameters not supported - skipping reflection of '%s'", function_name.c_str()));
			return;
		}

		// Collect a list of constructed parameters in case evaluating one of them fails
		cldb::Field parameter;
		std::string param_name_str = param_name.str();
		status = MakeField(*this, param_decl->getType(), param_name_str.c_str(), function_name, index++, parameter, 0);
		if (status.HasWarnings())
		{
			status.Print(function_decl->getLocation(), m_ASTContext.getSourceManager(), va("Failed to reflection function '%s'", function_name.c_str()));
			return;
		}
		parameters.push_back(parameter);
	}

	// Generate a hash unique to this function among other functions of the same name
	// This is so that its parameters/return code can re-parent themselves correctly
	cldb::Field* return_parameter_ptr = 0;
	if (return_parameter.type.text != "void")
		return_parameter_ptr = &return_parameter;
	cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(return_parameter_ptr, parameters);

	// Parent each parameter to the function
	return_parameter.parent_unique_id = unique_id;
	for (size_t i = 0; i < parameters.size(); i++)
		parameters[i].parent_unique_id = unique_id;

	// Add the function
	LOG(ast, INFO, "function %s\n", function_name.c_str());
	m_DB.AddPrimitive(cldb::Function(
		m_DB.GetName(function_name.c_str()),
		m_DB.GetName(parent_name.c_str()),
		unique_id));

	LOG_PUSH_INDENT(ast);

	// Only add the return parameter if it's non-void
	if (return_parameter.type.text != "void")
	{
		LOG(ast, INFO, "Returns: %s%s%s\n",
			return_parameter.qualifier.is_const ? "const " : "",
			return_parameter.type.text.c_str(),
			return_parameter.qualifier.op == cldb::Qualifier::POINTER ? "*" : return_parameter.qualifier.op == cldb::Qualifier::REFERENCE ? "&" : "");
		m_DB.AddPrimitive(return_parameter);
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
		m_DB.AddPrimitive(*i);
	}

	LOG_POP_INDENT(ast);
}
