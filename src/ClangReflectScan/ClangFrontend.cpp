
#include "ClangFrontend.h"

#include "llvm/Support/Host.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/DiagnosticOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"	
#include "clang/Frontend/Utils.h"
#include "clang/Parse/ParseAST.h"


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


ClangHost::ClangHost()
	// VC2005: If shouldClose is set to true, this forces an assert in the CRT on program
	// shutdown as stdout hasn't been opened by the app in the first place.
	: output_stream(1, false)
{
	// Create a diagnostic object for reporting warnings and errors to the user
	clang::DiagnosticOptions diag_options;
	clang::TextDiagnosticPrinter* text_diag_printer = new clang::TextDiagnosticPrinter(output_stream, diag_options);
	llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diag_id(new clang::DiagnosticIDs());
	diagnostic.reset(new clang::Diagnostic(diag_id, text_diag_printer));

	// Setup the language parsing options for C++
	lang_options.CPlusPlus = 1;
	lang_options.Bool = 1;

	// Setup access to the filesystem
	clang::FileSystemOptions fs_options;
	file_manager.reset(new clang::FileManager(fs_options));

	// Setup C++ header searching
	header_search.reset(new clang::HeaderSearch(*file_manager));
	//headerSearchOptions.AddPath("/usr/include/linux",
	//		clang::frontend::Angled,
	//		false,
	//		false,
	//		false);

	// Get the target machine info
	clang::TargetOptions target_options;
	target_options.Triple = llvm::sys::getHostTriple();
	target_info.reset(clang::TargetInfo::CreateTargetInfo(*diagnostic, target_options));

	// This will commit the header search options to the header search object
	clang::ApplyHeaderSearchOptions(*header_search, header_search_options, lang_options, target_info->getTriple());
}


ClangASTParser::ClangASTParser(ClangHost& host)
	: m_ClangHost(host)
	, m_SourceManager(*host.diagnostic, *host.file_manager)
	, m_IDTable(host.lang_options)
	, m_BuiltinContext(*host.target_info)
{
	// Initialise the pre-processor
	m_Preprocessor.reset(new clang::Preprocessor(
		*m_ClangHost.diagnostic,
		m_ClangHost.lang_options,
		*m_ClangHost.target_info,
		m_SourceManager,
		*m_ClangHost.header_search));
	clang::InitializePreprocessor(*m_Preprocessor, m_PPOptions, m_ClangHost.header_search_options, m_FEOptions);

	// Initialise the AST context
	m_ASTContext.reset(new clang::ASTContext(
		m_ClangHost.lang_options,
		m_SourceManager,
		*m_ClangHost.target_info,
		m_IDTable,
		m_SelectorTable,
		m_BuiltinContext,
		0));
}


void ClangASTParser::ParseAST(const char* filename)
{
	// Get the file  from the file system
	const clang::FileEntry* file = m_ClangHost.file_manager->getFile(filename);
	m_SourceManager.createMainFileID(file);

	// Parse the AST
	EmptyASTConsumer ast_consumer;
	clang::DiagnosticClient* client = m_ClangHost.diagnostic->getClient();
	client->BeginSourceFile(m_ClangHost.lang_options, m_Preprocessor.get());
	clang::ParseAST(*m_Preprocessor, &ast_consumer, *m_ASTContext);
	client->EndSourceFile();
}
