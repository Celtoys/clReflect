//===--- Expr.h - Classes for representing expressions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPR_H
#define LLVM_CLANG_AST_EXPR_H

#include "clang/AST/APValue.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ASTVector.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/UsuallyTinyPtrVector.h"
#include "clang/Basic/TypeTraits.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cctype>

namespace clang {
  class ASTContext;
  class APValue;
  class Decl;
  class IdentifierInfo;
  class ParmVarDecl;
  class NamedDecl;
  class ValueDecl;
  class BlockDecl;
  class CXXBaseSpecifier;
  class CXXOperatorCallExpr;
  class CXXMemberCallExpr;
  class ObjCPropertyRefExpr;
  class OpaqueValueExpr;

/// \brief A simple array of base specifiers.
typedef SmallVector<CXXBaseSpecifier*, 4> CXXCastPath;

/// Expr - This represents one expression.  Note that Expr's are subclasses of
/// Stmt.  This allows an expression to be transparently used any place a Stmt
/// is required.
///
class Expr : public Stmt {
  QualType TR;

protected:
  Expr(StmtClass SC, QualType T, ExprValueKind VK, ExprObjectKind OK,
       bool TD, bool VD, bool ID, bool ContainsUnexpandedParameterPack) 
    : Stmt(SC) 
  {
    ExprBits.TypeDependent = TD;
    ExprBits.ValueDependent = VD;
    ExprBits.InstantiationDependent = ID;
    ExprBits.ValueKind = VK;
    ExprBits.ObjectKind = OK;
    ExprBits.ContainsUnexpandedParameterPack = ContainsUnexpandedParameterPack;
    setType(T);
  }

  /// \brief Construct an empty expression.
  explicit Expr(StmtClass SC, EmptyShell) : Stmt(SC) { }

public:
  QualType getType() const { return TR; }
  void setType(QualType t) {
    // In C++, the type of an expression is always adjusted so that it
    // will not have reference type an expression will never have
    // reference type (C++ [expr]p6). Use
    // QualType::getNonReferenceType() to retrieve the non-reference
    // type. Additionally, inspect Expr::isLvalue to determine whether
    // an expression that is adjusted in this manner should be
    // considered an lvalue.
    assert((t.isNull() || !t->isReferenceType()) &&
           "Expressions can't have reference type");

    TR = t;
  }

  /// isValueDependent - Determines whether this expression is
  /// value-dependent (C++ [temp.dep.constexpr]). For example, the
  /// array bound of "Chars" in the following example is
  /// value-dependent.
  /// @code
  /// template<int Size, char (&Chars)[Size]> struct meta_string;
  /// @endcode
  bool isValueDependent() const { return ExprBits.ValueDependent; }

  /// \brief Set whether this expression is value-dependent or not.
  void setValueDependent(bool VD) { 
    ExprBits.ValueDependent = VD; 
    if (VD)
      ExprBits.InstantiationDependent = true;
  }

  /// isTypeDependent - Determines whether this expression is
  /// type-dependent (C++ [temp.dep.expr]), which means that its type
  /// could change from one template instantiation to the next. For
  /// example, the expressions "x" and "x + y" are type-dependent in
  /// the following code, but "y" is not type-dependent:
  /// @code
  /// template<typename T>
  /// void add(T x, int y) {
  ///   x + y;
  /// }
  /// @endcode
  bool isTypeDependent() const { return ExprBits.TypeDependent; }

  /// \brief Set whether this expression is type-dependent or not.
  void setTypeDependent(bool TD) { 
    ExprBits.TypeDependent = TD; 
    if (TD)
      ExprBits.InstantiationDependent = true;
  }

  /// \brief Whether this expression is instantiation-dependent, meaning that
  /// it depends in some way on a template parameter, even if neither its type
  /// nor (constant) value can change due to the template instantiation.
  ///
  /// In the following example, the expression \c sizeof(sizeof(T() + T())) is
  /// instantiation-dependent (since it involves a template parameter \c T), but
  /// is neither type- nor value-dependent, since the type of the inner
  /// \c sizeof is known (\c std::size_t) and therefore the size of the outer
  /// \c sizeof is known.
  ///
  /// \code
  /// template<typename T>
  /// void f(T x, T y) {
  ///   sizeof(sizeof(T() + T());
  /// }
  /// \endcode
  ///
  bool isInstantiationDependent() const { 
    return ExprBits.InstantiationDependent; 
  }
  
  /// \brief Set whether this expression is instantiation-dependent or not.
  void setInstantiationDependent(bool ID) { 
    ExprBits.InstantiationDependent = ID;
  }

  /// \brief Whether this expression contains an unexpanded parameter
  /// pack (for C++0x variadic templates).
  ///
  /// Given the following function template:
  ///
  /// \code
  /// template<typename F, typename ...Types>
  /// void forward(const F &f, Types &&...args) {
  ///   f(static_cast<Types&&>(args)...);
  /// }
  /// \endcode
  ///
  /// The expressions \c args and \c static_cast<Types&&>(args) both
  /// contain parameter packs.
  bool containsUnexpandedParameterPack() const { 
    return ExprBits.ContainsUnexpandedParameterPack; 
  }

  /// \brief Set the bit that describes whether this expression
  /// contains an unexpanded parameter pack.
  void setContainsUnexpandedParameterPack(bool PP = true) {
    ExprBits.ContainsUnexpandedParameterPack = PP;
  }

  /// getExprLoc - Return the preferred location for the arrow when diagnosing
  /// a problem with a generic expression.
  SourceLocation getExprLoc() const;

  /// isUnusedResultAWarning - Return true if this immediate expression should
  /// be warned about if the result is unused.  If so, fill in Loc and Ranges
  /// with location to warn on and the source range[s] to report with the
  /// warning.
  bool isUnusedResultAWarning(SourceLocation &Loc, SourceRange &R1,
                              SourceRange &R2, ASTContext &Ctx) const;

  /// isLValue - True if this expression is an "l-value" according to
  /// the rules of the current language.  C and C++ give somewhat
  /// different rules for this concept, but in general, the result of
  /// an l-value expression identifies a specific object whereas the
  /// result of an r-value expression is a value detached from any
  /// specific storage.
  ///
  /// C++0x divides the concept of "r-value" into pure r-values
  /// ("pr-values") and so-called expiring values ("x-values"), which
  /// identify specific objects that can be safely cannibalized for
  /// their resources.  This is an unfortunate abuse of terminology on
  /// the part of the C++ committee.  In Clang, when we say "r-value",
  /// we generally mean a pr-value.
  bool isLValue() const { return getValueKind() == VK_LValue; }
  bool isRValue() const { return getValueKind() == VK_RValue; }
  bool isXValue() const { return getValueKind() == VK_XValue; }
  bool isGLValue() const { return getValueKind() != VK_RValue; }

  enum LValueClassification {
    LV_Valid,
    LV_NotObjectType,
    LV_IncompleteVoidType,
    LV_DuplicateVectorComponents,
    LV_InvalidExpression,
    LV_InvalidMessageExpression,
    LV_MemberFunction,
    LV_SubObjCPropertySetting,
    LV_ClassTemporary
  };
  /// Reasons why an expression might not be an l-value.
  LValueClassification ClassifyLValue(ASTContext &Ctx) const;

  /// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
  /// does not have an incomplete type, does not have a const-qualified type,
  /// and if it is a structure or union, does not have any member (including,
  /// recursively, any member or element of all contained aggregates or unions)
  /// with a const-qualified type.
  ///
  /// \param Loc [in] [out] - A source location which *may* be filled
  /// in with the location of the expression making this a
  /// non-modifiable lvalue, if specified.
  enum isModifiableLvalueResult {
    MLV_Valid,
    MLV_NotObjectType,
    MLV_IncompleteVoidType,
    MLV_DuplicateVectorComponents,
    MLV_InvalidExpression,
    MLV_LValueCast,           // Specialized form of MLV_InvalidExpression.
    MLV_IncompleteType,
    MLV_ConstQualified,
    MLV_ArrayType,
    MLV_NotBlockQualified,
    MLV_ReadonlyProperty,
    MLV_NoSetterProperty,
    MLV_MemberFunction,
    MLV_SubObjCPropertySetting,
    MLV_InvalidMessageExpression,
    MLV_ClassTemporary
  };
  isModifiableLvalueResult isModifiableLvalue(ASTContext &Ctx,
                                              SourceLocation *Loc = 0) const;

  /// \brief The return type of classify(). Represents the C++0x expression
  ///        taxonomy.
  class Classification {
  public:
    /// \brief The various classification results. Most of these mean prvalue.
    enum Kinds {
      CL_LValue,
      CL_XValue,
      CL_Function, // Functions cannot be lvalues in C.
      CL_Void, // Void cannot be an lvalue in C.
      CL_AddressableVoid, // Void expression whose address can be taken in C.
      CL_DuplicateVectorComponents, // A vector shuffle with dupes.
      CL_MemberFunction, // An expression referring to a member function
      CL_SubObjCPropertySetting,
      CL_ClassTemporary, // A prvalue of class type
      CL_ObjCMessageRValue, // ObjC message is an rvalue
      CL_PRValue // A prvalue for any other reason, of any other type
    };
    /// \brief The results of modification testing.
    enum ModifiableType {
      CM_Untested, // testModifiable was false.
      CM_Modifiable,
      CM_RValue, // Not modifiable because it's an rvalue
      CM_Function, // Not modifiable because it's a function; C++ only
      CM_LValueCast, // Same as CM_RValue, but indicates GCC cast-as-lvalue ext
      CM_NotBlockQualified, // Not captured in the closure
      CM_NoSetterProperty,// Implicit assignment to ObjC property without setter
      CM_ConstQualified,
      CM_ArrayType,
      CM_IncompleteType
    };

  private:
    friend class Expr;

    unsigned short Kind;
    unsigned short Modifiable;

    explicit Classification(Kinds k, ModifiableType m)
      : Kind(k), Modifiable(m)
    {}

  public:
    Classification() {}

    Kinds getKind() const { return static_cast<Kinds>(Kind); }
    ModifiableType getModifiable() const {
      assert(Modifiable != CM_Untested && "Did not test for modifiability.");
      return static_cast<ModifiableType>(Modifiable);
    }
    bool isLValue() const { return Kind == CL_LValue; }
    bool isXValue() const { return Kind == CL_XValue; }
    bool isGLValue() const { return Kind <= CL_XValue; }
    bool isPRValue() const { return Kind >= CL_Function; }
    bool isRValue() const { return Kind >= CL_XValue; }
    bool isModifiable() const { return getModifiable() == CM_Modifiable; }
    
    /// \brief Create a simple, modifiably lvalue
    static Classification makeSimpleLValue() {
      return Classification(CL_LValue, CM_Modifiable);
    }
    
  };
  /// \brief Classify - Classify this expression according to the C++0x
  ///        expression taxonomy.
  ///
  /// C++0x defines ([basic.lval]) a new taxonomy of expressions to replace the
  /// old lvalue vs rvalue. This function determines the type of expression this
  /// is. There are three expression types:
  /// - lvalues are classical lvalues as in C++03.
  /// - prvalues are equivalent to rvalues in C++03.
  /// - xvalues are expressions yielding unnamed rvalue references, e.g. a
  ///   function returning an rvalue reference.
  /// lvalues and xvalues are collectively referred to as glvalues, while
  /// prvalues and xvalues together form rvalues.
  Classification Classify(ASTContext &Ctx) const {
    return ClassifyImpl(Ctx, 0);
  }

  /// \brief ClassifyModifiable - Classify this expression according to the
  ///        C++0x expression taxonomy, and see if it is valid on the left side
  ///        of an assignment.
  ///
  /// This function extends classify in that it also tests whether the
  /// expression is modifiable (C99 6.3.2.1p1).
  /// \param Loc A source location that might be filled with a relevant location
  ///            if the expression is not modifiable.
  Classification ClassifyModifiable(ASTContext &Ctx, SourceLocation &Loc) const{
    return ClassifyImpl(Ctx, &Loc);
  }

  /// getValueKindForType - Given a formal return or parameter type,
  /// give its value kind.
  static ExprValueKind getValueKindForType(QualType T) {
    if (const ReferenceType *RT = T->getAs<ReferenceType>())
      return (isa<LValueReferenceType>(RT)
                ? VK_LValue
                : (RT->getPointeeType()->isFunctionType()
                     ? VK_LValue : VK_XValue));
    return VK_RValue;
  }

  /// getValueKind - The value kind that this expression produces.
  ExprValueKind getValueKind() const {
    return static_cast<ExprValueKind>(ExprBits.ValueKind);
  }

  /// getObjectKind - The object kind that this expression produces.
  /// Object kinds are meaningful only for expressions that yield an
  /// l-value or x-value.
  ExprObjectKind getObjectKind() const {
    return static_cast<ExprObjectKind>(ExprBits.ObjectKind);
  }

  bool isOrdinaryOrBitFieldObject() const {
    ExprObjectKind OK = getObjectKind();
    return (OK == OK_Ordinary || OK == OK_BitField);
  }

  /// setValueKind - Set the value kind produced by this expression.
  void setValueKind(ExprValueKind Cat) { ExprBits.ValueKind = Cat; }

  /// setObjectKind - Set the object kind produced by this expression.
  void setObjectKind(ExprObjectKind Cat) { ExprBits.ObjectKind = Cat; }

private:
  Classification ClassifyImpl(ASTContext &Ctx, SourceLocation *Loc) const;

public:

  /// \brief If this expression refers to a bit-field, retrieve the
  /// declaration of that bit-field.
  FieldDecl *getBitField();

  const FieldDecl *getBitField() const {
    return const_cast<Expr*>(this)->getBitField();
  }

  /// \brief If this expression is an l-value for an Objective C
  /// property, find the underlying property reference expression.
  const ObjCPropertyRefExpr *getObjCProperty() const;

  /// \brief Returns whether this expression refers to a vector element.
  bool refersToVectorElement() const;
  
  /// isKnownToHaveBooleanValue - Return true if this is an integer expression
  /// that is known to return 0 or 1.  This happens for _Bool/bool expressions
  /// but also int expressions which are produced by things like comparisons in
  /// C.
  bool isKnownToHaveBooleanValue() const;
  
  /// isIntegerConstantExpr - Return true if this expression is a valid integer
  /// constant expression, and, if so, return its value in Result.  If not a
  /// valid i-c-e, return false and fill in Loc (if specified) with the location
  /// of the invalid expression.
  bool isIntegerConstantExpr(llvm::APSInt &Result, ASTContext &Ctx,
                             SourceLocation *Loc = 0,
                             bool isEvaluated = true) const;
  bool isIntegerConstantExpr(ASTContext &Ctx, SourceLocation *Loc = 0) const {
    llvm::APSInt X;
    return isIntegerConstantExpr(X, Ctx, Loc);
  }
  /// isConstantInitializer - Returns true if this expression is a constant
  /// initializer, which can be emitted at compile-time.
  bool isConstantInitializer(ASTContext &Ctx, bool ForRef) const;

  /// EvalResult is a struct with detailed info about an evaluated expression.
  struct EvalResult {
    /// Val - This is the value the expression can be folded to.
    APValue Val;

    /// HasSideEffects - Whether the evaluated expression has side effects.
    /// For example, (f() && 0) can be folded, but it still has side effects.
    bool HasSideEffects;

    /// Diag - If the expression is unfoldable, then Diag contains a note
    /// diagnostic indicating why it's not foldable. DiagLoc indicates a caret
    /// position for the error, and DiagExpr is the expression that caused
    /// the error.
    /// If the expression is foldable, but not an integer constant expression,
    /// Diag contains a note diagnostic that describes why it isn't an integer
    /// constant expression. If the expression *is* an integer constant
    /// expression, then Diag will be zero.
    unsigned Diag;
    const Expr *DiagExpr;
    SourceLocation DiagLoc;

    EvalResult() : HasSideEffects(false), Diag(0), DiagExpr(0) {}

    // isGlobalLValue - Return true if the evaluated lvalue expression
    // is global.
    bool isGlobalLValue() const;
    // hasSideEffects - Return true if the evaluated expression has
    // side effects.
    bool hasSideEffects() const {
      return HasSideEffects;
    }
  };

  /// Evaluate - Return true if this is a constant which we can fold using
  /// any crazy technique (that has nothing to do with language standards) that
  /// we want to.  If this function returns true, it returns the folded constant
  /// in Result.
  bool Evaluate(EvalResult &Result, const ASTContext &Ctx) const;

  /// EvaluateAsBooleanCondition - Return true if this is a constant
  /// which we we can fold and convert to a boolean condition using
  /// any crazy technique that we want to, even if the expression has
  /// side-effects.
  bool EvaluateAsBooleanCondition(bool &Result, const ASTContext &Ctx) const;

  /// EvaluateAsInt - Return true if this is a constant which we can fold and
  /// convert to an integer using any crazy technique that we want to.
  bool EvaluateAsInt(llvm::APSInt &Result, const ASTContext &Ctx) const;

  /// isEvaluatable - Call Evaluate to see if this expression can be constant
  /// folded, but discard the result.
  bool isEvaluatable(const ASTContext &Ctx) const;

  /// HasSideEffects - This routine returns true for all those expressions
  /// which must be evaluated each time and must not be optimized away 
  /// or evaluated at compile time. Example is a function call, volatile
  /// variable read.
  bool HasSideEffects(const ASTContext &Ctx) const;
  
  /// EvaluateKnownConstInt - Call Evaluate and return the folded integer. This
  /// must be called on an expression that constant folds to an integer.
  llvm::APSInt EvaluateKnownConstInt(const ASTContext &Ctx) const;

  /// EvaluateAsLValue - Evaluate an expression to see if it's a lvalue
  /// with link time known address.
  bool EvaluateAsLValue(EvalResult &Result, const ASTContext &Ctx) const;

  /// EvaluateAsLValue - Evaluate an expression to see if it's a lvalue.
  bool EvaluateAsAnyLValue(EvalResult &Result, const ASTContext &Ctx) const;

  /// \brief Enumeration used to describe the kind of Null pointer constant
  /// returned from \c isNullPointerConstant().
  enum NullPointerConstantKind {
    /// \brief Expression is not a Null pointer constant.
    NPCK_NotNull = 0,

    /// \brief Expression is a Null pointer constant built from a zero integer.
    NPCK_ZeroInteger,

    /// \brief Expression is a C++0X nullptr.
    NPCK_CXX0X_nullptr,

    /// \brief Expression is a GNU-style __null constant.
    NPCK_GNUNull
  };

  /// \brief Enumeration used to describe how \c isNullPointerConstant()
  /// should cope with value-dependent expressions.
  enum NullPointerConstantValueDependence {
    /// \brief Specifies that the expression should never be value-dependent.
    NPC_NeverValueDependent = 0,
    
    /// \brief Specifies that a value-dependent expression of integral or
    /// dependent type should be considered a null pointer constant.
    NPC_ValueDependentIsNull,
    
    /// \brief Specifies that a value-dependent expression should be considered
    /// to never be a null pointer constant.
    NPC_ValueDependentIsNotNull
  };
  
  /// isNullPointerConstant - C99 6.3.2.3p3 - Test if this reduces down to
  /// a Null pointer constant. The return value can further distinguish the
  /// kind of NULL pointer constant that was detected.
  NullPointerConstantKind isNullPointerConstant(
      ASTContext &Ctx,
      NullPointerConstantValueDependence NPC) const;

  /// isOBJCGCCandidate - Return true if this expression may be used in a read/
  /// write barrier.
  bool isOBJCGCCandidate(ASTContext &Ctx) const;

  /// \brief Returns true if this expression is a bound member function.
  bool isBoundMemberFunction(ASTContext &Ctx) const;

  /// \brief Given an expression of bound-member type, find the type
  /// of the member.  Returns null if this is an *overloaded* bound
  /// member expression.
  static QualType findBoundMemberType(const Expr *expr);

  /// \brief Result type of CanThrow().
  enum CanThrowResult {
    CT_Cannot,
    CT_Dependent,
    CT_Can
  };
  /// \brief Test if this expression, if evaluated, might throw, according to
  ///        the rules of C++ [expr.unary.noexcept].
  CanThrowResult CanThrow(ASTContext &C) const;

  /// IgnoreImpCasts - Skip past any implicit casts which might
  /// surround this expression.  Only skips ImplicitCastExprs.
  Expr *IgnoreImpCasts();

  /// IgnoreImplicit - Skip past any implicit AST nodes which might
  /// surround this expression.
  Expr *IgnoreImplicit() { return cast<Expr>(Stmt::IgnoreImplicit()); }

  /// IgnoreParens - Ignore parentheses.  If this Expr is a ParenExpr, return
  ///  its subexpression.  If that subexpression is also a ParenExpr,
  ///  then this method recursively returns its subexpression, and so forth.
  ///  Otherwise, the method returns the current Expr.
  Expr *IgnoreParens();

  /// IgnoreParenCasts - Ignore parentheses and casts.  Strip off any ParenExpr
  /// or CastExprs, returning their operand.
  Expr *IgnoreParenCasts();

  /// IgnoreParenImpCasts - Ignore parentheses and implicit casts.  Strip off any
  /// ParenExpr or ImplicitCastExprs, returning their operand.
  Expr *IgnoreParenImpCasts();

  /// IgnoreConversionOperator - Ignore conversion operator. If this Expr is a
  /// call to a conversion operator, return the argument.
  Expr *IgnoreConversionOperator();

  const Expr *IgnoreConversionOperator() const {
    return const_cast<Expr*>(this)->IgnoreConversionOperator();
  }

  const Expr *IgnoreParenImpCasts() const {
    return const_cast<Expr*>(this)->IgnoreParenImpCasts();
  }
  
  /// Ignore parentheses and lvalue casts.  Strip off any ParenExpr and
  /// CastExprs that represent lvalue casts, returning their operand.
  Expr *IgnoreParenLValueCasts();
  
  const Expr *IgnoreParenLValueCasts() const {
    return const_cast<Expr*>(this)->IgnoreParenLValueCasts();
  }

  /// IgnoreParenNoopCasts - Ignore parentheses and casts that do not change the
  /// value (including ptr->int casts of the same size).  Strip off any
  /// ParenExpr or CastExprs, returning their operand.
  Expr *IgnoreParenNoopCasts(ASTContext &Ctx);

  /// \brief Determine whether this expression is a default function argument.
  ///
  /// Default arguments are implicitly generated in the abstract syntax tree
  /// by semantic analysis for function calls, object constructions, etc. in 
  /// C++. Default arguments are represented by \c CXXDefaultArgExpr nodes;
  /// this routine also looks through any implicit casts to determine whether
  /// the expression is a default argument.
  bool isDefaultArgument() const;
  
  /// \brief Determine whether the result of this expression is a
  /// temporary object of the given class type.
  bool isTemporaryObject(ASTContext &Ctx, const CXXRecordDecl *TempTy) const;

  /// \brief Whether this expression is an implicit reference to 'this' in C++.
  bool isImplicitCXXThis() const;

  const Expr *IgnoreImpCasts() const {
    return const_cast<Expr*>(this)->IgnoreImpCasts();
  }
  const Expr *IgnoreParens() const {
    return const_cast<Expr*>(this)->IgnoreParens();
  }
  const Expr *IgnoreParenCasts() const {
    return const_cast<Expr*>(this)->IgnoreParenCasts();
  }
  const Expr *IgnoreParenNoopCasts(ASTContext &Ctx) const {
    return const_cast<Expr*>(this)->IgnoreParenNoopCasts(Ctx);
  }

  static bool hasAnyTypeDependentArguments(Expr** Exprs, unsigned NumExprs);
  static bool hasAnyValueDependentArguments(Expr** Exprs, unsigned NumExprs);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstExprConstant &&
           T->getStmtClass() <= lastExprConstant;
  }
  static bool classof(const Expr *) { return true; }
};


//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

/// OpaqueValueExpr - An expression referring to an opaque object of a
/// fixed type and value class.  These don't correspond to concrete
/// syntax; instead they're used to express operations (usually copy
/// operations) on values whose source is generally obvious from
/// context.
class OpaqueValueExpr : public Expr {
  friend class ASTStmtReader;
  Expr *SourceExpr;
  SourceLocation Loc;
  
public:
  OpaqueValueExpr(SourceLocation Loc, QualType T, ExprValueKind VK, 
                  ExprObjectKind OK = OK_Ordinary)
    : Expr(OpaqueValueExprClass, T, VK, OK,
           T->isDependentType(), T->isDependentType(), 
           T->isInstantiationDependentType(),
           false), 
      SourceExpr(0), Loc(Loc) {
  }

  /// Given an expression which invokes a copy constructor --- i.e.  a
  /// CXXConstructExpr, possibly wrapped in an ExprWithCleanups ---
  /// find the OpaqueValueExpr that's the source of the construction.
  static const OpaqueValueExpr *findInCopyConstruct(const Expr *expr);

  explicit OpaqueValueExpr(EmptyShell Empty)
    : Expr(OpaqueValueExprClass, Empty) { }

  /// \brief Retrieve the location of this expression.
  SourceLocation getLocation() const { return Loc; }
  
  SourceRange getSourceRange() const {
    if (SourceExpr) return SourceExpr->getSourceRange();
    return Loc;
  }
  SourceLocation getExprLoc() const {
    if (SourceExpr) return SourceExpr->getExprLoc();
    return Loc;
  }

  child_range children() { return child_range(); }

  /// The source expression of an opaque value expression is the
  /// expression which originally generated the value.  This is
  /// provided as a convenience for analyses that don't wish to
  /// precisely model the execution behavior of the program.
  ///
  /// The source expression is typically set when building the
  /// expression which binds the opaque value expression in the first
  /// place.
  Expr *getSourceExpr() const { return SourceExpr; }
  void setSourceExpr(Expr *e) { SourceExpr = e; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OpaqueValueExprClass;
  }
  static bool classof(const OpaqueValueExpr *) { return true; }
};

/// \brief A reference to a declared variable, function, enum, etc.
/// [C99 6.5.1p2]
///
/// This encodes all the information about how a declaration is referenced
/// within an expression.
///
/// There are several optional constructs attached to DeclRefExprs only when
/// they apply in order to conserve memory. These are laid out past the end of
/// the object, and flags in the DeclRefExprBitfield track whether they exist:
///
///   DeclRefExprBits.HasQualifier:
///       Specifies when this declaration reference expression has a C++
///       nested-name-specifier.
///   DeclRefExprBits.HasFoundDecl:
///       Specifies when this declaration reference expression has a record of
///       a NamedDecl (different from the referenced ValueDecl) which was found
///       during name lookup and/or overload resolution.
///   DeclRefExprBits.HasExplicitTemplateArgs:
///       Specifies when this declaration reference expression has an explicit
///       C++ template argument list.
class DeclRefExpr : public Expr {
  /// \brief The declaration that we are referencing.
  ValueDecl *D;

  /// \brief The location of the declaration name itself.
  SourceLocation Loc;

  /// \brief Provides source/type location info for the declaration name
  /// embedded in D.
  DeclarationNameLoc DNLoc;

  /// \brief Helper to retrieve the optional NestedNameSpecifierLoc.
  NestedNameSpecifierLoc &getInternalQualifierLoc() {
    assert(hasQualifier());
    return *reinterpret_cast<NestedNameSpecifierLoc *>(this + 1);
  }

  /// \brief Helper to retrieve the optional NestedNameSpecifierLoc.
  const NestedNameSpecifierLoc &getInternalQualifierLoc() const {
    return const_cast<DeclRefExpr *>(this)->getInternalQualifierLoc();
  }

  /// \brief Test whether there is a distinct FoundDecl attached to the end of
  /// this DRE.
  bool hasFoundDecl() const { return DeclRefExprBits.HasFoundDecl; }

  /// \brief Helper to retrieve the optional NamedDecl through which this
  /// reference occured.
  NamedDecl *&getInternalFoundDecl() {
    assert(hasFoundDecl());
    if (hasQualifier())
      return *reinterpret_cast<NamedDecl **>(&getInternalQualifierLoc() + 1);
    return *reinterpret_cast<NamedDecl **>(this + 1);
  }

  /// \brief Helper to retrieve the optional NamedDecl through which this
  /// reference occured.
  NamedDecl *getInternalFoundDecl() const {
    return const_cast<DeclRefExpr *>(this)->getInternalFoundDecl();
  }

  DeclRefExpr(NestedNameSpecifierLoc QualifierLoc,
              ValueDecl *D, const DeclarationNameInfo &NameInfo,
              NamedDecl *FoundD,
              const TemplateArgumentListInfo *TemplateArgs,
              QualType T, ExprValueKind VK);

  /// \brief Construct an empty declaration reference expression.
  explicit DeclRefExpr(EmptyShell Empty)
    : Expr(DeclRefExprClass, Empty) { }

  /// \brief Computes the type- and value-dependence flags for this
  /// declaration reference expression.
  void computeDependence();

public:
  DeclRefExpr(ValueDecl *D, QualType T, ExprValueKind VK, SourceLocation L,
              const DeclarationNameLoc &LocInfo = DeclarationNameLoc())
    : Expr(DeclRefExprClass, T, VK, OK_Ordinary, false, false, false, false),
      D(D), Loc(L), DNLoc(LocInfo) {
    DeclRefExprBits.HasQualifier = 0;
    DeclRefExprBits.HasExplicitTemplateArgs = 0;
    DeclRefExprBits.HasFoundDecl = 0;
    DeclRefExprBits.HadMultipleCandidates = 0;
    computeDependence();
  }

  static DeclRefExpr *Create(ASTContext &Context,
                             NestedNameSpecifierLoc QualifierLoc,
                             ValueDecl *D,
                             SourceLocation NameLoc,
                             QualType T, ExprValueKind VK,
                             NamedDecl *FoundD = 0,
                             const TemplateArgumentListInfo *TemplateArgs = 0);

  static DeclRefExpr *Create(ASTContext &Context,
                             NestedNameSpecifierLoc QualifierLoc,
                             ValueDecl *D,
                             const DeclarationNameInfo &NameInfo,
                             QualType T, ExprValueKind VK,
                             NamedDecl *FoundD = 0,
                             const TemplateArgumentListInfo *TemplateArgs = 0);

  /// \brief Construct an empty declaration reference expression.
  static DeclRefExpr *CreateEmpty(ASTContext &Context,
                                  bool HasQualifier,
                                  bool HasFoundDecl,
                                  bool HasExplicitTemplateArgs,
                                  unsigned NumTemplateArgs);

  ValueDecl *getDecl() { return D; }
  const ValueDecl *getDecl() const { return D; }
  void setDecl(ValueDecl *NewD) { D = NewD; }

  DeclarationNameInfo getNameInfo() const {
    return DeclarationNameInfo(getDecl()->getDeclName(), Loc, DNLoc);
  }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }
  SourceRange getSourceRange() const;

  /// \brief Determine whether this declaration reference was preceded by a
  /// C++ nested-name-specifier, e.g., \c N::foo.
  bool hasQualifier() const { return DeclRefExprBits.HasQualifier; }

  /// \brief If the name was qualified, retrieves the nested-name-specifier
  /// that precedes the name. Otherwise, returns NULL.
  NestedNameSpecifier *getQualifier() const {
    if (!hasQualifier())
      return 0;

    return getInternalQualifierLoc().getNestedNameSpecifier();
  }

  /// \brief If the name was qualified, retrieves the nested-name-specifier
  /// that precedes the name, with source-location information.
  NestedNameSpecifierLoc getQualifierLoc() const {
    if (!hasQualifier())
      return NestedNameSpecifierLoc();

    return getInternalQualifierLoc();
  }

  /// \brief Get the NamedDecl through which this reference occured.
  ///
  /// This Decl may be different from the ValueDecl actually referred to in the
  /// presence of using declarations, etc. It always returns non-NULL, and may
  /// simple return the ValueDecl when appropriate.
  NamedDecl *getFoundDecl() {
    return hasFoundDecl() ? getInternalFoundDecl() : D;
  }

  /// \brief Get the NamedDecl through which this reference occurred.
  /// See non-const variant.
  const NamedDecl *getFoundDecl() const {
    return hasFoundDecl() ? getInternalFoundDecl() : D;
  }

  /// \brief Determines whether this declaration reference was followed by an
  /// explict template argument list.
  bool hasExplicitTemplateArgs() const {
    return DeclRefExprBits.HasExplicitTemplateArgs;
  }

  /// \brief Retrieve the explicit template argument list that followed the
  /// member template name.
  ASTTemplateArgumentListInfo &getExplicitTemplateArgs() {
    assert(hasExplicitTemplateArgs());
    if (hasFoundDecl())
      return *reinterpret_cast<ASTTemplateArgumentListInfo *>(
        &getInternalFoundDecl() + 1);

    if (hasQualifier())
      return *reinterpret_cast<ASTTemplateArgumentListInfo *>(
        &getInternalQualifierLoc() + 1);

    return *reinterpret_cast<ASTTemplateArgumentListInfo *>(this + 1);
  }

  /// \brief Retrieve the explicit template argument list that followed the
  /// member template name.
  const ASTTemplateArgumentListInfo &getExplicitTemplateArgs() const {
    return const_cast<DeclRefExpr *>(this)->getExplicitTemplateArgs();
  }

  /// \brief Retrieves the optional explicit template arguments.
  /// This points to the same data as getExplicitTemplateArgs(), but
  /// returns null if there are no explicit template arguments.
  const ASTTemplateArgumentListInfo *getExplicitTemplateArgsOpt() const {
    if (!hasExplicitTemplateArgs()) return 0;
    return &getExplicitTemplateArgs();
  }

  /// \brief Copies the template arguments (if present) into the given
  /// structure.
  void copyTemplateArgumentsInto(TemplateArgumentListInfo &List) const {
    if (hasExplicitTemplateArgs())
      getExplicitTemplateArgs().copyInto(List);
  }

  /// \brief Retrieve the location of the left angle bracket following the
  /// member name ('<'), if any.
  SourceLocation getLAngleLoc() const {
    if (!hasExplicitTemplateArgs())
      return SourceLocation();

    return getExplicitTemplateArgs().LAngleLoc;
  }

  /// \brief Retrieve the template arguments provided as part of this
  /// template-id.
  const TemplateArgumentLoc *getTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return 0;

    return getExplicitTemplateArgs().getTemplateArgs();
  }

  /// \brief Retrieve the number of template arguments provided as part of this
  /// template-id.
  unsigned getNumTemplateArgs() const {
    if (!hasExplicitTemplateArgs())
      return 0;

    return getExplicitTemplateArgs().NumTemplateArgs;
  }

  /// \brief Retrieve the location of the right angle bracket following the
  /// template arguments ('>').
  SourceLocation getRAngleLoc() const {
    if (!hasExplicitTemplateArgs())
      return SourceLocation();

    return getExplicitTemplateArgs().RAngleLoc;
  }

  /// \brief Returns true if this expression refers to a function that
  /// was resolved from an overloaded set having size greater than 1.
  bool hadMultipleCandidates() const {
    return DeclRefExprBits.HadMultipleCandidates;
  }
  /// \brief Sets the flag telling whether this expression refers to
  /// a function that was resolved from an overloaded set having size
  /// greater than 1.
  void setHadMultipleCandidates(bool V = true) {
    DeclRefExprBits.HadMultipleCandidates = V;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DeclRefExprClass;
  }
  static bool classof(const DeclRefExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }

  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// PredefinedExpr - [C99 6.4.2.2] - A predefined identifier such as __func__.
class PredefinedExpr : public Expr {
public:
  enum IdentType {
    Func,
    Function,
    PrettyFunction,
    /// PrettyFunctionNoVirtual - The same as PrettyFunction, except that the
    /// 'virtual' keyword is omitted for virtual member functions.
    PrettyFunctionNoVirtual
  };

private:
  SourceLocation Loc;
  IdentType Type;
public:
  PredefinedExpr(SourceLocation l, QualType type, IdentType IT)
    : Expr(PredefinedExprClass, type, VK_LValue, OK_Ordinary,
           type->isDependentType(), type->isDependentType(), 
           type->isInstantiationDependentType(),
           /*ContainsUnexpandedParameterPack=*/false),
      Loc(l), Type(IT) {}

  /// \brief Construct an empty predefined expression.
  explicit PredefinedExpr(EmptyShell Empty)
    : Expr(PredefinedExprClass, Empty) { }

  IdentType getIdentType() const { return Type; }
  void setIdentType(IdentType IT) { Type = IT; }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  static std::string ComputeName(IdentType IT, const Decl *CurrentDecl);

  SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == PredefinedExprClass;
  }
  static bool classof(const PredefinedExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// \brief Used by IntegerLiteral/FloatingLiteral to store the numeric without
/// leaking memory.
///
/// For large floats/integers, APFloat/APInt will allocate memory from the heap
/// to represent these numbers.  Unfortunately, when we use a BumpPtrAllocator
/// to allocate IntegerLiteral/FloatingLiteral nodes the memory associated with
/// the APFloat/APInt values will never get freed. APNumericStorage uses
/// ASTContext's allocator for memory allocation.
class APNumericStorage {
  unsigned BitWidth;
  union {
    uint64_t VAL;    ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal;  ///< Used to store the >64 bits integer value.
  };

  bool hasAllocation() const { return llvm::APInt::getNumWords(BitWidth) > 1; }

  APNumericStorage(const APNumericStorage&); // do not implement
  APNumericStorage& operator=(const APNumericStorage&); // do not implement

protected:
  APNumericStorage() : BitWidth(0), VAL(0) { }

  llvm::APInt getIntValue() const {
    unsigned NumWords = llvm::APInt::getNumWords(BitWidth);
    if (NumWords > 1)
      return llvm::APInt(BitWidth, NumWords, pVal);
    else
      return llvm::APInt(BitWidth, VAL);
  }
  void setIntValue(ASTContext &C, const llvm::APInt &Val);
};

class APIntStorage : public APNumericStorage {
public:  
  llvm::APInt getValue() const { return getIntValue(); } 
  void setValue(ASTContext &C, const llvm::APInt &Val) { setIntValue(C, Val); }
};

class APFloatStorage : public APNumericStorage {
public:  
  llvm::APFloat getValue() const { return llvm::APFloat(getIntValue()); } 
  void setValue(ASTContext &C, const llvm::APFloat &Val) {
    setIntValue(C, Val.bitcastToAPInt());
  }
};

class IntegerLiteral : public Expr {
  APIntStorage Num;
  SourceLocation Loc;

  /// \brief Construct an empty integer literal.
  explicit IntegerLiteral(EmptyShell Empty)
    : Expr(IntegerLiteralClass, Empty) { }

public:
  // type should be IntTy, LongTy, LongLongTy, UnsignedIntTy, UnsignedLongTy,
  // or UnsignedLongLongTy
  IntegerLiteral(ASTContext &C, const llvm::APInt &V,
                 QualType type, SourceLocation l)
    : Expr(IntegerLiteralClass, type, VK_RValue, OK_Ordinary, false, false,
           false, false),
      Loc(l) {
    assert(type->isIntegerType() && "Illegal type in IntegerLiteral");
    assert(V.getBitWidth() == C.getIntWidth(type) &&
           "Integer type is not the correct size for constant.");
    setValue(C, V);
  }

  /// \brief Returns a new integer literal with value 'V' and type 'type'.
  /// \param type - either IntTy, LongTy, LongLongTy, UnsignedIntTy,
  /// UnsignedLongTy, or UnsignedLongLongTy which should match the size of V
  /// \param V - the value that the returned integer literal contains.
  static IntegerLiteral *Create(ASTContext &C, const llvm::APInt &V,
                                QualType type, SourceLocation l);
  /// \brief Returns a new empty integer literal.
  static IntegerLiteral *Create(ASTContext &C, EmptyShell Empty);

  llvm::APInt getValue() const { return Num.getValue(); }
  SourceRange getSourceRange() const { return SourceRange(Loc); }

  /// \brief Retrieve the location of the literal.
  SourceLocation getLocation() const { return Loc; }

  void setValue(ASTContext &C, const llvm::APInt &Val) { Num.setValue(C, Val); }
  void setLocation(SourceLocation Location) { Loc = Location; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == IntegerLiteralClass;
  }
  static bool classof(const IntegerLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

class CharacterLiteral : public Expr {
public:
  enum CharacterKind {
    Ascii,
    Wide,
    UTF16,
    UTF32
  };

private:
  unsigned Value;
  SourceLocation Loc;
  unsigned Kind : 2;
public:
  // type should be IntTy
  CharacterLiteral(unsigned value, CharacterKind kind, QualType type,
                   SourceLocation l)
    : Expr(CharacterLiteralClass, type, VK_RValue, OK_Ordinary, false, false,
           false, false),
      Value(value), Loc(l), Kind(kind) {
  }

  /// \brief Construct an empty character literal.
  CharacterLiteral(EmptyShell Empty) : Expr(CharacterLiteralClass, Empty) { }

  SourceLocation getLocation() const { return Loc; }
  CharacterKind getKind() const { return static_cast<CharacterKind>(Kind); }

  SourceRange getSourceRange() const { return SourceRange(Loc); }

  unsigned getValue() const { return Value; }

  void setLocation(SourceLocation Location) { Loc = Location; }
  void setKind(CharacterKind kind) { Kind = kind; }
  void setValue(unsigned Val) { Value = Val; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CharacterLiteralClass;
  }
  static bool classof(const CharacterLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

class FloatingLiteral : public Expr {
  APFloatStorage Num;
  bool IsExact : 1;
  SourceLocation Loc;

  FloatingLiteral(ASTContext &C, const llvm::APFloat &V, bool isexact,
                  QualType Type, SourceLocation L)
    : Expr(FloatingLiteralClass, Type, VK_RValue, OK_Ordinary, false, false,
           false, false),
      IsExact(isexact), Loc(L) {
    setValue(C, V);
  }

  /// \brief Construct an empty floating-point literal.
  explicit FloatingLiteral(EmptyShell Empty)
    : Expr(FloatingLiteralClass, Empty), IsExact(false) { }

public:
  static FloatingLiteral *Create(ASTContext &C, const llvm::APFloat &V,
                                 bool isexact, QualType Type, SourceLocation L);
  static FloatingLiteral *Create(ASTContext &C, EmptyShell Empty);

  llvm::APFloat getValue() const { return Num.getValue(); }
  void setValue(ASTContext &C, const llvm::APFloat &Val) {
    Num.setValue(C, Val);
  }

  bool isExact() const { return IsExact; }
  void setExact(bool E) { IsExact = E; }

  /// getValueAsApproximateDouble - This returns the value as an inaccurate
  /// double.  Note that this may cause loss of precision, but is useful for
  /// debugging dumps, etc.
  double getValueAsApproximateDouble() const;

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceRange getSourceRange() const { return SourceRange(Loc); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == FloatingLiteralClass;
  }
  static bool classof(const FloatingLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// ImaginaryLiteral - We support imaginary integer and floating point literals,
/// like "1.0i".  We represent these as a wrapper around FloatingLiteral and
/// IntegerLiteral classes.  Instances of this class always have a Complex type
/// whose element type matches the subexpression.
///
class ImaginaryLiteral : public Expr {
  Stmt *Val;
public:
  ImaginaryLiteral(Expr *val, QualType Ty)
    : Expr(ImaginaryLiteralClass, Ty, VK_RValue, OK_Ordinary, false, false,
           false, false),
      Val(val) {}

  /// \brief Build an empty imaginary literal.
  explicit ImaginaryLiteral(EmptyShell Empty)
    : Expr(ImaginaryLiteralClass, Empty) { }

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceRange getSourceRange() const { return Val->getSourceRange(); }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImaginaryLiteralClass;
  }
  static bool classof(const ImaginaryLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
};

/// StringLiteral - This represents a string literal expression, e.g. "foo"
/// or L"bar" (wide strings).  The actual string is returned by getStrData()
/// is NOT null-terminated, and the length of the string is determined by
/// calling getByteLength().  The C type for a string is always a
/// ConstantArrayType.  In C++, the char type is const qualified, in C it is
/// not.
///
/// Note that strings in C can be formed by concatenation of multiple string
/// literal pptokens in translation phase #6.  This keeps track of the locations
/// of each of these pieces.
///
/// Strings in C can also be truncated and extended by assigning into arrays,
/// e.g. with constructs like:
///   char X[2] = "foobar";
/// In this case, getByteLength() will return 6, but the string literal will
/// have type "char[2]".
class StringLiteral : public Expr {
public:
  enum StringKind {
    Ascii,
    Wide,
    UTF8,
    UTF16,
    UTF32
  };

private:
  friend class ASTStmtReader;

  const char *StrData;
  unsigned ByteLength;
  unsigned NumConcatenated;
  unsigned Kind : 3;
  bool IsPascal : 1;
  SourceLocation TokLocs[1];

  StringLiteral(QualType Ty) :
    Expr(StringLiteralClass, Ty, VK_LValue, OK_Ordinary, false, false, false,
         false) {}

public:
  /// This is the "fully general" constructor that allows representation of
  /// strings formed from multiple concatenated tokens.
  static StringLiteral *Create(ASTContext &C, StringRef Str, StringKind Kind,
                               bool Pascal, QualType Ty,
                               const SourceLocation *Loc, unsigned NumStrs);

  /// Simple constructor for string literals made from one token.
  static StringLiteral *Create(ASTContext &C, StringRef Str, StringKind Kind,
                               bool Pascal, QualType Ty,
                               SourceLocation Loc) {
    return Create(C, Str, Kind, Pascal, Ty, &Loc, 1);
  }

  /// \brief Construct an empty string literal.
  static StringLiteral *CreateEmpty(ASTContext &C, unsigned NumStrs);

  StringRef getString() const {
    return StringRef(StrData, ByteLength);
  }

  unsigned getByteLength() const { return ByteLength; }

  /// \brief Sets the string data to the given string data.
  void setString(ASTContext &C, StringRef Str);

  StringKind getKind() const { return static_cast<StringKind>(Kind); }
  bool isAscii() const { return Kind == Ascii; }
  bool isWide() const { return Kind == Wide; }
  bool isUTF8() const { return Kind == UTF8; }
  bool isUTF16() const { return Kind == UTF16; }
  bool isUTF32() const { return Kind == UTF32; }
  bool isPascal() const { return IsPascal; }

  bool containsNonAsciiOrNull() const {
    StringRef Str = getString();
    for (unsigned i = 0, e = Str.size(); i != e; ++i)
      if (!isascii(Str[i]) || !Str[i])
        return true;
    return false;
  }
  /// getNumConcatenated - Get the number of string literal tokens that were
  /// concatenated in translation phase #6 to form this string literal.
  unsigned getNumConcatenated() const { return NumConcatenated; }

  SourceLocation getStrTokenLoc(unsigned TokNum) const {
    assert(TokNum < NumConcatenated && "Invalid tok number");
    return TokLocs[TokNum];
  }
  void setStrTokenLoc(unsigned TokNum, SourceLocation L) {
    assert(TokNum < NumConcatenated && "Invalid tok number");
    TokLocs[TokNum] = L;
  }
  
  /// getLocationOfByte - Return a source location that points to the specified
  /// byte of this string literal.
  ///
  /// Strings are amazingly complex.  They can be formed from multiple tokens
  /// and can have escape sequences in them in addition to the usual trigraph
  /// and escaped newline business.  This routine handles this complexity.
  ///
  SourceLocation getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                                   const LangOptions &Features,
                                   const TargetInfo &Target) const;

  typedef const SourceLocation *tokloc_iterator;
  tokloc_iterator tokloc_begin() const { return TokLocs; }
  tokloc_iterator tokloc_end() const { return TokLocs+NumConcatenated; }

  SourceRange getSourceRange() const {
    return SourceRange(TokLocs[0], TokLocs[NumConcatenated-1]);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StringLiteralClass;
  }
  static bool classof(const StringLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// ParenExpr - This represents a parethesized expression, e.g. "(1)".  This
/// AST node is only formed if full location information is requested.
class ParenExpr : public Expr {
  SourceLocation L, R;
  Stmt *Val;
public:
  ParenExpr(SourceLocation l, SourceLocation r, Expr *val)
    : Expr(ParenExprClass, val->getType(),
           val->getValueKind(), val->getObjectKind(),
           val->isTypeDependent(), val->isValueDependent(),
           val->isInstantiationDependent(),
           val->containsUnexpandedParameterPack()),
      L(l), R(r), Val(val) {}

  /// \brief Construct an empty parenthesized expression.
  explicit ParenExpr(EmptyShell Empty)
    : Expr(ParenExprClass, Empty) { }

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  SourceRange getSourceRange() const { return SourceRange(L, R); }

  /// \brief Get the location of the left parentheses '('.
  SourceLocation getLParen() const { return L; }
  void setLParen(SourceLocation Loc) { L = Loc; }

  /// \brief Get the location of the right parentheses ')'.
  SourceLocation getRParen() const { return R; }
  void setRParen(SourceLocation Loc) { R = Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenExprClass;
  }
  static bool classof(const ParenExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
};


/// UnaryOperator - This represents the unary-expression's (except sizeof and
/// alignof), the postinc/postdec operators from postfix-expression, and various
/// extensions.
///
/// Notes on various nodes:
///
/// Real/Imag - These return the real/imag part of a complex operand.  If
///   applied to a non-complex value, the former returns its operand and the
///   later returns zero in the type of the operand.
///
class UnaryOperator : public Expr {
public:
  typedef UnaryOperatorKind Opcode;

private:
  unsigned Opc : 5;
  SourceLocation Loc;
  Stmt *Val;
public:

  UnaryOperator(Expr *input, Opcode opc, QualType type,
                ExprValueKind VK, ExprObjectKind OK, SourceLocation l)
    : Expr(UnaryOperatorClass, type, VK, OK,
           input->isTypeDependent() || type->isDependentType(),
           input->isValueDependent(),
           (input->isInstantiationDependent() || 
            type->isInstantiationDependentType()),
           input->containsUnexpandedParameterPack()),
      Opc(opc), Loc(l), Val(input) {}

  /// \brief Build an empty unary operator.
  explicit UnaryOperator(EmptyShell Empty)
    : Expr(UnaryOperatorClass, Empty), Opc(UO_AddrOf) { }

  Opcode getOpcode() const { return static_cast<Opcode>(Opc); }
  void setOpcode(Opcode O) { Opc = O; }

  Expr *getSubExpr() const { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  /// getOperatorLoc - Return the location of the operator.
  SourceLocation getOperatorLoc() const { return Loc; }
  void setOperatorLoc(SourceLocation L) { Loc = L; }

  /// isPostfix - Return true if this is a postfix operation, like x++.
  static bool isPostfix(Opcode Op) {
    return Op == UO_PostInc || Op == UO_PostDec;
  }

  /// isPrefix - Return true if this is a prefix operation, like --x.
  static bool isPrefix(Opcode Op) {
    return Op == UO_PreInc || Op == UO_PreDec;
  }

  bool isPrefix() const { return isPrefix(getOpcode()); }
  bool isPostfix() const { return isPostfix(getOpcode()); }
  bool isIncrementOp() const {
    return Opc == UO_PreInc || Opc == UO_PostInc;
  }
  bool isIncrementDecrementOp() const {
    return Opc <= UO_PreDec;
  }
  static bool isArithmeticOp(Opcode Op) {
    return Op >= UO_Plus && Op <= UO_LNot;
  }
  bool isArithmeticOp() const { return isArithmeticOp(getOpcode()); }

  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "sizeof" or "[pre]++"
  static const char *getOpcodeStr(Opcode Op);

  /// \brief Retrieve the unary opcode that corresponds to the given
  /// overloaded operator.
  static Opcode getOverloadedOpcode(OverloadedOperatorKind OO, bool Postfix);

  /// \brief Retrieve the overloaded operator kind that corresponds to
  /// the given unary opcode.
  static OverloadedOperatorKind getOverloadedOperator(Opcode Opc);

  SourceRange getSourceRange() const {
    if (isPostfix())
      return SourceRange(Val->getLocStart(), Loc);
    else
      return SourceRange(Loc, Val->getLocEnd());
  }
  SourceLocation getExprLoc() const { return Loc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryOperatorClass;
  }
  static bool classof(const UnaryOperator *) { return true; }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
};

/// OffsetOfExpr - [C99 7.17] - This represents an expression of the form
/// offsetof(record-type, member-designator). For example, given:
/// @code
/// struct S {
///   float f;
///   double d;    
/// };
/// struct T {
///   int i;
///   struct S s[10];
/// };
/// @endcode
/// we can represent and evaluate the expression @c offsetof(struct T, s[2].d). 

class OffsetOfExpr : public Expr {
public:
  // __builtin_offsetof(type, identifier(.identifier|[expr])*)
  class OffsetOfNode {
  public:
    /// \brief The kind of offsetof node we have.
    enum Kind {
      /// \brief An index into an array.
      Array = 0x00,
      /// \brief A field.
      Field = 0x01,
      /// \brief A field in a dependent type, known only by its name.
      Identifier = 0x02,
      /// \brief An implicit indirection through a C++ base class, when the
      /// field found is in a base class.
      Base = 0x03
    };

  private:
    enum { MaskBits = 2, Mask = 0x03 };
    
    /// \brief The source range that covers this part of the designator.
    SourceRange Range;
    
    /// \brief The data describing the designator, which comes in three
    /// different forms, depending on the lower two bits.
    ///   - An unsigned index into the array of Expr*'s stored after this node 
    ///     in memory, for [constant-expression] designators.
    ///   - A FieldDecl*, for references to a known field.
    ///   - An IdentifierInfo*, for references to a field with a given name
    ///     when the class type is dependent.
    ///   - A CXXBaseSpecifier*, for references that look at a field in a 
    ///     base class.
    uintptr_t Data;
    
  public:
    /// \brief Create an offsetof node that refers to an array element.
    OffsetOfNode(SourceLocation LBracketLoc, unsigned Index, 
                 SourceLocation RBracketLoc)
      : Range(LBracketLoc, RBracketLoc), Data((Index << 2) | Array) { }
    
    /// \brief Create an offsetof node that refers to a field.
    OffsetOfNode(SourceLocation DotLoc, FieldDecl *Field, 
                 SourceLocation NameLoc)
      : Range(DotLoc.isValid()? DotLoc : NameLoc, NameLoc), 
        Data(reinterpret_cast<uintptr_t>(Field) | OffsetOfNode::Field) { }
    
    /// \brief Create an offsetof node that refers to an identifier.
    OffsetOfNode(SourceLocation DotLoc, IdentifierInfo *Name,
                 SourceLocation NameLoc)
      : Range(DotLoc.isValid()? DotLoc : NameLoc, NameLoc), 
        Data(reinterpret_cast<uintptr_t>(Name) | Identifier) { }

    /// \brief Create an offsetof node that refers into a C++ base class.
    explicit OffsetOfNode(const CXXBaseSpecifier *Base)
      : Range(), Data(reinterpret_cast<uintptr_t>(Base) | OffsetOfNode::Base) {}
    
    /// \brief Determine what kind of offsetof node this is.
    Kind getKind() const { 
      return static_cast<Kind>(Data & Mask);
    }
    
    /// \brief For an array element node, returns the index into the array
    /// of expressions.
    unsigned getArrayExprIndex() const {
      assert(getKind() == Array);
      return Data >> 2;
    }

    /// \brief For a field offsetof node, returns the field.
    FieldDecl *getField() const {
      assert(getKind() == Field);
      return reinterpret_cast<FieldDecl *>(Data & ~(uintptr_t)Mask);
    }
    
    /// \brief For a field or identifier offsetof node, returns the name of
    /// the field.
    IdentifierInfo *getFieldName() const;
    
    /// \brief For a base class node, returns the base specifier.
    CXXBaseSpecifier *getBase() const {
      assert(getKind() == Base);
      return reinterpret_cast<CXXBaseSpecifier *>(Data & ~(uintptr_t)Mask);      
    }
    
    /// \brief Retrieve the source range that covers this offsetof node.
    ///
    /// For an array element node, the source range contains the locations of
    /// the square brackets. For a field or identifier node, the source range
    /// contains the location of the period (if there is one) and the 
    /// identifier.
    SourceRange getSourceRange() const { return Range; }
  };

private:
  
  SourceLocation OperatorLoc, RParenLoc;
  // Base type;
  TypeSourceInfo *TSInfo;
  // Number of sub-components (i.e. instances of OffsetOfNode).
  unsigned NumComps;
  // Number of sub-expressions (i.e. array subscript expressions).
  unsigned NumExprs;
  
  OffsetOfExpr(ASTContext &C, QualType type, 
               SourceLocation OperatorLoc, TypeSourceInfo *tsi,
               OffsetOfNode* compsPtr, unsigned numComps, 
               Expr** exprsPtr, unsigned numExprs,
               SourceLocation RParenLoc);

  explicit OffsetOfExpr(unsigned numComps, unsigned numExprs)
    : Expr(OffsetOfExprClass, EmptyShell()),
      TSInfo(0), NumComps(numComps), NumExprs(numExprs) {}  

public:
  
  static OffsetOfExpr *Create(ASTContext &C, QualType type, 
                              SourceLocation OperatorLoc, TypeSourceInfo *tsi, 
                              OffsetOfNode* compsPtr, unsigned numComps, 
                              Expr** exprsPtr, unsigned numExprs,
                              SourceLocation RParenLoc);

  static OffsetOfExpr *CreateEmpty(ASTContext &C, 
                                   unsigned NumComps, unsigned NumExprs);

  /// getOperatorLoc - Return the location of the operator.
  SourceLocation getOperatorLoc() const { return OperatorLoc; }
  void setOperatorLoc(SourceLocation L) { OperatorLoc = L; }

  /// \brief Return the location of the right parentheses.
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation R) { RParenLoc = R; }
  
  TypeSourceInfo *getTypeSourceInfo() const {
    return TSInfo;
  }
  void setTypeSourceInfo(TypeSourceInfo *tsi) {
    TSInfo = tsi;
  }
  
  const OffsetOfNode &getComponent(unsigned Idx) const {
    assert(Idx < NumComps && "Subscript out of range");
    return reinterpret_cast<const OffsetOfNode *> (this + 1)[Idx];
  }

  void setComponent(unsigned Idx, OffsetOfNode ON) {
    assert(Idx < NumComps && "Subscript out of range");
    reinterpret_cast<OffsetOfNode *> (this + 1)[Idx] = ON;
  }
  
  unsigned getNumComponents() const {
    return NumComps;
  }

  Expr* getIndexExpr(unsigned Idx) {
    assert(Idx < NumExprs && "Subscript out of range");
    return reinterpret_cast<Expr **>(
                    reinterpret_cast<OffsetOfNode *>(this+1) + NumComps)[Idx];
  }
  const Expr *getIndexExpr(unsigned Idx) const {
    return const_cast<OffsetOfExpr*>(this)->getIndexExpr(Idx);
  }

  void setIndexExpr(unsigned Idx, Expr* E) {
    assert(Idx < NumComps && "Subscript out of range");
    reinterpret_cast<Expr **>(
                reinterpret_cast<OffsetOfNode *>(this+1) + NumComps)[Idx] = E;
  }
  
  unsigned getNumExpressions() const {
    return NumExprs;
  }

  SourceRange getSourceRange() const {
    return SourceRange(OperatorLoc, RParenLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OffsetOfExprClass;
  }

  static bool classof(const OffsetOfExpr *) { return true; }

  // Iterators
  child_range children() {
    Stmt **begin =
      reinterpret_cast<Stmt**>(reinterpret_cast<OffsetOfNode*>(this + 1)
                               + NumComps);
    return child_range(begin, begin + NumExprs);
  }
};

/// UnaryExprOrTypeTraitExpr - expression with either a type or (unevaluated)
/// expression operand.  Used for sizeof/alignof (C99 6.5.3.4) and
/// vec_step (OpenCL 1.1 6.11.12).
class UnaryExprOrTypeTraitExpr : public Expr {
  unsigned Kind : 2;
  bool isType : 1;    // true if operand is a type, false if an expression
  union {
    TypeSourceInfo *Ty;
    Stmt *Ex;
  } Argument;
  SourceLocation OpLoc, RParenLoc;

public:
  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, TypeSourceInfo *TInfo,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp) :
      Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_RValue, OK_Ordinary,
           false, // Never type-dependent (C++ [temp.dep.expr]p3).
           // Value-dependent if the argument is type-dependent.
           TInfo->getType()->isDependentType(),
           TInfo->getType()->isInstantiationDependentType(),
           TInfo->getType()->containsUnexpandedParameterPack()),
      Kind(ExprKind), isType(true), OpLoc(op), RParenLoc(rp) {
    Argument.Ty = TInfo;
  }

  UnaryExprOrTypeTraitExpr(UnaryExprOrTypeTrait ExprKind, Expr *E,
                           QualType resultType, SourceLocation op,
                           SourceLocation rp) :
      Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_RValue, OK_Ordinary,
           false, // Never type-dependent (C++ [temp.dep.expr]p3).
           // Value-dependent if the argument is type-dependent.
           E->isTypeDependent(),
           E->isInstantiationDependent(),
           E->containsUnexpandedParameterPack()),
      Kind(ExprKind), isType(false), OpLoc(op), RParenLoc(rp) {
    Argument.Ex = E;
  }

  /// \brief Construct an empty sizeof/alignof expression.
  explicit UnaryExprOrTypeTraitExpr(EmptyShell Empty)
    : Expr(UnaryExprOrTypeTraitExprClass, Empty) { }

  UnaryExprOrTypeTrait getKind() const {
    return static_cast<UnaryExprOrTypeTrait>(Kind);
  }
  void setKind(UnaryExprOrTypeTrait K) { Kind = K; }

  bool isArgumentType() const { return isType; }
  QualType getArgumentType() const {
    return getArgumentTypeInfo()->getType();
  }
  TypeSourceInfo *getArgumentTypeInfo() const {
    assert(isArgumentType() && "calling getArgumentType() when arg is expr");
    return Argument.Ty;
  }
  Expr *getArgumentExpr() {
    assert(!isArgumentType() && "calling getArgumentExpr() when arg is type");
    return static_cast<Expr*>(Argument.Ex);
  }
  const Expr *getArgumentExpr() const {
    return const_cast<UnaryExprOrTypeTraitExpr*>(this)->getArgumentExpr();
  }

  void setArgument(Expr *E) { Argument.Ex = E; isType = false; }
  void setArgument(TypeSourceInfo *TInfo) {
    Argument.Ty = TInfo;
    isType = true;
  }

  /// Gets the argument type, or the type of the argument expression, whichever
  /// is appropriate.
  QualType getTypeOfArgument() const {
    return isArgumentType() ? getArgumentType() : getArgumentExpr()->getType();
  }

  SourceLocation getOperatorLoc() const { return OpLoc; }
  void setOperatorLoc(SourceLocation L) { OpLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(OpLoc, RParenLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == UnaryExprOrTypeTraitExprClass;
  }
  static bool classof(const UnaryExprOrTypeTraitExpr *) { return true; }

  // Iterators
  child_range children();
};

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

/// ArraySubscriptExpr - [C99 6.5.2.1] Array Subscripting.
class ArraySubscriptExpr : public Expr {
  enum { LHS, RHS, END_EXPR=2 };
  Stmt* SubExprs[END_EXPR];
  SourceLocation RBracketLoc;
public:
  ArraySubscriptExpr(Expr *lhs, Expr *rhs, QualType t,
                     ExprValueKind VK, ExprObjectKind OK,
                     SourceLocation rbracketloc)
  : Expr(ArraySubscriptExprClass, t, VK, OK,
         lhs->isTypeDependent() || rhs->isTypeDependent(),
         lhs->isValueDependent() || rhs->isValueDependent(),
         (lhs->isInstantiationDependent() ||
          rhs->isInstantiationDependent()),
         (lhs->containsUnexpandedParameterPack() ||
          rhs->containsUnexpandedParameterPack())),
    RBracketLoc(rbracketloc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }

  /// \brief Create an empty array subscript expression.
  explicit ArraySubscriptExpr(EmptyShell Shell)
    : Expr(ArraySubscriptExprClass, Shell) { }

  /// An array access can be written A[4] or 4[A] (both are equivalent).
  /// - getBase() and getIdx() always present the normalized view: A[4].
  ///    In this case getBase() returns "A" and getIdx() returns "4".
  /// - getLHS() and getRHS() present the syntactic view. e.g. for
  ///    4[A] getLHS() returns "4".
  /// Note: Because vector element access is also written A[4] we must
  /// predicate the format conversion in getBase and getIdx only on the
  /// the type of the RHS, as it is possible for the LHS to be a vector of
  /// integer type
  Expr *getLHS() { return cast<Expr>(SubExprs[LHS]); }
  const Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }

  Expr *getRHS() { return cast<Expr>(SubExprs[RHS]); }
  const Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  Expr *getBase() {
    return cast<Expr>(getRHS()->getType()->isIntegerType() ? getLHS():getRHS());
  }

  const Expr *getBase() const {
    return cast<Expr>(getRHS()->getType()->isIntegerType() ? getLHS():getRHS());
  }

  Expr *getIdx() {
    return cast<Expr>(getRHS()->getType()->isIntegerType() ? getRHS():getLHS());
  }

  const Expr *getIdx() const {
    return cast<Expr>(getRHS()->getType()->isIntegerType() ? getRHS():getLHS());
  }

  SourceRange getSourceRange() const {
    return SourceRange(getLHS()->getLocStart(), RBracketLoc);
  }

  SourceLocation getRBracketLoc() const { return RBracketLoc; }
  void setRBracketLoc(SourceLocation L) { RBracketLoc = L; }

  SourceLocation getExprLoc() const { return getBase()->getExprLoc(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ArraySubscriptExprClass;
  }
  static bool classof(const ArraySubscriptExpr *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
};


/// CallExpr - Represents a function call (C99 6.5.2.2, C++ [expr.call]).
/// CallExpr itself represents a normal function call, e.g., "f(x, 2)",
/// while its subclasses may represent alternative syntax that (semantically)
/// results in a function call. For example, CXXOperatorCallExpr is
/// a subclass for overloaded operator calls that use operator syntax, e.g.,
/// "str1 + str2" to resolve to a function call.
class CallExpr : public Expr {
  enum { FN=0, PREARGS_START=1 };
  Stmt **SubExprs;
  unsigned NumArgs;
  SourceLocation RParenLoc;

protected:
  // These versions of the constructor are for derived classes.
  CallExpr(ASTContext& C, StmtClass SC, Expr *fn, unsigned NumPreArgs,
           Expr **args, unsigned numargs, QualType t, ExprValueKind VK,
           SourceLocation rparenloc);
  CallExpr(ASTContext &C, StmtClass SC, unsigned NumPreArgs, EmptyShell Empty);

  Stmt *getPreArg(unsigned i) {
    assert(i < getNumPreArgs() && "Prearg access out of range!");
    return SubExprs[PREARGS_START+i];
  }
  const Stmt *getPreArg(unsigned i) const {
    assert(i < getNumPreArgs() && "Prearg access out of range!");
    return SubExprs[PREARGS_START+i];
  }
  void setPreArg(unsigned i, Stmt *PreArg) {
    assert(i < getNumPreArgs() && "Prearg access out of range!");
    SubExprs[PREARGS_START+i] = PreArg;
  }

  unsigned getNumPreArgs() const { return CallExprBits.NumPreArgs; }

public:
  CallExpr(ASTContext& C, Expr *fn, Expr **args, unsigned numargs, QualType t,
           ExprValueKind VK, SourceLocation rparenloc);

  /// \brief Build an empty call expression.
  CallExpr(ASTContext &C, StmtClass SC, EmptyShell Empty);

  const Expr *getCallee() const { return cast<Expr>(SubExprs[FN]); }
  Expr *getCallee() { return cast<Expr>(SubExprs[FN]); }
  void setCallee(Expr *F) { SubExprs[FN] = F; }

  Decl *getCalleeDecl();
  const Decl *getCalleeDecl() const {
    return const_cast<CallExpr*>(this)->getCalleeDecl();
  }

  /// \brief If the callee is a FunctionDecl, return it. Otherwise return 0.
  FunctionDecl *getDirectCallee();
  const FunctionDecl *getDirectCallee() const {
    return const_cast<CallExpr*>(this)->getDirectCallee();
  }

  /// getNumArgs - Return the number of actual arguments to this call.
  ///
  unsigned getNumArgs() const { return NumArgs; }

  /// \brief Retrieve the call arguments.
  Expr **getArgs() {
    return reinterpret_cast<Expr **>(SubExprs+getNumPreArgs()+PREARGS_START);
  }
  
  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return cast<Expr>(SubExprs[Arg+getNumPreArgs()+PREARGS_START]);
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return cast<Expr>(SubExprs[Arg+getNumPreArgs()+PREARGS_START]);
  }

  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    SubExprs[Arg+getNumPreArgs()+PREARGS_START] = ArgExpr;
  }

  /// setNumArgs - This changes the number of arguments present in this call.
  /// Any orphaned expressions are deleted by this, and any new operands are set
  /// to null.
  void setNumArgs(ASTContext& C, unsigned NumArgs);

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;

  arg_iterator arg_begin() { return SubExprs+PREARGS_START+getNumPreArgs(); }
  arg_iterator arg_end() {
    return SubExprs+PREARGS_START+getNumPreArgs()+getNumArgs();
  }
  const_arg_iterator arg_begin() const {
    return SubExprs+PREARGS_START+getNumPreArgs();
  }
  const_arg_iterator arg_end() const {
    return SubExprs+PREARGS_START+getNumPreArgs()+getNumArgs();
  }

  /// getNumCommas - Return the number of commas that must have been present in
  /// this function call.
  unsigned getNumCommas() const { return NumArgs ? NumArgs - 1 : 0; }

  /// isBuiltinCall - If this is a call to a builtin, return the builtin ID.  If
  /// not, return 0.
  unsigned isBuiltinCall(const ASTContext &Context) const;

  /// getCallReturnType - Get the return type of the call expr. This is not
  /// always the type of the expr itself, if the return type is a reference
  /// type.
  QualType getCallReturnType() const;

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstCallExprConstant &&
           T->getStmtClass() <= lastCallExprConstant;
  }
  static bool classof(const CallExpr *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0],
                       &SubExprs[0]+NumArgs+getNumPreArgs()+PREARGS_START);
  }
};

/// MemberExpr - [C99 6.5.2.3] Structure and Union Members.  X->F and X.F.
///
class MemberExpr : public Expr {
  /// Extra data stored in some member expressions.
  struct MemberNameQualifier {
    /// \brief The nested-name-specifier that qualifies the name, including
    /// source-location information.
    NestedNameSpecifierLoc QualifierLoc;

    /// \brief The DeclAccessPair through which the MemberDecl was found due to
    /// name qualifiers.
    DeclAccessPair FoundDecl;
  };

  /// Base - the expression for the base pointer or structure references.  In
  /// X.F, this is "X".
  Stmt *Base;

  /// MemberDecl - This is the decl being referenced by the field/member name.
  /// In X.F, this is the decl referenced by F.
  ValueDecl *MemberDecl;

  /// MemberLoc - This is the location of the member name.
  SourceLocation MemberLoc;

  /// MemberDNLoc - Provides source/type location info for the
  /// declaration name embedded in MemberDecl.
  DeclarationNameLoc MemberDNLoc;

  /// IsArrow - True if this is "X->F", false if this is "X.F".
  bool IsArrow : 1;

  /// \brief True if this member expression used a nested-name-specifier to
  /// refer to the member, e.g., "x->Base::f", or found its member via a using
  /// declaration.  When true, a MemberNameQualifier
  /// structure is allocated immediately after the MemberExpr.
  bool HasQualifierOrFoundDecl : 1;

  /// \brief True if this member expression specified a template argument list
  /// explicitly, e.g., x->f<int>. When true, an ExplicitTemplateArgumentList
  /// structure (and its TemplateArguments) are allocated immediately after
  /// the MemberExpr or, if the member expression also has a qualifier, after
  /// the MemberNameQualifier structure.
  bool HasExplicitTemplateArgumentList : 1;

  /// \brief True if this member expression refers to a method that
  /// was resolved from an overloaded set having size greater than 1.
  bool HadMultipleCandidates : 1;

  /// \brief Retrieve the qualifier that preceded the member name, if any.
  MemberNameQualifier *getMemberQualifier() {
    assert(HasQualifierOrFoundDecl);
    return reinterpret_cast<MemberNameQualifier *> (this + 1);
  }

  /// \brief Retrieve the qualifier that preceded the member name, if any.
  const MemberNameQualifier *getMemberQualifier() const {
    return const_cast<MemberExpr *>(this)->getMemberQualifier();
  }

public:
  MemberExpr(Expr *base, bool isarrow, ValueDecl *memberdecl,
             const DeclarationNameInfo &NameInfo, QualType ty,
             ExprValueKind VK, ExprObjectKind OK)
    : Expr(MemberExprClass, ty, VK, OK,
           base->isTypeDependent(), 
           base->isValueDependent(),
           base->isInstantiationDependent(),
           base->containsUnexpandedParameterPack()),
      Base(base), MemberDecl(memberdecl), MemberLoc(NameInfo.getLoc()),
      MemberDNLoc(NameInfo.getInfo()), IsArrow(isarrow),
      HasQualifierOrFoundDecl(false), HasExplicitTemplateArgumentList(false),
      HadMultipleCandidates(false) {
    assert(memberdecl->getDeclName() == NameInfo.getName());
  }

  // NOTE: this constructor should be used only when it is known that
  // the member name can not provide additional syntactic info
  // (i.e., source locations for C++ operator names or type source info
  // for constructors, destructors and conversion oeprators).
  MemberExpr(Expr *base, bool isarrow, ValueDecl *memberdecl,
             SourceLocation l, QualType ty,
             ExprValueKind VK, ExprObjectKind OK)
    : Expr(MemberExprClass, ty, VK, OK,
           base->isTypeDependent(), base->isValueDependent(),
           base->isInstantiationDependent(),
           base->containsUnexpandedParameterPack()),
      Base(base), MemberDecl(memberdecl), MemberLoc(l), MemberDNLoc(),
      IsArrow(isarrow),
      HasQualifierOrFoundDecl(false), HasExplicitTemplateArgumentList(false),
      HadMultipleCandidates(false) {}

  static MemberExpr *Create(ASTContext &C, Expr *base, bool isarrow,
                            NestedNameSpecifierLoc QualifierLoc,
                            ValueDecl *memberdecl, DeclAccessPair founddecl,
                            DeclarationNameInfo MemberNameInfo,
                            const TemplateArgumentListInfo *targs,
                            QualType ty, ExprValueKind VK, ExprObjectKind OK);

  void setBase(Expr *E) { Base = E; }
  Expr *getBase() const { return cast<Expr>(Base); }

  /// \brief Retrieve the member declaration to which this expression refers.
  ///
  /// The returned declaration will either be a FieldDecl or (in C++)
  /// a CXXMethodDecl.
  ValueDecl *getMemberDecl() const { return MemberDecl; }
  void setMemberDecl(ValueDecl *D) { MemberDecl = D; }

  /// \brief Retrieves the declaration found by lookup.
  DeclAccessPair getFoundDecl() const {
    if (!HasQualifierOrFoundDecl)
      return DeclAccessPair::make(getMemberDecl(),
                                  getMemberDecl()->getAccess());
    return getMemberQualifier()->FoundDecl;
  }

  /// \brief Determines whether this member expression actually had
  /// a C++ nested-name-specifier prior to the name of the member, e.g.,
  /// x->Base::foo.
  bool hasQualifier() const { return getQualifier() != 0; }

  /// \brief If the member name was qualified, retrieves the
  /// nested-name-specifier that precedes the member name. Otherwise, returns
  /// NULL.
  NestedNameSpecifier *getQualifier() const {
    if (!HasQualifierOrFoundDecl)
      return 0;

    return getMemberQualifier()->QualifierLoc.getNestedNameSpecifier();
  }

  /// \brief If the member name was qualified, retrieves the 
  /// nested-name-specifier that precedes the member name, with source-location
  /// information.
  NestedNameSpecifierLoc getQualifierLoc() const {
    if (!hasQualifier())
      return NestedNameSpecifierLoc();
    
    return getMemberQualifier()->QualifierLoc;
  }

  /// \brief Determines whether this member expression actually had a C++
  /// template argument list explicitly specified, e.g., x.f<int>.
  bool hasExplicitTemplateArgs() const {
    return HasExplicitTemplateArgumentList;
  }

  /// \brief Copies the template arguments (if present) into the given
  /// structure.
  void copyTemplateArgumentsInto(TemplateArgumentListInfo &List) const {
    if (hasExplicitTemplateArgs())
      getExplicitTemplateArgs().copyInto(List);
  }

  /// \brief Retrieve the explicit template argument list that
  /// follow the member template name.  This must only be called on an
  /// expression with explicit template arguments.
  ASTTemplateArgumentListInfo &getExplicitTemplateArgs() {
    assert(HasExplicitTemplateArgumentList);
    if (!HasQualifierOrFoundDecl)
      return *reinterpret_cast<ASTTemplateArgumentListInfo *>(this + 1);

    return *reinterpret_cast<ASTTemplateArgumentListInfo *>(
                                                      getMemberQualifier() + 1);
  }

  /// \brief Retrieve the explicit template argument list that
  /// followed the member template name.  This must only be called on
  /// an expression with explicit template arguments.
  const ASTTemplateArgumentListInfo &getExplicitTemplateArgs() const {
    return const_cast<MemberExpr *>(this)->getExplicitTemplateArgs();
  }

  /// \brief Retrieves the optional explicit template arguments.
  /// This points to the same data as getExplicitTemplateArgs(), but
  /// returns null if there are no explicit template arguments.
  const ASTTemplateArgumentListInfo *getOptionalExplicitTemplateArgs() const {
    if (!hasExplicitTemplateArgs()) return 0;
    return &getExplicitTemplateArgs();
  }
  
  /// \brief Retrieve the location of the left angle bracket following the
  /// member name ('<'), if any.
  SourceLocation getLAngleLoc() const {
    if (!HasExplicitTemplateArgumentList)
      return SourceLocation();

    return getExplicitTemplateArgs().LAngleLoc;
  }

  /// \brief Retrieve the template arguments provided as part of this
  /// template-id.
  const TemplateArgumentLoc *getTemplateArgs() const {
    if (!HasExplicitTemplateArgumentList)
      return 0;

    return getExplicitTemplateArgs().getTemplateArgs();
  }

  /// \brief Retrieve the number of template arguments provided as part of this
  /// template-id.
  unsigned getNumTemplateArgs() const {
    if (!HasExplicitTemplateArgumentList)
      return 0;

    return getExplicitTemplateArgs().NumTemplateArgs;
  }

  /// \brief Retrieve the location of the right angle bracket following the
  /// template arguments ('>').
  SourceLocation getRAngleLoc() const {
    if (!HasExplicitTemplateArgumentList)
      return SourceLocation();

    return getExplicitTemplateArgs().RAngleLoc;
  }

  /// \brief Retrieve the member declaration name info.
  DeclarationNameInfo getMemberNameInfo() const {
    return DeclarationNameInfo(MemberDecl->getDeclName(),
                               MemberLoc, MemberDNLoc);
  }

  bool isArrow() const { return IsArrow; }
  void setArrow(bool A) { IsArrow = A; }

  /// getMemberLoc - Return the location of the "member", in X->F, it is the
  /// location of 'F'.
  SourceLocation getMemberLoc() const { return MemberLoc; }
  void setMemberLoc(SourceLocation L) { MemberLoc = L; }

  SourceRange getSourceRange() const;
  
  SourceLocation getExprLoc() const { return MemberLoc; }

  /// \brief Determine whether the base of this explicit is implicit.
  bool isImplicitAccess() const {
    return getBase() && getBase()->isImplicitCXXThis();
  }

  /// \brief Returns true if this member expression refers to a method that
  /// was resolved from an overloaded set having size greater than 1.
  bool hadMultipleCandidates() const {
    return HadMultipleCandidates;
  }
  /// \brief Sets the flag telling whether this expression refers to
  /// a method that was resolved from an overloaded set having size
  /// greater than 1.
  void setHadMultipleCandidates(bool V = true) {
    HadMultipleCandidates = V;
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == MemberExprClass;
  }
  static bool classof(const MemberExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }

  friend class ASTReader;
  friend class ASTStmtWriter;
};

/// CompoundLiteralExpr - [C99 6.5.2.5]
///
class CompoundLiteralExpr : public Expr {
  /// LParenLoc - If non-null, this is the location of the left paren in a
  /// compound literal like "(int){4}".  This can be null if this is a
  /// synthesized compound expression.
  SourceLocation LParenLoc;

  /// The type as written.  This can be an incomplete array type, in
  /// which case the actual expression type will be different.
  TypeSourceInfo *TInfo;
  Stmt *Init;
  bool FileScope;
public:
  CompoundLiteralExpr(SourceLocation lparenloc, TypeSourceInfo *tinfo,
                      QualType T, ExprValueKind VK, Expr *init, bool fileScope)
    : Expr(CompoundLiteralExprClass, T, VK, OK_Ordinary,
           tinfo->getType()->isDependentType(), 
           init->isValueDependent(),
           (init->isInstantiationDependent() ||
            tinfo->getType()->isInstantiationDependentType()),
           init->containsUnexpandedParameterPack()),
      LParenLoc(lparenloc), TInfo(tinfo), Init(init), FileScope(fileScope) {}

  /// \brief Construct an empty compound literal.
  explicit CompoundLiteralExpr(EmptyShell Empty)
    : Expr(CompoundLiteralExprClass, Empty) { }

  const Expr *getInitializer() const { return cast<Expr>(Init); }
  Expr *getInitializer() { return cast<Expr>(Init); }
  void setInitializer(Expr *E) { Init = E; }

  bool isFileScope() const { return FileScope; }
  void setFileScope(bool FS) { FileScope = FS; }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }

  TypeSourceInfo *getTypeSourceInfo() const { return TInfo; }
  void setTypeSourceInfo(TypeSourceInfo* tinfo) { TInfo = tinfo; }

  SourceRange getSourceRange() const {
    // FIXME: Init should never be null.
    if (!Init)
      return SourceRange();
    if (LParenLoc.isInvalid())
      return Init->getSourceRange();
    return SourceRange(LParenLoc, Init->getLocEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CompoundLiteralExprClass;
  }
  static bool classof(const CompoundLiteralExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Init, &Init+1); }
};

/// CastExpr - Base class for type casts, including both implicit
/// casts (ImplicitCastExpr) and explicit casts that have some
/// representation in the source code (ExplicitCastExpr's derived
/// classes).
class CastExpr : public Expr {
public:
  typedef clang::CastKind CastKind;

private:
  Stmt *Op;

  void CheckCastConsistency() const;

  const CXXBaseSpecifier * const *path_buffer() const {
    return const_cast<CastExpr*>(this)->path_buffer();
  }
  CXXBaseSpecifier **path_buffer();

  void setBasePathSize(unsigned basePathSize) {
    CastExprBits.BasePathSize = basePathSize;
    assert(CastExprBits.BasePathSize == basePathSize &&
           "basePathSize doesn't fit in bits of CastExprBits.BasePathSize!");
  }

protected:
  CastExpr(StmtClass SC, QualType ty, ExprValueKind VK,
           const CastKind kind, Expr *op, unsigned BasePathSize) :
    Expr(SC, ty, VK, OK_Ordinary,
         // Cast expressions are type-dependent if the type is
         // dependent (C++ [temp.dep.expr]p3).
         ty->isDependentType(),
         // Cast expressions are value-dependent if the type is
         // dependent or if the subexpression is value-dependent.
         ty->isDependentType() || (op && op->isValueDependent()),
         (ty->isInstantiationDependentType() || 
          (op && op->isInstantiationDependent())),
         (ty->containsUnexpandedParameterPack() ||
          op->containsUnexpandedParameterPack())),
    Op(op) {
    assert(kind != CK_Invalid && "creating cast with invalid cast kind");
    CastExprBits.Kind = kind;
    setBasePathSize(BasePathSize);
#ifndef NDEBUG
    CheckCastConsistency();
#endif
  }

  /// \brief Construct an empty cast.
  CastExpr(StmtClass SC, EmptyShell Empty, unsigned BasePathSize)
    : Expr(SC, Empty) {
    setBasePathSize(BasePathSize);
  }

public:
  CastKind getCastKind() const { return (CastKind) CastExprBits.Kind; }
  void setCastKind(CastKind K) { CastExprBits.Kind = K; }
  const char *getCastKindName() const;

  Expr *getSubExpr() { return cast<Expr>(Op); }
  const Expr *getSubExpr() const { return cast<Expr>(Op); }
  void setSubExpr(Expr *E) { Op = E; }

  /// \brief Retrieve the cast subexpression as it was written in the source
  /// code, looking through any implicit casts or other intermediate nodes
  /// introduced by semantic analysis.
  Expr *getSubExprAsWritten();
  const Expr *getSubExprAsWritten() const {
    return const_cast<CastExpr *>(this)->getSubExprAsWritten();
  }

  typedef CXXBaseSpecifier **path_iterator;
  typedef const CXXBaseSpecifier * const *path_const_iterator;
  bool path_empty() const { return CastExprBits.BasePathSize == 0; }
  unsigned path_size() const { return CastExprBits.BasePathSize; }
  path_iterator path_begin() { return path_buffer(); }
  path_iterator path_end() { return path_buffer() + path_size(); }
  path_const_iterator path_begin() const { return path_buffer(); }
  path_const_iterator path_end() const { return path_buffer() + path_size(); }

  void setCastPath(const CXXCastPath &Path);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() >= firstCastExprConstant &&
           T->getStmtClass() <= lastCastExprConstant;
  }
  static bool classof(const CastExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Op, &Op+1); }
};

/// ImplicitCastExpr - Allows us to explicitly represent implicit type
/// conversions, which have no direct representation in the original
/// source code. For example: converting T[]->T*, void f()->void
/// (*f)(), float->double, short->int, etc.
///
/// In C, implicit casts always produce rvalues. However, in C++, an
/// implicit cast whose result is being bound to a reference will be
/// an lvalue or xvalue. For example:
///
/// @code
/// class Base { };
/// class Derived : public Base { };
/// Derived &&ref();
/// void f(Derived d) {
///   Base& b = d; // initializer is an ImplicitCastExpr
///                // to an lvalue of type Base
///   Base&& r = ref(); // initializer is an ImplicitCastExpr
///                     // to an xvalue of type Base
/// }
/// @endcode
class ImplicitCastExpr : public CastExpr {
private:
  ImplicitCastExpr(QualType ty, CastKind kind, Expr *op,
                   unsigned BasePathLength, ExprValueKind VK)
    : CastExpr(ImplicitCastExprClass, ty, VK, kind, op, BasePathLength) {
  }

  /// \brief Construct an empty implicit cast.
  explicit ImplicitCastExpr(EmptyShell Shell, unsigned PathSize)
    : CastExpr(ImplicitCastExprClass, Shell, PathSize) { }

public:
  enum OnStack_t { OnStack };
  ImplicitCastExpr(OnStack_t _, QualType ty, CastKind kind, Expr *op,
                   ExprValueKind VK)
    : CastExpr(ImplicitCastExprClass, ty, VK, kind, op, 0) {
  }

  static ImplicitCastExpr *Create(ASTContext &Context, QualType T,
                                  CastKind Kind, Expr *Operand,
                                  const CXXCastPath *BasePath,
                                  ExprValueKind Cat);

  static ImplicitCastExpr *CreateEmpty(ASTContext &Context, unsigned PathSize);

  SourceRange getSourceRange() const {
    return getSubExpr()->getSourceRange();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitCastExprClass;
  }
  static bool classof(const ImplicitCastExpr *) { return true; }
};

inline Expr *Expr::IgnoreImpCasts() {
  Expr *e = this;
  while (ImplicitCastExpr *ice = dyn_cast<ImplicitCastExpr>(e))
    e = ice->getSubExpr();
  return e;
}

/// ExplicitCastExpr - An explicit cast written in the source
/// code.
///
/// This class is effectively an abstract class, because it provides
/// the basic representation of an explicitly-written cast without
/// specifying which kind of cast (C cast, functional cast, static
/// cast, etc.) was written; specific derived classes represent the
/// particular style of cast and its location information.
///
/// Unlike implicit casts, explicit cast nodes have two different
/// types: the type that was written into the source code, and the
/// actual type of the expression as determined by semantic
/// analysis. These types may differ slightly. For example, in C++ one
/// can cast to a reference type, which indicates that the resulting
/// expression will be an lvalue or xvalue. The reference type, however,
/// will not be used as the type of the expression.
class ExplicitCastExpr : public CastExpr {
  /// TInfo - Source type info for the (written) type
  /// this expression is casting to.
  TypeSourceInfo *TInfo;

protected:
  ExplicitCastExpr(StmtClass SC, QualType exprTy, ExprValueKind VK,
                   CastKind kind, Expr *op, unsigned PathSize,
                   TypeSourceInfo *writtenTy)
    : CastExpr(SC, exprTy, VK, kind, op, PathSize), TInfo(writtenTy) {}

  /// \brief Construct an empty explicit cast.
  ExplicitCastExpr(StmtClass SC, EmptyShell Shell, unsigned PathSize)
    : CastExpr(SC, Shell, PathSize) { }

public:
  /// getTypeInfoAsWritten - Returns the type source info for the type
  /// that this expression is casting to.
  TypeSourceInfo *getTypeInfoAsWritten() const { return TInfo; }
  void setTypeInfoAsWritten(TypeSourceInfo *writtenTy) { TInfo = writtenTy; }

  /// getTypeAsWritten - Returns the type that this expression is
  /// casting to, as written in the source code.
  QualType getTypeAsWritten() const { return TInfo->getType(); }

  static bool classof(const Stmt *T) {
     return T->getStmtClass() >= firstExplicitCastExprConstant &&
            T->getStmtClass() <= lastExplicitCastExprConstant;
  }
  static bool classof(const ExplicitCastExpr *) { return true; }
};

/// CStyleCastExpr - An explicit cast in C (C99 6.5.4) or a C-style
/// cast in C++ (C++ [expr.cast]), which uses the syntax
/// (Type)expr. For example: @c (int)f.
class CStyleCastExpr : public ExplicitCastExpr {
  SourceLocation LPLoc; // the location of the left paren
  SourceLocation RPLoc; // the location of the right paren

  CStyleCastExpr(QualType exprTy, ExprValueKind vk, CastKind kind, Expr *op,
                 unsigned PathSize, TypeSourceInfo *writtenTy,
                 SourceLocation l, SourceLocation r)
    : ExplicitCastExpr(CStyleCastExprClass, exprTy, vk, kind, op, PathSize,
                       writtenTy), LPLoc(l), RPLoc(r) {}

  /// \brief Construct an empty C-style explicit cast.
  explicit CStyleCastExpr(EmptyShell Shell, unsigned PathSize)
    : ExplicitCastExpr(CStyleCastExprClass, Shell, PathSize) { }

public:
  static CStyleCastExpr *Create(ASTContext &Context, QualType T,
                                ExprValueKind VK, CastKind K,
                                Expr *Op, const CXXCastPath *BasePath,
                                TypeSourceInfo *WrittenTy, SourceLocation L,
                                SourceLocation R);

  static CStyleCastExpr *CreateEmpty(ASTContext &Context, unsigned PathSize);

  SourceLocation getLParenLoc() const { return LPLoc; }
  void setLParenLoc(SourceLocation L) { LPLoc = L; }

  SourceLocation getRParenLoc() const { return RPLoc; }
  void setRParenLoc(SourceLocation L) { RPLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(LPLoc, getSubExpr()->getSourceRange().getEnd());
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == CStyleCastExprClass;
  }
  static bool classof(const CStyleCastExpr *) { return true; }
};

/// \brief A builtin binary operation expression such as "x + y" or "x <= y".
///
/// This expression node kind describes a builtin binary operation,
/// such as "x + y" for integer values "x" and "y". The operands will
/// already have been converted to appropriate types (e.g., by
/// performing promotions or conversions).
///
/// In C++, where operators may be overloaded, a different kind of
/// expression node (CXXOperatorCallExpr) is used to express the
/// invocation of an overloaded operator with operator syntax. Within
/// a C++ template, whether BinaryOperator or CXXOperatorCallExpr is
/// used to store an expression "x + y" depends on the subexpressions
/// for x and y. If neither x or y is type-dependent, and the "+"
/// operator resolves to a built-in operation, BinaryOperator will be
/// used to express the computation (x and y may still be
/// value-dependent). If either x or y is type-dependent, or if the
/// "+" resolves to an overloaded operator, CXXOperatorCallExpr will
/// be used to express the computation.
class BinaryOperator : public Expr {
public:
  typedef BinaryOperatorKind Opcode;

private:
  unsigned Opc : 6;
  SourceLocation OpLoc;

  enum { LHS, RHS, END_EXPR };
  Stmt* SubExprs[END_EXPR];
public:

  BinaryOperator(Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
                 ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc)
    : Expr(BinaryOperatorClass, ResTy, VK, OK,
           lhs->isTypeDependent() || rhs->isTypeDependent(),
           lhs->isValueDependent() || rhs->isValueDependent(),
           (lhs->isInstantiationDependent() ||
            rhs->isInstantiationDependent()),
           (lhs->containsUnexpandedParameterPack() ||
            rhs->containsUnexpandedParameterPack())),
      Opc(opc), OpLoc(opLoc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
    assert(!isCompoundAssignmentOp() &&
           "Use ArithAssignBinaryOperator for compound assignments");
  }

  /// \brief Construct an empty binary operator.
  explicit BinaryOperator(EmptyShell Empty)
    : Expr(BinaryOperatorClass, Empty), Opc(BO_Comma) { }

  SourceLocation getOperatorLoc() const { return OpLoc; }
  void setOperatorLoc(SourceLocation L) { OpLoc = L; }

  Opcode getOpcode() const { return static_cast<Opcode>(Opc); }
  void setOpcode(Opcode O) { Opc = O; }

  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceRange getSourceRange() const {
    return SourceRange(getLHS()->getLocStart(), getRHS()->getLocEnd());
  }

  /// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
  /// corresponds to, e.g. "<<=".
  static const char *getOpcodeStr(Opcode Op);

  const char *getOpcodeStr() const { return getOpcodeStr(getOpcode()); }

  /// \brief Retrieve the binary opcode that corresponds to the given
  /// overloaded operator.
  static Opcode getOverloadedOpcode(OverloadedOperatorKind OO);

  /// \brief Retrieve the overloaded operator kind that corresponds to
  /// the given binary opcode.
  static OverloadedOperatorKind getOverloadedOperator(Opcode Opc);

  /// predicates to categorize the respective opcodes.
  bool isPtrMemOp() const { return Opc == BO_PtrMemD || Opc == BO_PtrMemI; }
  bool isMultiplicativeOp() const { return Opc >= BO_Mul && Opc <= BO_Rem; }
  static bool isAdditiveOp(Opcode Opc) { return Opc == BO_Add || Opc==BO_Sub; }
  bool isAdditiveOp() const { return isAdditiveOp(getOpcode()); }
  static bool isShiftOp(Opcode Opc) { return Opc == BO_Shl || Opc == BO_Shr; }
  bool isShiftOp() const { return isShiftOp(getOpcode()); }

  static bool isBitwiseOp(Opcode Opc) { return Opc >= BO_And && Opc <= BO_Or; }
  bool isBitwiseOp() const { return isBitwiseOp(getOpcode()); }

  static bool isRelationalOp(Opcode Opc) { return Opc >= BO_LT && Opc<=BO_GE; }
  bool isRelationalOp() const { return isRelationalOp(getOpcode()); }

  static bool isEqualityOp(Opcode Opc) { return Opc == BO_EQ || Opc == BO_NE; }
  bool isEqualityOp() const { return isEqualityOp(getOpcode()); }

  static bool isComparisonOp(Opcode Opc) { return Opc >= BO_LT && Opc<=BO_NE; }
  bool isComparisonOp() const { return isComparisonOp(getOpcode()); }

  static bool isLogicalOp(Opcode Opc) { return Opc == BO_LAnd || Opc==BO_LOr; }
  bool isLogicalOp() const { return isLogicalOp(getOpcode()); }

  static bool isAssignmentOp(Opcode Opc) {
    return Opc >= BO_Assign && Opc <= BO_OrAssign;
  }
  bool isAssignmentOp() const { return isAssignmentOp(getOpcode()); }

  static bool isCompoundAssignmentOp(Opcode Opc) {
    return Opc > BO_Assign && Opc <= BO_OrAssign;
  }
  bool isCompoundAssignmentOp() const {
    return isCompoundAssignmentOp(getOpcode());
  }

  static bool isShiftAssignOp(Opcode Opc) {
    return Opc == BO_ShlAssign || Opc == BO_ShrAssign;
  }
  bool isShiftAssignOp() const {
    return isShiftAssignOp(getOpcode());
  }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() >= firstBinaryOperatorConstant &&
           S->getStmtClass() <= lastBinaryOperatorConstant;
  }
  static bool classof(const BinaryOperator *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }

protected:
  BinaryOperator(Expr *lhs, Expr *rhs, Opcode opc, QualType ResTy,
                 ExprValueKind VK, ExprObjectKind OK,
                 SourceLocation opLoc, bool dead)
    : Expr(CompoundAssignOperatorClass, ResTy, VK, OK,
           lhs->isTypeDependent() || rhs->isTypeDependent(),
           lhs->isValueDependent() || rhs->isValueDependent(),
           (lhs->isInstantiationDependent() ||
            rhs->isInstantiationDependent()),
           (lhs->containsUnexpandedParameterPack() ||
            rhs->containsUnexpandedParameterPack())),
      Opc(opc), OpLoc(opLoc) {
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }

  BinaryOperator(StmtClass SC, EmptyShell Empty)
    : Expr(SC, Empty), Opc(BO_MulAssign) { }
};

/// CompoundAssignOperator - For compound assignments (e.g. +=), we keep
/// track of the type the operation is performed in.  Due to the semantics of
/// these operators, the operands are promoted, the arithmetic performed, an
/// implicit conversion back to the result type done, then the assignment takes
/// place.  This captures the intermediate type which the computation is done
/// in.
class CompoundAssignOperator : public BinaryOperator {
  QualType ComputationLHSType;
  QualType ComputationResultType;
public:
  CompoundAssignOperator(Expr *lhs, Expr *rhs, Opcode opc, QualType ResType,
                         ExprValueKind VK, ExprObjectKind OK,
                         QualType CompLHSType, QualType CompResultType,
                         SourceLocation OpLoc)
    : BinaryOperator(lhs, rhs, opc, ResType, VK, OK, OpLoc, true),
      ComputationLHSType(CompLHSType),
      ComputationResultType(CompResultType) {
    assert(isCompoundAssignmentOp() &&
           "Only should be used for compound assignments");
  }

  /// \brief Build an empty compound assignment operator expression.
  explicit CompoundAssignOperator(EmptyShell Empty)
    : BinaryOperator(CompoundAssignOperatorClass, Empty) { }

  // The two computation types are the type the LHS is converted
  // to for the computation and the type of the result; the two are
  // distinct in a few cases (specifically, int+=ptr and ptr-=ptr).
  QualType getComputationLHSType() const { return ComputationLHSType; }
  void setComputationLHSType(QualType T) { ComputationLHSType = T; }

  QualType getComputationResultType() const { return ComputationResultType; }
  void setComputationResultType(QualType T) { ComputationResultType = T; }

  static bool classof(const CompoundAssignOperator *) { return true; }
  static bool classof(const Stmt *S) {
    return S->getStmtClass() == CompoundAssignOperatorClass;
  }
};

/// AbstractConditionalOperator - An abstract base class for
/// ConditionalOperator and BinaryConditionalOperator.
class AbstractConditionalOperator : public Expr {
  SourceLocation QuestionLoc, ColonLoc;
  friend class ASTStmtReader;

protected:
  AbstractConditionalOperator(StmtClass SC, QualType T,
                              ExprValueKind VK, ExprObjectKind OK,
                              bool TD, bool VD, bool ID,
                              bool ContainsUnexpandedParameterPack,
                              SourceLocation qloc,
                              SourceLocation cloc)
    : Expr(SC, T, VK, OK, TD, VD, ID, ContainsUnexpandedParameterPack),
      QuestionLoc(qloc), ColonLoc(cloc) {}

  AbstractConditionalOperator(StmtClass SC, EmptyShell Empty)
    : Expr(SC, Empty) { }

public:
  // getCond - Return the expression representing the condition for
  //   the ?: operator.
  Expr *getCond() const;

  // getTrueExpr - Return the subexpression representing the value of
  //   the expression if the condition evaluates to true.
  Expr *getTrueExpr() const;

  // getFalseExpr - Return the subexpression representing the value of
  //   the expression if the condition evaluates to false.  This is
  //   the same as getRHS.
  Expr *getFalseExpr() const;

  SourceLocation getQuestionLoc() const { return QuestionLoc; }
  SourceLocation getColonLoc() const { return ColonLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass ||
           T->getStmtClass() == BinaryConditionalOperatorClass;
  }
  static bool classof(const AbstractConditionalOperator *) { return true; }
};

/// ConditionalOperator - The ?: ternary operator.  The GNU "missing
/// middle" extension is a BinaryConditionalOperator.
class ConditionalOperator : public AbstractConditionalOperator {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.

  friend class ASTStmtReader;
public:
  ConditionalOperator(Expr *cond, SourceLocation QLoc, Expr *lhs,
                      SourceLocation CLoc, Expr *rhs,
                      QualType t, ExprValueKind VK, ExprObjectKind OK)
    : AbstractConditionalOperator(ConditionalOperatorClass, t, VK, OK,
           // FIXME: the type of the conditional operator doesn't
           // depend on the type of the conditional, but the standard
           // seems to imply that it could. File a bug!
           (lhs->isTypeDependent() || rhs->isTypeDependent()),
           (cond->isValueDependent() || lhs->isValueDependent() ||
            rhs->isValueDependent()),
           (cond->isInstantiationDependent() ||
            lhs->isInstantiationDependent() ||
            rhs->isInstantiationDependent()),
           (cond->containsUnexpandedParameterPack() ||
            lhs->containsUnexpandedParameterPack() ||
            rhs->containsUnexpandedParameterPack()),
                                  QLoc, CLoc) {
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;
  }

  /// \brief Build an empty conditional operator.
  explicit ConditionalOperator(EmptyShell Empty)
    : AbstractConditionalOperator(ConditionalOperatorClass, Empty) { }

  // getCond - Return the expression representing the condition for
  //   the ?: operator.
  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  // getTrueExpr - Return the subexpression representing the value of
  //   the expression if the condition evaluates to true.
  Expr *getTrueExpr() const { return cast<Expr>(SubExprs[LHS]); }

  // getFalseExpr - Return the subexpression representing the value of
  //   the expression if the condition evaluates to false.  This is
  //   the same as getRHS.
  Expr *getFalseExpr() const { return cast<Expr>(SubExprs[RHS]); }
  
  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }

  SourceRange getSourceRange() const {
    return SourceRange(getCond()->getLocStart(), getRHS()->getLocEnd());
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ConditionalOperatorClass;
  }
  static bool classof(const ConditionalOperator *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
};

/// BinaryConditionalOperator - The GNU extension to the conditional
/// operator which allows the middle operand to be omitted.
///
/// This is a different expression kind on the assumption that almost
/// every client ends up needing to know that these are different.
class BinaryConditionalOperator : public AbstractConditionalOperator {
  enum { COMMON, COND, LHS, RHS, NUM_SUBEXPRS };

  /// - the common condition/left-hand-side expression, which will be
  ///   evaluated as the opaque value
  /// - the condition, expressed in terms of the opaque value
  /// - the left-hand-side, expressed in terms of the opaque value
  /// - the right-hand-side
  Stmt *SubExprs[NUM_SUBEXPRS];
  OpaqueValueExpr *OpaqueValue;

  friend class ASTStmtReader;
public:
  BinaryConditionalOperator(Expr *common, OpaqueValueExpr *opaqueValue,
                            Expr *cond, Expr *lhs, Expr *rhs,
                            SourceLocation qloc, SourceLocation cloc,
                            QualType t, ExprValueKind VK, ExprObjectKind OK)
    : AbstractConditionalOperator(BinaryConditionalOperatorClass, t, VK, OK,
           (common->isTypeDependent() || rhs->isTypeDependent()),
           (common->isValueDependent() || rhs->isValueDependent()),
           (common->isInstantiationDependent() ||
            rhs->isInstantiationDependent()),
           (common->containsUnexpandedParameterPack() ||
            rhs->containsUnexpandedParameterPack()),
                                  qloc, cloc),
      OpaqueValue(opaqueValue) {
    SubExprs[COMMON] = common;
    SubExprs[COND] = cond;
    SubExprs[LHS] = lhs;
    SubExprs[RHS] = rhs;

    OpaqueValue->setSourceExpr(common);
  }

  /// \brief Build an empty conditional operator.
  explicit BinaryConditionalOperator(EmptyShell Empty)
    : AbstractConditionalOperator(BinaryConditionalOperatorClass, Empty) { }

  /// \brief getCommon - Return the common expression, written to the
  ///   left of the condition.  The opaque value will be bound to the
  ///   result of this expression.
  Expr *getCommon() const { return cast<Expr>(SubExprs[COMMON]); }

  /// \brief getOpaqueValue - Return the opaque value placeholder.
  OpaqueValueExpr *getOpaqueValue() const { return OpaqueValue; }

  /// \brief getCond - Return the condition expression; this is defined
  ///   in terms of the opaque value.
  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }

  /// \brief getTrueExpr - Return the subexpression which will be
  ///   evaluated if the condition evaluates to true;  this is defined
  ///   in terms of the opaque value.
  Expr *getTrueExpr() const {
    return cast<Expr>(SubExprs[LHS]);
  }

  /// \brief getFalseExpr - Return the subexpression which will be
  ///   evaluated if the condnition evaluates to false; this is
  ///   defined in terms of the opaque value.
  Expr *getFalseExpr() const {
    return cast<Expr>(SubExprs[RHS]);
  }
  
  SourceRange getSourceRange() const {
    return SourceRange(getCommon()->getLocStart(), getFalseExpr()->getLocEnd());
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BinaryConditionalOperatorClass;
  }
  static bool classof(const BinaryConditionalOperator *) { return true; }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs + NUM_SUBEXPRS);
  }
};

inline Expr *AbstractConditionalOperator::getCond() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getCond();
  return cast<BinaryConditionalOperator>(this)->getCond();
}

inline Expr *AbstractConditionalOperator::getTrueExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getTrueExpr();
  return cast<BinaryConditionalOperator>(this)->getTrueExpr();
}

inline Expr *AbstractConditionalOperator::getFalseExpr() const {
  if (const ConditionalOperator *co = dyn_cast<ConditionalOperator>(this))
    return co->getFalseExpr();
  return cast<BinaryConditionalOperator>(this)->getFalseExpr();
}

/// AddrLabelExpr - The GNU address of label extension, representing &&label.
class AddrLabelExpr : public Expr {
  SourceLocation AmpAmpLoc, LabelLoc;
  LabelDecl *Label;
public:
  AddrLabelExpr(SourceLocation AALoc, SourceLocation LLoc, LabelDecl *L,
                QualType t)
    : Expr(AddrLabelExprClass, t, VK_RValue, OK_Ordinary, false, false, false,
           false),
      AmpAmpLoc(AALoc), LabelLoc(LLoc), Label(L) {}

  /// \brief Build an empty address of a label expression.
  explicit AddrLabelExpr(EmptyShell Empty)
    : Expr(AddrLabelExprClass, Empty) { }

  SourceLocation getAmpAmpLoc() const { return AmpAmpLoc; }
  void setAmpAmpLoc(SourceLocation L) { AmpAmpLoc = L; }
  SourceLocation getLabelLoc() const { return LabelLoc; }
  void setLabelLoc(SourceLocation L) { LabelLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(AmpAmpLoc, LabelLoc);
  }

  LabelDecl *getLabel() const { return Label; }
  void setLabel(LabelDecl *L) { Label = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AddrLabelExprClass;
  }
  static bool classof(const AddrLabelExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// StmtExpr - This is the GNU Statement Expression extension: ({int X=4; X;}).
/// The StmtExpr contains a single CompoundStmt node, which it evaluates and
/// takes the value of the last subexpression.
///
/// A StmtExpr is always an r-value; values "returned" out of a
/// StmtExpr will be copied.
class StmtExpr : public Expr {
  Stmt *SubStmt;
  SourceLocation LParenLoc, RParenLoc;
public:
  // FIXME: Does type-dependence need to be computed differently?
  // FIXME: Do we need to compute instantiation instantiation-dependence for
  // statements? (ugh!)
  StmtExpr(CompoundStmt *substmt, QualType T,
           SourceLocation lp, SourceLocation rp) :
    Expr(StmtExprClass, T, VK_RValue, OK_Ordinary,
         T->isDependentType(), false, false, false),
    SubStmt(substmt), LParenLoc(lp), RParenLoc(rp) { }

  /// \brief Build an empty statement expression.
  explicit StmtExpr(EmptyShell Empty) : Expr(StmtExprClass, Empty) { }

  CompoundStmt *getSubStmt() { return cast<CompoundStmt>(SubStmt); }
  const CompoundStmt *getSubStmt() const { return cast<CompoundStmt>(SubStmt); }
  void setSubStmt(CompoundStmt *S) { SubStmt = S; }

  SourceRange getSourceRange() const {
    return SourceRange(LParenLoc, RParenLoc);
  }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  void setLParenLoc(SourceLocation L) { LParenLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == StmtExprClass;
  }
  static bool classof(const StmtExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&SubStmt, &SubStmt+1); }
};


/// ShuffleVectorExpr - clang-specific builtin-in function
/// __builtin_shufflevector.
/// This AST node represents a operator that does a constant
/// shuffle, similar to LLVM's shufflevector instruction. It takes
/// two vectors and a variable number of constant indices,
/// and returns the appropriately shuffled vector.
class ShuffleVectorExpr : public Expr {
  SourceLocation BuiltinLoc, RParenLoc;

  // SubExprs - the list of values passed to the __builtin_shufflevector
  // function. The first two are vectors, and the rest are constant
  // indices.  The number of values in this list is always
  // 2+the number of indices in the vector type.
  Stmt **SubExprs;
  unsigned NumExprs;

public:
  ShuffleVectorExpr(ASTContext &C, Expr **args, unsigned nexpr,
                    QualType Type, SourceLocation BLoc,
                    SourceLocation RP);

  /// \brief Build an empty vector-shuffle expression.
  explicit ShuffleVectorExpr(EmptyShell Empty)
    : Expr(ShuffleVectorExprClass, Empty), SubExprs(0) { }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ShuffleVectorExprClass;
  }
  static bool classof(const ShuffleVectorExpr *) { return true; }

  /// getNumSubExprs - Return the size of the SubExprs array.  This includes the
  /// constant expression, the actual arguments passed in, and the function
  /// pointers.
  unsigned getNumSubExprs() const { return NumExprs; }

  /// \brief Retrieve the array of expressions.
  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }
  
  /// getExpr - Return the Expr at the specified index.
  Expr *getExpr(unsigned Index) {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }
  const Expr *getExpr(unsigned Index) const {
    assert((Index < NumExprs) && "Arg access out of range!");
    return cast<Expr>(SubExprs[Index]);
  }

  void setExprs(ASTContext &C, Expr ** Exprs, unsigned NumExprs);

  unsigned getShuffleMaskIdx(ASTContext &Ctx, unsigned N) {
    assert((N < NumExprs - 2) && "Shuffle idx out of range!");
    return getExpr(N+2)->EvaluateKnownConstInt(Ctx).getZExtValue();
  }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+NumExprs);
  }
};

/// ChooseExpr - GNU builtin-in function __builtin_choose_expr.
/// This AST node is similar to the conditional operator (?:) in C, with
/// the following exceptions:
/// - the test expression must be a integer constant expression.
/// - the expression returned acts like the chosen subexpression in every
///   visible way: the type is the same as that of the chosen subexpression,
///   and all predicates (whether it's an l-value, whether it's an integer
///   constant expression, etc.) return the same result as for the chosen
///   sub-expression.
class ChooseExpr : public Expr {
  enum { COND, LHS, RHS, END_EXPR };
  Stmt* SubExprs[END_EXPR]; // Left/Middle/Right hand sides.
  SourceLocation BuiltinLoc, RParenLoc;
public:
  ChooseExpr(SourceLocation BLoc, Expr *cond, Expr *lhs, Expr *rhs,
             QualType t, ExprValueKind VK, ExprObjectKind OK,
             SourceLocation RP, bool TypeDependent, bool ValueDependent)
    : Expr(ChooseExprClass, t, VK, OK, TypeDependent, ValueDependent,
           (cond->isInstantiationDependent() ||
            lhs->isInstantiationDependent() ||
            rhs->isInstantiationDependent()),
           (cond->containsUnexpandedParameterPack() ||
            lhs->containsUnexpandedParameterPack() ||
            rhs->containsUnexpandedParameterPack())),
      BuiltinLoc(BLoc), RParenLoc(RP) {
      SubExprs[COND] = cond;
      SubExprs[LHS] = lhs;
      SubExprs[RHS] = rhs;
    }

  /// \brief Build an empty __builtin_choose_expr.
  explicit ChooseExpr(EmptyShell Empty) : Expr(ChooseExprClass, Empty) { }

  /// isConditionTrue - Return whether the condition is true (i.e. not
  /// equal to zero).
  bool isConditionTrue(const ASTContext &C) const;

  /// getChosenSubExpr - Return the subexpression chosen according to the
  /// condition.
  Expr *getChosenSubExpr(const ASTContext &C) const {
    return isConditionTrue(C) ? getLHS() : getRHS();
  }

  Expr *getCond() const { return cast<Expr>(SubExprs[COND]); }
  void setCond(Expr *E) { SubExprs[COND] = E; }
  Expr *getLHS() const { return cast<Expr>(SubExprs[LHS]); }
  void setLHS(Expr *E) { SubExprs[LHS] = E; }
  Expr *getRHS() const { return cast<Expr>(SubExprs[RHS]); }
  void setRHS(Expr *E) { SubExprs[RHS] = E; }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ChooseExprClass;
  }
  static bool classof(const ChooseExpr *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&SubExprs[0], &SubExprs[0]+END_EXPR);
  }
};

/// GNUNullExpr - Implements the GNU __null extension, which is a name
/// for a null pointer constant that has integral type (e.g., int or
/// long) and is the same size and alignment as a pointer. The __null
/// extension is typically only used by system headers, which define
/// NULL as __null in C++ rather than using 0 (which is an integer
/// that may not match the size of a pointer).
class GNUNullExpr : public Expr {
  /// TokenLoc - The location of the __null keyword.
  SourceLocation TokenLoc;

public:
  GNUNullExpr(QualType Ty, SourceLocation Loc)
    : Expr(GNUNullExprClass, Ty, VK_RValue, OK_Ordinary, false, false, false,
           false),
      TokenLoc(Loc) { }

  /// \brief Build an empty GNU __null expression.
  explicit GNUNullExpr(EmptyShell Empty) : Expr(GNUNullExprClass, Empty) { }

  /// getTokenLocation - The location of the __null token.
  SourceLocation getTokenLocation() const { return TokenLoc; }
  void setTokenLocation(SourceLocation L) { TokenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(TokenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GNUNullExprClass;
  }
  static bool classof(const GNUNullExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// VAArgExpr, used for the builtin function __builtin_va_arg.
class VAArgExpr : public Expr {
  Stmt *Val;
  TypeSourceInfo *TInfo;
  SourceLocation BuiltinLoc, RParenLoc;
public:
  VAArgExpr(SourceLocation BLoc, Expr* e, TypeSourceInfo *TInfo,
            SourceLocation RPLoc, QualType t)
    : Expr(VAArgExprClass, t, VK_RValue, OK_Ordinary,
           t->isDependentType(), false,
           (TInfo->getType()->isInstantiationDependentType() ||
            e->isInstantiationDependent()),
           (TInfo->getType()->containsUnexpandedParameterPack() ||
            e->containsUnexpandedParameterPack())),
      Val(e), TInfo(TInfo),
      BuiltinLoc(BLoc),
      RParenLoc(RPLoc) { }

  /// \brief Create an empty __builtin_va_arg expression.
  explicit VAArgExpr(EmptyShell Empty) : Expr(VAArgExprClass, Empty) { }

  const Expr *getSubExpr() const { return cast<Expr>(Val); }
  Expr *getSubExpr() { return cast<Expr>(Val); }
  void setSubExpr(Expr *E) { Val = E; }

  TypeSourceInfo *getWrittenTypeInfo() const { return TInfo; }
  void setWrittenTypeInfo(TypeSourceInfo *TI) { TInfo = TI; }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == VAArgExprClass;
  }
  static bool classof(const VAArgExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Val, &Val+1); }
};

/// @brief Describes an C or C++ initializer list.
///
/// InitListExpr describes an initializer list, which can be used to
/// initialize objects of different types, including
/// struct/class/union types, arrays, and vectors. For example:
///
/// @code
/// struct foo x = { 1, { 2, 3 } };
/// @endcode
///
/// Prior to semantic analysis, an initializer list will represent the
/// initializer list as written by the user, but will have the
/// placeholder type "void". This initializer list is called the
/// syntactic form of the initializer, and may contain C99 designated
/// initializers (represented as DesignatedInitExprs), initializations
/// of subobject members without explicit braces, and so on. Clients
/// interested in the original syntax of the initializer list should
/// use the syntactic form of the initializer list.
///
/// After semantic analysis, the initializer list will represent the
/// semantic form of the initializer, where the initializations of all
/// subobjects are made explicit with nested InitListExpr nodes and
/// C99 designators have been eliminated by placing the designated
/// initializations into the subobject they initialize. Additionally,
/// any "holes" in the initialization, where no initializer has been
/// specified for a particular subobject, will be replaced with
/// implicitly-generated ImplicitValueInitExpr expressions that
/// value-initialize the subobjects. Note, however, that the
/// initializer lists may still have fewer initializers than there are
/// elements to initialize within the object.
///
/// Given the semantic form of the initializer list, one can retrieve
/// the original syntactic form of that initializer list (if it
/// exists) using getSyntacticForm(). Since many initializer lists
/// have the same syntactic and semantic forms, getSyntacticForm() may
/// return NULL, indicating that the current initializer list also
/// serves as its syntactic form.
class InitListExpr : public Expr {
  // FIXME: Eliminate this vector in favor of ASTContext allocation
  typedef ASTVector<Stmt *> InitExprsTy;
  InitExprsTy InitExprs;
  SourceLocation LBraceLoc, RBraceLoc;

  /// Contains the initializer list that describes the syntactic form
  /// written in the source code.
  InitListExpr *SyntacticForm;

  /// \brief Either:
  ///  If this initializer list initializes an array with more elements than
  ///  there are initializers in the list, specifies an expression to be used
  ///  for value initialization of the rest of the elements.
  /// Or
  ///  If this initializer list initializes a union, specifies which
  ///  field within the union will be initialized.
  llvm::PointerUnion<Expr *, FieldDecl *> ArrayFillerOrUnionFieldInit;

  /// Whether this initializer list originally had a GNU array-range
  /// designator in it. This is a temporary marker used by CodeGen.
  bool HadArrayRangeDesignator;

public:
  InitListExpr(ASTContext &C, SourceLocation lbraceloc,
               Expr **initexprs, unsigned numinits,
               SourceLocation rbraceloc);

  /// \brief Build an empty initializer list.
  explicit InitListExpr(ASTContext &C, EmptyShell Empty)
    : Expr(InitListExprClass, Empty), InitExprs(C) { }

  unsigned getNumInits() const { return InitExprs.size(); }

  /// \brief Retrieve the set of initializers.
  Expr **getInits() { return reinterpret_cast<Expr **>(InitExprs.data()); }
    
  const Expr *getInit(unsigned Init) const {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  Expr *getInit(unsigned Init) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    return cast_or_null<Expr>(InitExprs[Init]);
  }

  void setInit(unsigned Init, Expr *expr) {
    assert(Init < getNumInits() && "Initializer access out of range!");
    InitExprs[Init] = expr;
  }

  /// \brief Reserve space for some number of initializers.
  void reserveInits(ASTContext &C, unsigned NumInits);

  /// @brief Specify the number of initializers
  ///
  /// If there are more than @p NumInits initializers, the remaining
  /// initializers will be destroyed. If there are fewer than @p
  /// NumInits initializers, NULL expressions will be added for the
  /// unknown initializers.
  void resizeInits(ASTContext &Context, unsigned NumInits);

  /// @brief Updates the initializer at index @p Init with the new
  /// expression @p expr, and returns the old expression at that
  /// location.
  ///
  /// When @p Init is out of range for this initializer list, the
  /// initializer list will be extended with NULL expressions to
  /// accommodate the new entry.
  Expr *updateInit(ASTContext &C, unsigned Init, Expr *expr);

  /// \brief If this initializer list initializes an array with more elements
  /// than there are initializers in the list, specifies an expression to be
  /// used for value initialization of the rest of the elements.
  Expr *getArrayFiller() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<Expr *>();
  }
  const Expr *getArrayFiller() const {
    return const_cast<InitListExpr *>(this)->getArrayFiller();
  }
  void setArrayFiller(Expr *filler);

  /// \brief If this initializes a union, specifies which field in the
  /// union to initialize.
  ///
  /// Typically, this field is the first named field within the
  /// union. However, a designated initializer can specify the
  /// initialization of a different field within the union.
  FieldDecl *getInitializedFieldInUnion() {
    return ArrayFillerOrUnionFieldInit.dyn_cast<FieldDecl *>();
  }
  const FieldDecl *getInitializedFieldInUnion() const {
    return const_cast<InitListExpr *>(this)->getInitializedFieldInUnion();
  }
  void setInitializedFieldInUnion(FieldDecl *FD) {
    ArrayFillerOrUnionFieldInit = FD;
  }

  // Explicit InitListExpr's originate from source code (and have valid source
  // locations). Implicit InitListExpr's are created by the semantic analyzer.
  bool isExplicit() {
    return LBraceLoc.isValid() && RBraceLoc.isValid();
  }

  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  void setLBraceLoc(SourceLocation Loc) { LBraceLoc = Loc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }
  void setRBraceLoc(SourceLocation Loc) { RBraceLoc = Loc; }

  /// @brief Retrieve the initializer list that describes the
  /// syntactic form of the initializer.
  ///
  ///
  InitListExpr *getSyntacticForm() const { return SyntacticForm; }
  void setSyntacticForm(InitListExpr *Init) { SyntacticForm = Init; }

  bool hadArrayRangeDesignator() const { return HadArrayRangeDesignator; }
  void sawArrayRangeDesignator(bool ARD = true) {
    HadArrayRangeDesignator = ARD;
  }

  SourceRange getSourceRange() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == InitListExprClass;
  }
  static bool classof(const InitListExpr *) { return true; }

  // Iterators
  child_range children() {
    if (InitExprs.empty()) return child_range();
    return child_range(&InitExprs[0], &InitExprs[0] + InitExprs.size());
  }

  typedef InitExprsTy::iterator iterator;
  typedef InitExprsTy::const_iterator const_iterator;
  typedef InitExprsTy::reverse_iterator reverse_iterator;
  typedef InitExprsTy::const_reverse_iterator const_reverse_iterator;

  iterator begin() { return InitExprs.begin(); }
  const_iterator begin() const { return InitExprs.begin(); }
  iterator end() { return InitExprs.end(); }
  const_iterator end() const { return InitExprs.end(); }
  reverse_iterator rbegin() { return InitExprs.rbegin(); }
  const_reverse_iterator rbegin() const { return InitExprs.rbegin(); }
  reverse_iterator rend() { return InitExprs.rend(); }
  const_reverse_iterator rend() const { return InitExprs.rend(); }

  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// @brief Represents a C99 designated initializer expression.
///
/// A designated initializer expression (C99 6.7.8) contains one or
/// more designators (which can be field designators, array
/// designators, or GNU array-range designators) followed by an
/// expression that initializes the field or element(s) that the
/// designators refer to. For example, given:
///
/// @code
/// struct point {
///   double x;
///   double y;
/// };
/// struct point ptarray[10] = { [2].y = 1.0, [2].x = 2.0, [0].x = 1.0 };
/// @endcode
///
/// The InitListExpr contains three DesignatedInitExprs, the first of
/// which covers @c [2].y=1.0. This DesignatedInitExpr will have two
/// designators, one array designator for @c [2] followed by one field
/// designator for @c .y. The initalization expression will be 1.0.
class DesignatedInitExpr : public Expr {
public:
  /// \brief Forward declaration of the Designator class.
  class Designator;

private:
  /// The location of the '=' or ':' prior to the actual initializer
  /// expression.
  SourceLocation EqualOrColonLoc;

  /// Whether this designated initializer used the GNU deprecated
  /// syntax rather than the C99 '=' syntax.
  bool GNUSyntax : 1;

  /// The number of designators in this initializer expression.
  unsigned NumDesignators : 15;

  /// \brief The designators in this designated initialization
  /// expression.
  Designator *Designators;

  /// The number of subexpressions of this initializer expression,
  /// which contains both the initializer and any additional
  /// expressions used by array and array-range designators.
  unsigned NumSubExprs : 16;


  DesignatedInitExpr(ASTContext &C, QualType Ty, unsigned NumDesignators,
                     const Designator *Designators,
                     SourceLocation EqualOrColonLoc, bool GNUSyntax,
                     Expr **IndexExprs, unsigned NumIndexExprs,
                     Expr *Init);

  explicit DesignatedInitExpr(unsigned NumSubExprs)
    : Expr(DesignatedInitExprClass, EmptyShell()),
      NumDesignators(0), Designators(0), NumSubExprs(NumSubExprs) { }

public:
  /// A field designator, e.g., ".x".
  struct FieldDesignator {
    /// Refers to the field that is being initialized. The low bit
    /// of this field determines whether this is actually a pointer
    /// to an IdentifierInfo (if 1) or a FieldDecl (if 0). When
    /// initially constructed, a field designator will store an
    /// IdentifierInfo*. After semantic analysis has resolved that
    /// name, the field designator will instead store a FieldDecl*.
    uintptr_t NameOrField;

    /// The location of the '.' in the designated initializer.
    unsigned DotLoc;

    /// The location of the field name in the designated initializer.
    unsigned FieldLoc;
  };

  /// An array or GNU array-range designator, e.g., "[9]" or "[10..15]".
  struct ArrayOrRangeDesignator {
    /// Location of the first index expression within the designated
    /// initializer expression's list of subexpressions.
    unsigned Index;
    /// The location of the '[' starting the array range designator.
    unsigned LBracketLoc;
    /// The location of the ellipsis separating the start and end
    /// indices. Only valid for GNU array-range designators.
    unsigned EllipsisLoc;
    /// The location of the ']' terminating the array range designator.
    unsigned RBracketLoc;
  };

  /// @brief Represents a single C99 designator.
  ///
  /// @todo This class is infuriatingly similar to clang::Designator,
  /// but minor differences (storing indices vs. storing pointers)
  /// keep us from reusing it. Try harder, later, to rectify these
  /// differences.
  class Designator {
    /// @brief The kind of designator this describes.
    enum {
      FieldDesignator,
      ArrayDesignator,
      ArrayRangeDesignator
    } Kind;

    union {
      /// A field designator, e.g., ".x".
      struct FieldDesignator Field;
      /// An array or GNU array-range designator, e.g., "[9]" or "[10..15]".
      struct ArrayOrRangeDesignator ArrayOrRange;
    };
    friend class DesignatedInitExpr;

  public:
    Designator() {}

    /// @brief Initializes a field designator.
    Designator(const IdentifierInfo *FieldName, SourceLocation DotLoc,
               SourceLocation FieldLoc)
      : Kind(FieldDesignator) {
      Field.NameOrField = reinterpret_cast<uintptr_t>(FieldName) | 0x01;
      Field.DotLoc = DotLoc.getRawEncoding();
      Field.FieldLoc = FieldLoc.getRawEncoding();
    }

    /// @brief Initializes an array designator.
    Designator(unsigned Index, SourceLocation LBracketLoc,
               SourceLocation RBracketLoc)
      : Kind(ArrayDesignator) {
      ArrayOrRange.Index = Index;
      ArrayOrRange.LBracketLoc = LBracketLoc.getRawEncoding();
      ArrayOrRange.EllipsisLoc = SourceLocation().getRawEncoding();
      ArrayOrRange.RBracketLoc = RBracketLoc.getRawEncoding();
    }

    /// @brief Initializes a GNU array-range designator.
    Designator(unsigned Index, SourceLocation LBracketLoc,
               SourceLocation EllipsisLoc, SourceLocation RBracketLoc)
      : Kind(ArrayRangeDesignator) {
      ArrayOrRange.Index = Index;
      ArrayOrRange.LBracketLoc = LBracketLoc.getRawEncoding();
      ArrayOrRange.EllipsisLoc = EllipsisLoc.getRawEncoding();
      ArrayOrRange.RBracketLoc = RBracketLoc.getRawEncoding();
    }

    bool isFieldDesignator() const { return Kind == FieldDesignator; }
    bool isArrayDesignator() const { return Kind == ArrayDesignator; }
    bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

    IdentifierInfo *getFieldName() const;

    FieldDecl *getField() const {
      assert(Kind == FieldDesignator && "Only valid on a field designator");
      if (Field.NameOrField & 0x01)
        return 0;
      else
        return reinterpret_cast<FieldDecl *>(Field.NameOrField);
    }

    void setField(FieldDecl *FD) {
      assert(Kind == FieldDesignator && "Only valid on a field designator");
      Field.NameOrField = reinterpret_cast<uintptr_t>(FD);
    }

    SourceLocation getDotLoc() const {
      assert(Kind == FieldDesignator && "Only valid on a field designator");
      return SourceLocation::getFromRawEncoding(Field.DotLoc);
    }

    SourceLocation getFieldLoc() const {
      assert(Kind == FieldDesignator && "Only valid on a field designator");
      return SourceLocation::getFromRawEncoding(Field.FieldLoc);
    }

    SourceLocation getLBracketLoc() const {
      assert((Kind == ArrayDesignator || Kind == ArrayRangeDesignator) &&
             "Only valid on an array or array-range designator");
      return SourceLocation::getFromRawEncoding(ArrayOrRange.LBracketLoc);
    }

    SourceLocation getRBracketLoc() const {
      assert((Kind == ArrayDesignator || Kind == ArrayRangeDesignator) &&
             "Only valid on an array or array-range designator");
      return SourceLocation::getFromRawEncoding(ArrayOrRange.RBracketLoc);
    }

    SourceLocation getEllipsisLoc() const {
      assert(Kind == ArrayRangeDesignator &&
             "Only valid on an array-range designator");
      return SourceLocation::getFromRawEncoding(ArrayOrRange.EllipsisLoc);
    }

    unsigned getFirstExprIndex() const {
      assert((Kind == ArrayDesignator || Kind == ArrayRangeDesignator) &&
             "Only valid on an array or array-range designator");
      return ArrayOrRange.Index;
    }

    SourceLocation getStartLocation() const {
      if (Kind == FieldDesignator)
        return getDotLoc().isInvalid()? getFieldLoc() : getDotLoc();
      else
        return getLBracketLoc();
    }
    SourceLocation getEndLocation() const {
      return Kind == FieldDesignator ? getFieldLoc() : getRBracketLoc();
    }
    SourceRange getSourceRange() const {
      return SourceRange(getStartLocation(), getEndLocation());
    }
  };

  static DesignatedInitExpr *Create(ASTContext &C, Designator *Designators,
                                    unsigned NumDesignators,
                                    Expr **IndexExprs, unsigned NumIndexExprs,
                                    SourceLocation EqualOrColonLoc,
                                    bool GNUSyntax, Expr *Init);

  static DesignatedInitExpr *CreateEmpty(ASTContext &C, unsigned NumIndexExprs);

  /// @brief Returns the number of designators in this initializer.
  unsigned size() const { return NumDesignators; }

  // Iterator access to the designators.
  typedef Designator *designators_iterator;
  designators_iterator designators_begin() { return Designators; }
  designators_iterator designators_end() {
    return Designators + NumDesignators;
  }

  typedef const Designator *const_designators_iterator;
  const_designators_iterator designators_begin() const { return Designators; }
  const_designators_iterator designators_end() const {
    return Designators + NumDesignators;
  }

  typedef std::reverse_iterator<designators_iterator>
          reverse_designators_iterator;
  reverse_designators_iterator designators_rbegin() {
    return reverse_designators_iterator(designators_end());
  }
  reverse_designators_iterator designators_rend() {
    return reverse_designators_iterator(designators_begin());
  }

  typedef std::reverse_iterator<const_designators_iterator>
          const_reverse_designators_iterator;
  const_reverse_designators_iterator designators_rbegin() const {
    return const_reverse_designators_iterator(designators_end());
  }
  const_reverse_designators_iterator designators_rend() const {
    return const_reverse_designators_iterator(designators_begin());
  }

  Designator *getDesignator(unsigned Idx) { return &designators_begin()[Idx]; }

  void setDesignators(ASTContext &C, const Designator *Desigs, 
                      unsigned NumDesigs);

  Expr *getArrayIndex(const Designator& D);
  Expr *getArrayRangeStart(const Designator& D);
  Expr *getArrayRangeEnd(const Designator& D);

  /// @brief Retrieve the location of the '=' that precedes the
  /// initializer value itself, if present.
  SourceLocation getEqualOrColonLoc() const { return EqualOrColonLoc; }
  void setEqualOrColonLoc(SourceLocation L) { EqualOrColonLoc = L; }

  /// @brief Determines whether this designated initializer used the
  /// deprecated GNU syntax for designated initializers.
  bool usesGNUSyntax() const { return GNUSyntax; }
  void setGNUSyntax(bool GNU) { GNUSyntax = GNU; }

  /// @brief Retrieve the initializer value.
  Expr *getInit() const {
    return cast<Expr>(*const_cast<DesignatedInitExpr*>(this)->child_begin());
  }

  void setInit(Expr *init) {
    *child_begin() = init;
  }

  /// \brief Retrieve the total number of subexpressions in this
  /// designated initializer expression, including the actual
  /// initialized value and any expressions that occur within array
  /// and array-range designators.
  unsigned getNumSubExprs() const { return NumSubExprs; }

  Expr *getSubExpr(unsigned Idx) {
    assert(Idx < NumSubExprs && "Subscript out of range");
    char* Ptr = static_cast<char*>(static_cast<void *>(this));
    Ptr += sizeof(DesignatedInitExpr);
    return reinterpret_cast<Expr**>(reinterpret_cast<void**>(Ptr))[Idx];
  }

  void setSubExpr(unsigned Idx, Expr *E) {
    assert(Idx < NumSubExprs && "Subscript out of range");
    char* Ptr = static_cast<char*>(static_cast<void *>(this));
    Ptr += sizeof(DesignatedInitExpr);
    reinterpret_cast<Expr**>(reinterpret_cast<void**>(Ptr))[Idx] = E;
  }

  /// \brief Replaces the designator at index @p Idx with the series
  /// of designators in [First, Last).
  void ExpandDesignator(ASTContext &C, unsigned Idx, const Designator *First,
                        const Designator *Last);

  SourceRange getDesignatorsSourceRange() const;

  SourceRange getSourceRange() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == DesignatedInitExprClass;
  }
  static bool classof(const DesignatedInitExpr *) { return true; }

  // Iterators
  child_range children() {
    Stmt **begin = reinterpret_cast<Stmt**>(this + 1);
    return child_range(begin, begin + NumSubExprs);
  }
};

/// \brief Represents an implicitly-generated value initialization of
/// an object of a given type.
///
/// Implicit value initializations occur within semantic initializer
/// list expressions (InitListExpr) as placeholders for subobject
/// initializations not explicitly specified by the user.
///
/// \see InitListExpr
class ImplicitValueInitExpr : public Expr {
public:
  explicit ImplicitValueInitExpr(QualType ty)
    : Expr(ImplicitValueInitExprClass, ty, VK_RValue, OK_Ordinary,
           false, false, ty->isInstantiationDependentType(), false) { }

  /// \brief Construct an empty implicit value initialization.
  explicit ImplicitValueInitExpr(EmptyShell Empty)
    : Expr(ImplicitValueInitExprClass, Empty) { }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ImplicitValueInitExprClass;
  }
  static bool classof(const ImplicitValueInitExpr *) { return true; }

  SourceRange getSourceRange() const {
    return SourceRange();
  }

  // Iterators
  child_range children() { return child_range(); }
};


class ParenListExpr : public Expr {
  Stmt **Exprs;
  unsigned NumExprs;
  SourceLocation LParenLoc, RParenLoc;

public:
  ParenListExpr(ASTContext& C, SourceLocation lparenloc, Expr **exprs,
                unsigned numexprs, SourceLocation rparenloc, QualType T);

  /// \brief Build an empty paren list.
  explicit ParenListExpr(EmptyShell Empty) : Expr(ParenListExprClass, Empty) { }

  unsigned getNumExprs() const { return NumExprs; }

  const Expr* getExpr(unsigned Init) const {
    assert(Init < getNumExprs() && "Initializer access out of range!");
    return cast_or_null<Expr>(Exprs[Init]);
  }

  Expr* getExpr(unsigned Init) {
    assert(Init < getNumExprs() && "Initializer access out of range!");
    return cast_or_null<Expr>(Exprs[Init]);
  }

  Expr **getExprs() { return reinterpret_cast<Expr **>(Exprs); }

  SourceLocation getLParenLoc() const { return LParenLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  SourceRange getSourceRange() const {
    return SourceRange(LParenLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ParenListExprClass;
  }
  static bool classof(const ParenListExpr *) { return true; }

  // Iterators
  child_range children() {
    return child_range(&Exprs[0], &Exprs[0]+NumExprs);
  }

  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};


/// \brief Represents a C1X generic selection.
///
/// A generic selection (C1X 6.5.1.1) contains an unevaluated controlling
/// expression, followed by one or more generic associations.  Each generic
/// association specifies a type name and an expression, or "default" and an
/// expression (in which case it is known as a default generic association).
/// The type and value of the generic selection are identical to those of its
/// result expression, which is defined as the expression in the generic
/// association with a type name that is compatible with the type of the
/// controlling expression, or the expression in the default generic association
/// if no types are compatible.  For example:
///
/// @code
/// _Generic(X, double: 1, float: 2, default: 3)
/// @endcode
///
/// The above expression evaluates to 1 if 1.0 is substituted for X, 2 if 1.0f
/// or 3 if "hello".
///
/// As an extension, generic selections are allowed in C++, where the following
/// additional semantics apply:
///
/// Any generic selection whose controlling expression is type-dependent or
/// which names a dependent type in its association list is result-dependent,
/// which means that the choice of result expression is dependent.
/// Result-dependent generic associations are both type- and value-dependent.
class GenericSelectionExpr : public Expr {
  enum { CONTROLLING, END_EXPR };
  TypeSourceInfo **AssocTypes;
  Stmt **SubExprs;
  unsigned NumAssocs, ResultIndex;
  SourceLocation GenericLoc, DefaultLoc, RParenLoc;

public:
  GenericSelectionExpr(ASTContext &Context,
                       SourceLocation GenericLoc, Expr *ControllingExpr,
                       TypeSourceInfo **AssocTypes, Expr **AssocExprs,
                       unsigned NumAssocs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack,
                       unsigned ResultIndex);

  /// This constructor is used in the result-dependent case.
  GenericSelectionExpr(ASTContext &Context,
                       SourceLocation GenericLoc, Expr *ControllingExpr,
                       TypeSourceInfo **AssocTypes, Expr **AssocExprs,
                       unsigned NumAssocs, SourceLocation DefaultLoc,
                       SourceLocation RParenLoc,
                       bool ContainsUnexpandedParameterPack);

  explicit GenericSelectionExpr(EmptyShell Empty)
    : Expr(GenericSelectionExprClass, Empty) { }

  unsigned getNumAssocs() const { return NumAssocs; }

  SourceLocation getGenericLoc() const { return GenericLoc; }
  SourceLocation getDefaultLoc() const { return DefaultLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }

  const Expr *getAssocExpr(unsigned i) const {
    return cast<Expr>(SubExprs[END_EXPR+i]);
  }
  Expr *getAssocExpr(unsigned i) { return cast<Expr>(SubExprs[END_EXPR+i]); }

  const TypeSourceInfo *getAssocTypeSourceInfo(unsigned i) const {
    return AssocTypes[i];
  }
  TypeSourceInfo *getAssocTypeSourceInfo(unsigned i) { return AssocTypes[i]; }

  QualType getAssocType(unsigned i) const {
    if (const TypeSourceInfo *TS = getAssocTypeSourceInfo(i))
      return TS->getType();
    else
      return QualType();
  }

  const Expr *getControllingExpr() const {
    return cast<Expr>(SubExprs[CONTROLLING]);
  }
  Expr *getControllingExpr() { return cast<Expr>(SubExprs[CONTROLLING]); }

  /// Whether this generic selection is result-dependent.
  bool isResultDependent() const { return ResultIndex == -1U; }

  /// The zero-based index of the result expression's generic association in
  /// the generic selection's association list.  Defined only if the
  /// generic selection is not result-dependent.
  unsigned getResultIndex() const {
    assert(!isResultDependent() && "Generic selection is result-dependent");
    return ResultIndex;
  }

  /// The generic selection's result expression.  Defined only if the
  /// generic selection is not result-dependent.
  const Expr *getResultExpr() const { return getAssocExpr(getResultIndex()); }
  Expr *getResultExpr() { return getAssocExpr(getResultIndex()); }

  SourceRange getSourceRange() const {
    return SourceRange(GenericLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == GenericSelectionExprClass;
  }
  static bool classof(const GenericSelectionExpr *) { return true; }

  child_range children() {
    return child_range(SubExprs, SubExprs+END_EXPR+NumAssocs);
  }

  friend class ASTStmtReader;
};

//===----------------------------------------------------------------------===//
// Clang Extensions
//===----------------------------------------------------------------------===//


/// ExtVectorElementExpr - This represents access to specific elements of a
/// vector, and may occur on the left hand side or right hand side.  For example
/// the following is legal:  "V.xy = V.zw" if V is a 4 element extended vector.
///
/// Note that the base may have either vector or pointer to vector type, just
/// like a struct field reference.
///
class ExtVectorElementExpr : public Expr {
  Stmt *Base;
  IdentifierInfo *Accessor;
  SourceLocation AccessorLoc;
public:
  ExtVectorElementExpr(QualType ty, ExprValueKind VK, Expr *base,
                       IdentifierInfo &accessor, SourceLocation loc)
    : Expr(ExtVectorElementExprClass, ty, VK,
           (VK == VK_RValue ? OK_Ordinary : OK_VectorComponent),
           base->isTypeDependent(), base->isValueDependent(),
           base->isInstantiationDependent(),
           base->containsUnexpandedParameterPack()),
      Base(base), Accessor(&accessor), AccessorLoc(loc) {}

  /// \brief Build an empty vector element expression.
  explicit ExtVectorElementExpr(EmptyShell Empty)
    : Expr(ExtVectorElementExprClass, Empty) { }

  const Expr *getBase() const { return cast<Expr>(Base); }
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr *E) { Base = E; }

  IdentifierInfo &getAccessor() const { return *Accessor; }
  void setAccessor(IdentifierInfo *II) { Accessor = II; }

  SourceLocation getAccessorLoc() const { return AccessorLoc; }
  void setAccessorLoc(SourceLocation L) { AccessorLoc = L; }

  /// getNumElements - Get the number of components being selected.
  unsigned getNumElements() const;

  /// containsDuplicateElements - Return true if any element access is
  /// repeated.
  bool containsDuplicateElements() const;

  /// getEncodedElementAccess - Encode the elements accessed into an llvm
  /// aggregate Constant of ConstantInt(s).
  void getEncodedElementAccess(SmallVectorImpl<unsigned> &Elts) const;

  SourceRange getSourceRange() const {
    return SourceRange(getBase()->getLocStart(), AccessorLoc);
  }

  /// isArrow - Return true if the base expression is a pointer to vector,
  /// return false if the base expression is a vector.
  bool isArrow() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ExtVectorElementExprClass;
  }
  static bool classof(const ExtVectorElementExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }
};


/// BlockExpr - Adaptor class for mixing a BlockDecl with expressions.
/// ^{ statement-body }   or   ^(int arg1, float arg2){ statement-body }
class BlockExpr : public Expr {
protected:
  BlockDecl *TheBlock;
public:
  BlockExpr(BlockDecl *BD, QualType ty)
    : Expr(BlockExprClass, ty, VK_RValue, OK_Ordinary,
           ty->isDependentType(), false, 
           // FIXME: Check for instantiate-dependence in the statement?
           ty->isInstantiationDependentType(),
           false),
      TheBlock(BD) {}

  /// \brief Build an empty block expression.
  explicit BlockExpr(EmptyShell Empty) : Expr(BlockExprClass, Empty) { }

  const BlockDecl *getBlockDecl() const { return TheBlock; }
  BlockDecl *getBlockDecl() { return TheBlock; }
  void setBlockDecl(BlockDecl *BD) { TheBlock = BD; }

  // Convenience functions for probing the underlying BlockDecl.
  SourceLocation getCaretLocation() const;
  const Stmt *getBody() const;
  Stmt *getBody();

  SourceRange getSourceRange() const {
    return SourceRange(getCaretLocation(), getBody()->getLocEnd());
  }

  /// getFunctionType - Return the underlying function type for this block.
  const FunctionType *getFunctionType() const;

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BlockExprClass;
  }
  static bool classof(const BlockExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// BlockDeclRefExpr - A reference to a local variable declared in an
/// enclosing scope.
class BlockDeclRefExpr : public Expr {
  VarDecl *D;
  SourceLocation Loc;
  bool IsByRef : 1;
  bool ConstQualAdded : 1;
public:
  BlockDeclRefExpr(VarDecl *d, QualType t, ExprValueKind VK,
                   SourceLocation l, bool ByRef, bool constAdded = false);

  // \brief Build an empty reference to a declared variable in a
  // block.
  explicit BlockDeclRefExpr(EmptyShell Empty)
    : Expr(BlockDeclRefExprClass, Empty) { }

  VarDecl *getDecl() { return D; }
  const VarDecl *getDecl() const { return D; }
  void setDecl(VarDecl *VD) { D = VD; }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceRange getSourceRange() const { return SourceRange(Loc); }

  bool isByRef() const { return IsByRef; }
  void setByRef(bool BR) { IsByRef = BR; }

  bool isConstQualAdded() const { return ConstQualAdded; }
  void setConstQualAdded(bool C) { ConstQualAdded = C; }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == BlockDeclRefExprClass;
  }
  static bool classof(const BlockDeclRefExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// AsTypeExpr - Clang builtin function __builtin_astype [OpenCL 6.2.4.2]
/// This AST node provides support for reinterpreting a type to another
/// type of the same size.
class AsTypeExpr : public Expr { // Should this be an ExplicitCastExpr?
private:
  Stmt *SrcExpr;
  SourceLocation BuiltinLoc, RParenLoc;

  friend class ASTReader;
  friend class ASTStmtReader;
  explicit AsTypeExpr(EmptyShell Empty) : Expr(AsTypeExprClass, Empty) {}
  
public:
  AsTypeExpr(Expr* SrcExpr, QualType DstType,
             ExprValueKind VK, ExprObjectKind OK,
             SourceLocation BuiltinLoc, SourceLocation RParenLoc)
    : Expr(AsTypeExprClass, DstType, VK, OK, 
           DstType->isDependentType(),
           DstType->isDependentType() || SrcExpr->isValueDependent(),
           (DstType->isInstantiationDependentType() ||
            SrcExpr->isInstantiationDependent()),
           (DstType->containsUnexpandedParameterPack() ||
            SrcExpr->containsUnexpandedParameterPack())),
  SrcExpr(SrcExpr), BuiltinLoc(BuiltinLoc), RParenLoc(RParenLoc) {}
  
  /// getSrcExpr - Return the Expr to be converted.
  Expr *getSrcExpr() const { return cast<Expr>(SrcExpr); }

  /// getBuiltinLoc - Return the location of the __builtin_astype token.
  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }

  /// getRParenLoc - Return the location of final right parenthesis.
  SourceLocation getRParenLoc() const { return RParenLoc; }
  
  SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AsTypeExprClass; 
  }
  static bool classof(const AsTypeExpr *) { return true; }
  
  // Iterators
  child_range children() { return child_range(&SrcExpr, &SrcExpr+1); }
};

/// AtomicExpr - Variadic atomic builtins: __atomic_exchange, __atomic_fetch_*,
/// __atomic_load, __atomic_store, and __atomic_compare_exchange_*, for the
/// similarly-named C++0x instructions.  All of these instructions take one
/// primary pointer and at least one memory order.
class AtomicExpr : public Expr {
public:
  enum AtomicOp { Load, Store, CmpXchgStrong, CmpXchgWeak, Xchg,
                  Add, Sub, And, Or, Xor };
private:
  enum { PTR, ORDER, VAL1, ORDER_FAIL, VAL2, END_EXPR };
  Stmt* SubExprs[END_EXPR];
  unsigned NumSubExprs;
  SourceLocation BuiltinLoc, RParenLoc;
  AtomicOp Op;

public:
  AtomicExpr(SourceLocation BLoc, Expr **args, unsigned nexpr, QualType t,
             AtomicOp op, SourceLocation RP);

  /// \brief Build an empty AtomicExpr.
  explicit AtomicExpr(EmptyShell Empty) : Expr(AtomicExprClass, Empty) { }

  Expr *getPtr() const {
    return cast<Expr>(SubExprs[PTR]);
  }
  void setPtr(Expr *E) {
    SubExprs[PTR] = E;
  }
  Expr *getOrder() const {
    return cast<Expr>(SubExprs[ORDER]);
  }
  void setOrder(Expr *E) {
    SubExprs[ORDER] = E;
  }
  Expr *getVal1() const {
    assert(NumSubExprs >= 3);
    return cast<Expr>(SubExprs[VAL1]);
  }
  void setVal1(Expr *E) {
    assert(NumSubExprs >= 3);
    SubExprs[VAL1] = E;
  }
  Expr *getOrderFail() const {
    assert(NumSubExprs == 5);
    return cast<Expr>(SubExprs[ORDER_FAIL]);
  }
  void setOrderFail(Expr *E) {
    assert(NumSubExprs == 5);
    SubExprs[ORDER_FAIL] = E;
  }
  Expr *getVal2() const {
    assert(NumSubExprs == 5);
    return cast<Expr>(SubExprs[VAL2]);
  }
  void setVal2(Expr *E) {
    assert(NumSubExprs == 5);
    SubExprs[VAL2] = E;
  }

  AtomicOp getOp() const { return Op; }
  void setOp(AtomicOp op) { Op = op; }
  unsigned getNumSubExprs() { return NumSubExprs; }
  void setNumSubExprs(unsigned num) { NumSubExprs = num; }

  Expr **getSubExprs() { return reinterpret_cast<Expr **>(SubExprs); }

  bool isVolatile() const {
    return getPtr()->getType()->getPointeeType().isVolatileQualified();
  }

  bool isCmpXChg() const {
    return getOp() == AtomicExpr::CmpXchgStrong ||
           getOp() == AtomicExpr::CmpXchgWeak;
  }

  SourceLocation getBuiltinLoc() const { return BuiltinLoc; }
  void setBuiltinLoc(SourceLocation L) { BuiltinLoc = L; }

  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(BuiltinLoc, RParenLoc);
  }
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == AtomicExprClass;
  }
  static bool classof(const AtomicExpr *) { return true; }

  // Iterators
  child_range children() {
    return child_range(SubExprs, SubExprs+NumSubExprs);
  }
};
}  // end namespace clang

#endif
