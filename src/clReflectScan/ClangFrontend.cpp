
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "ClangFrontend.h"

#include <clcpp/clcpp.h>
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
		virtual bool HandleTopLevelDecl(clang::DeclGroupRef) { return true; }
	};
}


ClangParser::ClangParser(Arguments& args)
	// VC2005: If shouldClose is set to true, this forces an assert in the CRT on program
	// shutdown as stdout hasn't been opened by the app in the first place.
	: m_OutputStream(1, false)
{
	m_CompilerInvocation.reset(new clang::CompilerInvocation);

	// we add a customized macro here to distinguish a clreflect parsing process from a compling using clang
	clang::PreprocessorOptions& preprocessor_options = m_CompilerInvocation->getPreprocessorOpts();
	preprocessor_options.addMacroDef("__clcpp_parse__");

	// Add define/undefine macros to the pre-processor
	for (int i = 0; ; i++)
	{
		std::string macro = args.GetProperty("-D", i);
		if (macro == "")
			break;
		preprocessor_options.addMacroDef(macro.c_str());
	}
	for (int i = 0; ; i++)
	{
		std::string macro = args.GetProperty("-U", i);
		if (macro == "")
			break;
		preprocessor_options.addMacroUndef(macro.c_str());
	}

	// Setup the language parsing options for C++
	clang::LangOptions& lang_options = *m_CompilerInvocation->getLangOpts();
	m_CompilerInvocation->setLangDefaults(lang_options, clang::IK_CXX, clang::LangStandard::lang_cxx11);
	lang_options.CPlusPlus = 1;
	lang_options.Bool = 1;
	lang_options.RTTI = 0;

#if defined(CLCPP_USING_MSVC)
	lang_options.MicrosoftExt = 1;
	lang_options.MicrosoftMode = 1;
	lang_options.MSBitfields = 1;

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
#endif	// CLCPP_USING_MSVC

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
#if defined(CLCPP_USING_MSVC)
	m_DiagnosticOptions.Format = m_DiagnosticOptions.Msvc;
#else
	m_DiagnosticOptions.Format = m_DiagnosticOptions.Clang;
#endif	// CLCPP_USING_MSVC
	clang::TextDiagnosticPrinter *client = new clang::TextDiagnosticPrinter(m_OutputStream, m_DiagnosticOptions);
	m_CompilerInstance.createDiagnostics(0, NULL, client);
	m_CompilerInstance.getDiagnostics().setSuppressSystemWarnings(true);

	// Setup target options - ensure record layout calculations use the MSVC C++ ABI
	clang::TargetOptions& target_options = m_CompilerInvocation->getTargetOpts();
	target_options.Triple = llvm::sys::getDefaultTargetTriple();
#if defined(CLCPP_USING_MSVC)
	target_options.CXXABI = "microsoft";
#else
	target_options.CXXABI = "itanium";
#endif	// CLCPP_USING_MSVC
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

	// Initialize builtins
	if (m_CompilerInstance.hasPreprocessor()) {
		clang::Preprocessor& preprocessor = m_CompilerInstance.getPreprocessor();
		preprocessor.getBuiltinInfo().InitializeBuiltins(preprocessor.getIdentifierTable(),
			preprocessor.getLangOpts());
	}

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



void ClangParser::GetIncludedFiles(std::vector< std::pair<HeaderType,std::string> >& files) const
{
	const clang::HeaderSearch& header_search = m_CompilerInstance.getPreprocessor().getHeaderSearchInfo();

	// Get all files loaded during the scan
	llvm::SmallVector<const clang::FileEntry*, 16> file_uids;
	header_search.getFileMgr().GetUniqueIDMapping(file_uids);

	for (unsigned uid = 0, last_uid = file_uids.size(); uid != last_uid; ++uid)
	{
		const clang::FileEntry* fe = file_uids[uid];
		if (fe == 0)
			continue;

		// Only interested in header files
		const clang::HeaderFileInfo& hfi = header_search.getFileInfo(fe);
		if (!hfi.isNonDefault())
			continue;

		// Classify the kind of include
		clang::SrcMgr::CharacteristicKind kind = (clang::SrcMgr::CharacteristicKind)hfi.DirInfo;
		HeaderType header_type;
		if (kind == clang::SrcMgr::C_User)
			header_type = HeaderType_User;
		else if (kind == clang::SrcMgr::C_System)
			header_type = HeaderType_System;
		else
			header_type = HeaderType_ExternC;

		files.push_back(std::pair<HeaderType,std::string>(header_type, fe->getName()));
	}
}
