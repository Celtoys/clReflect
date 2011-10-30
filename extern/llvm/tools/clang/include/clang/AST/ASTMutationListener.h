//===--- ASTMutationListener.h - AST Mutation Interface --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTMutationListener interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_ASTMUTATIONLISTENER_H
#define LLVM_CLANG_AST_ASTMUTATIONLISTENER_H

namespace clang {
  class Decl;
  class DeclContext;
  class TagDecl;
  class CXXRecordDecl;
  class ClassTemplateDecl;
  class ClassTemplateSpecializationDecl;
  class FunctionDecl;
  class FunctionTemplateDecl;
  class ObjCCategoryDecl;
  class ObjCInterfaceDecl;

/// \brief An abstract interface that should be implemented by listeners
/// that want to be notified when an AST entity gets modified after its
/// initial creation.
class ASTMutationListener {
public:
  virtual ~ASTMutationListener();

  /// \brief A new TagDecl definition was completed.
  virtual void CompletedTagDefinition(const TagDecl *D) { }

  /// \brief A new declaration with name has been added to a DeclContext.
  virtual void AddedVisibleDecl(const DeclContext *DC, const Decl *D) {}

  /// \brief An implicit member was added after the definition was completed.
  virtual void AddedCXXImplicitMember(const CXXRecordDecl *RD, const Decl *D) {}

  /// \brief A template specialization (or partial one) was added to the
  /// template declaration.
  virtual void AddedCXXTemplateSpecialization(const ClassTemplateDecl *TD,
                                    const ClassTemplateSpecializationDecl *D) {}

  /// \brief A template specialization (or partial one) was added to the
  /// template declaration.
  virtual void AddedCXXTemplateSpecialization(const FunctionTemplateDecl *TD,
                                              const FunctionDecl *D) {}

  /// \brief An implicit member got a definition.
  virtual void CompletedImplicitDefinition(const FunctionDecl *D) {}

  /// \brief A static data member was implicitly instantiated.
  virtual void StaticDataMemberInstantiated(const VarDecl *D) {}

  /// \brief A new objc category class was added for an interface.
  virtual void AddedObjCCategoryToInterface(const ObjCCategoryDecl *CatD,
                                            const ObjCInterfaceDecl *IFD) {}
};

} // end namespace clang

#endif
