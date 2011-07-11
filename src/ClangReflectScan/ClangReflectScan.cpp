
#include "ClangReflectScan.h"
#include "ASTConsumer.h"

#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/PreprocessorOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"	
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/Sema.h"


ClangReflectScan::ClangReflectScan()
{
	// Create a diagnostic object for reporting warnings and errors to the user
	clang::DiagnosticOptions diag_options;
	clang::TextDiagnosticPrinter* text_diag_printer = new clang::TextDiagnosticPrinter(llvm::outs(), diag_options);
	llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diag_id(new clang::DiagnosticIDs());
	m_Diagnostic.reset(new clang::Diagnostic(diag_id, text_diag_printer));

	// Setup the language parsing options for C++
	m_LangOptions.CPlusPlus = 1;
	m_LangOptions.Bool = 1;

	// Setup access to the filesystem
	clang::FileSystemOptions fs_options;
	m_FileManager.reset(new clang::FileManager(fs_options));

	// Setup C++ header searching
	m_HeaderSearch.reset(new clang::HeaderSearch(*m_FileManager));
	//headerSearchOptions.AddPath("/usr/include/linux",
	//		clang::frontend::Angled,
	//		false,
	//		false,
	//		false);

	// Get the target machine info
	clang::TargetOptions target_options;
	target_options.Triple = llvm::sys::getHostTriple();
	m_TargetInfo.reset(clang::TargetInfo::CreateTargetInfo(*m_Diagnostic, target_options));

	// This will commit the header search options to the header search object
	clang::ApplyHeaderSearchOptions(*m_HeaderSearch, m_HeaderSearchOptions, m_LangOptions, m_TargetInfo->getTriple());
}


void ClangReflectScan::ConsumeAST(const char* filename, crdb::Database& db)
{
	// Need a source manager for managing all loaded C++ files
	clang::SourceManager source_manager(*m_Diagnostic, *m_FileManager);

	// Set up the options for the pre-processor
	clang::PreprocessorOptions pp_options;
	clang::FrontendOptions fe_options;

	// Create a preprocessor
	clang::Preprocessor preprocessor(*m_Diagnostic, m_LangOptions, *m_TargetInfo, source_manager, *m_HeaderSearch);
	clang::InitializePreprocessor(preprocessor, pp_options, m_HeaderSearchOptions, fe_options);

	// Create an AST context
	clang::IdentifierTable id_table(m_LangOptions);
	clang::SelectorTable selector_table;
    clang::Builtin::Context builtin_context(*m_TargetInfo);
	clang::ASTContext ast_context(m_LangOptions, source_manager, *m_TargetInfo, id_table, selector_table, builtin_context, 0);

	// Create a semantic analysis object
	ASTConsumer ast_consumer(ast_context, db);
	clang::Sema sema(preprocessor, ast_context, ast_consumer);

	// Get the file  from the file system
	const clang::FileEntry* file = m_FileManager->getFile(filename);
	source_manager.createMainFileID(file);

	// Parse the AST
	clang::DiagnosticClient* client = m_Diagnostic->getClient();
	client->BeginSourceFile(m_LangOptions, &preprocessor);
	clang::ParseAST(preprocessor, &ast_consumer, ast_context);
	client->EndSourceFile();
}
