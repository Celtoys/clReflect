//===--- ASTReaderDecl.cpp - Decl Deserialization ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ASTReader::ReadDeclRecord method, which is the
// entrypoint for loading a decl.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
using namespace clang;
using namespace clang::serialization;

//===----------------------------------------------------------------------===//
// Declaration deserialization
//===----------------------------------------------------------------------===//

namespace clang {
  class ASTDeclReader : public DeclVisitor<ASTDeclReader, void> {
    ASTReader &Reader;
    Module &F;
    llvm::BitstreamCursor &Cursor;
    const DeclID ThisDeclID;
    typedef ASTReader::RecordData RecordData;
    const RecordData &Record;
    unsigned &Idx;
    TypeID TypeIDForTypeDecl;
    
    DeclID DeclContextIDForTemplateParmDecl;
    DeclID LexicalDeclContextIDForTemplateParmDecl;

    uint64_t GetCurrentCursorOffset();
    
    SourceLocation ReadSourceLocation(const RecordData &R, unsigned &I) {
      return Reader.ReadSourceLocation(F, R, I);
    }
    
    SourceRange ReadSourceRange(const RecordData &R, unsigned &I) {
      return Reader.ReadSourceRange(F, R, I);
    }
    
    TypeSourceInfo *GetTypeSourceInfo(const RecordData &R, unsigned &I) {
      return Reader.GetTypeSourceInfo(F, R, I);
    }
    
    serialization::DeclID ReadDeclID(const RecordData &R, unsigned &I) {
      return Reader.ReadDeclID(F, R, I);
    }
    
    Decl *ReadDecl(const RecordData &R, unsigned &I) {
      return Reader.ReadDecl(F, R, I);
    }

    template<typename T>
    T *ReadDeclAs(const RecordData &R, unsigned &I) {
      return Reader.ReadDeclAs<T>(F, R, I);
    }

    void ReadQualifierInfo(QualifierInfo &Info,
                           const RecordData &R, unsigned &I) {
      Reader.ReadQualifierInfo(F, Info, R, I);
    }
    
    void ReadDeclarationNameLoc(DeclarationNameLoc &DNLoc, DeclarationName Name,
                                const RecordData &R, unsigned &I) {
      Reader.ReadDeclarationNameLoc(F, DNLoc, Name, R, I);
    }
    
    void ReadDeclarationNameInfo(DeclarationNameInfo &NameInfo,
                                const RecordData &R, unsigned &I) {
      Reader.ReadDeclarationNameInfo(F, NameInfo, R, I);
    }

    void ReadCXXDefinitionData(struct CXXRecordDecl::DefinitionData &Data,
                               const RecordData &R, unsigned &I);

    void InitializeCXXDefinitionData(CXXRecordDecl *D,
                                     CXXRecordDecl *DefinitionDecl,
                                     const RecordData &Record, unsigned &Idx);
  public:
    ASTDeclReader(ASTReader &Reader, Module &F,
                  llvm::BitstreamCursor &Cursor, DeclID thisDeclID,
                  const RecordData &Record, unsigned &Idx)
      : Reader(Reader), F(F), Cursor(Cursor), ThisDeclID(thisDeclID),
        Record(Record), Idx(Idx), TypeIDForTypeDecl(0) { }

    static void attachPreviousDecl(Decl *D, Decl *previous);

    void Visit(Decl *D);

    void UpdateDecl(Decl *D, Module &Module,
                    const RecordData &Record);

    static void setNextObjCCategory(ObjCCategoryDecl *Cat,
                                    ObjCCategoryDecl *Next) {
      Cat->NextClassCategory = Next;
    }

    void VisitDecl(Decl *D);
    void VisitTranslationUnitDecl(TranslationUnitDecl *TU);
    void VisitNamedDecl(NamedDecl *ND);
    void VisitLabelDecl(LabelDecl *LD);
    void VisitNamespaceDecl(NamespaceDecl *D);
    void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    void VisitTypeDecl(TypeDecl *TD);
    void VisitTypedefDecl(TypedefDecl *TD);
    void VisitTypeAliasDecl(TypeAliasDecl *TD);
    void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    void VisitTagDecl(TagDecl *TD);
    void VisitEnumDecl(EnumDecl *ED);
    void VisitRecordDecl(RecordDecl *RD);
    void VisitCXXRecordDecl(CXXRecordDecl *D);
    void VisitClassTemplateSpecializationDecl(
                                            ClassTemplateSpecializationDecl *D);
    void VisitClassTemplatePartialSpecializationDecl(
                                     ClassTemplatePartialSpecializationDecl *D);
    void VisitClassScopeFunctionSpecializationDecl(
                                       ClassScopeFunctionSpecializationDecl *D);
    void VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
    void VisitValueDecl(ValueDecl *VD);
    void VisitEnumConstantDecl(EnumConstantDecl *ECD);
    void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    void VisitDeclaratorDecl(DeclaratorDecl *DD);
    void VisitFunctionDecl(FunctionDecl *FD);
    void VisitCXXMethodDecl(CXXMethodDecl *D);
    void VisitCXXConstructorDecl(CXXConstructorDecl *D);
    void VisitCXXDestructorDecl(CXXDestructorDecl *D);
    void VisitCXXConversionDecl(CXXConversionDecl *D);
    void VisitFieldDecl(FieldDecl *FD);
    void VisitIndirectFieldDecl(IndirectFieldDecl *FD);
    void VisitVarDecl(VarDecl *VD);
    void VisitImplicitParamDecl(ImplicitParamDecl *PD);
    void VisitParmVarDecl(ParmVarDecl *PD);
    void VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
    void VisitTemplateDecl(TemplateDecl *D);
    void VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D);
    void VisitClassTemplateDecl(ClassTemplateDecl *D);
    void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
    void VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D);
    void VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
    void VisitUsingDecl(UsingDecl *D);
    void VisitUsingShadowDecl(UsingShadowDecl *D);
    void VisitLinkageSpecDecl(LinkageSpecDecl *D);
    void VisitFileScopeAsmDecl(FileScopeAsmDecl *AD);
    void VisitAccessSpecDecl(AccessSpecDecl *D);
    void VisitFriendDecl(FriendDecl *D);
    void VisitFriendTemplateDecl(FriendTemplateDecl *D);
    void VisitStaticAssertDecl(StaticAssertDecl *D);
    void VisitBlockDecl(BlockDecl *BD);

    std::pair<uint64_t, uint64_t> VisitDeclContext(DeclContext *DC);
    template <typename T> void VisitRedeclarable(Redeclarable<T> *D);

    // FIXME: Reorder according to DeclNodes.td?
    void VisitObjCMethodDecl(ObjCMethodDecl *D);
    void VisitObjCContainerDecl(ObjCContainerDecl *D);
    void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
    void VisitObjCIvarDecl(ObjCIvarDecl *D);
    void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
    void VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D);
    void VisitObjCClassDecl(ObjCClassDecl *D);
    void VisitObjCForwardProtocolDecl(ObjCForwardProtocolDecl *D);
    void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
    void VisitObjCImplDecl(ObjCImplDecl *D);
    void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
    void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
    void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
  };
}

uint64_t ASTDeclReader::GetCurrentCursorOffset() {
  return F.DeclsCursor.GetCurrentBitNo() + F.GlobalBitOffset;
}

void ASTDeclReader::Visit(Decl *D) {
  DeclVisitor<ASTDeclReader, void>::Visit(D);

  if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
    if (DD->DeclInfo) {
      DeclaratorDecl::ExtInfo *Info =
          DD->DeclInfo.get<DeclaratorDecl::ExtInfo *>();
      Info->TInfo =
          GetTypeSourceInfo(Record, Idx);
    }
    else {
      DD->DeclInfo = GetTypeSourceInfo(Record, Idx);
    }
  }

  if (TypeDecl *TD = dyn_cast<TypeDecl>(D)) {
    // if we have a fully initialized TypeDecl, we can safely read its type now.
    TD->setTypeForDecl(Reader.GetType(TypeIDForTypeDecl).getTypePtrOrNull());
  } else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // FunctionDecl's body was written last after all other Stmts/Exprs.
    if (Record[Idx++])
      FD->setLazyBody(GetCurrentCursorOffset());
  } else if (D->isTemplateParameter()) {
    // If we have a fully initialized template parameter, we can now
    // set its DeclContext.
    D->setDeclContext(
          cast_or_null<DeclContext>(
                            Reader.GetDecl(DeclContextIDForTemplateParmDecl)));
    D->setLexicalDeclContext(
          cast_or_null<DeclContext>(
                      Reader.GetDecl(LexicalDeclContextIDForTemplateParmDecl)));
  }
}

void ASTDeclReader::VisitDecl(Decl *D) {
  if (D->isTemplateParameter()) {
    // We don't want to deserialize the DeclContext of a template
    // parameter immediately, because the template parameter might be
    // used in the formulation of its DeclContext. Use the translation
    // unit DeclContext as a placeholder.
    DeclContextIDForTemplateParmDecl = ReadDeclID(Record, Idx);
    LexicalDeclContextIDForTemplateParmDecl = ReadDeclID(Record, Idx);
    D->setDeclContext(Reader.getContext().getTranslationUnitDecl()); 
  } else {
    D->setDeclContext(ReadDeclAs<DeclContext>(Record, Idx));
    D->setLexicalDeclContext(ReadDeclAs<DeclContext>(Record, Idx));
  }
  D->setLocation(ReadSourceLocation(Record, Idx));
  D->setInvalidDecl(Record[Idx++]);
  if (Record[Idx++]) { // hasAttrs
    AttrVec Attrs;
    Reader.ReadAttributes(F, Attrs, Record, Idx);
    D->setAttrs(Attrs);
  }
  D->setImplicit(Record[Idx++]);
  D->setUsed(Record[Idx++]);
  D->setReferenced(Record[Idx++]);
  D->setAccess((AccessSpecifier)Record[Idx++]);
  D->FromASTFile = true;
  D->ModulePrivate = Record[Idx++];
}

void ASTDeclReader::VisitTranslationUnitDecl(TranslationUnitDecl *TU) {
  llvm_unreachable("Translation units are not serialized");
}

void ASTDeclReader::VisitNamedDecl(NamedDecl *ND) {
  VisitDecl(ND);
  ND->setDeclName(Reader.ReadDeclarationName(F, Record, Idx));
}

void ASTDeclReader::VisitTypeDecl(TypeDecl *TD) {
  VisitNamedDecl(TD);
  TD->setLocStart(ReadSourceLocation(Record, Idx));
  // Delay type reading until after we have fully initialized the decl.
  TypeIDForTypeDecl = Reader.getGlobalTypeID(F, Record[Idx++]);
}

void ASTDeclReader::VisitTypedefDecl(TypedefDecl *TD) {
  VisitTypeDecl(TD);
  TD->setTypeSourceInfo(GetTypeSourceInfo(Record, Idx));
}

void ASTDeclReader::VisitTypeAliasDecl(TypeAliasDecl *TD) {
  VisitTypeDecl(TD);
  TD->setTypeSourceInfo(GetTypeSourceInfo(Record, Idx));
}

void ASTDeclReader::VisitTagDecl(TagDecl *TD) {
  VisitTypeDecl(TD);
  VisitRedeclarable(TD);
  TD->IdentifierNamespace = Record[Idx++];
  TD->setTagKind((TagDecl::TagKind)Record[Idx++]);
  TD->setCompleteDefinition(Record[Idx++]);
  TD->setEmbeddedInDeclarator(Record[Idx++]);
  TD->setFreeStanding(Record[Idx++]);
  TD->setRBraceLoc(ReadSourceLocation(Record, Idx));
  if (Record[Idx++]) { // hasExtInfo
    TagDecl::ExtInfo *Info = new (Reader.getContext()) TagDecl::ExtInfo();
    ReadQualifierInfo(*Info, Record, Idx);
    TD->TypedefNameDeclOrQualifier = Info;
  } else
    TD->setTypedefNameForAnonDecl(ReadDeclAs<TypedefNameDecl>(Record, Idx));
}

void ASTDeclReader::VisitEnumDecl(EnumDecl *ED) {
  VisitTagDecl(ED);
  if (TypeSourceInfo *TI = Reader.GetTypeSourceInfo(F, Record, Idx))
    ED->setIntegerTypeSourceInfo(TI);
  else
    ED->setIntegerType(Reader.readType(F, Record, Idx));
  ED->setPromotionType(Reader.readType(F, Record, Idx));
  ED->setNumPositiveBits(Record[Idx++]);
  ED->setNumNegativeBits(Record[Idx++]);
  ED->IsScoped = Record[Idx++];
  ED->IsScopedUsingClassTag = Record[Idx++];
  ED->IsFixed = Record[Idx++];
  ED->setInstantiationOfMemberEnum(ReadDeclAs<EnumDecl>(Record, Idx));
}

void ASTDeclReader::VisitRecordDecl(RecordDecl *RD) {
  VisitTagDecl(RD);
  RD->setHasFlexibleArrayMember(Record[Idx++]);
  RD->setAnonymousStructOrUnion(Record[Idx++]);
  RD->setHasObjectMember(Record[Idx++]);
}

void ASTDeclReader::VisitValueDecl(ValueDecl *VD) {
  VisitNamedDecl(VD);
  VD->setType(Reader.readType(F, Record, Idx));
}

void ASTDeclReader::VisitEnumConstantDecl(EnumConstantDecl *ECD) {
  VisitValueDecl(ECD);
  if (Record[Idx++])
    ECD->setInitExpr(Reader.ReadExpr(F));
  ECD->setInitVal(Reader.ReadAPSInt(Record, Idx));
}

void ASTDeclReader::VisitDeclaratorDecl(DeclaratorDecl *DD) {
  VisitValueDecl(DD);
  DD->setInnerLocStart(ReadSourceLocation(Record, Idx));
  if (Record[Idx++]) { // hasExtInfo
    DeclaratorDecl::ExtInfo *Info
        = new (Reader.getContext()) DeclaratorDecl::ExtInfo();
    ReadQualifierInfo(*Info, Record, Idx);
    DD->DeclInfo = Info;
  }
}

void ASTDeclReader::VisitFunctionDecl(FunctionDecl *FD) {
  VisitDeclaratorDecl(FD);
  VisitRedeclarable(FD);

  ReadDeclarationNameLoc(FD->DNLoc, FD->getDeclName(), Record, Idx);
  FD->IdentifierNamespace = Record[Idx++];
  switch ((FunctionDecl::TemplatedKind)Record[Idx++]) {
  default: llvm_unreachable("Unhandled TemplatedKind!");
  case FunctionDecl::TK_NonTemplate:
    break;
  case FunctionDecl::TK_FunctionTemplate:
    FD->setDescribedFunctionTemplate(ReadDeclAs<FunctionTemplateDecl>(Record, 
                                                                      Idx));
    break;
  case FunctionDecl::TK_MemberSpecialization: {
    FunctionDecl *InstFD = ReadDeclAs<FunctionDecl>(Record, Idx);
    TemplateSpecializationKind TSK = (TemplateSpecializationKind)Record[Idx++];
    SourceLocation POI = ReadSourceLocation(Record, Idx);
    FD->setInstantiationOfMemberFunction(Reader.getContext(), InstFD, TSK);
    FD->getMemberSpecializationInfo()->setPointOfInstantiation(POI);
    break;
  }
  case FunctionDecl::TK_FunctionTemplateSpecialization: {
    FunctionTemplateDecl *Template = ReadDeclAs<FunctionTemplateDecl>(Record, 
                                                                      Idx);
    TemplateSpecializationKind TSK = (TemplateSpecializationKind)Record[Idx++];
    
    // Template arguments.
    SmallVector<TemplateArgument, 8> TemplArgs;
    Reader.ReadTemplateArgumentList(TemplArgs, F, Record, Idx);
    
    // Template args as written.
    SmallVector<TemplateArgumentLoc, 8> TemplArgLocs;
    SourceLocation LAngleLoc, RAngleLoc;
    bool HasTemplateArgumentsAsWritten = Record[Idx++];
    if (HasTemplateArgumentsAsWritten) {
      unsigned NumTemplateArgLocs = Record[Idx++];
      TemplArgLocs.reserve(NumTemplateArgLocs);
      for (unsigned i=0; i != NumTemplateArgLocs; ++i)
        TemplArgLocs.push_back(
            Reader.ReadTemplateArgumentLoc(F, Record, Idx));
  
      LAngleLoc = ReadSourceLocation(Record, Idx);
      RAngleLoc = ReadSourceLocation(Record, Idx);
    }
    
    SourceLocation POI = ReadSourceLocation(Record, Idx);

    ASTContext &C = Reader.getContext();
    TemplateArgumentList *TemplArgList
      = TemplateArgumentList::CreateCopy(C, TemplArgs.data(), TemplArgs.size());
    TemplateArgumentListInfo TemplArgsInfo(LAngleLoc, RAngleLoc);
    for (unsigned i=0, e = TemplArgLocs.size(); i != e; ++i)
      TemplArgsInfo.addArgument(TemplArgLocs[i]);
    FunctionTemplateSpecializationInfo *FTInfo
        = FunctionTemplateSpecializationInfo::Create(C, FD, Template, TSK,
                                                     TemplArgList,
                             HasTemplateArgumentsAsWritten ? &TemplArgsInfo : 0,
                                                     POI);
    FD->TemplateOrSpecialization = FTInfo;

    if (FD->isCanonicalDecl()) { // if canonical add to template's set.
      // The template that contains the specializations set. It's not safe to
      // use getCanonicalDecl on Template since it may still be initializing.
      FunctionTemplateDecl *CanonTemplate
        = ReadDeclAs<FunctionTemplateDecl>(Record, Idx);
      // Get the InsertPos by FindNodeOrInsertPos() instead of calling
      // InsertNode(FTInfo) directly to avoid the getASTContext() call in
      // FunctionTemplateSpecializationInfo's Profile().
      // We avoid getASTContext because a decl in the parent hierarchy may
      // be initializing.
      llvm::FoldingSetNodeID ID;
      FunctionTemplateSpecializationInfo::Profile(ID, TemplArgs.data(),
                                                  TemplArgs.size(), C);
      void *InsertPos = 0;
      CanonTemplate->getSpecializations().FindNodeOrInsertPos(ID, InsertPos);
      assert(InsertPos && "Another specialization already inserted!");
      CanonTemplate->getSpecializations().InsertNode(FTInfo, InsertPos);
    }
    break;
  }
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
    // Templates.
    UnresolvedSet<8> TemplDecls;
    unsigned NumTemplates = Record[Idx++];
    while (NumTemplates--)
      TemplDecls.addDecl(ReadDeclAs<NamedDecl>(Record, Idx));
    
    // Templates args.
    TemplateArgumentListInfo TemplArgs;
    unsigned NumArgs = Record[Idx++];
    while (NumArgs--)
      TemplArgs.addArgument(Reader.ReadTemplateArgumentLoc(F, Record, Idx));
    TemplArgs.setLAngleLoc(ReadSourceLocation(Record, Idx));
    TemplArgs.setRAngleLoc(ReadSourceLocation(Record, Idx));
    
    FD->setDependentTemplateSpecialization(Reader.getContext(),
                                           TemplDecls, TemplArgs);
    break;
  }
  }

  // FunctionDecl's body is handled last at ASTDeclReader::Visit,
  // after everything else is read.

  FD->SClass = (StorageClass)Record[Idx++];
  FD->SClassAsWritten = (StorageClass)Record[Idx++];
  FD->IsInline = Record[Idx++];
  FD->IsInlineSpecified = Record[Idx++];
  FD->IsVirtualAsWritten = Record[Idx++];
  FD->IsPure = Record[Idx++];
  FD->HasInheritedPrototype = Record[Idx++];
  FD->HasWrittenPrototype = Record[Idx++];
  FD->IsDeleted = Record[Idx++];
  FD->IsTrivial = Record[Idx++];
  FD->IsDefaulted = Record[Idx++];
  FD->IsExplicitlyDefaulted = Record[Idx++];
  FD->HasImplicitReturnZero = Record[Idx++];
  FD->IsConstexpr = Record[Idx++];
  FD->EndRangeLoc = ReadSourceLocation(Record, Idx);

  // Read in the parameters.
  unsigned NumParams = Record[Idx++];
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(ReadDeclAs<ParmVarDecl>(Record, Idx));
  FD->setParams(Reader.getContext(), Params);
}

void ASTDeclReader::VisitObjCMethodDecl(ObjCMethodDecl *MD) {
  VisitNamedDecl(MD);
  if (Record[Idx++]) {
    // In practice, this won't be executed (since method definitions
    // don't occur in header files).
    MD->setBody(Reader.ReadStmt(F));
    MD->setSelfDecl(ReadDeclAs<ImplicitParamDecl>(Record, Idx));
    MD->setCmdDecl(ReadDeclAs<ImplicitParamDecl>(Record, Idx));
  }
  MD->setInstanceMethod(Record[Idx++]);
  MD->setVariadic(Record[Idx++]);
  MD->setSynthesized(Record[Idx++]);
  MD->setDefined(Record[Idx++]);

  MD->IsRedeclaration = Record[Idx++];
  MD->HasRedeclaration = Record[Idx++];
  if (MD->HasRedeclaration)
    Reader.getContext().setObjCMethodRedeclaration(MD,
                                       ReadDeclAs<ObjCMethodDecl>(Record, Idx));

  MD->setDeclImplementation((ObjCMethodDecl::ImplementationControl)Record[Idx++]);
  MD->setObjCDeclQualifier((Decl::ObjCDeclQualifier)Record[Idx++]);
  MD->SetRelatedResultType(Record[Idx++]);
  MD->setResultType(Reader.readType(F, Record, Idx));
  MD->setResultTypeSourceInfo(GetTypeSourceInfo(Record, Idx));
  MD->setEndLoc(ReadSourceLocation(Record, Idx));
  unsigned NumParams = Record[Idx++];
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(ReadDeclAs<ParmVarDecl>(Record, Idx));

  MD->SelLocsKind = Record[Idx++];
  unsigned NumStoredSelLocs = Record[Idx++];
  SmallVector<SourceLocation, 16> SelLocs;
  SelLocs.reserve(NumStoredSelLocs);
  for (unsigned i = 0; i != NumStoredSelLocs; ++i)
    SelLocs.push_back(ReadSourceLocation(Record, Idx));

  MD->setParamsAndSelLocs(Reader.getContext(), Params, SelLocs);
}

void ASTDeclReader::VisitObjCContainerDecl(ObjCContainerDecl *CD) {
  VisitNamedDecl(CD);
  CD->setAtStartLoc(ReadSourceLocation(Record, Idx));
  CD->setAtEndRange(ReadSourceRange(Record, Idx));
}

void ASTDeclReader::VisitObjCInterfaceDecl(ObjCInterfaceDecl *ID) {
  VisitObjCContainerDecl(ID);
  ID->setTypeForDecl(Reader.readType(F, Record, Idx).getTypePtrOrNull());
  ID->setSuperClass(ReadDeclAs<ObjCInterfaceDecl>(Record, Idx));
  
  // Read the directly referenced protocols and their SourceLocations.
  unsigned NumProtocols = Record[Idx++];
  SmallVector<ObjCProtocolDecl *, 16> Protocols;
  Protocols.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    Protocols.push_back(ReadDeclAs<ObjCProtocolDecl>(Record, Idx));
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    ProtoLocs.push_back(ReadSourceLocation(Record, Idx));
  ID->setProtocolList(Protocols.data(), NumProtocols, ProtoLocs.data(),
                      Reader.getContext());
  
  // Read the transitive closure of protocols referenced by this class.
  NumProtocols = Record[Idx++];
  Protocols.clear();
  Protocols.reserve(NumProtocols);
  for (unsigned I = 0; I != NumProtocols; ++I)
    Protocols.push_back(ReadDeclAs<ObjCProtocolDecl>(Record, Idx));
  ID->AllReferencedProtocols.set(Protocols.data(), NumProtocols,
                                 Reader.getContext());
  
  // Read the ivars.
  unsigned NumIvars = Record[Idx++];
  SmallVector<ObjCIvarDecl *, 16> IVars;
  IVars.reserve(NumIvars);
  for (unsigned I = 0; I != NumIvars; ++I)
    IVars.push_back(ReadDeclAs<ObjCIvarDecl>(Record, Idx));
  ID->setCategoryList(ReadDeclAs<ObjCCategoryDecl>(Record, Idx));
  
  // We will rebuild this list lazily.
  ID->setIvarList(0);
  ID->setForwardDecl(Record[Idx++]);
  ID->setImplicitInterfaceDecl(Record[Idx++]);
  ID->setSuperClassLoc(ReadSourceLocation(Record, Idx));
  ID->setLocEnd(ReadSourceLocation(Record, Idx));
}

void ASTDeclReader::VisitObjCIvarDecl(ObjCIvarDecl *IVD) {
  VisitFieldDecl(IVD);
  IVD->setAccessControl((ObjCIvarDecl::AccessControl)Record[Idx++]);
  // This field will be built lazily.
  IVD->setNextIvar(0);
  bool synth = Record[Idx++];
  IVD->setSynthesize(synth);
}

void ASTDeclReader::VisitObjCProtocolDecl(ObjCProtocolDecl *PD) {
  VisitObjCContainerDecl(PD);
  PD->setForwardDecl(Record[Idx++]);
  PD->setLocEnd(ReadSourceLocation(Record, Idx));
  unsigned NumProtoRefs = Record[Idx++];
  SmallVector<ObjCProtocolDecl *, 16> ProtoRefs;
  ProtoRefs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoRefs.push_back(ReadDeclAs<ObjCProtocolDecl>(Record, Idx));
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoLocs.push_back(ReadSourceLocation(Record, Idx));
  PD->setProtocolList(ProtoRefs.data(), NumProtoRefs, ProtoLocs.data(),
                      Reader.getContext());
}

void ASTDeclReader::VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *FD) {
  VisitFieldDecl(FD);
}

void ASTDeclReader::VisitObjCClassDecl(ObjCClassDecl *CD) {
  VisitDecl(CD);
  ObjCInterfaceDecl *ClassRef = ReadDeclAs<ObjCInterfaceDecl>(Record, Idx);
  SourceLocation SLoc = ReadSourceLocation(Record, Idx);
  CD->setClass(Reader.getContext(), ClassRef, SLoc);
}

void ASTDeclReader::VisitObjCForwardProtocolDecl(ObjCForwardProtocolDecl *FPD) {
  VisitDecl(FPD);
  unsigned NumProtoRefs = Record[Idx++];
  SmallVector<ObjCProtocolDecl *, 16> ProtoRefs;
  ProtoRefs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoRefs.push_back(ReadDeclAs<ObjCProtocolDecl>(Record, Idx));
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoLocs.push_back(ReadSourceLocation(Record, Idx));
  FPD->setProtocolList(ProtoRefs.data(), NumProtoRefs, ProtoLocs.data(),
                       Reader.getContext());
}

void ASTDeclReader::VisitObjCCategoryDecl(ObjCCategoryDecl *CD) {
  VisitObjCContainerDecl(CD);
  CD->ClassInterface = ReadDeclAs<ObjCInterfaceDecl>(Record, Idx);
  unsigned NumProtoRefs = Record[Idx++];
  SmallVector<ObjCProtocolDecl *, 16> ProtoRefs;
  ProtoRefs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoRefs.push_back(ReadDeclAs<ObjCProtocolDecl>(Record, Idx));
  SmallVector<SourceLocation, 16> ProtoLocs;
  ProtoLocs.reserve(NumProtoRefs);
  for (unsigned I = 0; I != NumProtoRefs; ++I)
    ProtoLocs.push_back(ReadSourceLocation(Record, Idx));
  CD->setProtocolList(ProtoRefs.data(), NumProtoRefs, ProtoLocs.data(),
                      Reader.getContext());
  CD->NextClassCategory = ReadDeclAs<ObjCCategoryDecl>(Record, Idx);
  CD->setHasSynthBitfield(Record[Idx++]);
  CD->setCategoryNameLoc(ReadSourceLocation(Record, Idx));
}

void ASTDeclReader::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *CAD) {
  VisitNamedDecl(CAD);
  CAD->setClassInterface(ReadDeclAs<ObjCInterfaceDecl>(Record, Idx));
}

void ASTDeclReader::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  D->setAtLoc(ReadSourceLocation(Record, Idx));
  D->setType(GetTypeSourceInfo(Record, Idx));
  // FIXME: stable encoding
  D->setPropertyAttributes(
                      (ObjCPropertyDecl::PropertyAttributeKind)Record[Idx++]);
  D->setPropertyAttributesAsWritten(
                      (ObjCPropertyDecl::PropertyAttributeKind)Record[Idx++]);
  // FIXME: stable encoding
  D->setPropertyImplementation(
                            (ObjCPropertyDecl::PropertyControl)Record[Idx++]);
  D->setGetterName(Reader.ReadDeclarationName(F,Record, Idx).getObjCSelector());
  D->setSetterName(Reader.ReadDeclarationName(F,Record, Idx).getObjCSelector());
  D->setGetterMethodDecl(ReadDeclAs<ObjCMethodDecl>(Record, Idx));
  D->setSetterMethodDecl(ReadDeclAs<ObjCMethodDecl>(Record, Idx));
  D->setPropertyIvarDecl(ReadDeclAs<ObjCIvarDecl>(Record, Idx));
}

void ASTDeclReader::VisitObjCImplDecl(ObjCImplDecl *D) {
  VisitObjCContainerDecl(D);
  D->setClassInterface(ReadDeclAs<ObjCInterfaceDecl>(Record, Idx));
}

void ASTDeclReader::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  VisitObjCImplDecl(D);
  D->setIdentifier(Reader.GetIdentifierInfo(F, Record, Idx));
}

void ASTDeclReader::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
  VisitObjCImplDecl(D);
  D->setSuperClass(ReadDeclAs<ObjCInterfaceDecl>(Record, Idx));
  llvm::tie(D->IvarInitializers, D->NumIvarInitializers)
      = Reader.ReadCXXCtorInitializers(F, Record, Idx);
  D->setHasSynthBitfield(Record[Idx++]);
}


void ASTDeclReader::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  VisitDecl(D);
  D->setAtLoc(ReadSourceLocation(Record, Idx));
  D->setPropertyDecl(ReadDeclAs<ObjCPropertyDecl>(Record, Idx));
  D->PropertyIvarDecl = ReadDeclAs<ObjCIvarDecl>(Record, Idx);
  D->IvarLoc = ReadSourceLocation(Record, Idx);
  D->setGetterCXXConstructor(Reader.ReadExpr(F));
  D->setSetterCXXAssignment(Reader.ReadExpr(F));
}

void ASTDeclReader::VisitFieldDecl(FieldDecl *FD) {
  VisitDeclaratorDecl(FD);
  FD->setMutable(Record[Idx++]);
  int BitWidthOrInitializer = Record[Idx++];
  if (BitWidthOrInitializer == 1)
    FD->setBitWidth(Reader.ReadExpr(F));
  else if (BitWidthOrInitializer == 2)
    FD->setInClassInitializer(Reader.ReadExpr(F));
  if (!FD->getDeclName()) {
    if (FieldDecl *Tmpl = ReadDeclAs<FieldDecl>(Record, Idx))
      Reader.getContext().setInstantiatedFromUnnamedFieldDecl(FD, Tmpl);
  }
}

void ASTDeclReader::VisitIndirectFieldDecl(IndirectFieldDecl *FD) {
  VisitValueDecl(FD);

  FD->ChainingSize = Record[Idx++];
  assert(FD->ChainingSize >= 2 && "Anonymous chaining must be >= 2");
  FD->Chaining = new (Reader.getContext())NamedDecl*[FD->ChainingSize];

  for (unsigned I = 0; I != FD->ChainingSize; ++I)
    FD->Chaining[I] = ReadDeclAs<NamedDecl>(Record, Idx);
}

void ASTDeclReader::VisitVarDecl(VarDecl *VD) {
  VisitDeclaratorDecl(VD);
  VisitRedeclarable(VD);
  VD->VarDeclBits.SClass = (StorageClass)Record[Idx++];
  VD->VarDeclBits.SClassAsWritten = (StorageClass)Record[Idx++];
  VD->VarDeclBits.ThreadSpecified = Record[Idx++];
  VD->VarDeclBits.HasCXXDirectInit = Record[Idx++];
  VD->VarDeclBits.ExceptionVar = Record[Idx++];
  VD->VarDeclBits.NRVOVariable = Record[Idx++];
  VD->VarDeclBits.CXXForRangeDecl = Record[Idx++];
  VD->VarDeclBits.ARCPseudoStrong = Record[Idx++];
  if (Record[Idx++])
    VD->setInit(Reader.ReadExpr(F));

  if (Record[Idx++]) { // HasMemberSpecializationInfo.
    VarDecl *Tmpl = ReadDeclAs<VarDecl>(Record, Idx);
    TemplateSpecializationKind TSK = (TemplateSpecializationKind)Record[Idx++];
    SourceLocation POI = ReadSourceLocation(Record, Idx);
    Reader.getContext().setInstantiatedFromStaticDataMember(VD, Tmpl, TSK,POI);
  }
}

void ASTDeclReader::VisitImplicitParamDecl(ImplicitParamDecl *PD) {
  VisitVarDecl(PD);
}

void ASTDeclReader::VisitParmVarDecl(ParmVarDecl *PD) {
  VisitVarDecl(PD);
  unsigned isObjCMethodParam = Record[Idx++];
  unsigned scopeDepth = Record[Idx++];
  unsigned scopeIndex = Record[Idx++];
  unsigned declQualifier = Record[Idx++];
  if (isObjCMethodParam) {
    assert(scopeDepth == 0);
    PD->setObjCMethodScopeInfo(scopeIndex);
    PD->ParmVarDeclBits.ScopeDepthOrObjCQuals = declQualifier;
  } else {
    PD->setScopeInfo(scopeDepth, scopeIndex);
  }
  PD->ParmVarDeclBits.IsKNRPromoted = Record[Idx++];
  PD->ParmVarDeclBits.HasInheritedDefaultArg = Record[Idx++];
  if (Record[Idx++]) // hasUninstantiatedDefaultArg.
    PD->setUninstantiatedDefaultArg(Reader.ReadExpr(F));
}

void ASTDeclReader::VisitFileScopeAsmDecl(FileScopeAsmDecl *AD) {
  VisitDecl(AD);
  AD->setAsmString(cast<StringLiteral>(Reader.ReadExpr(F)));
  AD->setRParenLoc(ReadSourceLocation(Record, Idx));
}

void ASTDeclReader::VisitBlockDecl(BlockDecl *BD) {
  VisitDecl(BD);
  BD->setBody(cast_or_null<CompoundStmt>(Reader.ReadStmt(F)));
  BD->setSignatureAsWritten(GetTypeSourceInfo(Record, Idx));
  unsigned NumParams = Record[Idx++];
  SmallVector<ParmVarDecl *, 16> Params;
  Params.reserve(NumParams);
  for (unsigned I = 0; I != NumParams; ++I)
    Params.push_back(ReadDeclAs<ParmVarDecl>(Record, Idx));
  BD->setParams(Params);

  bool capturesCXXThis = Record[Idx++];
  unsigned numCaptures = Record[Idx++];
  SmallVector<BlockDecl::Capture, 16> captures;
  captures.reserve(numCaptures);
  for (unsigned i = 0; i != numCaptures; ++i) {
    VarDecl *decl = ReadDeclAs<VarDecl>(Record, Idx);
    unsigned flags = Record[Idx++];
    bool byRef = (flags & 1);
    bool nested = (flags & 2);
    Expr *copyExpr = ((flags & 4) ? Reader.ReadExpr(F) : 0);

    captures.push_back(BlockDecl::Capture(decl, byRef, nested, copyExpr));
  }
  BD->setCaptures(Reader.getContext(), captures.begin(),
                  captures.end(), capturesCXXThis);
}

void ASTDeclReader::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  VisitDecl(D);
  D->setLanguage((LinkageSpecDecl::LanguageIDs)Record[Idx++]);
  D->setExternLoc(ReadSourceLocation(Record, Idx));
  D->setRBraceLoc(ReadSourceLocation(Record, Idx));
}

void ASTDeclReader::VisitLabelDecl(LabelDecl *D) {
  VisitNamedDecl(D);
  D->setLocStart(ReadSourceLocation(Record, Idx));
}


void ASTDeclReader::VisitNamespaceDecl(NamespaceDecl *D) {
  VisitNamedDecl(D);
  D->IsInline = Record[Idx++];
  D->LocStart = ReadSourceLocation(Record, Idx);
  D->RBraceLoc = ReadSourceLocation(Record, Idx);
  D->NextNamespace = Record[Idx++];

  bool IsOriginal = Record[Idx++];
  // FIXME: Modules will likely have trouble with pointing directly at
  // the original namespace.
  D->OrigOrAnonNamespace.setInt(IsOriginal);
  D->OrigOrAnonNamespace.setPointer(ReadDeclAs<NamespaceDecl>(Record, Idx));
}

void ASTDeclReader::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  VisitNamedDecl(D);
  D->NamespaceLoc = ReadSourceLocation(Record, Idx);
  D->IdentLoc = ReadSourceLocation(Record, Idx);
  D->QualifierLoc = Reader.ReadNestedNameSpecifierLoc(F, Record, Idx);
  D->Namespace = ReadDeclAs<NamedDecl>(Record, Idx);
}

void ASTDeclReader::VisitUsingDecl(UsingDecl *D) {
  VisitNamedDecl(D);
  D->setUsingLocation(ReadSourceLocation(Record, Idx));
  D->QualifierLoc = Reader.ReadNestedNameSpecifierLoc(F, Record, Idx);
  ReadDeclarationNameLoc(D->DNLoc, D->getDeclName(), Record, Idx);
  D->FirstUsingShadow = ReadDeclAs<UsingShadowDecl>(Record, Idx);
  D->setTypeName(Record[Idx++]);
  if (NamedDecl *Pattern = ReadDeclAs<NamedDecl>(Record, Idx))
    Reader.getContext().setInstantiatedFromUsingDecl(D, Pattern);
}

void ASTDeclReader::VisitUsingShadowDecl(UsingShadowDecl *D) {
  VisitNamedDecl(D);
  D->setTargetDecl(ReadDeclAs<NamedDecl>(Record, Idx));
  D->UsingOrNextShadow = ReadDeclAs<NamedDecl>(Record, Idx);
  UsingShadowDecl *Pattern = ReadDeclAs<UsingShadowDecl>(Record, Idx);
  if (Pattern)
    Reader.getContext().setInstantiatedFromUsingShadowDecl(D, Pattern);
}

void ASTDeclReader::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  VisitNamedDecl(D);
  D->UsingLoc = ReadSourceLocation(Record, Idx);
  D->NamespaceLoc = ReadSourceLocation(Record, Idx);
  D->QualifierLoc = Reader.ReadNestedNameSpecifierLoc(F, Record, Idx);
  D->NominatedNamespace = ReadDeclAs<NamedDecl>(Record, Idx);
  D->CommonAncestor = ReadDeclAs<DeclContext>(Record, Idx);
}

void ASTDeclReader::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  VisitValueDecl(D);
  D->setUsingLoc(ReadSourceLocation(Record, Idx));
  D->QualifierLoc = Reader.ReadNestedNameSpecifierLoc(F, Record, Idx);
  ReadDeclarationNameLoc(D->DNLoc, D->getDeclName(), Record, Idx);
}

void ASTDeclReader::VisitUnresolvedUsingTypenameDecl(
                                               UnresolvedUsingTypenameDecl *D) {
  VisitTypeDecl(D);
  D->TypenameLocation = ReadSourceLocation(Record, Idx);
  D->QualifierLoc = Reader.ReadNestedNameSpecifierLoc(F, Record, Idx);
}

void ASTDeclReader::ReadCXXDefinitionData(
                                   struct CXXRecordDecl::DefinitionData &Data,
                                   const RecordData &Record, unsigned &Idx) {
  Data.UserDeclaredConstructor = Record[Idx++];
  Data.UserDeclaredCopyConstructor = Record[Idx++];
  Data.UserDeclaredMoveConstructor = Record[Idx++];
  Data.UserDeclaredCopyAssignment = Record[Idx++];
  Data.UserDeclaredMoveAssignment = Record[Idx++];
  Data.UserDeclaredDestructor = Record[Idx++];
  Data.Aggregate = Record[Idx++];
  Data.PlainOldData = Record[Idx++];
  Data.Empty = Record[Idx++];
  Data.Polymorphic = Record[Idx++];
  Data.Abstract = Record[Idx++];
  Data.IsStandardLayout = Record[Idx++];
  Data.HasNoNonEmptyBases = Record[Idx++];
  Data.HasPrivateFields = Record[Idx++];
  Data.HasProtectedFields = Record[Idx++];
  Data.HasPublicFields = Record[Idx++];
  Data.HasMutableFields = Record[Idx++];
  Data.HasTrivialDefaultConstructor = Record[Idx++];
  Data.HasConstexprNonCopyMoveConstructor = Record[Idx++];
  Data.HasTrivialCopyConstructor = Record[Idx++];
  Data.HasTrivialMoveConstructor = Record[Idx++];
  Data.HasTrivialCopyAssignment = Record[Idx++];
  Data.HasTrivialMoveAssignment = Record[Idx++];
  Data.HasTrivialDestructor = Record[Idx++];
  Data.HasNonLiteralTypeFieldsOrBases = Record[Idx++];
  Data.ComputedVisibleConversions = Record[Idx++];
  Data.UserProvidedDefaultConstructor = Record[Idx++];
  Data.DeclaredDefaultConstructor = Record[Idx++];
  Data.DeclaredCopyConstructor = Record[Idx++];
  Data.DeclaredMoveConstructor = Record[Idx++];
  Data.DeclaredCopyAssignment = Record[Idx++];
  Data.DeclaredMoveAssignment = Record[Idx++];
  Data.DeclaredDestructor = Record[Idx++];
  Data.FailedImplicitMoveConstructor = Record[Idx++];
  Data.FailedImplicitMoveAssignment = Record[Idx++];

  Data.NumBases = Record[Idx++];
  if (Data.NumBases)
    Data.Bases = Reader.readCXXBaseSpecifiers(F, Record, Idx);
  Data.NumVBases = Record[Idx++];
  if (Data.NumVBases)
    Data.VBases = Reader.readCXXBaseSpecifiers(F, Record, Idx);
  
  Reader.ReadUnresolvedSet(F, Data.Conversions, Record, Idx);
  Reader.ReadUnresolvedSet(F, Data.VisibleConversions, Record, Idx);
  assert(Data.Definition && "Data.Definition should be already set!");
  Data.FirstFriend = ReadDeclAs<FriendDecl>(Record, Idx);
}

void ASTDeclReader::InitializeCXXDefinitionData(CXXRecordDecl *D,
                                                CXXRecordDecl *DefinitionDecl,
                                                const RecordData &Record,
                                                unsigned &Idx) {
  ASTContext &C = Reader.getContext();

  if (D == DefinitionDecl) {
    D->DefinitionData = new (C) struct CXXRecordDecl::DefinitionData(D);
    ReadCXXDefinitionData(*D->DefinitionData, Record, Idx);
    // We read the definition info. Check if there are pending forward
    // references that need to point to this DefinitionData pointer.
    ASTReader::PendingForwardRefsMap::iterator
        FindI = Reader.PendingForwardRefs.find(D);
    if (FindI != Reader.PendingForwardRefs.end()) {
      ASTReader::ForwardRefs &Refs = FindI->second;
      for (ASTReader::ForwardRefs::iterator
             I = Refs.begin(), E = Refs.end(); I != E; ++I)
        (*I)->DefinitionData = D->DefinitionData;
#ifndef NDEBUG
      // We later check whether PendingForwardRefs is empty to make sure all
      // pending references were linked.
      Reader.PendingForwardRefs.erase(D);
#endif
    }
  } else if (DefinitionDecl) {
    if (DefinitionDecl->DefinitionData) {
      D->DefinitionData = DefinitionDecl->DefinitionData;
    } else {
      // The definition is still initializing.
      Reader.PendingForwardRefs[DefinitionDecl].push_back(D);
    }
  }
}

void ASTDeclReader::VisitCXXRecordDecl(CXXRecordDecl *D) {
  VisitRecordDecl(D);

  CXXRecordDecl *DefinitionDecl = ReadDeclAs<CXXRecordDecl>(Record, Idx);
  InitializeCXXDefinitionData(D, DefinitionDecl, Record, Idx);

  ASTContext &C = Reader.getContext();

  enum CXXRecKind {
    CXXRecNotTemplate = 0, CXXRecTemplate, CXXRecMemberSpecialization
  };
  switch ((CXXRecKind)Record[Idx++]) {
  default:
    llvm_unreachable("Out of sync with ASTDeclWriter::VisitCXXRecordDecl?");
  case CXXRecNotTemplate:
    break;
  case CXXRecTemplate:
    D->TemplateOrInstantiation = ReadDeclAs<ClassTemplateDecl>(Record, Idx);
    break;
  case CXXRecMemberSpecialization: {
    CXXRecordDecl *RD = ReadDeclAs<CXXRecordDecl>(Record, Idx);
    TemplateSpecializationKind TSK = (TemplateSpecializationKind)Record[Idx++];
    SourceLocation POI = ReadSourceLocation(Record, Idx);
    MemberSpecializationInfo *MSI = new (C) MemberSpecializationInfo(RD, TSK);
    MSI->setPointOfInstantiation(POI);
    D->TemplateOrInstantiation = MSI;
    break;
  }
  }

  // Load the key function to avoid deserializing every method so we can
  // compute it.
  if (D->IsCompleteDefinition) {
    if (CXXMethodDecl *Key = ReadDeclAs<CXXMethodDecl>(Record, Idx))
      C.KeyFunctions[D] = Key;
  }
}

void ASTDeclReader::VisitCXXMethodDecl(CXXMethodDecl *D) {
  VisitFunctionDecl(D);
  unsigned NumOverridenMethods = Record[Idx++];
  while (NumOverridenMethods--) {
    // Avoid invariant checking of CXXMethodDecl::addOverriddenMethod,
    // MD may be initializing.
    if (CXXMethodDecl *MD = ReadDeclAs<CXXMethodDecl>(Record, Idx))
      Reader.getContext().addOverriddenMethod(D, MD);
  }
}

void ASTDeclReader::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  VisitCXXMethodDecl(D);
  
  D->IsExplicitSpecified = Record[Idx++];
  D->ImplicitlyDefined = Record[Idx++];
  llvm::tie(D->CtorInitializers, D->NumCtorInitializers)
      = Reader.ReadCXXCtorInitializers(F, Record, Idx);
}

void ASTDeclReader::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  VisitCXXMethodDecl(D);

  D->ImplicitlyDefined = Record[Idx++];
  D->OperatorDelete = ReadDeclAs<FunctionDecl>(Record, Idx);
}

void ASTDeclReader::VisitCXXConversionDecl(CXXConversionDecl *D) {
  VisitCXXMethodDecl(D);
  D->IsExplicitSpecified = Record[Idx++];
}

void ASTDeclReader::VisitAccessSpecDecl(AccessSpecDecl *D) {
  VisitDecl(D);
  D->setColonLoc(ReadSourceLocation(Record, Idx));
}

void ASTDeclReader::VisitFriendDecl(FriendDecl *D) {
  VisitDecl(D);
  if (Record[Idx++])
    D->Friend = GetTypeSourceInfo(Record, Idx);
  else
    D->Friend = ReadDeclAs<NamedDecl>(Record, Idx);
  D->NextFriend = Record[Idx++];
  D->UnsupportedFriend = (Record[Idx++] != 0);
  D->FriendLoc = ReadSourceLocation(Record, Idx);
}

void ASTDeclReader::VisitFriendTemplateDecl(FriendTemplateDecl *D) {
  VisitDecl(D);
  unsigned NumParams = Record[Idx++];
  D->NumParams = NumParams;
  D->Params = new TemplateParameterList*[NumParams];
  for (unsigned i = 0; i != NumParams; ++i)
    D->Params[i] = Reader.ReadTemplateParameterList(F, Record, Idx);
  if (Record[Idx++]) // HasFriendDecl
    D->Friend = ReadDeclAs<NamedDecl>(Record, Idx);
  else
    D->Friend = GetTypeSourceInfo(Record, Idx);
  D->FriendLoc = ReadSourceLocation(Record, Idx);
}

void ASTDeclReader::VisitTemplateDecl(TemplateDecl *D) {
  VisitNamedDecl(D);

  NamedDecl *TemplatedDecl = ReadDeclAs<NamedDecl>(Record, Idx);
  TemplateParameterList* TemplateParams
      = Reader.ReadTemplateParameterList(F, Record, Idx); 
  D->init(TemplatedDecl, TemplateParams);
}

void ASTDeclReader::VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D) {
  // Initialize CommonOrPrev before VisitTemplateDecl so that getCommonPtr()
  // can be used while this is still initializing.

  assert(D->CommonOrPrev.isNull() && "getCommonPtr was called earlier on this");
  DeclID PreviousDeclID = ReadDeclID(Record, Idx);
  DeclID FirstDeclID =  PreviousDeclID ? ReadDeclID(Record, Idx) : 0;
  // We delay loading of the redeclaration chain to avoid deeply nested calls.
  // We temporarily set the first (canonical) declaration as the previous one
  // which is the one that matters and mark the real previous DeclID to be
  // loaded & attached later on.
  RedeclarableTemplateDecl *FirstDecl =
      cast_or_null<RedeclarableTemplateDecl>(Reader.GetDecl(FirstDeclID));
  assert((FirstDecl == 0 || FirstDecl->getKind() == D->getKind()) &&
         "FirstDecl kind mismatch");
  if (FirstDecl) {
    D->CommonOrPrev = FirstDecl;
    // Mark the real previous DeclID to be loaded & attached later on.
    if (PreviousDeclID != FirstDeclID)
      Reader.PendingPreviousDecls.push_back(std::make_pair(D, PreviousDeclID));
  } else {
    D->CommonOrPrev = D->newCommon(Reader.getContext());
    if (RedeclarableTemplateDecl *RTD
          = ReadDeclAs<RedeclarableTemplateDecl>(Record, Idx)) {
      assert(RTD->getKind() == D->getKind() &&
             "InstantiatedFromMemberTemplate kind mismatch");
      D->setInstantiatedFromMemberTemplateImpl(RTD);
      if (Record[Idx++])
        D->setMemberSpecialization();
    }

    RedeclarableTemplateDecl *LatestDecl
      = ReadDeclAs<RedeclarableTemplateDecl>(Record, Idx);
  
    // This decl is a first one and the latest declaration that it points to is
    // in the same AST file. However, if this actually needs to point to a
    // redeclaration in another AST file, we need to update it by checking
    // the FirstLatestDeclIDs map which tracks this kind of decls.
    assert(Reader.GetDecl(ThisDeclID) == D && "Invalid ThisDeclID ?");
    ASTReader::FirstLatestDeclIDMap::iterator I
        = Reader.FirstLatestDeclIDs.find(ThisDeclID);
    if (I != Reader.FirstLatestDeclIDs.end()) {
      if (Decl *NewLatest = Reader.GetDecl(I->second))
        LatestDecl = cast<RedeclarableTemplateDecl>(NewLatest);
    }

    assert(LatestDecl->getKind() == D->getKind() && "Latest kind mismatch");
    D->getCommonPtr()->Latest = LatestDecl;
  }

  VisitTemplateDecl(D);
  D->IdentifierNamespace = Record[Idx++];
}

void ASTDeclReader::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->getPreviousDeclaration() == 0) {
    // This ClassTemplateDecl owns a CommonPtr; read it to keep track of all of
    // the specializations.
    SmallVector<serialization::DeclID, 2> SpecIDs;
    SpecIDs.push_back(0);
    
    // Specializations.
    unsigned Size = Record[Idx++];
    SpecIDs[0] += Size;
    for (unsigned I = 0; I != Size; ++I)
      SpecIDs.push_back(ReadDeclID(Record, Idx));

    // Partial specializations.
    Size = Record[Idx++];
    SpecIDs[0] += Size;
    for (unsigned I = 0; I != Size; ++I)
      SpecIDs.push_back(ReadDeclID(Record, Idx));

    if (SpecIDs[0]) {
      typedef serialization::DeclID DeclID;
      
      ClassTemplateDecl::Common *CommonPtr = D->getCommonPtr();
      CommonPtr->LazySpecializations
        = new (Reader.getContext()) DeclID [SpecIDs.size()];
      memcpy(CommonPtr->LazySpecializations, SpecIDs.data(), 
             SpecIDs.size() * sizeof(DeclID));
    }
    
    // InjectedClassNameType is computed.
  }
}

void ASTDeclReader::VisitClassTemplateSpecializationDecl(
                                           ClassTemplateSpecializationDecl *D) {
  VisitCXXRecordDecl(D);
  
  ASTContext &C = Reader.getContext();
  if (Decl *InstD = ReadDecl(Record, Idx)) {
    if (ClassTemplateDecl *CTD = dyn_cast<ClassTemplateDecl>(InstD)) {
      D->SpecializedTemplate = CTD;
    } else {
      SmallVector<TemplateArgument, 8> TemplArgs;
      Reader.ReadTemplateArgumentList(TemplArgs, F, Record, Idx);
      TemplateArgumentList *ArgList
        = TemplateArgumentList::CreateCopy(C, TemplArgs.data(), 
                                           TemplArgs.size());
      ClassTemplateSpecializationDecl::SpecializedPartialSpecialization *PS
          = new (C) ClassTemplateSpecializationDecl::
                                             SpecializedPartialSpecialization();
      PS->PartialSpecialization
          = cast<ClassTemplatePartialSpecializationDecl>(InstD);
      PS->TemplateArgs = ArgList;
      D->SpecializedTemplate = PS;
    }
  }

  // Explicit info.
  if (TypeSourceInfo *TyInfo = GetTypeSourceInfo(Record, Idx)) {
    ClassTemplateSpecializationDecl::ExplicitSpecializationInfo *ExplicitInfo
        = new (C) ClassTemplateSpecializationDecl::ExplicitSpecializationInfo;
    ExplicitInfo->TypeAsWritten = TyInfo;
    ExplicitInfo->ExternLoc = ReadSourceLocation(Record, Idx);
    ExplicitInfo->TemplateKeywordLoc = ReadSourceLocation(Record, Idx);
    D->ExplicitInfo = ExplicitInfo;
  }

  SmallVector<TemplateArgument, 8> TemplArgs;
  Reader.ReadTemplateArgumentList(TemplArgs, F, Record, Idx);
  D->TemplateArgs = TemplateArgumentList::CreateCopy(C, TemplArgs.data(), 
                                                     TemplArgs.size());
  D->PointOfInstantiation = ReadSourceLocation(Record, Idx);
  D->SpecializationKind = (TemplateSpecializationKind)Record[Idx++];
  
  if (D->isCanonicalDecl()) { // It's kept in the folding set.
    ClassTemplateDecl *CanonPattern = ReadDeclAs<ClassTemplateDecl>(Record,Idx);
    if (ClassTemplatePartialSpecializationDecl *Partial
                       = dyn_cast<ClassTemplatePartialSpecializationDecl>(D)) {
      CanonPattern->getCommonPtr()->PartialSpecializations.InsertNode(Partial);
    } else {
      CanonPattern->getCommonPtr()->Specializations.InsertNode(D);
    }
  }
}

void ASTDeclReader::VisitClassTemplatePartialSpecializationDecl(
                                    ClassTemplatePartialSpecializationDecl *D) {
  VisitClassTemplateSpecializationDecl(D);

  ASTContext &C = Reader.getContext();
  D->TemplateParams = Reader.ReadTemplateParameterList(F, Record, Idx);

  unsigned NumArgs = Record[Idx++];
  if (NumArgs) {
    D->NumArgsAsWritten = NumArgs;
    D->ArgsAsWritten = new (C) TemplateArgumentLoc[NumArgs];
    for (unsigned i=0; i != NumArgs; ++i)
      D->ArgsAsWritten[i] = Reader.ReadTemplateArgumentLoc(F, Record, Idx);
  }

  D->SequenceNumber = Record[Idx++];

  // These are read/set from/to the first declaration.
  if (D->getPreviousDeclaration() == 0) {
    D->InstantiatedFromMember.setPointer(
      ReadDeclAs<ClassTemplatePartialSpecializationDecl>(Record, Idx));
    D->InstantiatedFromMember.setInt(Record[Idx++]);
  }
}

void ASTDeclReader::VisitClassScopeFunctionSpecializationDecl(
                                    ClassScopeFunctionSpecializationDecl *D) {
  VisitDecl(D);
  D->Specialization = ReadDeclAs<CXXMethodDecl>(Record, Idx);
}

void ASTDeclReader::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->getPreviousDeclaration() == 0) {
    // This FunctionTemplateDecl owns a CommonPtr; read it.

    // Read the function specialization declarations.
    // FunctionTemplateDecl's FunctionTemplateSpecializationInfos are filled
    // when reading the specialized FunctionDecl.
    unsigned NumSpecs = Record[Idx++];
    while (NumSpecs--)
      (void)ReadDecl(Record, Idx);
  }
}

void ASTDeclReader::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  VisitTypeDecl(D);

  D->setDeclaredWithTypename(Record[Idx++]);

  bool Inherited = Record[Idx++];
  TypeSourceInfo *DefArg = GetTypeSourceInfo(Record, Idx);
  D->setDefaultArgument(DefArg, Inherited);
}

void ASTDeclReader::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
  VisitDeclaratorDecl(D);
  // TemplateParmPosition.
  D->setDepth(Record[Idx++]);
  D->setPosition(Record[Idx++]);
  if (D->isExpandedParameterPack()) {
    void **Data = reinterpret_cast<void **>(D + 1);
    for (unsigned I = 0, N = D->getNumExpansionTypes(); I != N; ++I) {
      Data[2*I] = Reader.readType(F, Record, Idx).getAsOpaquePtr();
      Data[2*I + 1] = GetTypeSourceInfo(Record, Idx);
    }
  } else {
    // Rest of NonTypeTemplateParmDecl.
    D->ParameterPack = Record[Idx++];
    if (Record[Idx++]) {
      Expr *DefArg = Reader.ReadExpr(F);
      bool Inherited = Record[Idx++];
      D->setDefaultArgument(DefArg, Inherited);
   }
  }
}

void ASTDeclReader::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  VisitTemplateDecl(D);
  // TemplateParmPosition.
  D->setDepth(Record[Idx++]);
  D->setPosition(Record[Idx++]);
  // Rest of TemplateTemplateParmDecl.
  TemplateArgumentLoc Arg = Reader.ReadTemplateArgumentLoc(F, Record, Idx);
  bool IsInherited = Record[Idx++];
  D->setDefaultArgument(Arg, IsInherited);
  D->ParameterPack = Record[Idx++];
}

void ASTDeclReader::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);
}

void ASTDeclReader::VisitStaticAssertDecl(StaticAssertDecl *D) {
  VisitDecl(D);
  D->AssertExpr = Reader.ReadExpr(F);
  D->Message = cast<StringLiteral>(Reader.ReadExpr(F));
  D->RParenLoc = ReadSourceLocation(Record, Idx);
}

std::pair<uint64_t, uint64_t>
ASTDeclReader::VisitDeclContext(DeclContext *DC) {
  uint64_t LexicalOffset = Record[Idx++];
  uint64_t VisibleOffset = Record[Idx++];
  return std::make_pair(LexicalOffset, VisibleOffset);
}

template <typename T>
void ASTDeclReader::VisitRedeclarable(Redeclarable<T> *D) {
  enum RedeclKind { NoRedeclaration = 0, PointsToPrevious, PointsToLatest };
  RedeclKind Kind = (RedeclKind)Record[Idx++];
  switch (Kind) {
  default:
    llvm_unreachable("Out of sync with ASTDeclWriter::VisitRedeclarable or"
                     " messed up reading");
  case NoRedeclaration:
    break;
  case PointsToPrevious: {
    DeclID PreviousDeclID = ReadDeclID(Record, Idx);
    DeclID FirstDeclID = ReadDeclID(Record, Idx);
    // We delay loading of the redeclaration chain to avoid deeply nested calls.
    // We temporarily set the first (canonical) declaration as the previous one
    // which is the one that matters and mark the real previous DeclID to be
    // loaded & attached later on.
    D->RedeclLink = typename Redeclarable<T>::PreviousDeclLink(
                                cast_or_null<T>(Reader.GetDecl(FirstDeclID)));
    if (PreviousDeclID != FirstDeclID)
      Reader.PendingPreviousDecls.push_back(std::make_pair(static_cast<T*>(D),
                                                           PreviousDeclID));
    break;
  }
  case PointsToLatest:
    D->RedeclLink = typename Redeclarable<T>::LatestDeclLink(
                                                   ReadDeclAs<T>(Record, Idx));
    break;
  }

  assert(!(Kind == PointsToPrevious &&
           Reader.FirstLatestDeclIDs.find(ThisDeclID) !=
               Reader.FirstLatestDeclIDs.end()) &&
         "This decl is not first, it should not be in the map");
  if (Kind == PointsToPrevious)
    return;

  // This decl is a first one and the latest declaration that it points to is in
  // the same AST file. However, if this actually needs to point to a
  // redeclaration in another AST file, we need to update it by checking the
  // FirstLatestDeclIDs map which tracks this kind of decls.
  assert(Reader.GetDecl(ThisDeclID) == static_cast<T*>(D) &&
         "Invalid ThisDeclID ?");
  ASTReader::FirstLatestDeclIDMap::iterator I
      = Reader.FirstLatestDeclIDs.find(ThisDeclID);
  if (I != Reader.FirstLatestDeclIDs.end()) {
    Decl *NewLatest = Reader.GetDecl(I->second);
    D->RedeclLink
        = typename Redeclarable<T>::LatestDeclLink(cast_or_null<T>(NewLatest));
  }
}

//===----------------------------------------------------------------------===//
// Attribute Reading
//===----------------------------------------------------------------------===//

/// \brief Reads attributes from the current stream position.
void ASTReader::ReadAttributes(Module &F, AttrVec &Attrs,
                               const RecordData &Record, unsigned &Idx) {
  for (unsigned i = 0, e = Record[Idx++]; i != e; ++i) {
    Attr *New = 0;
    attr::Kind Kind = (attr::Kind)Record[Idx++];
    SourceRange Range = ReadSourceRange(F, Record, Idx);

#include "clang/Serialization/AttrPCHRead.inc"

    assert(New && "Unable to decode attribute?");
    Attrs.push_back(New);
  }
}

//===----------------------------------------------------------------------===//
// ASTReader Implementation
//===----------------------------------------------------------------------===//

/// \brief Note that we have loaded the declaration with the given
/// Index.
///
/// This routine notes that this declaration has already been loaded,
/// so that future GetDecl calls will return this declaration rather
/// than trying to load a new declaration.
inline void ASTReader::LoadedDecl(unsigned Index, Decl *D) {
  assert(!DeclsLoaded[Index] && "Decl loaded twice?");
  DeclsLoaded[Index] = D;
}


/// \brief Determine whether the consumer will be interested in seeing
/// this declaration (via HandleTopLevelDecl).
///
/// This routine should return true for anything that might affect
/// code generation, e.g., inline function definitions, Objective-C
/// declarations with metadata, etc.
static bool isConsumerInterestedIn(Decl *D) {
  // An ObjCMethodDecl is never considered as "interesting" because its
  // implementation container always is.

  if (isa<FileScopeAsmDecl>(D) || 
      isa<ObjCProtocolDecl>(D) || 
      isa<ObjCImplDecl>(D))
    return true;
  if (VarDecl *Var = dyn_cast<VarDecl>(D))
    return Var->isFileVarDecl() &&
           Var->isThisDeclarationADefinition() == VarDecl::Definition;
  if (FunctionDecl *Func = dyn_cast<FunctionDecl>(D))
    return Func->doesThisDeclarationHaveABody();
  
  return false;
}

/// \brief Get the correct cursor and offset for loading a declaration.
ASTReader::RecordLocation
ASTReader::DeclCursorForID(DeclID ID) {
  // See if there's an override.
  DeclReplacementMap::iterator It = ReplacedDecls.find(ID);
  if (It != ReplacedDecls.end())
    return RecordLocation(It->second.first, It->second.second);

  GlobalDeclMapType::iterator I = GlobalDeclMap.find(ID);
  assert(I != GlobalDeclMap.end() && "Corrupted global declaration map");
  Module *M = I->second;
  return RecordLocation(M, 
           M->DeclOffsets[ID - M->BaseDeclID - NUM_PREDEF_DECL_IDS]);
}

ASTReader::RecordLocation ASTReader::getLocalBitOffset(uint64_t GlobalOffset) {
  ContinuousRangeMap<uint64_t, Module*, 4>::iterator I
    = GlobalBitOffsetsMap.find(GlobalOffset);

  assert(I != GlobalBitOffsetsMap.end() && "Corrupted global bit offsets map");
  return RecordLocation(I->second, GlobalOffset - I->second->GlobalBitOffset);
}

uint64_t ASTReader::getGlobalBitOffset(Module &M, uint32_t LocalOffset) {
  return LocalOffset + M.GlobalBitOffset;
}

void ASTDeclReader::attachPreviousDecl(Decl *D, Decl *previous) {
  assert(D && previous);
  if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
    TD->RedeclLink.setPointer(cast<TagDecl>(previous));
  } else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    FD->RedeclLink.setPointer(cast<FunctionDecl>(previous));
  } else if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
    VD->RedeclLink.setPointer(cast<VarDecl>(previous));
  } else {
    RedeclarableTemplateDecl *TD = cast<RedeclarableTemplateDecl>(D);
    TD->CommonOrPrev = cast<RedeclarableTemplateDecl>(previous);
  }
}

void ASTReader::loadAndAttachPreviousDecl(Decl *D, serialization::DeclID ID) {
  Decl *previous = GetDecl(ID);
  ASTDeclReader::attachPreviousDecl(D, previous);
}

/// \brief Read the declaration at the given offset from the AST file.
Decl *ASTReader::ReadDeclRecord(DeclID ID) {
  unsigned Index = ID - NUM_PREDEF_DECL_IDS;
  RecordLocation Loc = DeclCursorForID(ID);
  llvm::BitstreamCursor &DeclsCursor = Loc.F->DeclsCursor;
  // Keep track of where we are in the stream, then jump back there
  // after reading this declaration.
  SavedStreamPosition SavedPosition(DeclsCursor);

  ReadingKindTracker ReadingKind(Read_Decl, *this);

  // Note that we are loading a declaration record.
  Deserializing ADecl(this);

  DeclsCursor.JumpToBit(Loc.Offset);
  RecordData Record;
  unsigned Code = DeclsCursor.ReadCode();
  unsigned Idx = 0;
  ASTDeclReader Reader(*this, *Loc.F, DeclsCursor, ID, Record, Idx);

  Decl *D = 0;
  switch ((DeclCode)DeclsCursor.ReadRecord(Code, Record)) {
  case DECL_CONTEXT_LEXICAL:
  case DECL_CONTEXT_VISIBLE:
    llvm_unreachable("Record cannot be de-serialized with ReadDeclRecord");
  case DECL_TYPEDEF:
    D = TypedefDecl::Create(Context, 0, SourceLocation(), SourceLocation(),
                            0, 0);
    break;
  case DECL_TYPEALIAS:
    D = TypeAliasDecl::Create(Context, 0, SourceLocation(), SourceLocation(),
                              0, 0);
    break;
  case DECL_ENUM:
    D = EnumDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_RECORD:
    D = RecordDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_ENUM_CONSTANT:
    D = EnumConstantDecl::Create(Context, 0, SourceLocation(), 0, QualType(),
                                 0, llvm::APSInt());
    break;
  case DECL_FUNCTION:
    D = FunctionDecl::Create(Context, 0, SourceLocation(), SourceLocation(),
                             DeclarationName(), QualType(), 0);
    break;
  case DECL_LINKAGE_SPEC:
    D = LinkageSpecDecl::Create(Context, 0, SourceLocation(), SourceLocation(),
                                (LinkageSpecDecl::LanguageIDs)0,
                                SourceLocation());
    break;
  case DECL_LABEL:
    D = LabelDecl::Create(Context, 0, SourceLocation(), 0);
    break;
  case DECL_NAMESPACE:
    D = NamespaceDecl::Create(Context, 0, SourceLocation(),
                              SourceLocation(), 0);
    break;
  case DECL_NAMESPACE_ALIAS:
    D = NamespaceAliasDecl::Create(Context, 0, SourceLocation(),
                                   SourceLocation(), 0, 
                                   NestedNameSpecifierLoc(),
                                   SourceLocation(), 0);
    break;
  case DECL_USING:
    D = UsingDecl::Create(Context, 0, SourceLocation(),
                          NestedNameSpecifierLoc(), DeclarationNameInfo(), 
                          false);
    break;
  case DECL_USING_SHADOW:
    D = UsingShadowDecl::Create(Context, 0, SourceLocation(), 0, 0);
    break;
  case DECL_USING_DIRECTIVE:
    D = UsingDirectiveDecl::Create(Context, 0, SourceLocation(),
                                   SourceLocation(), NestedNameSpecifierLoc(),
                                   SourceLocation(), 0, 0);
    break;
  case DECL_UNRESOLVED_USING_VALUE:
    D = UnresolvedUsingValueDecl::Create(Context, 0, SourceLocation(),
                                         NestedNameSpecifierLoc(), 
                                         DeclarationNameInfo());
    break;
  case DECL_UNRESOLVED_USING_TYPENAME:
    D = UnresolvedUsingTypenameDecl::Create(Context, 0, SourceLocation(),
                                            SourceLocation(), 
                                            NestedNameSpecifierLoc(),
                                            SourceLocation(),
                                            DeclarationName());
    break;
  case DECL_CXX_RECORD:
    D = CXXRecordDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CXX_METHOD:
    D = CXXMethodDecl::Create(Context, 0, SourceLocation(),
                              DeclarationNameInfo(), QualType(), 0,
                              false, SC_None, false, false, SourceLocation());
    break;
  case DECL_CXX_CONSTRUCTOR:
    D = CXXConstructorDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CXX_DESTRUCTOR:
    D = CXXDestructorDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CXX_CONVERSION:
    D = CXXConversionDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_ACCESS_SPEC:
    D = AccessSpecDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_FRIEND:
    D = FriendDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_FRIEND_TEMPLATE:
    D = FriendTemplateDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CLASS_TEMPLATE:
    D = ClassTemplateDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CLASS_TEMPLATE_SPECIALIZATION:
    D = ClassTemplateSpecializationDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION:
    D = ClassTemplatePartialSpecializationDecl::Create(Context,
                                                       Decl::EmptyShell());
    break;
  case DECL_CLASS_SCOPE_FUNCTION_SPECIALIZATION:
    D = ClassScopeFunctionSpecializationDecl::Create(Context,
                                                     Decl::EmptyShell());
    break;
  case DECL_FUNCTION_TEMPLATE:
      D = FunctionTemplateDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_TEMPLATE_TYPE_PARM:
    D = TemplateTypeParmDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_NON_TYPE_TEMPLATE_PARM:
    D = NonTypeTemplateParmDecl::Create(Context, 0, SourceLocation(),
                                        SourceLocation(), 0, 0, 0, QualType(),
                                        false, 0);
    break;
  case DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK:
    D = NonTypeTemplateParmDecl::Create(Context, 0, SourceLocation(),
                                        SourceLocation(), 0, 0, 0, QualType(),
                                        0, 0, Record[Idx++], 0);
    break;
  case DECL_TEMPLATE_TEMPLATE_PARM:
    D = TemplateTemplateParmDecl::Create(Context, 0, SourceLocation(), 0, 0,
                                         false, 0, 0);
    break;
  case DECL_TYPE_ALIAS_TEMPLATE:
    D = TypeAliasTemplateDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_STATIC_ASSERT:
    D = StaticAssertDecl::Create(Context, 0, SourceLocation(), 0, 0,
                                 SourceLocation());
    break;

  case DECL_OBJC_METHOD:
    D = ObjCMethodDecl::Create(Context, SourceLocation(), SourceLocation(),
                               Selector(), QualType(), 0, 0);
    break;
  case DECL_OBJC_INTERFACE:
    D = ObjCInterfaceDecl::Create(Context, 0, SourceLocation(), 0);
    break;
  case DECL_OBJC_IVAR:
    D = ObjCIvarDecl::Create(Context, 0, SourceLocation(), SourceLocation(),
                             0, QualType(), 0, ObjCIvarDecl::None);
    break;
  case DECL_OBJC_PROTOCOL:
    D = ObjCProtocolDecl::Create(Context, 0, 0, SourceLocation(),
                                 SourceLocation());
    break;
  case DECL_OBJC_AT_DEFS_FIELD:
    D = ObjCAtDefsFieldDecl::Create(Context, 0, SourceLocation(),
                                    SourceLocation(), 0, QualType(), 0);
    break;
  case DECL_OBJC_CLASS:
    D = ObjCClassDecl::Create(Context, 0, SourceLocation());
    break;
  case DECL_OBJC_FORWARD_PROTOCOL:
    D = ObjCForwardProtocolDecl::Create(Context, 0, SourceLocation());
    break;
  case DECL_OBJC_CATEGORY:
    D = ObjCCategoryDecl::Create(Context, Decl::EmptyShell());
    break;
  case DECL_OBJC_CATEGORY_IMPL:
    D = ObjCCategoryImplDecl::Create(Context, 0, 0, 0, SourceLocation(),
                                     SourceLocation());
    break;
  case DECL_OBJC_IMPLEMENTATION:
    D = ObjCImplementationDecl::Create(Context, 0, 0, 0, SourceLocation(),
                                       SourceLocation());
    break;
  case DECL_OBJC_COMPATIBLE_ALIAS:
    D = ObjCCompatibleAliasDecl::Create(Context, 0, SourceLocation(), 0, 0);
    break;
  case DECL_OBJC_PROPERTY:
    D = ObjCPropertyDecl::Create(Context, 0, SourceLocation(), 0, SourceLocation(),
                                 0);
    break;
  case DECL_OBJC_PROPERTY_IMPL:
    D = ObjCPropertyImplDecl::Create(Context, 0, SourceLocation(),
                                     SourceLocation(), 0,
                                     ObjCPropertyImplDecl::Dynamic, 0,
                                     SourceLocation());
    break;
  case DECL_FIELD:
    D = FieldDecl::Create(Context, 0, SourceLocation(), SourceLocation(), 0,
                          QualType(), 0, 0, false, false);
    break;
  case DECL_INDIRECTFIELD:
    D = IndirectFieldDecl::Create(Context, 0, SourceLocation(), 0, QualType(),
                                  0, 0);
    break;
  case DECL_VAR:
    D = VarDecl::Create(Context, 0, SourceLocation(), SourceLocation(), 0,
                        QualType(), 0, SC_None, SC_None);
    break;

  case DECL_IMPLICIT_PARAM:
    D = ImplicitParamDecl::Create(Context, 0, SourceLocation(), 0, QualType());
    break;

  case DECL_PARM_VAR:
    D = ParmVarDecl::Create(Context, 0, SourceLocation(), SourceLocation(), 0,
                            QualType(), 0, SC_None, SC_None, 0);
    break;
  case DECL_FILE_SCOPE_ASM:
    D = FileScopeAsmDecl::Create(Context, 0, 0, SourceLocation(),
                                 SourceLocation());
    break;
  case DECL_BLOCK:
    D = BlockDecl::Create(Context, 0, SourceLocation());
    break;
  case DECL_CXX_BASE_SPECIFIERS:
    Error("attempt to read a C++ base-specifier record as a declaration");
    return 0;
  }

  assert(D && "Unknown declaration reading AST file");
  LoadedDecl(Index, D);
  Reader.Visit(D);

  // If this declaration is also a declaration context, get the
  // offsets for its tables of lexical and visible declarations.
  if (DeclContext *DC = dyn_cast<DeclContext>(D)) {
    std::pair<uint64_t, uint64_t> Offsets = Reader.VisitDeclContext(DC);
    if (Offsets.first || Offsets.second) {
      if (Offsets.first != 0)
        DC->setHasExternalLexicalStorage(true);
      if (Offsets.second != 0)
        DC->setHasExternalVisibleStorage(true);
      if (ReadDeclContextStorage(*Loc.F, DeclsCursor, Offsets, 
                                 Loc.F->DeclContextInfos[DC]))
        return 0;
    }

    // Now add the pending visible updates for this decl context, if it has any.
    DeclContextVisibleUpdatesPending::iterator I =
        PendingVisibleUpdates.find(ID);
    if (I != PendingVisibleUpdates.end()) {
      // There are updates. This means the context has external visible
      // storage, even if the original stored version didn't.
      DC->setHasExternalVisibleStorage(true);
      DeclContextVisibleUpdates &U = I->second;
      for (DeclContextVisibleUpdates::iterator UI = U.begin(), UE = U.end();
           UI != UE; ++UI) {
        UI->second->DeclContextInfos[DC].NameLookupTableData = UI->first;
      }
      PendingVisibleUpdates.erase(I);
    }
  }
  assert(Idx == Record.size());

  // Load any relevant update records.
  loadDeclUpdateRecords(ID, D);
  
  if (ObjCChainedCategoriesInterfaces.count(ID))
    loadObjCChainedCategories(ID, cast<ObjCInterfaceDecl>(D));
  
  // If we have deserialized a declaration that has a definition the
  // AST consumer might need to know about, queue it.
  // We don't pass it to the consumer immediately because we may be in recursive
  // loading, and some declarations may still be initializing.
  if (isConsumerInterestedIn(D))
      InterestingDecls.push_back(D);
  
  return D;
}

void ASTReader::loadDeclUpdateRecords(serialization::DeclID ID, Decl *D) {
  // The declaration may have been modified by files later in the chain.
  // If this is the case, read the record containing the updates from each file
  // and pass it to ASTDeclReader to make the modifications.
  DeclUpdateOffsetsMap::iterator UpdI = DeclUpdateOffsets.find(ID);
  if (UpdI != DeclUpdateOffsets.end()) {
    FileOffsetsTy &UpdateOffsets = UpdI->second;
    for (FileOffsetsTy::iterator
         I = UpdateOffsets.begin(), E = UpdateOffsets.end(); I != E; ++I) {
      Module *F = I->first;
      uint64_t Offset = I->second;
      llvm::BitstreamCursor &Cursor = F->DeclsCursor;
      SavedStreamPosition SavedPosition(Cursor);
      Cursor.JumpToBit(Offset);
      RecordData Record;
      unsigned Code = Cursor.ReadCode();
      unsigned RecCode = Cursor.ReadRecord(Code, Record);
      (void)RecCode;
      assert(RecCode == DECL_UPDATES && "Expected DECL_UPDATES record!");
      
      unsigned Idx = 0;
      ASTDeclReader Reader(*this, *F, Cursor, ID, Record, Idx);
      Reader.UpdateDecl(D, *F, Record);
    }
  }
}

namespace {
  /// \brief Given an ObjC interface, goes through the modules and links to the
  /// interface all the categories for it.
  class ObjCChainedCategoriesVisitor {
    ASTReader &Reader;
    serialization::GlobalDeclID InterfaceID;
    ObjCInterfaceDecl *Interface;
    ObjCCategoryDecl *GlobHeadCat, *GlobTailCat;
    llvm::DenseMap<DeclarationName, ObjCCategoryDecl *> NameCategoryMap;

  public:
    ObjCChainedCategoriesVisitor(ASTReader &Reader,
                                 serialization::GlobalDeclID InterfaceID,
                                 ObjCInterfaceDecl *Interface)
      : Reader(Reader), InterfaceID(InterfaceID), Interface(Interface),
        GlobHeadCat(0), GlobTailCat(0) { }

    static bool visit(Module &M, void *UserData) {
      return static_cast<ObjCChainedCategoriesVisitor *>(UserData)->visit(M);
    }

    bool visit(Module &M) {
      if (Reader.isDeclIDFromModule(InterfaceID, M))
        return true; // We reached the module where the interface originated
                    // from. Stop traversing the imported modules.

      Module::ChainedObjCCategoriesMap::iterator
        I = M.ChainedObjCCategories.find(InterfaceID);
      if (I == M.ChainedObjCCategories.end())
        return false;

      ObjCCategoryDecl *
        HeadCat = Reader.GetLocalDeclAs<ObjCCategoryDecl>(M, I->second.first);
      ObjCCategoryDecl *
        TailCat = Reader.GetLocalDeclAs<ObjCCategoryDecl>(M, I->second.second);

      addCategories(HeadCat, TailCat);
      return false;
    }

    void addCategories(ObjCCategoryDecl *HeadCat,
                       ObjCCategoryDecl *TailCat = 0) {
      if (!HeadCat) {
        assert(!TailCat);
        return;
      }

      if (!TailCat) {
        TailCat = HeadCat;
        while (TailCat->getNextClassCategory())
          TailCat = TailCat->getNextClassCategory();
      }

      if (!GlobHeadCat) {
        GlobHeadCat = HeadCat;
        GlobTailCat = TailCat;
      } else {
        ASTDeclReader::setNextObjCCategory(GlobTailCat, HeadCat);
        GlobTailCat = TailCat;
      }

      llvm::DenseSet<DeclarationName> Checked;
      for (ObjCCategoryDecl *Cat = HeadCat,
                            *CatEnd = TailCat->getNextClassCategory();
             Cat != CatEnd; Cat = Cat->getNextClassCategory()) {
        if (Checked.count(Cat->getDeclName()))
          continue;
        Checked.insert(Cat->getDeclName());
        checkForDuplicate(Cat);
      }
    }

    /// \brief Warns for duplicate categories that come from different modules.
    void checkForDuplicate(ObjCCategoryDecl *Cat) {
      DeclarationName Name = Cat->getDeclName();
      // Find the top category with the same name. We do not want to warn for
      // duplicates along the established chain because there were already
      // warnings for them when the module was created. We only want to warn for
      // duplicates between non-dependent modules:
      //
      //   MT     //
      //  /  \    //
      // ML  MR   //
      //
      // We want to warn for duplicates between ML and MR,not between ML and MT.
      //
      // FIXME: We should not warn for duplicates in diamond:
      //
      //   MT     //
      //  /  \    //
      // ML  MR   //
      //  \  /    //
      //   MB     //
      //
      // If there are duplicates in ML/MR, there will be warning when creating
      // MB *and* when importing MB. We should not warn when importing.
      for (ObjCCategoryDecl *Next = Cat->getNextClassCategory(); Next;
             Next = Next->getNextClassCategory()) {
        if (Next->getDeclName() == Name)
          Cat = Next;
      }

      ObjCCategoryDecl *&PrevCat = NameCategoryMap[Name];
      if (!PrevCat)
        PrevCat = Cat;

      if (PrevCat != Cat) {
        Reader.Diag(Cat->getLocation(), diag::warn_dup_category_def)
          << Interface->getDeclName() << Name;
        Reader.Diag(PrevCat->getLocation(), diag::note_previous_definition);
      }
    }

    ObjCCategoryDecl *getHeadCategory() const { return GlobHeadCat; }
  };
}

void ASTReader::loadObjCChainedCategories(serialization::GlobalDeclID ID,
                                          ObjCInterfaceDecl *D) {
  ObjCChainedCategoriesVisitor Visitor(*this, ID, D);
  ModuleMgr.visit(ObjCChainedCategoriesVisitor::visit, &Visitor);
  // Also add the categories that the interface already links to.
  Visitor.addCategories(D->getCategoryList());
  D->setCategoryList(Visitor.getHeadCategory());
}

void ASTDeclReader::UpdateDecl(Decl *D, Module &Module,
                               const RecordData &Record) {
  unsigned Idx = 0;
  while (Idx < Record.size()) {
    switch ((DeclUpdateKind)Record[Idx++]) {
    case UPD_CXX_SET_DEFINITIONDATA: {
      CXXRecordDecl *RD = cast<CXXRecordDecl>(D);
      CXXRecordDecl *DefinitionDecl
        = Reader.ReadDeclAs<CXXRecordDecl>(Module, Record, Idx);
      assert(!RD->DefinitionData && "DefinitionData is already set!");
      InitializeCXXDefinitionData(RD, DefinitionDecl, Record, Idx);
      break;
    }

    case UPD_CXX_ADDED_IMPLICIT_MEMBER:
      cast<CXXRecordDecl>(D)->addedMember(Reader.ReadDecl(Module, Record, Idx));
      break;

    case UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION:
      // It will be added to the template's specializations set when loaded.
      (void)Reader.ReadDecl(Module, Record, Idx);
      break;

    case UPD_CXX_ADDED_ANONYMOUS_NAMESPACE: {
      NamespaceDecl *Anon
        = Reader.ReadDeclAs<NamespaceDecl>(Module, Record, Idx);
      // Guard against these being loaded out of original order. Don't use
      // getNextNamespace(), since it tries to access the context and can't in
      // the middle of deserialization.
      if (!Anon->NextNamespace) {
        if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(D))
          TU->setAnonymousNamespace(Anon);
        else
          cast<NamespaceDecl>(D)->OrigOrAnonNamespace.setPointer(Anon);
      }
      break;
    }

    case UPD_CXX_INSTANTIATED_STATIC_DATA_MEMBER:
      cast<VarDecl>(D)->getMemberSpecializationInfo()->setPointOfInstantiation(
          Reader.ReadSourceLocation(Module, Record, Idx));
      break;
    }
  }
}
