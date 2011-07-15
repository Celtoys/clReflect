
#pragma once


#include <map>


namespace clang
{
	class TranslationUnitDecl;
}

namespace crdb
{
	class Database;
}


// TODO: Optimise the use of strings in this class - it's not really any good
class ReflectionSpecs
{
public:
	ReflectionSpecs(bool reflect_all);

	void Gather(clang::TranslationUnitDecl* tu_decl);

	bool IsReflected(std::string name) const;

	void Write(const char* filename, const crdb::Database& db) const;

private:
	bool m_ReflectAll;

	std::map<std::string, bool> m_ReflectionSpecs;
};