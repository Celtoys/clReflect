//===--- ExprCXX.cpp - (C++) Expression AST Node Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Expr class declared in ExprCXX.h
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/IdentifierTable.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/TypeLoc.h"
using namespace clang;


//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

QualType CXXTypeidExpr::getTypeOperand() const {
  assert(isTypeOperand() && "Cannot call getTypeOperand for typeid(expr)");
  return Operand.get<TypeSourceInfo *>()->getType().getNonReferenceType()
                                                        .getUnqualifiedType();
}

QualType CXXUuidofExpr::getTypeOperand() const {
  assert(isTypeOperand() && "Cannot call getTypeOperand for __uuidof(expr)");
  return Operand.get<TypeSourceInfo *>()->getType().getNonReferenceType()
                                                        .getUnqualifiedType();
}

// CXXScalarValueInitExpr
SourceRange CXXScalarValueInitExpr::getSourceRange() const {
  SourceLocation Start = RParenLoc;
  if (TypeInfo)
    Start = TypeInfo->getTypeLoc().getBeginLoc();
  return SourceRange(Start, RParenLoc);
}

// CXXNewExpr
CXXNewExpr::CXXNewExpr(ASTContext &C, bool globalNew, FunctionDecl *operatorNew,
                       Expr **placementArgs, unsigned numPlaceArgs,
                       SourceRange TypeIdParens, Expr *arraySize,
                       CXXConstructorDecl *constructor, bool initializer,
                       Expr **constructorArgs, unsigned numConsArgs,
                       bool HadMultipleCandidates,
                       FunctionDecl *operatorDelete,
                       bool usualArrayDeleteWantsSize, QualType ty,
                       TypeSourceInfo *AllocatedTypeInfo,
                       SourceLocation startLoc, SourceLocation endLoc,
                       SourceLocation constructorLParen,
                       SourceLocation constructorRParen)
  : Expr(CXXNewExprClass, ty, VK_RValue, OK_Ordinary,
         ty->isDependentType(), ty->isDependentType(),
         ty->isInstantiationDependentType(),
         ty->containsUnexpandedParameterPack()),
    GlobalNew(globalNew), Initializer(initializer),
    UsualArrayDeleteWantsSize(usualArrayDeleteWantsSize),
    HadMultipleCandidates(HadMultipleCandidates),
    SubExprs(0), OperatorNew(operatorNew),
    OperatorDelete(operatorDelete), Constructor(constructor),
    AllocatedTypeInfo(AllocatedTypeInfo), TypeIdParens(TypeIdParens),
    StartLoc(startLoc), EndLoc(endLoc), ConstructorLParen(constructorLParen),
    ConstructorRParen(constructorRParen) {
  AllocateArgsArray(C, arraySize != 0, numPlaceArgs, numConsArgs);
  unsigned i = 0;
  if (Array) {
    if (arraySize->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    
    if (arraySize->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    SubExprs[i++] = arraySize;
  }

  for (unsigned j = 0; j < NumPlacementArgs; ++j) {
    if (placementArgs[j]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (placementArgs[j]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    SubExprs[i++] = placementArgs[j];
  }

  for (unsigned j = 0; j < NumConstructorArgs; ++j) {
    if (constructorArgs[j]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (constructorArgs[j]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    SubExprs[i++] = constructorArgs[j];
  }
}

void CXXNewExpr::AllocateArgsArray(ASTContext &C, bool isArray,
                                   unsigned numPlaceArgs, unsigned numConsArgs){
  assert(SubExprs == 0 && "SubExprs already allocated");
  Array = isArray;
  NumPlacementArgs = numPlaceArgs;
  NumConstructorArgs = numConsArgs; 
  
  unsigned TotalSize = Array + NumPlacementArgs + NumConstructorArgs;
  SubExprs = new (C) Stmt*[TotalSize];
}

bool CXXNewExpr::shouldNullCheckAllocation(ASTContext &Ctx) const {
  return getOperatorNew()->getType()->
    castAs<FunctionProtoType>()->isNothrow(Ctx);
}

// CXXDeleteExpr
QualType CXXDeleteExpr::getDestroyedType() const {
  const Expr *Arg = getArgument();
  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg)) {
    if (ICE->getCastKind() != CK_UserDefinedConversion &&
        ICE->getType()->isVoidPointerType())
      Arg = ICE->getSubExpr();
    else
      break;
  }
  // The type-to-delete may not be a pointer if it's a dependent type.
  const QualType ArgType = Arg->getType();

  if (ArgType->isDependentType() && !ArgType->isPointerType())
    return QualType();

  return ArgType->getAs<PointerType>()->getPointeeType();
}

// CXXPseudoDestructorExpr
PseudoDestructorTypeStorage::PseudoDestructorTypeStorage(TypeSourceInfo *Info)
 : Type(Info) 
{
  Location = Info->getTypeLoc().getLocalSourceRange().getBegin();
}

CXXPseudoDestructorExpr::CXXPseudoDestructorExpr(ASTContext &Context,
                Expr *Base, bool isArrow, SourceLocation OperatorLoc,
                NestedNameSpecifierLoc QualifierLoc, TypeSourceInfo *ScopeType, 
                SourceLocation ColonColonLoc, SourceLocation TildeLoc, 
                PseudoDestructorTypeStorage DestroyedType)
  : Expr(CXXPseudoDestructorExprClass,
         Context.getPointerType(Context.getFunctionType(Context.VoidTy, 0, 0,
                                         FunctionProtoType::ExtProtoInfo())),
         VK_RValue, OK_Ordinary,
         /*isTypeDependent=*/(Base->isTypeDependent() ||
           (DestroyedType.getTypeSourceInfo() &&
            DestroyedType.getTypeSourceInfo()->getType()->isDependentType())),
         /*isValueDependent=*/Base->isValueDependent(),
         (Base->isInstantiationDependent() ||
          (QualifierLoc &&
           QualifierLoc.getNestedNameSpecifier()->isInstantiationDependent()) ||
          (ScopeType &&
           ScopeType->getType()->isInstantiationDependentType()) ||
          (DestroyedType.getTypeSourceInfo() &&
           DestroyedType.getTypeSourceInfo()->getType()
                                             ->isInstantiationDependentType())),
         // ContainsUnexpandedParameterPack
         (Base->containsUnexpandedParameterPack() ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()
                                        ->containsUnexpandedParameterPack()) ||
          (ScopeType && 
           ScopeType->getType()->containsUnexpandedParameterPack()) ||
          (DestroyedType.getTypeSourceInfo() &&
           DestroyedType.getTypeSourceInfo()->getType()
                                   ->containsUnexpandedParameterPack()))),
    Base(static_cast<Stmt *>(Base)), IsArrow(isArrow),
    OperatorLoc(OperatorLoc), QualifierLoc(QualifierLoc),
    ScopeType(ScopeType), ColonColonLoc(ColonColonLoc), TildeLoc(TildeLoc),
    DestroyedType(DestroyedType) { }

QualType CXXPseudoDestructorExpr::getDestroyedType() const {
  if (TypeSourceInfo *TInfo = DestroyedType.getTypeSourceInfo())
    return TInfo->getType();
  
  return QualType();
}

SourceRange CXXPseudoDestructorExpr::getSourceRange() const {
  SourceLocation End = DestroyedType.getLocation();
  if (TypeSourceInfo *TInfo = DestroyedType.getTypeSourceInfo())
    End = TInfo->getTypeLoc().getLocalSourceRange().getEnd();
  return SourceRange(Base->getLocStart(), End);
}


// UnresolvedLookupExpr
UnresolvedLookupExpr *
UnresolvedLookupExpr::Create(ASTContext &C, 
                             CXXRecordDecl *NamingClass,
                             NestedNameSpecifierLoc QualifierLoc,
                             const DeclarationNameInfo &NameInfo,
                             bool ADL,
                             const TemplateArgumentListInfo &Args,
                             UnresolvedSetIterator Begin, 
                             UnresolvedSetIterator End) 
{
  void *Mem = C.Allocate(sizeof(UnresolvedLookupExpr) + 
                         ASTTemplateArgumentListInfo::sizeFor(Args));
  return new (Mem) UnresolvedLookupExpr(C, NamingClass, QualifierLoc, NameInfo,
                                        ADL, /*Overload*/ true, &Args,
                                        Begin, End, /*StdIsAssociated=*/false);
}

UnresolvedLookupExpr *
UnresolvedLookupExpr::CreateEmpty(ASTContext &C, bool HasExplicitTemplateArgs, 
                                  unsigned NumTemplateArgs) {
  std::size_t size = sizeof(UnresolvedLookupExpr);
  if (HasExplicitTemplateArgs)
    size += ASTTemplateArgumentListInfo::sizeFor(NumTemplateArgs);

  void *Mem = C.Allocate(size, llvm::alignOf<UnresolvedLookupExpr>());
  UnresolvedLookupExpr *E = new (Mem) UnresolvedLookupExpr(EmptyShell());
  E->HasExplicitTemplateArgs = HasExplicitTemplateArgs;
  return E;
}

OverloadExpr::OverloadExpr(StmtClass K, ASTContext &C, 
                           NestedNameSpecifierLoc QualifierLoc,
                           const DeclarationNameInfo &NameInfo,
                           const TemplateArgumentListInfo *TemplateArgs,
                           UnresolvedSetIterator Begin, 
                           UnresolvedSetIterator End,
                           bool KnownDependent,
                           bool KnownInstantiationDependent,
                           bool KnownContainsUnexpandedParameterPack)
  : Expr(K, C.OverloadTy, VK_LValue, OK_Ordinary, KnownDependent, 
         KnownDependent,
         (KnownInstantiationDependent ||
          NameInfo.isInstantiationDependent() ||
          (QualifierLoc &&
           QualifierLoc.getNestedNameSpecifier()->isInstantiationDependent())),
         (KnownContainsUnexpandedParameterPack ||
          NameInfo.containsUnexpandedParameterPack() ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()
                                      ->containsUnexpandedParameterPack()))),
    Results(0), NumResults(End - Begin), NameInfo(NameInfo), 
    QualifierLoc(QualifierLoc), HasExplicitTemplateArgs(TemplateArgs != 0)
{
  NumResults = End - Begin;
  if (NumResults) {
    // Determine whether this expression is type-dependent.
    for (UnresolvedSetImpl::const_iterator I = Begin; I != End; ++I) {
      if ((*I)->getDeclContext()->isDependentContext() ||
          isa<UnresolvedUsingValueDecl>(*I)) {
        ExprBits.TypeDependent = true;
        ExprBits.ValueDependent = true;
      }
    }

    Results = static_cast<DeclAccessPair *>(
                                C.Allocate(sizeof(DeclAccessPair) * NumResults, 
                                           llvm::alignOf<DeclAccessPair>()));
    memcpy(Results, &*Begin.getIterator(), 
           NumResults * sizeof(DeclAccessPair));
  }

  // If we have explicit template arguments, check for dependent
  // template arguments and whether they contain any unexpanded pack
  // expansions.
  if (TemplateArgs) {
    bool Dependent = false;
    bool InstantiationDependent = false;
    bool ContainsUnexpandedParameterPack = false;
    getExplicitTemplateArgs().initializeFrom(*TemplateArgs, Dependent,
                                             InstantiationDependent,
                                             ContainsUnexpandedParameterPack);

    if (Dependent) {
      ExprBits.TypeDependent = true;
      ExprBits.ValueDependent = true;
    }
    if (InstantiationDependent)
      ExprBits.InstantiationDependent = true;
    if (ContainsUnexpandedParameterPack)
      ExprBits.ContainsUnexpandedParameterPack = true;
  }

  if (isTypeDependent())
    setType(C.DependentTy);
}

void OverloadExpr::initializeResults(ASTContext &C,
                                     UnresolvedSetIterator Begin,
                                     UnresolvedSetIterator End) {
  assert(Results == 0 && "Results already initialized!");
  NumResults = End - Begin;
  if (NumResults) {
     Results = static_cast<DeclAccessPair *>(
                               C.Allocate(sizeof(DeclAccessPair) * NumResults,
 
                                          llvm::alignOf<DeclAccessPair>()));
     memcpy(Results, &*Begin.getIterator(), 
            NumResults * sizeof(DeclAccessPair));
  }
}

CXXRecordDecl *OverloadExpr::getNamingClass() const {
  if (isa<UnresolvedLookupExpr>(this))
    return cast<UnresolvedLookupExpr>(this)->getNamingClass();
  else
    return cast<UnresolvedMemberExpr>(this)->getNamingClass();
}

// DependentScopeDeclRefExpr
DependentScopeDeclRefExpr::DependentScopeDeclRefExpr(QualType T,
                            NestedNameSpecifierLoc QualifierLoc,
                            const DeclarationNameInfo &NameInfo,
                            const TemplateArgumentListInfo *Args)
  : Expr(DependentScopeDeclRefExprClass, T, VK_LValue, OK_Ordinary,
         true, true,
         (NameInfo.isInstantiationDependent() ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()->isInstantiationDependent())),
         (NameInfo.containsUnexpandedParameterPack() ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()
                            ->containsUnexpandedParameterPack()))),
    QualifierLoc(QualifierLoc), NameInfo(NameInfo), 
    HasExplicitTemplateArgs(Args != 0)
{
  if (Args) {
    bool Dependent = true;
    bool InstantiationDependent = true;
    bool ContainsUnexpandedParameterPack
      = ExprBits.ContainsUnexpandedParameterPack;

    reinterpret_cast<ASTTemplateArgumentListInfo*>(this+1)
      ->initializeFrom(*Args, Dependent, InstantiationDependent,
                       ContainsUnexpandedParameterPack);
    
    ExprBits.ContainsUnexpandedParameterPack = ContainsUnexpandedParameterPack;
  }
}

DependentScopeDeclRefExpr *
DependentScopeDeclRefExpr::Create(ASTContext &C,
                                  NestedNameSpecifierLoc QualifierLoc,
                                  const DeclarationNameInfo &NameInfo,
                                  const TemplateArgumentListInfo *Args) {
  std::size_t size = sizeof(DependentScopeDeclRefExpr);
  if (Args)
    size += ASTTemplateArgumentListInfo::sizeFor(*Args);
  void *Mem = C.Allocate(size);
  return new (Mem) DependentScopeDeclRefExpr(C.DependentTy, QualifierLoc, 
                                             NameInfo, Args);
}

DependentScopeDeclRefExpr *
DependentScopeDeclRefExpr::CreateEmpty(ASTContext &C,
                                       bool HasExplicitTemplateArgs,
                                       unsigned NumTemplateArgs) {
  std::size_t size = sizeof(DependentScopeDeclRefExpr);
  if (HasExplicitTemplateArgs)
    size += ASTTemplateArgumentListInfo::sizeFor(NumTemplateArgs);
  void *Mem = C.Allocate(size);
  DependentScopeDeclRefExpr *E 
    = new (Mem) DependentScopeDeclRefExpr(QualType(), NestedNameSpecifierLoc(),
                                          DeclarationNameInfo(), 0);
  E->HasExplicitTemplateArgs = HasExplicitTemplateArgs;
  return E;
}

SourceRange CXXConstructExpr::getSourceRange() const {
  if (isa<CXXTemporaryObjectExpr>(this))
    return cast<CXXTemporaryObjectExpr>(this)->getSourceRange();

  if (ParenRange.isValid())
    return SourceRange(Loc, ParenRange.getEnd());

  SourceLocation End = Loc;
  for (unsigned I = getNumArgs(); I > 0; --I) {
    const Expr *Arg = getArg(I-1);
    if (!Arg->isDefaultArgument()) {
      SourceLocation NewEnd = Arg->getLocEnd();
      if (NewEnd.isValid()) {
        End = NewEnd;
        break;
      }
    }
  }

  return SourceRange(Loc, End);
}

SourceRange CXXOperatorCallExpr::getSourceRange() const {
  OverloadedOperatorKind Kind = getOperator();
  if (Kind == OO_PlusPlus || Kind == OO_MinusMinus) {
    if (getNumArgs() == 1)
      // Prefix operator
      return SourceRange(getOperatorLoc(),
                         getArg(0)->getSourceRange().getEnd());
    else
      // Postfix operator
      return SourceRange(getArg(0)->getSourceRange().getBegin(),
                         getOperatorLoc());
  } else if (Kind == OO_Arrow) {
    return getArg(0)->getSourceRange();
  } else if (Kind == OO_Call) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (Kind == OO_Subscript) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (getNumArgs() == 1) {
    return SourceRange(getOperatorLoc(), getArg(0)->getSourceRange().getEnd());
  } else if (getNumArgs() == 2) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(),
                       getArg(1)->getSourceRange().getEnd());
  } else {
    return SourceRange();
  }
}

Expr *CXXMemberCallExpr::getImplicitObjectArgument() const {
  if (const MemberExpr *MemExpr = 
        dyn_cast<MemberExpr>(getCallee()->IgnoreParens()))
    return MemExpr->getBase();

  // FIXME: Will eventually need to cope with member pointers.
  return 0;
}

CXXMethodDecl *CXXMemberCallExpr::getMethodDecl() const {
  if (const MemberExpr *MemExpr = 
      dyn_cast<MemberExpr>(getCallee()->IgnoreParens()))
    return cast<CXXMethodDecl>(MemExpr->getMemberDecl());

  // FIXME: Will eventually need to cope with member pointers.
  return 0;
}


CXXRecordDecl *CXXMemberCallExpr::getRecordDecl() {
  Expr* ThisArg = getImplicitObjectArgument();
  if (!ThisArg)
    return 0;

  if (ThisArg->getType()->isAnyPointerType())
    return ThisArg->getType()->getPointeeType()->getAsCXXRecordDecl();

  return ThisArg->getType()->getAsCXXRecordDecl();
}


//===----------------------------------------------------------------------===//
//  Named casts
//===----------------------------------------------------------------------===//

/// getCastName - Get the name of the C++ cast being used, e.g.,
/// "static_cast", "dynamic_cast", "reinterpret_cast", or
/// "const_cast". The returned pointer must not be freed.
const char *CXXNamedCastExpr::getCastName() const {
  switch (getStmtClass()) {
  case CXXStaticCastExprClass:      return "static_cast";
  case CXXDynamicCastExprClass:     return "dynamic_cast";
  case CXXReinterpretCastExprClass: return "reinterpret_cast";
  case CXXConstCastExprClass:       return "const_cast";
  default:                          return "<invalid cast>";
  }
}

CXXStaticCastExpr *CXXStaticCastExpr::Create(ASTContext &C, QualType T,
                                             ExprValueKind VK,
                                             CastKind K, Expr *Op,
                                             const CXXCastPath *BasePath,
                                             TypeSourceInfo *WrittenTy,
                                             SourceLocation L, 
                                             SourceLocation RParenLoc) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer = C.Allocate(sizeof(CXXStaticCastExpr)
                            + PathSize * sizeof(CXXBaseSpecifier*));
  CXXStaticCastExpr *E =
    new (Buffer) CXXStaticCastExpr(T, VK, K, Op, PathSize, WrittenTy, L,
                                   RParenLoc);
  if (PathSize) E->setCastPath(*BasePath);
  return E;
}

CXXStaticCastExpr *CXXStaticCastExpr::CreateEmpty(ASTContext &C,
                                                  unsigned PathSize) {
  void *Buffer =
    C.Allocate(sizeof(CXXStaticCastExpr) + PathSize * sizeof(CXXBaseSpecifier*));
  return new (Buffer) CXXStaticCastExpr(EmptyShell(), PathSize);
}

CXXDynamicCastExpr *CXXDynamicCastExpr::Create(ASTContext &C, QualType T,
                                               ExprValueKind VK,
                                               CastKind K, Expr *Op,
                                               const CXXCastPath *BasePath,
                                               TypeSourceInfo *WrittenTy,
                                               SourceLocation L, 
                                               SourceLocation RParenLoc) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer = C.Allocate(sizeof(CXXDynamicCastExpr)
                            + PathSize * sizeof(CXXBaseSpecifier*));
  CXXDynamicCastExpr *E =
    new (Buffer) CXXDynamicCastExpr(T, VK, K, Op, PathSize, WrittenTy, L,
                                    RParenLoc);
  if (PathSize) E->setCastPath(*BasePath);
  return E;
}

CXXDynamicCastExpr *CXXDynamicCastExpr::CreateEmpty(ASTContext &C,
                                                    unsigned PathSize) {
  void *Buffer =
    C.Allocate(sizeof(CXXDynamicCastExpr) + PathSize * sizeof(CXXBaseSpecifier*));
  return new (Buffer) CXXDynamicCastExpr(EmptyShell(), PathSize);
}

/// isAlwaysNull - Return whether the result of the dynamic_cast is proven
/// to always be null. For example:
///
/// struct A { };
/// struct B final : A { };
/// struct C { };
///
/// C *f(B* b) { return dynamic_cast<C*>(b); }
bool CXXDynamicCastExpr::isAlwaysNull() const
{
  QualType SrcType = getSubExpr()->getType();
  QualType DestType = getType();

  if (const PointerType *SrcPTy = SrcType->getAs<PointerType>()) {
    SrcType = SrcPTy->getPointeeType();
    DestType = DestType->castAs<PointerType>()->getPointeeType();
  }

  const CXXRecordDecl *SrcRD = 
    cast<CXXRecordDecl>(SrcType->castAs<RecordType>()->getDecl());

  if (!SrcRD->hasAttr<FinalAttr>())
    return false;

  const CXXRecordDecl *DestRD = 
    cast<CXXRecordDecl>(DestType->castAs<RecordType>()->getDecl());

  return !DestRD->isDerivedFrom(SrcRD);
}

CXXReinterpretCastExpr *
CXXReinterpretCastExpr::Create(ASTContext &C, QualType T, ExprValueKind VK,
                               CastKind K, Expr *Op,
                               const CXXCastPath *BasePath,
                               TypeSourceInfo *WrittenTy, SourceLocation L, 
                               SourceLocation RParenLoc) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer =
    C.Allocate(sizeof(CXXReinterpretCastExpr) + PathSize * sizeof(CXXBaseSpecifier*));
  CXXReinterpretCastExpr *E =
    new (Buffer) CXXReinterpretCastExpr(T, VK, K, Op, PathSize, WrittenTy, L,
                                        RParenLoc);
  if (PathSize) E->setCastPath(*BasePath);
  return E;
}

CXXReinterpretCastExpr *
CXXReinterpretCastExpr::CreateEmpty(ASTContext &C, unsigned PathSize) {
  void *Buffer = C.Allocate(sizeof(CXXReinterpretCastExpr)
                            + PathSize * sizeof(CXXBaseSpecifier*));
  return new (Buffer) CXXReinterpretCastExpr(EmptyShell(), PathSize);
}

CXXConstCastExpr *CXXConstCastExpr::Create(ASTContext &C, QualType T,
                                           ExprValueKind VK, Expr *Op,
                                           TypeSourceInfo *WrittenTy,
                                           SourceLocation L, 
                                           SourceLocation RParenLoc) {
  return new (C) CXXConstCastExpr(T, VK, Op, WrittenTy, L, RParenLoc);
}

CXXConstCastExpr *CXXConstCastExpr::CreateEmpty(ASTContext &C) {
  return new (C) CXXConstCastExpr(EmptyShell());
}

CXXFunctionalCastExpr *
CXXFunctionalCastExpr::Create(ASTContext &C, QualType T, ExprValueKind VK,
                              TypeSourceInfo *Written, SourceLocation L,
                              CastKind K, Expr *Op, const CXXCastPath *BasePath,
                               SourceLocation R) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer = C.Allocate(sizeof(CXXFunctionalCastExpr)
                            + PathSize * sizeof(CXXBaseSpecifier*));
  CXXFunctionalCastExpr *E =
    new (Buffer) CXXFunctionalCastExpr(T, VK, Written, L, K, Op, PathSize, R);
  if (PathSize) E->setCastPath(*BasePath);
  return E;
}

CXXFunctionalCastExpr *
CXXFunctionalCastExpr::CreateEmpty(ASTContext &C, unsigned PathSize) {
  void *Buffer = C.Allocate(sizeof(CXXFunctionalCastExpr)
                            + PathSize * sizeof(CXXBaseSpecifier*));
  return new (Buffer) CXXFunctionalCastExpr(EmptyShell(), PathSize);
}


CXXDefaultArgExpr *
CXXDefaultArgExpr::Create(ASTContext &C, SourceLocation Loc, 
                          ParmVarDecl *Param, Expr *SubExpr) {
  void *Mem = C.Allocate(sizeof(CXXDefaultArgExpr) + sizeof(Stmt *));
  return new (Mem) CXXDefaultArgExpr(CXXDefaultArgExprClass, Loc, Param, 
                                     SubExpr);
}

CXXTemporary *CXXTemporary::Create(ASTContext &C,
                                   const CXXDestructorDecl *Destructor) {
  return new (C) CXXTemporary(Destructor);
}

CXXBindTemporaryExpr *CXXBindTemporaryExpr::Create(ASTContext &C,
                                                   CXXTemporary *Temp,
                                                   Expr* SubExpr) {
  assert(SubExpr->getType()->isRecordType() &&
         "Expression bound to a temporary must have record type!");

  return new (C) CXXBindTemporaryExpr(Temp, SubExpr);
}

CXXTemporaryObjectExpr::CXXTemporaryObjectExpr(ASTContext &C,
                                               CXXConstructorDecl *Cons,
                                               TypeSourceInfo *Type,
                                               Expr **Args,
                                               unsigned NumArgs,
                                               SourceRange parenRange,
                                               bool HadMultipleCandidates,
                                               bool ZeroInitialization)
  : CXXConstructExpr(C, CXXTemporaryObjectExprClass, 
                     Type->getType().getNonReferenceType(), 
                     Type->getTypeLoc().getBeginLoc(),
                     Cons, false, Args, NumArgs,
                     HadMultipleCandidates, ZeroInitialization,
                     CXXConstructExpr::CK_Complete, parenRange),
    Type(Type) {
}

SourceRange CXXTemporaryObjectExpr::getSourceRange() const {
  return SourceRange(Type->getTypeLoc().getBeginLoc(),
                     getParenRange().getEnd());
}

CXXConstructExpr *CXXConstructExpr::Create(ASTContext &C, QualType T,
                                           SourceLocation Loc,
                                           CXXConstructorDecl *D, bool Elidable,
                                           Expr **Args, unsigned NumArgs,
                                           bool HadMultipleCandidates,
                                           bool ZeroInitialization,
                                           ConstructionKind ConstructKind,
                                           SourceRange ParenRange) {
  return new (C) CXXConstructExpr(C, CXXConstructExprClass, T, Loc, D, 
                                  Elidable, Args, NumArgs,
                                  HadMultipleCandidates, ZeroInitialization,
                                  ConstructKind, ParenRange);
}

CXXConstructExpr::CXXConstructExpr(ASTContext &C, StmtClass SC, QualType T,
                                   SourceLocation Loc,
                                   CXXConstructorDecl *D, bool elidable,
                                   Expr **args, unsigned numargs,
                                   bool HadMultipleCandidates,
                                   bool ZeroInitialization,
                                   ConstructionKind ConstructKind,
                                   SourceRange ParenRange)
  : Expr(SC, T, VK_RValue, OK_Ordinary,
         T->isDependentType(), T->isDependentType(),
         T->isInstantiationDependentType(),
         T->containsUnexpandedParameterPack()),
    Constructor(D), Loc(Loc), ParenRange(ParenRange),  NumArgs(numargs),
    Elidable(elidable), HadMultipleCandidates(HadMultipleCandidates),
    ZeroInitialization(ZeroInitialization),
    ConstructKind(ConstructKind), Args(0)
{
  if (NumArgs) {
    Args = new (C) Stmt*[NumArgs];
    
    for (unsigned i = 0; i != NumArgs; ++i) {
      assert(args[i] && "NULL argument in CXXConstructExpr");

      if (args[i]->isValueDependent())
        ExprBits.ValueDependent = true;
      if (args[i]->isInstantiationDependent())
        ExprBits.InstantiationDependent = true;
      if (args[i]->containsUnexpandedParameterPack())
        ExprBits.ContainsUnexpandedParameterPack = true;
  
      Args[i] = args[i];
    }
  }
}

ExprWithCleanups::ExprWithCleanups(ASTContext &C,
                                   Expr *subexpr,
                                   CXXTemporary **temps,
                                   unsigned numtemps)
  : Expr(ExprWithCleanupsClass, subexpr->getType(),
         subexpr->getValueKind(), subexpr->getObjectKind(),
         subexpr->isTypeDependent(), subexpr->isValueDependent(),
         subexpr->isInstantiationDependent(),
         subexpr->containsUnexpandedParameterPack()),
    SubExpr(subexpr), Temps(0), NumTemps(0) {
  if (numtemps) {
    setNumTemporaries(C, numtemps);
    for (unsigned i = 0; i != numtemps; ++i)
      Temps[i] = temps[i];
  }
}

void ExprWithCleanups::setNumTemporaries(ASTContext &C, unsigned N) {
  assert(Temps == 0 && "Cannot resize with this");
  NumTemps = N;
  Temps = new (C) CXXTemporary*[NumTemps];
}


ExprWithCleanups *ExprWithCleanups::Create(ASTContext &C,
                                           Expr *SubExpr,
                                           CXXTemporary **Temps,
                                           unsigned NumTemps) {
  return new (C) ExprWithCleanups(C, SubExpr, Temps, NumTemps);
}

CXXUnresolvedConstructExpr::CXXUnresolvedConstructExpr(TypeSourceInfo *Type,
                                                 SourceLocation LParenLoc,
                                                 Expr **Args,
                                                 unsigned NumArgs,
                                                 SourceLocation RParenLoc)
  : Expr(CXXUnresolvedConstructExprClass, 
         Type->getType().getNonReferenceType(),
         (Type->getType()->isLValueReferenceType() ? VK_LValue
          :Type->getType()->isRValueReferenceType()? VK_XValue
          :VK_RValue),
         OK_Ordinary,
         Type->getType()->isDependentType(), true, true,
         Type->getType()->containsUnexpandedParameterPack()),
    Type(Type),
    LParenLoc(LParenLoc),
    RParenLoc(RParenLoc),
    NumArgs(NumArgs) {
  Stmt **StoredArgs = reinterpret_cast<Stmt **>(this + 1);
  for (unsigned I = 0; I != NumArgs; ++I) {
    if (Args[I]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    StoredArgs[I] = Args[I];
  }
}

CXXUnresolvedConstructExpr *
CXXUnresolvedConstructExpr::Create(ASTContext &C,
                                   TypeSourceInfo *Type,
                                   SourceLocation LParenLoc,
                                   Expr **Args,
                                   unsigned NumArgs,
                                   SourceLocation RParenLoc) {
  void *Mem = C.Allocate(sizeof(CXXUnresolvedConstructExpr) +
                         sizeof(Expr *) * NumArgs);
  return new (Mem) CXXUnresolvedConstructExpr(Type, LParenLoc,
                                              Args, NumArgs, RParenLoc);
}

CXXUnresolvedConstructExpr *
CXXUnresolvedConstructExpr::CreateEmpty(ASTContext &C, unsigned NumArgs) {
  Stmt::EmptyShell Empty;
  void *Mem = C.Allocate(sizeof(CXXUnresolvedConstructExpr) +
                         sizeof(Expr *) * NumArgs);
  return new (Mem) CXXUnresolvedConstructExpr(Empty, NumArgs);
}

SourceRange CXXUnresolvedConstructExpr::getSourceRange() const {
  return SourceRange(Type->getTypeLoc().getBeginLoc(), RParenLoc);
}

CXXDependentScopeMemberExpr::CXXDependentScopeMemberExpr(ASTContext &C,
                                                 Expr *Base, QualType BaseType,
                                                 bool IsArrow,
                                                 SourceLocation OperatorLoc,
                                           NestedNameSpecifierLoc QualifierLoc,
                                          NamedDecl *FirstQualifierFoundInScope,
                                          DeclarationNameInfo MemberNameInfo,
                                   const TemplateArgumentListInfo *TemplateArgs)
  : Expr(CXXDependentScopeMemberExprClass, C.DependentTy,
         VK_LValue, OK_Ordinary, true, true, true,
         ((Base && Base->containsUnexpandedParameterPack()) ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()
                                       ->containsUnexpandedParameterPack()) ||
          MemberNameInfo.containsUnexpandedParameterPack())),
    Base(Base), BaseType(BaseType), IsArrow(IsArrow),
    HasExplicitTemplateArgs(TemplateArgs != 0),
    OperatorLoc(OperatorLoc), QualifierLoc(QualifierLoc), 
    FirstQualifierFoundInScope(FirstQualifierFoundInScope),
    MemberNameInfo(MemberNameInfo) {
  if (TemplateArgs) {
    bool Dependent = true;
    bool InstantiationDependent = true;
    bool ContainsUnexpandedParameterPack = false;
    getExplicitTemplateArgs().initializeFrom(*TemplateArgs, Dependent,
                                             InstantiationDependent,
                                             ContainsUnexpandedParameterPack);
    if (ContainsUnexpandedParameterPack)
      ExprBits.ContainsUnexpandedParameterPack = true;
  }
}

CXXDependentScopeMemberExpr::CXXDependentScopeMemberExpr(ASTContext &C,
                          Expr *Base, QualType BaseType,
                          bool IsArrow,
                          SourceLocation OperatorLoc,
                          NestedNameSpecifierLoc QualifierLoc,
                          NamedDecl *FirstQualifierFoundInScope,
                          DeclarationNameInfo MemberNameInfo)
  : Expr(CXXDependentScopeMemberExprClass, C.DependentTy,
         VK_LValue, OK_Ordinary, true, true, true,
         ((Base && Base->containsUnexpandedParameterPack()) ||
          (QualifierLoc && 
           QualifierLoc.getNestedNameSpecifier()->
                                         containsUnexpandedParameterPack()) ||
          MemberNameInfo.containsUnexpandedParameterPack())),
    Base(Base), BaseType(BaseType), IsArrow(IsArrow),
    HasExplicitTemplateArgs(false), OperatorLoc(OperatorLoc),
    QualifierLoc(QualifierLoc), 
    FirstQualifierFoundInScope(FirstQualifierFoundInScope),
    MemberNameInfo(MemberNameInfo) { }

CXXDependentScopeMemberExpr *
CXXDependentScopeMemberExpr::Create(ASTContext &C,
                                Expr *Base, QualType BaseType, bool IsArrow,
                                SourceLocation OperatorLoc,
                                NestedNameSpecifierLoc QualifierLoc,
                                NamedDecl *FirstQualifierFoundInScope,
                                DeclarationNameInfo MemberNameInfo,
                                const TemplateArgumentListInfo *TemplateArgs) {
  if (!TemplateArgs)
    return new (C) CXXDependentScopeMemberExpr(C, Base, BaseType,
                                               IsArrow, OperatorLoc,
                                               QualifierLoc,
                                               FirstQualifierFoundInScope,
                                               MemberNameInfo);

  std::size_t size = sizeof(CXXDependentScopeMemberExpr);
  if (TemplateArgs)
    size += ASTTemplateArgumentListInfo::sizeFor(*TemplateArgs);

  void *Mem = C.Allocate(size, llvm::alignOf<CXXDependentScopeMemberExpr>());
  return new (Mem) CXXDependentScopeMemberExpr(C, Base, BaseType,
                                               IsArrow, OperatorLoc,
                                               QualifierLoc,
                                               FirstQualifierFoundInScope,
                                               MemberNameInfo, TemplateArgs);
}

CXXDependentScopeMemberExpr *
CXXDependentScopeMemberExpr::CreateEmpty(ASTContext &C,
                                         bool HasExplicitTemplateArgs,
                                         unsigned NumTemplateArgs) {
  if (!HasExplicitTemplateArgs)
    return new (C) CXXDependentScopeMemberExpr(C, 0, QualType(),
                                               0, SourceLocation(), 
                                               NestedNameSpecifierLoc(), 0,
                                               DeclarationNameInfo());

  std::size_t size = sizeof(CXXDependentScopeMemberExpr) +
                     ASTTemplateArgumentListInfo::sizeFor(NumTemplateArgs);
  void *Mem = C.Allocate(size, llvm::alignOf<CXXDependentScopeMemberExpr>());
  CXXDependentScopeMemberExpr *E
    =  new (Mem) CXXDependentScopeMemberExpr(C, 0, QualType(),
                                             0, SourceLocation(), 
                                             NestedNameSpecifierLoc(), 0,
                                             DeclarationNameInfo(), 0);
  E->HasExplicitTemplateArgs = true;
  return E;
}

bool CXXDependentScopeMemberExpr::isImplicitAccess() const {
  if (Base == 0)
    return true;
  
  return cast<Expr>(Base)->isImplicitCXXThis();
}

static bool hasOnlyNonStaticMemberFunctions(UnresolvedSetIterator begin,
                                            UnresolvedSetIterator end) {
  do {
    NamedDecl *decl = *begin;
    if (isa<UnresolvedUsingValueDecl>(decl))
      return false;
    if (isa<UsingShadowDecl>(decl))
      decl = cast<UsingShadowDecl>(decl)->getUnderlyingDecl();

    // Unresolved member expressions should only contain methods and
    // method templates.
    assert(isa<CXXMethodDecl>(decl) || isa<FunctionTemplateDecl>(decl));

    if (isa<FunctionTemplateDecl>(decl))
      decl = cast<FunctionTemplateDecl>(decl)->getTemplatedDecl();
    if (cast<CXXMethodDecl>(decl)->isStatic())
      return false;
  } while (++begin != end);

  return true;
}

UnresolvedMemberExpr::UnresolvedMemberExpr(ASTContext &C, 
                                           bool HasUnresolvedUsing,
                                           Expr *Base, QualType BaseType,
                                           bool IsArrow,
                                           SourceLocation OperatorLoc,
                                           NestedNameSpecifierLoc QualifierLoc,
                                   const DeclarationNameInfo &MemberNameInfo,
                                   const TemplateArgumentListInfo *TemplateArgs,
                                           UnresolvedSetIterator Begin, 
                                           UnresolvedSetIterator End)
  : OverloadExpr(UnresolvedMemberExprClass, C, QualifierLoc, MemberNameInfo,
                 TemplateArgs, Begin, End,
                 // Dependent
                 ((Base && Base->isTypeDependent()) ||
                  BaseType->isDependentType()),
                 ((Base && Base->isInstantiationDependent()) ||
                   BaseType->isInstantiationDependentType()),
                 // Contains unexpanded parameter pack
                 ((Base && Base->containsUnexpandedParameterPack()) ||
                  BaseType->containsUnexpandedParameterPack())),
    IsArrow(IsArrow), HasUnresolvedUsing(HasUnresolvedUsing),
    Base(Base), BaseType(BaseType), OperatorLoc(OperatorLoc) {

  // Check whether all of the members are non-static member functions,
  // and if so, mark give this bound-member type instead of overload type.
  if (hasOnlyNonStaticMemberFunctions(Begin, End))
    setType(C.BoundMemberTy);
}

bool UnresolvedMemberExpr::isImplicitAccess() const {
  if (Base == 0)
    return true;
  
  return cast<Expr>(Base)->isImplicitCXXThis();
}

UnresolvedMemberExpr *
UnresolvedMemberExpr::Create(ASTContext &C, 
                             bool HasUnresolvedUsing,
                             Expr *Base, QualType BaseType, bool IsArrow,
                             SourceLocation OperatorLoc,
                             NestedNameSpecifierLoc QualifierLoc,
                             const DeclarationNameInfo &MemberNameInfo,
                             const TemplateArgumentListInfo *TemplateArgs,
                             UnresolvedSetIterator Begin, 
                             UnresolvedSetIterator End) {
  std::size_t size = sizeof(UnresolvedMemberExpr);
  if (TemplateArgs)
    size += ASTTemplateArgumentListInfo::sizeFor(*TemplateArgs);

  void *Mem = C.Allocate(size, llvm::alignOf<UnresolvedMemberExpr>());
  return new (Mem) UnresolvedMemberExpr(C, 
                             HasUnresolvedUsing, Base, BaseType,
                             IsArrow, OperatorLoc, QualifierLoc,
                             MemberNameInfo, TemplateArgs, Begin, End);
}

UnresolvedMemberExpr *
UnresolvedMemberExpr::CreateEmpty(ASTContext &C, bool HasExplicitTemplateArgs,
                                  unsigned NumTemplateArgs) {
  std::size_t size = sizeof(UnresolvedMemberExpr);
  if (HasExplicitTemplateArgs)
    size += ASTTemplateArgumentListInfo::sizeFor(NumTemplateArgs);

  void *Mem = C.Allocate(size, llvm::alignOf<UnresolvedMemberExpr>());
  UnresolvedMemberExpr *E = new (Mem) UnresolvedMemberExpr(EmptyShell());
  E->HasExplicitTemplateArgs = HasExplicitTemplateArgs;
  return E;
}

CXXRecordDecl *UnresolvedMemberExpr::getNamingClass() const {
  // Unlike for UnresolvedLookupExpr, it is very easy to re-derive this.

  // If there was a nested name specifier, it names the naming class.
  // It can't be dependent: after all, we were actually able to do the
  // lookup.
  CXXRecordDecl *Record = 0;
  if (getQualifier()) {
    const Type *T = getQualifier()->getAsType();
    assert(T && "qualifier in member expression does not name type");
    Record = T->getAsCXXRecordDecl();
    assert(Record && "qualifier in member expression does not name record");
  }
  // Otherwise the naming class must have been the base class.
  else {
    QualType BaseType = getBaseType().getNonReferenceType();
    if (isArrow()) {
      const PointerType *PT = BaseType->getAs<PointerType>();
      assert(PT && "base of arrow member access is not pointer");
      BaseType = PT->getPointeeType();
    }
    
    Record = BaseType->getAsCXXRecordDecl();
    assert(Record && "base of member expression does not name record");
  }
  
  return Record;
}

SubstNonTypeTemplateParmPackExpr::
SubstNonTypeTemplateParmPackExpr(QualType T, 
                                 NonTypeTemplateParmDecl *Param,
                                 SourceLocation NameLoc,
                                 const TemplateArgument &ArgPack)
  : Expr(SubstNonTypeTemplateParmPackExprClass, T, VK_RValue, OK_Ordinary, 
         true, true, true, true),
    Param(Param), Arguments(ArgPack.pack_begin()), 
    NumArguments(ArgPack.pack_size()), NameLoc(NameLoc) { }

TemplateArgument SubstNonTypeTemplateParmPackExpr::getArgumentPack() const {
  return TemplateArgument(Arguments, NumArguments);
}


