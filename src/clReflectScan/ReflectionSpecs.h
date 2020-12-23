
//
// ===============================================================================
// clReflect, ReflectionSpecs.h - First pass traversal of the clang AST for C++,
// locating reflection specifications.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <map>
#include <string>


namespace clang
{
	class TranslationUnitDecl;
}

namespace cldb
{
	class Database;
}


// Info for reflecting a container
struct ReflectionSpecContainer
{
	typedef std::map<std::string, ReflectionSpecContainer> MapType;
	std::string read_iterator_type;
	std::string write_iterator_type;
	bool has_key;
};

enum ReflectionSpecType
{
	RST_None,
	RST_Full,
	RST_Partial,
	RST_Container
};


class ReflectionSpecs
{
public:
    ReflectionSpecs(const std::string& spec_log);

    void Gather(clang::TranslationUnitDecl* tu_decl);

	ReflectionSpecType Get(const std::string& name) const;

	const ReflectionSpecContainer::MapType& GetContainerSpecs() const { return m_ContainerSpecs; }

private:
	void AddReflectionSpec(const std::string& symbol, ReflectionSpecType type);

    typedef std::map<std::string, ReflectionSpecType> ReflectionSpecMap;
	ReflectionSpecMap m_ReflectionSpecs;

	ReflectionSpecContainer::MapType m_ContainerSpecs;
};
