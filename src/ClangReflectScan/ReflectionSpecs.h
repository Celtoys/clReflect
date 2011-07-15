
#pragma once

#include <map>


namespace clang
{
	class TranslationUnitDecl;
}


// TODO: Optimise the use of strings in this class - it's not really any good
class ReflectionSpecs
{
public:
	void Gather(clang::TranslationUnitDecl* tu_decl);

	bool IsReflected(std::string name) const;

private:
	std::map<std::string, bool> m_ReflectionSpecs;
};