
#pragma once

#include "llvm/Support/raw_ostream.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/HeaderSearchOptions.h"
#include "clang/Frontend/PreprocessorOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"


namespace clang
{
	class Diagnostic;
	class FileManager;
	class HeaderSearch;
	class Sema;
}


//
// Shared clang objects used during the parsing ASTs
//
struct ClangHost
{
	ClangHost();

	llvm::raw_fd_ostream output_stream;

	clang::DiagnosticOptions diag_options;
	clang::LangOptions lang_options;
	clang::HeaderSearchOptions header_search_options;

	llvm::OwningPtr<clang::Diagnostic> diagnostic;
	llvm::OwningPtr<clang::FileManager> file_manager;
	llvm::OwningPtr<clang::HeaderSearch> header_search;
	llvm::OwningPtr<clang::TargetInfo> target_info;
};


//
// Parse a file token stream, building a clang AST Context
// This context can then be used to walk the AST as many times as needed
//
class ClangASTParser
{
public:
	ClangASTParser(ClangHost& host);

	void ParseAST(const char* filename);

	void GetIncludedFiles(std::vector<std::string>& files) const;

	clang::ASTContext& GetASTContext() { return *m_ASTContext; }

private:

	ClangHost& m_ClangHost;

	// Need a source manager for managing all loaded C++ files
	clang::SourceManager m_SourceManager;

	// Pre-processor and frontend options
	clang::PreprocessorOptions m_PPOptions;
	clang::FrontendOptions m_FEOptions;

	// Create a preprocessor
	llvm::OwningPtr<clang::Preprocessor> m_Preprocessor;

	clang::IdentifierTable m_IDTable;
	clang::SelectorTable m_SelectorTable;
	clang::Builtin::Context m_BuiltinContext;

	// Create an AST context
	llvm::OwningPtr<clang::ASTContext> m_ASTContext;

	// Create a semantic analysis object
	llvm::OwningPtr<clang::Sema> m_Sema;
};