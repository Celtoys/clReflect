
//
// ===============================================================================
// clReflect, ASTConsumer.h - Traversal of the clang AST for C++, returning an
// offline Reflection Database.
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

#include "clReflectCore/Database.h"


class ReflectionSpecs;


namespace clang
{
	class ASTContext;
	class ASTRecordLayout;
	class NamedDecl;
	class TranslationUnitDecl;
	class QualType;
}


class ASTConsumer
{
public:
	ASTConsumer(clang::ASTContext& context, cldb::Database& db, const ReflectionSpecs& rspecs, const std::string& ast_log);

	void WalkTranlationUnit(clang::TranslationUnitDecl* tu_decl);

private:
	void AddDecl(clang::NamedDecl* decl, const std::string& parent_name, const clang::ASTRecordLayout* layout);
	void AddNamespaceDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddClassDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddEnumDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddFunctionDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddMethodDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddFieldDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name, const clang::ASTRecordLayout* layout);
	void AddClassTemplateDecl(clang::NamedDecl* decl, const std::string& name, const std::string& parent_name);
	void AddContainedDecls(clang::NamedDecl* decl, const std::string& parent_name, const clang::ASTRecordLayout* layout);

	bool MakeField(clang::QualType qual_type, const char* param_name, const std::string& parent_name, int index, cldb::Field& field, int flags);
	void MakeFunction(clang::NamedDecl* decl, const std::string& function_name, const std::string& parent_name, std::vector<cldb::Field>& parameters);

	cldb::Database& m_DB;

	clang::ASTContext& m_ASTContext;

	const ReflectionSpecs& m_ReflectionSpecs;
};
