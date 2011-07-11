
#include "clang/AST/ASTConsumer.h"

#include "Database.h"


namespace clang
{
	class NamedDecl;
}


class ASTConsumer : public clang::ASTConsumer
{
public:
    ASTConsumer();
    virtual ~ASTConsumer();

    virtual void HandleTopLevelDecl(clang::DeclGroupRef d);

private:
	void AddDecl(clang::NamedDecl* decl, const crdb::Name& parent_name);
	void AddNamespaceDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddClassDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddEnumDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddFunctionDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddMethodDecl(clang::NamedDecl* decl, const crdb::Name& name, const crdb::Name& parent_name);
	void AddContainedDecls(clang::NamedDecl* decl, const crdb::Name& parent_name);

	crdb::Database m_DB;
};
