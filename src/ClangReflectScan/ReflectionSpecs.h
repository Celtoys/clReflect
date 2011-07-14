
#pragma once

#include "llvm/ADT/StringMap.h"


namespace clang
{
	class TranslationUnitDecl;
}


class ReflectionSpecs
{
public:
	void Gather(clang::TranslationUnitDecl* tu_decl);

	bool IsReflected(llvm::StringRef name) const;

private:
	llvm::StringMap<bool> m_ReflectionSpecs;
};