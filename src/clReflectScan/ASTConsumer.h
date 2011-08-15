
#include "clReflectCore/Database.h"


class ReflectionSpecs;


namespace clang
{
	class ASTContext;
	class ASTRecordLayout;
	class NamedDecl;
	class TranslationUnitDecl;
}


class ASTConsumer
{
public:
	ASTConsumer(clang::ASTContext& context, cldb::Database& db, const ReflectionSpecs& rspecs, const std::string& ast_log);

	void WalkTranlationUnit(clang::TranslationUnitDecl* tu_decl);

private:
	void AddDecl(clang::NamedDecl* decl, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout);
	void AddNamespaceDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddClassDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddEnumDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddFunctionDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddMethodDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddFieldDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout);
	void AddClassTemplateDecl(clang::NamedDecl* decl, const cldb::Name& name, const cldb::Name& parent_name);
	void AddContainedDecls(clang::NamedDecl* decl, const cldb::Name& parent_name, const clang::ASTRecordLayout* layout);

	cldb::Database& m_DB;

	clang::ASTContext& m_ASTContext;

	const ReflectionSpecs& m_ReflectionSpecs;
};
