
//
// ===============================================================================
// clReflect, ClangFrontend.h - All code required to initialise and control
// the clang frontend.
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


struct Arguments;


//
// Shared clang objects used during the parsing ASTs
//
struct ClangHost
{
	ClangHost(Arguments& args);

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

	bool ParseAST(const char* filename);

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