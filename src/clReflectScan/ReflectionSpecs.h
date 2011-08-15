
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
	ReflectionSpecs(bool reflect_all, const std::string& spec_log);

	void Gather(clang::TranslationUnitDecl* tu_decl);

	bool IsReflected(std::string name) const;

private:
	bool m_ReflectAll;

	std::map<std::string, bool> m_ReflectionSpecs;
};