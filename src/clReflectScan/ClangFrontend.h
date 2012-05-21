
//
// ===============================================================================
// clReflect, ClangFrontend.h - All code required to initialise and control
// the clang frontend.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once

#include "llvm/Support/raw_ostream.h"

#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"


struct Arguments;


//
// Parse a file token stream, building a clang AST Context
// This context can then be used to walk the AST as many times as needed
//
class ClangParser
{
public:
	ClangParser(Arguments& args);
	enum HeaderType
	{
		HeaderType_User,
		HeaderType_System,
		HeaderType_ExternC
	};

	bool ParseAST(const char* filename);

	void GetIncludedFiles(std::vector< std::pair<HeaderType,std::string> >& files) const;

	clang::ASTContext& GetASTContext() { return m_CompilerInstance.getASTContext(); }

private:
	llvm::raw_fd_ostream m_OutputStream;
	clang::DiagnosticOptions m_DiagnosticOptions;
	llvm::OwningPtr<clang::CompilerInvocation> m_CompilerInvocation;
	clang::CompilerInstance m_CompilerInstance;
	llvm::OwningPtr<clang::TargetInfo> m_TargetInfo;
};
