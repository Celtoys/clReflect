//===------ SemaDeclCXX.cpp - Semantic Analysis for C++ Declarations ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ declarations.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include <map>
#include <set>

using namespace clang;

//===----------------------------------------------------------------------===//
// CheckDefaultArgumentVisitor
//===----------------------------------------------------------------------===//

namespace {
  /// CheckDefaultArgumentVisitor - C++ [dcl.fct.default] Traverses
  /// the default argument of a parameter to determine whether it
  /// contains any ill-formed subexpressions. For example, this will
  /// diagnose the use of local variables or parameters within the
  /// default argument expression.
  class CheckDefaultArgumentVisitor
    : public StmtVisitor<CheckDefaultArgumentVisitor, bool> {
    Expr *DefaultArg;
    Sema *S;

  public:
    CheckDefaultArgumentVisitor(Expr *defarg, Sema *s)
      : DefaultArg(defarg), S(s) {}

    bool VisitExpr(Expr *Node);
    bool VisitDeclRefExpr(DeclRefExpr *DRE);
    bool VisitCXXThisExpr(CXXThisExpr *ThisE);
  };

  /// VisitExpr - Visit all of the children of this expression.
  bool CheckDefaultArgumentVisitor::VisitExpr(Expr *Node) {
    bool IsInvalid = false;
    for (Stmt::child_range I = Node->children(); I; ++I)
      IsInvalid |= Visit(*I);
    return IsInvalid;
  }

  /// VisitDeclRefExpr - Visit a reference to a declaration, to
  /// determine whether this declaration can be used in the default
  /// argument expression.
  bool CheckDefaultArgumentVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
    NamedDecl *Decl = DRE->getDecl();
    if (ParmVarDecl *Param = dyn_cast<ParmVarDecl>(Decl)) {
      // C++ [dcl.fct.default]p9
      //   Default arguments are evaluated each time the function is
      //   called. The order of evaluation of function arguments is
      //   unspecified. Consequently, parameters of a function shall not
      //   be used in default argument expressions, even if they are not
      //   evaluated. Parameters of a function declared before a default
      //   argument expression are in scope and can hide namespace and
      //   class member names.
      return S->Diag(DRE->getSourceRange().getBegin(),
                     diag::err_param_default_argument_references_param)
         << Param->getDeclName() << DefaultArg->getSourceRange();
    } else if (VarDecl *VDecl = dyn_cast<VarDecl>(Decl)) {
      // C++ [dcl.fct.default]p7
      //   Local variables shall not be used in default argument
      //   expressions.
      if (VDecl->isLocalVarDecl())
        return S->Diag(DRE->getSourceRange().getBegin(),
                       diag::err_param_default_argument_references_local)
          << VDecl->getDeclName() << DefaultArg->getSourceRange();
    }

    return false;
  }

  /// VisitCXXThisExpr - Visit a C++ "this" expression.
  bool CheckDefaultArgumentVisitor::VisitCXXThisExpr(CXXThisExpr *ThisE) {
    // C++ [dcl.fct.default]p8:
    //   The keyword this shall not be used in a default argument of a
    //   member function.
    return S->Diag(ThisE->getSourceRange().getBegin(),
                   diag::err_param_default_argument_references_this)
               << ThisE->getSourceRange();
  }
}

void Sema::ImplicitExceptionSpecification::CalledDecl(CXXMethodDecl *Method) {
  assert(Context && "ImplicitExceptionSpecification without an ASTContext");
  // If we have an MSAny or unknown spec already, don't bother.
  if (!Method || ComputedEST == EST_MSAny || ComputedEST == EST_Delayed)
    return;

  const FunctionProtoType *Proto
    = Method->getType()->getAs<FunctionProtoType>();

  ExceptionSpecificationType EST = Proto->getExceptionSpecType();

  // If this function can throw any exceptions, make a note of that.
  if (EST == EST_Delayed || EST == EST_MSAny || EST == EST_None) {
    ClearExceptions();
    ComputedEST = EST;
    return;
  }

  // FIXME: If the call to this decl is using any of its default arguments, we
  // need to search them for potentially-throwing calls.

  // If this function has a basic noexcept, it doesn't affect the outcome.
  if (EST == EST_BasicNoexcept)
    return;

  // If we have a throw-all spec at this point, ignore the function.
  if (ComputedEST == EST_None)
    return;

  // If we're still at noexcept(true) and there's a nothrow() callee,
  // change to that specification.
  if (EST == EST_DynamicNone) {
    if (ComputedEST == EST_BasicNoexcept)
      ComputedEST = EST_DynamicNone;
    return;
  }

  // Check out noexcept specs.
  if (EST == EST_ComputedNoexcept) {
    FunctionProtoType::NoexceptResult NR = Proto->getNoexceptSpec(*Context);
    assert(NR != FunctionProtoType::NR_NoNoexcept &&
           "Must have noexcept result for EST_ComputedNoexcept.");
    assert(NR != FunctionProtoType::NR_Dependent &&
           "Should not generate implicit declarations for dependent cases, "
           "and don't know how to handle them anyway.");

    // noexcept(false) -> no spec on the new function
    if (NR == FunctionProtoType::NR_Throw) {
      ClearExceptions();
      ComputedEST = EST_None;
    }
    // noexcept(true) won't change anything either.
    return;
  }

  assert(EST == EST_Dynamic && "EST case not considered earlier.");
  assert(ComputedEST != EST_None &&
         "Shouldn't collect exceptions when throw-all is guaranteed.");
  ComputedEST = EST_Dynamic;
  // Record the exceptions in this function's exception specification.
  for (FunctionProtoType::exception_iterator E = Proto->exception_begin(),
                                          EEnd = Proto->exception_end();
       E != EEnd; ++E)
    if (ExceptionsSeen.insert(Context->getCanonicalType(*E)))
      Exceptions.push_back(*E);
}

void Sema::ImplicitExceptionSpecification::CalledExpr(Expr *E) {
  if (!E || ComputedEST == EST_MSAny || ComputedEST == EST_Delayed)
    return;

  // FIXME:
  //
  // C++0x [except.spec]p14:
  //   [An] implicit exception-specification specifies the type-id T if and
  // only if T is allowed by the exception-specification of a function directly
  // invoked by f's implicit definition; f shall allow all exceptions if any
  // function it directly invokes allows all exceptions, and f shall allow no
  // exceptions if every function it directly invokes allows no exceptions.
  //
  // Note in particular that if an implicit exception-specification is generated
  // for a function containing a throw-expression, that specification can still
  // be noexcept(true).
  //
  // Note also that 'directly invoked' is not defined in the standard, and there
  // is no indication that we should only consider potentially-evaluated calls.
  //
  // Ultimately we should implement the intent of the standard: the exception
  // specification should be the set of exceptions which can be thrown by the
  // implicit definition. For now, we assume that any non-nothrow expression can
  // throw any exception.

  if (E->CanThrow(*Context))
    ComputedEST = EST_None;
}

bool
Sema::SetParamDefaultArgument(ParmVarDecl *Param, Expr *Arg,
                              SourceLocation EqualLoc) {
  if (RequireCompleteType(Param->getLocation(), Param->getType(),
                          diag::err_typecheck_decl_incomplete_type)) {
    Param->setInvalidDecl();
    return true;
  }

  // C++ [dcl.fct.default]p5
  //   A default argument expression is implicitly converted (clause
  //   4) to the parameter type. The default argument expression has
  //   the same semantic constraints as the initializer expression in
  //   a declaration of a variable of the parameter type, using the
  //   copy-initialization semantics (8.5).
  InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
                                                                    Param);
  InitializationKind Kind = InitializationKind::CreateCopy(Param->getLocation(),
                                                           EqualLoc);
  InitializationSequence InitSeq(*this, Entity, Kind, &Arg, 1);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind,
                                      MultiExprArg(*this, &Arg, 1));
  if (Result.isInvalid())
    return true;
  Arg = Result.takeAs<Expr>();

  CheckImplicitConversions(Arg, EqualLoc);
  Arg = MaybeCreateExprWithCleanups(Arg);

  // Okay: add the default argument to the parameter
  Param->setDefaultArg(Arg);

  // We have already instantiated this parameter; provide each of the 
  // instantiations with the uninstantiated default argument.
  UnparsedDefaultArgInstantiationsMap::iterator InstPos
    = UnparsedDefaultArgInstantiations.find(Param);
  if (InstPos != UnparsedDefaultArgInstantiations.end()) {
    for (unsigned I = 0, N = InstPos->second.size(); I != N; ++I)
      InstPos->second[I]->setUninstantiatedDefaultArg(Arg);
    
    // We're done tracking this parameter's instantiations.
    UnparsedDefaultArgInstantiations.erase(InstPos);
  }
  
  return false;
}

/// ActOnParamDefaultArgument - Check whether the default argument
/// provided for a function parameter is well-formed. If so, attach it
/// to the parameter declaration.
void
Sema::ActOnParamDefaultArgument(Decl *param, SourceLocation EqualLoc,
                                Expr *DefaultArg) {
  if (!param || !DefaultArg)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  UnparsedDefaultArgLocs.erase(Param);

  // Default arguments are only permitted in C++
  if (!getLangOptions().CPlusPlus) {
    Diag(EqualLoc, diag::err_param_default_argument)
      << DefaultArg->getSourceRange();
    Param->setInvalidDecl();
    return;
  }

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(DefaultArg, UPPC_DefaultArgument)) {
    Param->setInvalidDecl();
    return;
  }    
      
  // Check that the default argument is well-formed
  CheckDefaultArgumentVisitor DefaultArgChecker(DefaultArg, this);
  if (DefaultArgChecker.Visit(DefaultArg)) {
    Param->setInvalidDecl();
    return;
  }

  SetParamDefaultArgument(Param, DefaultArg, EqualLoc);
}

/// ActOnParamUnparsedDefaultArgument - We've seen a default
/// argument for a function parameter, but we can't parse it yet
/// because we're inside a class definition. Note that this default
/// argument will be parsed later.
void Sema::ActOnParamUnparsedDefaultArgument(Decl *param,
                                             SourceLocation EqualLoc,
                                             SourceLocation ArgLoc) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  if (Param)
    Param->setUnparsedDefaultArg();

  UnparsedDefaultArgLocs[Param] = ArgLoc;
}

/// ActOnParamDefaultArgumentError - Parsing or semantic analysis of
/// the default argument for the parameter param failed.
void Sema::ActOnParamDefaultArgumentError(Decl *param) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);

  Param->setInvalidDecl();

  UnparsedDefaultArgLocs.erase(Param);
}

/// CheckExtraCXXDefaultArguments - Check for any extra default
/// arguments in the declarator, which is not a function declaration
/// or definition and therefore is not permitted to have default
/// arguments. This routine should be invoked for every declarator
/// that is not a function declaration or definition.
void Sema::CheckExtraCXXDefaultArguments(Declarator &D) {
  // C++ [dcl.fct.default]p3
  //   A default argument expression shall be specified only in the
  //   parameter-declaration-clause of a function declaration or in a
  //   template-parameter (14.1). It shall not be specified for a
  //   parameter pack. If it is specified in a
  //   parameter-declaration-clause, it shall not occur within a
  //   declarator or abstract-declarator of a parameter-declaration.
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = D.getTypeObject(i);
    if (chunk.Kind == DeclaratorChunk::Function) {
      for (unsigned argIdx = 0, e = chunk.Fun.NumArgs; argIdx != e; ++argIdx) {
        ParmVarDecl *Param =
          cast<ParmVarDecl>(chunk.Fun.ArgInfo[argIdx].Param);
        if (Param->hasUnparsedDefaultArg()) {
          CachedTokens *Toks = chunk.Fun.ArgInfo[argIdx].DefaultArgTokens;
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << SourceRange((*Toks)[1].getLocation(), Toks->back().getLocation());
          delete Toks;
          chunk.Fun.ArgInfo[argIdx].DefaultArgTokens = 0;
        } else if (Param->getDefaultArg()) {
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << Param->getDefaultArg()->getSourceRange();
          Param->setDefaultArg(0);
        }
      }
    }
  }
}

// MergeCXXFunctionDecl - Merge two declarations of the same C++
// function, once we already know that they have the same
// type. Subroutine of MergeFunctionDecl. Returns true if there was an
// error, false otherwise.
bool Sema::MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old) {
  bool Invalid = false;

  // C++ [dcl.fct.default]p4:
  //   For non-template functions, default arguments can be added in
  //   later declarations of a function in the same
  //   scope. Declarations in different scopes have completely
  //   distinct sets of default arguments. That is, declarations in
  //   inner scopes do not acquire default arguments from
  //   declarations in outer scopes, and vice versa. In a given
  //   function declaration, all parameters subsequent to a
  //   parameter with a default argument shall have default
  //   arguments supplied in this or previous declarations. A
  //   default argument shall not be redefined by a later
  //   declaration (not even to the same value).
  //
  // C++ [dcl.fct.default]p6:
  //   Except for member functions of class templates, the default arguments 
  //   in a member function definition that appears outside of the class 
  //   definition are added to the set of default arguments provided by the 
  //   member function declaration in the class definition.
  for (unsigned p = 0, NumParams = Old->getNumParams(); p < NumParams; ++p) {
    ParmVarDecl *OldParam = Old->getParamDecl(p);
    ParmVarDecl *NewParam = New->getParamDecl(p);

    if (OldParam->hasDefaultArg() && NewParam->hasDefaultArg()) {

      unsigned DiagDefaultParamID =
        diag::err_param_default_argument_redefinition;

      // MSVC accepts that default parameters be redefined for member functions
      // of template class. The new default parameter's value is ignored.
      Invalid = true;
      if (getLangOptions().MicrosoftExt) {
        CXXMethodDecl* MD = dyn_cast<CXXMethodDecl>(New);
        if (MD && MD->getParent()->getDescribedClassTemplate()) {
          // Merge the old default argument into the new parameter.
          NewParam->setHasInheritedDefaultArg();
          if (OldParam->hasUninstantiatedDefaultArg())
            NewParam->setUninstantiatedDefaultArg(
                                      OldParam->getUninstantiatedDefaultArg());
          else
            NewParam->setDefaultArg(OldParam->getInit());
          DiagDefaultParamID = diag::warn_param_default_argument_redefinition;
          Invalid = false;
        }
      }
      
      // FIXME: If we knew where the '=' was, we could easily provide a fix-it 
      // hint here. Alternatively, we could walk the type-source information
      // for NewParam to find the last source location in the type... but it
      // isn't worth the effort right now. This is the kind of test case that
      // is hard to get right:
      //   int f(int);
      //   void g(int (*fp)(int) = f);
      //   void g(int (*fp)(int) = &f);
      Diag(NewParam->getLocation(), DiagDefaultParamID)
        << NewParam->getDefaultArgRange();
      
      // Look for the function declaration where the default argument was
      // actually written, which may be a declaration prior to Old.
      for (FunctionDecl *Older = Old->getPreviousDeclaration();
           Older; Older = Older->getPreviousDeclaration()) {
        if (!Older->getParamDecl(p)->hasDefaultArg())
          break;
        
        OldParam = Older->getParamDecl(p);
      }        
      
      Diag(OldParam->getLocation(), diag::note_previous_definition)
        << OldParam->getDefaultArgRange();
    } else if (OldParam->hasDefaultArg()) {
      // Merge the old default argument into the new parameter.
      // It's important to use getInit() here;  getDefaultArg()
      // strips off any top-level ExprWithCleanups.
      NewParam->setHasInheritedDefaultArg();
      if (OldParam->hasUninstantiatedDefaultArg())
        NewParam->setUninstantiatedDefaultArg(
                                      OldParam->getUninstantiatedDefaultArg());
      else
        NewParam->setDefaultArg(OldParam->getInit());
    } else if (NewParam->hasDefaultArg()) {
      if (New->getDescribedFunctionTemplate()) {
        // Paragraph 4, quoted above, only applies to non-template functions.
        Diag(NewParam->getLocation(),
             diag::err_param_default_argument_template_redecl)
          << NewParam->getDefaultArgRange();
        Diag(Old->getLocation(), diag::note_template_prev_declaration)
          << false;
      } else if (New->getTemplateSpecializationKind()
                   != TSK_ImplicitInstantiation &&
                 New->getTemplateSpecializationKind() != TSK_Undeclared) {
        // C++ [temp.expr.spec]p21:
        //   Default function arguments shall not be specified in a declaration
        //   or a definition for one of the following explicit specializations:
        //     - the explicit specialization of a function template;
        //     - the explicit specialization of a member function template;
        //     - the explicit specialization of a member function of a class 
        //       template where the class template specialization to which the
        //       member function specialization belongs is implicitly 
        //       instantiated.
        Diag(NewParam->getLocation(), diag::err_template_spec_default_arg)
          << (New->getTemplateSpecializationKind() ==TSK_ExplicitSpecialization)
          << New->getDeclName()
          << NewParam->getDefaultArgRange();
      } else if (New->getDeclContext()->isDependentContext()) {
        // C++ [dcl.fct.default]p6 (DR217):
        //   Default arguments for a member function of a class template shall 
        //   be specified on the initial declaration of the member function 
        //   within the class template.
        //
        // Reading the tea leaves a bit in DR217 and its reference to DR205 
        // leads me to the conclusion that one cannot add default function 
        // arguments for an out-of-line definition of a member function of a 
        // dependent type.
        int WhichKind = 2;
        if (CXXRecordDecl *Record 
              = dyn_cast<CXXRecordDecl>(New->getDeclContext())) {
          if (Record->getDescribedClassTemplate())
            WhichKind = 0;
          else if (isa<ClassTemplatePartialSpecializationDecl>(Record))
            WhichKind = 1;
          else
            WhichKind = 2;
        }
        
        Diag(NewParam->getLocation(), 
             diag::err_param_default_argument_member_template_redecl)
          << WhichKind
          << NewParam->getDefaultArgRange();
      } else if (CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(New)) {
        CXXSpecialMember NewSM = getSpecialMember(Ctor),
                         OldSM = getSpecialMember(cast<CXXConstructorDecl>(Old));
        if (NewSM != OldSM) {
          Diag(NewParam->getLocation(),diag::warn_default_arg_makes_ctor_special)
            << NewParam->getDefaultArgRange() << NewSM;
          Diag(Old->getLocation(), diag::note_previous_declaration_special)
            << OldSM;
        }
      }
    }
  }

  // C++0x [dcl.constexpr]p1: If any declaration of a function or function
  // template has a constexpr specifier then all its declarations shall
  // contain the constexpr specifier. [Note: An explicit specialization can
  // differ from the template declaration with respect to the constexpr
  // specifier. -- end note]
  //
  // FIXME: Don't reject changes in constexpr in explicit specializations.
  if (New->isConstexpr() != Old->isConstexpr()) {
    Diag(New->getLocation(), diag::err_constexpr_redecl_mismatch)
      << New << New->isConstexpr();
    Diag(Old->getLocation(), diag::note_previous_declaration);
    Invalid = true;
  }

  if (CheckEquivalentExceptionSpec(Old, New))
    Invalid = true;

  return Invalid;
}

/// \brief Merge the exception specifications of two variable declarations.
///
/// This is called when there's a redeclaration of a VarDecl. The function
/// checks if the redeclaration might have an exception specification and
/// validates compatibility and merges the specs if necessary.
void Sema::MergeVarDeclExceptionSpecs(VarDecl *New, VarDecl *Old) {
  // Shortcut if exceptions are disabled.
  if (!getLangOptions().CXXExceptions)
    return;

  assert(Context.hasSameType(New->getType(), Old->getType()) &&
         "Should only be called if types are otherwise the same.");

  QualType NewType = New->getType();
  QualType OldType = Old->getType();

  // We're only interested in pointers and references to functions, as well
  // as pointers to member functions.
  if (const ReferenceType *R = NewType->getAs<ReferenceType>()) {
    NewType = R->getPointeeType();
    OldType = OldType->getAs<ReferenceType>()->getPointeeType();
  } else if (const PointerType *P = NewType->getAs<PointerType>()) {
    NewType = P->getPointeeType();
    OldType = OldType->getAs<PointerType>()->getPointeeType();
  } else if (const MemberPointerType *M = NewType->getAs<MemberPointerType>()) {
    NewType = M->getPointeeType();
    OldType = OldType->getAs<MemberPointerType>()->getPointeeType();
  }

  if (!NewType->isFunctionProtoType())
    return;

  // There's lots of special cases for functions. For function pointers, system
  // libraries are hopefully not as broken so that we don't need these
  // workarounds.
  if (CheckEquivalentExceptionSpec(
        OldType->getAs<FunctionProtoType>(), Old->getLocation(),
        NewType->getAs<FunctionProtoType>(), New->getLocation())) {
    New->setInvalidDecl();
  }
}

/// CheckCXXDefaultArguments - Verify that the default arguments for a
/// function declaration are well-formed according to C++
/// [dcl.fct.default].
void Sema::CheckCXXDefaultArguments(FunctionDecl *FD) {
  unsigned NumParams = FD->getNumParams();
  unsigned p;

  // Find first parameter with a default argument
  for (p = 0; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (Param->hasDefaultArg())
      break;
  }

  // C++ [dcl.fct.default]p4:
  //   In a given function declaration, all parameters
  //   subsequent to a parameter with a default argument shall
  //   have default arguments supplied in this or previous
  //   declarations. A default argument shall not be redefined
  //   by a later declaration (not even to the same value).
  unsigned LastMissingDefaultArg = 0;
  for (; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (!Param->hasDefaultArg()) {
      if (Param->isInvalidDecl())
        /* We already complained about this parameter. */;
      else if (Param->getIdentifier())
        Diag(Param->getLocation(),
             diag::err_param_default_argument_missing_name)
          << Param->getIdentifier();
      else
        Diag(Param->getLocation(),
             diag::err_param_default_argument_missing);

      LastMissingDefaultArg = p;
    }
  }

  if (LastMissingDefaultArg > 0) {
    // Some default arguments were missing. Clear out all of the
    // default arguments up to (and including) the last missing
    // default argument, so that we leave the function parameters
    // in a semantically valid state.
    for (p = 0; p <= LastMissingDefaultArg; ++p) {
      ParmVarDecl *Param = FD->getParamDecl(p);
      if (Param->hasDefaultArg()) {
        Param->setDefaultArg(0);
      }
    }
  }
}

// CheckConstexprParameterTypes - Check whether a function's parameter types
// are all literal types. If so, return true. If not, produce a suitable
// diagnostic depending on @p CCK and return false.
static bool CheckConstexprParameterTypes(Sema &SemaRef, const FunctionDecl *FD,
                                         Sema::CheckConstexprKind CCK) {
  unsigned ArgIndex = 0;
  const FunctionProtoType *FT = FD->getType()->getAs<FunctionProtoType>();
  for (FunctionProtoType::arg_type_iterator i = FT->arg_type_begin(),
       e = FT->arg_type_end(); i != e; ++i, ++ArgIndex) {
    const ParmVarDecl *PD = FD->getParamDecl(ArgIndex);
    SourceLocation ParamLoc = PD->getLocation();
    if (!(*i)->isDependentType() &&
        SemaRef.RequireLiteralType(ParamLoc, *i, CCK == Sema::CCK_Declaration ?
                            SemaRef.PDiag(diag::err_constexpr_non_literal_param)
                                     << ArgIndex+1 << PD->getSourceRange()
                                     << isa<CXXConstructorDecl>(FD) :
                                   SemaRef.PDiag(),
                                   /*AllowIncompleteType*/ true)) {
      if (CCK == Sema::CCK_NoteNonConstexprInstantiation)
        SemaRef.Diag(ParamLoc, diag::note_constexpr_tmpl_non_literal_param)
          << ArgIndex+1 << PD->getSourceRange()
          << isa<CXXConstructorDecl>(FD) << *i;
      return false;
    }
  }
  return true;
}

// CheckConstexprFunctionDecl - Check whether a function declaration satisfies
// the requirements of a constexpr function declaration or a constexpr
// constructor declaration. Return true if it does, false if not.
//
// This implements C++0x [dcl.constexpr]p3,4, as amended by N3308.
//
// \param CCK Specifies whether to produce diagnostics if the function does not
// satisfy the requirements.
bool Sema::CheckConstexprFunctionDecl(const FunctionDecl *NewFD,
                                      CheckConstexprKind CCK) {
  assert((CCK != CCK_NoteNonConstexprInstantiation ||
          (NewFD->getTemplateInstantiationPattern() &&
           NewFD->getTemplateInstantiationPattern()->isConstexpr())) &&
         "only constexpr templates can be instantiated non-constexpr");

  if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(NewFD)) {
    // C++0x [dcl.constexpr]p4:
    //  In the definition of a constexpr constructor, each of the parameter
    //  types shall be a literal type.
    if (!CheckConstexprParameterTypes(*this, NewFD, CCK))
      return false;

    //  In addition, either its function-body shall be = delete or = default or
    //  it shall satisfy the following constraints:
    //  - the class shall not have any virtual base classes;
    const CXXRecordDecl *RD = CD->getParent();
    if (RD->getNumVBases()) {
      // Note, this is still illegal if the body is = default, since the
      // implicit body does not satisfy the requirements of a constexpr
      // constructor. We also reject cases where the body is = delete, as
      // required by N3308.
      if (CCK != CCK_Instantiation) {
        Diag(NewFD->getLocation(),
             CCK == CCK_Declaration ? diag::err_constexpr_virtual_base
                                    : diag::note_constexpr_tmpl_virtual_base)
          << RD->isStruct() << RD->getNumVBases();
        for (CXXRecordDecl::base_class_const_iterator I = RD->vbases_begin(),
               E = RD->vbases_end(); I != E; ++I)
          Diag(I->getSourceRange().getBegin(),
               diag::note_constexpr_virtual_base_here) << I->getSourceRange();
      }
      return false;
    }
  } else {
    // C++0x [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints:
    // - it shall not be virtual;
    const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(NewFD);
    if (Method && Method->isVirtual()) {
      if (CCK != CCK_Instantiation) {
        Diag(NewFD->getLocation(),
             CCK == CCK_Declaration ? diag::err_constexpr_virtual
                                    : diag::note_constexpr_tmpl_virtual);

        // If it's not obvious why this function is virtual, find an overridden
        // function which uses the 'virtual' keyword.
        const CXXMethodDecl *WrittenVirtual = Method;
        while (!WrittenVirtual->isVirtualAsWritten())
          WrittenVirtual = *WrittenVirtual->begin_overridden_methods();
        if (WrittenVirtual != Method)
          Diag(WrittenVirtual->getLocation(), 
               diag::note_overridden_virtual_function);
      }
      return false;
    }

    // - its return type shall be a literal type;
    QualType RT = NewFD->getResultType();
    if (!RT->isDependentType() &&
        RequireLiteralType(NewFD->getLocation(), RT, CCK == CCK_Declaration ?
                           PDiag(diag::err_constexpr_non_literal_return) :
                           PDiag(),
                           /*AllowIncompleteType*/ true)) {
      if (CCK == CCK_NoteNonConstexprInstantiation)
        Diag(NewFD->getLocation(),
             diag::note_constexpr_tmpl_non_literal_return) << RT;
      return false;
    }

    // - each of its parameter types shall be a literal type;
    if (!CheckConstexprParameterTypes(*this, NewFD, CCK))
      return false;
  }

  return true;
}

/// Check the given declaration statement is legal within a constexpr function
/// body. C++0x [dcl.constexpr]p3,p4.
///
/// \return true if the body is OK, false if we have diagnosed a problem.
static bool CheckConstexprDeclStmt(Sema &SemaRef, const FunctionDecl *Dcl,
                                   DeclStmt *DS) {
  // C++0x [dcl.constexpr]p3 and p4:
  //  The definition of a constexpr function(p3) or constructor(p4) [...] shall
  //  contain only
  for (DeclStmt::decl_iterator DclIt = DS->decl_begin(),
         DclEnd = DS->decl_end(); DclIt != DclEnd; ++DclIt) {
    switch ((*DclIt)->getKind()) {
    case Decl::StaticAssert:
    case Decl::Using:
    case Decl::UsingShadow:
    case Decl::UsingDirective:
    case Decl::UnresolvedUsingTypename:
      //   - static_assert-declarations
      //   - using-declarations,
      //   - using-directives,
      continue;

    case Decl::Typedef:
    case Decl::TypeAlias: {
      //   - typedef declarations and alias-declarations that do not define
      //     classes or enumerations,
      TypedefNameDecl *TN = cast<TypedefNameDecl>(*DclIt);
      if (TN->getUnderlyingType()->isVariablyModifiedType()) {
        // Don't allow variably-modified types in constexpr functions.
        TypeLoc TL = TN->getTypeSourceInfo()->getTypeLoc();
        SemaRef.Diag(TL.getBeginLoc(), diag::err_constexpr_vla)
          << TL.getSourceRange() << TL.getType()
          << isa<CXXConstructorDecl>(Dcl);
        return false;
      }
      continue;
    }

    case Decl::Enum:
    case Decl::CXXRecord:
      // As an extension, we allow the declaration (but not the definition) of
      // classes and enumerations in all declarations, not just in typedef and
      // alias declarations.
      if (cast<TagDecl>(*DclIt)->isThisDeclarationADefinition()) {
        SemaRef.Diag(DS->getLocStart(), diag::err_constexpr_type_definition)
          << isa<CXXConstructorDecl>(Dcl);
        return false;
      }
      continue;

    case Decl::Var:
      SemaRef.Diag(DS->getLocStart(), diag::err_constexpr_var_declaration)
        << isa<CXXConstructorDecl>(Dcl);
      return false;

    default:
      SemaRef.Diag(DS->getLocStart(), diag::err_constexpr_body_invalid_stmt)
        << isa<CXXConstructorDecl>(Dcl);
      return false;
    }
  }

  return true;
}

/// Check that the given field is initialized within a constexpr constructor.
///
/// \param Dcl The constexpr constructor being checked.
/// \param Field The field being checked. This may be a member of an anonymous
///        struct or union nested within the class being checked.
/// \param Inits All declarations, including anonymous struct/union members and
///        indirect members, for which any initialization was provided.
/// \param Diagnosed Set to true if an error is produced.
static void CheckConstexprCtorInitializer(Sema &SemaRef,
                                          const FunctionDecl *Dcl,
                                          FieldDecl *Field,
                                          llvm::SmallSet<Decl*, 16> &Inits,
                                          bool &Diagnosed) {
  if (Field->isUnnamedBitfield())
    return;
  
  if (!Inits.count(Field)) {
    if (!Diagnosed) {
      SemaRef.Diag(Dcl->getLocation(), diag::err_constexpr_ctor_missing_init);
      Diagnosed = true;
    }
    SemaRef.Diag(Field->getLocation(), diag::note_constexpr_ctor_missing_init);
  } else if (Field->isAnonymousStructOrUnion()) {
    const RecordDecl *RD = Field->getType()->castAs<RecordType>()->getDecl();
    for (RecordDecl::field_iterator I = RD->field_begin(), E = RD->field_end();
         I != E; ++I)
      // If an anonymous union contains an anonymous struct of which any member
      // is initialized, all members must be initialized.
      if (!RD->isUnion() || Inits.count(*I))
        CheckConstexprCtorInitializer(SemaRef, Dcl, *I, Inits, Diagnosed);
  }
}

/// Check the body for the given constexpr function declaration only contains
/// the permitted types of statement. C++11 [dcl.constexpr]p3,p4.
///
/// \return true if the body is OK, false if we have diagnosed a problem.
bool Sema::CheckConstexprFunctionBody(const FunctionDecl *Dcl, Stmt *Body) {
  if (isa<CXXTryStmt>(Body)) {
    // C++0x [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints: [...]
    // - its function-body shall be = delete, = default, or a
    //   compound-statement
    //
    // C++0x [dcl.constexpr]p4:
    //  In the definition of a constexpr constructor, [...]
    // - its function-body shall not be a function-try-block;
    Diag(Body->getLocStart(), diag::err_constexpr_function_try_block)
      << isa<CXXConstructorDecl>(Dcl);
    return false;
  }

  // - its function-body shall be [...] a compound-statement that contains only
  CompoundStmt *CompBody = cast<CompoundStmt>(Body);

  llvm::SmallVector<SourceLocation, 4> ReturnStmts;
  for (CompoundStmt::body_iterator BodyIt = CompBody->body_begin(),
         BodyEnd = CompBody->body_end(); BodyIt != BodyEnd; ++BodyIt) {
    switch ((*BodyIt)->getStmtClass()) {
    case Stmt::NullStmtClass:
      //   - null statements,
      continue;

    case Stmt::DeclStmtClass:
      //   - static_assert-declarations
      //   - using-declarations,
      //   - using-directives,
      //   - typedef declarations and alias-declarations that do not define
      //     classes or enumerations,
      if (!CheckConstexprDeclStmt(*this, Dcl, cast<DeclStmt>(*BodyIt)))
        return false;
      continue;

    case Stmt::ReturnStmtClass:
      //   - and exactly one return statement;
      if (isa<CXXConstructorDecl>(Dcl))
        break;

      ReturnStmts.push_back((*BodyIt)->getLocStart());
      // FIXME
      // - every constructor call and implicit conversion used in initializing
      //   the return value shall be one of those allowed in a constant
      //   expression.
      // Deal with this as part of a general check that the function can produce
      // a constant expression (for [dcl.constexpr]p5).
      continue;

    default:
      break;
    }

    Diag((*BodyIt)->getLocStart(), diag::err_constexpr_body_invalid_stmt)
      << isa<CXXConstructorDecl>(Dcl);
    return false;
  }

  if (const CXXConstructorDecl *Constructor
        = dyn_cast<CXXConstructorDecl>(Dcl)) {
    const CXXRecordDecl *RD = Constructor->getParent();
    // - every non-static data member and base class sub-object shall be
    //   initialized;
    if (RD->isUnion()) {
      // DR1359: Exactly one member of a union shall be initialized.
      if (Constructor->getNumCtorInitializers() == 0) {
        Diag(Dcl->getLocation(), diag::err_constexpr_union_ctor_no_init);
        return false;
      }
    } else if (!Constructor->isDependentContext() &&
               !Constructor->isDelegatingConstructor()) {
      assert(RD->getNumVBases() == 0 && "constexpr ctor with virtual bases");

      // Skip detailed checking if we have enough initializers, and we would
      // allow at most one initializer per member.
      bool AnyAnonStructUnionMembers = false;
      unsigned Fields = 0;
      for (CXXRecordDecl::field_iterator I = RD->field_begin(),
           E = RD->field_end(); I != E; ++I, ++Fields) {
        if ((*I)->isAnonymousStructOrUnion()) {
          AnyAnonStructUnionMembers = true;
          break;
        }
      }
      if (AnyAnonStructUnionMembers ||
          Constructor->getNumCtorInitializers() != RD->getNumBases() + Fields) {
        // Check initialization of non-static data members. Base classes are
        // always initialized so do not need to be checked. Dependent bases
        // might not have initializers in the member initializer list.
        llvm::SmallSet<Decl*, 16> Inits;
        for (CXXConstructorDecl::init_const_iterator
               I = Constructor->init_begin(), E = Constructor->init_end();
             I != E; ++I) {
          if (FieldDecl *FD = (*I)->getMember())
            Inits.insert(FD);
          else if (IndirectFieldDecl *ID = (*I)->getIndirectMember())
            Inits.insert(ID->chain_begin(), ID->chain_end());
        }

        bool Diagnosed = false;
        for (CXXRecordDecl::field_iterator I = RD->field_begin(),
             E = RD->field_end(); I != E; ++I)
          CheckConstexprCtorInitializer(*this, Dcl, *I, Inits, Diagnosed);
        if (Diagnosed)
          return false;
      }
    }

    // FIXME
    // - every constructor involved in initializing non-static data members
    //   and base class sub-objects shall be a constexpr constructor;
    // - every assignment-expression that is an initializer-clause appearing
    //   directly or indirectly within a brace-or-equal-initializer for
    //   a non-static data member that is not named by a mem-initializer-id
    //   shall be a constant expression; and
    // - every implicit conversion used in converting a constructor argument
    //   to the corresponding parameter type and converting
    //   a full-expression to the corresponding member type shall be one of
    //   those allowed in a constant expression.
    // Deal with these as part of a general check that the function can produce
    // a constant expression (for [dcl.constexpr]p5).
  } else {
    if (ReturnStmts.empty()) {
      Diag(Dcl->getLocation(), diag::err_constexpr_body_no_return);
      return false;
    }
    if (ReturnStmts.size() > 1) {
      Diag(ReturnStmts.back(), diag::err_constexpr_body_multiple_return);
      for (unsigned I = 0; I < ReturnStmts.size() - 1; ++I)
        Diag(ReturnStmts[I], diag::note_constexpr_body_previous_return);
      return false;
    }
  }

  return true;
}

/// isCurrentClassName - Determine whether the identifier II is the
/// name of the class type currently being defined. In the case of
/// nested classes, this will only return true if II is the name of
/// the innermost class.
bool Sema::isCurrentClassName(const IdentifierInfo &II, Scope *,
                              const CXXScopeSpec *SS) {
  assert(getLangOptions().CPlusPlus && "No class names in C!");

  CXXRecordDecl *CurDecl;
  if (SS && SS->isSet() && !SS->isInvalid()) {
    DeclContext *DC = computeDeclContext(*SS, true);
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(DC);
  } else
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(CurContext);

  if (CurDecl && CurDecl->getIdentifier())
    return &II == CurDecl->getIdentifier();
  else
    return false;
}

/// \brief Check the validity of a C++ base class specifier.
///
/// \returns a new CXXBaseSpecifier if well-formed, emits diagnostics
/// and returns NULL otherwise.
CXXBaseSpecifier *
Sema::CheckBaseSpecifier(CXXRecordDecl *Class,
                         SourceRange SpecifierRange,
                         bool Virtual, AccessSpecifier Access,
                         TypeSourceInfo *TInfo,
                         SourceLocation EllipsisLoc) {
  QualType BaseType = TInfo->getType();

  // C++ [class.union]p1:
  //   A union shall not have base classes.
  if (Class->isUnion()) {
    Diag(Class->getLocation(), diag::err_base_clause_on_union)
      << SpecifierRange;
    return 0;
  }

  if (EllipsisLoc.isValid() && 
      !TInfo->getType()->containsUnexpandedParameterPack()) {
    Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
      << TInfo->getTypeLoc().getSourceRange();
    EllipsisLoc = SourceLocation();
  }
  
  if (BaseType->isDependentType())
    return new (Context) CXXBaseSpecifier(SpecifierRange, Virtual,
                                          Class->getTagKind() == TTK_Class,
                                          Access, TInfo, EllipsisLoc);

  SourceLocation BaseLoc = TInfo->getTypeLoc().getBeginLoc();

  // Base specifiers must be record types.
  if (!BaseType->isRecordType()) {
    Diag(BaseLoc, diag::err_base_must_be_class) << SpecifierRange;
    return 0;
  }

  // C++ [class.union]p1:
  //   A union shall not be used as a base class.
  if (BaseType->isUnionType()) {
    Diag(BaseLoc, diag::err_union_as_base_class) << SpecifierRange;
    return 0;
  }

  // C++ [class.derived]p2:
  //   The class-name in a base-specifier shall not be an incompletely
  //   defined class.
  if (RequireCompleteType(BaseLoc, BaseType,
                          PDiag(diag::err_incomplete_base_class)
                            << SpecifierRange)) {
    Class->setInvalidDecl();
    return 0;
  }

  // If the base class is polymorphic or isn't empty, the new one is/isn't, too.
  RecordDecl *BaseDecl = BaseType->getAs<RecordType>()->getDecl();
  assert(BaseDecl && "Record type has no declaration");
  BaseDecl = BaseDecl->getDefinition();
  assert(BaseDecl && "Base type is not incomplete, but has no definition");
  CXXRecordDecl * CXXBaseDecl = cast<CXXRecordDecl>(BaseDecl);
  assert(CXXBaseDecl && "Base type is not a C++ type");

  // C++ [class]p3:
  //   If a class is marked final and it appears as a base-type-specifier in 
  //   base-clause, the program is ill-formed.
  if (CXXBaseDecl->hasAttr<FinalAttr>()) {
    Diag(BaseLoc, diag::err_class_marked_final_used_as_base) 
      << CXXBaseDecl->getDeclName();
    Diag(CXXBaseDecl->getLocation(), diag::note_previous_decl)
      << CXXBaseDecl->getDeclName();
    return 0;
  }

  if (BaseDecl->isInvalidDecl())
    Class->setInvalidDecl();
  
  // Create the base specifier.
  return new (Context) CXXBaseSpecifier(SpecifierRange, Virtual,
                                        Class->getTagKind() == TTK_Class,
                                        Access, TInfo, EllipsisLoc);
}

/// ActOnBaseSpecifier - Parsed a base specifier. A base specifier is
/// one entry in the base class list of a class specifier, for
/// example:
///    class foo : public bar, virtual private baz {
/// 'public bar' and 'virtual private baz' are each base-specifiers.
BaseResult
Sema::ActOnBaseSpecifier(Decl *classdecl, SourceRange SpecifierRange,
                         bool Virtual, AccessSpecifier Access,
                         ParsedType basetype, SourceLocation BaseLoc,
                         SourceLocation EllipsisLoc) {
  if (!classdecl)
    return true;

  AdjustDeclIfTemplate(classdecl);
  CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(classdecl);
  if (!Class)
    return true;

  TypeSourceInfo *TInfo = 0;
  GetTypeFromParser(basetype, &TInfo);

  if (EllipsisLoc.isInvalid() &&
      DiagnoseUnexpandedParameterPack(SpecifierRange.getBegin(), TInfo, 
                                      UPPC_BaseType))
    return true;
  
  if (CXXBaseSpecifier *BaseSpec = CheckBaseSpecifier(Class, SpecifierRange,
                                                      Virtual, Access, TInfo,
                                                      EllipsisLoc))
    return BaseSpec;

  return true;
}

/// \brief Performs the actual work of attaching the given base class
/// specifiers to a C++ class.
bool Sema::AttachBaseSpecifiers(CXXRecordDecl *Class, CXXBaseSpecifier **Bases,
                                unsigned NumBases) {
 if (NumBases == 0)
    return false;

  // Used to keep track of which base types we have already seen, so
  // that we can properly diagnose redundant direct base types. Note
  // that the key is always the unqualified canonical type of the base
  // class.
  std::map<QualType, CXXBaseSpecifier*, QualTypeOrdering> KnownBaseTypes;

  // Copy non-redundant base specifiers into permanent storage.
  unsigned NumGoodBases = 0;
  bool Invalid = false;
  for (unsigned idx = 0; idx < NumBases; ++idx) {
    QualType NewBaseType
      = Context.getCanonicalType(Bases[idx]->getType());
    NewBaseType = NewBaseType.getLocalUnqualifiedType();
    if (KnownBaseTypes[NewBaseType]) {
      // C++ [class.mi]p3:
      //   A class shall not be specified as a direct base class of a
      //   derived class more than once.
      Diag(Bases[idx]->getSourceRange().getBegin(),
           diag::err_duplicate_base_class)
        << KnownBaseTypes[NewBaseType]->getType()
        << Bases[idx]->getSourceRange();

      // Delete the duplicate base class specifier; we're going to
      // overwrite its pointer later.
      Context.Deallocate(Bases[idx]);

      Invalid = true;
    } else {
      // Okay, add this new base class.
      KnownBaseTypes[NewBaseType] = Bases[idx];
      Bases[NumGoodBases++] = Bases[idx];
    }
  }

  // Attach the remaining base class specifiers to the derived class.
  Class->setBases(Bases, NumGoodBases);

  // Delete the remaining (good) base class specifiers, since their
  // data has been copied into the CXXRecordDecl.
  for (unsigned idx = 0; idx < NumGoodBases; ++idx)
    Context.Deallocate(Bases[idx]);

  return Invalid;
}

/// ActOnBaseSpecifiers - Attach the given base specifiers to the
/// class, after checking whether there are any duplicate base
/// classes.
void Sema::ActOnBaseSpecifiers(Decl *ClassDecl, CXXBaseSpecifier **Bases,
                               unsigned NumBases) {
  if (!ClassDecl || !Bases || !NumBases)
    return;

  AdjustDeclIfTemplate(ClassDecl);
  AttachBaseSpecifiers(cast<CXXRecordDecl>(ClassDecl),
                       (CXXBaseSpecifier**)(Bases), NumBases);
}

static CXXRecordDecl *GetClassForType(QualType T) {
  if (const RecordType *RT = T->getAs<RecordType>())
    return cast<CXXRecordDecl>(RT->getDecl());
  else if (const InjectedClassNameType *ICT = T->getAs<InjectedClassNameType>())
    return ICT->getDecl();
  else
    return 0;
}

/// \brief Determine whether the type \p Derived is a C++ class that is
/// derived from the type \p Base.
bool Sema::IsDerivedFrom(QualType Derived, QualType Base) {
  if (!getLangOptions().CPlusPlus)
    return false;
  
  CXXRecordDecl *DerivedRD = GetClassForType(Derived);
  if (!DerivedRD)
    return false;
  
  CXXRecordDecl *BaseRD = GetClassForType(Base);
  if (!BaseRD)
    return false;
  
  // FIXME: instantiate DerivedRD if necessary.  We need a PoI for this.
  return DerivedRD->hasDefinition() && DerivedRD->isDerivedFrom(BaseRD);
}

/// \brief Determine whether the type \p Derived is a C++ class that is
/// derived from the type \p Base.
bool Sema::IsDerivedFrom(QualType Derived, QualType Base, CXXBasePaths &Paths) {
  if (!getLangOptions().CPlusPlus)
    return false;
  
  CXXRecordDecl *DerivedRD = GetClassForType(Derived);
  if (!DerivedRD)
    return false;
  
  CXXRecordDecl *BaseRD = GetClassForType(Base);
  if (!BaseRD)
    return false;
  
  return DerivedRD->isDerivedFrom(BaseRD, Paths);
}

void Sema::BuildBasePathArray(const CXXBasePaths &Paths, 
                              CXXCastPath &BasePathArray) {
  assert(BasePathArray.empty() && "Base path array must be empty!");
  assert(Paths.isRecordingPaths() && "Must record paths!");
  
  const CXXBasePath &Path = Paths.front();
       
  // We first go backward and check if we have a virtual base.
  // FIXME: It would be better if CXXBasePath had the base specifier for
  // the nearest virtual base.
  unsigned Start = 0;
  for (unsigned I = Path.size(); I != 0; --I) {
    if (Path[I - 1].Base->isVirtual()) {
      Start = I - 1;
      break;
    }
  }

  // Now add all bases.
  for (unsigned I = Start, E = Path.size(); I != E; ++I)
    BasePathArray.push_back(const_cast<CXXBaseSpecifier*>(Path[I].Base));
}

/// \brief Determine whether the given base path includes a virtual
/// base class.
bool Sema::BasePathInvolvesVirtualBase(const CXXCastPath &BasePath) {
  for (CXXCastPath::const_iterator B = BasePath.begin(), 
                                BEnd = BasePath.end();
       B != BEnd; ++B)
    if ((*B)->isVirtual())
      return true;

  return false;
}

/// CheckDerivedToBaseConversion - Check whether the Derived-to-Base
/// conversion (where Derived and Base are class types) is
/// well-formed, meaning that the conversion is unambiguous (and
/// that all of the base classes are accessible). Returns true
/// and emits a diagnostic if the code is ill-formed, returns false
/// otherwise. Loc is the location where this routine should point to
/// if there is an error, and Range is the source range to highlight
/// if there is an error.
bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   unsigned InaccessibleBaseID,
                                   unsigned AmbigiousBaseConvID,
                                   SourceLocation Loc, SourceRange Range,
                                   DeclarationName Name,
                                   CXXCastPath *BasePath) {
  // First, determine whether the path from Derived to Base is
  // ambiguous. This is slightly more expensive than checking whether
  // the Derived to Base conversion exists, because here we need to
  // explore multiple paths to determine if there is an ambiguity.
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  bool DerivationOkay = IsDerivedFrom(Derived, Base, Paths);
  assert(DerivationOkay &&
         "Can only be used with a derived-to-base conversion");
  (void)DerivationOkay;
  
  if (!Paths.isAmbiguous(Context.getCanonicalType(Base).getUnqualifiedType())) {
    if (InaccessibleBaseID) {
      // Check that the base class can be accessed.
      switch (CheckBaseClassAccess(Loc, Base, Derived, Paths.front(),
                                   InaccessibleBaseID)) {
        case AR_inaccessible: 
          return true;
        case AR_accessible: 
        case AR_dependent:
        case AR_delayed:
          break;
      }
    }
    
    // Build a base path if necessary.
    if (BasePath)
      BuildBasePathArray(Paths, *BasePath);
    return false;
  }
  
  // We know that the derived-to-base conversion is ambiguous, and
  // we're going to produce a diagnostic. Perform the derived-to-base
  // search just one more time to compute all of the possible paths so
  // that we can print them out. This is more expensive than any of
  // the previous derived-to-base checks we've done, but at this point
  // performance isn't as much of an issue.
  Paths.clear();
  Paths.setRecordingPaths(true);
  bool StillOkay = IsDerivedFrom(Derived, Base, Paths);
  assert(StillOkay && "Can only be used with a derived-to-base conversion");
  (void)StillOkay;
  
  // Build up a textual representation of the ambiguous paths, e.g.,
  // D -> B -> A, that will be used to illustrate the ambiguous
  // conversions in the diagnostic. We only print one of the paths
  // to each base class subobject.
  std::string PathDisplayStr = getAmbiguousPathsDisplayString(Paths);
  
  Diag(Loc, AmbigiousBaseConvID)
  << Derived << Base << PathDisplayStr << Range << Name;
  return true;
}

bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   SourceLocation Loc, SourceRange Range,
                                   CXXCastPath *BasePath,
                                   bool IgnoreAccess) {
  return CheckDerivedToBaseConversion(Derived, Base,
                                      IgnoreAccess ? 0
                                       : diag::err_upcast_to_inaccessible_base,
                                      diag::err_ambiguous_derived_to_base_conv,
                                      Loc, Range, DeclarationName(), 
                                      BasePath);
}


/// @brief Builds a string representing ambiguous paths from a
/// specific derived class to different subobjects of the same base
/// class.
///
/// This function builds a string that can be used in error messages
/// to show the different paths that one can take through the
/// inheritance hierarchy to go from the derived class to different
/// subobjects of a base class. The result looks something like this:
/// @code
/// struct D -> struct B -> struct A
/// struct D -> struct C -> struct A
/// @endcode
std::string Sema::getAmbiguousPathsDisplayString(CXXBasePaths &Paths) {
  std::string PathDisplayStr;
  std::set<unsigned> DisplayedPaths;
  for (CXXBasePaths::paths_iterator Path = Paths.begin();
       Path != Paths.end(); ++Path) {
    if (DisplayedPaths.insert(Path->back().SubobjectNumber).second) {
      // We haven't displayed a path to this particular base
      // class subobject yet.
      PathDisplayStr += "\n    ";
      PathDisplayStr += Context.getTypeDeclType(Paths.getOrigin()).getAsString();
      for (CXXBasePath::const_iterator Element = Path->begin();
           Element != Path->end(); ++Element)
        PathDisplayStr += " -> " + Element->Base->getType().getAsString();
    }
  }
  
  return PathDisplayStr;
}

//===----------------------------------------------------------------------===//
// C++ class member Handling
//===----------------------------------------------------------------------===//

/// ActOnAccessSpecifier - Parsed an access specifier followed by a colon.
bool Sema::ActOnAccessSpecifier(AccessSpecifier Access,
                                SourceLocation ASLoc,
                                SourceLocation ColonLoc,
                                AttributeList *Attrs) {
  assert(Access != AS_none && "Invalid kind for syntactic access specifier!");
  AccessSpecDecl *ASDecl = AccessSpecDecl::Create(Context, Access, CurContext,
                                                  ASLoc, ColonLoc);
  CurContext->addHiddenDecl(ASDecl);
  return ProcessAccessDeclAttributeList(ASDecl, Attrs);
}

/// CheckOverrideControl - Check C++0x override control semantics.
void Sema::CheckOverrideControl(const Decl *D) {
  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D);
  if (!MD || !MD->isVirtual())
    return;

  if (MD->isDependentContext())
    return;

  // C++0x [class.virtual]p3:
  //   If a virtual function is marked with the virt-specifier override and does
  //   not override a member function of a base class, 
  //   the program is ill-formed.
  bool HasOverriddenMethods = 
    MD->begin_overridden_methods() != MD->end_overridden_methods();
  if (MD->hasAttr<OverrideAttr>() && !HasOverriddenMethods) {
    Diag(MD->getLocation(), 
                 diag::err_function_marked_override_not_overriding)
      << MD->getDeclName();
    return;
  }
}

/// CheckIfOverriddenFunctionIsMarkedFinal - Checks whether a virtual member 
/// function overrides a virtual member function marked 'final', according to
/// C++0x [class.virtual]p3.
bool Sema::CheckIfOverriddenFunctionIsMarkedFinal(const CXXMethodDecl *New,
                                                  const CXXMethodDecl *Old) {
  if (!Old->hasAttr<FinalAttr>())
    return false;

  Diag(New->getLocation(), diag::err_final_function_overridden)
    << New->getDeclName();
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  return true;
}

/// ActOnCXXMemberDeclarator - This is invoked when a C++ class member
/// declarator is parsed. 'AS' is the access specifier, 'BW' specifies the
/// bitfield width if there is one, 'InitExpr' specifies the initializer if
/// one has been parsed, and 'HasDeferredInit' is true if an initializer is
/// present but parsing it has been deferred.
Decl *
Sema::ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS, Declarator &D,
                               MultiTemplateParamsArg TemplateParameterLists,
                               Expr *BW, const VirtSpecifiers &VS,
                               bool HasDeferredInit) {
  const DeclSpec &DS = D.getDeclSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  SourceLocation Loc = NameInfo.getLoc();

  // For anonymous bitfields, the location should point to the type.
  if (Loc.isInvalid())
    Loc = D.getSourceRange().getBegin();

  Expr *BitWidth = static_cast<Expr*>(BW);

  assert(isa<CXXRecordDecl>(CurContext));
  assert(!DS.isFriendSpecified());

  bool isFunc = D.isDeclarationOfFunction();

  // C++ 9.2p6: A member shall not be declared to have automatic storage
  // duration (auto, register) or with the extern storage-class-specifier.
  // C++ 7.1.1p8: The mutable specifier can be applied only to names of class
  // data members and cannot be applied to names declared const or static,
  // and cannot be applied to reference members.
  switch (DS.getStorageClassSpec()) {
    case DeclSpec::SCS_unspecified:
    case DeclSpec::SCS_typedef:
    case DeclSpec::SCS_static:
      // FALL THROUGH.
      break;
    case DeclSpec::SCS_mutable:
      if (isFunc) {
        if (DS.getStorageClassSpecLoc().isValid())
          Diag(DS.getStorageClassSpecLoc(), diag::err_mutable_function);
        else
          Diag(DS.getThreadSpecLoc(), diag::err_mutable_function);

        // FIXME: It would be nicer if the keyword was ignored only for this
        // declarator. Otherwise we could get follow-up errors.
        D.getMutableDeclSpec().ClearStorageClassSpecs();
      }
      break;
    default:
      if (DS.getStorageClassSpecLoc().isValid())
        Diag(DS.getStorageClassSpecLoc(),
             diag::err_storageclass_invalid_for_member);
      else
        Diag(DS.getThreadSpecLoc(), diag::err_storageclass_invalid_for_member);
      D.getMutableDeclSpec().ClearStorageClassSpecs();
  }

  bool isInstField = ((DS.getStorageClassSpec() == DeclSpec::SCS_unspecified ||
                       DS.getStorageClassSpec() == DeclSpec::SCS_mutable) &&
                      !isFunc);

  Decl *Member;
  if (isInstField) {
    CXXScopeSpec &SS = D.getCXXScopeSpec();

    // Data members must have identifiers for names.
    if (Name.getNameKind() != DeclarationName::Identifier) {
      Diag(Loc, diag::err_bad_variable_name)
        << Name;
      return 0;
    }
    
    IdentifierInfo *II = Name.getAsIdentifierInfo();

    // Member field could not be with "template" keyword.
    // So TemplateParameterLists should be empty in this case.
    if (TemplateParameterLists.size()) {
      TemplateParameterList* TemplateParams = TemplateParameterLists.get()[0];
      if (TemplateParams->size()) {
        // There is no such thing as a member field template.
        Diag(D.getIdentifierLoc(), diag::err_template_member)
            << II
            << SourceRange(TemplateParams->getTemplateLoc(),
                TemplateParams->getRAngleLoc());
      } else {
        // There is an extraneous 'template<>' for this member.
        Diag(TemplateParams->getTemplateLoc(),
            diag::err_template_member_noparams)
            << II
            << SourceRange(TemplateParams->getTemplateLoc(),
                TemplateParams->getRAngleLoc());
      }
      return 0;
    }

    if (SS.isSet() && !SS.isInvalid()) {
      // The user provided a superfluous scope specifier inside a class
      // definition:
      //
      // class X {
      //   int X::member;
      // };
      DeclContext *DC = 0;
      if ((DC = computeDeclContext(SS, false)) && DC->Equals(CurContext))
        Diag(D.getIdentifierLoc(), diag::warn_member_extra_qualification)
        << Name << FixItHint::CreateRemoval(SS.getRange());
      else
        Diag(D.getIdentifierLoc(), diag::err_member_qualification)
          << Name << SS.getRange();
       
      SS.clear();
    }

    Member = HandleField(S, cast<CXXRecordDecl>(CurContext), Loc, D, BitWidth,
                         HasDeferredInit, AS);
    assert(Member && "HandleField never returns null");
  } else {
    assert(!HasDeferredInit);

    Member = HandleDeclarator(S, D, move(TemplateParameterLists));
    if (!Member) {
      return 0;
    }

    // Non-instance-fields can't have a bitfield.
    if (BitWidth) {
      if (Member->isInvalidDecl()) {
        // don't emit another diagnostic.
      } else if (isa<VarDecl>(Member)) {
        // C++ 9.6p3: A bit-field shall not be a static member.
        // "static member 'A' cannot be a bit-field"
        Diag(Loc, diag::err_static_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else if (isa<TypedefDecl>(Member)) {
        // "typedef member 'x' cannot be a bit-field"
        Diag(Loc, diag::err_typedef_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else {
        // A function typedef ("typedef int f(); f a;").
        // C++ 9.6p3: A bit-field shall have integral or enumeration type.
        Diag(Loc, diag::err_not_integral_type_bitfield)
          << Name << cast<ValueDecl>(Member)->getType()
          << BitWidth->getSourceRange();
      }

      BitWidth = 0;
      Member->setInvalidDecl();
    }

    Member->setAccess(AS);

    // If we have declared a member function template, set the access of the
    // templated declaration as well.
    if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(Member))
      FunTmpl->getTemplatedDecl()->setAccess(AS);
  }

  if (VS.isOverrideSpecified()) {
    CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Member);
    if (!MD || !MD->isVirtual()) {
      Diag(Member->getLocStart(), 
           diag::override_keyword_only_allowed_on_virtual_member_functions)
        << "override" << FixItHint::CreateRemoval(VS.getOverrideLoc());
    } else
      MD->addAttr(new (Context) OverrideAttr(VS.getOverrideLoc(), Context));
  }
  if (VS.isFinalSpecified()) {
    CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Member);
    if (!MD || !MD->isVirtual()) {
      Diag(Member->getLocStart(), 
           diag::override_keyword_only_allowed_on_virtual_member_functions)
      << "final" << FixItHint::CreateRemoval(VS.getFinalLoc());
    } else
      MD->addAttr(new (Context) FinalAttr(VS.getFinalLoc(), Context));
  }

  if (VS.getLastLocation().isValid()) {
    // Update the end location of a method that has a virt-specifiers.
    if (CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Member))
      MD->setRangeEnd(VS.getLastLocation());
  }
      
  CheckOverrideControl(Member);

  assert((Name || isInstField) && "No identifier for non-field ?");

  if (isInstField)
    FieldCollector->Add(cast<FieldDecl>(Member));
  return Member;
}

/// ActOnCXXInClassMemberInitializer - This is invoked after parsing an
/// in-class initializer for a non-static C++ class member, and after
/// instantiating an in-class initializer in a class template. Such actions
/// are deferred until the class is complete.
void
Sema::ActOnCXXInClassMemberInitializer(Decl *D, SourceLocation EqualLoc,
                                       Expr *InitExpr) {
  FieldDecl *FD = cast<FieldDecl>(D);

  if (!InitExpr) {
    FD->setInvalidDecl();
    FD->removeInClassInitializer();
    return;
  }

  ExprResult Init = InitExpr;
  if (!FD->getType()->isDependentType() && !InitExpr->isTypeDependent()) {
    // FIXME: if there is no EqualLoc, this is list-initialization.
    Init = PerformCopyInitialization(
      InitializedEntity::InitializeMember(FD), EqualLoc, InitExpr);
    if (Init.isInvalid()) {
      FD->setInvalidDecl();
      return;
    }

    CheckImplicitConversions(Init.get(), EqualLoc);
  }

  // C++0x [class.base.init]p7:
  //   The initialization of each base and member constitutes a
  //   full-expression.
  Init = MaybeCreateExprWithCleanups(Init);
  if (Init.isInvalid()) {
    FD->setInvalidDecl();
    return;
  }

  InitExpr = Init.release();

  FD->setInClassInitializer(InitExpr);
}

/// \brief Find the direct and/or virtual base specifiers that
/// correspond to the given base type, for use in base initialization
/// within a constructor.
static bool FindBaseInitializer(Sema &SemaRef, 
                                CXXRecordDecl *ClassDecl,
                                QualType BaseType,
                                const CXXBaseSpecifier *&DirectBaseSpec,
                                const CXXBaseSpecifier *&VirtualBaseSpec) {
  // First, check for a direct base class.
  DirectBaseSpec = 0;
  for (CXXRecordDecl::base_class_const_iterator Base
         = ClassDecl->bases_begin(); 
       Base != ClassDecl->bases_end(); ++Base) {
    if (SemaRef.Context.hasSameUnqualifiedType(BaseType, Base->getType())) {
      // We found a direct base of this type. That's what we're
      // initializing.
      DirectBaseSpec = &*Base;
      break;
    }
  }

  // Check for a virtual base class.
  // FIXME: We might be able to short-circuit this if we know in advance that
  // there are no virtual bases.
  VirtualBaseSpec = 0;
  if (!DirectBaseSpec || !DirectBaseSpec->isVirtual()) {
    // We haven't found a base yet; search the class hierarchy for a
    // virtual base class.
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (SemaRef.IsDerivedFrom(SemaRef.Context.getTypeDeclType(ClassDecl), 
                              BaseType, Paths)) {
      for (CXXBasePaths::paths_iterator Path = Paths.begin();
           Path != Paths.end(); ++Path) {
        if (Path->back().Base->isVirtual()) {
          VirtualBaseSpec = Path->back().Base;
          break;
        }
      }
    }
  }

  return DirectBaseSpec || VirtualBaseSpec;
}

/// \brief Handle a C++ member initializer using braced-init-list syntax.
MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          SourceLocation IdLoc,
                          Expr *InitList,
                          SourceLocation EllipsisLoc) {
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             IdLoc, MultiInitializer(InitList), EllipsisLoc);
}

/// \brief Handle a C++ member initializer using parentheses syntax.
MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          SourceLocation IdLoc,
                          SourceLocation LParenLoc,
                          Expr **Args, unsigned NumArgs,
                          SourceLocation RParenLoc,
                          SourceLocation EllipsisLoc) {
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             IdLoc, MultiInitializer(LParenLoc, Args, NumArgs,
                                                     RParenLoc),
                             EllipsisLoc);
}

/// \brief Handle a C++ member initializer.
MemInitResult
Sema::BuildMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          SourceLocation IdLoc,
                          const MultiInitializer &Args,
                          SourceLocation EllipsisLoc) {
  if (!ConstructorD)
    return true;

  AdjustDeclIfTemplate(ConstructorD);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorD);
  if (!Constructor) {
    // The user wrote a constructor initializer on a function that is
    // not a C++ constructor. Ignore the error for now, because we may
    // have more member initializers coming; we'll diagnose it just
    // once in ActOnMemInitializers.
    return true;
  }

  CXXRecordDecl *ClassDecl = Constructor->getParent();

  // C++ [class.base.init]p2:
  //   Names in a mem-initializer-id are looked up in the scope of the
  //   constructor's class and, if not found in that scope, are looked
  //   up in the scope containing the constructor's definition.
  //   [Note: if the constructor's class contains a member with the
  //   same name as a direct or virtual base class of the class, a
  //   mem-initializer-id naming the member or base class and composed
  //   of a single identifier refers to the class member. A
  //   mem-initializer-id for the hidden base class may be specified
  //   using a qualified name. ]
  if (!SS.getScopeRep() && !TemplateTypeTy) {
    // Look for a member, first.
    FieldDecl *Member = 0;
    DeclContext::lookup_result Result
      = ClassDecl->lookup(MemberOrBase);
    if (Result.first != Result.second) {
      Member = dyn_cast<FieldDecl>(*Result.first);

      if (Member) {
        if (EllipsisLoc.isValid())
          Diag(EllipsisLoc, diag::err_pack_expansion_member_init)
            << MemberOrBase << SourceRange(IdLoc, Args.getEndLoc());

        return BuildMemberInitializer(Member, Args, IdLoc);
      }

      // Handle anonymous union case.
      if (IndirectFieldDecl* IndirectField
            = dyn_cast<IndirectFieldDecl>(*Result.first)) {
        if (EllipsisLoc.isValid())
          Diag(EllipsisLoc, diag::err_pack_expansion_member_init)
            << MemberOrBase << SourceRange(IdLoc, Args.getEndLoc());

         return BuildMemberInitializer(IndirectField, Args, IdLoc);
      }
    }
  }
  // It didn't name a member, so see if it names a class.
  QualType BaseType;
  TypeSourceInfo *TInfo = 0;

  if (TemplateTypeTy) {
    BaseType = GetTypeFromParser(TemplateTypeTy, &TInfo);
  } else {
    LookupResult R(*this, MemberOrBase, IdLoc, LookupOrdinaryName);
    LookupParsedName(R, S, &SS);

    TypeDecl *TyD = R.getAsSingle<TypeDecl>();
    if (!TyD) {
      if (R.isAmbiguous()) return true;

      // We don't want access-control diagnostics here.
      R.suppressDiagnostics();

      if (SS.isSet() && isDependentScopeSpecifier(SS)) {
        bool NotUnknownSpecialization = false;
        DeclContext *DC = computeDeclContext(SS, false);
        if (CXXRecordDecl *Record = dyn_cast_or_null<CXXRecordDecl>(DC)) 
          NotUnknownSpecialization = !Record->hasAnyDependentBases();

        if (!NotUnknownSpecialization) {
          // When the scope specifier can refer to a member of an unknown
          // specialization, we take it as a type name.
          BaseType = CheckTypenameType(ETK_None, SourceLocation(),
                                       SS.getWithLocInContext(Context),
                                       *MemberOrBase, IdLoc);
          if (BaseType.isNull())
            return true;

          R.clear();
          R.setLookupName(MemberOrBase);
        }
      }

      // If no results were found, try to correct typos.
      TypoCorrection Corr;
      if (R.empty() && BaseType.isNull() &&
          (Corr = CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S, &SS,
                              ClassDecl, false, CTC_NoKeywords))) {
        std::string CorrectedStr(Corr.getAsString(getLangOptions()));
        std::string CorrectedQuotedStr(Corr.getQuoted(getLangOptions()));
        if (FieldDecl *Member = Corr.getCorrectionDeclAs<FieldDecl>()) {
          if (Member->getDeclContext()->getRedeclContext()->Equals(ClassDecl)) {
            // We have found a non-static data member with a similar
            // name to what was typed; complain and initialize that
            // member.
            Diag(R.getNameLoc(), diag::err_mem_init_not_member_or_class_suggest)
              << MemberOrBase << true << CorrectedQuotedStr
              << FixItHint::CreateReplacement(R.getNameLoc(), CorrectedStr);
            Diag(Member->getLocation(), diag::note_previous_decl)
              << CorrectedQuotedStr;

            return BuildMemberInitializer(Member, Args, IdLoc);
          }
        } else if (TypeDecl *Type = Corr.getCorrectionDeclAs<TypeDecl>()) {
          const CXXBaseSpecifier *DirectBaseSpec;
          const CXXBaseSpecifier *VirtualBaseSpec;
          if (FindBaseInitializer(*this, ClassDecl, 
                                  Context.getTypeDeclType(Type),
                                  DirectBaseSpec, VirtualBaseSpec)) {
            // We have found a direct or virtual base class with a
            // similar name to what was typed; complain and initialize
            // that base class.
            Diag(R.getNameLoc(), diag::err_mem_init_not_member_or_class_suggest)
              << MemberOrBase << false << CorrectedQuotedStr
              << FixItHint::CreateReplacement(R.getNameLoc(), CorrectedStr);

            const CXXBaseSpecifier *BaseSpec = DirectBaseSpec? DirectBaseSpec 
                                                             : VirtualBaseSpec;
            Diag(BaseSpec->getSourceRange().getBegin(),
                 diag::note_base_class_specified_here)
              << BaseSpec->getType()
              << BaseSpec->getSourceRange();

            TyD = Type;
          }
        }
      }

      if (!TyD && BaseType.isNull()) {
        Diag(IdLoc, diag::err_mem_init_not_member_or_class)
          << MemberOrBase << SourceRange(IdLoc, Args.getEndLoc());
        return true;
      }
    }

    if (BaseType.isNull()) {
      BaseType = Context.getTypeDeclType(TyD);
      if (SS.isSet()) {
        NestedNameSpecifier *Qualifier =
          static_cast<NestedNameSpecifier*>(SS.getScopeRep());

        // FIXME: preserve source range information
        BaseType = Context.getElaboratedType(ETK_None, Qualifier, BaseType);
      }
    }
  }

  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(BaseType, IdLoc);

  return BuildBaseInitializer(BaseType, TInfo, Args, ClassDecl, EllipsisLoc);
}

/// Checks a member initializer expression for cases where reference (or
/// pointer) members are bound to by-value parameters (or their addresses).
static void CheckForDanglingReferenceOrPointer(Sema &S, ValueDecl *Member,
                                               Expr *Init,
                                               SourceLocation IdLoc) {
  QualType MemberTy = Member->getType();

  // We only handle pointers and references currently.
  // FIXME: Would this be relevant for ObjC object pointers? Or block pointers?
  if (!MemberTy->isReferenceType() && !MemberTy->isPointerType())
    return;

  const bool IsPointer = MemberTy->isPointerType();
  if (IsPointer) {
    if (const UnaryOperator *Op
          = dyn_cast<UnaryOperator>(Init->IgnoreParenImpCasts())) {
      // The only case we're worried about with pointers requires taking the
      // address.
      if (Op->getOpcode() != UO_AddrOf)
        return;

      Init = Op->getSubExpr();
    } else {
      // We only handle address-of expression initializers for pointers.
      return;
    }
  }

  if (isa<MaterializeTemporaryExpr>(Init->IgnoreParens())) {
    // Taking the address of a temporary will be diagnosed as a hard error.
    if (IsPointer)
      return;

    S.Diag(Init->getExprLoc(), diag::warn_bind_ref_member_to_temporary)
      << Member << Init->getSourceRange();
  } else if (const DeclRefExpr *DRE
               = dyn_cast<DeclRefExpr>(Init->IgnoreParens())) {
    // We only warn when referring to a non-reference parameter declaration.
    const ParmVarDecl *Parameter = dyn_cast<ParmVarDecl>(DRE->getDecl());
    if (!Parameter || Parameter->getType()->isReferenceType())
      return;

    S.Diag(Init->getExprLoc(),
           IsPointer ? diag::warn_init_ptr_member_to_parameter_addr
                     : diag::warn_bind_ref_member_to_parameter)
      << Member << Parameter << Init->getSourceRange();
  } else {
    // Other initializers are fine.
    return;
  }

  S.Diag(Member->getLocation(), diag::note_ref_or_ptr_member_declared_here)
    << (unsigned)IsPointer;
}

/// Checks an initializer expression for use of uninitialized fields, such as
/// containing the field that is being initialized. Returns true if there is an
/// uninitialized field was used an updates the SourceLocation parameter; false
/// otherwise.
static bool InitExprContainsUninitializedFields(const Stmt *S,
                                                const ValueDecl *LhsField,
                                                SourceLocation *L) {
  assert(isa<FieldDecl>(LhsField) || isa<IndirectFieldDecl>(LhsField));

  if (isa<CallExpr>(S)) {
    // Do not descend into function calls or constructors, as the use
    // of an uninitialized field may be valid. One would have to inspect
    // the contents of the function/ctor to determine if it is safe or not.
    // i.e. Pass-by-value is never safe, but pass-by-reference and pointers
    // may be safe, depending on what the function/ctor does.
    return false;
  }
  if (const MemberExpr *ME = dyn_cast<MemberExpr>(S)) {
    const NamedDecl *RhsField = ME->getMemberDecl();

    if (const VarDecl *VD = dyn_cast<VarDecl>(RhsField)) {
      // The member expression points to a static data member.
      assert(VD->isStaticDataMember() && 
             "Member points to non-static data member!");
      (void)VD;
      return false;
    }
    
    if (isa<EnumConstantDecl>(RhsField)) {
      // The member expression points to an enum.
      return false;
    }

    if (RhsField == LhsField) {
      // Initializing a field with itself. Throw a warning.
      // But wait; there are exceptions!
      // Exception #1:  The field may not belong to this record.
      // e.g. Foo(const Foo& rhs) : A(rhs.A) {}
      const Expr *base = ME->getBase();
      if (base != NULL && !isa<CXXThisExpr>(base->IgnoreParenCasts())) {
        // Even though the field matches, it does not belong to this record.
        return false;
      }
      // None of the exceptions triggered; return true to indicate an
      // uninitialized field was used.
      *L = ME->getMemberLoc();
      return true;
    }
  } else if (isa<UnaryExprOrTypeTraitExpr>(S)) {
    // sizeof/alignof doesn't reference contents, do not warn.
    return false;
  } else if (const UnaryOperator *UOE = dyn_cast<UnaryOperator>(S)) {
    // address-of doesn't reference contents (the pointer may be dereferenced
    // in the same expression but it would be rare; and weird).
    if (UOE->getOpcode() == UO_AddrOf)
      return false;
  }
  for (Stmt::const_child_range it = S->children(); it; ++it) {
    if (!*it) {
      // An expression such as 'member(arg ?: "")' may trigger this.
      continue;
    }
    if (InitExprContainsUninitializedFields(*it, LhsField, L))
      return true;
  }
  return false;
}

MemInitResult
Sema::BuildMemberInitializer(ValueDecl *Member,
                             const MultiInitializer &Args,
                             SourceLocation IdLoc) {
  FieldDecl *DirectMember = dyn_cast<FieldDecl>(Member);
  IndirectFieldDecl *IndirectMember = dyn_cast<IndirectFieldDecl>(Member);
  assert((DirectMember || IndirectMember) &&
         "Member must be a FieldDecl or IndirectFieldDecl");

  if (Member->isInvalidDecl())
    return true;

  // Diagnose value-uses of fields to initialize themselves, e.g.
  //   foo(foo)
  // where foo is not also a parameter to the constructor.
  // TODO: implement -Wuninitialized and fold this into that framework.
  for (MultiInitializer::iterator I = Args.begin(), E = Args.end();
       I != E; ++I) {
    SourceLocation L;
    Expr *Arg = *I;
    if (DesignatedInitExpr *DIE = dyn_cast<DesignatedInitExpr>(Arg))
      Arg = DIE->getInit();
    if (InitExprContainsUninitializedFields(Arg, Member, &L)) {
      // FIXME: Return true in the case when other fields are used before being
      // uninitialized. For example, let this field be the i'th field. When
      // initializing the i'th field, throw a warning if any of the >= i'th
      // fields are used, as they are not yet initialized.
      // Right now we are only handling the case where the i'th field uses
      // itself in its initializer.
      Diag(L, diag::warn_field_is_uninit);
    }
  }

  bool HasDependentArg = Args.isTypeDependent();

  Expr *Init;
  if (Member->getType()->isDependentType() || HasDependentArg) {
    // Can't check initialization for a member of dependent type or when
    // any of the arguments are type-dependent expressions.
    Init = Args.CreateInitExpr(Context,Member->getType().getNonReferenceType());

    DiscardCleanupsInEvaluationContext();
  } else {
    // Initialize the member.
    InitializedEntity MemberEntity =
      DirectMember ? InitializedEntity::InitializeMember(DirectMember, 0)
                   : InitializedEntity::InitializeMember(IndirectMember, 0);
    InitializationKind Kind =
      InitializationKind::CreateDirect(IdLoc, Args.getStartLoc(),
                                       Args.getEndLoc());

    ExprResult MemberInit = Args.PerformInit(*this, MemberEntity, Kind);
    if (MemberInit.isInvalid())
      return true;

    CheckImplicitConversions(MemberInit.get(), Args.getStartLoc());

    // C++0x [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    MemberInit = MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    // If we are in a dependent context, template instantiation will
    // perform this type-checking again. Just save the arguments that we
    // received in a ParenListExpr.
    // FIXME: This isn't quite ideal, since our ASTs don't capture all
    // of the information that we have about the member
    // initializer. However, deconstructing the ASTs is a dicey process,
    // and this approach is far more likely to get the corner cases right.
    if (CurContext->isDependentContext()) {
      Init = Args.CreateInitExpr(Context,
                                 Member->getType().getNonReferenceType());
    } else {
      Init = MemberInit.get();
      CheckForDanglingReferenceOrPointer(*this, Member, Init, IdLoc);
    }
  }

  if (DirectMember) {
    return new (Context) CXXCtorInitializer(Context, DirectMember,
                                                    IdLoc, Args.getStartLoc(),
                                                    Init, Args.getEndLoc());
  } else {
    return new (Context) CXXCtorInitializer(Context, IndirectMember,
                                                    IdLoc, Args.getStartLoc(),
                                                    Init, Args.getEndLoc());
  }
}

MemInitResult
Sema::BuildDelegatingInitializer(TypeSourceInfo *TInfo,
                                 const MultiInitializer &Args,
                                 SourceLocation NameLoc,
                                 CXXRecordDecl *ClassDecl) {
  SourceLocation Loc = TInfo->getTypeLoc().getLocalSourceRange().getBegin();
  if (!LangOpts.CPlusPlus0x)
    return Diag(Loc, diag::err_delegation_0x_only)
      << TInfo->getTypeLoc().getLocalSourceRange();

  // Initialize the object.
  InitializedEntity DelegationEntity = InitializedEntity::InitializeDelegation(
                                     QualType(ClassDecl->getTypeForDecl(), 0));
  InitializationKind Kind =
    InitializationKind::CreateDirect(NameLoc, Args.getStartLoc(),
                                     Args.getEndLoc());

  ExprResult DelegationInit = Args.PerformInit(*this, DelegationEntity, Kind);
  if (DelegationInit.isInvalid())
    return true;

  CXXConstructExpr *ConExpr = cast<CXXConstructExpr>(DelegationInit.get());
  CXXConstructorDecl *Constructor
    = ConExpr->getConstructor();
  assert(Constructor && "Delegating constructor with no target?");

  CheckImplicitConversions(DelegationInit.get(), Args.getStartLoc());

  // C++0x [class.base.init]p7:
  //   The initialization of each base and member constitutes a
  //   full-expression.
  DelegationInit = MaybeCreateExprWithCleanups(DelegationInit);
  if (DelegationInit.isInvalid())
    return true;

  assert(!CurContext->isDependentContext());
  return new (Context) CXXCtorInitializer(Context, Loc, Args.getStartLoc(),
                                          Constructor,
                                          DelegationInit.takeAs<Expr>(),
                                          Args.getEndLoc());
}

MemInitResult
Sema::BuildBaseInitializer(QualType BaseType, TypeSourceInfo *BaseTInfo,
                           const MultiInitializer &Args,
                           CXXRecordDecl *ClassDecl,
                           SourceLocation EllipsisLoc) {
  bool HasDependentArg = Args.isTypeDependent();

  SourceLocation BaseLoc
    = BaseTInfo->getTypeLoc().getLocalSourceRange().getBegin();

  if (!BaseType->isDependentType() && !BaseType->isRecordType())
    return Diag(BaseLoc, diag::err_base_init_does_not_name_class)
             << BaseType << BaseTInfo->getTypeLoc().getLocalSourceRange();

  // C++ [class.base.init]p2:
  //   [...] Unless the mem-initializer-id names a nonstatic data
  //   member of the constructor's class or a direct or virtual base
  //   of that class, the mem-initializer is ill-formed. A
  //   mem-initializer-list can initialize a base class using any
  //   name that denotes that base class type.
  bool Dependent = BaseType->isDependentType() || HasDependentArg;

  if (EllipsisLoc.isValid()) {
    // This is a pack expansion.
    if (!BaseType->containsUnexpandedParameterPack())  {
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << SourceRange(BaseLoc, Args.getEndLoc());

      EllipsisLoc = SourceLocation();
    }
  } else {
    // Check for any unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(BaseLoc, BaseTInfo, UPPC_Initializer))
      return true;

    if (Args.DiagnoseUnexpandedParameterPack(*this))
      return true;
  }

  // Check for direct and virtual base classes.
  const CXXBaseSpecifier *DirectBaseSpec = 0;
  const CXXBaseSpecifier *VirtualBaseSpec = 0;
  if (!Dependent) { 
    if (Context.hasSameUnqualifiedType(QualType(ClassDecl->getTypeForDecl(),0),
                                       BaseType))
      return BuildDelegatingInitializer(BaseTInfo, Args, BaseLoc, ClassDecl);

    FindBaseInitializer(*this, ClassDecl, BaseType, DirectBaseSpec, 
                        VirtualBaseSpec);

    // C++ [base.class.init]p2:
    // Unless the mem-initializer-id names a nonstatic data member of the
    // constructor's class or a direct or virtual base of that class, the
    // mem-initializer is ill-formed.
    if (!DirectBaseSpec && !VirtualBaseSpec) {
      // If the class has any dependent bases, then it's possible that
      // one of those types will resolve to the same type as
      // BaseType. Therefore, just treat this as a dependent base
      // class initialization.  FIXME: Should we try to check the
      // initialization anyway? It seems odd.
      if (ClassDecl->hasAnyDependentBases())
        Dependent = true;
      else
        return Diag(BaseLoc, diag::err_not_direct_base_or_virtual)
          << BaseType << Context.getTypeDeclType(ClassDecl)
          << BaseTInfo->getTypeLoc().getLocalSourceRange();
    }
  }

  if (Dependent) {
    // Can't check initialization for a base of dependent type or when
    // any of the arguments are type-dependent expressions.
    Expr *BaseInit = Args.CreateInitExpr(Context, BaseType);

    DiscardCleanupsInEvaluationContext();

    return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                            /*IsVirtual=*/false,
                                            Args.getStartLoc(), BaseInit,
                                            Args.getEndLoc(), EllipsisLoc);
  }

  // C++ [base.class.init]p2:
  //   If a mem-initializer-id is ambiguous because it designates both
  //   a direct non-virtual base class and an inherited virtual base
  //   class, the mem-initializer is ill-formed.
  if (DirectBaseSpec && VirtualBaseSpec)
    return Diag(BaseLoc, diag::err_base_init_direct_and_virtual)
      << BaseType << BaseTInfo->getTypeLoc().getLocalSourceRange();

  CXXBaseSpecifier *BaseSpec
    = const_cast<CXXBaseSpecifier *>(DirectBaseSpec);
  if (!BaseSpec)
    BaseSpec = const_cast<CXXBaseSpecifier *>(VirtualBaseSpec);

  // Initialize the base.
  InitializedEntity BaseEntity =
    InitializedEntity::InitializeBase(Context, BaseSpec, VirtualBaseSpec);
  InitializationKind Kind = 
    InitializationKind::CreateDirect(BaseLoc, Args.getStartLoc(),
                                     Args.getEndLoc());

  ExprResult BaseInit = Args.PerformInit(*this, BaseEntity, Kind);
  if (BaseInit.isInvalid())
    return true;

  CheckImplicitConversions(BaseInit.get(), Args.getStartLoc());

  // C++0x [class.base.init]p7:
  //   The initialization of each base and member constitutes a 
  //   full-expression.
  BaseInit = MaybeCreateExprWithCleanups(BaseInit);
  if (BaseInit.isInvalid())
    return true;

  // If we are in a dependent context, template instantiation will
  // perform this type-checking again. Just save the arguments that we
  // received in a ParenListExpr.
  // FIXME: This isn't quite ideal, since our ASTs don't capture all
  // of the information that we have about the base
  // initializer. However, deconstructing the ASTs is a dicey process,
  // and this approach is far more likely to get the corner cases right.
  if (CurContext->isDependentContext())
    BaseInit = Owned(Args.CreateInitExpr(Context, BaseType));

  return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                          BaseSpec->isVirtual(),
                                          Args.getStartLoc(),
                                          BaseInit.takeAs<Expr>(),
                                          Args.getEndLoc(), EllipsisLoc);
}

// Create a static_cast\<T&&>(expr).
static Expr *CastForMoving(Sema &SemaRef, Expr *E) {
  QualType ExprType = E->getType();
  QualType TargetType = SemaRef.Context.getRValueReferenceType(ExprType);
  SourceLocation ExprLoc = E->getLocStart();
  TypeSourceInfo *TargetLoc = SemaRef.Context.getTrivialTypeSourceInfo(
      TargetType, ExprLoc);

  return SemaRef.BuildCXXNamedCast(ExprLoc, tok::kw_static_cast, TargetLoc, E,
                                   SourceRange(ExprLoc, ExprLoc),
                                   E->getSourceRange()).take();
}

/// ImplicitInitializerKind - How an implicit base or member initializer should
/// initialize its base or member.
enum ImplicitInitializerKind {
  IIK_Default,
  IIK_Copy,
  IIK_Move
};

static bool
BuildImplicitBaseInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                             ImplicitInitializerKind ImplicitInitKind,
                             CXXBaseSpecifier *BaseSpec,
                             bool IsInheritedVirtualBase,
                             CXXCtorInitializer *&CXXBaseInit) {
  InitializedEntity InitEntity
    = InitializedEntity::InitializeBase(SemaRef.Context, BaseSpec,
                                        IsInheritedVirtualBase);

  ExprResult BaseInit;
  
  switch (ImplicitInitKind) {
  case IIK_Default: {
    InitializationKind InitKind
      = InitializationKind::CreateDefault(Constructor->getLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, 0, 0);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind,
                               MultiExprArg(SemaRef, 0, 0));
    break;
  }

  case IIK_Move:
  case IIK_Copy: {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();
    
    Expr *CopyCtorArg = 
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(), Param, 
                          Constructor->getLocation(), ParamType,
                          VK_LValue, 0);

    // Cast to the base class to avoid ambiguities.
    QualType ArgTy = 
      SemaRef.Context.getQualifiedType(BaseSpec->getType().getUnqualifiedType(), 
                                       ParamType.getQualifiers());

    if (Moving) {
      CopyCtorArg = CastForMoving(SemaRef, CopyCtorArg);
    }

    CXXCastPath BasePath;
    BasePath.push_back(BaseSpec);
    CopyCtorArg = SemaRef.ImpCastExprToType(CopyCtorArg, ArgTy,
                                            CK_UncheckedDerivedToBase,
                                            Moving ? VK_XValue : VK_LValue,
                                            &BasePath).take();

    InitializationKind InitKind
      = InitializationKind::CreateDirect(Constructor->getLocation(),
                                         SourceLocation(), SourceLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, 
                                   &CopyCtorArg, 1);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind,
                               MultiExprArg(&CopyCtorArg, 1));
    break;
  }
  }

  BaseInit = SemaRef.MaybeCreateExprWithCleanups(BaseInit);
  if (BaseInit.isInvalid())
    return true;
        
  CXXBaseInit =
    new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
               SemaRef.Context.getTrivialTypeSourceInfo(BaseSpec->getType(), 
                                                        SourceLocation()),
                                             BaseSpec->isVirtual(),
                                             SourceLocation(),
                                             BaseInit.takeAs<Expr>(),
                                             SourceLocation(),
                                             SourceLocation());

  return false;
}

static bool RefersToRValueRef(Expr *MemRef) {
  ValueDecl *Referenced = cast<MemberExpr>(MemRef)->getMemberDecl();
  return Referenced->getType()->isRValueReferenceType();
}

static bool
BuildImplicitMemberInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                               ImplicitInitializerKind ImplicitInitKind,
                               FieldDecl *Field, IndirectFieldDecl *Indirect,
                               CXXCtorInitializer *&CXXMemberInit) {
  if (Field->isInvalidDecl())
    return true;

  SourceLocation Loc = Constructor->getLocation();

  if (ImplicitInitKind == IIK_Copy || ImplicitInitKind == IIK_Move) {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();

    // Suppress copying zero-width bitfields.
    if (Field->isBitField() && Field->getBitWidthValue(SemaRef.Context) == 0)
      return false;
    
    Expr *MemberExprBase = 
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(), Param, 
                          Loc, ParamType, VK_LValue, 0);

    if (Moving) {
      MemberExprBase = CastForMoving(SemaRef, MemberExprBase);
    }

    // Build a reference to this field within the parameter.
    CXXScopeSpec SS;
    LookupResult MemberLookup(SemaRef, Field->getDeclName(), Loc,
                              Sema::LookupMemberName);
    MemberLookup.addDecl(Indirect ? cast<ValueDecl>(Indirect)
                                  : cast<ValueDecl>(Field), AS_public);
    MemberLookup.resolveKind();
    ExprResult CtorArg 
      = SemaRef.BuildMemberReferenceExpr(MemberExprBase,
                                         ParamType, Loc,
                                         /*IsArrow=*/false,
                                         SS,
                                         /*FirstQualifierInScope=*/0,
                                         MemberLookup,
                                         /*TemplateArgs=*/0);    
    if (CtorArg.isInvalid())
      return true;

    // C++11 [class.copy]p15:
    //   - if a member m has rvalue reference type T&&, it is direct-initialized
    //     with static_cast<T&&>(x.m);
    if (RefersToRValueRef(CtorArg.get())) {
      CtorArg = CastForMoving(SemaRef, CtorArg.take());
    }

    // When the field we are copying is an array, create index variables for 
    // each dimension of the array. We use these index variables to subscript
    // the source array, and other clients (e.g., CodeGen) will perform the
    // necessary iteration with these index variables.
    SmallVector<VarDecl *, 4> IndexVariables;
    QualType BaseType = Field->getType();
    QualType SizeType = SemaRef.Context.getSizeType();
    bool InitializingArray = false;
    while (const ConstantArrayType *Array
                          = SemaRef.Context.getAsConstantArrayType(BaseType)) {
      InitializingArray = true;
      // Create the iteration variable for this array index.
      IdentifierInfo *IterationVarName = 0;
      {
        llvm::SmallString<8> Str;
        llvm::raw_svector_ostream OS(Str);
        OS << "__i" << IndexVariables.size();
        IterationVarName = &SemaRef.Context.Idents.get(OS.str());
      }
      VarDecl *IterationVar
        = VarDecl::Create(SemaRef.Context, SemaRef.CurContext, Loc, Loc,
                          IterationVarName, SizeType,
                        SemaRef.Context.getTrivialTypeSourceInfo(SizeType, Loc),
                          SC_None, SC_None);
      IndexVariables.push_back(IterationVar);
      
      // Create a reference to the iteration variable.
      ExprResult IterationVarRef
        = SemaRef.BuildDeclRefExpr(IterationVar, SizeType, VK_RValue, Loc);
      assert(!IterationVarRef.isInvalid() &&
             "Reference to invented variable cannot fail!");

      // Subscript the array with this iteration variable.
      CtorArg = SemaRef.CreateBuiltinArraySubscriptExpr(CtorArg.take(), Loc,
                                                        IterationVarRef.take(),
                                                        Loc);
      if (CtorArg.isInvalid())
        return true;

      BaseType = Array->getElementType();
    }

    // The array subscript expression is an lvalue, which is wrong for moving.
    if (Moving && InitializingArray)
      CtorArg = CastForMoving(SemaRef, CtorArg.take());

    // Construct the entity that we will be initializing. For an array, this
    // will be first element in the array, which may require several levels
    // of array-subscript entities. 
    SmallVector<InitializedEntity, 4> Entities;
    Entities.reserve(1 + IndexVariables.size());
    if (Indirect)
      Entities.push_back(InitializedEntity::InitializeMember(Indirect));
    else
      Entities.push_back(InitializedEntity::InitializeMember(Field));
    for (unsigned I = 0, N = IndexVariables.size(); I != N; ++I)
      Entities.push_back(InitializedEntity::InitializeElement(SemaRef.Context,
                                                              0,
                                                              Entities.back()));
    
    // Direct-initialize to use the copy constructor.
    InitializationKind InitKind =
      InitializationKind::CreateDirect(Loc, SourceLocation(), SourceLocation());
    
    Expr *CtorArgE = CtorArg.takeAs<Expr>();
    InitializationSequence InitSeq(SemaRef, Entities.back(), InitKind,
                                   &CtorArgE, 1);
    
    ExprResult MemberInit
      = InitSeq.Perform(SemaRef, Entities.back(), InitKind, 
                        MultiExprArg(&CtorArgE, 1));
    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    if (Indirect) {
      assert(IndexVariables.size() == 0 && 
             "Indirect field improperly initialized");
      CXXMemberInit
        = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Indirect, 
                                                   Loc, Loc, 
                                                   MemberInit.takeAs<Expr>(), 
                                                   Loc);
    } else
      CXXMemberInit = CXXCtorInitializer::Create(SemaRef.Context, Field, Loc, 
                                                 Loc, MemberInit.takeAs<Expr>(), 
                                                 Loc,
                                                 IndexVariables.data(),
                                                 IndexVariables.size());
    return false;
  }

  assert(ImplicitInitKind == IIK_Default && "Unhandled implicit init kind!");

  QualType FieldBaseElementType = 
    SemaRef.Context.getBaseElementType(Field->getType());
  
  if (FieldBaseElementType->isRecordType()) {
    InitializedEntity InitEntity 
      = Indirect? InitializedEntity::InitializeMember(Indirect)
                : InitializedEntity::InitializeMember(Field);
    InitializationKind InitKind = 
      InitializationKind::CreateDefault(Loc);
    
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, 0, 0);
    ExprResult MemberInit = 
      InitSeq.Perform(SemaRef, InitEntity, InitKind, MultiExprArg());

    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;
    
    if (Indirect)
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Indirect, Loc, 
                                                               Loc,
                                                               MemberInit.get(),
                                                               Loc);
    else
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Field, Loc, Loc,
                                                               MemberInit.get(),
                                                               Loc);
    return false;
  }

  if (!Field->getParent()->isUnion()) {
    if (FieldBaseElementType->isReferenceType()) {
      SemaRef.Diag(Constructor->getLocation(), 
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit() 
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 0 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }

    if (FieldBaseElementType.isConstQualified()) {
      SemaRef.Diag(Constructor->getLocation(), 
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit() 
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 1 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }
  }
  
  if (SemaRef.getLangOptions().ObjCAutoRefCount &&
      FieldBaseElementType->isObjCRetainableType() &&
      FieldBaseElementType.getObjCLifetime() != Qualifiers::OCL_None &&
      FieldBaseElementType.getObjCLifetime() != Qualifiers::OCL_ExplicitNone) {
    // Instant objects:
    //   Default-initialize Objective-C pointers to NULL.
    CXXMemberInit
      = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Field, 
                                                 Loc, Loc, 
                 new (SemaRef.Context) ImplicitValueInitExpr(Field->getType()), 
                                                 Loc);
    return false;
  }
      
  // Nothing to initialize.
  CXXMemberInit = 0;
  return false;
}

namespace {
struct BaseAndFieldInfo {
  Sema &S;
  CXXConstructorDecl *Ctor;
  bool AnyErrorsInInits;
  ImplicitInitializerKind IIK;
  llvm::DenseMap<const void *, CXXCtorInitializer*> AllBaseFields;
  SmallVector<CXXCtorInitializer*, 8> AllToInit;

  BaseAndFieldInfo(Sema &S, CXXConstructorDecl *Ctor, bool ErrorsInInits)
    : S(S), Ctor(Ctor), AnyErrorsInInits(ErrorsInInits) {
    bool Generated = Ctor->isImplicit() || Ctor->isDefaulted();
    if (Generated && Ctor->isCopyConstructor())
      IIK = IIK_Copy;
    else if (Generated && Ctor->isMoveConstructor())
      IIK = IIK_Move;
    else
      IIK = IIK_Default;
  }
};
}

/// \brief Determine whether the given indirect field declaration is somewhere
/// within an anonymous union.
static bool isWithinAnonymousUnion(IndirectFieldDecl *F) {
  for (IndirectFieldDecl::chain_iterator C = F->chain_begin(), 
                                      CEnd = F->chain_end();
       C != CEnd; ++C)
    if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>((*C)->getDeclContext()))
      if (Record->isUnion())
        return true;
        
  return false;
}

static bool CollectFieldInitializer(Sema &SemaRef, BaseAndFieldInfo &Info,
                                    FieldDecl *Field, 
                                    IndirectFieldDecl *Indirect = 0) {

  // Overwhelmingly common case: we have a direct initializer for this field.
  if (CXXCtorInitializer *Init = Info.AllBaseFields.lookup(Field)) {
    Info.AllToInit.push_back(Init);
    return false;
  }

  // C++0x [class.base.init]p8: if the entity is a non-static data member that
  // has a brace-or-equal-initializer, the entity is initialized as specified
  // in [dcl.init].
  if (Field->hasInClassInitializer()) {
    CXXCtorInitializer *Init;
    if (Indirect)
      Init = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Indirect,
                                                      SourceLocation(),
                                                      SourceLocation(), 0,
                                                      SourceLocation());
    else
      Init = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Field,
                                                      SourceLocation(),
                                                      SourceLocation(), 0,
                                                      SourceLocation());
    Info.AllToInit.push_back(Init);
    return false;
  }

  // Don't build an implicit initializer for union members if none was
  // explicitly specified.
  if (Field->getParent()->isUnion() ||
      (Indirect && isWithinAnonymousUnion(Indirect)))
    return false;

  // Don't try to build an implicit initializer if there were semantic
  // errors in any of the initializers (and therefore we might be
  // missing some that the user actually wrote).
  if (Info.AnyErrorsInInits || Field->isInvalidDecl())
    return false;

  CXXCtorInitializer *Init = 0;
  if (BuildImplicitMemberInitializer(Info.S, Info.Ctor, Info.IIK, Field,
                                     Indirect, Init))
    return true;

  if (Init)
    Info.AllToInit.push_back(Init);

  return false;
}

bool
Sema::SetDelegatingInitializer(CXXConstructorDecl *Constructor,
                               CXXCtorInitializer *Initializer) {
  assert(Initializer->isDelegatingInitializer());
  Constructor->setNumCtorInitializers(1);
  CXXCtorInitializer **initializer =
    new (Context) CXXCtorInitializer*[1];
  memcpy(initializer, &Initializer, sizeof (CXXCtorInitializer*));
  Constructor->setCtorInitializers(initializer);

  if (CXXDestructorDecl *Dtor = LookupDestructor(Constructor->getParent())) {
    MarkDeclarationReferenced(Initializer->getSourceLocation(), Dtor);
    DiagnoseUseOfDecl(Dtor, Initializer->getSourceLocation());
  }

  DelegatingCtorDecls.push_back(Constructor);

  return false;
}

bool Sema::SetCtorInitializers(CXXConstructorDecl *Constructor,
                               CXXCtorInitializer **Initializers,
                               unsigned NumInitializers,
                               bool AnyErrors) {
  if (Constructor->isDependentContext()) {
    // Just store the initializers as written, they will be checked during
    // instantiation.
    if (NumInitializers > 0) {
      Constructor->setNumCtorInitializers(NumInitializers);
      CXXCtorInitializer **baseOrMemberInitializers =
        new (Context) CXXCtorInitializer*[NumInitializers];
      memcpy(baseOrMemberInitializers, Initializers,
             NumInitializers * sizeof(CXXCtorInitializer*));
      Constructor->setCtorInitializers(baseOrMemberInitializers);
    }
    
    return false;
  }

  BaseAndFieldInfo Info(*this, Constructor, AnyErrors);

  // We need to build the initializer AST according to order of construction
  // and not what user specified in the Initializers list.
  CXXRecordDecl *ClassDecl = Constructor->getParent()->getDefinition();
  if (!ClassDecl)
    return true;
  
  bool HadError = false;

  for (unsigned i = 0; i < NumInitializers; i++) {
    CXXCtorInitializer *Member = Initializers[i];
    
    if (Member->isBaseInitializer())
      Info.AllBaseFields[Member->getBaseClass()->getAs<RecordType>()] = Member;
    else
      Info.AllBaseFields[Member->getAnyMember()] = Member;
  }

  // Keep track of the direct virtual bases.
  llvm::SmallPtrSet<CXXBaseSpecifier *, 16> DirectVBases;
  for (CXXRecordDecl::base_class_iterator I = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); I != E; ++I) {
    if (I->isVirtual())
      DirectVBases.insert(I);
  }

  // Push virtual bases before others.
  for (CXXRecordDecl::base_class_iterator VBase = ClassDecl->vbases_begin(),
       E = ClassDecl->vbases_end(); VBase != E; ++VBase) {

    if (CXXCtorInitializer *Value
        = Info.AllBaseFields.lookup(VBase->getType()->getAs<RecordType>())) {
      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors) {
      bool IsInheritedVirtualBase = !DirectVBases.count(VBase);
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       VBase, IsInheritedVirtualBase, 
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Non-virtual bases.
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); Base != E; ++Base) {
    // Virtuals are in the virtual base list and already constructed.
    if (Base->isVirtual())
      continue;

    if (CXXCtorInitializer *Value
          = Info.AllBaseFields.lookup(Base->getType()->getAs<RecordType>())) {
      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors) {
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       Base, /*IsInheritedVirtualBase=*/false,
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Fields.
  for (DeclContext::decl_iterator Mem = ClassDecl->decls_begin(),
                               MemEnd = ClassDecl->decls_end();
       Mem != MemEnd; ++Mem) {
    if (FieldDecl *F = dyn_cast<FieldDecl>(*Mem)) {
      // C++ [class.bit]p2:
      //   A declaration for a bit-field that omits the identifier declares an
      //   unnamed bit-field. Unnamed bit-fields are not members and cannot be
      //   initialized.
      if (F->isUnnamedBitfield())
        continue;
      
      if (F->getType()->isIncompleteArrayType()) {
        assert(ClassDecl->hasFlexibleArrayMember() &&
               "Incomplete array type is not valid");
        continue;
      }
      
      // If we're not generating the implicit copy/move constructor, then we'll
      // handle anonymous struct/union fields based on their individual
      // indirect fields.
      if (F->isAnonymousStructOrUnion() && Info.IIK == IIK_Default)
        continue;
          
      if (CollectFieldInitializer(*this, Info, F))
        HadError = true;
      continue;
    }
    
    // Beyond this point, we only consider default initialization.
    if (Info.IIK != IIK_Default)
      continue;
    
    if (IndirectFieldDecl *F = dyn_cast<IndirectFieldDecl>(*Mem)) {
      if (F->getType()->isIncompleteArrayType()) {
        assert(ClassDecl->hasFlexibleArrayMember() &&
               "Incomplete array type is not valid");
        continue;
      }
      
      // Initialize each field of an anonymous struct individually.
      if (CollectFieldInitializer(*this, Info, F->getAnonField(), F))
        HadError = true;
      
      continue;        
    }
  }

  NumInitializers = Info.AllToInit.size();
  if (NumInitializers > 0) {
    Constructor->setNumCtorInitializers(NumInitializers);
    CXXCtorInitializer **baseOrMemberInitializers =
      new (Context) CXXCtorInitializer*[NumInitializers];
    memcpy(baseOrMemberInitializers, Info.AllToInit.data(),
           NumInitializers * sizeof(CXXCtorInitializer*));
    Constructor->setCtorInitializers(baseOrMemberInitializers);

    // Constructors implicitly reference the base and member
    // destructors.
    MarkBaseAndMemberDestructorsReferenced(Constructor->getLocation(),
                                           Constructor->getParent());
  }

  return HadError;
}

static void *GetKeyForTopLevelField(FieldDecl *Field) {
  // For anonymous unions, use the class declaration as the key.
  if (const RecordType *RT = Field->getType()->getAs<RecordType>()) {
    if (RT->getDecl()->isAnonymousStructOrUnion())
      return static_cast<void *>(RT->getDecl());
  }
  return static_cast<void *>(Field);
}

static void *GetKeyForBase(ASTContext &Context, QualType BaseType) {
  return const_cast<Type*>(Context.getCanonicalType(BaseType).getTypePtr());
}

static void *GetKeyForMember(ASTContext &Context,
                             CXXCtorInitializer *Member) {
  if (!Member->isAnyMemberInitializer())
    return GetKeyForBase(Context, QualType(Member->getBaseClass(), 0));
    
  // For fields injected into the class via declaration of an anonymous union,
  // use its anonymous union class declaration as the unique key.
  FieldDecl *Field = Member->getAnyMember();
 
  // If the field is a member of an anonymous struct or union, our key
  // is the anonymous record decl that's a direct child of the class.
  RecordDecl *RD = Field->getParent();
  if (RD->isAnonymousStructOrUnion()) {
    while (true) {
      RecordDecl *Parent = cast<RecordDecl>(RD->getDeclContext());
      if (Parent->isAnonymousStructOrUnion())
        RD = Parent;
      else
        break;
    }
      
    return static_cast<void *>(RD);
  }

  return static_cast<void *>(Field);
}

static void
DiagnoseBaseOrMemInitializerOrder(Sema &SemaRef,
                                  const CXXConstructorDecl *Constructor,
                                  CXXCtorInitializer **Inits,
                                  unsigned NumInits) {
  if (Constructor->getDeclContext()->isDependentContext())
    return;

  // Don't check initializers order unless the warning is enabled at the
  // location of at least one initializer. 
  bool ShouldCheckOrder = false;
  for (unsigned InitIndex = 0; InitIndex != NumInits; ++InitIndex) {
    CXXCtorInitializer *Init = Inits[InitIndex];
    if (SemaRef.Diags.getDiagnosticLevel(diag::warn_initializer_out_of_order,
                                         Init->getSourceLocation())
          != DiagnosticsEngine::Ignored) {
      ShouldCheckOrder = true;
      break;
    }
  }
  if (!ShouldCheckOrder)
    return;
  
  // Build the list of bases and members in the order that they'll
  // actually be initialized.  The explicit initializers should be in
  // this same order but may be missing things.
  SmallVector<const void*, 32> IdealInitKeys;

  const CXXRecordDecl *ClassDecl = Constructor->getParent();

  // 1. Virtual bases.
  for (CXXRecordDecl::base_class_const_iterator VBase =
       ClassDecl->vbases_begin(),
       E = ClassDecl->vbases_end(); VBase != E; ++VBase)
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, VBase->getType()));

  // 2. Non-virtual bases.
  for (CXXRecordDecl::base_class_const_iterator Base = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); Base != E; ++Base) {
    if (Base->isVirtual())
      continue;
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, Base->getType()));
  }

  // 3. Direct fields.
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
       E = ClassDecl->field_end(); Field != E; ++Field) {
    if (Field->isUnnamedBitfield())
      continue;
    
    IdealInitKeys.push_back(GetKeyForTopLevelField(*Field));
  }
  
  unsigned NumIdealInits = IdealInitKeys.size();
  unsigned IdealIndex = 0;

  CXXCtorInitializer *PrevInit = 0;
  for (unsigned InitIndex = 0; InitIndex != NumInits; ++InitIndex) {
    CXXCtorInitializer *Init = Inits[InitIndex];
    void *InitKey = GetKeyForMember(SemaRef.Context, Init);

    // Scan forward to try to find this initializer in the idealized
    // initializers list.
    for (; IdealIndex != NumIdealInits; ++IdealIndex)
      if (InitKey == IdealInitKeys[IdealIndex])
        break;

    // If we didn't find this initializer, it must be because we
    // scanned past it on a previous iteration.  That can only
    // happen if we're out of order;  emit a warning.
    if (IdealIndex == NumIdealInits && PrevInit) {
      Sema::SemaDiagnosticBuilder D =
        SemaRef.Diag(PrevInit->getSourceLocation(),
                     diag::warn_initializer_out_of_order);

      if (PrevInit->isAnyMemberInitializer())
        D << 0 << PrevInit->getAnyMember()->getDeclName();
      else
        D << 1 << PrevInit->getBaseClassInfo()->getType();
      
      if (Init->isAnyMemberInitializer())
        D << 0 << Init->getAnyMember()->getDeclName();
      else
        D << 1 << Init->getBaseClassInfo()->getType();

      // Move back to the initializer's location in the ideal list.
      for (IdealIndex = 0; IdealIndex != NumIdealInits; ++IdealIndex)
        if (InitKey == IdealInitKeys[IdealIndex])
          break;

      assert(IdealIndex != NumIdealInits &&
             "initializer not found in initializer list");
    }

    PrevInit = Init;
  }
}

namespace {
bool CheckRedundantInit(Sema &S,
                        CXXCtorInitializer *Init,
                        CXXCtorInitializer *&PrevInit) {
  if (!PrevInit) {
    PrevInit = Init;
    return false;
  }

  if (FieldDecl *Field = Init->getMember())
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_mem_initialization)
      << Field->getDeclName()
      << Init->getSourceRange();
  else {
    const Type *BaseClass = Init->getBaseClass();
    assert(BaseClass && "neither field nor base");
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_base_initialization)
      << QualType(BaseClass, 0)
      << Init->getSourceRange();
  }
  S.Diag(PrevInit->getSourceLocation(), diag::note_previous_initializer)
    << 0 << PrevInit->getSourceRange();

  return true;
}

typedef std::pair<NamedDecl *, CXXCtorInitializer *> UnionEntry;
typedef llvm::DenseMap<RecordDecl*, UnionEntry> RedundantUnionMap;

bool CheckRedundantUnionInit(Sema &S,
                             CXXCtorInitializer *Init,
                             RedundantUnionMap &Unions) {
  FieldDecl *Field = Init->getAnyMember();
  RecordDecl *Parent = Field->getParent();
  if (!Parent->isAnonymousStructOrUnion())
    return false;

  NamedDecl *Child = Field;
  do {
    if (Parent->isUnion()) {
      UnionEntry &En = Unions[Parent];
      if (En.first && En.first != Child) {
        S.Diag(Init->getSourceLocation(),
               diag::err_multiple_mem_union_initialization)
          << Field->getDeclName()
          << Init->getSourceRange();
        S.Diag(En.second->getSourceLocation(), diag::note_previous_initializer)
          << 0 << En.second->getSourceRange();
        return true;
      } else if (!En.first) {
        En.first = Child;
        En.second = Init;
      }
    }

    Child = Parent;
    Parent = cast<RecordDecl>(Parent->getDeclContext());
  } while (Parent->isAnonymousStructOrUnion());

  return false;
}
}

/// ActOnMemInitializers - Handle the member initializers for a constructor.
void Sema::ActOnMemInitializers(Decl *ConstructorDecl,
                                SourceLocation ColonLoc,
                                CXXCtorInitializer **meminits,
                                unsigned NumMemInits,
                                bool AnyErrors) {
  if (!ConstructorDecl)
    return;

  AdjustDeclIfTemplate(ConstructorDecl);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorDecl);

  if (!Constructor) {
    Diag(ColonLoc, diag::err_only_constructors_take_base_inits);
    return;
  }
  
  CXXCtorInitializer **MemInits =
    reinterpret_cast<CXXCtorInitializer **>(meminits);

  // Mapping for the duplicate initializers check.
  // For member initializers, this is keyed with a FieldDecl*.
  // For base initializers, this is keyed with a Type*.
  llvm::DenseMap<void*, CXXCtorInitializer *> Members;

  // Mapping for the inconsistent anonymous-union initializers check.
  RedundantUnionMap MemberUnions;

  bool HadError = false;
  for (unsigned i = 0; i < NumMemInits; i++) {
    CXXCtorInitializer *Init = MemInits[i];

    // Set the source order index.
    Init->setSourceOrder(i);

    if (Init->isAnyMemberInitializer()) {
      FieldDecl *Field = Init->getAnyMember();
      if (CheckRedundantInit(*this, Init, Members[Field]) ||
          CheckRedundantUnionInit(*this, Init, MemberUnions))
        HadError = true;
    } else if (Init->isBaseInitializer()) {
      void *Key = GetKeyForBase(Context, QualType(Init->getBaseClass(), 0));
      if (CheckRedundantInit(*this, Init, Members[Key]))
        HadError = true;
    } else {
      assert(Init->isDelegatingInitializer());
      // This must be the only initializer
      if (i != 0 || NumMemInits > 1) {
        Diag(MemInits[0]->getSourceLocation(),
             diag::err_delegating_initializer_alone)
          << MemInits[0]->getSourceRange();
        HadError = true;
        // We will treat this as being the only initializer.
      }
      SetDelegatingInitializer(Constructor, MemInits[i]);
      // Return immediately as the initializer is set.
      return;
    }
  }

  if (HadError)
    return;

  DiagnoseBaseOrMemInitializerOrder(*this, Constructor, MemInits, NumMemInits);

  SetCtorInitializers(Constructor, MemInits, NumMemInits, AnyErrors);
}

void
Sema::MarkBaseAndMemberDestructorsReferenced(SourceLocation Location,
                                             CXXRecordDecl *ClassDecl) {
  // Ignore dependent contexts. Also ignore unions, since their members never
  // have destructors implicitly called.
  if (ClassDecl->isDependentContext() || ClassDecl->isUnion())
    return;

  // FIXME: all the access-control diagnostics are positioned on the
  // field/base declaration.  That's probably good; that said, the
  // user might reasonably want to know why the destructor is being
  // emitted, and we currently don't say.
  
  // Non-static data members.
  for (CXXRecordDecl::field_iterator I = ClassDecl->field_begin(),
       E = ClassDecl->field_end(); I != E; ++I) {
    FieldDecl *Field = *I;
    if (Field->isInvalidDecl())
      continue;
    QualType FieldType = Context.getBaseElementType(Field->getType());
    
    const RecordType* RT = FieldType->getAs<RecordType>();
    if (!RT)
      continue;
    
    CXXRecordDecl *FieldClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    if (FieldClassDecl->isInvalidDecl())
      continue;
    if (FieldClassDecl->hasTrivialDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(FieldClassDecl);
    assert(Dtor && "No dtor found for FieldClassDecl!");
    CheckDestructorAccess(Field->getLocation(), Dtor,
                          PDiag(diag::err_access_dtor_field)
                            << Field->getDeclName()
                            << FieldType);

    MarkDeclarationReferenced(Location, const_cast<CXXDestructorDecl*>(Dtor));
  }

  llvm::SmallPtrSet<const RecordType *, 8> DirectVirtualBases;

  // Bases.
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); Base != E; ++Base) {
    // Bases are always records in a well-formed non-dependent class.
    const RecordType *RT = Base->getType()->getAs<RecordType>();

    // Remember direct virtual bases.
    if (Base->isVirtual())
      DirectVirtualBases.insert(RT);

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    // Ignore trivial destructors.
    if (BaseClassDecl->hasTrivialDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    assert(Dtor && "No dtor found for BaseClassDecl!");

    // FIXME: caret should be on the start of the class name
    CheckDestructorAccess(Base->getSourceRange().getBegin(), Dtor,
                          PDiag(diag::err_access_dtor_base)
                            << Base->getType()
                            << Base->getSourceRange());
    
    MarkDeclarationReferenced(Location, const_cast<CXXDestructorDecl*>(Dtor));
  }
  
  // Virtual bases.
  for (CXXRecordDecl::base_class_iterator VBase = ClassDecl->vbases_begin(),
       E = ClassDecl->vbases_end(); VBase != E; ++VBase) {

    // Bases are always records in a well-formed non-dependent class.
    const RecordType *RT = VBase->getType()->getAs<RecordType>();

    // Ignore direct virtual bases.
    if (DirectVirtualBases.count(RT))
      continue;

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    // Ignore trivial destructors.
    if (BaseClassDecl->hasTrivialDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    assert(Dtor && "No dtor found for BaseClassDecl!");
    CheckDestructorAccess(ClassDecl->getLocation(), Dtor,
                          PDiag(diag::err_access_dtor_vbase)
                            << VBase->getType());

    MarkDeclarationReferenced(Location, const_cast<CXXDestructorDecl*>(Dtor));
  }
}

void Sema::ActOnDefaultCtorInitializers(Decl *CDtorDecl) {
  if (!CDtorDecl)
    return;

  if (CXXConstructorDecl *Constructor
      = dyn_cast<CXXConstructorDecl>(CDtorDecl))
    SetCtorInitializers(Constructor, 0, 0, /*AnyErrors=*/false);
}

bool Sema::RequireNonAbstractType(SourceLocation Loc, QualType T,
                                  unsigned DiagID, AbstractDiagSelID SelID) {
  if (SelID == -1)
    return RequireNonAbstractType(Loc, T, PDiag(DiagID));
  else
    return RequireNonAbstractType(Loc, T, PDiag(DiagID) << SelID);
}

bool Sema::RequireNonAbstractType(SourceLocation Loc, QualType T,
                                  const PartialDiagnostic &PD) {
  if (!getLangOptions().CPlusPlus)
    return false;

  if (const ArrayType *AT = Context.getAsArrayType(T))
    return RequireNonAbstractType(Loc, AT->getElementType(), PD);

  if (const PointerType *PT = T->getAs<PointerType>()) {
    // Find the innermost pointer type.
    while (const PointerType *T = PT->getPointeeType()->getAs<PointerType>())
      PT = T;

    if (const ArrayType *AT = Context.getAsArrayType(PT->getPointeeType()))
      return RequireNonAbstractType(Loc, AT->getElementType(), PD);
  }

  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;

  const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());

  // We can't answer whether something is abstract until it has a
  // definition.  If it's currently being defined, we'll walk back
  // over all the declarations when we have a full definition.
  const CXXRecordDecl *Def = RD->getDefinition();
  if (!Def || Def->isBeingDefined())
    return false;

  if (!RD->isAbstract())
    return false;

  Diag(Loc, PD) << RD->getDeclName();
  DiagnoseAbstractType(RD);

  return true;
}

void Sema::DiagnoseAbstractType(const CXXRecordDecl *RD) {
  // Check if we've already emitted the list of pure virtual functions
  // for this class.
  if (PureVirtualClassDiagSet && PureVirtualClassDiagSet->count(RD))
    return;

  CXXFinalOverriderMap FinalOverriders;
  RD->getFinalOverriders(FinalOverriders);

  // Keep a set of seen pure methods so we won't diagnose the same method
  // more than once.
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> SeenPureMethods;
  
  for (CXXFinalOverriderMap::iterator M = FinalOverriders.begin(), 
                                   MEnd = FinalOverriders.end();
       M != MEnd; 
       ++M) {
    for (OverridingMethods::iterator SO = M->second.begin(), 
                                  SOEnd = M->second.end();
         SO != SOEnd; ++SO) {
      // C++ [class.abstract]p4:
      //   A class is abstract if it contains or inherits at least one
      //   pure virtual function for which the final overrider is pure
      //   virtual.

      // 
      if (SO->second.size() != 1)
        continue;

      if (!SO->second.front().Method->isPure())
        continue;

      if (!SeenPureMethods.insert(SO->second.front().Method))
        continue;

      Diag(SO->second.front().Method->getLocation(), 
           diag::note_pure_virtual_function) 
        << SO->second.front().Method->getDeclName() << RD->getDeclName();
    }
  }

  if (!PureVirtualClassDiagSet)
    PureVirtualClassDiagSet.reset(new RecordDeclSetTy);
  PureVirtualClassDiagSet->insert(RD);
}

namespace {
struct AbstractUsageInfo {
  Sema &S;
  CXXRecordDecl *Record;
  CanQualType AbstractType;
  bool Invalid;

  AbstractUsageInfo(Sema &S, CXXRecordDecl *Record)
    : S(S), Record(Record),
      AbstractType(S.Context.getCanonicalType(
                   S.Context.getTypeDeclType(Record))),
      Invalid(false) {}

  void DiagnoseAbstractType() {
    if (Invalid) return;
    S.DiagnoseAbstractType(Record);
    Invalid = true;
  }

  void CheckType(const NamedDecl *D, TypeLoc TL, Sema::AbstractDiagSelID Sel);
};

struct CheckAbstractUsage {
  AbstractUsageInfo &Info;
  const NamedDecl *Ctx;

  CheckAbstractUsage(AbstractUsageInfo &Info, const NamedDecl *Ctx)
    : Info(Info), Ctx(Ctx) {}

  void Visit(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    case TypeLoc::CLASS: Check(cast<CLASS##TypeLoc>(TL), Sel); break;
#include "clang/AST/TypeLocNodes.def"
    }
  }

  void Check(FunctionProtoTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getResultLoc(), Sema::AbstractReturnType);
    for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I) {
      if (!TL.getArg(I))
        continue;
      
      TypeSourceInfo *TSI = TL.getArg(I)->getTypeSourceInfo();
      if (TSI) Visit(TSI->getTypeLoc(), Sema::AbstractParamType);
    }
  }

  void Check(ArrayTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getElementLoc(), Sema::AbstractArrayType);
  }

  void Check(TemplateSpecializationTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Visit the type parameters from a permissive context.
    for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I) {
      TemplateArgumentLoc TAL = TL.getArgLoc(I);
      if (TAL.getArgument().getKind() == TemplateArgument::Type)
        if (TypeSourceInfo *TSI = TAL.getTypeSourceInfo())
          Visit(TSI->getTypeLoc(), Sema::AbstractNone);
      // TODO: other template argument types?
    }
  }

  // Visit pointee types from a permissive context.
#define CheckPolymorphic(Type) \
  void Check(Type TL, Sema::AbstractDiagSelID Sel) { \
    Visit(TL.getNextTypeLoc(), Sema::AbstractNone); \
  }
  CheckPolymorphic(PointerTypeLoc)
  CheckPolymorphic(ReferenceTypeLoc)
  CheckPolymorphic(MemberPointerTypeLoc)
  CheckPolymorphic(BlockPointerTypeLoc)
  CheckPolymorphic(AtomicTypeLoc)

  /// Handle all the types we haven't given a more specific
  /// implementation for above.
  void Check(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Every other kind of type that we haven't called out already
    // that has an inner type is either (1) sugar or (2) contains that
    // inner type in some way as a subobject.
    if (TypeLoc Next = TL.getNextTypeLoc())
      return Visit(Next, Sel);

    // If there's no inner type and we're in a permissive context,
    // don't diagnose.
    if (Sel == Sema::AbstractNone) return;

    // Check whether the type matches the abstract type.
    QualType T = TL.getType();
    if (T->isArrayType()) {
      Sel = Sema::AbstractArrayType;
      T = Info.S.Context.getBaseElementType(T);
    }
    CanQualType CT = T->getCanonicalTypeUnqualified().getUnqualifiedType();
    if (CT != Info.AbstractType) return;

    // It matched; do some magic.
    if (Sel == Sema::AbstractArrayType) {
      Info.S.Diag(Ctx->getLocation(), diag::err_array_of_abstract_type)
        << T << TL.getSourceRange();
    } else {
      Info.S.Diag(Ctx->getLocation(), diag::err_abstract_type_in_decl)
        << Sel << T << TL.getSourceRange();
    }
    Info.DiagnoseAbstractType();
  }
};

void AbstractUsageInfo::CheckType(const NamedDecl *D, TypeLoc TL,
                                  Sema::AbstractDiagSelID Sel) {
  CheckAbstractUsage(*this, D).Visit(TL, Sel);
}

}

/// Check for invalid uses of an abstract type in a method declaration.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    CXXMethodDecl *MD) {
  // No need to do the check on definitions, which require that
  // the return/param types be complete.
  if (MD->doesThisDeclarationHaveABody())
    return;

  // For safety's sake, just ignore it if we don't have type source
  // information.  This should never happen for non-implicit methods,
  // but...
  if (TypeSourceInfo *TSI = MD->getTypeSourceInfo())
    Info.CheckType(MD, TSI->getTypeLoc(), Sema::AbstractNone);
}

/// Check for invalid uses of an abstract type within a class definition.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    CXXRecordDecl *RD) {
  for (CXXRecordDecl::decl_iterator
         I = RD->decls_begin(), E = RD->decls_end(); I != E; ++I) {
    Decl *D = *I;
    if (D->isImplicit()) continue;

    // Methods and method templates.
    if (isa<CXXMethodDecl>(D)) {
      CheckAbstractClassUsage(Info, cast<CXXMethodDecl>(D));
    } else if (isa<FunctionTemplateDecl>(D)) {
      FunctionDecl *FD = cast<FunctionTemplateDecl>(D)->getTemplatedDecl();
      CheckAbstractClassUsage(Info, cast<CXXMethodDecl>(FD));

    // Fields and static variables.
    } else if (isa<FieldDecl>(D)) {
      FieldDecl *FD = cast<FieldDecl>(D);
      if (TypeSourceInfo *TSI = FD->getTypeSourceInfo())
        Info.CheckType(FD, TSI->getTypeLoc(), Sema::AbstractFieldType);
    } else if (isa<VarDecl>(D)) {
      VarDecl *VD = cast<VarDecl>(D);
      if (TypeSourceInfo *TSI = VD->getTypeSourceInfo())
        Info.CheckType(VD, TSI->getTypeLoc(), Sema::AbstractVariableType);

    // Nested classes and class templates.
    } else if (isa<CXXRecordDecl>(D)) {
      CheckAbstractClassUsage(Info, cast<CXXRecordDecl>(D));
    } else if (isa<ClassTemplateDecl>(D)) {
      CheckAbstractClassUsage(Info,
                             cast<ClassTemplateDecl>(D)->getTemplatedDecl());
    }
  }
}

/// \brief Perform semantic checks on a class definition that has been
/// completing, introducing implicitly-declared members, checking for
/// abstract types, etc.
void Sema::CheckCompletedCXXClass(CXXRecordDecl *Record) {
  if (!Record)
    return;

  if (Record->isAbstract() && !Record->isInvalidDecl()) {
    AbstractUsageInfo Info(*this, Record);
    CheckAbstractClassUsage(Info, Record);
  }
  
  // If this is not an aggregate type and has no user-declared constructor,
  // complain about any non-static data members of reference or const scalar
  // type, since they will never get initializers.
  if (!Record->isInvalidDecl() && !Record->isDependentType() &&
      !Record->isAggregate() && !Record->hasUserDeclaredConstructor()) {
    bool Complained = false;
    for (RecordDecl::field_iterator F = Record->field_begin(), 
                                 FEnd = Record->field_end();
         F != FEnd; ++F) {
      if (F->hasInClassInitializer() || F->isUnnamedBitfield())
        continue;

      if (F->getType()->isReferenceType() ||
          (F->getType().isConstQualified() && F->getType()->isScalarType())) {
        if (!Complained) {
          Diag(Record->getLocation(), diag::warn_no_constructor_for_refconst)
            << Record->getTagKind() << Record;
          Complained = true;
        }
        
        Diag(F->getLocation(), diag::note_refconst_member_not_initialized)
          << F->getType()->isReferenceType()
          << F->getDeclName();
      }
    }
  }

  if (Record->isDynamicClass() && !Record->isDependentType())
    DynamicClasses.push_back(Record);

  if (Record->getIdentifier()) {
    // C++ [class.mem]p13:
    //   If T is the name of a class, then each of the following shall have a 
    //   name different from T:
    //     - every member of every anonymous union that is a member of class T.
    //
    // C++ [class.mem]p14:
    //   In addition, if class T has a user-declared constructor (12.1), every 
    //   non-static data member of class T shall have a name different from T.
    for (DeclContext::lookup_result R = Record->lookup(Record->getDeclName());
         R.first != R.second; ++R.first) {
      NamedDecl *D = *R.first;
      if ((isa<FieldDecl>(D) && Record->hasUserDeclaredConstructor()) ||
          isa<IndirectFieldDecl>(D)) {
        Diag(D->getLocation(), diag::err_member_name_of_class)
          << D->getDeclName();
        break;
      }
    }
  }

  // Warn if the class has virtual methods but non-virtual public destructor.
  if (Record->isPolymorphic() && !Record->isDependentType()) {
    CXXDestructorDecl *dtor = Record->getDestructor();
    if (!dtor || (!dtor->isVirtual() && dtor->getAccess() == AS_public))
      Diag(dtor ? dtor->getLocation() : Record->getLocation(),
           diag::warn_non_virtual_dtor) << Context.getRecordType(Record);
  }

  // See if a method overloads virtual methods in a base
  /// class without overriding any.
  if (!Record->isDependentType()) {
    for (CXXRecordDecl::method_iterator M = Record->method_begin(),
                                     MEnd = Record->method_end();
         M != MEnd; ++M) {
      if (!(*M)->isStatic())
        DiagnoseHiddenVirtualMethods(Record, *M);
    }
  }

  // C++0x [dcl.constexpr]p8: A constexpr specifier for a non-static member
  // function that is not a constructor declares that member function to be
  // const. [...] The class of which that function is a member shall be
  // a literal type.
  //
  // It's fine to diagnose constructors here too: such constructors cannot
  // produce a constant expression, so are ill-formed (no diagnostic required).
  //
  // If the class has virtual bases, any constexpr members will already have
  // been diagnosed by the checks performed on the member declaration, so
  // suppress this (less useful) diagnostic.
  if (LangOpts.CPlusPlus0x && !Record->isDependentType() &&
      !Record->isLiteral() && !Record->getNumVBases()) {
    for (CXXRecordDecl::method_iterator M = Record->method_begin(),
                                     MEnd = Record->method_end();
         M != MEnd; ++M) {
      if ((*M)->isConstexpr()) {
        switch (Record->getTemplateSpecializationKind()) {
        case TSK_ImplicitInstantiation:
        case TSK_ExplicitInstantiationDeclaration:
        case TSK_ExplicitInstantiationDefinition:
          // If a template instantiates to a non-literal type, but its members
          // instantiate to constexpr functions, the template is technically
          // ill-formed, but we allow it for sanity. Such members are treated as
          // non-constexpr.
          (*M)->setConstexpr(false);
          continue;

        case TSK_Undeclared:
        case TSK_ExplicitSpecialization:
          RequireLiteralType((*M)->getLocation(), Context.getRecordType(Record),
                             PDiag(diag::err_constexpr_method_non_literal));
          break;
        }

        // Only produce one error per class.
        break;
      }
    }
  }

  // Declare inherited constructors. We do this eagerly here because:
  // - The standard requires an eager diagnostic for conflicting inherited
  //   constructors from different classes.
  // - The lazy declaration of the other implicit constructors is so as to not
  //   waste space and performance on classes that are not meant to be
  //   instantiated (e.g. meta-functions). This doesn't apply to classes that
  //   have inherited constructors.
  DeclareInheritedConstructors(Record);

  if (!Record->isDependentType())
    CheckExplicitlyDefaultedMethods(Record);
}

void Sema::CheckExplicitlyDefaultedMethods(CXXRecordDecl *Record) {
  for (CXXRecordDecl::method_iterator MI = Record->method_begin(),
                                      ME = Record->method_end();
       MI != ME; ++MI) {
    if (!MI->isInvalidDecl() && MI->isExplicitlyDefaulted()) {
      switch (getSpecialMember(*MI)) {
      case CXXDefaultConstructor:
        CheckExplicitlyDefaultedDefaultConstructor(
                                                  cast<CXXConstructorDecl>(*MI));
        break;

      case CXXDestructor:
        CheckExplicitlyDefaultedDestructor(cast<CXXDestructorDecl>(*MI));
        break;

      case CXXCopyConstructor:
        CheckExplicitlyDefaultedCopyConstructor(cast<CXXConstructorDecl>(*MI));
        break;

      case CXXCopyAssignment:
        CheckExplicitlyDefaultedCopyAssignment(*MI);
        break;

      case CXXMoveConstructor:
        CheckExplicitlyDefaultedMoveConstructor(cast<CXXConstructorDecl>(*MI));
        break;

      case CXXMoveAssignment:
        CheckExplicitlyDefaultedMoveAssignment(*MI);
        break;

      case CXXInvalid:
        llvm_unreachable("non-special member explicitly defaulted!");
      }
    }
  }

}

void Sema::CheckExplicitlyDefaultedDefaultConstructor(CXXConstructorDecl *CD) {
  assert(CD->isExplicitlyDefaulted() && CD->isDefaultConstructor());
  
  // Whether this was the first-declared instance of the constructor.
  // This affects whether we implicitly add an exception spec (and, eventually,
  // constexpr). It is also ill-formed to explicitly default a constructor such
  // that it would be deleted. (C++0x [decl.fct.def.default])
  bool First = CD == CD->getCanonicalDecl();

  bool HadError = false;
  if (CD->getNumParams() != 0) {
    Diag(CD->getLocation(), diag::err_defaulted_default_ctor_params)
      << CD->getSourceRange();
    HadError = true;
  }

  ImplicitExceptionSpecification Spec
    = ComputeDefaultedDefaultCtorExceptionSpec(CD->getParent());
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  if (EPI.ExceptionSpecType == EST_Delayed) {
    // Exception specification depends on some deferred part of the class. We'll
    // try again when the class's definition has been fully processed.
    return;
  }
  const FunctionProtoType *CtorType = CD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  if (CtorType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXDefaultConstructor,
          PDiag(),
          ExceptionType, SourceLocation(),
          CtorType, CD->getLocation())) {
      HadError = true;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // We know there are no parameters.
    EPI.ExtInfo = CtorType->getExtInfo();
    CD->setType(Context.getFunctionType(Context.VoidTy, 0, 0, EPI));
  }

  if (HadError) {
    CD->setInvalidDecl();
    return;
  }

  if (ShouldDeleteSpecialMember(CD, CXXDefaultConstructor)) {
    if (First) {
      CD->setDeletedAsWritten();
    } else {
      Diag(CD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXDefaultConstructor;
      CD->setInvalidDecl();
    }
  }
}

void Sema::CheckExplicitlyDefaultedCopyConstructor(CXXConstructorDecl *CD) {
  assert(CD->isExplicitlyDefaulted() && CD->isCopyConstructor());

  // Whether this was the first-declared instance of the constructor.
  bool First = CD == CD->getCanonicalDecl();

  bool HadError = false;
  if (CD->getNumParams() != 1) {
    Diag(CD->getLocation(), diag::err_defaulted_copy_ctor_params)
      << CD->getSourceRange();
    HadError = true;
  }

  ImplicitExceptionSpecification Spec(Context);
  bool Const;
  llvm::tie(Spec, Const) =
    ComputeDefaultedCopyCtorExceptionSpecAndConst(CD->getParent());
  
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  const FunctionProtoType *CtorType = CD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  // Check for parameter type matching.
  // This is a copy ctor so we know it's a cv-qualified reference to T.
  QualType ArgType = CtorType->getArgType(0);
  if (ArgType->getPointeeType().isVolatileQualified()) {
    Diag(CD->getLocation(), diag::err_defaulted_copy_ctor_volatile_param);
    HadError = true;
  }
  if (ArgType->getPointeeType().isConstQualified() && !Const) {
    Diag(CD->getLocation(), diag::err_defaulted_copy_ctor_const_param);
    HadError = true;
  }

  if (CtorType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXCopyConstructor,
          PDiag(),
          ExceptionType, SourceLocation(),
          CtorType, CD->getLocation())) {
      HadError = true;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // We duplicate the one parameter type.
    EPI.ExtInfo = CtorType->getExtInfo();
    CD->setType(Context.getFunctionType(Context.VoidTy, &ArgType, 1, EPI));
  }

  if (HadError) {
    CD->setInvalidDecl();
    return;
  }

  if (ShouldDeleteSpecialMember(CD, CXXCopyConstructor)) {
    if (First) {
      CD->setDeletedAsWritten();
    } else {
      Diag(CD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXCopyConstructor;
      CD->setInvalidDecl();
    }
  }
}

void Sema::CheckExplicitlyDefaultedCopyAssignment(CXXMethodDecl *MD) {
  assert(MD->isExplicitlyDefaulted());

  // Whether this was the first-declared instance of the operator
  bool First = MD == MD->getCanonicalDecl();

  bool HadError = false;
  if (MD->getNumParams() != 1) {
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_params)
      << MD->getSourceRange();
    HadError = true;
  }

  QualType ReturnType =
    MD->getType()->getAs<FunctionType>()->getResultType();
  if (!ReturnType->isLValueReferenceType() ||
      !Context.hasSameType(
        Context.getCanonicalType(ReturnType->getPointeeType()),
        Context.getCanonicalType(Context.getTypeDeclType(MD->getParent())))) {
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_return_type);
    HadError = true;
  }

  ImplicitExceptionSpecification Spec(Context);
  bool Const;
  llvm::tie(Spec, Const) =
    ComputeDefaultedCopyCtorExceptionSpecAndConst(MD->getParent());
  
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  const FunctionProtoType *OperType = MD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  QualType ArgType = OperType->getArgType(0);
  if (!ArgType->isLValueReferenceType()) {
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_not_ref);
    HadError = true;
  } else {
    if (ArgType->getPointeeType().isVolatileQualified()) {
      Diag(MD->getLocation(), diag::err_defaulted_copy_assign_volatile_param);
      HadError = true;
    }
    if (ArgType->getPointeeType().isConstQualified() && !Const) {
      Diag(MD->getLocation(), diag::err_defaulted_copy_assign_const_param);
      HadError = true;
    }
  }

  if (OperType->getTypeQuals()) {
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_quals);
    HadError = true;
  }

  if (OperType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXCopyAssignment,
          PDiag(),
          ExceptionType, SourceLocation(),
          OperType, MD->getLocation())) {
      HadError = true;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // We duplicate the one parameter type.
    EPI.RefQualifier = OperType->getRefQualifier();
    EPI.ExtInfo = OperType->getExtInfo();
    MD->setType(Context.getFunctionType(ReturnType, &ArgType, 1, EPI));
  }

  if (HadError) {
    MD->setInvalidDecl();
    return;
  }

  if (ShouldDeleteCopyAssignmentOperator(MD)) {
    if (First) {
      MD->setDeletedAsWritten();
    } else {
      Diag(MD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXCopyAssignment;
      MD->setInvalidDecl();
    }
  }
}

void Sema::CheckExplicitlyDefaultedMoveConstructor(CXXConstructorDecl *CD) {
  assert(CD->isExplicitlyDefaulted() && CD->isMoveConstructor());

  // Whether this was the first-declared instance of the constructor.
  bool First = CD == CD->getCanonicalDecl();

  bool HadError = false;
  if (CD->getNumParams() != 1) {
    Diag(CD->getLocation(), diag::err_defaulted_move_ctor_params)
      << CD->getSourceRange();
    HadError = true;
  }

  ImplicitExceptionSpecification Spec(
      ComputeDefaultedMoveCtorExceptionSpec(CD->getParent()));

  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  const FunctionProtoType *CtorType = CD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  // Check for parameter type matching.
  // This is a move ctor so we know it's a cv-qualified rvalue reference to T.
  QualType ArgType = CtorType->getArgType(0);
  if (ArgType->getPointeeType().isVolatileQualified()) {
    Diag(CD->getLocation(), diag::err_defaulted_move_ctor_volatile_param);
    HadError = true;
  }
  if (ArgType->getPointeeType().isConstQualified()) {
    Diag(CD->getLocation(), diag::err_defaulted_move_ctor_const_param);
    HadError = true;
  }

  if (CtorType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXMoveConstructor,
          PDiag(),
          ExceptionType, SourceLocation(),
          CtorType, CD->getLocation())) {
      HadError = true;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // We duplicate the one parameter type.
    EPI.ExtInfo = CtorType->getExtInfo();
    CD->setType(Context.getFunctionType(Context.VoidTy, &ArgType, 1, EPI));
  }

  if (HadError) {
    CD->setInvalidDecl();
    return;
  }

  if (ShouldDeleteSpecialMember(CD, CXXMoveConstructor)) {
    if (First) {
      CD->setDeletedAsWritten();
    } else {
      Diag(CD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXMoveConstructor;
      CD->setInvalidDecl();
    }
  }
}

void Sema::CheckExplicitlyDefaultedMoveAssignment(CXXMethodDecl *MD) {
  assert(MD->isExplicitlyDefaulted());

  // Whether this was the first-declared instance of the operator
  bool First = MD == MD->getCanonicalDecl();

  bool HadError = false;
  if (MD->getNumParams() != 1) {
    Diag(MD->getLocation(), diag::err_defaulted_move_assign_params)
      << MD->getSourceRange();
    HadError = true;
  }

  QualType ReturnType =
    MD->getType()->getAs<FunctionType>()->getResultType();
  if (!ReturnType->isLValueReferenceType() ||
      !Context.hasSameType(
        Context.getCanonicalType(ReturnType->getPointeeType()),
        Context.getCanonicalType(Context.getTypeDeclType(MD->getParent())))) {
    Diag(MD->getLocation(), diag::err_defaulted_move_assign_return_type);
    HadError = true;
  }

  ImplicitExceptionSpecification Spec(
      ComputeDefaultedMoveCtorExceptionSpec(MD->getParent()));
  
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  const FunctionProtoType *OperType = MD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  QualType ArgType = OperType->getArgType(0);
  if (!ArgType->isRValueReferenceType()) {
    Diag(MD->getLocation(), diag::err_defaulted_move_assign_not_ref);
    HadError = true;
  } else {
    if (ArgType->getPointeeType().isVolatileQualified()) {
      Diag(MD->getLocation(), diag::err_defaulted_move_assign_volatile_param);
      HadError = true;
    }
    if (ArgType->getPointeeType().isConstQualified()) {
      Diag(MD->getLocation(), diag::err_defaulted_move_assign_const_param);
      HadError = true;
    }
  }

  if (OperType->getTypeQuals()) {
    Diag(MD->getLocation(), diag::err_defaulted_move_assign_quals);
    HadError = true;
  }

  if (OperType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXMoveAssignment,
          PDiag(),
          ExceptionType, SourceLocation(),
          OperType, MD->getLocation())) {
      HadError = true;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // We duplicate the one parameter type.
    EPI.RefQualifier = OperType->getRefQualifier();
    EPI.ExtInfo = OperType->getExtInfo();
    MD->setType(Context.getFunctionType(ReturnType, &ArgType, 1, EPI));
  }

  if (HadError) {
    MD->setInvalidDecl();
    return;
  }

  if (ShouldDeleteMoveAssignmentOperator(MD)) {
    if (First) {
      MD->setDeletedAsWritten();
    } else {
      Diag(MD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXMoveAssignment;
      MD->setInvalidDecl();
    }
  }
}

void Sema::CheckExplicitlyDefaultedDestructor(CXXDestructorDecl *DD) {
  assert(DD->isExplicitlyDefaulted());

  // Whether this was the first-declared instance of the destructor.
  bool First = DD == DD->getCanonicalDecl();

  ImplicitExceptionSpecification Spec
    = ComputeDefaultedDtorExceptionSpec(DD->getParent());
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  const FunctionProtoType *DtorType = DD->getType()->getAs<FunctionProtoType>(),
                          *ExceptionType = Context.getFunctionType(
                         Context.VoidTy, 0, 0, EPI)->getAs<FunctionProtoType>();

  if (DtorType->hasExceptionSpec()) {
    if (CheckEquivalentExceptionSpec(
          PDiag(diag::err_incorrect_defaulted_exception_spec)
            << CXXDestructor,
          PDiag(),
          ExceptionType, SourceLocation(),
          DtorType, DD->getLocation())) {
      DD->setInvalidDecl();
      return;
    }
  } else if (First) {
    // We set the declaration to have the computed exception spec here.
    // There are no parameters.
    EPI.ExtInfo = DtorType->getExtInfo();
    DD->setType(Context.getFunctionType(Context.VoidTy, 0, 0, EPI));
  }

  if (ShouldDeleteDestructor(DD)) {
    if (First) {
      DD->setDeletedAsWritten();
    } else {
      Diag(DD->getLocation(), diag::err_out_of_line_default_deletes)
        << CXXDestructor;
      DD->setInvalidDecl();
    }
  }
}

/// This function implements the following C++0x paragraphs:
///  - [class.ctor]/5
///  - [class.copy]/11
bool Sema::ShouldDeleteSpecialMember(CXXMethodDecl *MD, CXXSpecialMember CSM) {
  assert(!MD->isInvalidDecl());
  CXXRecordDecl *RD = MD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus0x || RD->isInvalidDecl())
    return false;

  bool IsUnion = RD->isUnion();
  bool IsConstructor = false;
  bool IsAssignment = false;
  bool IsMove = false;
  
  bool ConstArg = false;

  switch (CSM) {
  case CXXDefaultConstructor:
    IsConstructor = true;
    break;
  case CXXCopyConstructor:
    IsConstructor = true;
    ConstArg = MD->getParamDecl(0)->getType().isConstQualified();
    break;
  case CXXMoveConstructor:
    IsConstructor = true;
    IsMove = true;
    break;
  default:
    llvm_unreachable("function only currently implemented for default ctors");
  }

  SourceLocation Loc = MD->getLocation();

  // Do access control from the special member function
  ContextRAII MethodContext(*this, MD);

  bool AllConst = true;

  // We do this because we should never actually use an anonymous
  // union's constructor.
  if (IsUnion && RD->isAnonymousStructOrUnion())
    return false;

  // FIXME: We should put some diagnostic logic right into this function.

  for (CXXRecordDecl::base_class_iterator BI = RD->bases_begin(),
                                          BE = RD->bases_end();
       BI != BE; ++BI) {
    // We'll handle this one later
    if (BI->isVirtual())
      continue;

    CXXRecordDecl *BaseDecl = BI->getType()->getAsCXXRecordDecl();
    assert(BaseDecl && "base isn't a CXXRecordDecl");

    // Unless we have an assignment operator, the base's destructor must
    // be accessible and not deleted.
    if (!IsAssignment) {
      CXXDestructorDecl *BaseDtor = LookupDestructor(BaseDecl);
      if (BaseDtor->isDeleted())
        return true;
      if (CheckDestructorAccess(Loc, BaseDtor, PDiag()) !=
          AR_accessible)
        return true;
    }

    // Finding the corresponding member in the base should lead to a
    // unique, accessible, non-deleted function. If we are doing
    // a destructor, we have already checked this case.
    if (CSM != CXXDestructor) {
      SpecialMemberOverloadResult *SMOR =
        LookupSpecialMember(BaseDecl, CSM, ConstArg, false, false, false,
                            false);
      if (!SMOR->hasSuccess())
        return true;
      CXXMethodDecl *BaseMember = SMOR->getMethod();
      if (IsConstructor) {
        CXXConstructorDecl *BaseCtor = cast<CXXConstructorDecl>(BaseMember);
        if (CheckConstructorAccess(Loc, BaseCtor, BaseCtor->getAccess(),
                                   PDiag()) != AR_accessible)
          return true;

        // For a move operation, the corresponding operation must actually
        // be a move operation (and not a copy selected by overload
        // resolution) unless we are working on a trivially copyable class.
        if (IsMove && !BaseCtor->isMoveConstructor() &&
            !BaseDecl->isTriviallyCopyable())
          return true;
      }
    }
  }

  for (CXXRecordDecl::base_class_iterator BI = RD->vbases_begin(),
                                          BE = RD->vbases_end();
       BI != BE; ++BI) {
    CXXRecordDecl *BaseDecl = BI->getType()->getAsCXXRecordDecl();
    assert(BaseDecl && "base isn't a CXXRecordDecl");

    // Unless we have an assignment operator, the base's destructor must
    // be accessible and not deleted.
    if (!IsAssignment) {
      CXXDestructorDecl *BaseDtor = LookupDestructor(BaseDecl);
      if (BaseDtor->isDeleted())
        return true;
      if (CheckDestructorAccess(Loc, BaseDtor, PDiag()) !=
          AR_accessible)
        return true;
    }

    // Finding the corresponding member in the base should lead to a
    // unique, accessible, non-deleted function.
    if (CSM != CXXDestructor) {
      SpecialMemberOverloadResult *SMOR =
        LookupSpecialMember(BaseDecl, CSM, ConstArg, false, false, false,
                            false);
      if (!SMOR->hasSuccess())
        return true;
      CXXMethodDecl *BaseMember = SMOR->getMethod();
      if (IsConstructor) {
        CXXConstructorDecl *BaseCtor = cast<CXXConstructorDecl>(BaseMember);
        if (CheckConstructorAccess(Loc, BaseCtor, BaseCtor->getAccess(),
                                   PDiag()) != AR_accessible)
          return true;

        // For a move operation, the corresponding operation must actually
        // be a move operation (and not a copy selected by overload
        // resolution) unless we are working on a trivially copyable class.
        if (IsMove && !BaseCtor->isMoveConstructor() &&
            !BaseDecl->isTriviallyCopyable())
          return true;
      }
    }
  }

  for (CXXRecordDecl::field_iterator FI = RD->field_begin(),
                                     FE = RD->field_end();
       FI != FE; ++FI) {
    if (FI->isInvalidDecl() || FI->isUnnamedBitfield())
      continue;
    
    QualType FieldType = Context.getBaseElementType(FI->getType());
    CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();

    // For a default constructor, all references must be initialized in-class
    // and, if a union, it must have a non-const member.
    if (CSM == CXXDefaultConstructor) {
      if (FieldType->isReferenceType() && !FI->hasInClassInitializer())
        return true;

      if (IsUnion && !FieldType.isConstQualified())
        AllConst = false;
    // For a copy constructor, data members must not be of rvalue reference
    // type.
    } else if (CSM == CXXCopyConstructor) {
      if (FieldType->isRValueReferenceType())
        return true;
    }

    if (FieldRecord) {
      // For a default constructor, a const member must have a user-provided
      // default constructor or else be explicitly initialized.
      if (CSM == CXXDefaultConstructor && FieldType.isConstQualified() &&
          !FI->hasInClassInitializer() &&
          !FieldRecord->hasUserProvidedDefaultConstructor())
        return true;
 
      // Some additional restrictions exist on the variant members.
      if (!IsUnion && FieldRecord->isUnion() &&
          FieldRecord->isAnonymousStructOrUnion()) {
        // We're okay to reuse AllConst here since we only care about the
        // value otherwise if we're in a union.
        AllConst = true;

        for (CXXRecordDecl::field_iterator UI = FieldRecord->field_begin(),
                                           UE = FieldRecord->field_end();
             UI != UE; ++UI) {
          QualType UnionFieldType = Context.getBaseElementType(UI->getType());
          CXXRecordDecl *UnionFieldRecord =
            UnionFieldType->getAsCXXRecordDecl();

          if (!UnionFieldType.isConstQualified())
            AllConst = false;

          if (UnionFieldRecord) {
            // FIXME: Checking for accessibility and validity of this
            //        destructor is technically going beyond the
            //        standard, but this is believed to be a defect.
            if (!IsAssignment) {
              CXXDestructorDecl *FieldDtor = LookupDestructor(UnionFieldRecord);
              if (FieldDtor->isDeleted())
                return true;
              if (CheckDestructorAccess(Loc, FieldDtor, PDiag()) !=
                  AR_accessible)
                return true;
              if (!FieldDtor->isTrivial())
                return true;
            }

            if (CSM != CXXDestructor) {
              SpecialMemberOverloadResult *SMOR =
                LookupSpecialMember(UnionFieldRecord, CSM, ConstArg, false,
                                    false, false, false);
              // FIXME: Checking for accessibility and validity of this
              //        corresponding member is technically going beyond the
              //        standard, but this is believed to be a defect.
              if (!SMOR->hasSuccess())
                return true;

              CXXMethodDecl *FieldMember = SMOR->getMethod();
              // A member of a union must have a trivial corresponding
              // constructor.
              if (!FieldMember->isTrivial())
                return true;

              if (IsConstructor) {
                CXXConstructorDecl *FieldCtor = cast<CXXConstructorDecl>(FieldMember);
                if (CheckConstructorAccess(Loc, FieldCtor, FieldCtor->getAccess(),
                                           PDiag()) != AR_accessible)
                return true;
              }
            }
          }
        }

        // At least one member in each anonymous union must be non-const
        if (CSM == CXXDefaultConstructor && AllConst)
          return true;

        // Don't try to initialize the anonymous union
        // This is technically non-conformant, but sanity demands it.
        continue;
      }

      // Unless we're doing assignment, the field's destructor must be
      // accessible and not deleted.
      if (!IsAssignment) {
        CXXDestructorDecl *FieldDtor = LookupDestructor(FieldRecord);
        if (FieldDtor->isDeleted())
          return true;
        if (CheckDestructorAccess(Loc, FieldDtor, PDiag()) !=
            AR_accessible)
          return true;
      }

      // Check that the corresponding member of the field is accessible,
      // unique, and non-deleted. We don't do this if it has an explicit
      // initialization when default-constructing.
      if (CSM != CXXDestructor &&
          (CSM != CXXDefaultConstructor || !FI->hasInClassInitializer())) {
        SpecialMemberOverloadResult *SMOR =
          LookupSpecialMember(FieldRecord, CSM, ConstArg, false, false, false,
                              false);
        if (!SMOR->hasSuccess())
          return true;

        CXXMethodDecl *FieldMember = SMOR->getMethod();
        if (IsConstructor) {
          CXXConstructorDecl *FieldCtor = cast<CXXConstructorDecl>(FieldMember);
          if (CheckConstructorAccess(Loc, FieldCtor, FieldCtor->getAccess(),
                                     PDiag()) != AR_accessible)
          return true;

          // For a move operation, the corresponding operation must actually
          // be a move operation (and not a copy selected by overload
          // resolution) unless we are working on a trivially copyable class.
          if (IsMove && !FieldCtor->isMoveConstructor() &&
              !FieldRecord->isTriviallyCopyable())
            return true;
        }

        // We need the corresponding member of a union to be trivial so that
        // we can safely copy them all simultaneously.
        // FIXME: Note that performing the check here (where we rely on the lack
        // of an in-class initializer) is technically ill-formed. However, this
        // seems most obviously to be a bug in the standard.
        if (IsUnion && !FieldMember->isTrivial())
          return true;
      }
    } else if (CSM == CXXDefaultConstructor && !IsUnion &&
               FieldType.isConstQualified() && !FI->hasInClassInitializer()) {
      // We can't initialize a const member of non-class type to any value.
      return true;
    }
  }

  // We can't have all const members in a union when default-constructing,
  // or else they're all nonsensical garbage values that can't be changed.
  if (CSM == CXXDefaultConstructor && IsUnion && AllConst)
    return true;

  return false;
}

bool Sema::ShouldDeleteCopyAssignmentOperator(CXXMethodDecl *MD) {
  CXXRecordDecl *RD = MD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus0x || RD->isInvalidDecl())
    return false;

  SourceLocation Loc = MD->getLocation();

  // Do access control from the constructor
  ContextRAII MethodContext(*this, MD);

  bool Union = RD->isUnion();

  unsigned ArgQuals =
    MD->getParamDecl(0)->getType()->getPointeeType().isConstQualified() ?
      Qualifiers::Const : 0;

  // We do this because we should never actually use an anonymous
  // union's constructor.
  if (Union && RD->isAnonymousStructOrUnion())
    return false;

  // FIXME: We should put some diagnostic logic right into this function.

  // C++0x [class.copy]/20
  //    A defaulted [copy] assignment operator for class X is defined as deleted
  //    if X has:

  for (CXXRecordDecl::base_class_iterator BI = RD->bases_begin(),
                                          BE = RD->bases_end();
       BI != BE; ++BI) {
    // We'll handle this one later
    if (BI->isVirtual())
      continue;

    QualType BaseType = BI->getType();
    CXXRecordDecl *BaseDecl = BaseType->getAsCXXRecordDecl();
    assert(BaseDecl && "base isn't a CXXRecordDecl");

    // -- a [direct base class] B that cannot be [copied] because overload
    //    resolution, as applied to B's [copy] assignment operator, results in
    //    an ambiguity or a function that is deleted or inaccessible from the
    //    assignment operator
    CXXMethodDecl *CopyOper = LookupCopyingAssignment(BaseDecl, ArgQuals, false,
                                                      0);
    if (!CopyOper || CopyOper->isDeleted())
      return true;
    if (CheckDirectMemberAccess(Loc, CopyOper, PDiag()) != AR_accessible)
      return true;
  }

  for (CXXRecordDecl::base_class_iterator BI = RD->vbases_begin(),
                                          BE = RD->vbases_end();
       BI != BE; ++BI) {
    QualType BaseType = BI->getType();
    CXXRecordDecl *BaseDecl = BaseType->getAsCXXRecordDecl();
    assert(BaseDecl && "base isn't a CXXRecordDecl");

    // -- a [virtual base class] B that cannot be [copied] because overload
    //    resolution, as applied to B's [copy] assignment operator, results in
    //    an ambiguity or a function that is deleted or inaccessible from the
    //    assignment operator
    CXXMethodDecl *CopyOper = LookupCopyingAssignment(BaseDecl, ArgQuals, false,
                                                      0);
    if (!CopyOper || CopyOper->isDeleted())
      return true;
    if (CheckDirectMemberAccess(Loc, CopyOper, PDiag()) != AR_accessible)
      return true;
  }

  for (CXXRecordDecl::field_iterator FI = RD->field_begin(),
                                     FE = RD->field_end();
       FI != FE; ++FI) {
    if (FI->isUnnamedBitfield())
      continue;
    
    QualType FieldType = Context.getBaseElementType(FI->getType());
    
    // -- a non-static data member of reference type
    if (FieldType->isReferenceType())
      return true;

    // -- a non-static data member of const non-class type (or array thereof)
    if (FieldType.isConstQualified() && !FieldType->isRecordType())
      return true;
 
    CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();

    if (FieldRecord) {
      // This is an anonymous union
      if (FieldRecord->isUnion() && FieldRecord->isAnonymousStructOrUnion()) {
        // Anonymous unions inside unions do not variant members create
        if (!Union) {
          for (CXXRecordDecl::field_iterator UI = FieldRecord->field_begin(),
                                             UE = FieldRecord->field_end();
               UI != UE; ++UI) {
            QualType UnionFieldType = Context.getBaseElementType(UI->getType());
            CXXRecordDecl *UnionFieldRecord =
              UnionFieldType->getAsCXXRecordDecl();

            // -- a variant member with a non-trivial [copy] assignment operator
            //    and X is a union-like class
            if (UnionFieldRecord &&
                !UnionFieldRecord->hasTrivialCopyAssignment())
              return true;
          }
        }

        // Don't try to initalize an anonymous union
        continue;
      // -- a variant member with a non-trivial [copy] assignment operator
      //    and X is a union-like class
      } else if (Union && !FieldRecord->hasTrivialCopyAssignment()) {
          return true;
      }

      CXXMethodDecl *CopyOper = LookupCopyingAssignment(FieldRecord, ArgQuals,
                                                        false, 0);
      if (!CopyOper || CopyOper->isDeleted())
        return true;
      if (CheckDirectMemberAccess(Loc, CopyOper, PDiag()) != AR_accessible)
        return true;
    }
  }

  return false;
}

bool Sema::ShouldDeleteMoveAssignmentOperator(CXXMethodDecl *MD) {
  CXXRecordDecl *RD = MD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus0x || RD->isInvalidDecl())
    return false;

  SourceLocation Loc = MD->getLocation();

  // Do access control from the constructor
  ContextRAII MethodContext(*this, MD);

  bool Union = RD->isUnion();

  // We do this because we should never actually use an anonymous
  // union's constructor.
  if (Union && RD->isAnonymousStructOrUnion())
    return false;

  // C++0x [class.copy]/20
  //    A defaulted [move] assignment operator for class X is defined as deleted
  //    if X has:

  //    -- for the move constructor, [...] any direct or indirect virtual base
  //       class.
  if (RD->getNumVBases() != 0)
    return true;

  for (CXXRecordDecl::base_class_iterator BI = RD->bases_begin(),
                                          BE = RD->bases_end();
       BI != BE; ++BI) {

    QualType BaseType = BI->getType();
    CXXRecordDecl *BaseDecl = BaseType->getAsCXXRecordDecl();
    assert(BaseDecl && "base isn't a CXXRecordDecl");

    // -- a [direct base class] B that cannot be [moved] because overload
    //    resolution, as applied to B's [move] assignment operator, results in
    //    an ambiguity or a function that is deleted or inaccessible from the
    //    assignment operator
    CXXMethodDecl *MoveOper = LookupMovingAssignment(BaseDecl, false, 0);
    if (!MoveOper || MoveOper->isDeleted())
      return true;
    if (CheckDirectMemberAccess(Loc, MoveOper, PDiag()) != AR_accessible)
      return true;

    // -- for the move assignment operator, a [direct base class] with a type
    //    that does not have a move assignment operator and is not trivially
    //    copyable.
    if (!MoveOper->isMoveAssignmentOperator() &&
        !BaseDecl->isTriviallyCopyable())
      return true;
  }

  for (CXXRecordDecl::field_iterator FI = RD->field_begin(),
                                     FE = RD->field_end();
       FI != FE; ++FI) {
    if (FI->isUnnamedBitfield())
      continue;
        
    QualType FieldType = Context.getBaseElementType(FI->getType());
    
    // -- a non-static data member of reference type
    if (FieldType->isReferenceType())
      return true;

    // -- a non-static data member of const non-class type (or array thereof)
    if (FieldType.isConstQualified() && !FieldType->isRecordType())
      return true;

    CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();

    if (FieldRecord) {
      // This is an anonymous union
      if (FieldRecord->isUnion() && FieldRecord->isAnonymousStructOrUnion()) {
        // Anonymous unions inside unions do not variant members create
        if (!Union) {
          for (CXXRecordDecl::field_iterator UI = FieldRecord->field_begin(),
                                             UE = FieldRecord->field_end();
               UI != UE; ++UI) {
            QualType UnionFieldType = Context.getBaseElementType(UI->getType());
            CXXRecordDecl *UnionFieldRecord =
              UnionFieldType->getAsCXXRecordDecl();

            // -- a variant member with a non-trivial [move] assignment operator
            //    and X is a union-like class
            if (UnionFieldRecord &&
                !UnionFieldRecord->hasTrivialMoveAssignment())
              return true;
          }
        }

        // Don't try to initalize an anonymous union
        continue;
      // -- a variant member with a non-trivial [move] assignment operator
      //    and X is a union-like class
      } else if (Union && !FieldRecord->hasTrivialMoveAssignment()) {
          return true;
      }

      CXXMethodDecl *MoveOper = LookupMovingAssignment(FieldRecord, false, 0);
      if (!MoveOper || MoveOper->isDeleted())
        return true;
      if (CheckDirectMemberAccess(Loc, MoveOper, PDiag()) != AR_accessible)
        return true;

      // -- for the move assignment operator, a [non-static data member] with a
      //    type that does not have a move assignment operator and is not
      //    trivially copyable.
      if (!MoveOper->isMoveAssignmentOperator() &&
          !FieldRecord->isTriviallyCopyable())
        return true;
    }
  }

  return false;
}

bool Sema::ShouldDeleteDestructor(CXXDestructorDecl *DD) {
  CXXRecordDecl *RD = DD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus0x || RD->isInvalidDecl())
    return false;

  SourceLocation Loc = DD->getLocation();

  // Do access control from the destructor
  ContextRAII CtorContext(*this, DD);

  bool Union = RD->isUnion();

  // We do this because we should never actually use an anonymous
  // union's destructor.
  if (Union && RD->isAnonymousStructOrUnion())
    return false;

  // C++0x [class.dtor]p5
  //    A defaulted destructor for a class X is defined as deleted if:
  for (CXXRecordDecl::base_class_iterator BI = RD->bases_begin(),
                                          BE = RD->bases_end();
       BI != BE; ++BI) {
    // We'll handle this one later
    if (BI->isVirtual())
      continue;

    CXXRecordDecl *BaseDecl = BI->getType()->getAsCXXRecordDecl();
    CXXDestructorDecl *BaseDtor = LookupDestructor(BaseDecl);
    assert(BaseDtor && "base has no destructor");

    // -- any direct or virtual base class has a deleted destructor or
    //    a destructor that is inaccessible from the defaulted destructor
    if (BaseDtor->isDeleted())
      return true;
    if (CheckDestructorAccess(Loc, BaseDtor, PDiag()) !=
        AR_accessible)
      return true;
  }

  for (CXXRecordDecl::base_class_iterator BI = RD->vbases_begin(),
                                          BE = RD->vbases_end();
       BI != BE; ++BI) {
    CXXRecordDecl *BaseDecl = BI->getType()->getAsCXXRecordDecl();
    CXXDestructorDecl *BaseDtor = LookupDestructor(BaseDecl);
    assert(BaseDtor && "base has no destructor");

    // -- any direct or virtual base class has a deleted destructor or
    //    a destructor that is inaccessible from the defaulted destructor
    if (BaseDtor->isDeleted())
      return true;
    if (CheckDestructorAccess(Loc, BaseDtor, PDiag()) !=
        AR_accessible)
      return true;
  }

  for (CXXRecordDecl::field_iterator FI = RD->field_begin(),
                                     FE = RD->field_end();
       FI != FE; ++FI) {
    QualType FieldType = Context.getBaseElementType(FI->getType());
    CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();
    if (FieldRecord) {
      if (FieldRecord->isUnion() && FieldRecord->isAnonymousStructOrUnion()) {
         for (CXXRecordDecl::field_iterator UI = FieldRecord->field_begin(),
                                            UE = FieldRecord->field_end();
              UI != UE; ++UI) {
           QualType UnionFieldType = Context.getBaseElementType(FI->getType());
           CXXRecordDecl *UnionFieldRecord =
             UnionFieldType->getAsCXXRecordDecl();

           // -- X is a union-like class that has a variant member with a non-
           //    trivial destructor.
           if (UnionFieldRecord && !UnionFieldRecord->hasTrivialDestructor())
             return true;
         }
      // Technically we are supposed to do this next check unconditionally.
      // But that makes absolutely no sense.
      } else {
        CXXDestructorDecl *FieldDtor = LookupDestructor(FieldRecord);

        // -- any of the non-static data members has class type M (or array
        //    thereof) and M has a deleted destructor or a destructor that is
        //    inaccessible from the defaulted destructor
        if (FieldDtor->isDeleted())
          return true;
        if (CheckDestructorAccess(Loc, FieldDtor, PDiag()) !=
          AR_accessible)
        return true;
        
        // -- X is a union-like class that has a variant member with a non-
        //    trivial destructor.
        if (Union && !FieldDtor->isTrivial())
          return true;
      }
    }
  }

  if (DD->isVirtual()) {
    FunctionDecl *OperatorDelete = 0;
    DeclarationName Name =
      Context.DeclarationNames.getCXXOperatorName(OO_Delete);
    if (FindDeallocationFunction(Loc, RD, Name, OperatorDelete,
          false))
      return true;
  }


  return false;
}

/// \brief Data used with FindHiddenVirtualMethod
namespace {
  struct FindHiddenVirtualMethodData {
    Sema *S;
    CXXMethodDecl *Method;
    llvm::SmallPtrSet<const CXXMethodDecl *, 8> OverridenAndUsingBaseMethods;
    SmallVector<CXXMethodDecl *, 8> OverloadedMethods;
  };
}

/// \brief Member lookup function that determines whether a given C++
/// method overloads virtual methods in a base class without overriding any,
/// to be used with CXXRecordDecl::lookupInBases().
static bool FindHiddenVirtualMethod(const CXXBaseSpecifier *Specifier,
                                    CXXBasePath &Path,
                                    void *UserData) {
  RecordDecl *BaseRecord = Specifier->getType()->getAs<RecordType>()->getDecl();

  FindHiddenVirtualMethodData &Data
    = *static_cast<FindHiddenVirtualMethodData*>(UserData);

  DeclarationName Name = Data.Method->getDeclName();
  assert(Name.getNameKind() == DeclarationName::Identifier);

  bool foundSameNameMethod = false;
  SmallVector<CXXMethodDecl *, 8> overloadedMethods;
  for (Path.Decls = BaseRecord->lookup(Name);
       Path.Decls.first != Path.Decls.second;
       ++Path.Decls.first) {
    NamedDecl *D = *Path.Decls.first;
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D)) {
      MD = MD->getCanonicalDecl();
      foundSameNameMethod = true;
      // Interested only in hidden virtual methods.
      if (!MD->isVirtual())
        continue;
      // If the method we are checking overrides a method from its base
      // don't warn about the other overloaded methods.
      if (!Data.S->IsOverload(Data.Method, MD, false))
        return true;
      // Collect the overload only if its hidden.
      if (!Data.OverridenAndUsingBaseMethods.count(MD))
        overloadedMethods.push_back(MD);
    }
  }

  if (foundSameNameMethod)
    Data.OverloadedMethods.append(overloadedMethods.begin(),
                                   overloadedMethods.end());
  return foundSameNameMethod;
}

/// \brief See if a method overloads virtual methods in a base class without
/// overriding any.
void Sema::DiagnoseHiddenVirtualMethods(CXXRecordDecl *DC, CXXMethodDecl *MD) {
  if (Diags.getDiagnosticLevel(diag::warn_overloaded_virtual,
                               MD->getLocation()) == DiagnosticsEngine::Ignored)
    return;
  if (MD->getDeclName().getNameKind() != DeclarationName::Identifier)
    return;

  CXXBasePaths Paths(/*FindAmbiguities=*/true, // true to look in all bases.
                     /*bool RecordPaths=*/false,
                     /*bool DetectVirtual=*/false);
  FindHiddenVirtualMethodData Data;
  Data.Method = MD;
  Data.S = this;

  // Keep the base methods that were overriden or introduced in the subclass
  // by 'using' in a set. A base method not in this set is hidden.
  for (DeclContext::lookup_result res = DC->lookup(MD->getDeclName());
       res.first != res.second; ++res.first) {
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(*res.first))
      for (CXXMethodDecl::method_iterator I = MD->begin_overridden_methods(),
                                          E = MD->end_overridden_methods();
           I != E; ++I)
        Data.OverridenAndUsingBaseMethods.insert((*I)->getCanonicalDecl());
    if (UsingShadowDecl *shad = dyn_cast<UsingShadowDecl>(*res.first))
      if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(shad->getTargetDecl()))
        Data.OverridenAndUsingBaseMethods.insert(MD->getCanonicalDecl());
  }

  if (DC->lookupInBases(&FindHiddenVirtualMethod, &Data, Paths) &&
      !Data.OverloadedMethods.empty()) {
    Diag(MD->getLocation(), diag::warn_overloaded_virtual)
      << MD << (Data.OverloadedMethods.size() > 1);

    for (unsigned i = 0, e = Data.OverloadedMethods.size(); i != e; ++i) {
      CXXMethodDecl *overloadedMD = Data.OverloadedMethods[i];
      Diag(overloadedMD->getLocation(),
           diag::note_hidden_overloaded_virtual_declared_here) << overloadedMD;
    }
  }
}

void Sema::ActOnFinishCXXMemberSpecification(Scope* S, SourceLocation RLoc,
                                             Decl *TagDecl,
                                             SourceLocation LBrac,
                                             SourceLocation RBrac,
                                             AttributeList *AttrList) {
  if (!TagDecl)
    return;

  AdjustDeclIfTemplate(TagDecl);

  ActOnFields(S, RLoc, TagDecl, llvm::makeArrayRef(
              // strict aliasing violation!
              reinterpret_cast<Decl**>(FieldCollector->getCurFields()),
              FieldCollector->getCurNumFields()), LBrac, RBrac, AttrList);

  CheckCompletedCXXClass(
                        dyn_cast_or_null<CXXRecordDecl>(TagDecl));
}

/// AddImplicitlyDeclaredMembersToClass - Adds any implicitly-declared
/// special functions, such as the default constructor, copy
/// constructor, or destructor, to the given C++ class (C++
/// [special]p1).  This routine can only be executed just before the
/// definition of the class is complete.
void Sema::AddImplicitlyDeclaredMembersToClass(CXXRecordDecl *ClassDecl) {
  if (!ClassDecl->hasUserDeclaredConstructor())
    ++ASTContext::NumImplicitDefaultConstructors;

  if (!ClassDecl->hasUserDeclaredCopyConstructor())
    ++ASTContext::NumImplicitCopyConstructors;

  if (!ClassDecl->hasUserDeclaredCopyAssignment()) {
    ++ASTContext::NumImplicitCopyAssignmentOperators;
    
    // If we have a dynamic class, then the copy assignment operator may be 
    // virtual, so we have to declare it immediately. This ensures that, e.g.,
    // it shows up in the right place in the vtable and that we diagnose 
    // problems with the implicit exception specification.    
    if (ClassDecl->isDynamicClass())
      DeclareImplicitCopyAssignment(ClassDecl);
  }

  if (!ClassDecl->hasUserDeclaredDestructor()) {
    ++ASTContext::NumImplicitDestructors;
    
    // If we have a dynamic class, then the destructor may be virtual, so we 
    // have to declare the destructor immediately. This ensures that, e.g., it
    // shows up in the right place in the vtable and that we diagnose problems
    // with the implicit exception specification.
    if (ClassDecl->isDynamicClass())
      DeclareImplicitDestructor(ClassDecl);
  }
}

void Sema::ActOnReenterDeclaratorTemplateScope(Scope *S, DeclaratorDecl *D) {
  if (!D)
    return;

  int NumParamList = D->getNumTemplateParameterLists();
  for (int i = 0; i < NumParamList; i++) {
    TemplateParameterList* Params = D->getTemplateParameterList(i);
    for (TemplateParameterList::iterator Param = Params->begin(),
                                      ParamEnd = Params->end();
          Param != ParamEnd; ++Param) {
      NamedDecl *Named = cast<NamedDecl>(*Param);
      if (Named->getDeclName()) {
        S->AddDecl(Named);
        IdResolver.AddDecl(Named);
      }
    }
  }
}

void Sema::ActOnReenterTemplateScope(Scope *S, Decl *D) {
  if (!D)
    return;
  
  TemplateParameterList *Params = 0;
  if (TemplateDecl *Template = dyn_cast<TemplateDecl>(D))
    Params = Template->getTemplateParameters();
  else if (ClassTemplatePartialSpecializationDecl *PartialSpec
           = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
    Params = PartialSpec->getTemplateParameters();
  else
    return;

  for (TemplateParameterList::iterator Param = Params->begin(),
                                    ParamEnd = Params->end();
       Param != ParamEnd; ++Param) {
    NamedDecl *Named = cast<NamedDecl>(*Param);
    if (Named->getDeclName()) {
      S->AddDecl(Named);
      IdResolver.AddDecl(Named);
    }
  }
}

void Sema::ActOnStartDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  AdjustDeclIfTemplate(RecordD);
  CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordD);
  PushDeclContext(S, Record);
}

void Sema::ActOnFinishDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  PopDeclContext();
}

/// ActOnStartDelayedCXXMethodDeclaration - We have completed
/// parsing a top-level (non-nested) C++ class, and we are now
/// parsing those parts of the given Method declaration that could
/// not be parsed earlier (C++ [class.mem]p2), such as default
/// arguments. This action should enter the scope of the given
/// Method declaration as if we had just parsed the qualified method
/// name. However, it should not bring the parameters into scope;
/// that will be performed by ActOnDelayedCXXMethodParameter.
void Sema::ActOnStartDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
}

/// ActOnDelayedCXXMethodParameter - We've already started a delayed
/// C++ method declaration. We're (re-)introducing the given
/// function parameter into scope for use in parsing later parts of
/// the method declaration. For example, we could see an
/// ActOnParamDefaultArgument event for this parameter.
void Sema::ActOnDelayedCXXMethodParameter(Scope *S, Decl *ParamD) {
  if (!ParamD)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(ParamD);

  // If this parameter has an unparsed default argument, clear it out
  // to make way for the parsed default argument.
  if (Param->hasUnparsedDefaultArg())
    Param->setDefaultArg(0);

  S->AddDecl(Param);
  if (Param->getDeclName())
    IdResolver.AddDecl(Param);
}

/// ActOnFinishDelayedCXXMethodDeclaration - We have finished
/// processing the delayed method declaration for Method. The method
/// declaration is now considered finished. There may be a separate
/// ActOnStartOfFunctionDef action later (not necessarily
/// immediately!) for this method, if it was also defined inside the
/// class body.
void Sema::ActOnFinishDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
  if (!MethodD)
    return;

  AdjustDeclIfTemplate(MethodD);

  FunctionDecl *Method = cast<FunctionDecl>(MethodD);

  // Now that we have our default arguments, check the constructor
  // again. It could produce additional diagnostics or affect whether
  // the class has implicitly-declared destructors, among other
  // things.
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Method))
    CheckConstructor(Constructor);

  // Check the default arguments, which we may have added.
  if (!Method->isInvalidDecl())
    CheckCXXDefaultArguments(Method);
}

/// CheckConstructorDeclarator - Called by ActOnDeclarator to check
/// the well-formedness of the constructor declarator @p D with type @p
/// R. If there are any errors in the declarator, this routine will
/// emit diagnostics and set the invalid bit to true.  In any case, the type
/// will be updated to reflect a well-formed type for the constructor and
/// returned.
QualType Sema::CheckConstructorDeclarator(Declarator &D, QualType R,
                                          StorageClass &SC) {
  bool isVirtual = D.getDeclSpec().isVirtualSpecified();

  // C++ [class.ctor]p3:
  //   A constructor shall not be virtual (10.3) or static (9.4). A
  //   constructor can be invoked for a const, volatile or const
  //   volatile object. A constructor shall not be declared const,
  //   volatile, or const volatile (9.3.2).
  if (isVirtual) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "virtual" << SourceRange(D.getDeclSpec().getVirtualSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
    SC = SC_None;
  }

  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.TypeQuals != 0) {
    if (FTI.TypeQuals & Qualifiers::Const)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_constructor)
        << "const" << SourceRange(D.getIdentifierLoc());
    if (FTI.TypeQuals & Qualifiers::Volatile)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_constructor)
        << "volatile" << SourceRange(D.getIdentifierLoc());
    if (FTI.TypeQuals & Qualifiers::Restrict)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_constructor)
        << "restrict" << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }

  // C++0x [class.ctor]p4:
  //   A constructor shall not be declared with a ref-qualifier.
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_constructor)
      << FTI.RefQualifierIsLValueRef 
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }
  
  // Rebuild the function type "R" without any type qualifiers (in
  // case any of the errors above fired) and with "void" as the
  // return type, since constructors don't have return types.
  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();
  if (Proto->getResultType() == Context.VoidTy && !D.isInvalidType())
    return R;

  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.TypeQuals = 0;
  EPI.RefQualifier = RQ_None;
  
  return Context.getFunctionType(Context.VoidTy, Proto->arg_type_begin(),
                                 Proto->getNumArgs(), EPI);
}

/// CheckConstructor - Checks a fully-formed constructor for
/// well-formedness, issuing any diagnostics required. Returns true if
/// the constructor declarator is invalid.
void Sema::CheckConstructor(CXXConstructorDecl *Constructor) {
  CXXRecordDecl *ClassDecl
    = dyn_cast<CXXRecordDecl>(Constructor->getDeclContext());
  if (!ClassDecl)
    return Constructor->setInvalidDecl();

  // C++ [class.copy]p3:
  //   A declaration of a constructor for a class X is ill-formed if
  //   its first parameter is of type (optionally cv-qualified) X and
  //   either there are no other parameters or else all other
  //   parameters have default arguments.
  if (!Constructor->isInvalidDecl() &&
      ((Constructor->getNumParams() == 1) ||
       (Constructor->getNumParams() > 1 &&
        Constructor->getParamDecl(1)->hasDefaultArg())) &&
      Constructor->getTemplateSpecializationKind()
                                              != TSK_ImplicitInstantiation) {
    QualType ParamType = Constructor->getParamDecl(0)->getType();
    QualType ClassTy = Context.getTagDeclType(ClassDecl);
    if (Context.getCanonicalType(ParamType).getUnqualifiedType() == ClassTy) {
      SourceLocation ParamLoc = Constructor->getParamDecl(0)->getLocation();
      const char *ConstRef 
        = Constructor->getParamDecl(0)->getIdentifier() ? "const &" 
                                                        : " const &";
      Diag(ParamLoc, diag::err_constructor_byvalue_arg)
        << FixItHint::CreateInsertion(ParamLoc, ConstRef);

      // FIXME: Rather that making the constructor invalid, we should endeavor
      // to fix the type.
      Constructor->setInvalidDecl();
    }
  }
}

/// CheckDestructor - Checks a fully-formed destructor definition for
/// well-formedness, issuing any diagnostics required.  Returns true
/// on error.
bool Sema::CheckDestructor(CXXDestructorDecl *Destructor) {
  CXXRecordDecl *RD = Destructor->getParent();
  
  if (Destructor->isVirtual()) {
    SourceLocation Loc;
    
    if (!Destructor->isImplicit())
      Loc = Destructor->getLocation();
    else
      Loc = RD->getLocation();
    
    // If we have a virtual destructor, look up the deallocation function
    FunctionDecl *OperatorDelete = 0;
    DeclarationName Name = 
    Context.DeclarationNames.getCXXOperatorName(OO_Delete);
    if (FindDeallocationFunction(Loc, RD, Name, OperatorDelete))
      return true;

    MarkDeclarationReferenced(Loc, OperatorDelete);
    
    Destructor->setOperatorDelete(OperatorDelete);
  }
  
  return false;
}

static inline bool
FTIHasSingleVoidArgument(DeclaratorChunk::FunctionTypeInfo &FTI) {
  return (FTI.NumArgs == 1 && !FTI.isVariadic && FTI.ArgInfo[0].Ident == 0 &&
          FTI.ArgInfo[0].Param &&
          cast<ParmVarDecl>(FTI.ArgInfo[0].Param)->getType()->isVoidType());
}

/// CheckDestructorDeclarator - Called by ActOnDeclarator to check
/// the well-formednes of the destructor declarator @p D with type @p
/// R. If there are any errors in the declarator, this routine will
/// emit diagnostics and set the declarator to invalid.  Even if this happens,
/// will be updated to reflect a well-formed type for the destructor and
/// returned.
QualType Sema::CheckDestructorDeclarator(Declarator &D, QualType R,
                                         StorageClass& SC) {
  // C++ [class.dtor]p1:
  //   [...] A typedef-name that names a class is a class-name
  //   (7.1.3); however, a typedef-name that names a class shall not
  //   be used as the identifier in the declarator for a destructor
  //   declaration.
  QualType DeclaratorType = GetTypeFromParser(D.getName().DestructorName);
  if (const TypedefType *TT = DeclaratorType->getAs<TypedefType>())
    Diag(D.getIdentifierLoc(), diag::err_destructor_typedef_name)
      << DeclaratorType << isa<TypeAliasDecl>(TT->getDecl());
  else if (const TemplateSpecializationType *TST =
             DeclaratorType->getAs<TemplateSpecializationType>())
    if (TST->isTypeAlias())
      Diag(D.getIdentifierLoc(), diag::err_destructor_typedef_name)
        << DeclaratorType << 1;

  // C++ [class.dtor]p2:
  //   A destructor is used to destroy objects of its class type. A
  //   destructor takes no parameters, and no return type can be
  //   specified for it (not even void). The address of a destructor
  //   shall not be taken. A destructor shall not be static. A
  //   destructor can be invoked for a const, volatile or const
  //   volatile object. A destructor shall not be declared const,
  //   volatile or const volatile (9.3.2).
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_destructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc())
        << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());
    
    SC = SC_None;
  }
  if (D.getDeclSpec().hasTypeSpecifier() && !D.isInvalidType()) {
    // Destructors don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float ~X();
    //   };
    //
    // The return type will be eliminated later.
    Diag(D.getIdentifierLoc(), diag::err_destructor_return_type)
      << SourceRange(D.getDeclSpec().getTypeSpecTypeLoc())
      << SourceRange(D.getIdentifierLoc());
  }

  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.TypeQuals != 0 && !D.isInvalidType()) {
    if (FTI.TypeQuals & Qualifiers::Const)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_destructor)
        << "const" << SourceRange(D.getIdentifierLoc());
    if (FTI.TypeQuals & Qualifiers::Volatile)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_destructor)
        << "volatile" << SourceRange(D.getIdentifierLoc());
    if (FTI.TypeQuals & Qualifiers::Restrict)
      Diag(D.getIdentifierLoc(), diag::err_invalid_qualified_destructor)
        << "restrict" << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }

  // C++0x [class.dtor]p2:
  //   A destructor shall not be declared with a ref-qualifier.
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_destructor)
      << FTI.RefQualifierIsLValueRef
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }
  
  // Make sure we don't have any parameters.
  if (FTI.NumArgs > 0 && !FTIHasSingleVoidArgument(FTI)) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_with_params);

    // Delete the parameters.
    FTI.freeArgs();
    D.setInvalidType();
  }

  // Make sure the destructor isn't variadic.
  if (FTI.isVariadic) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_variadic);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any type qualifiers or
  // parameters (in case any of the errors above fired) and with
  // "void" as the return type, since destructors don't have return
  // types. 
  if (!D.isInvalidType())
    return R;

  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();
  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.Variadic = false;
  EPI.TypeQuals = 0;
  EPI.RefQualifier = RQ_None;
  return Context.getFunctionType(Context.VoidTy, 0, 0, EPI);
}

/// CheckConversionDeclarator - Called by ActOnDeclarator to check the
/// well-formednes of the conversion function declarator @p D with
/// type @p R. If there are any errors in the declarator, this routine
/// will emit diagnostics and return true. Otherwise, it will return
/// false. Either way, the type @p R will be updated to reflect a
/// well-formed type for the conversion operator.
void Sema::CheckConversionDeclarator(Declarator &D, QualType &R,
                                     StorageClass& SC) {
  // C++ [class.conv.fct]p1:
  //   Neither parameter types nor return type can be specified. The
  //   type of a conversion function (8.3.5) is "function taking no
  //   parameter returning conversion-type-id."
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_conv_function_not_member)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
    SC = SC_None;
  }

  QualType ConvType = GetTypeFromParser(D.getName().ConversionFunctionId);

  if (D.getDeclSpec().hasTypeSpecifier() && !D.isInvalidType()) {
    // Conversion functions don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float operator bool();
    //   };
    //
    // The return type will be changed later anyway.
    Diag(D.getIdentifierLoc(), diag::err_conv_function_return_type)
      << SourceRange(D.getDeclSpec().getTypeSpecTypeLoc())
      << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }

  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();

  // Make sure we don't have any parameters.
  if (Proto->getNumArgs() > 0) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_params);

    // Delete the parameters.
    D.getFunctionTypeInfo().freeArgs();
    D.setInvalidType();
  } else if (Proto->isVariadic()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_variadic);
    D.setInvalidType();
  }

  // Diagnose "&operator bool()" and other such nonsense.  This
  // is actually a gcc extension which we don't support.
  if (Proto->getResultType() != ConvType) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_complex_decl)
      << Proto->getResultType();
    D.setInvalidType();
    ConvType = Proto->getResultType();
  }

  // C++ [class.conv.fct]p4:
  //   The conversion-type-id shall not represent a function type nor
  //   an array type.
  if (ConvType->isArrayType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_array);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  } else if (ConvType->isFunctionType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_function);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any parameters (in case any
  // of the errors above fired) and with the conversion type as the
  // return type.
  if (D.isInvalidType())
    R = Context.getFunctionType(ConvType, 0, 0, Proto->getExtProtoInfo());

  // C++0x explicit conversion operators.
  if (D.getDeclSpec().isExplicitSpecified() && !getLangOptions().CPlusPlus0x)
    Diag(D.getDeclSpec().getExplicitSpecLoc(),
         diag::warn_explicit_conversion_functions)
      << SourceRange(D.getDeclSpec().getExplicitSpecLoc());
}

/// ActOnConversionDeclarator - Called by ActOnDeclarator to complete
/// the declaration of the given C++ conversion function. This routine
/// is responsible for recording the conversion function in the C++
/// class, if possible.
Decl *Sema::ActOnConversionDeclarator(CXXConversionDecl *Conversion) {
  assert(Conversion && "Expected to receive a conversion function declaration");

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Conversion->getDeclContext());

  // Make sure we aren't redeclaring the conversion function.
  QualType ConvType = Context.getCanonicalType(Conversion->getConversionType());

  // C++ [class.conv.fct]p1:
  //   [...] A conversion function is never used to convert a
  //   (possibly cv-qualified) object to the (possibly cv-qualified)
  //   same object type (or a reference to it), to a (possibly
  //   cv-qualified) base class of that type (or a reference to it),
  //   or to (possibly cv-qualified) void.
  // FIXME: Suppress this warning if the conversion function ends up being a
  // virtual function that overrides a virtual function in a base class.
  QualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  if (const ReferenceType *ConvTypeRef = ConvType->getAs<ReferenceType>())
    ConvType = ConvTypeRef->getPointeeType();
  if (Conversion->getTemplateSpecializationKind() != TSK_Undeclared &&
      Conversion->getTemplateSpecializationKind() != TSK_ExplicitSpecialization)
    /* Suppress diagnostics for instantiations. */;
  else if (ConvType->isRecordType()) {
    ConvType = Context.getCanonicalType(ConvType).getUnqualifiedType();
    if (ConvType == ClassType)
      Diag(Conversion->getLocation(), diag::warn_conv_to_self_not_used)
        << ClassType;
    else if (IsDerivedFrom(ClassType, ConvType))
      Diag(Conversion->getLocation(), diag::warn_conv_to_base_not_used)
        <<  ClassType << ConvType;
  } else if (ConvType->isVoidType()) {
    Diag(Conversion->getLocation(), diag::warn_conv_to_void_not_used)
      << ClassType << ConvType;
  }

  if (FunctionTemplateDecl *ConversionTemplate
                                = Conversion->getDescribedFunctionTemplate())
    return ConversionTemplate;
  
  return Conversion;
}

//===----------------------------------------------------------------------===//
// Namespace Handling
//===----------------------------------------------------------------------===//



/// ActOnStartNamespaceDef - This is called at the start of a namespace
/// definition.
Decl *Sema::ActOnStartNamespaceDef(Scope *NamespcScope,
                                   SourceLocation InlineLoc,
                                   SourceLocation NamespaceLoc,
                                   SourceLocation IdentLoc,
                                   IdentifierInfo *II,
                                   SourceLocation LBrace,
                                   AttributeList *AttrList) {
  SourceLocation StartLoc = InlineLoc.isValid() ? InlineLoc : NamespaceLoc;
  // For anonymous namespace, take the location of the left brace.
  SourceLocation Loc = II ? IdentLoc : LBrace;
  NamespaceDecl *Namespc = NamespaceDecl::Create(Context, CurContext,
                                                 StartLoc, Loc, II);
  Namespc->setInline(InlineLoc.isValid());

  Scope *DeclRegionScope = NamespcScope->getParent();

  ProcessDeclAttributeList(DeclRegionScope, Namespc, AttrList);

  if (const VisibilityAttr *Attr = Namespc->getAttr<VisibilityAttr>())
    PushNamespaceVisibilityAttr(Attr);

  if (II) {
    // C++ [namespace.def]p2:
    //   The identifier in an original-namespace-definition shall not
    //   have been previously defined in the declarative region in
    //   which the original-namespace-definition appears. The
    //   identifier in an original-namespace-definition is the name of
    //   the namespace. Subsequently in that declarative region, it is
    //   treated as an original-namespace-name.
    //
    // Since namespace names are unique in their scope, and we don't
    // look through using directives, just look for any ordinary names.
    
    const unsigned IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Member | 
      Decl::IDNS_Type | Decl::IDNS_Using | Decl::IDNS_Tag | 
      Decl::IDNS_Namespace;
    NamedDecl *PrevDecl = 0;
    for (DeclContext::lookup_result R 
            = CurContext->getRedeclContext()->lookup(II);
         R.first != R.second; ++R.first) {
      if ((*R.first)->getIdentifierNamespace() & IDNS) {
        PrevDecl = *R.first;
        break;
      }
    }
    
    if (NamespaceDecl *OrigNS = dyn_cast_or_null<NamespaceDecl>(PrevDecl)) {
      // This is an extended namespace definition.
      if (Namespc->isInline() != OrigNS->isInline()) {
        // inline-ness must match
        if (OrigNS->isInline()) {
          // The user probably just forgot the 'inline', so suggest that it
          // be added back.
          Diag(Namespc->getLocation(), 
               diag::warn_inline_namespace_reopened_noninline)
            << FixItHint::CreateInsertion(NamespaceLoc, "inline ");
        } else {
          Diag(Namespc->getLocation(), diag::err_inline_namespace_mismatch)
            << Namespc->isInline();
        }
        Diag(OrigNS->getLocation(), diag::note_previous_definition);

        // Recover by ignoring the new namespace's inline status.
        Namespc->setInline(OrigNS->isInline());
      }

      // Attach this namespace decl to the chain of extended namespace
      // definitions.
      OrigNS->setNextNamespace(Namespc);
      Namespc->setOriginalNamespace(OrigNS->getOriginalNamespace());

      // Remove the previous declaration from the scope.
      if (DeclRegionScope->isDeclScope(OrigNS)) {
        IdResolver.RemoveDecl(OrigNS);
        DeclRegionScope->RemoveDecl(OrigNS);
      }
    } else if (PrevDecl) {
      // This is an invalid name redefinition.
      Diag(Namespc->getLocation(), diag::err_redefinition_different_kind)
       << Namespc->getDeclName();
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      Namespc->setInvalidDecl();
      // Continue on to push Namespc as current DeclContext and return it.
    } else if (II->isStr("std") && 
               CurContext->getRedeclContext()->isTranslationUnit()) {
      // This is the first "real" definition of the namespace "std", so update
      // our cache of the "std" namespace to point at this definition.
      if (NamespaceDecl *StdNS = getStdNamespace()) {
        // We had already defined a dummy namespace "std". Link this new 
        // namespace definition to the dummy namespace "std".
        StdNS->setNextNamespace(Namespc);
        StdNS->setLocation(IdentLoc);
        Namespc->setOriginalNamespace(StdNS->getOriginalNamespace());
      }
      
      // Make our StdNamespace cache point at the first real definition of the
      // "std" namespace.
      StdNamespace = Namespc;

      // Add this instance of "std" to the set of known namespaces
      KnownNamespaces[Namespc] = false;
    } else if (!Namespc->isInline()) {
      // Since this is an "original" namespace, add it to the known set of
      // namespaces if it is not an inline namespace.
      KnownNamespaces[Namespc] = false;
    }

    PushOnScopeChains(Namespc, DeclRegionScope);
  } else {
    // Anonymous namespaces.
    assert(Namespc->isAnonymousNamespace());

    // Link the anonymous namespace into its parent.
    NamespaceDecl *PrevDecl;
    DeclContext *Parent = CurContext->getRedeclContext();
    if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(Parent)) {
      PrevDecl = TU->getAnonymousNamespace();
      TU->setAnonymousNamespace(Namespc);
    } else {
      NamespaceDecl *ND = cast<NamespaceDecl>(Parent);
      PrevDecl = ND->getAnonymousNamespace();
      ND->setAnonymousNamespace(Namespc);
    }

    // Link the anonymous namespace with its previous declaration.
    if (PrevDecl) {
      assert(PrevDecl->isAnonymousNamespace());
      assert(!PrevDecl->getNextNamespace());
      Namespc->setOriginalNamespace(PrevDecl->getOriginalNamespace());
      PrevDecl->setNextNamespace(Namespc);

      if (Namespc->isInline() != PrevDecl->isInline()) {
        // inline-ness must match
        Diag(Namespc->getLocation(), diag::err_inline_namespace_mismatch)
          << Namespc->isInline();
        Diag(PrevDecl->getLocation(), diag::note_previous_definition);
        Namespc->setInvalidDecl();
        // Recover by ignoring the new namespace's inline status.
        Namespc->setInline(PrevDecl->isInline());
      }
    }

    CurContext->addDecl(Namespc);

    // C++ [namespace.unnamed]p1.  An unnamed-namespace-definition
    //   behaves as if it were replaced by
    //     namespace unique { /* empty body */ }
    //     using namespace unique;
    //     namespace unique { namespace-body }
    //   where all occurrences of 'unique' in a translation unit are
    //   replaced by the same identifier and this identifier differs
    //   from all other identifiers in the entire program.

    // We just create the namespace with an empty name and then add an
    // implicit using declaration, just like the standard suggests.
    //
    // CodeGen enforces the "universally unique" aspect by giving all
    // declarations semantically contained within an anonymous
    // namespace internal linkage.

    if (!PrevDecl) {
      UsingDirectiveDecl* UD
        = UsingDirectiveDecl::Create(Context, CurContext,
                                     /* 'using' */ LBrace,
                                     /* 'namespace' */ SourceLocation(),
                                     /* qualifier */ NestedNameSpecifierLoc(),
                                     /* identifier */ SourceLocation(),
                                     Namespc,
                                     /* Ancestor */ CurContext);
      UD->setImplicit();
      CurContext->addDecl(UD);
    }
  }

  // Although we could have an invalid decl (i.e. the namespace name is a
  // redefinition), push it as current DeclContext and try to continue parsing.
  // FIXME: We should be able to push Namespc here, so that the each DeclContext
  // for the namespace has the declarations that showed up in that particular
  // namespace definition.
  PushDeclContext(NamespcScope, Namespc);
  return Namespc;
}

/// getNamespaceDecl - Returns the namespace a decl represents. If the decl
/// is a namespace alias, returns the namespace it points to.
static inline NamespaceDecl *getNamespaceDecl(NamedDecl *D) {
  if (NamespaceAliasDecl *AD = dyn_cast_or_null<NamespaceAliasDecl>(D))
    return AD->getNamespace();
  return dyn_cast_or_null<NamespaceDecl>(D);
}

/// ActOnFinishNamespaceDef - This callback is called after a namespace is
/// exited. Decl is the DeclTy returned by ActOnStartNamespaceDef.
void Sema::ActOnFinishNamespaceDef(Decl *Dcl, SourceLocation RBrace) {
  NamespaceDecl *Namespc = dyn_cast_or_null<NamespaceDecl>(Dcl);
  assert(Namespc && "Invalid parameter, expected NamespaceDecl");
  Namespc->setRBraceLoc(RBrace);
  PopDeclContext();
  if (Namespc->hasAttr<VisibilityAttr>())
    PopPragmaVisibility();
}

CXXRecordDecl *Sema::getStdBadAlloc() const {
  return cast_or_null<CXXRecordDecl>(
                                  StdBadAlloc.get(Context.getExternalSource()));
}

NamespaceDecl *Sema::getStdNamespace() const {
  return cast_or_null<NamespaceDecl>(
                                 StdNamespace.get(Context.getExternalSource()));
}

/// \brief Retrieve the special "std" namespace, which may require us to 
/// implicitly define the namespace.
NamespaceDecl *Sema::getOrCreateStdNamespace() {
  if (!StdNamespace) {
    // The "std" namespace has not yet been defined, so build one implicitly.
    StdNamespace = NamespaceDecl::Create(Context, 
                                         Context.getTranslationUnitDecl(),
                                         SourceLocation(), SourceLocation(),
                                         &PP.getIdentifierTable().get("std"));
    getStdNamespace()->setImplicit(true);
  }
  
  return getStdNamespace();
}

/// \brief Determine whether a using statement is in a context where it will be
/// apply in all contexts.
static bool IsUsingDirectiveInToplevelContext(DeclContext *CurContext) {
  switch (CurContext->getDeclKind()) {
    case Decl::TranslationUnit:
      return true;
    case Decl::LinkageSpec:
      return IsUsingDirectiveInToplevelContext(CurContext->getParent());
    default:
      return false;
  }
}

static bool TryNamespaceTypoCorrection(Sema &S, LookupResult &R, Scope *Sc,
                                       CXXScopeSpec &SS,
                                       SourceLocation IdentLoc,
                                       IdentifierInfo *Ident) {
  R.clear();
  if (TypoCorrection Corrected = S.CorrectTypo(R.getLookupNameInfo(),
                                               R.getLookupKind(), Sc, &SS, NULL,
                                               false, S.CTC_NoKeywords, NULL)) {
    if (Corrected.getCorrectionDeclAs<NamespaceDecl>() ||
        Corrected.getCorrectionDeclAs<NamespaceAliasDecl>()) {
      std::string CorrectedStr(Corrected.getAsString(S.getLangOptions()));
      std::string CorrectedQuotedStr(Corrected.getQuoted(S.getLangOptions()));
      if (DeclContext *DC = S.computeDeclContext(SS, false))
        S.Diag(IdentLoc, diag::err_using_directive_member_suggest)
          << Ident << DC << CorrectedQuotedStr << SS.getRange()
          << FixItHint::CreateReplacement(IdentLoc, CorrectedStr);
      else
        S.Diag(IdentLoc, diag::err_using_directive_suggest)
          << Ident << CorrectedQuotedStr
          << FixItHint::CreateReplacement(IdentLoc, CorrectedStr);

      S.Diag(Corrected.getCorrectionDecl()->getLocation(),
           diag::note_namespace_defined_here) << CorrectedQuotedStr;

      Ident = Corrected.getCorrectionAsIdentifierInfo();
      R.addDecl(Corrected.getCorrectionDecl());
      return true;
    }
    R.setLookupName(Ident);
  }
  return false;
}

Decl *Sema::ActOnUsingDirective(Scope *S,
                                          SourceLocation UsingLoc,
                                          SourceLocation NamespcLoc,
                                          CXXScopeSpec &SS,
                                          SourceLocation IdentLoc,
                                          IdentifierInfo *NamespcName,
                                          AttributeList *AttrList) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  assert(NamespcName && "Invalid NamespcName.");
  assert(IdentLoc.isValid() && "Invalid NamespceName location.");

  // This can only happen along a recovery path.
  while (S->getFlags() & Scope::TemplateParamScope)
    S = S->getParent();
  assert(S->getFlags() & Scope::DeclScope && "Invalid Scope.");

  UsingDirectiveDecl *UDir = 0;
  NestedNameSpecifier *Qualifier = 0;
  if (SS.isSet())
    Qualifier = static_cast<NestedNameSpecifier *>(SS.getScopeRep());
  
  // Lookup namespace name.
  LookupResult R(*this, NamespcName, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS);
  if (R.isAmbiguous())
    return 0;

  if (R.empty()) {
    R.clear();
    // Allow "using namespace std;" or "using namespace ::std;" even if 
    // "std" hasn't been defined yet, for GCC compatibility.
    if ((!Qualifier || Qualifier->getKind() == NestedNameSpecifier::Global) &&
        NamespcName->isStr("std")) {
      Diag(IdentLoc, diag::ext_using_undefined_std);
      R.addDecl(getOrCreateStdNamespace());
      R.resolveKind();
    } 
    // Otherwise, attempt typo correction.
    else TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, NamespcName);
  }
  
  if (!R.empty()) {
    NamedDecl *Named = R.getFoundDecl();
    assert((isa<NamespaceDecl>(Named) || isa<NamespaceAliasDecl>(Named))
        && "expected namespace decl");
    // C++ [namespace.udir]p1:
    //   A using-directive specifies that the names in the nominated
    //   namespace can be used in the scope in which the
    //   using-directive appears after the using-directive. During
    //   unqualified name lookup (3.4.1), the names appear as if they
    //   were declared in the nearest enclosing namespace which
    //   contains both the using-directive and the nominated
    //   namespace. [Note: in this context, "contains" means "contains
    //   directly or indirectly". ]

    // Find enclosing context containing both using-directive and
    // nominated namespace.
    NamespaceDecl *NS = getNamespaceDecl(Named);
    DeclContext *CommonAncestor = cast<DeclContext>(NS);
    while (CommonAncestor && !CommonAncestor->Encloses(CurContext))
      CommonAncestor = CommonAncestor->getParent();

    UDir = UsingDirectiveDecl::Create(Context, CurContext, UsingLoc, NamespcLoc,
                                      SS.getWithLocInContext(Context),
                                      IdentLoc, Named, CommonAncestor);

    if (IsUsingDirectiveInToplevelContext(CurContext) &&
        !SourceMgr.isFromMainFile(SourceMgr.getExpansionLoc(IdentLoc))) {
      Diag(IdentLoc, diag::warn_using_directive_in_header);
    }

    PushUsingDirective(S, UDir);
  } else {
    Diag(IdentLoc, diag::err_expected_namespace_name) << SS.getRange();
  }

  // FIXME: We ignore attributes for now.
  return UDir;
}

void Sema::PushUsingDirective(Scope *S, UsingDirectiveDecl *UDir) {
  // If scope has associated entity, then using directive is at namespace
  // or translation unit scope. We add UsingDirectiveDecls, into
  // it's lookup structure.
  if (DeclContext *Ctx = static_cast<DeclContext*>(S->getEntity()))
    Ctx->addDecl(UDir);
  else
    // Otherwise it is block-sope. using-directives will affect lookup
    // only to the end of scope.
    S->PushUsingDirective(UDir);
}


Decl *Sema::ActOnUsingDeclaration(Scope *S,
                                  AccessSpecifier AS,
                                  bool HasUsingKeyword,
                                  SourceLocation UsingLoc,
                                  CXXScopeSpec &SS,
                                  UnqualifiedId &Name,
                                  AttributeList *AttrList,
                                  bool IsTypeName,
                                  SourceLocation TypenameLoc) {
  assert(S->getFlags() & Scope::DeclScope && "Invalid Scope.");

  switch (Name.getKind()) {
  case UnqualifiedId::IK_ImplicitSelfParam:
  case UnqualifiedId::IK_Identifier:
  case UnqualifiedId::IK_OperatorFunctionId:
  case UnqualifiedId::IK_LiteralOperatorId:
  case UnqualifiedId::IK_ConversionFunctionId:
    break;
      
  case UnqualifiedId::IK_ConstructorName:
  case UnqualifiedId::IK_ConstructorTemplateId:
    // C++0x inherited constructors.
    if (getLangOptions().CPlusPlus0x) break;

    Diag(Name.getSourceRange().getBegin(), diag::err_using_decl_constructor)
      << SS.getRange();
    return 0;
      
  case UnqualifiedId::IK_DestructorName:
    Diag(Name.getSourceRange().getBegin(), diag::err_using_decl_destructor)
      << SS.getRange();
    return 0;
      
  case UnqualifiedId::IK_TemplateId:
    Diag(Name.getSourceRange().getBegin(), diag::err_using_decl_template_id)
      << SourceRange(Name.TemplateId->LAngleLoc, Name.TemplateId->RAngleLoc);
    return 0;
  }

  DeclarationNameInfo TargetNameInfo = GetNameFromUnqualifiedId(Name);
  DeclarationName TargetName = TargetNameInfo.getName();
  if (!TargetName)
    return 0;

  // Warn about using declarations.
  // TODO: store that the declaration was written without 'using' and
  // talk about access decls instead of using decls in the
  // diagnostics.
  if (!HasUsingKeyword) {
    UsingLoc = Name.getSourceRange().getBegin();
    
    Diag(UsingLoc, diag::warn_access_decl_deprecated)
      << FixItHint::CreateInsertion(SS.getRange().getBegin(), "using ");
  }

  if (DiagnoseUnexpandedParameterPack(SS, UPPC_UsingDeclaration) ||
      DiagnoseUnexpandedParameterPack(TargetNameInfo, UPPC_UsingDeclaration))
    return 0;

  NamedDecl *UD = BuildUsingDeclaration(S, AS, UsingLoc, SS,
                                        TargetNameInfo, AttrList,
                                        /* IsInstantiation */ false,
                                        IsTypeName, TypenameLoc);
  if (UD)
    PushOnScopeChains(UD, S, /*AddToContext*/ false);

  return UD;
}

/// \brief Determine whether a using declaration considers the given
/// declarations as "equivalent", e.g., if they are redeclarations of
/// the same entity or are both typedefs of the same type.
static bool 
IsEquivalentForUsingDecl(ASTContext &Context, NamedDecl *D1, NamedDecl *D2,
                         bool &SuppressRedeclaration) {
  if (D1->getCanonicalDecl() == D2->getCanonicalDecl()) {
    SuppressRedeclaration = false;
    return true;
  }

  if (TypedefNameDecl *TD1 = dyn_cast<TypedefNameDecl>(D1))
    if (TypedefNameDecl *TD2 = dyn_cast<TypedefNameDecl>(D2)) {
      SuppressRedeclaration = true;
      return Context.hasSameType(TD1->getUnderlyingType(),
                                 TD2->getUnderlyingType());
    }

  return false;
}


/// Determines whether to create a using shadow decl for a particular
/// decl, given the set of decls existing prior to this using lookup.
bool Sema::CheckUsingShadowDecl(UsingDecl *Using, NamedDecl *Orig,
                                const LookupResult &Previous) {
  // Diagnose finding a decl which is not from a base class of the
  // current class.  We do this now because there are cases where this
  // function will silently decide not to build a shadow decl, which
  // will pre-empt further diagnostics.
  //
  // We don't need to do this in C++0x because we do the check once on
  // the qualifier.
  //
  // FIXME: diagnose the following if we care enough:
  //   struct A { int foo; };
  //   struct B : A { using A::foo; };
  //   template <class T> struct C : A {};
  //   template <class T> struct D : C<T> { using B::foo; } // <---
  // This is invalid (during instantiation) in C++03 because B::foo
  // resolves to the using decl in B, which is not a base class of D<T>.
  // We can't diagnose it immediately because C<T> is an unknown
  // specialization.  The UsingShadowDecl in D<T> then points directly
  // to A::foo, which will look well-formed when we instantiate.
  // The right solution is to not collapse the shadow-decl chain.
  if (!getLangOptions().CPlusPlus0x && CurContext->isRecord()) {
    DeclContext *OrigDC = Orig->getDeclContext();

    // Handle enums and anonymous structs.
    if (isa<EnumDecl>(OrigDC)) OrigDC = OrigDC->getParent();
    CXXRecordDecl *OrigRec = cast<CXXRecordDecl>(OrigDC);
    while (OrigRec->isAnonymousStructOrUnion())
      OrigRec = cast<CXXRecordDecl>(OrigRec->getDeclContext());

    if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(OrigRec)) {
      if (OrigDC == CurContext) {
        Diag(Using->getLocation(),
             diag::err_using_decl_nested_name_specifier_is_current_class)
          << Using->getQualifierLoc().getSourceRange();
        Diag(Orig->getLocation(), diag::note_using_decl_target);
        return true;
      }

      Diag(Using->getQualifierLoc().getBeginLoc(),
           diag::err_using_decl_nested_name_specifier_is_not_base_class)
        << Using->getQualifier()
        << cast<CXXRecordDecl>(CurContext)
        << Using->getQualifierLoc().getSourceRange();
      Diag(Orig->getLocation(), diag::note_using_decl_target);
      return true;
    }
  }

  if (Previous.empty()) return false;

  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target))
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();

  // If the target happens to be one of the previous declarations, we
  // don't have a conflict.
  // 
  // FIXME: but we might be increasing its access, in which case we
  // should redeclare it.
  NamedDecl *NonTag = 0, *Tag = 0;
  for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
         I != E; ++I) {
    NamedDecl *D = (*I)->getUnderlyingDecl();
    bool Result;
    if (IsEquivalentForUsingDecl(Context, D, Target, Result))
      return Result;

    (isa<TagDecl>(D) ? Tag : NonTag) = D;
  }

  if (Target->isFunctionOrFunctionTemplate()) {
    FunctionDecl *FD;
    if (isa<FunctionTemplateDecl>(Target))
      FD = cast<FunctionTemplateDecl>(Target)->getTemplatedDecl();
    else
      FD = cast<FunctionDecl>(Target);

    NamedDecl *OldDecl = 0;
    switch (CheckOverload(0, FD, Previous, OldDecl, /*IsForUsingDecl*/ true)) {
    case Ovl_Overload:
      return false;

    case Ovl_NonFunction:
      Diag(Using->getLocation(), diag::err_using_decl_conflict);
      break;
      
    // We found a decl with the exact signature.
    case Ovl_Match:
      // If we're in a record, we want to hide the target, so we
      // return true (without a diagnostic) to tell the caller not to
      // build a shadow decl.
      if (CurContext->isRecord())
        return true;

      // If we're not in a record, this is an error.
      Diag(Using->getLocation(), diag::err_using_decl_conflict);
      break;
    }

    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(OldDecl->getLocation(), diag::note_using_decl_conflict);
    return true;
  }

  // Target is not a function.

  if (isa<TagDecl>(Target)) {
    // No conflict between a tag and a non-tag.
    if (!Tag) return false;

    Diag(Using->getLocation(), diag::err_using_decl_conflict);
    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(Tag->getLocation(), diag::note_using_decl_conflict);
    return true;
  }

  // No conflict between a tag and a non-tag.
  if (!NonTag) return false;

  Diag(Using->getLocation(), diag::err_using_decl_conflict);
  Diag(Target->getLocation(), diag::note_using_decl_target);
  Diag(NonTag->getLocation(), diag::note_using_decl_conflict);
  return true;
}

/// Builds a shadow declaration corresponding to a 'using' declaration.
UsingShadowDecl *Sema::BuildUsingShadowDecl(Scope *S,
                                            UsingDecl *UD,
                                            NamedDecl *Orig) {

  // If we resolved to another shadow declaration, just coalesce them.
  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target)) {
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();
    assert(!isa<UsingShadowDecl>(Target) && "nested shadow declaration");
  }
  
  UsingShadowDecl *Shadow
    = UsingShadowDecl::Create(Context, CurContext,
                              UD->getLocation(), UD, Target);
  UD->addShadowDecl(Shadow);
  
  Shadow->setAccess(UD->getAccess());
  if (Orig->isInvalidDecl() || UD->isInvalidDecl())
    Shadow->setInvalidDecl();
  
  if (S)
    PushOnScopeChains(Shadow, S);
  else
    CurContext->addDecl(Shadow);


  return Shadow;
}

/// Hides a using shadow declaration.  This is required by the current
/// using-decl implementation when a resolvable using declaration in a
/// class is followed by a declaration which would hide or override
/// one or more of the using decl's targets; for example:
///
///   struct Base { void foo(int); };
///   struct Derived : Base {
///     using Base::foo;
///     void foo(int);
///   };
///
/// The governing language is C++03 [namespace.udecl]p12:
///
///   When a using-declaration brings names from a base class into a
///   derived class scope, member functions in the derived class
///   override and/or hide member functions with the same name and
///   parameter types in a base class (rather than conflicting).
///
/// There are two ways to implement this:
///   (1) optimistically create shadow decls when they're not hidden
///       by existing declarations, or
///   (2) don't create any shadow decls (or at least don't make them
///       visible) until we've fully parsed/instantiated the class.
/// The problem with (1) is that we might have to retroactively remove
/// a shadow decl, which requires several O(n) operations because the
/// decl structures are (very reasonably) not designed for removal.
/// (2) avoids this but is very fiddly and phase-dependent.
void Sema::HideUsingShadowDecl(Scope *S, UsingShadowDecl *Shadow) {
  if (Shadow->getDeclName().getNameKind() ==
        DeclarationName::CXXConversionFunctionName)
    cast<CXXRecordDecl>(Shadow->getDeclContext())->removeConversion(Shadow);

  // Remove it from the DeclContext...
  Shadow->getDeclContext()->removeDecl(Shadow);

  // ...and the scope, if applicable...
  if (S) {
    S->RemoveDecl(Shadow);
    IdResolver.RemoveDecl(Shadow);
  }

  // ...and the using decl.
  Shadow->getUsingDecl()->removeShadowDecl(Shadow);

  // TODO: complain somehow if Shadow was used.  It shouldn't
  // be possible for this to happen, because...?
}

/// Builds a using declaration.
///
/// \param IsInstantiation - Whether this call arises from an
///   instantiation of an unresolved using declaration.  We treat
///   the lookup differently for these declarations.
NamedDecl *Sema::BuildUsingDeclaration(Scope *S, AccessSpecifier AS,
                                       SourceLocation UsingLoc,
                                       CXXScopeSpec &SS,
                                       const DeclarationNameInfo &NameInfo,
                                       AttributeList *AttrList,
                                       bool IsInstantiation,
                                       bool IsTypeName,
                                       SourceLocation TypenameLoc) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  SourceLocation IdentLoc = NameInfo.getLoc();
  assert(IdentLoc.isValid() && "Invalid TargetName location.");

  // FIXME: We ignore attributes for now.

  if (SS.isEmpty()) {
    Diag(IdentLoc, diag::err_using_requires_qualname);
    return 0;
  }

  // Do the redeclaration lookup in the current scope.
  LookupResult Previous(*this, NameInfo, LookupUsingDeclName,
                        ForRedeclaration);
  Previous.setHideTags(false);
  if (S) {
    LookupName(Previous, S);

    // It is really dumb that we have to do this.
    LookupResult::Filter F = Previous.makeFilter();
    while (F.hasNext()) {
      NamedDecl *D = F.next();
      if (!isDeclInScope(D, CurContext, S))
        F.erase();
    }
    F.done();
  } else {
    assert(IsInstantiation && "no scope in non-instantiation");
    assert(CurContext->isRecord() && "scope not record in instantiation");
    LookupQualifiedName(Previous, CurContext);
  }

  // Check for invalid redeclarations.
  if (CheckUsingDeclRedeclaration(UsingLoc, IsTypeName, SS, IdentLoc, Previous))
    return 0;

  // Check for bad qualifiers.
  if (CheckUsingDeclQualifier(UsingLoc, SS, IdentLoc))
    return 0;

  DeclContext *LookupContext = computeDeclContext(SS);
  NamedDecl *D;
  NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
  if (!LookupContext) {
    if (IsTypeName) {
      // FIXME: not all declaration name kinds are legal here
      D = UnresolvedUsingTypenameDecl::Create(Context, CurContext,
                                              UsingLoc, TypenameLoc,
                                              QualifierLoc,
                                              IdentLoc, NameInfo.getName());
    } else {
      D = UnresolvedUsingValueDecl::Create(Context, CurContext, UsingLoc, 
                                           QualifierLoc, NameInfo);
    }
  } else {
    D = UsingDecl::Create(Context, CurContext, UsingLoc, QualifierLoc,
                          NameInfo, IsTypeName);
  }
  D->setAccess(AS);
  CurContext->addDecl(D);

  if (!LookupContext) return D;
  UsingDecl *UD = cast<UsingDecl>(D);

  if (RequireCompleteDeclContext(SS, LookupContext)) {
    UD->setInvalidDecl();
    return UD;
  }

  // Constructor inheriting using decls get special treatment.
  if (NameInfo.getName().getNameKind() == DeclarationName::CXXConstructorName) {
    if (CheckInheritedConstructorUsingDecl(UD))
      UD->setInvalidDecl();
    return UD;
  }

  // Otherwise, look up the target name.

  LookupResult R(*this, NameInfo, LookupOrdinaryName);

  // Unlike most lookups, we don't always want to hide tag
  // declarations: tag names are visible through the using declaration
  // even if hidden by ordinary names, *except* in a dependent context
  // where it's important for the sanity of two-phase lookup.
  if (!IsInstantiation)
    R.setHideTags(false);

  LookupQualifiedName(R, LookupContext);

  if (R.empty()) {
    Diag(IdentLoc, diag::err_no_member) 
      << NameInfo.getName() << LookupContext << SS.getRange();
    UD->setInvalidDecl();
    return UD;
  }

  if (R.isAmbiguous()) {
    UD->setInvalidDecl();
    return UD;
  }

  if (IsTypeName) {
    // If we asked for a typename and got a non-type decl, error out.
    if (!R.getAsSingle<TypeDecl>()) {
      Diag(IdentLoc, diag::err_using_typename_non_type);
      for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
        Diag((*I)->getUnderlyingDecl()->getLocation(),
             diag::note_using_decl_target);
      UD->setInvalidDecl();
      return UD;
    }
  } else {
    // If we asked for a non-typename and we got a type, error out,
    // but only if this is an instantiation of an unresolved using
    // decl.  Otherwise just silently find the type name.
    if (IsInstantiation && R.getAsSingle<TypeDecl>()) {
      Diag(IdentLoc, diag::err_using_dependent_value_is_type);
      Diag(R.getFoundDecl()->getLocation(), diag::note_using_decl_target);
      UD->setInvalidDecl();
      return UD;
    }
  }

  // C++0x N2914 [namespace.udecl]p6:
  // A using-declaration shall not name a namespace.
  if (R.getAsSingle<NamespaceDecl>()) {
    Diag(IdentLoc, diag::err_using_decl_can_not_refer_to_namespace)
      << SS.getRange();
    UD->setInvalidDecl();
    return UD;
  }

  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    if (!CheckUsingShadowDecl(UD, *I, Previous))
      BuildUsingShadowDecl(S, UD, *I);
  }

  return UD;
}

/// Additional checks for a using declaration referring to a constructor name.
bool Sema::CheckInheritedConstructorUsingDecl(UsingDecl *UD) {
  if (UD->isTypeName()) {
    // FIXME: Cannot specify typename when specifying constructor
    return true;
  }

  const Type *SourceType = UD->getQualifier()->getAsType();
  assert(SourceType &&
         "Using decl naming constructor doesn't have type in scope spec.");
  CXXRecordDecl *TargetClass = cast<CXXRecordDecl>(CurContext);

  // Check whether the named type is a direct base class.
  CanQualType CanonicalSourceType = SourceType->getCanonicalTypeUnqualified();
  CXXRecordDecl::base_class_iterator BaseIt, BaseE;
  for (BaseIt = TargetClass->bases_begin(), BaseE = TargetClass->bases_end();
       BaseIt != BaseE; ++BaseIt) {
    CanQualType BaseType = BaseIt->getType()->getCanonicalTypeUnqualified();
    if (CanonicalSourceType == BaseType)
      break;
  }

  if (BaseIt == BaseE) {
    // Did not find SourceType in the bases.
    Diag(UD->getUsingLocation(),
         diag::err_using_decl_constructor_not_in_direct_base)
      << UD->getNameInfo().getSourceRange()
      << QualType(SourceType, 0) << TargetClass;
    return true;
  }

  BaseIt->setInheritConstructors();

  return false;
}

/// Checks that the given using declaration is not an invalid
/// redeclaration.  Note that this is checking only for the using decl
/// itself, not for any ill-formedness among the UsingShadowDecls.
bool Sema::CheckUsingDeclRedeclaration(SourceLocation UsingLoc,
                                       bool isTypeName,
                                       const CXXScopeSpec &SS,
                                       SourceLocation NameLoc,
                                       const LookupResult &Prev) {
  // C++03 [namespace.udecl]p8:
  // C++0x [namespace.udecl]p10:
  //   A using-declaration is a declaration and can therefore be used
  //   repeatedly where (and only where) multiple declarations are
  //   allowed.
  //
  // That's in non-member contexts.
  if (!CurContext->getRedeclContext()->isRecord())
    return false;

  NestedNameSpecifier *Qual
    = static_cast<NestedNameSpecifier*>(SS.getScopeRep());

  for (LookupResult::iterator I = Prev.begin(), E = Prev.end(); I != E; ++I) {
    NamedDecl *D = *I;

    bool DTypename;
    NestedNameSpecifier *DQual;
    if (UsingDecl *UD = dyn_cast<UsingDecl>(D)) {
      DTypename = UD->isTypeName();
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingValueDecl *UD
                 = dyn_cast<UnresolvedUsingValueDecl>(D)) {
      DTypename = false;
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingTypenameDecl *UD
                 = dyn_cast<UnresolvedUsingTypenameDecl>(D)) {
      DTypename = true;
      DQual = UD->getQualifier();
    } else continue;

    // using decls differ if one says 'typename' and the other doesn't.
    // FIXME: non-dependent using decls?
    if (isTypeName != DTypename) continue;

    // using decls differ if they name different scopes (but note that
    // template instantiation can cause this check to trigger when it
    // didn't before instantiation).
    if (Context.getCanonicalNestedNameSpecifier(Qual) !=
        Context.getCanonicalNestedNameSpecifier(DQual))
      continue;

    Diag(NameLoc, diag::err_using_decl_redeclaration) << SS.getRange();
    Diag(D->getLocation(), diag::note_using_decl) << 1;
    return true;
  }

  return false;
}


/// Checks that the given nested-name qualifier used in a using decl
/// in the current context is appropriately related to the current
/// scope.  If an error is found, diagnoses it and returns true.
bool Sema::CheckUsingDeclQualifier(SourceLocation UsingLoc,
                                   const CXXScopeSpec &SS,
                                   SourceLocation NameLoc) {
  DeclContext *NamedContext = computeDeclContext(SS);

  if (!CurContext->isRecord()) {
    // C++03 [namespace.udecl]p3:
    // C++0x [namespace.udecl]p8:
    //   A using-declaration for a class member shall be a member-declaration.

    // If we weren't able to compute a valid scope, it must be a
    // dependent class scope.
    if (!NamedContext || NamedContext->isRecord()) {
      Diag(NameLoc, diag::err_using_decl_can_not_refer_to_class_member)
        << SS.getRange();
      return true;
    }

    // Otherwise, everything is known to be fine.
    return false;
  }

  // The current scope is a record.

  // If the named context is dependent, we can't decide much.
  if (!NamedContext) {
    // FIXME: in C++0x, we can diagnose if we can prove that the
    // nested-name-specifier does not refer to a base class, which is
    // still possible in some cases.

    // Otherwise we have to conservatively report that things might be
    // okay.
    return false;
  }

  if (!NamedContext->isRecord()) {
    // Ideally this would point at the last name in the specifier,
    // but we don't have that level of source info.
    Diag(SS.getRange().getBegin(),
         diag::err_using_decl_nested_name_specifier_is_not_class)
      << (NestedNameSpecifier*) SS.getScopeRep() << SS.getRange();
    return true;
  }

  if (!NamedContext->isDependentContext() &&
      RequireCompleteDeclContext(const_cast<CXXScopeSpec&>(SS), NamedContext))
    return true;

  if (getLangOptions().CPlusPlus0x) {
    // C++0x [namespace.udecl]p3:
    //   In a using-declaration used as a member-declaration, the
    //   nested-name-specifier shall name a base class of the class
    //   being defined.

    if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(
                                 cast<CXXRecordDecl>(NamedContext))) {
      if (CurContext == NamedContext) {
        Diag(NameLoc,
             diag::err_using_decl_nested_name_specifier_is_current_class)
          << SS.getRange();
        return true;
      }

      Diag(SS.getRange().getBegin(),
           diag::err_using_decl_nested_name_specifier_is_not_base_class)
        << (NestedNameSpecifier*) SS.getScopeRep()
        << cast<CXXRecordDecl>(CurContext)
        << SS.getRange();
      return true;
    }

    return false;
  }

  // C++03 [namespace.udecl]p4:
  //   A using-declaration used as a member-declaration shall refer
  //   to a member of a base class of the class being defined [etc.].

  // Salient point: SS doesn't have to name a base class as long as
  // lookup only finds members from base classes.  Therefore we can
  // diagnose here only if we can prove that that can't happen,
  // i.e. if the class hierarchies provably don't intersect.

  // TODO: it would be nice if "definitely valid" results were cached
  // in the UsingDecl and UsingShadowDecl so that these checks didn't
  // need to be repeated.

  struct UserData {
    llvm::DenseSet<const CXXRecordDecl*> Bases;

    static bool collect(const CXXRecordDecl *Base, void *OpaqueData) {
      UserData *Data = reinterpret_cast<UserData*>(OpaqueData);
      Data->Bases.insert(Base);
      return true;
    }

    bool hasDependentBases(const CXXRecordDecl *Class) {
      return !Class->forallBases(collect, this);
    }

    /// Returns true if the base is dependent or is one of the
    /// accumulated base classes.
    static bool doesNotContain(const CXXRecordDecl *Base, void *OpaqueData) {
      UserData *Data = reinterpret_cast<UserData*>(OpaqueData);
      return !Data->Bases.count(Base);
    }

    bool mightShareBases(const CXXRecordDecl *Class) {
      return Bases.count(Class) || !Class->forallBases(doesNotContain, this);
    }
  };

  UserData Data;

  // Returns false if we find a dependent base.
  if (Data.hasDependentBases(cast<CXXRecordDecl>(CurContext)))
    return false;

  // Returns false if the class has a dependent base or if it or one
  // of its bases is present in the base set of the current context.
  if (Data.mightShareBases(cast<CXXRecordDecl>(NamedContext)))
    return false;

  Diag(SS.getRange().getBegin(),
       diag::err_using_decl_nested_name_specifier_is_not_base_class)
    << (NestedNameSpecifier*) SS.getScopeRep()
    << cast<CXXRecordDecl>(CurContext)
    << SS.getRange();

  return true;
}

Decl *Sema::ActOnAliasDeclaration(Scope *S,
                                  AccessSpecifier AS,
                                  MultiTemplateParamsArg TemplateParamLists,
                                  SourceLocation UsingLoc,
                                  UnqualifiedId &Name,
                                  TypeResult Type) {
  // Skip up to the relevant declaration scope.
  while (S->getFlags() & Scope::TemplateParamScope)
    S = S->getParent();
  assert((S->getFlags() & Scope::DeclScope) &&
         "got alias-declaration outside of declaration scope");

  if (Type.isInvalid())
    return 0;

  bool Invalid = false;
  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Name);
  TypeSourceInfo *TInfo = 0;
  GetTypeFromParser(Type.get(), &TInfo);

  if (DiagnoseClassNameShadow(CurContext, NameInfo))
    return 0;

  if (DiagnoseUnexpandedParameterPack(Name.StartLocation, TInfo,
                                      UPPC_DeclarationType)) {
    Invalid = true;
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy, 
                                             TInfo->getTypeLoc().getBeginLoc());
  }

  LookupResult Previous(*this, NameInfo, LookupOrdinaryName, ForRedeclaration);
  LookupName(Previous, S);

  // Warn about shadowing the name of a template parameter.
  if (Previous.isSingleResult() &&
      Previous.getFoundDecl()->isTemplateParameter()) {
    if (DiagnoseTemplateParameterShadow(Name.StartLocation,
                                        Previous.getFoundDecl()))
      Invalid = true;
    Previous.clear();
  }

  assert(Name.Kind == UnqualifiedId::IK_Identifier &&
         "name in alias declaration must be an identifier");
  TypeAliasDecl *NewTD = TypeAliasDecl::Create(Context, CurContext, UsingLoc,
                                               Name.StartLocation,
                                               Name.Identifier, TInfo);

  NewTD->setAccess(AS);

  if (Invalid)
    NewTD->setInvalidDecl();

  CheckTypedefForVariablyModifiedType(S, NewTD);
  Invalid |= NewTD->isInvalidDecl();

  bool Redeclaration = false;

  NamedDecl *NewND;
  if (TemplateParamLists.size()) {
    TypeAliasTemplateDecl *OldDecl = 0;
    TemplateParameterList *OldTemplateParams = 0;

    if (TemplateParamLists.size() != 1) {
      Diag(UsingLoc, diag::err_alias_template_extra_headers)
        << SourceRange(TemplateParamLists.get()[1]->getTemplateLoc(),
         TemplateParamLists.get()[TemplateParamLists.size()-1]->getRAngleLoc());
    }
    TemplateParameterList *TemplateParams = TemplateParamLists.get()[0];

    // Only consider previous declarations in the same scope.
    FilterLookupForScope(Previous, CurContext, S, /*ConsiderLinkage*/false,
                         /*ExplicitInstantiationOrSpecialization*/false);
    if (!Previous.empty()) {
      Redeclaration = true;

      OldDecl = Previous.getAsSingle<TypeAliasTemplateDecl>();
      if (!OldDecl && !Invalid) {
        Diag(UsingLoc, diag::err_redefinition_different_kind)
          << Name.Identifier;

        NamedDecl *OldD = Previous.getRepresentativeDecl();
        if (OldD->getLocation().isValid())
          Diag(OldD->getLocation(), diag::note_previous_definition);

        Invalid = true;
      }

      if (!Invalid && OldDecl && !OldDecl->isInvalidDecl()) {
        if (TemplateParameterListsAreEqual(TemplateParams,
                                           OldDecl->getTemplateParameters(),
                                           /*Complain=*/true,
                                           TPL_TemplateMatch))
          OldTemplateParams = OldDecl->getTemplateParameters();
        else
          Invalid = true;

        TypeAliasDecl *OldTD = OldDecl->getTemplatedDecl();
        if (!Invalid &&
            !Context.hasSameType(OldTD->getUnderlyingType(),
                                 NewTD->getUnderlyingType())) {
          // FIXME: The C++0x standard does not clearly say this is ill-formed,
          // but we can't reasonably accept it.
          Diag(NewTD->getLocation(), diag::err_redefinition_different_typedef)
            << 2 << NewTD->getUnderlyingType() << OldTD->getUnderlyingType();
          if (OldTD->getLocation().isValid())
            Diag(OldTD->getLocation(), diag::note_previous_definition);
          Invalid = true;
        }
      }
    }

    // Merge any previous default template arguments into our parameters,
    // and check the parameter list.
    if (CheckTemplateParameterList(TemplateParams, OldTemplateParams,
                                   TPC_TypeAliasTemplate))
      return 0;

    TypeAliasTemplateDecl *NewDecl =
      TypeAliasTemplateDecl::Create(Context, CurContext, UsingLoc,
                                    Name.Identifier, TemplateParams,
                                    NewTD);

    NewDecl->setAccess(AS);

    if (Invalid)
      NewDecl->setInvalidDecl();
    else if (OldDecl)
      NewDecl->setPreviousDeclaration(OldDecl);

    NewND = NewDecl;
  } else {
    ActOnTypedefNameDecl(S, CurContext, NewTD, Previous, Redeclaration);
    NewND = NewTD;
  }

  if (!Redeclaration)
    PushOnScopeChains(NewND, S);

  return NewND;
}

Decl *Sema::ActOnNamespaceAliasDef(Scope *S,
                                             SourceLocation NamespaceLoc,
                                             SourceLocation AliasLoc,
                                             IdentifierInfo *Alias,
                                             CXXScopeSpec &SS,
                                             SourceLocation IdentLoc,
                                             IdentifierInfo *Ident) {

  // Lookup the namespace name.
  LookupResult R(*this, Ident, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS);

  // Check if we have a previous declaration with the same name.
  NamedDecl *PrevDecl
    = LookupSingleName(S, Alias, AliasLoc, LookupOrdinaryName, 
                       ForRedeclaration);
  if (PrevDecl && !isDeclInScope(PrevDecl, CurContext, S))
    PrevDecl = 0;

  if (PrevDecl) {
    if (NamespaceAliasDecl *AD = dyn_cast<NamespaceAliasDecl>(PrevDecl)) {
      // We already have an alias with the same name that points to the same
      // namespace, so don't create a new one.
      // FIXME: At some point, we'll want to create the (redundant)
      // declaration to maintain better source information.
      if (!R.isAmbiguous() && !R.empty() &&
          AD->getNamespace()->Equals(getNamespaceDecl(R.getFoundDecl())))
        return 0;
    }

    unsigned DiagID = isa<NamespaceDecl>(PrevDecl) ? diag::err_redefinition :
      diag::err_redefinition_different_kind;
    Diag(AliasLoc, DiagID) << Alias;
    Diag(PrevDecl->getLocation(), diag::note_previous_definition);
    return 0;
  }

  if (R.isAmbiguous())
    return 0;

  if (R.empty()) {
    if (!TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, Ident)) {
      Diag(NamespaceLoc, diag::err_expected_namespace_name) << SS.getRange();
      return 0;
    }
  }

  NamespaceAliasDecl *AliasDecl =
    NamespaceAliasDecl::Create(Context, CurContext, NamespaceLoc, AliasLoc,
                               Alias, SS.getWithLocInContext(Context),
                               IdentLoc, R.getFoundDecl());

  PushOnScopeChains(AliasDecl, S);
  return AliasDecl;
}

namespace {
  /// \brief Scoped object used to handle the state changes required in Sema
  /// to implicitly define the body of a C++ member function;
  class ImplicitlyDefinedFunctionScope {
    Sema &S;
    Sema::ContextRAII SavedContext;
    
  public:
    ImplicitlyDefinedFunctionScope(Sema &S, CXXMethodDecl *Method)
      : S(S), SavedContext(S, Method) 
    {
      S.PushFunctionScope();
      S.PushExpressionEvaluationContext(Sema::PotentiallyEvaluated);
    }
    
    ~ImplicitlyDefinedFunctionScope() {
      S.PopExpressionEvaluationContext();
      S.PopFunctionOrBlockScope();
    }
  };
}

Sema::ImplicitExceptionSpecification
Sema::ComputeDefaultedDefaultCtorExceptionSpec(CXXRecordDecl *ClassDecl) {
  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an 
  //   exception-specification. [...]
  ImplicitExceptionSpecification ExceptSpec(Context);
  if (ClassDecl->isInvalidDecl())
    return ExceptSpec;

  // Direct base-class constructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->bases_begin(),
                                       BEnd = ClassDecl->bases_end();
       B != BEnd; ++B) {
    if (B->isVirtual()) // Handled below.
      continue;
    
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>()) {
      CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      CXXConstructorDecl *Constructor = LookupDefaultConstructor(BaseClassDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  // Virtual base-class constructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->vbases_begin(),
                                       BEnd = ClassDecl->vbases_end();
       B != BEnd; ++B) {
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>()) {
      CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      CXXConstructorDecl *Constructor = LookupDefaultConstructor(BaseClassDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  // Field constructors.
  for (RecordDecl::field_iterator F = ClassDecl->field_begin(),
                               FEnd = ClassDecl->field_end();
       F != FEnd; ++F) {
    if (F->hasInClassInitializer()) {
      if (Expr *E = F->getInClassInitializer())
        ExceptSpec.CalledExpr(E);
      else if (!F->isInvalidDecl())
        ExceptSpec.SetDelayed();
    } else if (const RecordType *RecordTy
              = Context.getBaseElementType(F->getType())->getAs<RecordType>()) {
      CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(RecordTy->getDecl());
      CXXConstructorDecl *Constructor = LookupDefaultConstructor(FieldRecDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      // In particular, the problem is that this function never gets called. It
      // might just be ill-formed because this function attempts to refer to
      // a deleted function here.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  return ExceptSpec;
}

CXXConstructorDecl *Sema::DeclareImplicitDefaultConstructor(
                                                     CXXRecordDecl *ClassDecl) {
  // C++ [class.ctor]p5:
  //   A default constructor for a class X is a constructor of class X
  //   that can be called without an argument. If there is no
  //   user-declared constructor for class X, a default constructor is
  //   implicitly declared. An implicitly-declared default constructor
  //   is an inline public member of its class.
  assert(!ClassDecl->hasUserDeclaredConstructor() && 
         "Should not build implicit default constructor!");

  ImplicitExceptionSpecification Spec = 
    ComputeDefaultedDefaultCtorExceptionSpec(ClassDecl);
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();

  // Create the actual constructor declaration.
  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXConstructorDecl *DefaultCon
    = CXXConstructorDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                                 Context.getFunctionType(Context.VoidTy,
                                                         0, 0, EPI),
                                 /*TInfo=*/0,
                                 /*isExplicit=*/false,
                                 /*isInline=*/true,
                                 /*isImplicitlyDeclared=*/true,
                                 // FIXME: apply the rules for definitions here
                                 /*isConstexpr=*/false);
  DefaultCon->setAccess(AS_public);
  DefaultCon->setDefaulted();
  DefaultCon->setImplicit();
  DefaultCon->setTrivial(ClassDecl->hasTrivialDefaultConstructor());
  
  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitDefaultConstructorsDeclared;
  
  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(DefaultCon, S, false);
  ClassDecl->addDecl(DefaultCon);

  if (ShouldDeleteSpecialMember(DefaultCon, CXXDefaultConstructor))
    DefaultCon->setDeletedAsWritten();
  
  return DefaultCon;
}

void Sema::DefineImplicitDefaultConstructor(SourceLocation CurrentLocation,
                                            CXXConstructorDecl *Constructor) {
  assert((Constructor->isDefaulted() && Constructor->isDefaultConstructor() &&
          !Constructor->doesThisDeclarationHaveABody() &&
          !Constructor->isDeleted()) &&
    "DefineImplicitDefaultConstructor - call it for implicit default ctor");

  CXXRecordDecl *ClassDecl = Constructor->getParent();
  assert(ClassDecl && "DefineImplicitDefaultConstructor - invalid constructor");

  ImplicitlyDefinedFunctionScope Scope(*this, Constructor);
  DiagnosticErrorTrap Trap(Diags);
  if (SetCtorInitializers(Constructor, 0, 0, /*AnyErrors=*/false) ||
      Trap.hasErrorOccurred()) {
    Diag(CurrentLocation, diag::note_member_synthesized_at) 
      << CXXDefaultConstructor << Context.getTagDeclType(ClassDecl);
    Constructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Constructor->getLocation();
  Constructor->setBody(new (Context) CompoundStmt(Context, 0, 0, Loc, Loc));

  Constructor->setUsed();
  MarkVTableUsed(CurrentLocation, ClassDecl);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Constructor);
  }
}

/// Get any existing defaulted default constructor for the given class. Do not
/// implicitly define one if it does not exist.
static CXXConstructorDecl *getDefaultedDefaultConstructorUnsafe(Sema &Self,
                                                             CXXRecordDecl *D) {
  ASTContext &Context = Self.Context;
  QualType ClassType = Context.getTypeDeclType(D);
  DeclarationName ConstructorName
    = Context.DeclarationNames.getCXXConstructorName(
                      Context.getCanonicalType(ClassType.getUnqualifiedType()));

  DeclContext::lookup_const_iterator Con, ConEnd;
  for (llvm::tie(Con, ConEnd) = D->lookup(ConstructorName);
       Con != ConEnd; ++Con) {
    // A function template cannot be defaulted.
    if (isa<FunctionTemplateDecl>(*Con))
      continue;

    CXXConstructorDecl *Constructor = cast<CXXConstructorDecl>(*Con);
    if (Constructor->isDefaultConstructor())
      return Constructor->isDefaulted() ? Constructor : 0;
  }
  return 0;
}

void Sema::ActOnFinishDelayedMemberInitializers(Decl *D) {
  if (!D) return;
  AdjustDeclIfTemplate(D);

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(D);
  CXXConstructorDecl *CtorDecl
    = getDefaultedDefaultConstructorUnsafe(*this, ClassDecl);

  if (!CtorDecl) return;

  // Compute the exception specification for the default constructor.
  const FunctionProtoType *CtorTy =
    CtorDecl->getType()->castAs<FunctionProtoType>();
  if (CtorTy->getExceptionSpecType() == EST_Delayed) {
    ImplicitExceptionSpecification Spec = 
      ComputeDefaultedDefaultCtorExceptionSpec(ClassDecl);
    FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
    assert(EPI.ExceptionSpecType != EST_Delayed);

    CtorDecl->setType(Context.getFunctionType(Context.VoidTy, 0, 0, EPI));
  }

  // If the default constructor is explicitly defaulted, checking the exception
  // specification is deferred until now.
  if (!CtorDecl->isInvalidDecl() && CtorDecl->isExplicitlyDefaulted() &&
      !ClassDecl->isDependentType())
    CheckExplicitlyDefaultedDefaultConstructor(CtorDecl);
}

void Sema::DeclareInheritedConstructors(CXXRecordDecl *ClassDecl) {
  // We start with an initial pass over the base classes to collect those that
  // inherit constructors from. If there are none, we can forgo all further
  // processing.
  typedef SmallVector<const RecordType *, 4> BasesVector;
  BasesVector BasesToInheritFrom;
  for (CXXRecordDecl::base_class_iterator BaseIt = ClassDecl->bases_begin(),
                                          BaseE = ClassDecl->bases_end();
         BaseIt != BaseE; ++BaseIt) {
    if (BaseIt->getInheritConstructors()) {
      QualType Base = BaseIt->getType();
      if (Base->isDependentType()) {
        // If we inherit constructors from anything that is dependent, just
        // abort processing altogether. We'll get another chance for the
        // instantiations.
        return;
      }
      BasesToInheritFrom.push_back(Base->castAs<RecordType>());
    }
  }
  if (BasesToInheritFrom.empty())
    return;

  // Now collect the constructors that we already have in the current class.
  // Those take precedence over inherited constructors.
  // C++0x [class.inhctor]p3: [...] a constructor is implicitly declared [...]
  //   unless there is a user-declared constructor with the same signature in
  //   the class where the using-declaration appears.
  llvm::SmallSet<const Type *, 8> ExistingConstructors;
  for (CXXRecordDecl::ctor_iterator CtorIt = ClassDecl->ctor_begin(),
                                    CtorE = ClassDecl->ctor_end();
       CtorIt != CtorE; ++CtorIt) {
    ExistingConstructors.insert(
        Context.getCanonicalType(CtorIt->getType()).getTypePtr());
  }

  Scope *S = getScopeForContext(ClassDecl);
  DeclarationName CreatedCtorName =
      Context.DeclarationNames.getCXXConstructorName(
          ClassDecl->getTypeForDecl()->getCanonicalTypeUnqualified());

  // Now comes the true work.
  // First, we keep a map from constructor types to the base that introduced
  // them. Needed for finding conflicting constructors. We also keep the
  // actually inserted declarations in there, for pretty diagnostics.
  typedef std::pair<CanQualType, CXXConstructorDecl *> ConstructorInfo;
  typedef llvm::DenseMap<const Type *, ConstructorInfo> ConstructorToSourceMap;
  ConstructorToSourceMap InheritedConstructors;
  for (BasesVector::iterator BaseIt = BasesToInheritFrom.begin(),
                             BaseE = BasesToInheritFrom.end();
       BaseIt != BaseE; ++BaseIt) {
    const RecordType *Base = *BaseIt;
    CanQualType CanonicalBase = Base->getCanonicalTypeUnqualified();
    CXXRecordDecl *BaseDecl = cast<CXXRecordDecl>(Base->getDecl());
    for (CXXRecordDecl::ctor_iterator CtorIt = BaseDecl->ctor_begin(),
                                      CtorE = BaseDecl->ctor_end();
         CtorIt != CtorE; ++CtorIt) {
      // Find the using declaration for inheriting this base's constructors.
      DeclarationName Name =
          Context.DeclarationNames.getCXXConstructorName(CanonicalBase);
      UsingDecl *UD = dyn_cast_or_null<UsingDecl>(
          LookupSingleName(S, Name,SourceLocation(), LookupUsingDeclName));
      SourceLocation UsingLoc = UD ? UD->getLocation() :
                                     ClassDecl->getLocation();

      // C++0x [class.inhctor]p1: The candidate set of inherited constructors
      //   from the class X named in the using-declaration consists of actual
      //   constructors and notional constructors that result from the
      //   transformation of defaulted parameters as follows:
      //   - all non-template default constructors of X, and
      //   - for each non-template constructor of X that has at least one
      //     parameter with a default argument, the set of constructors that
      //     results from omitting any ellipsis parameter specification and
      //     successively omitting parameters with a default argument from the
      //     end of the parameter-type-list.
      CXXConstructorDecl *BaseCtor = *CtorIt;
      bool CanBeCopyOrMove = BaseCtor->isCopyOrMoveConstructor();
      const FunctionProtoType *BaseCtorType =
          BaseCtor->getType()->getAs<FunctionProtoType>();

      for (unsigned params = BaseCtor->getMinRequiredArguments(),
                    maxParams = BaseCtor->getNumParams();
           params <= maxParams; ++params) {
        // Skip default constructors. They're never inherited.
        if (params == 0)
          continue;
        // Skip copy and move constructors for the same reason.
        if (CanBeCopyOrMove && params == 1)
          continue;

        // Build up a function type for this particular constructor.
        // FIXME: The working paper does not consider that the exception spec
        // for the inheriting constructor might be larger than that of the
        // source. This code doesn't yet, either. When it does, this code will
        // need to be delayed until after exception specifications and in-class
        // member initializers are attached.
        const Type *NewCtorType;
        if (params == maxParams)
          NewCtorType = BaseCtorType;
        else {
          SmallVector<QualType, 16> Args;
          for (unsigned i = 0; i < params; ++i) {
            Args.push_back(BaseCtorType->getArgType(i));
          }
          FunctionProtoType::ExtProtoInfo ExtInfo =
              BaseCtorType->getExtProtoInfo();
          ExtInfo.Variadic = false;
          NewCtorType = Context.getFunctionType(BaseCtorType->getResultType(),
                                                Args.data(), params, ExtInfo)
                       .getTypePtr();
        }
        const Type *CanonicalNewCtorType =
            Context.getCanonicalType(NewCtorType);

        // Now that we have the type, first check if the class already has a
        // constructor with this signature.
        if (ExistingConstructors.count(CanonicalNewCtorType))
          continue;

        // Then we check if we have already declared an inherited constructor
        // with this signature.
        std::pair<ConstructorToSourceMap::iterator, bool> result =
            InheritedConstructors.insert(std::make_pair(
                CanonicalNewCtorType,
                std::make_pair(CanonicalBase, (CXXConstructorDecl*)0)));
        if (!result.second) {
          // Already in the map. If it came from a different class, that's an
          // error. Not if it's from the same.
          CanQualType PreviousBase = result.first->second.first;
          if (CanonicalBase != PreviousBase) {
            const CXXConstructorDecl *PrevCtor = result.first->second.second;
            const CXXConstructorDecl *PrevBaseCtor =
                PrevCtor->getInheritedConstructor();
            assert(PrevBaseCtor && "Conflicting constructor was not inherited");

            Diag(UsingLoc, diag::err_using_decl_constructor_conflict);
            Diag(BaseCtor->getLocation(),
                 diag::note_using_decl_constructor_conflict_current_ctor);
            Diag(PrevBaseCtor->getLocation(),
                 diag::note_using_decl_constructor_conflict_previous_ctor);
            Diag(PrevCtor->getLocation(),
                 diag::note_using_decl_constructor_conflict_previous_using);
          }
          continue;
        }

        // OK, we're there, now add the constructor.
        // C++0x [class.inhctor]p8: [...] that would be performed by a
        //   user-written inline constructor [...]
        DeclarationNameInfo DNI(CreatedCtorName, UsingLoc);
        CXXConstructorDecl *NewCtor = CXXConstructorDecl::Create(
            Context, ClassDecl, UsingLoc, DNI, QualType(NewCtorType, 0),
            /*TInfo=*/0, BaseCtor->isExplicit(), /*Inline=*/true,
            /*ImplicitlyDeclared=*/true,
            // FIXME: Due to a defect in the standard, we treat inherited
            // constructors as constexpr even if that makes them ill-formed.
            /*Constexpr=*/BaseCtor->isConstexpr());
        NewCtor->setAccess(BaseCtor->getAccess());

        // Build up the parameter decls and add them.
        SmallVector<ParmVarDecl *, 16> ParamDecls;
        for (unsigned i = 0; i < params; ++i) {
          ParamDecls.push_back(ParmVarDecl::Create(Context, NewCtor,
                                                   UsingLoc, UsingLoc,
                                                   /*IdentifierInfo=*/0,
                                                   BaseCtorType->getArgType(i),
                                                   /*TInfo=*/0, SC_None,
                                                   SC_None, /*DefaultArg=*/0));
        }
        NewCtor->setParams(ParamDecls);
        NewCtor->setInheritedConstructor(BaseCtor);

        PushOnScopeChains(NewCtor, S, false);
        ClassDecl->addDecl(NewCtor);
        result.first->second.second = NewCtor;
      }
    }
  }
}

Sema::ImplicitExceptionSpecification
Sema::ComputeDefaultedDtorExceptionSpec(CXXRecordDecl *ClassDecl) {
  // C++ [except.spec]p14: 
  //   An implicitly declared special member function (Clause 12) shall have 
  //   an exception-specification.
  ImplicitExceptionSpecification ExceptSpec(Context);
  if (ClassDecl->isInvalidDecl())
    return ExceptSpec;

  // Direct base-class destructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->bases_begin(),
                                       BEnd = ClassDecl->bases_end();
       B != BEnd; ++B) {
    if (B->isVirtual()) // Handled below.
      continue;
    
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>())
      ExceptSpec.CalledDecl(
                   LookupDestructor(cast<CXXRecordDecl>(BaseType->getDecl())));
  }

  // Virtual base-class destructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->vbases_begin(),
                                       BEnd = ClassDecl->vbases_end();
       B != BEnd; ++B) {
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>())
      ExceptSpec.CalledDecl(
                  LookupDestructor(cast<CXXRecordDecl>(BaseType->getDecl())));
  }

  // Field destructors.
  for (RecordDecl::field_iterator F = ClassDecl->field_begin(),
                               FEnd = ClassDecl->field_end();
       F != FEnd; ++F) {
    if (const RecordType *RecordTy
        = Context.getBaseElementType(F->getType())->getAs<RecordType>())
      ExceptSpec.CalledDecl(
                  LookupDestructor(cast<CXXRecordDecl>(RecordTy->getDecl())));
  }

  return ExceptSpec;
}

CXXDestructorDecl *Sema::DeclareImplicitDestructor(CXXRecordDecl *ClassDecl) {
  // C++ [class.dtor]p2:
  //   If a class has no user-declared destructor, a destructor is
  //   declared implicitly. An implicitly-declared destructor is an
  //   inline public member of its class.
  
  ImplicitExceptionSpecification Spec =
      ComputeDefaultedDtorExceptionSpec(ClassDecl); 
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();

  // Create the actual destructor declaration.
  QualType Ty = Context.getFunctionType(Context.VoidTy, 0, 0, EPI);

  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXDestructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXDestructorDecl *Destructor
      = CXXDestructorDecl::Create(Context, ClassDecl, ClassLoc, NameInfo, Ty, 0,
                                  /*isInline=*/true,
                                  /*isImplicitlyDeclared=*/true);
  Destructor->setAccess(AS_public);
  Destructor->setDefaulted();
  Destructor->setImplicit();
  Destructor->setTrivial(ClassDecl->hasTrivialDestructor());
  
  // Note that we have declared this destructor.
  ++ASTContext::NumImplicitDestructorsDeclared;
  
  // Introduce this destructor into its scope.
  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(Destructor, S, false);
  ClassDecl->addDecl(Destructor);
  
  // This could be uniqued if it ever proves significant.
  Destructor->setTypeSourceInfo(Context.getTrivialTypeSourceInfo(Ty));

  if (ShouldDeleteDestructor(Destructor))
    Destructor->setDeletedAsWritten();
  
  AddOverriddenMethods(ClassDecl, Destructor);
  
  return Destructor;
}

void Sema::DefineImplicitDestructor(SourceLocation CurrentLocation,
                                    CXXDestructorDecl *Destructor) {
  assert((Destructor->isDefaulted() &&
          !Destructor->doesThisDeclarationHaveABody()) &&
         "DefineImplicitDestructor - call it for implicit default dtor");
  CXXRecordDecl *ClassDecl = Destructor->getParent();
  assert(ClassDecl && "DefineImplicitDestructor - invalid destructor");

  if (Destructor->isInvalidDecl())
    return;

  ImplicitlyDefinedFunctionScope Scope(*this, Destructor);

  DiagnosticErrorTrap Trap(Diags);
  MarkBaseAndMemberDestructorsReferenced(Destructor->getLocation(),
                                         Destructor->getParent());

  if (CheckDestructor(Destructor) || Trap.hasErrorOccurred()) {
    Diag(CurrentLocation, diag::note_member_synthesized_at) 
      << CXXDestructor << Context.getTagDeclType(ClassDecl);

    Destructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Destructor->getLocation();
  Destructor->setBody(new (Context) CompoundStmt(Context, 0, 0, Loc, Loc));
  Destructor->setImplicitlyDefined(true);
  Destructor->setUsed();
  MarkVTableUsed(CurrentLocation, ClassDecl);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Destructor);
  }
}

void Sema::AdjustDestructorExceptionSpec(CXXRecordDecl *classDecl,
                                         CXXDestructorDecl *destructor) {
  // C++11 [class.dtor]p3:
  //   A declaration of a destructor that does not have an exception-
  //   specification is implicitly considered to have the same exception-
  //   specification as an implicit declaration.
  const FunctionProtoType *dtorType = destructor->getType()->
                                        getAs<FunctionProtoType>();
  if (dtorType->hasExceptionSpec())
    return;

  ImplicitExceptionSpecification exceptSpec =
      ComputeDefaultedDtorExceptionSpec(classDecl);

  // Replace the destructor's type, building off the existing one. Fortunately,
  // the only thing of interest in the destructor type is its extended info.
  // The return and arguments are fixed.
  FunctionProtoType::ExtProtoInfo epi = dtorType->getExtProtoInfo();
  epi.ExceptionSpecType = exceptSpec.getExceptionSpecType();
  epi.NumExceptions = exceptSpec.size();
  epi.Exceptions = exceptSpec.data();
  QualType ty = Context.getFunctionType(Context.VoidTy, 0, 0, epi);

  destructor->setType(ty);

  // FIXME: If the destructor has a body that could throw, and the newly created
  // spec doesn't allow exceptions, we should emit a warning, because this
  // change in behavior can break conforming C++03 programs at runtime.
  // However, we don't have a body yet, so it needs to be done somewhere else.
}

/// \brief Builds a statement that copies/moves the given entity from \p From to
/// \c To.
///
/// This routine is used to copy/move the members of a class with an
/// implicitly-declared copy/move assignment operator. When the entities being
/// copied are arrays, this routine builds for loops to copy them.
///
/// \param S The Sema object used for type-checking.
///
/// \param Loc The location where the implicit copy/move is being generated.
///
/// \param T The type of the expressions being copied/moved. Both expressions
/// must have this type.
///
/// \param To The expression we are copying/moving to.
///
/// \param From The expression we are copying/moving from.
///
/// \param CopyingBaseSubobject Whether we're copying/moving a base subobject.
/// Otherwise, it's a non-static member subobject.
///
/// \param Copying Whether we're copying or moving.
///
/// \param Depth Internal parameter recording the depth of the recursion.
///
/// \returns A statement or a loop that copies the expressions.
static StmtResult
BuildSingleCopyAssign(Sema &S, SourceLocation Loc, QualType T, 
                      Expr *To, Expr *From,
                      bool CopyingBaseSubobject, bool Copying,
                      unsigned Depth = 0) {
  // C++0x [class.copy]p28:
  //   Each subobject is assigned in the manner appropriate to its type:
  //
  //     - if the subobject is of class type, as if by a call to operator= with
  //       the subobject as the object expression and the corresponding
  //       subobject of x as a single function argument (as if by explicit
  //       qualification; that is, ignoring any possible virtual overriding
  //       functions in more derived classes);
  if (const RecordType *RecordTy = T->getAs<RecordType>()) {
    CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(RecordTy->getDecl());
    
    // Look for operator=.
    DeclarationName Name
      = S.Context.DeclarationNames.getCXXOperatorName(OO_Equal);
    LookupResult OpLookup(S, Name, Loc, Sema::LookupOrdinaryName);
    S.LookupQualifiedName(OpLookup, ClassDecl, false);
    
    // Filter out any result that isn't a copy/move-assignment operator.
    LookupResult::Filter F = OpLookup.makeFilter();
    while (F.hasNext()) {
      NamedDecl *D = F.next();
      if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D))
        if (Copying ? Method->isCopyAssignmentOperator() :
                      Method->isMoveAssignmentOperator())
          continue;

      F.erase();
    }
    F.done();
    
    // Suppress the protected check (C++ [class.protected]) for each of the
    // assignment operators we found. This strange dance is required when 
    // we're assigning via a base classes's copy-assignment operator. To
    // ensure that we're getting the right base class subobject (without 
    // ambiguities), we need to cast "this" to that subobject type; to
    // ensure that we don't go through the virtual call mechanism, we need
    // to qualify the operator= name with the base class (see below). However,
    // this means that if the base class has a protected copy assignment
    // operator, the protected member access check will fail. So, we
    // rewrite "protected" access to "public" access in this case, since we
    // know by construction that we're calling from a derived class.
    if (CopyingBaseSubobject) {
      for (LookupResult::iterator L = OpLookup.begin(), LEnd = OpLookup.end();
           L != LEnd; ++L) {
        if (L.getAccess() == AS_protected)
          L.setAccess(AS_public);
      }
    }
    
    // Create the nested-name-specifier that will be used to qualify the
    // reference to operator=; this is required to suppress the virtual
    // call mechanism.
    CXXScopeSpec SS;
    SS.MakeTrivial(S.Context, 
                   NestedNameSpecifier::Create(S.Context, 0, false, 
                                               T.getTypePtr()),
                   Loc);
    
    // Create the reference to operator=.
    ExprResult OpEqualRef
      = S.BuildMemberReferenceExpr(To, T, Loc, /*isArrow=*/false, SS, 
                                   /*FirstQualifierInScope=*/0, OpLookup, 
                                   /*TemplateArgs=*/0,
                                   /*SuppressQualifierCheck=*/true);
    if (OpEqualRef.isInvalid())
      return StmtError();
    
    // Build the call to the assignment operator.

    ExprResult Call = S.BuildCallToMemberFunction(/*Scope=*/0, 
                                                  OpEqualRef.takeAs<Expr>(),
                                                  Loc, &From, 1, Loc);
    if (Call.isInvalid())
      return StmtError();
    
    return S.Owned(Call.takeAs<Stmt>());
  }

  //     - if the subobject is of scalar type, the built-in assignment 
  //       operator is used.
  const ConstantArrayType *ArrayTy = S.Context.getAsConstantArrayType(T);  
  if (!ArrayTy) {
    ExprResult Assignment = S.CreateBuiltinBinOp(Loc, BO_Assign, To, From);
    if (Assignment.isInvalid())
      return StmtError();
    
    return S.Owned(Assignment.takeAs<Stmt>());
  }
    
  //     - if the subobject is an array, each element is assigned, in the 
  //       manner appropriate to the element type;
  
  // Construct a loop over the array bounds, e.g.,
  //
  //   for (__SIZE_TYPE__ i0 = 0; i0 != array-size; ++i0)
  //
  // that will copy each of the array elements. 
  QualType SizeType = S.Context.getSizeType();
  
  // Create the iteration variable.
  IdentifierInfo *IterationVarName = 0;
  {
    llvm::SmallString<8> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << "__i" << Depth;
    IterationVarName = &S.Context.Idents.get(OS.str());
  }
  VarDecl *IterationVar = VarDecl::Create(S.Context, S.CurContext, Loc, Loc,
                                          IterationVarName, SizeType,
                            S.Context.getTrivialTypeSourceInfo(SizeType, Loc),
                                          SC_None, SC_None);
  
  // Initialize the iteration variable to zero.
  llvm::APInt Zero(S.Context.getTypeSize(SizeType), 0);
  IterationVar->setInit(IntegerLiteral::Create(S.Context, Zero, SizeType, Loc));

  // Create a reference to the iteration variable; we'll use this several
  // times throughout.
  Expr *IterationVarRef
    = S.BuildDeclRefExpr(IterationVar, SizeType, VK_RValue, Loc).take();
  assert(IterationVarRef && "Reference to invented variable cannot fail!");
  
  // Create the DeclStmt that holds the iteration variable.
  Stmt *InitStmt = new (S.Context) DeclStmt(DeclGroupRef(IterationVar),Loc,Loc);
  
  // Create the comparison against the array bound.
  llvm::APInt Upper
    = ArrayTy->getSize().zextOrTrunc(S.Context.getTypeSize(SizeType));
  Expr *Comparison
    = new (S.Context) BinaryOperator(IterationVarRef,
                     IntegerLiteral::Create(S.Context, Upper, SizeType, Loc),
                                     BO_NE, S.Context.BoolTy,
                                     VK_RValue, OK_Ordinary, Loc);
  
  // Create the pre-increment of the iteration variable.
  Expr *Increment
    = new (S.Context) UnaryOperator(IterationVarRef, UO_PreInc, SizeType,
                                    VK_LValue, OK_Ordinary, Loc);
  
  // Subscript the "from" and "to" expressions with the iteration variable.
  From = AssertSuccess(S.CreateBuiltinArraySubscriptExpr(From, Loc,
                                                         IterationVarRef, Loc));
  To = AssertSuccess(S.CreateBuiltinArraySubscriptExpr(To, Loc,
                                                       IterationVarRef, Loc));
  if (!Copying) // Cast to rvalue
    From = CastForMoving(S, From);

  // Build the copy/move for an individual element of the array.
  StmtResult Copy = BuildSingleCopyAssign(S, Loc, ArrayTy->getElementType(),
                                          To, From, CopyingBaseSubobject,
                                          Copying, Depth + 1);
  if (Copy.isInvalid())
    return StmtError();
  
  // Construct the loop that copies all elements of this array.
  return S.ActOnForStmt(Loc, Loc, InitStmt, 
                        S.MakeFullExpr(Comparison),
                        0, S.MakeFullExpr(Increment),
                        Loc, Copy.take());
}

std::pair<Sema::ImplicitExceptionSpecification, bool>
Sema::ComputeDefaultedCopyAssignmentExceptionSpecAndConst(
                                                   CXXRecordDecl *ClassDecl) {
  if (ClassDecl->isInvalidDecl())
    return std::make_pair(ImplicitExceptionSpecification(Context), false);

  // C++ [class.copy]p10:
  //   If the class definition does not explicitly declare a copy
  //   assignment operator, one is declared implicitly.
  //   The implicitly-defined copy assignment operator for a class X
  //   will have the form
  //
  //       X& X::operator=(const X&)
  //
  //   if
  bool HasConstCopyAssignment = true;
  
  //       -- each direct base class B of X has a copy assignment operator
  //          whose parameter is of type const B&, const volatile B& or B,
  //          and
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
                                       BaseEnd = ClassDecl->bases_end();
       HasConstCopyAssignment && Base != BaseEnd; ++Base) {
    // We'll handle this below
    if (LangOpts.CPlusPlus0x && Base->isVirtual())
      continue;

    assert(!Base->getType()->isDependentType() &&
           "Cannot generate implicit members for class with dependent bases.");
    CXXRecordDecl *BaseClassDecl = Base->getType()->getAsCXXRecordDecl();
    LookupCopyingAssignment(BaseClassDecl, Qualifiers::Const, false, 0,
                            &HasConstCopyAssignment);
  }

  // In C++0x, the above citation has "or virtual added"
  if (LangOpts.CPlusPlus0x) {
    for (CXXRecordDecl::base_class_iterator Base = ClassDecl->vbases_begin(),
                                         BaseEnd = ClassDecl->vbases_end();
         HasConstCopyAssignment && Base != BaseEnd; ++Base) {
      assert(!Base->getType()->isDependentType() &&
             "Cannot generate implicit members for class with dependent bases.");
      CXXRecordDecl *BaseClassDecl = Base->getType()->getAsCXXRecordDecl();
      LookupCopyingAssignment(BaseClassDecl, Qualifiers::Const, false, 0,
                              &HasConstCopyAssignment);
    }
  }
  
  //       -- for all the nonstatic data members of X that are of a class
  //          type M (or array thereof), each such class type has a copy
  //          assignment operator whose parameter is of type const M&,
  //          const volatile M& or M.
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end();
       HasConstCopyAssignment && Field != FieldEnd;
       ++Field) {
    QualType FieldType = Context.getBaseElementType((*Field)->getType());
    if (CXXRecordDecl *FieldClassDecl = FieldType->getAsCXXRecordDecl()) {
      LookupCopyingAssignment(FieldClassDecl, Qualifiers::Const, false, 0,
                              &HasConstCopyAssignment);
    }
  }
  
  //   Otherwise, the implicitly declared copy assignment operator will
  //   have the form
  //
  //       X& X::operator=(X&)
  
  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an 
  //   exception-specification. [...]

  // It is unspecified whether or not an implicit copy assignment operator
  // attempts to deduplicate calls to assignment operators of virtual bases are
  // made. As such, this exception specification is effectively unspecified.
  // Based on a similar decision made for constness in C++0x, we're erring on
  // the side of assuming such calls to be made regardless of whether they
  // actually happen.
  ImplicitExceptionSpecification ExceptSpec(Context);
  unsigned ArgQuals = HasConstCopyAssignment ? Qualifiers::Const : 0;
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
                                       BaseEnd = ClassDecl->bases_end();
       Base != BaseEnd; ++Base) {
    if (Base->isVirtual())
      continue;

    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXMethodDecl *CopyAssign = LookupCopyingAssignment(BaseClassDecl,
                                                            ArgQuals, false, 0))
      ExceptSpec.CalledDecl(CopyAssign);
  }

  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->vbases_begin(),
                                       BaseEnd = ClassDecl->vbases_end();
       Base != BaseEnd; ++Base) {
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXMethodDecl *CopyAssign = LookupCopyingAssignment(BaseClassDecl,
                                                            ArgQuals, false, 0))
      ExceptSpec.CalledDecl(CopyAssign);
  }

  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end();
       Field != FieldEnd;
       ++Field) {
    QualType FieldType = Context.getBaseElementType((*Field)->getType());
    if (CXXRecordDecl *FieldClassDecl = FieldType->getAsCXXRecordDecl()) {
      if (CXXMethodDecl *CopyAssign =
          LookupCopyingAssignment(FieldClassDecl, ArgQuals, false, 0))
        ExceptSpec.CalledDecl(CopyAssign);
    }
  }

  return std::make_pair(ExceptSpec, HasConstCopyAssignment);
}

CXXMethodDecl *Sema::DeclareImplicitCopyAssignment(CXXRecordDecl *ClassDecl) {
  // Note: The following rules are largely analoguous to the copy
  // constructor rules. Note that virtual bases are not taken into account
  // for determining the argument type of the operator. Note also that
  // operators taking an object instead of a reference are allowed.

  ImplicitExceptionSpecification Spec(Context);
  bool Const;
  llvm::tie(Spec, Const) =
    ComputeDefaultedCopyAssignmentExceptionSpecAndConst(ClassDecl);

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  if (Const)
    ArgType = ArgType.withConst();
  ArgType = Context.getLValueReferenceType(ArgType);

  //   An implicitly-declared copy assignment operator is an inline public
  //   member of its class.
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *CopyAssignment
    = CXXMethodDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                            Context.getFunctionType(RetType, &ArgType, 1, EPI),
                            /*TInfo=*/0, /*isStatic=*/false,
                            /*StorageClassAsWritten=*/SC_None,
                            /*isInline=*/true, /*isConstexpr=*/false,
                            SourceLocation());
  CopyAssignment->setAccess(AS_public);
  CopyAssignment->setDefaulted();
  CopyAssignment->setImplicit();
  CopyAssignment->setTrivial(ClassDecl->hasTrivialCopyAssignment());
  
  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, CopyAssignment,
                                               ClassLoc, ClassLoc, /*Id=*/0,
                                               ArgType, /*TInfo=*/0,
                                               SC_None,
                                               SC_None, 0);
  CopyAssignment->setParams(FromParam);
  
  // Note that we have added this copy-assignment operator.
  ++ASTContext::NumImplicitCopyAssignmentOperatorsDeclared;

  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(CopyAssignment, S, false);
  ClassDecl->addDecl(CopyAssignment);
  
  // C++0x [class.copy]p18:
  //   ... If the class definition declares a move constructor or move
  //   assignment operator, the implicitly declared copy assignment operator is
  //   defined as deleted; ...
  if (ClassDecl->hasUserDeclaredMoveConstructor() ||
      ClassDecl->hasUserDeclaredMoveAssignment() ||
      ShouldDeleteCopyAssignmentOperator(CopyAssignment))
    CopyAssignment->setDeletedAsWritten();
  
  AddOverriddenMethods(ClassDecl, CopyAssignment);
  return CopyAssignment;
}

void Sema::DefineImplicitCopyAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *CopyAssignOperator) {
  assert((CopyAssignOperator->isDefaulted() && 
          CopyAssignOperator->isOverloadedOperator() &&
          CopyAssignOperator->getOverloadedOperator() == OO_Equal &&
          !CopyAssignOperator->doesThisDeclarationHaveABody()) &&
         "DefineImplicitCopyAssignment called for wrong function");

  CXXRecordDecl *ClassDecl = CopyAssignOperator->getParent();

  if (ClassDecl->isInvalidDecl() || CopyAssignOperator->isInvalidDecl()) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }
  
  CopyAssignOperator->setUsed();

  ImplicitlyDefinedFunctionScope Scope(*this, CopyAssignOperator);
  DiagnosticErrorTrap Trap(Diags);

  // C++0x [class.copy]p30:
  //   The implicitly-defined or explicitly-defaulted copy assignment operator
  //   for a non-union class X performs memberwise copy assignment of its 
  //   subobjects. The direct base classes of X are assigned first, in the 
  //   order of their declaration in the base-specifier-list, and then the 
  //   immediate non-static data members of X are assigned, in the order in 
  //   which they were declared in the class definition.
  
  // The statements that form the synthesized function body.
  ASTOwningVector<Stmt*> Statements(*this);
  
  // The parameter for the "other" object, which we are copying from.
  ParmVarDecl *Other = CopyAssignOperator->getParamDecl(0);
  Qualifiers OtherQuals = Other->getType().getQualifiers();
  QualType OtherRefType = Other->getType();
  if (const LValueReferenceType *OtherRef
                                = OtherRefType->getAs<LValueReferenceType>()) {
    OtherRefType = OtherRef->getPointeeType();
    OtherQuals = OtherRefType.getQualifiers();
  }
  
  // Our location for everything implicitly-generated.
  SourceLocation Loc = CopyAssignOperator->getLocation();
  
  // Construct a reference to the "other" object. We'll be using this 
  // throughout the generated ASTs.
  Expr *OtherRef = BuildDeclRefExpr(Other, OtherRefType, VK_LValue, Loc).take();
  assert(OtherRef && "Reference to parameter cannot fail!");
  
  // Construct the "this" pointer. We'll be using this throughout the generated
  // ASTs.
  Expr *This = ActOnCXXThis(Loc).takeAs<Expr>();
  assert(This && "Reference to this cannot fail!");
  
  // Assign base classes.
  bool Invalid = false;
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); Base != E; ++Base) {
    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&>(other));
    QualType BaseType = Base->getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    Expr *From = OtherRef;
    From = ImpCastExprToType(From, Context.getQualifiedType(BaseType, OtherQuals),
                             CK_UncheckedDerivedToBase,
                             VK_LValue, &BasePath).take();

    // Dereference "this".
    ExprResult To = CreateBuiltinUnaryOp(Loc, UO_Deref, This);
    
    // Implicitly cast "this" to the appropriately-qualified base type.
    To = ImpCastExprToType(To.take(), 
                           Context.getCVRQualifiedType(BaseType,
                                     CopyAssignOperator->getTypeQualifiers()),
                           CK_UncheckedDerivedToBase, 
                           VK_LValue, &BasePath);

    // Build the copy.
    StmtResult Copy = BuildSingleCopyAssign(*this, Loc, BaseType,
                                            To.get(), From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXCopyAssignment << Context.getTagDeclType(ClassDecl);
      CopyAssignOperator->setInvalidDecl();
      return;
    }
    
    // Success! Record the copy.
    Statements.push_back(Copy.takeAs<Expr>());
  }
  
  // \brief Reference to the __builtin_memcpy function.
  Expr *BuiltinMemCpyRef = 0;
  // \brief Reference to the __builtin_objc_memmove_collectable function.
  Expr *CollectableMemCpyRef = 0;
  
  // Assign non-static members.
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end(); 
       Field != FieldEnd; ++Field) {
    if (Field->isUnnamedBitfield())
      continue;
    
    // Check for members of reference type; we can't copy those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXCopyAssignment << Context.getTagDeclType(ClassDecl);
      Invalid = true;
      continue;
    }
    
    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXCopyAssignment << Context.getTagDeclType(ClassDecl);
      Invalid = true;      
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isBitField() && Field->getBitWidthValue(Context) == 0)
      continue;
    
    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() && 
             "Incomplete array type is not valid");
      continue;
    }
    
    // Build references to the field in the object we're copying from and to.
    CXXScopeSpec SS; // Intentionally empty
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(*Field);
    MemberLookup.resolveKind();
    ExprResult From = BuildMemberReferenceExpr(OtherRef, OtherRefType,
                                               Loc, /*IsArrow=*/false,
                                               SS, 0, MemberLookup, 0);
    ExprResult To = BuildMemberReferenceExpr(This, This->getType(),
                                             Loc, /*IsArrow=*/true,
                                             SS, 0, MemberLookup, 0);
    assert(!From.isInvalid() && "Implicit field reference cannot fail");
    assert(!To.isInvalid() && "Implicit field reference cannot fail");
    
    // If the field should be copied with __builtin_memcpy rather than via
    // explicit assignments, do so. This optimization only applies for arrays 
    // of scalars and arrays of class type with trivial copy-assignment 
    // operators.
    if (FieldType->isArrayType() && !FieldType.isVolatileQualified()
        && BaseType.hasTrivialAssignment(Context, /*Copying=*/true)) {
      // Compute the size of the memory buffer to be copied.
      QualType SizeType = Context.getSizeType();
      llvm::APInt Size(Context.getTypeSize(SizeType), 
                       Context.getTypeSizeInChars(BaseType).getQuantity());
      for (const ConstantArrayType *Array
              = Context.getAsConstantArrayType(FieldType);
           Array; 
           Array = Context.getAsConstantArrayType(Array->getElementType())) {
        llvm::APInt ArraySize
          = Array->getSize().zextOrTrunc(Size.getBitWidth());
        Size *= ArraySize;
      }
          
      // Take the address of the field references for "from" and "to".
      From = CreateBuiltinUnaryOp(Loc, UO_AddrOf, From.get());
      To = CreateBuiltinUnaryOp(Loc, UO_AddrOf, To.get());
          
      bool NeedsCollectableMemCpy = 
          (BaseType->isRecordType() && 
           BaseType->getAs<RecordType>()->getDecl()->hasObjectMember());
          
      if (NeedsCollectableMemCpy) {
        if (!CollectableMemCpyRef) {
          // Create a reference to the __builtin_objc_memmove_collectable function.
          LookupResult R(*this, 
                         &Context.Idents.get("__builtin_objc_memmove_collectable"), 
                         Loc, LookupOrdinaryName);
          LookupName(R, TUScope, true);
        
          FunctionDecl *CollectableMemCpy = R.getAsSingle<FunctionDecl>();
          if (!CollectableMemCpy) {
            // Something went horribly wrong earlier, and we will have 
            // complained about it.
            Invalid = true;
            continue;
          }
        
          CollectableMemCpyRef = BuildDeclRefExpr(CollectableMemCpy, 
                                                  CollectableMemCpy->getType(),
                                                  VK_LValue, Loc, 0).take();
          assert(CollectableMemCpyRef && "Builtin reference cannot fail");
        }
      }
      // Create a reference to the __builtin_memcpy builtin function.
      else if (!BuiltinMemCpyRef) {
        LookupResult R(*this, &Context.Idents.get("__builtin_memcpy"), Loc,
                       LookupOrdinaryName);
        LookupName(R, TUScope, true);
        
        FunctionDecl *BuiltinMemCpy = R.getAsSingle<FunctionDecl>();
        if (!BuiltinMemCpy) {
          // Something went horribly wrong earlier, and we will have complained
          // about it.
          Invalid = true;
          continue;
        }

        BuiltinMemCpyRef = BuildDeclRefExpr(BuiltinMemCpy, 
                                            BuiltinMemCpy->getType(),
                                            VK_LValue, Loc, 0).take();
        assert(BuiltinMemCpyRef && "Builtin reference cannot fail");
      }
          
      ASTOwningVector<Expr*> CallArgs(*this);
      CallArgs.push_back(To.takeAs<Expr>());
      CallArgs.push_back(From.takeAs<Expr>());
      CallArgs.push_back(IntegerLiteral::Create(Context, Size, SizeType, Loc));
      ExprResult Call = ExprError();
      if (NeedsCollectableMemCpy)
        Call = ActOnCallExpr(/*Scope=*/0,
                             CollectableMemCpyRef,
                             Loc, move_arg(CallArgs), 
                             Loc);
      else
        Call = ActOnCallExpr(/*Scope=*/0,
                             BuiltinMemCpyRef,
                             Loc, move_arg(CallArgs), 
                             Loc);
          
      assert(!Call.isInvalid() && "Call to __builtin_memcpy cannot fail!");
      Statements.push_back(Call.takeAs<Expr>());
      continue;
    }
    
    // Build the copy of this field.
    StmtResult Copy = BuildSingleCopyAssign(*this, Loc, FieldType, 
                                            To.get(), From.get(),
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXCopyAssignment << Context.getTagDeclType(ClassDecl);
      CopyAssignOperator->setInvalidDecl();
      return;
    }
    
    // Success! Record the copy.
    Statements.push_back(Copy.takeAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    ExprResult ThisObj = CreateBuiltinUnaryOp(Loc, UO_Deref, This);
    
    StmtResult Return = ActOnReturnStmt(Loc, ThisObj.get());
    if (Return.isInvalid())
      Invalid = true;
    else {
      Statements.push_back(Return.takeAs<Stmt>());

      if (Trap.hasErrorOccurred()) {
        Diag(CurrentLocation, diag::note_member_synthesized_at) 
          << CXXCopyAssignment << Context.getTagDeclType(ClassDecl);
        Invalid = true;
      }
    }
  }

  if (Invalid) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }
  
  StmtResult Body = ActOnCompoundStmt(Loc, Loc, move_arg(Statements),
                                            /*isStmtExpr=*/false);
  assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  CopyAssignOperator->setBody(Body.takeAs<Stmt>());

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyAssignOperator);
  }
}

Sema::ImplicitExceptionSpecification
Sema::ComputeDefaultedMoveAssignmentExceptionSpec(CXXRecordDecl *ClassDecl) {
  ImplicitExceptionSpecification ExceptSpec(Context);

  if (ClassDecl->isInvalidDecl())
    return ExceptSpec;

  // C++0x [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an 
  //   exception-specification. [...]

  // It is unspecified whether or not an implicit move assignment operator
  // attempts to deduplicate calls to assignment operators of virtual bases are
  // made. As such, this exception specification is effectively unspecified.
  // Based on a similar decision made for constness in C++0x, we're erring on
  // the side of assuming such calls to be made regardless of whether they
  // actually happen.
  // Note that a move constructor is not implicitly declared when there are
  // virtual bases, but it can still be user-declared and explicitly defaulted.
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
                                       BaseEnd = ClassDecl->bases_end();
       Base != BaseEnd; ++Base) {
    if (Base->isVirtual())
      continue;

    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXMethodDecl *MoveAssign = LookupMovingAssignment(BaseClassDecl,
                                                           false, 0))
      ExceptSpec.CalledDecl(MoveAssign);
  }

  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->vbases_begin(),
                                       BaseEnd = ClassDecl->vbases_end();
       Base != BaseEnd; ++Base) {
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXMethodDecl *MoveAssign = LookupMovingAssignment(BaseClassDecl,
                                                           false, 0))
      ExceptSpec.CalledDecl(MoveAssign);
  }

  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end();
       Field != FieldEnd;
       ++Field) {
    QualType FieldType = Context.getBaseElementType((*Field)->getType());
    if (CXXRecordDecl *FieldClassDecl = FieldType->getAsCXXRecordDecl()) {
      if (CXXMethodDecl *MoveAssign = LookupMovingAssignment(FieldClassDecl,
                                                             false, 0))
        ExceptSpec.CalledDecl(MoveAssign);
    }
  }

  return ExceptSpec;
}

CXXMethodDecl *Sema::DeclareImplicitMoveAssignment(CXXRecordDecl *ClassDecl) {
  // Note: The following rules are largely analoguous to the move
  // constructor rules.

  ImplicitExceptionSpecification Spec(
      ComputeDefaultedMoveAssignmentExceptionSpec(ClassDecl));

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  ArgType = Context.getRValueReferenceType(ArgType);

  //   An implicitly-declared move assignment operator is an inline public
  //   member of its class.
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *MoveAssignment
    = CXXMethodDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                            Context.getFunctionType(RetType, &ArgType, 1, EPI),
                            /*TInfo=*/0, /*isStatic=*/false,
                            /*StorageClassAsWritten=*/SC_None,
                            /*isInline=*/true,
                            /*isConstexpr=*/false,
                            SourceLocation());
  MoveAssignment->setAccess(AS_public);
  MoveAssignment->setDefaulted();
  MoveAssignment->setImplicit();
  MoveAssignment->setTrivial(ClassDecl->hasTrivialMoveAssignment());

  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveAssignment,
                                               ClassLoc, ClassLoc, /*Id=*/0,
                                               ArgType, /*TInfo=*/0,
                                               SC_None,
                                               SC_None, 0);
  MoveAssignment->setParams(FromParam);

  // Note that we have added this copy-assignment operator.
  ++ASTContext::NumImplicitMoveAssignmentOperatorsDeclared;

  // C++0x [class.copy]p9:
  //   If the definition of a class X does not explicitly declare a move
  //   assignment operator, one will be implicitly declared as defaulted if and
  //   only if:
  //   [...]
  //   - the move assignment operator would not be implicitly defined as
  //     deleted.
  if (ShouldDeleteMoveAssignmentOperator(MoveAssignment)) {
    // Cache this result so that we don't try to generate this over and over
    // on every lookup, leaking memory and wasting time.
    ClassDecl->setFailedImplicitMoveAssignment();
    return 0;
  }

  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(MoveAssignment, S, false);
  ClassDecl->addDecl(MoveAssignment);

  AddOverriddenMethods(ClassDecl, MoveAssignment);
  return MoveAssignment;
}

void Sema::DefineImplicitMoveAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *MoveAssignOperator) {
  assert((MoveAssignOperator->isDefaulted() && 
          MoveAssignOperator->isOverloadedOperator() &&
          MoveAssignOperator->getOverloadedOperator() == OO_Equal &&
          !MoveAssignOperator->doesThisDeclarationHaveABody()) &&
         "DefineImplicitMoveAssignment called for wrong function");

  CXXRecordDecl *ClassDecl = MoveAssignOperator->getParent();

  if (ClassDecl->isInvalidDecl() || MoveAssignOperator->isInvalidDecl()) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }
  
  MoveAssignOperator->setUsed();

  ImplicitlyDefinedFunctionScope Scope(*this, MoveAssignOperator);
  DiagnosticErrorTrap Trap(Diags);

  // C++0x [class.copy]p28:
  //   The implicitly-defined or move assignment operator for a non-union class
  //   X performs memberwise move assignment of its subobjects. The direct base
  //   classes of X are assigned first, in the order of their declaration in the
  //   base-specifier-list, and then the immediate non-static data members of X
  //   are assigned, in the order in which they were declared in the class
  //   definition.

  // The statements that form the synthesized function body.
  ASTOwningVector<Stmt*> Statements(*this);

  // The parameter for the "other" object, which we are move from.
  ParmVarDecl *Other = MoveAssignOperator->getParamDecl(0);
  QualType OtherRefType = Other->getType()->
      getAs<RValueReferenceType>()->getPointeeType();
  assert(OtherRefType.getQualifiers() == 0 &&
         "Bad argument type of defaulted move assignment");

  // Our location for everything implicitly-generated.
  SourceLocation Loc = MoveAssignOperator->getLocation();

  // Construct a reference to the "other" object. We'll be using this 
  // throughout the generated ASTs.
  Expr *OtherRef = BuildDeclRefExpr(Other, OtherRefType, VK_LValue, Loc).take();
  assert(OtherRef && "Reference to parameter cannot fail!");
  // Cast to rvalue.
  OtherRef = CastForMoving(*this, OtherRef);

  // Construct the "this" pointer. We'll be using this throughout the generated
  // ASTs.
  Expr *This = ActOnCXXThis(Loc).takeAs<Expr>();
  assert(This && "Reference to this cannot fail!");
  
  // Assign base classes.
  bool Invalid = false;
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
       E = ClassDecl->bases_end(); Base != E; ++Base) {
    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&&>(other));
    QualType BaseType = Base->getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    Expr *From = OtherRef;
    From = ImpCastExprToType(From, BaseType, CK_UncheckedDerivedToBase,
                             VK_XValue, &BasePath).take();

    // Dereference "this".
    ExprResult To = CreateBuiltinUnaryOp(Loc, UO_Deref, This);

    // Implicitly cast "this" to the appropriately-qualified base type.
    To = ImpCastExprToType(To.take(), 
                           Context.getCVRQualifiedType(BaseType,
                                     MoveAssignOperator->getTypeQualifiers()),
                           CK_UncheckedDerivedToBase, 
                           VK_LValue, &BasePath);

    // Build the move.
    StmtResult Move = BuildSingleCopyAssign(*this, Loc, BaseType,
                                            To.get(), From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXMoveAssignment << Context.getTagDeclType(ClassDecl);
      MoveAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the move.
    Statements.push_back(Move.takeAs<Expr>());
  }

  // \brief Reference to the __builtin_memcpy function.
  Expr *BuiltinMemCpyRef = 0;
  // \brief Reference to the __builtin_objc_memmove_collectable function.
  Expr *CollectableMemCpyRef = 0;

  // Assign non-static members.
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end(); 
       Field != FieldEnd; ++Field) {
    if (Field->isUnnamedBitfield())
      continue;

    // Check for members of reference type; we can't move those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXMoveAssignment << Context.getTagDeclType(ClassDecl);
      Invalid = true;
      continue;
    }

    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXMoveAssignment << Context.getTagDeclType(ClassDecl);
      Invalid = true;      
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isBitField() && Field->getBitWidthValue(Context) == 0)
      continue;
    
    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() && 
             "Incomplete array type is not valid");
      continue;
    }
    
    // Build references to the field in the object we're copying from and to.
    CXXScopeSpec SS; // Intentionally empty
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(*Field);
    MemberLookup.resolveKind();
    ExprResult From = BuildMemberReferenceExpr(OtherRef, OtherRefType,
                                               Loc, /*IsArrow=*/false,
                                               SS, 0, MemberLookup, 0);
    ExprResult To = BuildMemberReferenceExpr(This, This->getType(),
                                             Loc, /*IsArrow=*/true,
                                             SS, 0, MemberLookup, 0);
    assert(!From.isInvalid() && "Implicit field reference cannot fail");
    assert(!To.isInvalid() && "Implicit field reference cannot fail");

    assert(!From.get()->isLValue() && // could be xvalue or prvalue
        "Member reference with rvalue base must be rvalue except for reference "
        "members, which aren't allowed for move assignment.");

    // If the field should be copied with __builtin_memcpy rather than via
    // explicit assignments, do so. This optimization only applies for arrays 
    // of scalars and arrays of class type with trivial move-assignment 
    // operators.
    if (FieldType->isArrayType() && !FieldType.isVolatileQualified()
        && BaseType.hasTrivialAssignment(Context, /*Copying=*/false)) {
      // Compute the size of the memory buffer to be copied.
      QualType SizeType = Context.getSizeType();
      llvm::APInt Size(Context.getTypeSize(SizeType), 
                       Context.getTypeSizeInChars(BaseType).getQuantity());
      for (const ConstantArrayType *Array
              = Context.getAsConstantArrayType(FieldType);
           Array; 
           Array = Context.getAsConstantArrayType(Array->getElementType())) {
        llvm::APInt ArraySize
          = Array->getSize().zextOrTrunc(Size.getBitWidth());
        Size *= ArraySize;
      }

      // Take the address of the field references for "from" and "to". We
      // directly construct UnaryOperators here because semantic analysis
      // does not permit us to take the address of an xvalue.
      From = new (Context) UnaryOperator(From.get(), UO_AddrOf,
                             Context.getPointerType(From.get()->getType()),
                             VK_RValue, OK_Ordinary, Loc);
      To = new (Context) UnaryOperator(To.get(), UO_AddrOf,
                           Context.getPointerType(To.get()->getType()),
                           VK_RValue, OK_Ordinary, Loc);
          
      bool NeedsCollectableMemCpy = 
          (BaseType->isRecordType() && 
           BaseType->getAs<RecordType>()->getDecl()->hasObjectMember());
          
      if (NeedsCollectableMemCpy) {
        if (!CollectableMemCpyRef) {
          // Create a reference to the __builtin_objc_memmove_collectable function.
          LookupResult R(*this, 
                         &Context.Idents.get("__builtin_objc_memmove_collectable"), 
                         Loc, LookupOrdinaryName);
          LookupName(R, TUScope, true);
        
          FunctionDecl *CollectableMemCpy = R.getAsSingle<FunctionDecl>();
          if (!CollectableMemCpy) {
            // Something went horribly wrong earlier, and we will have 
            // complained about it.
            Invalid = true;
            continue;
          }
        
          CollectableMemCpyRef = BuildDeclRefExpr(CollectableMemCpy, 
                                                  CollectableMemCpy->getType(),
                                                  VK_LValue, Loc, 0).take();
          assert(CollectableMemCpyRef && "Builtin reference cannot fail");
        }
      }
      // Create a reference to the __builtin_memcpy builtin function.
      else if (!BuiltinMemCpyRef) {
        LookupResult R(*this, &Context.Idents.get("__builtin_memcpy"), Loc,
                       LookupOrdinaryName);
        LookupName(R, TUScope, true);
        
        FunctionDecl *BuiltinMemCpy = R.getAsSingle<FunctionDecl>();
        if (!BuiltinMemCpy) {
          // Something went horribly wrong earlier, and we will have complained
          // about it.
          Invalid = true;
          continue;
        }

        BuiltinMemCpyRef = BuildDeclRefExpr(BuiltinMemCpy, 
                                            BuiltinMemCpy->getType(),
                                            VK_LValue, Loc, 0).take();
        assert(BuiltinMemCpyRef && "Builtin reference cannot fail");
      }
          
      ASTOwningVector<Expr*> CallArgs(*this);
      CallArgs.push_back(To.takeAs<Expr>());
      CallArgs.push_back(From.takeAs<Expr>());
      CallArgs.push_back(IntegerLiteral::Create(Context, Size, SizeType, Loc));
      ExprResult Call = ExprError();
      if (NeedsCollectableMemCpy)
        Call = ActOnCallExpr(/*Scope=*/0,
                             CollectableMemCpyRef,
                             Loc, move_arg(CallArgs), 
                             Loc);
      else
        Call = ActOnCallExpr(/*Scope=*/0,
                             BuiltinMemCpyRef,
                             Loc, move_arg(CallArgs), 
                             Loc);
          
      assert(!Call.isInvalid() && "Call to __builtin_memcpy cannot fail!");
      Statements.push_back(Call.takeAs<Expr>());
      continue;
    }
    
    // Build the move of this field.
    StmtResult Move = BuildSingleCopyAssign(*this, Loc, FieldType, 
                                            To.get(), From.get(),
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      Diag(CurrentLocation, diag::note_member_synthesized_at) 
        << CXXMoveAssignment << Context.getTagDeclType(ClassDecl);
      MoveAssignOperator->setInvalidDecl();
      return;
    }
    
    // Success! Record the copy.
    Statements.push_back(Move.takeAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    ExprResult ThisObj = CreateBuiltinUnaryOp(Loc, UO_Deref, This);
    
    StmtResult Return = ActOnReturnStmt(Loc, ThisObj.get());
    if (Return.isInvalid())
      Invalid = true;
    else {
      Statements.push_back(Return.takeAs<Stmt>());

      if (Trap.hasErrorOccurred()) {
        Diag(CurrentLocation, diag::note_member_synthesized_at) 
          << CXXMoveAssignment << Context.getTagDeclType(ClassDecl);
        Invalid = true;
      }
    }
  }

  if (Invalid) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }
  
  StmtResult Body = ActOnCompoundStmt(Loc, Loc, move_arg(Statements),
                                            /*isStmtExpr=*/false);
  assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  MoveAssignOperator->setBody(Body.takeAs<Stmt>());

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveAssignOperator);
  }
}

std::pair<Sema::ImplicitExceptionSpecification, bool>
Sema::ComputeDefaultedCopyCtorExceptionSpecAndConst(CXXRecordDecl *ClassDecl) {
  if (ClassDecl->isInvalidDecl())
    return std::make_pair(ImplicitExceptionSpecification(Context), false);

  // C++ [class.copy]p5:
  //   The implicitly-declared copy constructor for a class X will
  //   have the form
  //
  //       X::X(const X&)
  //
  //   if
  // FIXME: It ought to be possible to store this on the record.
  bool HasConstCopyConstructor = true;
  
  //     -- each direct or virtual base class B of X has a copy
  //        constructor whose first parameter is of type const B& or
  //        const volatile B&, and
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
                                       BaseEnd = ClassDecl->bases_end();
       HasConstCopyConstructor && Base != BaseEnd; 
       ++Base) {
    // Virtual bases are handled below.
    if (Base->isVirtual())
      continue;
    
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    LookupCopyingConstructor(BaseClassDecl, Qualifiers::Const,
                             &HasConstCopyConstructor);
  }

  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->vbases_begin(),
                                       BaseEnd = ClassDecl->vbases_end();
       HasConstCopyConstructor && Base != BaseEnd; 
       ++Base) {
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    LookupCopyingConstructor(BaseClassDecl, Qualifiers::Const,
                             &HasConstCopyConstructor);
  }
  
  //     -- for all the nonstatic data members of X that are of a
  //        class type M (or array thereof), each such class type
  //        has a copy constructor whose first parameter is of type
  //        const M& or const volatile M&.
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end();
       HasConstCopyConstructor && Field != FieldEnd;
       ++Field) {
    QualType FieldType = Context.getBaseElementType((*Field)->getType());
    if (CXXRecordDecl *FieldClassDecl = FieldType->getAsCXXRecordDecl()) {
      LookupCopyingConstructor(FieldClassDecl, Qualifiers::Const,
                               &HasConstCopyConstructor);
    }
  }
  //   Otherwise, the implicitly declared copy constructor will have
  //   the form
  //
  //       X::X(X&)
 
  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an 
  //   exception-specification. [...]
  ImplicitExceptionSpecification ExceptSpec(Context);
  unsigned Quals = HasConstCopyConstructor? Qualifiers::Const : 0;
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->bases_begin(),
                                       BaseEnd = ClassDecl->bases_end();
       Base != BaseEnd; 
       ++Base) {
    // Virtual bases are handled below.
    if (Base->isVirtual())
      continue;
    
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXConstructorDecl *CopyConstructor =
          LookupCopyingConstructor(BaseClassDecl, Quals))
      ExceptSpec.CalledDecl(CopyConstructor);
  }
  for (CXXRecordDecl::base_class_iterator Base = ClassDecl->vbases_begin(),
                                       BaseEnd = ClassDecl->vbases_end();
       Base != BaseEnd; 
       ++Base) {
    CXXRecordDecl *BaseClassDecl
      = cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());
    if (CXXConstructorDecl *CopyConstructor =
          LookupCopyingConstructor(BaseClassDecl, Quals))
      ExceptSpec.CalledDecl(CopyConstructor);
  }
  for (CXXRecordDecl::field_iterator Field = ClassDecl->field_begin(),
                                  FieldEnd = ClassDecl->field_end();
       Field != FieldEnd;
       ++Field) {
    QualType FieldType = Context.getBaseElementType((*Field)->getType());
    if (CXXRecordDecl *FieldClassDecl = FieldType->getAsCXXRecordDecl()) {
      if (CXXConstructorDecl *CopyConstructor =
        LookupCopyingConstructor(FieldClassDecl, Quals))
      ExceptSpec.CalledDecl(CopyConstructor);
    }
  }

  return std::make_pair(ExceptSpec, HasConstCopyConstructor);
}

CXXConstructorDecl *Sema::DeclareImplicitCopyConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  // C++ [class.copy]p4:
  //   If the class definition does not explicitly declare a copy
  //   constructor, one is declared implicitly.

  ImplicitExceptionSpecification Spec(Context);
  bool Const;
  llvm::tie(Spec, Const) =
    ComputeDefaultedCopyCtorExceptionSpecAndConst(ClassDecl);

  QualType ClassType = Context.getTypeDeclType(ClassDecl);
  QualType ArgType = ClassType;
  if (Const)
    ArgType = ArgType.withConst();
  ArgType = Context.getLValueReferenceType(ArgType);
 
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  //   An implicitly-declared copy constructor is an inline public
  //   member of its class. 
  CXXConstructorDecl *CopyConstructor
    = CXXConstructorDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                                 Context.getFunctionType(Context.VoidTy,
                                                         &ArgType, 1, EPI),
                                 /*TInfo=*/0,
                                 /*isExplicit=*/false,
                                 /*isInline=*/true,
                                 /*isImplicitlyDeclared=*/true,
                                 // FIXME: apply the rules for definitions here
                                 /*isConstexpr=*/false);
  CopyConstructor->setAccess(AS_public);
  CopyConstructor->setDefaulted();
  CopyConstructor->setTrivial(ClassDecl->hasTrivialCopyConstructor());
  
  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitCopyConstructorsDeclared;
  
  // Add the parameter to the constructor.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, CopyConstructor,
                                               ClassLoc, ClassLoc,
                                               /*IdentifierInfo=*/0,
                                               ArgType, /*TInfo=*/0,
                                               SC_None,
                                               SC_None, 0);
  CopyConstructor->setParams(FromParam);

  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(CopyConstructor, S, false);
  ClassDecl->addDecl(CopyConstructor);

  // C++0x [class.copy]p7:
  //   ... If the class definition declares a move constructor or move
  //   assignment operator, the implicitly declared constructor is defined as
  //   deleted; ...
  if (ClassDecl->hasUserDeclaredMoveConstructor() ||
      ClassDecl->hasUserDeclaredMoveAssignment() ||
      ShouldDeleteSpecialMember(CopyConstructor, CXXCopyConstructor))
    CopyConstructor->setDeletedAsWritten();
  
  return CopyConstructor;
}

void Sema::DefineImplicitCopyConstructor(SourceLocation CurrentLocation,
                                   CXXConstructorDecl *CopyConstructor) {
  assert((CopyConstructor->isDefaulted() &&
          CopyConstructor->isCopyConstructor() &&
          !CopyConstructor->doesThisDeclarationHaveABody()) &&
         "DefineImplicitCopyConstructor - call it for implicit copy ctor");

  CXXRecordDecl *ClassDecl = CopyConstructor->getParent();
  assert(ClassDecl && "DefineImplicitCopyConstructor - invalid constructor");

  ImplicitlyDefinedFunctionScope Scope(*this, CopyConstructor);
  DiagnosticErrorTrap Trap(Diags);

  if (SetCtorInitializers(CopyConstructor, 0, 0, /*AnyErrors=*/false) ||
      Trap.hasErrorOccurred()) {
    Diag(CurrentLocation, diag::note_member_synthesized_at) 
      << CXXCopyConstructor << Context.getTagDeclType(ClassDecl);
    CopyConstructor->setInvalidDecl();
  }  else {
    CopyConstructor->setBody(ActOnCompoundStmt(CopyConstructor->getLocation(),
                                               CopyConstructor->getLocation(),
                                               MultiStmtArg(*this, 0, 0), 
                                               /*isStmtExpr=*/false)
                                                              .takeAs<Stmt>());
    CopyConstructor->setImplicitlyDefined(true);
  }
  
  CopyConstructor->setUsed();
  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyConstructor);
  }
}

Sema::ImplicitExceptionSpecification
Sema::ComputeDefaultedMoveCtorExceptionSpec(CXXRecordDecl *ClassDecl) {
  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an 
  //   exception-specification. [...]
  ImplicitExceptionSpecification ExceptSpec(Context);
  if (ClassDecl->isInvalidDecl())
    return ExceptSpec;

  // Direct base-class constructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->bases_begin(),
                                       BEnd = ClassDecl->bases_end();
       B != BEnd; ++B) {
    if (B->isVirtual()) // Handled below.
      continue;
    
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>()) {
      CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      CXXConstructorDecl *Constructor = LookupMovingConstructor(BaseClassDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  // Virtual base-class constructors.
  for (CXXRecordDecl::base_class_iterator B = ClassDecl->vbases_begin(),
                                       BEnd = ClassDecl->vbases_end();
       B != BEnd; ++B) {
    if (const RecordType *BaseType = B->getType()->getAs<RecordType>()) {
      CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      CXXConstructorDecl *Constructor = LookupMovingConstructor(BaseClassDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  // Field constructors.
  for (RecordDecl::field_iterator F = ClassDecl->field_begin(),
                               FEnd = ClassDecl->field_end();
       F != FEnd; ++F) {
    if (F->hasInClassInitializer()) {
      if (Expr *E = F->getInClassInitializer())
        ExceptSpec.CalledExpr(E);
      else if (!F->isInvalidDecl())
        ExceptSpec.SetDelayed();
    } else if (const RecordType *RecordTy
              = Context.getBaseElementType(F->getType())->getAs<RecordType>()) {
      CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(RecordTy->getDecl());
      CXXConstructorDecl *Constructor = LookupMovingConstructor(FieldRecDecl);
      // If this is a deleted function, add it anyway. This might be conformant
      // with the standard. This might not. I'm not sure. It might not matter.
      // In particular, the problem is that this function never gets called. It
      // might just be ill-formed because this function attempts to refer to
      // a deleted function here.
      if (Constructor)
        ExceptSpec.CalledDecl(Constructor);
    }
  }

  return ExceptSpec;
}

CXXConstructorDecl *Sema::DeclareImplicitMoveConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  ImplicitExceptionSpecification Spec(
      ComputeDefaultedMoveCtorExceptionSpec(ClassDecl));

  QualType ClassType = Context.getTypeDeclType(ClassDecl);
  QualType ArgType = Context.getRValueReferenceType(ClassType);
 
  FunctionProtoType::ExtProtoInfo EPI = Spec.getEPI();

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  // C++0x [class.copy]p11:
  //   An implicitly-declared copy/move constructor is an inline public
  //   member of its class. 
  CXXConstructorDecl *MoveConstructor
    = CXXConstructorDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                                 Context.getFunctionType(Context.VoidTy,
                                                         &ArgType, 1, EPI),
                                 /*TInfo=*/0,
                                 /*isExplicit=*/false,
                                 /*isInline=*/true,
                                 /*isImplicitlyDeclared=*/true,
                                 // FIXME: apply the rules for definitions here
                                 /*isConstexpr=*/false);
  MoveConstructor->setAccess(AS_public);
  MoveConstructor->setDefaulted();
  MoveConstructor->setTrivial(ClassDecl->hasTrivialMoveConstructor());
  
  // Add the parameter to the constructor.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveConstructor,
                                               ClassLoc, ClassLoc,
                                               /*IdentifierInfo=*/0,
                                               ArgType, /*TInfo=*/0,
                                               SC_None,
                                               SC_None, 0);
  MoveConstructor->setParams(FromParam);

  // C++0x [class.copy]p9:
  //   If the definition of a class X does not explicitly declare a move
  //   constructor, one will be implicitly declared as defaulted if and only if:
  //   [...]
  //   - the move constructor would not be implicitly defined as deleted.
  if (ShouldDeleteSpecialMember(MoveConstructor, CXXMoveConstructor)) {
    // Cache this result so that we don't try to generate this over and over
    // on every lookup, leaking memory and wasting time.
    ClassDecl->setFailedImplicitMoveConstructor();
    return 0;
  }

  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitMoveConstructorsDeclared;

  if (Scope *S = getScopeForContext(ClassDecl))
    PushOnScopeChains(MoveConstructor, S, false);
  ClassDecl->addDecl(MoveConstructor);

  return MoveConstructor;
}

void Sema::DefineImplicitMoveConstructor(SourceLocation CurrentLocation,
                                   CXXConstructorDecl *MoveConstructor) {
  assert((MoveConstructor->isDefaulted() &&
          MoveConstructor->isMoveConstructor() &&
          !MoveConstructor->doesThisDeclarationHaveABody()) &&
         "DefineImplicitMoveConstructor - call it for implicit move ctor");

  CXXRecordDecl *ClassDecl = MoveConstructor->getParent();
  assert(ClassDecl && "DefineImplicitMoveConstructor - invalid constructor");

  ImplicitlyDefinedFunctionScope Scope(*this, MoveConstructor);
  DiagnosticErrorTrap Trap(Diags);

  if (SetCtorInitializers(MoveConstructor, 0, 0, /*AnyErrors=*/false) ||
      Trap.hasErrorOccurred()) {
    Diag(CurrentLocation, diag::note_member_synthesized_at) 
      << CXXMoveConstructor << Context.getTagDeclType(ClassDecl);
    MoveConstructor->setInvalidDecl();
  }  else {
    MoveConstructor->setBody(ActOnCompoundStmt(MoveConstructor->getLocation(),
                                               MoveConstructor->getLocation(),
                                               MultiStmtArg(*this, 0, 0), 
                                               /*isStmtExpr=*/false)
                                                              .takeAs<Stmt>());
    MoveConstructor->setImplicitlyDefined(true);
  }

  MoveConstructor->setUsed();

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveConstructor);
  }
}

ExprResult
Sema::BuildCXXConstructExpr(SourceLocation ConstructLoc, QualType DeclInitType,
                            CXXConstructorDecl *Constructor,
                            MultiExprArg ExprArgs,
                            bool HadMultipleCandidates,
                            bool RequiresZeroInit,
                            unsigned ConstructKind,
                            SourceRange ParenRange) {
  bool Elidable = false;

  // C++0x [class.copy]p34:
  //   When certain criteria are met, an implementation is allowed to
  //   omit the copy/move construction of a class object, even if the
  //   copy/move constructor and/or destructor for the object have
  //   side effects. [...]
  //     - when a temporary class object that has not been bound to a
  //       reference (12.2) would be copied/moved to a class object
  //       with the same cv-unqualified type, the copy/move operation
  //       can be omitted by constructing the temporary object
  //       directly into the target of the omitted copy/move
  if (ConstructKind == CXXConstructExpr::CK_Complete &&
      Constructor->isCopyOrMoveConstructor() && ExprArgs.size() >= 1) {
    Expr *SubExpr = ((Expr **)ExprArgs.get())[0];
    Elidable = SubExpr->isTemporaryObject(Context, Constructor->getParent());
  }

  return BuildCXXConstructExpr(ConstructLoc, DeclInitType, Constructor,
                               Elidable, move(ExprArgs), HadMultipleCandidates,
                               RequiresZeroInit, ConstructKind, ParenRange);
}

/// BuildCXXConstructExpr - Creates a complete call to a constructor,
/// including handling of its default argument expressions.
ExprResult
Sema::BuildCXXConstructExpr(SourceLocation ConstructLoc, QualType DeclInitType,
                            CXXConstructorDecl *Constructor, bool Elidable,
                            MultiExprArg ExprArgs,
                            bool HadMultipleCandidates,
                            bool RequiresZeroInit,
                            unsigned ConstructKind,
                            SourceRange ParenRange) {
  unsigned NumExprs = ExprArgs.size();
  Expr **Exprs = (Expr **)ExprArgs.release();

  for (specific_attr_iterator<NonNullAttr>
           i = Constructor->specific_attr_begin<NonNullAttr>(),
           e = Constructor->specific_attr_end<NonNullAttr>(); i != e; ++i) {
    const NonNullAttr *NonNull = *i;
    CheckNonNullArguments(NonNull, ExprArgs.get(), ConstructLoc);
  }

  MarkDeclarationReferenced(ConstructLoc, Constructor);
  return Owned(CXXConstructExpr::Create(Context, DeclInitType, ConstructLoc,
                                        Constructor, Elidable, Exprs, NumExprs,
                                        HadMultipleCandidates, RequiresZeroInit,
              static_cast<CXXConstructExpr::ConstructionKind>(ConstructKind),
                                        ParenRange));
}

bool Sema::InitializeVarWithConstructor(VarDecl *VD,
                                        CXXConstructorDecl *Constructor,
                                        MultiExprArg Exprs,
                                        bool HadMultipleCandidates) {
  // FIXME: Provide the correct paren SourceRange when available.
  ExprResult TempResult =
    BuildCXXConstructExpr(VD->getLocation(), VD->getType(), Constructor,
                          move(Exprs), HadMultipleCandidates, false,
                          CXXConstructExpr::CK_Complete, SourceRange());
  if (TempResult.isInvalid())
    return true;

  Expr *Temp = TempResult.takeAs<Expr>();
  CheckImplicitConversions(Temp, VD->getLocation());
  MarkDeclarationReferenced(VD->getLocation(), Constructor);
  Temp = MaybeCreateExprWithCleanups(Temp);
  VD->setInit(Temp);

  return false;
}

void Sema::FinalizeVarWithDestructor(VarDecl *VD, const RecordType *Record) {
  if (VD->isInvalidDecl()) return;

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Record->getDecl());
  if (ClassDecl->isInvalidDecl()) return;
  if (ClassDecl->hasTrivialDestructor()) return;
  if (ClassDecl->isDependentContext()) return;

  CXXDestructorDecl *Destructor = LookupDestructor(ClassDecl);
  MarkDeclarationReferenced(VD->getLocation(), Destructor);
  CheckDestructorAccess(VD->getLocation(), Destructor,
                        PDiag(diag::err_access_dtor_var)
                        << VD->getDeclName()
                        << VD->getType());

  if (!VD->hasGlobalStorage()) return;

  // Emit warning for non-trivial dtor in global scope (a real global,
  // class-static, function-static).
  Diag(VD->getLocation(), diag::warn_exit_time_destructor);

  // TODO: this should be re-enabled for static locals by !CXAAtExit
  if (!VD->isStaticLocal())
    Diag(VD->getLocation(), diag::warn_global_destructor);
}

/// AddCXXDirectInitializerToDecl - This action is called immediately after
/// ActOnDeclarator, when a C++ direct initializer is present.
/// e.g: "int x(1);"
void Sema::AddCXXDirectInitializerToDecl(Decl *RealDecl,
                                         SourceLocation LParenLoc,
                                         MultiExprArg Exprs,
                                         SourceLocation RParenLoc,
                                         bool TypeMayContainAuto) {
  assert(Exprs.size() != 0 && Exprs.get() && "missing expressions");

  // If there is no declaration, there was an error parsing it.  Just ignore
  // the initializer.
  if (RealDecl == 0)
    return;

  VarDecl *VDecl = dyn_cast<VarDecl>(RealDecl);
  if (!VDecl) {
    Diag(RealDecl->getLocation(), diag::err_illegal_initializer);
    RealDecl->setInvalidDecl();
    return;
  }

  // C++0x [decl.spec.auto]p6. Deduce the type which 'auto' stands in for.
  if (TypeMayContainAuto && VDecl->getType()->getContainedAutoType()) {
    // FIXME: n3225 doesn't actually seem to indicate this is ill-formed
    if (Exprs.size() > 1) {
      Diag(Exprs.get()[1]->getSourceRange().getBegin(),
           diag::err_auto_var_init_multiple_expressions)
        << VDecl->getDeclName() << VDecl->getType()
        << VDecl->getSourceRange();
      RealDecl->setInvalidDecl();
      return;
    }

    Expr *Init = Exprs.get()[0];
    TypeSourceInfo *DeducedType = 0;
    if (!DeduceAutoType(VDecl->getTypeSourceInfo(), Init, DeducedType))
      Diag(VDecl->getLocation(), diag::err_auto_var_deduction_failure)
        << VDecl->getDeclName() << VDecl->getType() << Init->getType()
        << Init->getSourceRange();
    if (!DeducedType) {
      RealDecl->setInvalidDecl();
      return;
    }
    VDecl->setTypeSourceInfo(DeducedType);
    VDecl->setType(DeducedType->getType());

    // In ARC, infer lifetime.
    if (getLangOptions().ObjCAutoRefCount && inferObjCARCLifetime(VDecl))
      VDecl->setInvalidDecl();

    // If this is a redeclaration, check that the type we just deduced matches
    // the previously declared type.
    if (VarDecl *Old = VDecl->getPreviousDeclaration())
      MergeVarDeclTypes(VDecl, Old);
  }

  // We will represent direct-initialization similarly to copy-initialization:
  //    int x(1);  -as-> int x = 1;
  //    ClassType x(a,b,c); -as-> ClassType x = ClassType(a,b,c);
  //
  // Clients that want to distinguish between the two forms, can check for
  // direct initializer using VarDecl::hasCXXDirectInitializer().
  // A major benefit is that clients that don't particularly care about which
  // exactly form was it (like the CodeGen) can handle both cases without
  // special case code.

  // C++ 8.5p11:
  // The form of initialization (using parentheses or '=') is generally
  // insignificant, but does matter when the entity being initialized has a
  // class type.

  if (!VDecl->getType()->isDependentType() &&
      !VDecl->getType()->isIncompleteArrayType() && 
      RequireCompleteType(VDecl->getLocation(), VDecl->getType(),
                          diag::err_typecheck_decl_incomplete_type)) {
    VDecl->setInvalidDecl();
    return;
  }

  // The variable can not have an abstract class type.
  if (RequireNonAbstractType(VDecl->getLocation(), VDecl->getType(),
                             diag::err_abstract_type_in_decl,
                             AbstractVariableType))
    VDecl->setInvalidDecl();

  const VarDecl *Def;
  if ((Def = VDecl->getDefinition()) && Def != VDecl) {
    Diag(VDecl->getLocation(), diag::err_redefinition)
    << VDecl->getDeclName();
    Diag(Def->getLocation(), diag::note_previous_definition);
    VDecl->setInvalidDecl();
    return;
  }

  // C++ [class.static.data]p4
  //   If a static data member is of const integral or const
  //   enumeration type, its declaration in the class definition can
  //   specify a constant-initializer which shall be an integral
  //   constant expression (5.19). In that case, the member can appear
  //   in integral constant expressions. The member shall still be
  //   defined in a namespace scope if it is used in the program and the
  //   namespace scope definition shall not contain an initializer.
  //
  // We already performed a redefinition check above, but for static
  // data members we also need to check whether there was an in-class
  // declaration with an initializer.
  const VarDecl* PrevInit = 0;
  if (VDecl->isStaticDataMember() && VDecl->getAnyInitializer(PrevInit)) {
    Diag(VDecl->getLocation(), diag::err_redefinition) << VDecl->getDeclName();
    Diag(PrevInit->getLocation(), diag::note_previous_definition);
    return;
  } 

  bool IsDependent = false;
  for (unsigned I = 0, N = Exprs.size(); I != N; ++I) {
    if (DiagnoseUnexpandedParameterPack(Exprs.get()[I], UPPC_Expression)) {
      VDecl->setInvalidDecl();
      return;
    }

    if (Exprs.get()[I]->isTypeDependent())
      IsDependent = true;
  }

  // If either the declaration has a dependent type or if any of the
  // expressions is type-dependent, we represent the initialization
  // via a ParenListExpr for later use during template instantiation.
  if (VDecl->getType()->isDependentType() || IsDependent) {
    // Let clients know that initialization was done with a direct initializer.
    VDecl->setCXXDirectInitializer(true);

    // Store the initialization expressions as a ParenListExpr.
    unsigned NumExprs = Exprs.size();
    VDecl->setInit(new (Context) ParenListExpr(
        Context, LParenLoc, (Expr **)Exprs.release(), NumExprs, RParenLoc,
        VDecl->getType().getNonReferenceType()));
    return;
  }
  
  // Capture the variable that is being initialized and the style of
  // initialization.
  InitializedEntity Entity = InitializedEntity::InitializeVariable(VDecl);
  
  // FIXME: Poor source location information.
  InitializationKind Kind
    = InitializationKind::CreateDirect(VDecl->getLocation(),
                                       LParenLoc, RParenLoc);
  
  QualType T = VDecl->getType();
  InitializationSequence InitSeq(*this, Entity, Kind, 
                                 Exprs.get(), Exprs.size());
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, move(Exprs), &T);
  if (Result.isInvalid()) {
    VDecl->setInvalidDecl();
    return;
  } else if (T != VDecl->getType()) {
    VDecl->setType(T);
    Result.get()->setType(T);
  }

  
  Expr *Init = Result.get();
  CheckImplicitConversions(Init, LParenLoc);
  
  if (VDecl->isConstexpr() && !VDecl->isInvalidDecl() &&
      !Init->isValueDependent() &&
      !Init->isConstantInitializer(Context,
                                   VDecl->getType()->isReferenceType())) {
    // FIXME: Improve this diagnostic to explain why the initializer is not
    // a constant expression.
    Diag(VDecl->getLocation(), diag::err_constexpr_var_requires_const_init)
      << VDecl << Init->getSourceRange();
  }

  Init = MaybeCreateExprWithCleanups(Init);
  VDecl->setInit(Init);
  VDecl->setCXXDirectInitializer(true);

  CheckCompleteVariableDeclaration(VDecl);
}

/// \brief Given a constructor and the set of arguments provided for the
/// constructor, convert the arguments and add any required default arguments
/// to form a proper call to this constructor.
///
/// \returns true if an error occurred, false otherwise.
bool 
Sema::CompleteConstructorCall(CXXConstructorDecl *Constructor,
                              MultiExprArg ArgsPtr,
                              SourceLocation Loc,                                    
                              ASTOwningVector<Expr*> &ConvertedArgs) {
  // FIXME: This duplicates a lot of code from Sema::ConvertArgumentsForCall.
  unsigned NumArgs = ArgsPtr.size();
  Expr **Args = (Expr **)ArgsPtr.get();

  const FunctionProtoType *Proto 
    = Constructor->getType()->getAs<FunctionProtoType>();
  assert(Proto && "Constructor without a prototype?");
  unsigned NumArgsInProto = Proto->getNumArgs();
  
  // If too few arguments are available, we'll fill in the rest with defaults.
  if (NumArgs < NumArgsInProto)
    ConvertedArgs.reserve(NumArgsInProto);
  else
    ConvertedArgs.reserve(NumArgs);

  VariadicCallType CallType = 
    Proto->isVariadic() ? VariadicConstructor : VariadicDoesNotApply;
  SmallVector<Expr *, 8> AllArgs;
  bool Invalid = GatherArgumentsForCall(Loc, Constructor,
                                        Proto, 0, Args, NumArgs, AllArgs, 
                                        CallType);
  for (unsigned i =0, size = AllArgs.size(); i < size; i++)
    ConvertedArgs.push_back(AllArgs[i]);
  return Invalid;
}

static inline bool
CheckOperatorNewDeleteDeclarationScope(Sema &SemaRef, 
                                       const FunctionDecl *FnDecl) {
  const DeclContext *DC = FnDecl->getDeclContext()->getRedeclContext();
  if (isa<NamespaceDecl>(DC)) {
    return SemaRef.Diag(FnDecl->getLocation(), 
                        diag::err_operator_new_delete_declared_in_namespace)
      << FnDecl->getDeclName();
  }
  
  if (isa<TranslationUnitDecl>(DC) && 
      FnDecl->getStorageClass() == SC_Static) {
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_declared_static)
      << FnDecl->getDeclName();
  }
  
  return false;
}

static inline bool
CheckOperatorNewDeleteTypes(Sema &SemaRef, const FunctionDecl *FnDecl,
                            CanQualType ExpectedResultType,
                            CanQualType ExpectedFirstParamType,
                            unsigned DependentParamTypeDiag,
                            unsigned InvalidParamTypeDiag) {
  QualType ResultType = 
    FnDecl->getType()->getAs<FunctionType>()->getResultType();

  // Check that the result type is not dependent.
  if (ResultType->isDependentType())
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_dependent_result_type)
    << FnDecl->getDeclName() << ExpectedResultType;

  // Check that the result type is what we expect.
  if (SemaRef.Context.getCanonicalType(ResultType) != ExpectedResultType)
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_invalid_result_type) 
    << FnDecl->getDeclName() << ExpectedResultType;
  
  // A function template must have at least 2 parameters.
  if (FnDecl->getDescribedFunctionTemplate() && FnDecl->getNumParams() < 2)
    return SemaRef.Diag(FnDecl->getLocation(),
                      diag::err_operator_new_delete_template_too_few_parameters)
        << FnDecl->getDeclName();
  
  // The function decl must have at least 1 parameter.
  if (FnDecl->getNumParams() == 0)
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_too_few_parameters)
      << FnDecl->getDeclName();
 
  // Check the the first parameter type is not dependent.
  QualType FirstParamType = FnDecl->getParamDecl(0)->getType();
  if (FirstParamType->isDependentType())
    return SemaRef.Diag(FnDecl->getLocation(), DependentParamTypeDiag)
      << FnDecl->getDeclName() << ExpectedFirstParamType;

  // Check that the first parameter type is what we expect.
  if (SemaRef.Context.getCanonicalType(FirstParamType).getUnqualifiedType() != 
      ExpectedFirstParamType)
    return SemaRef.Diag(FnDecl->getLocation(), InvalidParamTypeDiag)
    << FnDecl->getDeclName() << ExpectedFirstParamType;
  
  return false;
}

static bool
CheckOperatorNewDeclaration(Sema &SemaRef, const FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.allocation]p1:
  //   A program is ill-formed if an allocation function is declared in a
  //   namespace scope other than global scope or declared static in global 
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  CanQualType SizeTy = 
    SemaRef.Context.getCanonicalType(SemaRef.Context.getSizeType());

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The return type shall be void*. The first parameter shall have type 
  //  std::size_t.
  if (CheckOperatorNewDeleteTypes(SemaRef, FnDecl, SemaRef.Context.VoidPtrTy, 
                                  SizeTy,
                                  diag::err_operator_new_dependent_param_type,
                                  diag::err_operator_new_param_type))
    return true;

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The first parameter shall not have an associated default argument.
  if (FnDecl->getParamDecl(0)->hasDefaultArg())
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_default_arg)
      << FnDecl->getDeclName() << FnDecl->getParamDecl(0)->getDefaultArgRange();

  return false;
}

static bool
CheckOperatorDeleteDeclaration(Sema &SemaRef, const FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.deallocation]p1:
  //   A program is ill-formed if deallocation functions are declared in a
  //   namespace scope other than global scope or declared static in global 
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  // C++ [basic.stc.dynamic.deallocation]p2:
  //   Each deallocation function shall return void and its first parameter 
  //   shall be void*.
  if (CheckOperatorNewDeleteTypes(SemaRef, FnDecl, SemaRef.Context.VoidTy, 
                                  SemaRef.Context.VoidPtrTy,
                                 diag::err_operator_delete_dependent_param_type,
                                 diag::err_operator_delete_param_type))
    return true;

  return false;
}

/// CheckOverloadedOperatorDeclaration - Check whether the declaration
/// of this overloaded operator is well-formed. If so, returns false;
/// otherwise, emits appropriate diagnostics and returns true.
bool Sema::CheckOverloadedOperatorDeclaration(FunctionDecl *FnDecl) {
  assert(FnDecl && FnDecl->isOverloadedOperator() &&
         "Expected an overloaded operator declaration");

  OverloadedOperatorKind Op = FnDecl->getOverloadedOperator();

  // C++ [over.oper]p5:
  //   The allocation and deallocation functions, operator new,
  //   operator new[], operator delete and operator delete[], are
  //   described completely in 3.7.3. The attributes and restrictions
  //   found in the rest of this subclause do not apply to them unless
  //   explicitly stated in 3.7.3.
  if (Op == OO_Delete || Op == OO_Array_Delete)
    return CheckOperatorDeleteDeclaration(*this, FnDecl);
  
  if (Op == OO_New || Op == OO_Array_New)
    return CheckOperatorNewDeclaration(*this, FnDecl);

  // C++ [over.oper]p6:
  //   An operator function shall either be a non-static member
  //   function or be a non-member function and have at least one
  //   parameter whose type is a class, a reference to a class, an
  //   enumeration, or a reference to an enumeration.
  if (CXXMethodDecl *MethodDecl = dyn_cast<CXXMethodDecl>(FnDecl)) {
    if (MethodDecl->isStatic())
      return Diag(FnDecl->getLocation(),
                  diag::err_operator_overload_static) << FnDecl->getDeclName();
  } else {
    bool ClassOrEnumParam = false;
    for (FunctionDecl::param_iterator Param = FnDecl->param_begin(),
                                   ParamEnd = FnDecl->param_end();
         Param != ParamEnd; ++Param) {
      QualType ParamType = (*Param)->getType().getNonReferenceType();
      if (ParamType->isDependentType() || ParamType->isRecordType() ||
          ParamType->isEnumeralType()) {
        ClassOrEnumParam = true;
        break;
      }
    }

    if (!ClassOrEnumParam)
      return Diag(FnDecl->getLocation(),
                  diag::err_operator_overload_needs_class_or_enum)
        << FnDecl->getDeclName();
  }

  // C++ [over.oper]p8:
  //   An operator function cannot have default arguments (8.3.6),
  //   except where explicitly stated below.
  //
  // Only the function-call operator allows default arguments
  // (C++ [over.call]p1).
  if (Op != OO_Call) {
    for (FunctionDecl::param_iterator Param = FnDecl->param_begin();
         Param != FnDecl->param_end(); ++Param) {
      if ((*Param)->hasDefaultArg())
        return Diag((*Param)->getLocation(),
                    diag::err_operator_overload_default_arg)
          << FnDecl->getDeclName() << (*Param)->getDefaultArgRange();
    }
  }

  static const bool OperatorUses[NUM_OVERLOADED_OPERATORS][3] = {
    { false, false, false }
#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
    , { Unary, Binary, MemberOnly }
#include "clang/Basic/OperatorKinds.def"
  };

  bool CanBeUnaryOperator = OperatorUses[Op][0];
  bool CanBeBinaryOperator = OperatorUses[Op][1];
  bool MustBeMemberOperator = OperatorUses[Op][2];

  // C++ [over.oper]p8:
  //   [...] Operator functions cannot have more or fewer parameters
  //   than the number required for the corresponding operator, as
  //   described in the rest of this subclause.
  unsigned NumParams = FnDecl->getNumParams()
                     + (isa<CXXMethodDecl>(FnDecl)? 1 : 0);
  if (Op != OO_Call &&
      ((NumParams == 1 && !CanBeUnaryOperator) ||
       (NumParams == 2 && !CanBeBinaryOperator) ||
       (NumParams < 1) || (NumParams > 2))) {
    // We have the wrong number of parameters.
    unsigned ErrorKind;
    if (CanBeUnaryOperator && CanBeBinaryOperator) {
      ErrorKind = 2;  // 2 -> unary or binary.
    } else if (CanBeUnaryOperator) {
      ErrorKind = 0;  // 0 -> unary
    } else {
      assert(CanBeBinaryOperator &&
             "All non-call overloaded operators are unary or binary!");
      ErrorKind = 1;  // 1 -> binary
    }

    return Diag(FnDecl->getLocation(), diag::err_operator_overload_must_be)
      << FnDecl->getDeclName() << NumParams << ErrorKind;
  }

  // Overloaded operators other than operator() cannot be variadic.
  if (Op != OO_Call &&
      FnDecl->getType()->getAs<FunctionProtoType>()->isVariadic()) {
    return Diag(FnDecl->getLocation(), diag::err_operator_overload_variadic)
      << FnDecl->getDeclName();
  }

  // Some operators must be non-static member functions.
  if (MustBeMemberOperator && !isa<CXXMethodDecl>(FnDecl)) {
    return Diag(FnDecl->getLocation(),
                diag::err_operator_overload_must_be_member)
      << FnDecl->getDeclName();
  }

  // C++ [over.inc]p1:
  //   The user-defined function called operator++ implements the
  //   prefix and postfix ++ operator. If this function is a member
  //   function with no parameters, or a non-member function with one
  //   parameter of class or enumeration type, it defines the prefix
  //   increment operator ++ for objects of that type. If the function
  //   is a member function with one parameter (which shall be of type
  //   int) or a non-member function with two parameters (the second
  //   of which shall be of type int), it defines the postfix
  //   increment operator ++ for objects of that type.
  if ((Op == OO_PlusPlus || Op == OO_MinusMinus) && NumParams == 2) {
    ParmVarDecl *LastParam = FnDecl->getParamDecl(FnDecl->getNumParams() - 1);
    bool ParamIsInt = false;
    if (const BuiltinType *BT = LastParam->getType()->getAs<BuiltinType>())
      ParamIsInt = BT->getKind() == BuiltinType::Int;

    if (!ParamIsInt)
      return Diag(LastParam->getLocation(),
                  diag::err_operator_overload_post_incdec_must_be_int)
        << LastParam->getType() << (Op == OO_MinusMinus);
  }

  return false;
}

/// CheckLiteralOperatorDeclaration - Check whether the declaration
/// of this literal operator function is well-formed. If so, returns
/// false; otherwise, emits appropriate diagnostics and returns true.
bool Sema::CheckLiteralOperatorDeclaration(FunctionDecl *FnDecl) {
  DeclContext *DC = FnDecl->getDeclContext();
  Decl::Kind Kind = DC->getDeclKind();
  if (Kind != Decl::TranslationUnit && Kind != Decl::Namespace &&
      Kind != Decl::LinkageSpec) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_outside_namespace)
      << FnDecl->getDeclName();
    return true;
  }

  bool Valid = false;

  // template <char...> type operator "" name() is the only valid template
  // signature, and the only valid signature with no parameters.
  if (FnDecl->param_size() == 0) {
    if (FunctionTemplateDecl *TpDecl = FnDecl->getDescribedFunctionTemplate()) {
      // Must have only one template parameter
      TemplateParameterList *Params = TpDecl->getTemplateParameters();
      if (Params->size() == 1) {
        NonTypeTemplateParmDecl *PmDecl =
          cast<NonTypeTemplateParmDecl>(Params->getParam(0));

        // The template parameter must be a char parameter pack.
        if (PmDecl && PmDecl->isTemplateParameterPack() &&
            Context.hasSameType(PmDecl->getType(), Context.CharTy))
          Valid = true;
      }
    }
  } else {
    // Check the first parameter
    FunctionDecl::param_iterator Param = FnDecl->param_begin();

    QualType T = (*Param)->getType();

    // unsigned long long int, long double, and any character type are allowed
    // as the only parameters.
    if (Context.hasSameType(T, Context.UnsignedLongLongTy) ||
        Context.hasSameType(T, Context.LongDoubleTy) ||
        Context.hasSameType(T, Context.CharTy) ||
        Context.hasSameType(T, Context.WCharTy) ||
        Context.hasSameType(T, Context.Char16Ty) ||
        Context.hasSameType(T, Context.Char32Ty)) {
      if (++Param == FnDecl->param_end())
        Valid = true;
      goto FinishedParams;
    }

    // Otherwise it must be a pointer to const; let's strip those qualifiers.
    const PointerType *PT = T->getAs<PointerType>();
    if (!PT)
      goto FinishedParams;
    T = PT->getPointeeType();
    if (!T.isConstQualified())
      goto FinishedParams;
    T = T.getUnqualifiedType();

    // Move on to the second parameter;
    ++Param;

    // If there is no second parameter, the first must be a const char *
    if (Param == FnDecl->param_end()) {
      if (Context.hasSameType(T, Context.CharTy))
        Valid = true;
      goto FinishedParams;
    }

    // const char *, const wchar_t*, const char16_t*, and const char32_t*
    // are allowed as the first parameter to a two-parameter function
    if (!(Context.hasSameType(T, Context.CharTy) ||
          Context.hasSameType(T, Context.WCharTy) ||
          Context.hasSameType(T, Context.Char16Ty) ||
          Context.hasSameType(T, Context.Char32Ty)))
      goto FinishedParams;

    // The second and final parameter must be an std::size_t
    T = (*Param)->getType().getUnqualifiedType();
    if (Context.hasSameType(T, Context.getSizeType()) &&
        ++Param == FnDecl->param_end())
      Valid = true;
  }

  // FIXME: This diagnostic is absolutely terrible.
FinishedParams:
  if (!Valid) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_params)
      << FnDecl->getDeclName();
    return true;
  }

  StringRef LiteralName 
    = FnDecl->getDeclName().getCXXLiteralIdentifier()->getName();
  if (LiteralName[0] != '_') {
    // C++0x [usrlit.suffix]p1:
    //   Literal suffix identifiers that do not start with an underscore are 
    //   reserved for future standardization.
    bool IsHexFloat = true;
    if (LiteralName.size() > 1 && 
        (LiteralName[0] == 'P' || LiteralName[0] == 'p')) {
      for (unsigned I = 1, N = LiteralName.size(); I < N; ++I) {
        if (!isdigit(LiteralName[I])) {
          IsHexFloat = false;
          break;
        }
      }
    }
    
    if (IsHexFloat)
      Diag(FnDecl->getLocation(), diag::warn_user_literal_hexfloat)
        << LiteralName;
    else
      Diag(FnDecl->getLocation(), diag::warn_user_literal_reserved);
  }
  
  return false;
}

/// ActOnStartLinkageSpecification - Parsed the beginning of a C++
/// linkage specification, including the language and (if present)
/// the '{'. ExternLoc is the location of the 'extern', LangLoc is
/// the location of the language string literal, which is provided
/// by Lang/StrSize. LBraceLoc, if valid, provides the location of
/// the '{' brace. Otherwise, this linkage specification does not
/// have any braces.
Decl *Sema::ActOnStartLinkageSpecification(Scope *S, SourceLocation ExternLoc,
                                           SourceLocation LangLoc,
                                           StringRef Lang,
                                           SourceLocation LBraceLoc) {
  LinkageSpecDecl::LanguageIDs Language;
  if (Lang == "\"C\"")
    Language = LinkageSpecDecl::lang_c;
  else if (Lang == "\"C++\"")
    Language = LinkageSpecDecl::lang_cxx;
  else {
    Diag(LangLoc, diag::err_bad_language);
    return 0;
  }

  // FIXME: Add all the various semantics of linkage specifications

  LinkageSpecDecl *D = LinkageSpecDecl::Create(Context, CurContext,
                                               ExternLoc, LangLoc, Language);
  CurContext->addDecl(D);
  PushDeclContext(S, D);
  return D;
}

/// ActOnFinishLinkageSpecification - Complete the definition of
/// the C++ linkage specification LinkageSpec. If RBraceLoc is
/// valid, it's the position of the closing '}' brace in a linkage
/// specification that uses braces.
Decl *Sema::ActOnFinishLinkageSpecification(Scope *S,
                                            Decl *LinkageSpec,
                                            SourceLocation RBraceLoc) {
  if (LinkageSpec) {
    if (RBraceLoc.isValid()) {
      LinkageSpecDecl* LSDecl = cast<LinkageSpecDecl>(LinkageSpec);
      LSDecl->setRBraceLoc(RBraceLoc);
    }
    PopDeclContext();
  }
  return LinkageSpec;
}

/// \brief Perform semantic analysis for the variable declaration that
/// occurs within a C++ catch clause, returning the newly-created
/// variable.
VarDecl *Sema::BuildExceptionDeclaration(Scope *S,
                                         TypeSourceInfo *TInfo,
                                         SourceLocation StartLoc,
                                         SourceLocation Loc,
                                         IdentifierInfo *Name) {
  bool Invalid = false;
  QualType ExDeclType = TInfo->getType();
  
  // Arrays and functions decay.
  if (ExDeclType->isArrayType())
    ExDeclType = Context.getArrayDecayedType(ExDeclType);
  else if (ExDeclType->isFunctionType())
    ExDeclType = Context.getPointerType(ExDeclType);

  // C++ 15.3p1: The exception-declaration shall not denote an incomplete type.
  // The exception-declaration shall not denote a pointer or reference to an
  // incomplete type, other than [cv] void*.
  // N2844 forbids rvalue references.
  if (!ExDeclType->isDependentType() && ExDeclType->isRValueReferenceType()) {
    Diag(Loc, diag::err_catch_rvalue_ref);
    Invalid = true;
  }

  // GCC allows catching pointers and references to incomplete types
  // as an extension; so do we, but we warn by default.

  QualType BaseType = ExDeclType;
  int Mode = 0; // 0 for direct type, 1 for pointer, 2 for reference
  unsigned DK = diag::err_catch_incomplete;
  bool IncompleteCatchIsInvalid = true;
  if (const PointerType *Ptr = BaseType->getAs<PointerType>()) {
    BaseType = Ptr->getPointeeType();
    Mode = 1;
    DK = diag::ext_catch_incomplete_ptr;
    IncompleteCatchIsInvalid = false;
  } else if (const ReferenceType *Ref = BaseType->getAs<ReferenceType>()) {
    // For the purpose of error recovery, we treat rvalue refs like lvalue refs.
    BaseType = Ref->getPointeeType();
    Mode = 2;
    DK = diag::ext_catch_incomplete_ref;
    IncompleteCatchIsInvalid = false;
  }
  if (!Invalid && (Mode == 0 || !BaseType->isVoidType()) &&
      !BaseType->isDependentType() && RequireCompleteType(Loc, BaseType, DK) &&
      IncompleteCatchIsInvalid)
    Invalid = true;

  if (!Invalid && !ExDeclType->isDependentType() &&
      RequireNonAbstractType(Loc, ExDeclType,
                             diag::err_abstract_type_in_decl,
                             AbstractVariableType))
    Invalid = true;

  // Only the non-fragile NeXT runtime currently supports C++ catches
  // of ObjC types, and no runtime supports catching ObjC types by value.
  if (!Invalid && getLangOptions().ObjC1) {
    QualType T = ExDeclType;
    if (const ReferenceType *RT = T->getAs<ReferenceType>())
      T = RT->getPointeeType();

    if (T->isObjCObjectType()) {
      Diag(Loc, diag::err_objc_object_catch);
      Invalid = true;
    } else if (T->isObjCObjectPointerType()) {
      if (!getLangOptions().ObjCNonFragileABI)
        Diag(Loc, diag::warn_objc_pointer_cxx_catch_fragile);
    }
  }

  VarDecl *ExDecl = VarDecl::Create(Context, CurContext, StartLoc, Loc, Name,
                                    ExDeclType, TInfo, SC_None, SC_None);
  ExDecl->setExceptionVariable(true);
  
  if (!Invalid && !ExDeclType->isDependentType()) {
    if (const RecordType *recordType = ExDeclType->getAs<RecordType>()) {
      // C++ [except.handle]p16:
      //   The object declared in an exception-declaration or, if the 
      //   exception-declaration does not specify a name, a temporary (12.2) is 
      //   copy-initialized (8.5) from the exception object. [...]
      //   The object is destroyed when the handler exits, after the destruction
      //   of any automatic objects initialized within the handler.
      //
      // We just pretend to initialize the object with itself, then make sure 
      // it can be destroyed later.
      QualType initType = ExDeclType;

      InitializedEntity entity =
        InitializedEntity::InitializeVariable(ExDecl);
      InitializationKind initKind =
        InitializationKind::CreateCopy(Loc, SourceLocation());

      Expr *opaqueValue =
        new (Context) OpaqueValueExpr(Loc, initType, VK_LValue, OK_Ordinary);
      InitializationSequence sequence(*this, entity, initKind, &opaqueValue, 1);
      ExprResult result = sequence.Perform(*this, entity, initKind,
                                           MultiExprArg(&opaqueValue, 1));
      if (result.isInvalid())
        Invalid = true;
      else {
        // If the constructor used was non-trivial, set this as the
        // "initializer".
        CXXConstructExpr *construct = cast<CXXConstructExpr>(result.take());
        if (!construct->getConstructor()->isTrivial()) {
          Expr *init = MaybeCreateExprWithCleanups(construct);
          ExDecl->setInit(init);
        }
        
        // And make sure it's destructable.
        FinalizeVarWithDestructor(ExDecl, recordType);
      }
    }
  }
  
  if (Invalid)
    ExDecl->setInvalidDecl();

  return ExDecl;
}

/// ActOnExceptionDeclarator - Parsed the exception-declarator in a C++ catch
/// handler.
Decl *Sema::ActOnExceptionDeclarator(Scope *S, Declarator &D) {
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);
  bool Invalid = D.isInvalidType();

  // Check for unexpanded parameter packs.
  if (TInfo && DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                               UPPC_ExceptionType)) {
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy, 
                                             D.getIdentifierLoc());
    Invalid = true;
  }

  IdentifierInfo *II = D.getIdentifier();
  if (NamedDecl *PrevDecl = LookupSingleName(S, II, D.getIdentifierLoc(),
                                             LookupOrdinaryName,
                                             ForRedeclaration)) {
    // The scope should be freshly made just for us. There is just no way
    // it contains any previous declaration.
    assert(!S->isDeclScope(PrevDecl));
    if (PrevDecl->isTemplateParameter()) {
      // Maybe we will complain about the shadowed template parameter.
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
    }
  }

  if (D.getCXXScopeSpec().isSet() && !Invalid) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_catch_declarator)
      << D.getCXXScopeSpec().getRange();
    Invalid = true;
  }

  VarDecl *ExDecl = BuildExceptionDeclaration(S, TInfo,
                                              D.getSourceRange().getBegin(),
                                              D.getIdentifierLoc(),
                                              D.getIdentifier());
  if (Invalid)
    ExDecl->setInvalidDecl();

  // Add the exception declaration into this scope.
  if (II)
    PushOnScopeChains(ExDecl, S);
  else
    CurContext->addDecl(ExDecl);

  ProcessDeclAttributes(S, ExDecl, D);
  return ExDecl;
}

Decl *Sema::ActOnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                         Expr *AssertExpr,
                                         Expr *AssertMessageExpr_,
                                         SourceLocation RParenLoc) {
  StringLiteral *AssertMessage = cast<StringLiteral>(AssertMessageExpr_);

  if (!AssertExpr->isTypeDependent() && !AssertExpr->isValueDependent()) {
    llvm::APSInt Value(32);
    if (!AssertExpr->isIntegerConstantExpr(Value, Context)) {
      Diag(StaticAssertLoc,
           diag::err_static_assert_expression_is_not_constant) <<
        AssertExpr->getSourceRange();
      return 0;
    }

    if (Value == 0) {
      Diag(StaticAssertLoc, diag::err_static_assert_failed)
        << AssertMessage->getString() << AssertExpr->getSourceRange();
    }
  }

  if (DiagnoseUnexpandedParameterPack(AssertExpr, UPPC_StaticAssertExpression))
    return 0;

  Decl *Decl = StaticAssertDecl::Create(Context, CurContext, StaticAssertLoc,
                                        AssertExpr, AssertMessage, RParenLoc);

  CurContext->addDecl(Decl);
  return Decl;
}

/// \brief Perform semantic analysis of the given friend type declaration.
///
/// \returns A friend declaration that.
FriendDecl *Sema::CheckFriendTypeDecl(SourceLocation FriendLoc, 
                                      TypeSourceInfo *TSInfo) {
  assert(TSInfo && "NULL TypeSourceInfo for friend type declaration");
  
  QualType T = TSInfo->getType();
  SourceRange TypeRange = TSInfo->getTypeLoc().getLocalSourceRange();
  
  if (!getLangOptions().CPlusPlus0x) {
    // C++03 [class.friend]p2:
    //   An elaborated-type-specifier shall be used in a friend declaration
    //   for a class.*
    //
    //   * The class-key of the elaborated-type-specifier is required.
    if (!ActiveTemplateInstantiations.empty()) {
      // Do not complain about the form of friend template types during
      // template instantiation; we will already have complained when the
      // template was declared.
    } else if (!T->isElaboratedTypeSpecifier()) {
      // If we evaluated the type to a record type, suggest putting
      // a tag in front.
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RecordDecl *RD = RT->getDecl();
        
        std::string InsertionText = std::string(" ") + RD->getKindName();
        
        Diag(TypeRange.getBegin(), diag::ext_unelaborated_friend_type)
          << (unsigned) RD->getTagKind()
          << T
          << FixItHint::CreateInsertion(PP.getLocForEndOfToken(FriendLoc),
                                        InsertionText);
      } else {
        Diag(FriendLoc, diag::ext_nonclass_type_friend)
          << T
          << SourceRange(FriendLoc, TypeRange.getEnd());
      }
    } else if (T->getAs<EnumType>()) {
      Diag(FriendLoc, diag::ext_enum_friend)
        << T
        << SourceRange(FriendLoc, TypeRange.getEnd());
    }
  }
  
  // C++0x [class.friend]p3:
  //   If the type specifier in a friend declaration designates a (possibly
  //   cv-qualified) class type, that class is declared as a friend; otherwise, 
  //   the friend declaration is ignored.
  
  // FIXME: C++0x has some syntactic restrictions on friend type declarations
  // in [class.friend]p3 that we do not implement.
  
  return FriendDecl::Create(Context, CurContext, FriendLoc, TSInfo, FriendLoc);
}

/// Handle a friend tag declaration where the scope specifier was
/// templated.
Decl *Sema::ActOnTemplatedFriendTag(Scope *S, SourceLocation FriendLoc,
                                    unsigned TagSpec, SourceLocation TagLoc,
                                    CXXScopeSpec &SS,
                                    IdentifierInfo *Name, SourceLocation NameLoc,
                                    AttributeList *Attr,
                                    MultiTemplateParamsArg TempParamLists) {
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  bool isExplicitSpecialization = false;
  bool Invalid = false;

  if (TemplateParameterList *TemplateParams
        = MatchTemplateParametersToScopeSpecifier(TagLoc, NameLoc, SS,
                                                  TempParamLists.get(),
                                                  TempParamLists.size(),
                                                  /*friend*/ true,
                                                  isExplicitSpecialization,
                                                  Invalid)) {
    if (TemplateParams->size() > 0) {
      // This is a declaration of a class template.
      if (Invalid)
        return 0;

      return CheckClassTemplate(S, TagSpec, TUK_Friend, TagLoc,
                                SS, Name, NameLoc, Attr,
                                TemplateParams, AS_public,
                                /*ModulePrivateLoc=*/SourceLocation(),
                                TempParamLists.size() - 1,
                   (TemplateParameterList**) TempParamLists.release()).take();
    } else {
      // The "template<>" header is extraneous.
      Diag(TemplateParams->getTemplateLoc(), diag::err_template_tag_noparams)
        << TypeWithKeyword::getTagTypeKindName(Kind) << Name;
      isExplicitSpecialization = true;
    }
  }

  if (Invalid) return 0;

  assert(SS.isNotEmpty() && "valid templated tag with no SS and no direct?");

  bool isAllExplicitSpecializations = true;
  for (unsigned I = TempParamLists.size(); I-- > 0; ) {
    if (TempParamLists.get()[I]->size()) {
      isAllExplicitSpecializations = false;
      break;
    }
  }

  // FIXME: don't ignore attributes.

  // If it's explicit specializations all the way down, just forget
  // about the template header and build an appropriate non-templated
  // friend.  TODO: for source fidelity, remember the headers.
  if (isAllExplicitSpecializations) {
    NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
    ElaboratedTypeKeyword Keyword
      = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
    QualType T = CheckTypenameType(Keyword, TagLoc, QualifierLoc,
                                   *Name, NameLoc);
    if (T.isNull())
      return 0;

    TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
    if (isa<DependentNameType>(T)) {
      DependentNameTypeLoc TL = cast<DependentNameTypeLoc>(TSI->getTypeLoc());
      TL.setKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      TL.setNameLoc(NameLoc);
    } else {
      ElaboratedTypeLoc TL = cast<ElaboratedTypeLoc>(TSI->getTypeLoc());
      TL.setKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      cast<TypeSpecTypeLoc>(TL.getNamedTypeLoc()).setNameLoc(NameLoc);
    }

    FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                            TSI, FriendLoc);
    Friend->setAccess(AS_public);
    CurContext->addDecl(Friend);
    return Friend;
  }

  // Handle the case of a templated-scope friend class.  e.g.
  //   template <class T> class A<T>::B;
  // FIXME: we don't support these right now.
  ElaboratedTypeKeyword ETK = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
  QualType T = Context.getDependentNameType(ETK, SS.getScopeRep(), Name);
  TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
  DependentNameTypeLoc TL = cast<DependentNameTypeLoc>(TSI->getTypeLoc());
  TL.setKeywordLoc(TagLoc);
  TL.setQualifierLoc(SS.getWithLocInContext(Context));
  TL.setNameLoc(NameLoc);

  FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                          TSI, FriendLoc);
  Friend->setAccess(AS_public);
  Friend->setUnsupportedFriend(true);
  CurContext->addDecl(Friend);
  return Friend;
}


/// Handle a friend type declaration.  This works in tandem with
/// ActOnTag.
///
/// Notes on friend class templates:
///
/// We generally treat friend class declarations as if they were
/// declaring a class.  So, for example, the elaborated type specifier
/// in a friend declaration is required to obey the restrictions of a
/// class-head (i.e. no typedefs in the scope chain), template
/// parameters are required to match up with simple template-ids, &c.
/// However, unlike when declaring a template specialization, it's
/// okay to refer to a template specialization without an empty
/// template parameter declaration, e.g.
///   friend class A<T>::B<unsigned>;
/// We permit this as a special case; if there are any template
/// parameters present at all, require proper matching, i.e.
///   template <> template <class T> friend class A<int>::B;
Decl *Sema::ActOnFriendTypeDecl(Scope *S, const DeclSpec &DS,
                                MultiTemplateParamsArg TempParams) {
  SourceLocation Loc = DS.getSourceRange().getBegin();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  // Try to convert the decl specifier to a type.  This works for
  // friend templates because ActOnTag never produces a ClassTemplateDecl
  // for a TUK_Friend.
  Declarator TheDeclarator(DS, Declarator::MemberContext);
  TypeSourceInfo *TSI = GetTypeForDeclarator(TheDeclarator, S);
  QualType T = TSI->getType();
  if (TheDeclarator.isInvalidType())
    return 0;

  if (DiagnoseUnexpandedParameterPack(Loc, TSI, UPPC_FriendDeclaration))
    return 0;

  // This is definitely an error in C++98.  It's probably meant to
  // be forbidden in C++0x, too, but the specification is just
  // poorly written.
  //
  // The problem is with declarations like the following:
  //   template <T> friend A<T>::foo;
  // where deciding whether a class C is a friend or not now hinges
  // on whether there exists an instantiation of A that causes
  // 'foo' to equal C.  There are restrictions on class-heads
  // (which we declare (by fiat) elaborated friend declarations to
  // be) that makes this tractable.
  //
  // FIXME: handle "template <> friend class A<T>;", which
  // is possibly well-formed?  Who even knows?
  if (TempParams.size() && !T->isElaboratedTypeSpecifier()) {
    Diag(Loc, diag::err_tagless_friend_type_template)
      << DS.getSourceRange();
    return 0;
  }
  
  // C++98 [class.friend]p1: A friend of a class is a function
  //   or class that is not a member of the class . . .
  // This is fixed in DR77, which just barely didn't make the C++03
  // deadline.  It's also a very silly restriction that seriously
  // affects inner classes and which nobody else seems to implement;
  // thus we never diagnose it, not even in -pedantic.
  //
  // But note that we could warn about it: it's always useless to
  // friend one of your own members (it's not, however, worthless to
  // friend a member of an arbitrary specialization of your template).

  Decl *D;
  if (unsigned NumTempParamLists = TempParams.size())
    D = FriendTemplateDecl::Create(Context, CurContext, Loc,
                                   NumTempParamLists,
                                   TempParams.release(),
                                   TSI,
                                   DS.getFriendSpecLoc());
  else
    D = CheckFriendTypeDecl(DS.getFriendSpecLoc(), TSI);
  
  if (!D)
    return 0;
  
  D->setAccess(AS_public);
  CurContext->addDecl(D);

  return D;
}

Decl *Sema::ActOnFriendFunctionDecl(Scope *S, Declarator &D,
                                    MultiTemplateParamsArg TemplateParams) {
  const DeclSpec &DS = D.getDeclSpec();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  SourceLocation Loc = D.getIdentifierLoc();
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);

  // C++ [class.friend]p1
  //   A friend of a class is a function or class....
  // Note that this sees through typedefs, which is intended.
  // It *doesn't* see through dependent types, which is correct
  // according to [temp.arg.type]p3:
  //   If a declaration acquires a function type through a
  //   type dependent on a template-parameter and this causes
  //   a declaration that does not use the syntactic form of a
  //   function declarator to have a function type, the program
  //   is ill-formed.
  if (!TInfo->getType()->isFunctionType()) {
    Diag(Loc, diag::err_unexpected_friend);

    // It might be worthwhile to try to recover by creating an
    // appropriate declaration.
    return 0;
  }

  // C++ [namespace.memdef]p3
  //  - If a friend declaration in a non-local class first declares a
  //    class or function, the friend class or function is a member
  //    of the innermost enclosing namespace.
  //  - The name of the friend is not found by simple name lookup
  //    until a matching declaration is provided in that namespace
  //    scope (either before or after the class declaration granting
  //    friendship).
  //  - If a friend function is called, its name may be found by the
  //    name lookup that considers functions from namespaces and
  //    classes associated with the types of the function arguments.
  //  - When looking for a prior declaration of a class or a function
  //    declared as a friend, scopes outside the innermost enclosing
  //    namespace scope are not considered.

  CXXScopeSpec &SS = D.getCXXScopeSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  assert(Name);

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(Loc, TInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(NameInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(SS, UPPC_FriendDeclaration))
    return 0;

  // The context we found the declaration in, or in which we should
  // create the declaration.
  DeclContext *DC;
  Scope *DCScope = S;
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        ForRedeclaration);

  // FIXME: there are different rules in local classes

  // There are four cases here.
  //   - There's no scope specifier, in which case we just go to the
  //     appropriate scope and look for a function or function template
  //     there as appropriate.
  // Recover from invalid scope qualifiers as if they just weren't there.
  if (SS.isInvalid() || !SS.isSet()) {
    // C++0x [namespace.memdef]p3:
    //   If the name in a friend declaration is neither qualified nor
    //   a template-id and the declaration is a function or an
    //   elaborated-type-specifier, the lookup to determine whether
    //   the entity has been previously declared shall not consider
    //   any scopes outside the innermost enclosing namespace.
    // C++0x [class.friend]p11:
    //   If a friend declaration appears in a local class and the name
    //   specified is an unqualified name, a prior declaration is
    //   looked up without considering scopes that are outside the
    //   innermost enclosing non-class scope. For a friend function
    //   declaration, if there is no prior declaration, the program is
    //   ill-formed.
    bool isLocal = cast<CXXRecordDecl>(CurContext)->isLocalClass();
    bool isTemplateId = D.getName().getKind() == UnqualifiedId::IK_TemplateId;

    // Find the appropriate context according to the above.
    DC = CurContext;
    while (true) {
      // Skip class contexts.  If someone can cite chapter and verse
      // for this behavior, that would be nice --- it's what GCC and
      // EDG do, and it seems like a reasonable intent, but the spec
      // really only says that checks for unqualified existing
      // declarations should stop at the nearest enclosing namespace,
      // not that they should only consider the nearest enclosing
      // namespace.
      while (DC->isRecord()) 
        DC = DC->getParent();

      LookupQualifiedName(Previous, DC);

      // TODO: decide what we think about using declarations.
      if (isLocal || !Previous.empty())
        break;

      if (isTemplateId) {
        if (isa<TranslationUnitDecl>(DC)) break;
      } else {
        if (DC->isFileContext()) break;
      }
      DC = DC->getParent();
    }

    // C++ [class.friend]p1: A friend of a class is a function or
    //   class that is not a member of the class . . .
    // C++0x changes this for both friend types and functions.
    // Most C++ 98 compilers do seem to give an error here, so
    // we do, too.
    if (!Previous.empty() && DC->Equals(CurContext)
        && !getLangOptions().CPlusPlus0x)
      Diag(DS.getFriendSpecLoc(), diag::err_friend_is_member);

    DCScope = getScopeForDeclContext(S, DC);

    // C++ [class.friend]p6:
    //   A function can be defined in a friend declaration of a class if and 
    //   only if the class is a non-local class (9.8), the function name is
    //   unqualified, and the function has namespace scope.
    if (isLocal && D.isFunctionDefinition()) {
      Diag(NameInfo.getBeginLoc(), diag::err_friend_def_in_local_class);
    }
    
  //   - There's a non-dependent scope specifier, in which case we
  //     compute it and do a previous lookup there for a function
  //     or function template.
  } else if (!SS.getScopeRep()->isDependent()) {
    DC = computeDeclContext(SS);
    if (!DC) return 0;

    if (RequireCompleteDeclContext(SS, DC)) return 0;

    LookupQualifiedName(Previous, DC);

    // Ignore things found implicitly in the wrong scope.
    // TODO: better diagnostics for this case.  Suggesting the right
    // qualified scope would be nice...
    LookupResult::Filter F = Previous.makeFilter();
    while (F.hasNext()) {
      NamedDecl *D = F.next();
      if (!DC->InEnclosingNamespaceSetOf(
              D->getDeclContext()->getRedeclContext()))
        F.erase();
    }
    F.done();

    if (Previous.empty()) {
      D.setInvalidType();
      Diag(Loc, diag::err_qualified_friend_not_found)
          << Name << TInfo->getType();
      return 0;
    }

    // C++ [class.friend]p1: A friend of a class is a function or
    //   class that is not a member of the class . . .
    if (DC->Equals(CurContext))
      Diag(DS.getFriendSpecLoc(), diag::err_friend_is_member);
    
    if (D.isFunctionDefinition()) {
      // C++ [class.friend]p6:
      //   A function can be defined in a friend declaration of a class if and 
      //   only if the class is a non-local class (9.8), the function name is
      //   unqualified, and the function has namespace scope.
      SemaDiagnosticBuilder DB
        = Diag(SS.getRange().getBegin(), diag::err_qualified_friend_def);
      
      DB << SS.getScopeRep();
      if (DC->isFileContext())
        DB << FixItHint::CreateRemoval(SS.getRange());
      SS.clear();
    }

  //   - There's a scope specifier that does not match any template
  //     parameter lists, in which case we use some arbitrary context,
  //     create a method or method template, and wait for instantiation.
  //   - There's a scope specifier that does match some template
  //     parameter lists, which we don't handle right now.
  } else {
    if (D.isFunctionDefinition()) {
      // C++ [class.friend]p6:
      //   A function can be defined in a friend declaration of a class if and 
      //   only if the class is a non-local class (9.8), the function name is
      //   unqualified, and the function has namespace scope.
      Diag(SS.getRange().getBegin(), diag::err_qualified_friend_def)
        << SS.getScopeRep();
    }
    
    DC = CurContext;
    assert(isa<CXXRecordDecl>(DC) && "friend declaration not in class?");
  }
  
  if (!DC->isRecord()) {
    // This implies that it has to be an operator or function.
    if (D.getName().getKind() == UnqualifiedId::IK_ConstructorName ||
        D.getName().getKind() == UnqualifiedId::IK_DestructorName ||
        D.getName().getKind() == UnqualifiedId::IK_ConversionFunctionId) {
      Diag(Loc, diag::err_introducing_special_friend) <<
        (D.getName().getKind() == UnqualifiedId::IK_ConstructorName ? 0 :
         D.getName().getKind() == UnqualifiedId::IK_DestructorName ? 1 : 2);
      return 0;
    }
  }

  bool AddToScope = true;
  NamedDecl *ND = ActOnFunctionDeclarator(DCScope, D, DC, TInfo, Previous,
                                          move(TemplateParams), AddToScope);
  if (!ND) return 0;

  assert(ND->getDeclContext() == DC);
  assert(ND->getLexicalDeclContext() == CurContext);

  // Add the function declaration to the appropriate lookup tables,
  // adjusting the redeclarations list as necessary.  We don't
  // want to do this yet if the friending class is dependent.
  //
  // Also update the scope-based lookup if the target context's
  // lookup context is in lexical scope.
  if (!CurContext->isDependentContext()) {
    DC = DC->getRedeclContext();
    DC->makeDeclVisibleInContext(ND, /* Recoverable=*/ false);
    if (Scope *EnclosingScope = getScopeForDeclContext(S, DC))
      PushOnScopeChains(ND, EnclosingScope, /*AddToContext=*/ false);
  }

  FriendDecl *FrD = FriendDecl::Create(Context, CurContext,
                                       D.getIdentifierLoc(), ND,
                                       DS.getFriendSpecLoc());
  FrD->setAccess(AS_public);
  CurContext->addDecl(FrD);

  if (ND->isInvalidDecl())
    FrD->setInvalidDecl();
  else {
    FunctionDecl *FD;
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      FD = FTD->getTemplatedDecl();
    else
      FD = cast<FunctionDecl>(ND);

    // Mark templated-scope function declarations as unsupported.
    if (FD->getNumTemplateParameterLists())
      FrD->setUnsupportedFriend(true);
  }

  return ND;
}

void Sema::SetDeclDeleted(Decl *Dcl, SourceLocation DelLoc) {
  AdjustDeclIfTemplate(Dcl);

  FunctionDecl *Fn = dyn_cast<FunctionDecl>(Dcl);
  if (!Fn) {
    Diag(DelLoc, diag::err_deleted_non_function);
    return;
  }
  if (const FunctionDecl *Prev = Fn->getPreviousDeclaration()) {
    Diag(DelLoc, diag::err_deleted_decl_not_first);
    Diag(Prev->getLocation(), diag::note_previous_declaration);
    // If the declaration wasn't the first, we delete the function anyway for
    // recovery.
  }
  Fn->setDeletedAsWritten();
}

void Sema::SetDeclDefaulted(Decl *Dcl, SourceLocation DefaultLoc) {
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Dcl);

  if (MD) {
    if (MD->getParent()->isDependentType()) {
      MD->setDefaulted();
      MD->setExplicitlyDefaulted();
      return;
    }

    CXXSpecialMember Member = getSpecialMember(MD);
    if (Member == CXXInvalid) {
      Diag(DefaultLoc, diag::err_default_special_members);
      return;
    }

    MD->setDefaulted();
    MD->setExplicitlyDefaulted();

    // If this definition appears within the record, do the checking when
    // the record is complete.
    const FunctionDecl *Primary = MD;
    if (MD->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
      // Find the uninstantiated declaration that actually had the '= default'
      // on it.
      MD->getTemplateInstantiationPattern()->isDefined(Primary);

    if (Primary == Primary->getCanonicalDecl())
      return;

    switch (Member) {
    case CXXDefaultConstructor: {
      CXXConstructorDecl *CD = cast<CXXConstructorDecl>(MD);
      CheckExplicitlyDefaultedDefaultConstructor(CD);
      if (!CD->isInvalidDecl())
        DefineImplicitDefaultConstructor(DefaultLoc, CD);
      break;
    }

    case CXXCopyConstructor: {
      CXXConstructorDecl *CD = cast<CXXConstructorDecl>(MD);
      CheckExplicitlyDefaultedCopyConstructor(CD);
      if (!CD->isInvalidDecl())
        DefineImplicitCopyConstructor(DefaultLoc, CD);
      break;
    }

    case CXXCopyAssignment: {
      CheckExplicitlyDefaultedCopyAssignment(MD);
      if (!MD->isInvalidDecl())
        DefineImplicitCopyAssignment(DefaultLoc, MD);
      break;
    }

    case CXXDestructor: {
      CXXDestructorDecl *DD = cast<CXXDestructorDecl>(MD);
      CheckExplicitlyDefaultedDestructor(DD);
      if (!DD->isInvalidDecl())
        DefineImplicitDestructor(DefaultLoc, DD);
      break;
    }

    case CXXMoveConstructor: {
      CXXConstructorDecl *CD = cast<CXXConstructorDecl>(MD);
      CheckExplicitlyDefaultedMoveConstructor(CD);
      if (!CD->isInvalidDecl())
        DefineImplicitMoveConstructor(DefaultLoc, CD);
      break;
    }

    case CXXMoveAssignment: {
      CheckExplicitlyDefaultedMoveAssignment(MD);
      if (!MD->isInvalidDecl())
        DefineImplicitMoveAssignment(DefaultLoc, MD);
      break;
    }

    case CXXInvalid:
      llvm_unreachable("Invalid special member.");
    }
  } else {
    Diag(DefaultLoc, diag::err_default_special_members);
  }
}

static void SearchForReturnInStmt(Sema &Self, Stmt *S) {
  for (Stmt::child_range CI = S->children(); CI; ++CI) {
    Stmt *SubStmt = *CI;
    if (!SubStmt)
      continue;
    if (isa<ReturnStmt>(SubStmt))
      Self.Diag(SubStmt->getSourceRange().getBegin(),
           diag::err_return_in_constructor_handler);
    if (!isa<Expr>(SubStmt))
      SearchForReturnInStmt(Self, SubStmt);
  }
}

void Sema::DiagnoseReturnInConstructorExceptionHandler(CXXTryStmt *TryBlock) {
  for (unsigned I = 0, E = TryBlock->getNumHandlers(); I != E; ++I) {
    CXXCatchStmt *Handler = TryBlock->getHandler(I);
    SearchForReturnInStmt(*this, Handler);
  }
}

bool Sema::CheckOverridingFunctionReturnType(const CXXMethodDecl *New,
                                             const CXXMethodDecl *Old) {
  QualType NewTy = New->getType()->getAs<FunctionType>()->getResultType();
  QualType OldTy = Old->getType()->getAs<FunctionType>()->getResultType();

  if (Context.hasSameType(NewTy, OldTy) ||
      NewTy->isDependentType() || OldTy->isDependentType())
    return false;

  // Check if the return types are covariant
  QualType NewClassTy, OldClassTy;

  /// Both types must be pointers or references to classes.
  if (const PointerType *NewPT = NewTy->getAs<PointerType>()) {
    if (const PointerType *OldPT = OldTy->getAs<PointerType>()) {
      NewClassTy = NewPT->getPointeeType();
      OldClassTy = OldPT->getPointeeType();
    }
  } else if (const ReferenceType *NewRT = NewTy->getAs<ReferenceType>()) {
    if (const ReferenceType *OldRT = OldTy->getAs<ReferenceType>()) {
      if (NewRT->getTypeClass() == OldRT->getTypeClass()) {
        NewClassTy = NewRT->getPointeeType();
        OldClassTy = OldRT->getPointeeType();
      }
    }
  }

  // The return types aren't either both pointers or references to a class type.
  if (NewClassTy.isNull()) {
    Diag(New->getLocation(),
         diag::err_different_return_type_for_overriding_virtual_function)
      << New->getDeclName() << NewTy << OldTy;
    Diag(Old->getLocation(), diag::note_overridden_virtual_function);

    return true;
  }

  // C++ [class.virtual]p6:
  //   If the return type of D::f differs from the return type of B::f, the 
  //   class type in the return type of D::f shall be complete at the point of
  //   declaration of D::f or shall be the class type D.
  if (const RecordType *RT = NewClassTy->getAs<RecordType>()) {
    if (!RT->isBeingDefined() &&
        RequireCompleteType(New->getLocation(), NewClassTy, 
                            PDiag(diag::err_covariant_return_incomplete)
                              << New->getDeclName()))
    return true;
  }

  if (!Context.hasSameUnqualifiedType(NewClassTy, OldClassTy)) {
    // Check if the new class derives from the old class.
    if (!IsDerivedFrom(NewClassTy, OldClassTy)) {
      Diag(New->getLocation(),
           diag::err_covariant_return_not_derived)
      << New->getDeclName() << NewTy << OldTy;
      Diag(Old->getLocation(), diag::note_overridden_virtual_function);
      return true;
    }

    // Check if we the conversion from derived to base is valid.
    if (CheckDerivedToBaseConversion(NewClassTy, OldClassTy,
                    diag::err_covariant_return_inaccessible_base,
                    diag::err_covariant_return_ambiguous_derived_to_base_conv,
                    // FIXME: Should this point to the return type?
                    New->getLocation(), SourceRange(), New->getDeclName(), 0)) {
      // FIXME: this note won't trigger for delayed access control
      // diagnostics, and it's impossible to get an undelayed error
      // here from access control during the original parse because
      // the ParsingDeclSpec/ParsingDeclarator are still in scope.
      Diag(Old->getLocation(), diag::note_overridden_virtual_function);
      return true;
    }
  }

  // The qualifiers of the return types must be the same.
  if (NewTy.getLocalCVRQualifiers() != OldTy.getLocalCVRQualifiers()) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_different_qualifications)
    << New->getDeclName() << NewTy << OldTy;
    Diag(Old->getLocation(), diag::note_overridden_virtual_function);
    return true;
  };


  // The new class type must have the same or less qualifiers as the old type.
  if (NewClassTy.isMoreQualifiedThan(OldClassTy)) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_class_type_more_qualified)
    << New->getDeclName() << NewTy << OldTy;
    Diag(Old->getLocation(), diag::note_overridden_virtual_function);
    return true;
  };

  return false;
}

/// \brief Mark the given method pure.
///
/// \param Method the method to be marked pure.
///
/// \param InitRange the source range that covers the "0" initializer.
bool Sema::CheckPureMethod(CXXMethodDecl *Method, SourceRange InitRange) {
  SourceLocation EndLoc = InitRange.getEnd();
  if (EndLoc.isValid())
    Method->setRangeEnd(EndLoc);

  if (Method->isVirtual() || Method->getParent()->isDependentContext()) {
    Method->setPure();
    return false;
  }

  if (!Method->isInvalidDecl())
    Diag(Method->getLocation(), diag::err_non_virtual_pure)
      << Method->getDeclName() << InitRange;
  return true;
}

/// ActOnCXXEnterDeclInitializer - Invoked when we are about to parse
/// an initializer for the out-of-line declaration 'Dcl'.  The scope
/// is a fresh scope pushed for just this purpose.
///
/// After this method is called, according to [C++ 3.4.1p13], if 'Dcl' is a
/// static data member of class X, names should be looked up in the scope of
/// class X.
void Sema::ActOnCXXEnterDeclInitializer(Scope *S, Decl *D) {
  // If there is no declaration, there was an error parsing it.
  if (D == 0 || D->isInvalidDecl()) return;

  // We should only get called for declarations with scope specifiers, like:
  //   int foo::bar;
  assert(D->isOutOfLine());
  EnterDeclaratorContext(S, D->getDeclContext());
}

/// ActOnCXXExitDeclInitializer - Invoked after we are finished parsing an
/// initializer for the out-of-line declaration 'D'.
void Sema::ActOnCXXExitDeclInitializer(Scope *S, Decl *D) {
  // If there is no declaration, there was an error parsing it.
  if (D == 0 || D->isInvalidDecl()) return;

  assert(D->isOutOfLine());
  ExitDeclaratorContext(S);
}

/// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
/// C++ if/switch/while/for statement.
/// e.g: "if (int x = f()) {...}"
DeclResult Sema::ActOnCXXConditionDeclaration(Scope *S, Declarator &D) {
  // C++ 6.4p2:
  // The declarator shall not specify a function or an array.
  // The type-specifier-seq shall not contain typedef and shall not declare a
  // new class or enumeration.
  assert(D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class of condition decl.");

  Decl *Dcl = ActOnDeclarator(S, D);
  if (!Dcl)
    return true;

  if (isa<FunctionDecl>(Dcl)) { // The declarator shall not specify a function.
    Diag(Dcl->getLocation(), diag::err_invalid_use_of_function_type)
      << D.getSourceRange();
    return true;
  }

  return Dcl;
}

void Sema::LoadExternalVTableUses() {
  if (!ExternalSource)
    return;
  
  SmallVector<ExternalVTableUse, 4> VTables;
  ExternalSource->ReadUsedVTables(VTables);
  SmallVector<VTableUse, 4> NewUses;
  for (unsigned I = 0, N = VTables.size(); I != N; ++I) {
    llvm::DenseMap<CXXRecordDecl *, bool>::iterator Pos
      = VTablesUsed.find(VTables[I].Record);
    // Even if a definition wasn't required before, it may be required now.
    if (Pos != VTablesUsed.end()) {
      if (!Pos->second && VTables[I].DefinitionRequired)
        Pos->second = true;
      continue;
    }
    
    VTablesUsed[VTables[I].Record] = VTables[I].DefinitionRequired;
    NewUses.push_back(VTableUse(VTables[I].Record, VTables[I].Location));
  }
  
  VTableUses.insert(VTableUses.begin(), NewUses.begin(), NewUses.end());
}

void Sema::MarkVTableUsed(SourceLocation Loc, CXXRecordDecl *Class,
                          bool DefinitionRequired) {
  // Ignore any vtable uses in unevaluated operands or for classes that do
  // not have a vtable.
  if (!Class->isDynamicClass() || Class->isDependentContext() ||
      CurContext->isDependentContext() ||
      ExprEvalContexts.back().Context == Unevaluated)
    return;

  // Try to insert this class into the map.
  LoadExternalVTableUses();
  Class = cast<CXXRecordDecl>(Class->getCanonicalDecl());
  std::pair<llvm::DenseMap<CXXRecordDecl *, bool>::iterator, bool>
    Pos = VTablesUsed.insert(std::make_pair(Class, DefinitionRequired));
  if (!Pos.second) {
    // If we already had an entry, check to see if we are promoting this vtable
    // to required a definition. If so, we need to reappend to the VTableUses
    // list, since we may have already processed the first entry.
    if (DefinitionRequired && !Pos.first->second) {
      Pos.first->second = true;
    } else {
      // Otherwise, we can early exit.
      return;
    }
  }

  // Local classes need to have their virtual members marked
  // immediately. For all other classes, we mark their virtual members
  // at the end of the translation unit.
  if (Class->isLocalClass())
    MarkVirtualMembersReferenced(Loc, Class);
  else
    VTableUses.push_back(std::make_pair(Class, Loc));
}

bool Sema::DefineUsedVTables() {
  LoadExternalVTableUses();
  if (VTableUses.empty())
    return false;

  // Note: The VTableUses vector could grow as a result of marking
  // the members of a class as "used", so we check the size each
  // time through the loop and prefer indices (with are stable) to
  // iterators (which are not).
  bool DefinedAnything = false;
  for (unsigned I = 0; I != VTableUses.size(); ++I) {
    CXXRecordDecl *Class = VTableUses[I].first->getDefinition();
    if (!Class)
      continue;

    SourceLocation Loc = VTableUses[I].second;

    // If this class has a key function, but that key function is
    // defined in another translation unit, we don't need to emit the
    // vtable even though we're using it.
    const CXXMethodDecl *KeyFunction = Context.getKeyFunction(Class);
    if (KeyFunction && !KeyFunction->hasBody()) {
      switch (KeyFunction->getTemplateSpecializationKind()) {
      case TSK_Undeclared:
      case TSK_ExplicitSpecialization:
      case TSK_ExplicitInstantiationDeclaration:
        // The key function is in another translation unit.
        continue;

      case TSK_ExplicitInstantiationDefinition:
      case TSK_ImplicitInstantiation:
        // We will be instantiating the key function.
        break;
      }
    } else if (!KeyFunction) {
      // If we have a class with no key function that is the subject
      // of an explicit instantiation declaration, suppress the
      // vtable; it will live with the explicit instantiation
      // definition.
      bool IsExplicitInstantiationDeclaration
        = Class->getTemplateSpecializationKind()
                                      == TSK_ExplicitInstantiationDeclaration;
      for (TagDecl::redecl_iterator R = Class->redecls_begin(),
                                 REnd = Class->redecls_end();
           R != REnd; ++R) {
        TemplateSpecializationKind TSK
          = cast<CXXRecordDecl>(*R)->getTemplateSpecializationKind();
        if (TSK == TSK_ExplicitInstantiationDeclaration)
          IsExplicitInstantiationDeclaration = true;
        else if (TSK == TSK_ExplicitInstantiationDefinition) {
          IsExplicitInstantiationDeclaration = false;
          break;
        }
      }

      if (IsExplicitInstantiationDeclaration)
        continue;
    }

    // Mark all of the virtual members of this class as referenced, so
    // that we can build a vtable. Then, tell the AST consumer that a
    // vtable for this class is required.
    DefinedAnything = true;
    MarkVirtualMembersReferenced(Loc, Class);
    CXXRecordDecl *Canonical = cast<CXXRecordDecl>(Class->getCanonicalDecl());
    Consumer.HandleVTable(Class, VTablesUsed[Canonical]);

    // Optionally warn if we're emitting a weak vtable.
    if (Class->getLinkage() == ExternalLinkage &&
        Class->getTemplateSpecializationKind() != TSK_ImplicitInstantiation) {
      const FunctionDecl *KeyFunctionDef = 0;
      if (!KeyFunction || 
          (KeyFunction->hasBody(KeyFunctionDef) && 
           KeyFunctionDef->isInlined()))
        Diag(Class->getLocation(), diag::warn_weak_vtable) << Class;
    }
  }
  VTableUses.clear();

  return DefinedAnything;
}

void Sema::MarkVirtualMembersReferenced(SourceLocation Loc,
                                        const CXXRecordDecl *RD) {
  for (CXXRecordDecl::method_iterator i = RD->method_begin(), 
       e = RD->method_end(); i != e; ++i) {
    CXXMethodDecl *MD = *i;

    // C++ [basic.def.odr]p2:
    //   [...] A virtual member function is used if it is not pure. [...]
    if (MD->isVirtual() && !MD->isPure())
      MarkDeclarationReferenced(Loc, MD);
  }

  // Only classes that have virtual bases need a VTT.
  if (RD->getNumVBases() == 0)
    return;

  for (CXXRecordDecl::base_class_const_iterator i = RD->bases_begin(),
           e = RD->bases_end(); i != e; ++i) {
    const CXXRecordDecl *Base =
        cast<CXXRecordDecl>(i->getType()->getAs<RecordType>()->getDecl());
    if (Base->getNumVBases() == 0)
      continue;
    MarkVirtualMembersReferenced(Loc, Base);
  }
}

/// SetIvarInitializers - This routine builds initialization ASTs for the
/// Objective-C implementation whose ivars need be initialized.
void Sema::SetIvarInitializers(ObjCImplementationDecl *ObjCImplementation) {
  if (!getLangOptions().CPlusPlus)
    return;
  if (ObjCInterfaceDecl *OID = ObjCImplementation->getClassInterface()) {
    SmallVector<ObjCIvarDecl*, 8> ivars;
    CollectIvarsToConstructOrDestruct(OID, ivars);
    if (ivars.empty())
      return;
    SmallVector<CXXCtorInitializer*, 32> AllToInit;
    for (unsigned i = 0; i < ivars.size(); i++) {
      FieldDecl *Field = ivars[i];
      if (Field->isInvalidDecl())
        continue;
      
      CXXCtorInitializer *Member;
      InitializedEntity InitEntity = InitializedEntity::InitializeMember(Field);
      InitializationKind InitKind = 
        InitializationKind::CreateDefault(ObjCImplementation->getLocation());
      
      InitializationSequence InitSeq(*this, InitEntity, InitKind, 0, 0);
      ExprResult MemberInit = 
        InitSeq.Perform(*this, InitEntity, InitKind, MultiExprArg());
      MemberInit = MaybeCreateExprWithCleanups(MemberInit);
      // Note, MemberInit could actually come back empty if no initialization 
      // is required (e.g., because it would call a trivial default constructor)
      if (!MemberInit.get() || MemberInit.isInvalid())
        continue;

      Member =
        new (Context) CXXCtorInitializer(Context, Field, SourceLocation(),
                                         SourceLocation(),
                                         MemberInit.takeAs<Expr>(),
                                         SourceLocation());
      AllToInit.push_back(Member);
      
      // Be sure that the destructor is accessible and is marked as referenced.
      if (const RecordType *RecordTy
                  = Context.getBaseElementType(Field->getType())
                                                        ->getAs<RecordType>()) {
                    CXXRecordDecl *RD = cast<CXXRecordDecl>(RecordTy->getDecl());
        if (CXXDestructorDecl *Destructor = LookupDestructor(RD)) {
          MarkDeclarationReferenced(Field->getLocation(), Destructor);
          CheckDestructorAccess(Field->getLocation(), Destructor,
                            PDiag(diag::err_access_dtor_ivar)
                              << Context.getBaseElementType(Field->getType()));
        }
      }      
    }
    ObjCImplementation->setIvarInitializers(Context, 
                                            AllToInit.data(), AllToInit.size());
  }
}

static
void DelegatingCycleHelper(CXXConstructorDecl* Ctor,
                           llvm::SmallSet<CXXConstructorDecl*, 4> &Valid,
                           llvm::SmallSet<CXXConstructorDecl*, 4> &Invalid,
                           llvm::SmallSet<CXXConstructorDecl*, 4> &Current,
                           Sema &S) {
  llvm::SmallSet<CXXConstructorDecl*, 4>::iterator CI = Current.begin(),
                                                   CE = Current.end();
  if (Ctor->isInvalidDecl())
    return;

  const FunctionDecl *FNTarget = 0;
  CXXConstructorDecl *Target;
  
  // We ignore the result here since if we don't have a body, Target will be
  // null below.
  (void)Ctor->getTargetConstructor()->hasBody(FNTarget);
  Target
= const_cast<CXXConstructorDecl*>(cast_or_null<CXXConstructorDecl>(FNTarget));

  CXXConstructorDecl *Canonical = Ctor->getCanonicalDecl(),
                     // Avoid dereferencing a null pointer here.
                     *TCanonical = Target ? Target->getCanonicalDecl() : 0;

  if (!Current.insert(Canonical))
    return;

  // We know that beyond here, we aren't chaining into a cycle.
  if (!Target || !Target->isDelegatingConstructor() ||
      Target->isInvalidDecl() || Valid.count(TCanonical)) {
    for (CI = Current.begin(), CE = Current.end(); CI != CE; ++CI)
      Valid.insert(*CI);
    Current.clear();
  // We've hit a cycle.
  } else if (TCanonical == Canonical || Invalid.count(TCanonical) ||
             Current.count(TCanonical)) {
    // If we haven't diagnosed this cycle yet, do so now.
    if (!Invalid.count(TCanonical)) {
      S.Diag((*Ctor->init_begin())->getSourceLocation(),
             diag::warn_delegating_ctor_cycle)
        << Ctor;

      // Don't add a note for a function delegating directo to itself.
      if (TCanonical != Canonical)
        S.Diag(Target->getLocation(), diag::note_it_delegates_to);

      CXXConstructorDecl *C = Target;
      while (C->getCanonicalDecl() != Canonical) {
        (void)C->getTargetConstructor()->hasBody(FNTarget);
        assert(FNTarget && "Ctor cycle through bodiless function");

        C
       = const_cast<CXXConstructorDecl*>(cast<CXXConstructorDecl>(FNTarget));
        S.Diag(C->getLocation(), diag::note_which_delegates_to);
      }
    }

    for (CI = Current.begin(), CE = Current.end(); CI != CE; ++CI)
      Invalid.insert(*CI);
    Current.clear();
  } else {
    DelegatingCycleHelper(Target, Valid, Invalid, Current, S);
  }
}
   

void Sema::CheckDelegatingCtorCycles() {
  llvm::SmallSet<CXXConstructorDecl*, 4> Valid, Invalid, Current;

  llvm::SmallSet<CXXConstructorDecl*, 4>::iterator CI = Current.begin(),
                                                   CE = Current.end();

  for (DelegatingCtorDeclsType::iterator
         I = DelegatingCtorDecls.begin(ExternalSource),
         E = DelegatingCtorDecls.end();
       I != E; ++I) {
   DelegatingCycleHelper(*I, Valid, Invalid, Current, *this);
  }

  for (CI = Invalid.begin(), CE = Invalid.end(); CI != CE; ++CI)
    (*CI)->setInvalidDecl();
}

/// IdentifyCUDATarget - Determine the CUDA compilation target for this function
Sema::CUDAFunctionTarget Sema::IdentifyCUDATarget(const FunctionDecl *D) {
  // Implicitly declared functions (e.g. copy constructors) are
  // __host__ __device__
  if (D->isImplicit())
    return CFT_HostDevice;

  if (D->hasAttr<CUDAGlobalAttr>())
    return CFT_Global;

  if (D->hasAttr<CUDADeviceAttr>()) {
    if (D->hasAttr<CUDAHostAttr>())
      return CFT_HostDevice;
    else
      return CFT_Device;
  }

  return CFT_Host;
}

bool Sema::CheckCUDATarget(CUDAFunctionTarget CallerTarget,
                           CUDAFunctionTarget CalleeTarget) {
  // CUDA B.1.1 "The __device__ qualifier declares a function that is...
  // Callable from the device only."
  if (CallerTarget == CFT_Host && CalleeTarget == CFT_Device)
    return true;

  // CUDA B.1.2 "The __global__ qualifier declares a function that is...
  // Callable from the host only."
  // CUDA B.1.3 "The __host__ qualifier declares a function that is...
  // Callable from the host only."
  if ((CallerTarget == CFT_Device || CallerTarget == CFT_Global) &&
      (CalleeTarget == CFT_Host || CalleeTarget == CFT_Global))
    return true;

  if (CallerTarget == CFT_HostDevice && CalleeTarget != CFT_HostDevice)
    return true;

  return false;
}
