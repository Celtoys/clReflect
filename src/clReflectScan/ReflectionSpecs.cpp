
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

#include "ReflectionSpecs.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/Logging.h>

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCxx.h>
#include <clang/AST/DeclGroup.h>


namespace
{
	clang::AnnotateAttr* GetReflectionSpec(clang::DeclContext::decl_iterator i)
	{
		// Ignore anything that's not a namespace
		clang::NamespaceDecl* ns_decl = llvm::dyn_cast<clang::NamespaceDecl>(*i);
		if (ns_decl == 0)
			return 0;

		// Looking for internal registration namespaces
		if (ns_decl->getName() != "clcpp_internal")
			return 0;

		// Immediately prevent this namespace from being parsed by subsequent passes
		// NOTE: I tried calling removeDecl from within the translation unit but it was asserting
		// in some context-singleton code which I couldn't fully grep. This is an alternative.
		ns_decl->setInvalidDecl();

		// Get the first declaration
		clang::DeclContext::decl_iterator j = ns_decl->decls_begin();
		if (j == ns_decl->decls_end())
		{
			LOG(spec, WARNING, "Ill-formed Reflection Spec; no body found\n");
			return 0;
		}

		// Cast to a C++ record
		clang::CXXRecordDecl* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(*j);
		if (record_decl == 0)
		{
			LOG(spec, WARNING, "Ill-formed Reflection Spec; first declaration must be a reflection structure\n");
			return 0;
		}

		// Get the first attribute for the C++ record
		clang::specific_attr_iterator<clang::AnnotateAttr> k = record_decl->specific_attr_begin<clang::AnnotateAttr>();
		if (k == record_decl->specific_attr_end<clang::AnnotateAttr>())
		{
			LOG(spec, WARNING, "Ill-formed Reflection Spec; no annotation attribute found on the reflection structure\n");
			return 0;
		}

		return *k;
	}


	void AddUnmarkedSpecs(std::map<std::string, bool>& specs)
	{
		// Loop up through the parent scopes looking for unmarked names
		for (std::map<std::string, bool>::const_iterator i = specs.begin(); i != specs.end(); ++i)
		{
			std::string spec = i->first;
			while (true)
			{
				size_t sep_pos = spec.rfind("::");
				if (sep_pos == spec.npos)
					break;

				// Insert partial reflection requests to ensure their primitives are created
				// to contain the children
				spec = spec.substr(0, sep_pos);
				std::map<std::string, bool>::const_iterator j = specs.find(spec);
				if (j == specs.end())
					specs[spec] = true;
			}
		}
	}


	void CheckForWarnings(std::map<std::string, bool>& specs)
	{
		// Loop through the parents of all scoped names
		for (std::map<std::string, bool>::const_iterator i = specs.begin(); i != specs.end(); ++i)
		{
			// Only interested in scanning full reflection specs
			std::string spec = i->first;
			if (i->second == true)
				continue;

			while (true)
			{
				size_t sep_pos = spec.rfind("::");
				if (sep_pos == spec.npos)
					break;

				spec = spec.substr(0, sep_pos);
				std::map<std::string, bool>::const_iterator j = specs.find(spec);
				if (j != specs.end() && j->second == false)
					LOG(spec, WARNING, "Reflection Spec for '%s' unnecessary as the parent '%s' has already been marked for full Reflection\n", i->first.c_str(), spec.c_str());
			}
		}
	}
}


ReflectionSpecs::ReflectionSpecs(bool reflect_all, const std::string& spec_log)
	: m_ReflectAll(reflect_all)
{
	LOG_TO_STDOUT(spec, WARNING);
	LOG_TO_STDOUT(spec, ERROR);

	if (spec_log != "")
		LOG_TO_FILE(spec, ALL, spec_log.c_str());
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

		enum Type
		{
			FULL_REFLECTION = 1,
			PARTIAL_REFLECTION = 2,
			CONTAINER_REFLECTION = 4
		};

		// Decode the reflection spec type
		Type type;
		llvm::StringRef reflect_spec = attribute->getAnnotation();
		if (reflect_spec.startswith("full-"))
			type = FULL_REFLECTION;
		else if (reflect_spec.startswith("part-"))
			type = PARTIAL_REFLECTION;
		else if (reflect_spec.startswith("container-"))
			type = CONTAINER_REFLECTION;
		else
			LOG(spec, WARNING, "Ill-formed Reflection Spec; couldn't figure out what type it is\n");

		if (type & (FULL_REFLECTION | PARTIAL_REFLECTION))
		{
			AddReflectionSpec(reflect_spec.substr(5), type == PARTIAL_REFLECTION);
		}

		else if (type & CONTAINER_REFLECTION)
		{
			// Split the fields of the annotation
			llvm::SmallVector<llvm::StringRef, 5> info;
			reflect_spec.split(info, "-");
			if (info.size() != 5)
			{
				LOG(spec, WARNING, "Ill-formed Reflection Spec Container; element count doesn't match expected count\n");
				++i;
				continue;
			}

			// Parse the type info
			ReflectionSpecContainer rsc;
			rsc.read_iterator_type = info[2];
			rsc.write_iterator_type = info[3];
			rsc.has_key = false;

			// Parse the key info
			if (info[4] == "haskey")
				rsc.has_key = true;
			else if (info[4] != "nokey")
			{
				LOG(spec, WARNING, "Ill-formed Reflection Spec Container; expecting 'haskey' or 'nokey' as last parameter\n");
				++i;
				continue;
			}

			m_ContainerSpecs[info[1].str()] = rsc;
			LOG(spec, INFO, "Reflection Spec Container: %s / %s / %s / %s", info[1].str().c_str(), info[2].str().c_str(), info[3].str().c_str(), info[4].str().c_str());
		}

		++i;
	}

	AddUnmarkedSpecs(m_ReflectionSpecs);
	CheckForWarnings(m_ReflectionSpecs);
}


bool ReflectionSpecs::IsReflected(std::string name) const
{
	// Check the optional override first
	if (m_ReflectAll)
		return true;

	// If the symbol itself has been marked for reflection, it's irrelevant whether it's for partial
	// or full reflection - just reflect it. It's the contents that vary on this.
	if (m_ReflectionSpecs.find(name) != m_ReflectionSpecs.end())
		return true;

	// Loop up through the parent scopes looking for the closest reflection spec
	while (true)
	{
		size_t sep_pos = name.rfind("::");
		if (sep_pos == name.npos)
			break;

		// If a parent has a reflection spec entry, only reflect if it's full reflection
		name = name.substr(0, sep_pos);
		std::map<std::string, bool>::const_iterator i = m_ReflectionSpecs.find(name);
		if (i != m_ReflectionSpecs.end())
			return i->second == false;
	}

	return false;
}


void ReflectionSpecs::AddReflectionSpec(const std::string& symbol, bool partial)
{
	// Check for existence in the map before adding
	if (m_ReflectionSpecs.find(symbol) == m_ReflectionSpecs.end())
	{
		m_ReflectionSpecs[symbol] = partial;
		LOG(spec, INFO, "Reflection Spec: %s (%s)\n", symbol.c_str(), partial ? "partial" : "full");
	}
}