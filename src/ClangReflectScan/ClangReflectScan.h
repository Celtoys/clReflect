
#pragma once


#include "llvm/ADT/OwningPtr.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Frontend/HeaderSearchOptions.h"
#include "clang/Lex/HeaderSearch.h"


namespace crdb
{
	class Database;
}


class ClangReflectScan
{
public:
	ClangReflectScan();

	void ConsumeAST(const char* filename, crdb::Database& db);

private:
	clang::LangOptions m_LangOptions;
	clang::HeaderSearchOptions m_HeaderSearchOptions;

	llvm::OwningPtr<clang::Diagnostic> m_Diagnostic;
	llvm::OwningPtr<clang::FileManager> m_FileManager;
	llvm::OwningPtr<clang::HeaderSearch> m_HeaderSearch;
	llvm::OwningPtr<clang::TargetInfo> m_TargetInfo;
};
