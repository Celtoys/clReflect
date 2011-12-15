
//
// ===============================================================================
// clReflect
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

#include "ClangFrontend.h"

#include <clReflectCore/Arguments.h>

#include "llvm/Support/Host.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"	
#include "clang/Frontend/LangStandard.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/Sema.h"


namespace
{
	//
	// Empty consumer that gets called during parsing of the AST.
	// I'm not entirely sure how to build an AST without having a callback called
	// for each top-level declaration so this will do for now.
	//
	struct EmptyASTConsumer : public clang::ASTConsumer
	{
		virtual ~EmptyASTConsumer() { }
		virtual void HandleTopLevelDecl(clang::DeclGroupRef) { }
	};
}


ClangParser::ClangParser(Arguments& args)
	// VC2005: If shouldClose is set to true, this forces an assert in the CRT on program
	// shutdown as stdout hasn't been opened by the app in the first place.
	: m_OutputStream(1, false)
{
	m_CompilerInvocation.reset(new clang::CompilerInvocation);

	// Setup the language parsing options for C++
	clang::LangOptions& lang_options = m_CompilerInvocation->getLangOpts();
	m_CompilerInvocation->setLangDefaults(lang_options, clang::IK_CXX, clang::LangStandard::lang_cxx03);
	lang_options.CPlusPlus = 1;
	lang_options.Bool = 1;
	lang_options.MicrosoftExt = 1;
	lang_options.MicrosoftMode = 1;
	lang_options.MSBitfields = 1;
	lang_options.RTTI = 0;

	//
	// This is MSVC specific to get STL compiling with clang. MSVC doesn't do semantic analysis
	// of templates until instantiation, whereas clang will try to resolve non type-based function
	// calls. In MSVC STL land, this causes hundreds of errors referencing '_invalid_parameter_noinfo'.
	//
	// The problem in a nutshell:
	//
	//    template <typename TYPE> void A()
	//    {
	//       // Causes an error in clang because B() is not defined yet, MSVC is fine
	//       B();
	//    }
	//    void B() { }
	//
	lang_options.DelayedTemplateParsing = 1;

	// Gather C++ header searches from the command-line
	clang::HeaderSearchOptions& header_search_options = m_CompilerInvocation->getHeaderSearchOpts();
	for (int i = 0; ; i++)
	{
		std::string include = args.GetProperty("-i", i);
		if (include == "")
			break;
		header_search_options.AddPath(include.c_str(), clang::frontend::Angled, false, false, false);
	}
	for (int i = 0; ; i++)
	{
		std::string include = args.GetProperty("-isystem", i);
		if (include == "")
			break;
		header_search_options.AddPath(include.c_str(), clang::frontend::System, false, false, false);
	}

	// Setup diagnostics output; MSVC line-clicking and suppress warnings from system headers
	m_DiagnosticOptions.Format = m_DiagnosticOptions.Msvc;
	clang::TextDiagnosticPrinter *client = new clang::TextDiagnosticPrinter(m_OutputStream, m_DiagnosticOptions);
	m_CompilerInstance.createDiagnostics(0, NULL, client);
	m_CompilerInstance.getDiagnostics().setSuppressSystemWarnings(true);

	// Setup target options - ensure record layout calculations use the MSVC C++ ABI
	clang::TargetOptions& target_options = m_CompilerInvocation->getTargetOpts();
	target_options.Triple = llvm::sys::getHostTriple();
	target_options.CXXABI = "microsoft";
	m_TargetInfo.reset(clang::TargetInfo::CreateTargetInfo(m_CompilerInstance.getDiagnostics(), target_options));
	m_CompilerInstance.setTarget(m_TargetInfo.take());

	// Set the invokation on the instance
	m_CompilerInstance.createFileManager();
	m_CompilerInstance.createSourceManager(m_CompilerInstance.getFileManager());
	m_CompilerInstance.setInvocation(m_CompilerInvocation.take());
}


bool ClangParser::ParseAST(const char* filename)
{
	// Recreate preprocessor and AST context
	m_CompilerInstance.createPreprocessor();
	m_CompilerInstance.createASTContext();

	// Get the file  from the file system
	const clang::FileEntry* file = m_CompilerInstance.getFileManager().getFile(filename);
	m_CompilerInstance.getSourceManager().createMainFileID(file);

	// Parse the AST
	EmptyASTConsumer ast_consumer;
	clang::DiagnosticConsumer* client = m_CompilerInstance.getDiagnostics().getClient();
	client->BeginSourceFile(m_CompilerInstance.getLangOpts(), &m_CompilerInstance.getPreprocessor());
	clang::ParseAST(m_CompilerInstance.getPreprocessor(), &ast_consumer, m_CompilerInstance.getASTContext());
	client->EndSourceFile();

	return client->getNumErrors() == 0;
}



void ClangParser::GetIncludedFiles(std::vector<std::pair<HeaderType,std::string>>& files) const
{
	// First need a mapping from unique file ID to the File Entry
	llvm::SmallVector<const clang::FileEntry*, 0> uid_to_files;
	m_CompilerInstance.getFileManager().GetUniqueIDMapping(uid_to_files);

	// Now iterate over every included header file info
	clang::HeaderSearch& header_search = m_CompilerInstance.getPreprocessor().getHeaderSearchInfo();
	clang::HeaderSearch::header_file_iterator begin = header_search.header_file_begin();
	clang::HeaderSearch::header_file_iterator end = header_search.header_file_end();
	for (clang::HeaderSearch::header_file_iterator i = begin; i != end; ++i)
	{
		// Map from header file info to file entry and add to the filename list
		// Header file infos are stored in their vector, indexed by file UID and this
		// is the only way you can get at that index
		size_t file_uid = std::distance(begin, i);
		const clang::FileEntry* file_entry = uid_to_files[file_uid];
		HeaderType header_type;
		if (header_search.getFileDirFlavor(file_entry)==clang::SrcMgr::C_User)
			header_type = HeaderType_User;
		else if (header_search.getFileDirFlavor(file_entry)==clang::SrcMgr::C_System)
			header_type = HeaderType_System;
		else
			header_type = HeaderType_ExternC;

		files.push_back(std::pair<HeaderType,std::string>(header_type,file_entry->getName()));
	}
}
