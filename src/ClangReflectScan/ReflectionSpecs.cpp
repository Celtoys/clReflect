
#include "ReflectionSpecs.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCxx.h"
#include "clang/AST/DeclGroup.h"


namespace
{
	clang::AnnotateAttr* GetReflectionSpec(clang::DeclContext::decl_iterator i)
	{
		// Ignore anything that's not a namespace
		clang::NamespaceDecl* ns_decl = dyn_cast<clang::NamespaceDecl>(*i);
		if (ns_decl == 0)
		{
			return 0;
		}

		// Looking for internal registration namespaces
		if (ns_decl->getNameAsString() != "crdb_internal")
		{
			return 0;
		}

		// Immediately prevent this namespace from being parsed by subsequent passes
		// NOTE: I tried calling removeDecl from within the translation unit but it was asserting
		// in some context-singleton code which I couldn't fully grep. This is an alternative.
		ns_decl->setInvalidDecl();

		// Get the first declaration
		clang::DeclContext::decl_iterator j = ns_decl->decls_begin();
		if (j == ns_decl->decls_end())
		{
			printf("WARNING: Ill-formed Reflection Spec; no body found\n");
			return 0;
		}

		// Cast to a C++ record
		clang::CXXRecordDecl* record_decl = dyn_cast<clang::CXXRecordDecl>(*j);
		if (record_decl == 0)
		{
			printf("WARNING: Ill-formed Reflection Spec; first declaration must be a reflection structure\n");
			return 0;
		}

		// Get the first attribute for the C++ record
		clang::specific_attr_iterator<clang::AnnotateAttr> k = record_decl->specific_attr_begin<clang::AnnotateAttr>();
		if (k == record_decl->specific_attr_end<clang::AnnotateAttr>())
		{
			printf("WARNING: Ill-formed Reflection Spec; no annotation attribute found on the reflection structure\n");
			return 0;
		}

		return *k;
	}
}


void ReflectionSpecs::Gather(clang::TranslationUnitDecl* tu_decl)
{
	// Iterate over every reflection spec in the translation unit
	clang::DeclContext::decl_iterator i = tu_decl->decls_begin();
	while (i != tu_decl->decls_end())
	{
		clang::AnnotateAttr* attribute = GetReflectionSpec(i);
		if (attribute == 0)
		{
			++i;
			continue;
		}

		// Decode the reflection spec type
		bool partial_reflect = false;
		llvm::StringRef reflect_spec = attribute->getAnnotation();
		if (reflect_spec.startswith("full-"))
		{
			partial_reflect = false;
		}
		else if (reflect_spec.startswith("part-"))
		{
			partial_reflect = true;
		}
		else
		{
			printf("WARNING: Ill-formed Reflection Spec; can't determine if it's full or partial reflection\n");
		}

		// Strip the partial descriptor and check for existence in the map
		reflect_spec = reflect_spec.substr(5);
		if (m_ReflectionSpecs.find(reflect_spec) != m_ReflectionSpecs.end())
		{
			printf("WARNING: Ignoring duplicate Reflection Spec '%s'\n", reflect_spec.str().c_str());
			++i;
			continue;
		}

		m_ReflectionSpecs[reflect_spec] = partial_reflect;
		printf("Reflection Spec: %s (%s)\n", reflect_spec.str().c_str(), partial_reflect ? "partial" : "full");

		++i;
	}

	for (llvm::StringMap<bool>::const_iterator i = m_ReflectionSpecs.begin(); i != m_ReflectionSpecs.end(); ++i)
	{
		llvm::StringRef spec = i->getKey();
		if (spec.find("::") == spec.npos)
		{
			continue;
		}

		// Loop up through the parent scopes looking for unmarked names
		while (true)
		{
			size_t sep_pos = spec.rfind("::");
			if (sep_pos == spec.npos)
			{
				break;
			}

			// Insert partial reflection requests to ensure their primitives are created
			// to contain the children
			spec = spec.substr(0, sep_pos);
			llvm::StringMap<bool>::const_iterator i = m_ReflectionSpecs.find(spec);
			if (i == m_ReflectionSpecs.end())
			{
				m_ReflectionSpecs[spec] = true;
			}
		}
	}
}


bool ReflectionSpecs::IsReflected(llvm::StringRef name) const
{
	// If the symbol itself has been marked for reflection, it's irrelevant whether it's for partial
	// or full reflection - just reflect it. It's the contents that vary on this.
	if (m_ReflectionSpecs.find(name) != m_ReflectionSpecs.end())
	{
		return true;
	}

	// Loop up through the parent scopes looking for the closest reflection spec
	while (true)
	{
		size_t sep_pos = name.rfind("::");
		if (sep_pos == name.npos)
		{
			break;
		}

		// If a parent has a reflection spec entry, only reflect if it's full reflection
		name = name.substr(0, sep_pos);
		llvm::StringMap<bool>::const_iterator i = m_ReflectionSpecs.find(name);
		if (i != m_ReflectionSpecs.end())
		{
			return i->getValue() == false;
		}
	}

	return false;
}