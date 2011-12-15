
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

	void GetIncludedFiles(std::vector<std::pair<HeaderType,std::string>>& files) const;

	clang::ASTContext& GetASTContext() { return m_CompilerInstance.getASTContext(); }

private:
	llvm::raw_fd_ostream m_OutputStream;
	clang::DiagnosticOptions m_DiagnosticOptions;
	llvm::OwningPtr<clang::CompilerInvocation> m_CompilerInvocation;
	clang::CompilerInstance m_CompilerInstance;
	llvm::OwningPtr<clang::TargetInfo> m_TargetInfo;
};
