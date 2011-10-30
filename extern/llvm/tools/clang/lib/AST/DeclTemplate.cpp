//===--- DeclTemplate.cpp - Template Declaration AST Node Implementation --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the C++ related Decl classes for templates.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/Basic/IdentifierTable.h"
#include "llvm/ADT/STLExtras.h"
#include <memory>
using namespace clang;

//===----------------------------------------------------------------------===//
// TemplateParameterList Implementation
//===----------------------------------------------------------------------===//

TemplateParameterList::TemplateParameterList(SourceLocation TemplateLoc,
                                             SourceLocation LAngleLoc,
                                             NamedDecl **Params, unsigned NumParams,
                                             SourceLocation RAngleLoc)
  : TemplateLoc(TemplateLoc), LAngleLoc(LAngleLoc), RAngleLoc(RAngleLoc),
    NumParams(NumParams) {
  for (unsigned Idx = 0; Idx < NumParams; ++Idx)
    begin()[Idx] = Params[Idx];
}

TemplateParameterList *
TemplateParameterList::Create(const ASTContext &C, SourceLocation TemplateLoc,
                              SourceLocation LAngleLoc, NamedDecl **Params,
                              unsigned NumParams, SourceLocation RAngleLoc) {
  unsigned Size = sizeof(TemplateParameterList) 
                + sizeof(NamedDecl *) * NumParams;
  unsigned Align = llvm::AlignOf<TemplateParameterList>::Alignment;
  void *Mem = C.Allocate(Size, Align);
  return new (Mem) TemplateParameterList(TemplateLoc, LAngleLoc, Params,
                                         NumParams, RAngleLoc);
}

unsigned TemplateParameterList::getMinRequiredArguments() const {
  unsigned NumRequiredArgs = 0;
  for (iterator P = const_cast<TemplateParameterList *>(this)->begin(), 
             PEnd = const_cast<TemplateParameterList *>(this)->end(); 
       P != PEnd; ++P) {
    if ((*P)->isTemplateParameterPack()) {
      if (NonTypeTemplateParmDecl *NTTP = dyn_cast<NonTypeTemplateParmDecl>(*P))
        if (NTTP->isExpandedParameterPack()) {
          NumRequiredArgs += NTTP->getNumExpansionTypes();
          continue;
        }
      
      break;
    }
  
    if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(*P)) {
      if (TTP->hasDefaultArgument())
        break;
    } else if (NonTypeTemplateParmDecl *NTTP 
                                    = dyn_cast<NonTypeTemplateParmDecl>(*P)) {
      if (NTTP->hasDefaultArgument())
        break;
    } else if (cast<TemplateTemplateParmDecl>(*P)->hasDefaultArgument())
      break;
    
    ++NumRequiredArgs;
  }
  
  return NumRequiredArgs;
}

unsigned TemplateParameterList::getDepth() const {
  if (size() == 0)
    return 0;
  
  const NamedDecl *FirstParm = getParam(0);
  if (const TemplateTypeParmDecl *TTP
        = dyn_cast<TemplateTypeParmDecl>(FirstParm))
    return TTP->getDepth();
  else if (const NonTypeTemplateParmDecl *NTTP 
             = dyn_cast<NonTypeTemplateParmDecl>(FirstParm))
    return NTTP->getDepth();
  else
    return cast<TemplateTemplateParmDecl>(FirstParm)->getDepth();
}

static void AdoptTemplateParameterList(TemplateParameterList *Params,
                                       DeclContext *Owner) {
  for (TemplateParameterList::iterator P = Params->begin(), 
                                    PEnd = Params->end();
       P != PEnd; ++P) {
    (*P)->setDeclContext(Owner);
    
    if (TemplateTemplateParmDecl *TTP = dyn_cast<TemplateTemplateParmDecl>(*P))
      AdoptTemplateParameterList(TTP->getTemplateParameters(), Owner);
  }
}

//===----------------------------------------------------------------------===//
// RedeclarableTemplateDecl Implementation
//===----------------------------------------------------------------------===//

RedeclarableTemplateDecl::CommonBase *RedeclarableTemplateDecl::getCommonPtr() {
  // Find the first declaration of this function template.
  RedeclarableTemplateDecl *First = getCanonicalDecl();

  if (First->CommonOrPrev.isNull()) {
    CommonBase *CommonPtr = First->newCommon(getASTContext());
    First->CommonOrPrev = CommonPtr;
    CommonPtr->Latest = First;
  }
  return First->CommonOrPrev.get<CommonBase*>();
}


RedeclarableTemplateDecl *RedeclarableTemplateDecl::getCanonicalDeclImpl() {
  RedeclarableTemplateDecl *Tmpl = this;
  while (Tmpl->getPreviousDeclaration())
    Tmpl = Tmpl->getPreviousDeclaration();
  return Tmpl;
}

void RedeclarableTemplateDecl::setPreviousDeclarationImpl(
                                               RedeclarableTemplateDecl *Prev) {
  if (Prev) {
    CommonBase *Common = Prev->getCommonPtr();
    Prev = Common->Latest;
    Common->Latest = this;
    CommonOrPrev = Prev;
  } else {
    assert(CommonOrPrev.is<CommonBase*>() && "Cannot reset TemplateDecl Prev");
  }
}

RedeclarableTemplateDecl *RedeclarableTemplateDecl::getNextRedeclaration() {
  if (CommonOrPrev.is<RedeclarableTemplateDecl*>())
    return CommonOrPrev.get<RedeclarableTemplateDecl*>();
  CommonBase *Common = CommonOrPrev.get<CommonBase*>();
  return Common ? Common->Latest : this;
}

template <class EntryType>
typename RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::DeclType*
RedeclarableTemplateDecl::findSpecializationImpl(
                                 llvm::FoldingSet<EntryType> &Specs,
                                 const TemplateArgument *Args, unsigned NumArgs,
                                 void *&InsertPos) {
  typedef SpecEntryTraits<EntryType> SETraits;
  llvm::FoldingSetNodeID ID;
  EntryType::Profile(ID,Args,NumArgs, getASTContext());
  EntryType *Entry = Specs.FindNodeOrInsertPos(ID, InsertPos);
  return Entry ? SETraits::getMostRecentDeclaration(Entry) : 0;
}

/// \brief Generate the injected template arguments for the given template
/// parameter list, e.g., for the injected-class-name of a class template.
static void GenerateInjectedTemplateArgs(ASTContext &Context,
                                        TemplateParameterList *Params,
                                         TemplateArgument *Args) {
  for (TemplateParameterList::iterator Param = Params->begin(),
                                    ParamEnd = Params->end();
       Param != ParamEnd; ++Param) {
    TemplateArgument Arg;
    if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(*Param)) {
      QualType ArgType = Context.getTypeDeclType(TTP);
      if (TTP->isParameterPack())
        ArgType = Context.getPackExpansionType(ArgType, 
                                               llvm::Optional<unsigned>());
      
      Arg = TemplateArgument(ArgType);
    } else if (NonTypeTemplateParmDecl *NTTP =
               dyn_cast<NonTypeTemplateParmDecl>(*Param)) {
      Expr *E = new (Context) DeclRefExpr(NTTP,
                                  NTTP->getType().getNonLValueExprType(Context),
                                  Expr::getValueKindForType(NTTP->getType()),
                                          NTTP->getLocation());
      
      if (NTTP->isParameterPack())
        E = new (Context) PackExpansionExpr(Context.DependentTy, E,
                                            NTTP->getLocation(),
                                            llvm::Optional<unsigned>());
      Arg = TemplateArgument(E);
    } else {
      TemplateTemplateParmDecl *TTP = cast<TemplateTemplateParmDecl>(*Param);
      if (TTP->isParameterPack())
        Arg = TemplateArgument(TemplateName(TTP), llvm::Optional<unsigned>());
      else
        Arg = TemplateArgument(TemplateName(TTP));
    }
    
    if ((*Param)->isTemplateParameterPack())
      Arg = TemplateArgument::CreatePackCopy(Context, &Arg, 1);
    
    *Args++ = Arg;
  }
}
                                      
//===----------------------------------------------------------------------===//
// FunctionTemplateDecl Implementation
//===----------------------------------------------------------------------===//

void FunctionTemplateDecl::DeallocateCommon(void *Ptr) {
  static_cast<Common *>(Ptr)->~Common();
}

FunctionTemplateDecl *FunctionTemplateDecl::Create(ASTContext &C,
                                                   DeclContext *DC,
                                                   SourceLocation L,
                                                   DeclarationName Name,
                                               TemplateParameterList *Params,
                                                   NamedDecl *Decl) {
  AdoptTemplateParameterList(Params, cast<DeclContext>(Decl));
  return new (C) FunctionTemplateDecl(DC, L, Name, Params, Decl);
}

FunctionTemplateDecl *FunctionTemplateDecl::Create(ASTContext &C, EmptyShell) {
  return new (C) FunctionTemplateDecl(0, SourceLocation(), DeclarationName(),
                                      0, 0);
}

RedeclarableTemplateDecl::CommonBase *
FunctionTemplateDecl::newCommon(ASTContext &C) {
  Common *CommonPtr = new (C) Common;
  C.AddDeallocation(DeallocateCommon, CommonPtr);
  return CommonPtr;
}

FunctionDecl *
FunctionTemplateDecl::findSpecialization(const TemplateArgument *Args,
                                         unsigned NumArgs, void *&InsertPos) {
  return findSpecializationImpl(getSpecializations(), Args, NumArgs, InsertPos);
}

void FunctionTemplateDecl::addSpecialization(
      FunctionTemplateSpecializationInfo *Info, void *InsertPos) {
  getSpecializations().InsertNode(Info, InsertPos);
  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(this, Info->Function);
}

std::pair<const TemplateArgument *, unsigned> 
FunctionTemplateDecl::getInjectedTemplateArgs() {
  TemplateParameterList *Params = getTemplateParameters();
  Common *CommonPtr = getCommonPtr();
  if (!CommonPtr->InjectedArgs) {
    CommonPtr->InjectedArgs
      = new (getASTContext()) TemplateArgument [Params->size()];
    GenerateInjectedTemplateArgs(getASTContext(), Params, 
                                 CommonPtr->InjectedArgs);
  }
  
  return std::make_pair(CommonPtr->InjectedArgs, Params->size());
}

//===----------------------------------------------------------------------===//
// ClassTemplateDecl Implementation
//===----------------------------------------------------------------------===//

void ClassTemplateDecl::DeallocateCommon(void *Ptr) {
  static_cast<Common *>(Ptr)->~Common();
}

ClassTemplateDecl *ClassTemplateDecl::Create(ASTContext &C,
                                             DeclContext *DC,
                                             SourceLocation L,
                                             DeclarationName Name,
                                             TemplateParameterList *Params,
                                             NamedDecl *Decl,
                                             ClassTemplateDecl *PrevDecl) {
  AdoptTemplateParameterList(Params, cast<DeclContext>(Decl));
  ClassTemplateDecl *New = new (C) ClassTemplateDecl(DC, L, Name, Params, Decl);
  New->setPreviousDeclaration(PrevDecl);
  return New;
}

ClassTemplateDecl *ClassTemplateDecl::Create(ASTContext &C, EmptyShell Empty) {
  return new (C) ClassTemplateDecl(Empty);
}

void ClassTemplateDecl::LoadLazySpecializations() {
  Common *CommonPtr = getCommonPtr();
  if (CommonPtr->LazySpecializations) {
    ASTContext &Context = getASTContext();
    uint32_t *Specs = CommonPtr->LazySpecializations;
    CommonPtr->LazySpecializations = 0;
    for (uint32_t I = 0, N = *Specs++; I != N; ++I)
      (void)Context.getExternalSource()->GetExternalDecl(Specs[I]);
  }
}

llvm::FoldingSet<ClassTemplateSpecializationDecl> &
ClassTemplateDecl::getSpecializations() {
  LoadLazySpecializations();
  return getCommonPtr()->Specializations;
}  

llvm::FoldingSet<ClassTemplatePartialSpecializationDecl> &
ClassTemplateDecl::getPartialSpecializations() {
  LoadLazySpecializations();
  return getCommonPtr()->PartialSpecializations;
}  

RedeclarableTemplateDecl::CommonBase *
ClassTemplateDecl::newCommon(ASTContext &C) {
  Common *CommonPtr = new (C) Common;
  C.AddDeallocation(DeallocateCommon, CommonPtr);
  return CommonPtr;
}

ClassTemplateSpecializationDecl *
ClassTemplateDecl::findSpecialization(const TemplateArgument *Args,
                                      unsigned NumArgs, void *&InsertPos) {
  return findSpecializationImpl(getSpecializations(), Args, NumArgs, InsertPos);
}

void ClassTemplateDecl::AddSpecialization(ClassTemplateSpecializationDecl *D,
                                          void *InsertPos) {
  getSpecializations().InsertNode(D, InsertPos);
  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(this, D);
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecialization(const TemplateArgument *Args,
                                             unsigned NumArgs,
                                             void *&InsertPos) {
  return findSpecializationImpl(getPartialSpecializations(), Args, NumArgs,
                                InsertPos);
}

void ClassTemplateDecl::AddPartialSpecialization(
                                      ClassTemplatePartialSpecializationDecl *D,
                                      void *InsertPos) {
  getPartialSpecializations().InsertNode(D, InsertPos);
  if (ASTMutationListener *L = getASTMutationListener())
    L->AddedCXXTemplateSpecialization(this, D);
}

void ClassTemplateDecl::getPartialSpecializations(
          SmallVectorImpl<ClassTemplatePartialSpecializationDecl *> &PS) {
  llvm::FoldingSet<ClassTemplatePartialSpecializationDecl> &PartialSpecs
    = getPartialSpecializations();
  PS.clear();
  PS.resize(PartialSpecs.size());
  for (llvm::FoldingSet<ClassTemplatePartialSpecializationDecl>::iterator
       P = PartialSpecs.begin(), PEnd = PartialSpecs.end();
       P != PEnd; ++P) {
    assert(!PS[P->getSequenceNumber()]);
    PS[P->getSequenceNumber()] = P->getMostRecentDeclaration();
  }
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecialization(QualType T) {
  ASTContext &Context = getASTContext();
  typedef llvm::FoldingSet<ClassTemplatePartialSpecializationDecl>::iterator
    partial_spec_iterator;
  for (partial_spec_iterator P = getPartialSpecializations().begin(),
                          PEnd = getPartialSpecializations().end();
       P != PEnd; ++P) {
    if (Context.hasSameType(P->getInjectedSpecializationType(), T))
      return P->getMostRecentDeclaration();
  }

  return 0;
}

ClassTemplatePartialSpecializationDecl *
ClassTemplateDecl::findPartialSpecInstantiatedFromMember(
                                    ClassTemplatePartialSpecializationDecl *D) {
  Decl *DCanon = D->getCanonicalDecl();
  for (llvm::FoldingSet<ClassTemplatePartialSpecializationDecl>::iterator
            P = getPartialSpecializations().begin(),
         PEnd = getPartialSpecializations().end();
       P != PEnd; ++P) {
    if (P->getInstantiatedFromMember()->getCanonicalDecl() == DCanon)
      return P->getMostRecentDeclaration();
  }

  return 0;
}

QualType
ClassTemplateDecl::getInjectedClassNameSpecialization() {
  Common *CommonPtr = getCommonPtr();
  if (!CommonPtr->InjectedClassNameType.isNull())
    return CommonPtr->InjectedClassNameType;

  // C++0x [temp.dep.type]p2:
  //  The template argument list of a primary template is a template argument 
  //  list in which the nth template argument has the value of the nth template
  //  parameter of the class template. If the nth template parameter is a 
  //  template parameter pack (14.5.3), the nth template argument is a pack 
  //  expansion (14.5.3) whose pattern is the name of the template parameter 
  //  pack.
  ASTContext &Context = getASTContext();
  TemplateParameterList *Params = getTemplateParameters();
  SmallVector<TemplateArgument, 16> TemplateArgs;
  TemplateArgs.resize(Params->size());
  GenerateInjectedTemplateArgs(getASTContext(), Params, TemplateArgs.data());
  CommonPtr->InjectedClassNameType
    = Context.getTemplateSpecializationType(TemplateName(this),
                                            &TemplateArgs[0],
                                            TemplateArgs.size());
  return CommonPtr->InjectedClassNameType;
}

//===----------------------------------------------------------------------===//
// TemplateTypeParm Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//

TemplateTypeParmDecl *
TemplateTypeParmDecl::Create(const ASTContext &C, DeclContext *DC,
                             SourceLocation KeyLoc, SourceLocation NameLoc,
                             unsigned D, unsigned P, IdentifierInfo *Id,
                             bool Typename, bool ParameterPack) {
  TemplateTypeParmDecl *TTPDecl =
    new (C) TemplateTypeParmDecl(DC, KeyLoc, NameLoc, Id, Typename);
  QualType TTPType = C.getTemplateTypeParmType(D, P, ParameterPack, TTPDecl);
  TTPDecl->TypeForDecl = TTPType.getTypePtr();
  return TTPDecl;
}

TemplateTypeParmDecl *
TemplateTypeParmDecl::Create(const ASTContext &C, EmptyShell Empty) {
  return new (C) TemplateTypeParmDecl(0, SourceLocation(), SourceLocation(),
                                      0, false);
}

SourceLocation TemplateTypeParmDecl::getDefaultArgumentLoc() const {
  return hasDefaultArgument()
    ? DefaultArgument->getTypeLoc().getBeginLoc()
    : SourceLocation();
}

SourceRange TemplateTypeParmDecl::getSourceRange() const {
  if (hasDefaultArgument() && !defaultArgumentWasInherited())
    return SourceRange(getLocStart(),
                       DefaultArgument->getTypeLoc().getEndLoc());
  else
    return TypeDecl::getSourceRange();
}

unsigned TemplateTypeParmDecl::getDepth() const {
  return TypeForDecl->getAs<TemplateTypeParmType>()->getDepth();
}

unsigned TemplateTypeParmDecl::getIndex() const {
  return TypeForDecl->getAs<TemplateTypeParmType>()->getIndex();
}

bool TemplateTypeParmDecl::isParameterPack() const {
  return TypeForDecl->getAs<TemplateTypeParmType>()->isParameterPack();
}

//===----------------------------------------------------------------------===//
// NonTypeTemplateParmDecl Method Implementations
//===----------------------------------------------------------------------===//

NonTypeTemplateParmDecl::NonTypeTemplateParmDecl(DeclContext *DC, 
                                                 SourceLocation StartLoc,
                                                 SourceLocation IdLoc,
                                                 unsigned D, unsigned P,
                                                 IdentifierInfo *Id, 
                                                 QualType T, 
                                                 TypeSourceInfo *TInfo,
                                                 const QualType *ExpandedTypes,
                                                 unsigned NumExpandedTypes,
                                                TypeSourceInfo **ExpandedTInfos)
  : DeclaratorDecl(NonTypeTemplateParm, DC, IdLoc, Id, T, TInfo, StartLoc),
    TemplateParmPosition(D, P), DefaultArgumentAndInherited(0, false),
    ParameterPack(true), ExpandedParameterPack(true),
    NumExpandedTypes(NumExpandedTypes)
{
  if (ExpandedTypes && ExpandedTInfos) {
    void **TypesAndInfos = reinterpret_cast<void **>(this + 1);
    for (unsigned I = 0; I != NumExpandedTypes; ++I) {
      TypesAndInfos[2*I] = ExpandedTypes[I].getAsOpaquePtr();
      TypesAndInfos[2*I + 1] = ExpandedTInfos[I];
    }
  }
}

NonTypeTemplateParmDecl *
NonTypeTemplateParmDecl::Create(const ASTContext &C, DeclContext *DC,
                                SourceLocation StartLoc, SourceLocation IdLoc,
                                unsigned D, unsigned P, IdentifierInfo *Id,
                                QualType T, bool ParameterPack,
                                TypeSourceInfo *TInfo) {
  return new (C) NonTypeTemplateParmDecl(DC, StartLoc, IdLoc, D, P, Id,
                                         T, ParameterPack, TInfo);
}

NonTypeTemplateParmDecl *
NonTypeTemplateParmDecl::Create(const ASTContext &C, DeclContext *DC, 
                                SourceLocation StartLoc, SourceLocation IdLoc,
                                unsigned D, unsigned P, 
                                IdentifierInfo *Id, QualType T, 
                                TypeSourceInfo *TInfo,
                                const QualType *ExpandedTypes, 
                                unsigned NumExpandedTypes,
                                TypeSourceInfo **ExpandedTInfos) {
  unsigned Size = sizeof(NonTypeTemplateParmDecl) 
                + NumExpandedTypes * 2 * sizeof(void*);
  void *Mem = C.Allocate(Size);
  return new (Mem) NonTypeTemplateParmDecl(DC, StartLoc, IdLoc,
                                           D, P, Id, T, TInfo,
                                           ExpandedTypes, NumExpandedTypes, 
                                           ExpandedTInfos);
}

SourceRange NonTypeTemplateParmDecl::getSourceRange() const {
  if (hasDefaultArgument() && !defaultArgumentWasInherited())
    return SourceRange(getOuterLocStart(),
                       getDefaultArgument()->getSourceRange().getEnd());
  return DeclaratorDecl::getSourceRange();
}

SourceLocation NonTypeTemplateParmDecl::getDefaultArgumentLoc() const {
  return hasDefaultArgument()
    ? getDefaultArgument()->getSourceRange().getBegin()
    : SourceLocation();
}

//===----------------------------------------------------------------------===//
// TemplateTemplateParmDecl Method Implementations
//===----------------------------------------------------------------------===//

TemplateTemplateParmDecl *
TemplateTemplateParmDecl::Create(const ASTContext &C, DeclContext *DC,
                                 SourceLocation L, unsigned D, unsigned P,
                                 bool ParameterPack, IdentifierInfo *Id,
                                 TemplateParameterList *Params) {
  return new (C) TemplateTemplateParmDecl(DC, L, D, P, ParameterPack, Id, 
                                          Params);
}

//===----------------------------------------------------------------------===//
// TemplateArgumentList Implementation
//===----------------------------------------------------------------------===//
TemplateArgumentList *
TemplateArgumentList::CreateCopy(ASTContext &Context,
                                 const TemplateArgument *Args,
                                 unsigned NumArgs) {
  std::size_t Size = sizeof(TemplateArgumentList)
                   + NumArgs * sizeof(TemplateArgument);
  void *Mem = Context.Allocate(Size);
  TemplateArgument *StoredArgs 
    = reinterpret_cast<TemplateArgument *>(
                                static_cast<TemplateArgumentList *>(Mem) + 1);
  std::uninitialized_copy(Args, Args + NumArgs, StoredArgs);
  return new (Mem) TemplateArgumentList(StoredArgs, NumArgs, true);
}

FunctionTemplateSpecializationInfo *
FunctionTemplateSpecializationInfo::Create(ASTContext &C, FunctionDecl *FD,
                                           FunctionTemplateDecl *Template,
                                           TemplateSpecializationKind TSK,
                                       const TemplateArgumentList *TemplateArgs,
                          const TemplateArgumentListInfo *TemplateArgsAsWritten,
                                           SourceLocation POI) {
  const ASTTemplateArgumentListInfo *ArgsAsWritten = 0;
  if (TemplateArgsAsWritten)
    ArgsAsWritten = ASTTemplateArgumentListInfo::Create(C,
                                                        *TemplateArgsAsWritten);

  return new (C) FunctionTemplateSpecializationInfo(FD, Template, TSK,
                                                    TemplateArgs,
                                                    ArgsAsWritten,
                                                    POI);
}

//===----------------------------------------------------------------------===//
// ClassTemplateSpecializationDecl Implementation
//===----------------------------------------------------------------------===//
ClassTemplateSpecializationDecl::
ClassTemplateSpecializationDecl(ASTContext &Context, Kind DK, TagKind TK,
                                DeclContext *DC, SourceLocation StartLoc,
                                SourceLocation IdLoc,
                                ClassTemplateDecl *SpecializedTemplate,
                                const TemplateArgument *Args,
                                unsigned NumArgs,
                                ClassTemplateSpecializationDecl *PrevDecl)
  : CXXRecordDecl(DK, TK, DC, StartLoc, IdLoc,
                  SpecializedTemplate->getIdentifier(),
                  PrevDecl),
    SpecializedTemplate(SpecializedTemplate),
    ExplicitInfo(0),
    TemplateArgs(TemplateArgumentList::CreateCopy(Context, Args, NumArgs)),
    SpecializationKind(TSK_Undeclared) {
}

ClassTemplateSpecializationDecl::ClassTemplateSpecializationDecl(Kind DK)
  : CXXRecordDecl(DK, TTK_Struct, 0, SourceLocation(), SourceLocation(), 0, 0),
    ExplicitInfo(0),
    SpecializationKind(TSK_Undeclared) {
}

ClassTemplateSpecializationDecl *
ClassTemplateSpecializationDecl::Create(ASTContext &Context, TagKind TK,
                                        DeclContext *DC,
                                        SourceLocation StartLoc,
                                        SourceLocation IdLoc,
                                        ClassTemplateDecl *SpecializedTemplate,
                                        const TemplateArgument *Args,
                                        unsigned NumArgs,
                                   ClassTemplateSpecializationDecl *PrevDecl) {
  ClassTemplateSpecializationDecl *Result
    = new (Context)ClassTemplateSpecializationDecl(Context,
                                                   ClassTemplateSpecialization,
                                                   TK, DC, StartLoc, IdLoc,
                                                   SpecializedTemplate,
                                                   Args, NumArgs,
                                                   PrevDecl);
  Context.getTypeDeclType(Result, PrevDecl);
  return Result;
}

ClassTemplateSpecializationDecl *
ClassTemplateSpecializationDecl::Create(ASTContext &Context, EmptyShell Empty) {
  return
    new (Context)ClassTemplateSpecializationDecl(ClassTemplateSpecialization);
}

void
ClassTemplateSpecializationDecl::getNameForDiagnostic(std::string &S,
                                                  const PrintingPolicy &Policy,
                                                      bool Qualified) const {
  NamedDecl::getNameForDiagnostic(S, Policy, Qualified);

  const TemplateArgumentList &TemplateArgs = getTemplateArgs();
  S += TemplateSpecializationType::PrintTemplateArgumentList(
                                                          TemplateArgs.data(),
                                                          TemplateArgs.size(),
                                                             Policy);
}

ClassTemplateDecl *
ClassTemplateSpecializationDecl::getSpecializedTemplate() const {
  if (SpecializedPartialSpecialization *PartialSpec
      = SpecializedTemplate.dyn_cast<SpecializedPartialSpecialization*>())
    return PartialSpec->PartialSpecialization->getSpecializedTemplate();
  return SpecializedTemplate.get<ClassTemplateDecl*>();
}

SourceRange
ClassTemplateSpecializationDecl::getSourceRange() const {
  if (ExplicitInfo) {
    SourceLocation Begin = getExternLoc();
    if (Begin.isInvalid())
      Begin = getTemplateKeywordLoc();
    SourceLocation End = getRBraceLoc();
    if (End.isInvalid())
      End = getTypeAsWritten()->getTypeLoc().getEndLoc();
    return SourceRange(Begin, End);
  }
  else {
    // No explicit info available.
    llvm::PointerUnion<ClassTemplateDecl *,
                       ClassTemplatePartialSpecializationDecl *>
      inst_from = getInstantiatedFrom();
    if (inst_from.isNull())
      return getSpecializedTemplate()->getSourceRange();
    if (ClassTemplateDecl *ctd = inst_from.dyn_cast<ClassTemplateDecl*>())
      return ctd->getSourceRange();
    return inst_from.get<ClassTemplatePartialSpecializationDecl*>()
      ->getSourceRange();
  }
}

//===----------------------------------------------------------------------===//
// ClassTemplatePartialSpecializationDecl Implementation
//===----------------------------------------------------------------------===//
ClassTemplatePartialSpecializationDecl::
ClassTemplatePartialSpecializationDecl(ASTContext &Context, TagKind TK,
                                       DeclContext *DC,
                                       SourceLocation StartLoc,
                                       SourceLocation IdLoc,
                                       TemplateParameterList *Params,
                                       ClassTemplateDecl *SpecializedTemplate,
                                       const TemplateArgument *Args,
                                       unsigned NumArgs,
                                       TemplateArgumentLoc *ArgInfos,
                                       unsigned NumArgInfos,
                               ClassTemplatePartialSpecializationDecl *PrevDecl,
                                       unsigned SequenceNumber)
  : ClassTemplateSpecializationDecl(Context,
                                    ClassTemplatePartialSpecialization,
                                    TK, DC, StartLoc, IdLoc,
                                    SpecializedTemplate,
                                    Args, NumArgs, PrevDecl),
    TemplateParams(Params), ArgsAsWritten(ArgInfos),
    NumArgsAsWritten(NumArgInfos), SequenceNumber(SequenceNumber),
    InstantiatedFromMember(0, false)
{ 
  AdoptTemplateParameterList(Params, this);
}

ClassTemplatePartialSpecializationDecl *
ClassTemplatePartialSpecializationDecl::
Create(ASTContext &Context, TagKind TK,DeclContext *DC,
       SourceLocation StartLoc, SourceLocation IdLoc,
       TemplateParameterList *Params,
       ClassTemplateDecl *SpecializedTemplate,
       const TemplateArgument *Args,
       unsigned NumArgs,
       const TemplateArgumentListInfo &ArgInfos,
       QualType CanonInjectedType,
       ClassTemplatePartialSpecializationDecl *PrevDecl,
       unsigned SequenceNumber) {
  unsigned N = ArgInfos.size();
  TemplateArgumentLoc *ClonedArgs = new (Context) TemplateArgumentLoc[N];
  for (unsigned I = 0; I != N; ++I)
    ClonedArgs[I] = ArgInfos[I];

  ClassTemplatePartialSpecializationDecl *Result
    = new (Context)ClassTemplatePartialSpecializationDecl(Context, TK, DC,
                                                          StartLoc, IdLoc,
                                                          Params,
                                                          SpecializedTemplate,
                                                          Args, NumArgs,
                                                          ClonedArgs, N,
                                                          PrevDecl,
                                                          SequenceNumber);
  Result->setSpecializationKind(TSK_ExplicitSpecialization);

  Context.getInjectedClassNameType(Result, CanonInjectedType);
  return Result;
}

ClassTemplatePartialSpecializationDecl *
ClassTemplatePartialSpecializationDecl::Create(ASTContext &Context,
                                               EmptyShell Empty) {
  return new (Context)ClassTemplatePartialSpecializationDecl();
}

//===----------------------------------------------------------------------===//
// FriendTemplateDecl Implementation
//===----------------------------------------------------------------------===//

FriendTemplateDecl *FriendTemplateDecl::Create(ASTContext &Context,
                                               DeclContext *DC,
                                               SourceLocation L,
                                               unsigned NParams,
                                               TemplateParameterList **Params,
                                               FriendUnion Friend,
                                               SourceLocation FLoc) {
  FriendTemplateDecl *Result
    = new (Context) FriendTemplateDecl(DC, L, NParams, Params, Friend, FLoc);
  return Result;
}

FriendTemplateDecl *FriendTemplateDecl::Create(ASTContext &Context,
                                               EmptyShell Empty) {
  return new (Context) FriendTemplateDecl(Empty);
}

//===----------------------------------------------------------------------===//
// TypeAliasTemplateDecl Implementation
//===----------------------------------------------------------------------===//

TypeAliasTemplateDecl *TypeAliasTemplateDecl::Create(ASTContext &C,
                                                     DeclContext *DC,
                                                     SourceLocation L,
                                                     DeclarationName Name,
                                                  TemplateParameterList *Params,
                                                     NamedDecl *Decl) {
  AdoptTemplateParameterList(Params, DC);
  return new (C) TypeAliasTemplateDecl(DC, L, Name, Params, Decl);
}

TypeAliasTemplateDecl *TypeAliasTemplateDecl::Create(ASTContext &C,
                                                     EmptyShell) {
  return new (C) TypeAliasTemplateDecl(0, SourceLocation(), DeclarationName(),
                                       0, 0);
}

void TypeAliasTemplateDecl::DeallocateCommon(void *Ptr) {
  static_cast<Common *>(Ptr)->~Common();
}
RedeclarableTemplateDecl::CommonBase *
TypeAliasTemplateDecl::newCommon(ASTContext &C) {
  Common *CommonPtr = new (C) Common;
  C.AddDeallocation(DeallocateCommon, CommonPtr);
  return CommonPtr;
}

