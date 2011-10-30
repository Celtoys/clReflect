//===--- ExprObjC.h - Classes for representing ObjC expressions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ExprObjC interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPROBJC_H
#define LLVM_CLANG_AST_EXPROBJC_H

#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/SelectorLocationsKind.h"
#include "clang/Basic/IdentifierTable.h"

namespace clang {
  class IdentifierInfo;
  class ASTContext;

/// ObjCStringLiteral, used for Objective-C string literals
/// i.e. @"foo".
class ObjCStringLiteral : public Expr {
  Stmt *String;
  SourceLocation AtLoc;
public:
  ObjCStringLiteral(StringLiteral *SL, QualType T, SourceLocation L)
    : Expr(ObjCStringLiteralClass, T, VK_RValue, OK_Ordinary, false, false,
           false, false),
      String(SL), AtLoc(L) {}
  explicit ObjCStringLiteral(EmptyShell Empty)
    : Expr(ObjCStringLiteralClass, Empty) {}

  StringLiteral *getString() { return cast<StringLiteral>(String); }
  const StringLiteral *getString() const { return cast<StringLiteral>(String); }
  void setString(StringLiteral *S) { String = S; }

  SourceLocation getAtLoc() const { return AtLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(AtLoc, String->getLocEnd());
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCStringLiteralClass;
  }
  static bool classof(const ObjCStringLiteral *) { return true; }

  // Iterators
  child_range children() { return child_range(&String, &String+1); }
};

/// ObjCEncodeExpr, used for @encode in Objective-C.  @encode has the same type
/// and behavior as StringLiteral except that the string initializer is obtained
/// from ASTContext with the encoding type as an argument.
class ObjCEncodeExpr : public Expr {
  TypeSourceInfo *EncodedType;
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCEncodeExpr(QualType T, TypeSourceInfo *EncodedType,
                 SourceLocation at, SourceLocation rp)
    : Expr(ObjCEncodeExprClass, T, VK_LValue, OK_Ordinary,
           EncodedType->getType()->isDependentType(),
           EncodedType->getType()->isDependentType(),
           EncodedType->getType()->isInstantiationDependentType(),
           EncodedType->getType()->containsUnexpandedParameterPack()), 
      EncodedType(EncodedType), AtLoc(at), RParenLoc(rp) {}

  explicit ObjCEncodeExpr(EmptyShell Empty) : Expr(ObjCEncodeExprClass, Empty){}


  SourceLocation getAtLoc() const { return AtLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  QualType getEncodedType() const { return EncodedType->getType(); }

  TypeSourceInfo *getEncodedTypeSourceInfo() const { return EncodedType; }
  void setEncodedTypeSourceInfo(TypeSourceInfo *EncType) { 
    EncodedType = EncType; 
  }

  SourceRange getSourceRange() const {
    return SourceRange(AtLoc, RParenLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCEncodeExprClass;
  }
  static bool classof(const ObjCEncodeExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// ObjCSelectorExpr used for @selector in Objective-C.
class ObjCSelectorExpr : public Expr {
  Selector SelName;
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCSelectorExpr(QualType T, Selector selInfo,
                   SourceLocation at, SourceLocation rp)
    : Expr(ObjCSelectorExprClass, T, VK_RValue, OK_Ordinary, false, false, 
           false, false),
    SelName(selInfo), AtLoc(at), RParenLoc(rp){}
  explicit ObjCSelectorExpr(EmptyShell Empty)
   : Expr(ObjCSelectorExprClass, Empty) {}

  Selector getSelector() const { return SelName; }
  void setSelector(Selector S) { SelName = S; }

  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(AtLoc, RParenLoc);
  }

  /// getNumArgs - Return the number of actual arguments to this call.
  unsigned getNumArgs() const { return SelName.getNumArgs(); }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCSelectorExprClass;
  }
  static bool classof(const ObjCSelectorExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// ObjCProtocolExpr used for protocol expression in Objective-C.  This is used
/// as: @protocol(foo), as in:
///   obj conformsToProtocol:@protocol(foo)]
/// The return type is "Protocol*".
class ObjCProtocolExpr : public Expr {
  ObjCProtocolDecl *TheProtocol;
  SourceLocation AtLoc, RParenLoc;
public:
  ObjCProtocolExpr(QualType T, ObjCProtocolDecl *protocol,
                   SourceLocation at, SourceLocation rp)
    : Expr(ObjCProtocolExprClass, T, VK_RValue, OK_Ordinary, false, false,
           false, false),
      TheProtocol(protocol), AtLoc(at), RParenLoc(rp) {}
  explicit ObjCProtocolExpr(EmptyShell Empty)
    : Expr(ObjCProtocolExprClass, Empty) {}

  ObjCProtocolDecl *getProtocol() const { return TheProtocol; }
  void setProtocol(ObjCProtocolDecl *P) { TheProtocol = P; }

  SourceLocation getAtLoc() const { return AtLoc; }
  SourceLocation getRParenLoc() const { return RParenLoc; }
  void setAtLoc(SourceLocation L) { AtLoc = L; }
  void setRParenLoc(SourceLocation L) { RParenLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(AtLoc, RParenLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCProtocolExprClass;
  }
  static bool classof(const ObjCProtocolExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(); }
};

/// ObjCIvarRefExpr - A reference to an ObjC instance variable.
class ObjCIvarRefExpr : public Expr {
  class ObjCIvarDecl *D;
  SourceLocation Loc;
  Stmt *Base;
  bool IsArrow:1;      // True if this is "X->F", false if this is "X.F".
  bool IsFreeIvar:1;   // True if ivar reference has no base (self assumed).

public:
  ObjCIvarRefExpr(ObjCIvarDecl *d, QualType t,
                  SourceLocation l, Expr *base,
                  bool arrow = false, bool freeIvar = false) :
    Expr(ObjCIvarRefExprClass, t, VK_LValue, OK_Ordinary,
         /*TypeDependent=*/false, base->isValueDependent(), 
         base->isInstantiationDependent(),
         base->containsUnexpandedParameterPack()), 
    D(d), Loc(l), Base(base), IsArrow(arrow), IsFreeIvar(freeIvar) {}

  explicit ObjCIvarRefExpr(EmptyShell Empty)
    : Expr(ObjCIvarRefExprClass, Empty) {}

  ObjCIvarDecl *getDecl() { return D; }
  const ObjCIvarDecl *getDecl() const { return D; }
  void setDecl(ObjCIvarDecl *d) { D = d; }

  const Expr *getBase() const { return cast<Expr>(Base); }
  Expr *getBase() { return cast<Expr>(Base); }
  void setBase(Expr * base) { Base = base; }

  bool isArrow() const { return IsArrow; }
  bool isFreeIvar() const { return IsFreeIvar; }
  void setIsArrow(bool A) { IsArrow = A; }
  void setIsFreeIvar(bool A) { IsFreeIvar = A; }

  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  SourceRange getSourceRange() const {
    return isFreeIvar() ? SourceRange(Loc)
    : SourceRange(getBase()->getLocStart(), Loc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCIvarRefExprClass;
  }
  static bool classof(const ObjCIvarRefExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }
};

/// ObjCPropertyRefExpr - A dot-syntax expression to access an ObjC
/// property.
///
class ObjCPropertyRefExpr : public Expr {
private:
  /// If the bool is true, this is an implicit property reference; the
  /// pointer is an (optional) ObjCMethodDecl and Setter may be set.
  /// if the bool is false, this is an explicit property reference;
  /// the pointer is an ObjCPropertyDecl and Setter is always null.
  llvm::PointerIntPair<NamedDecl*, 1, bool> PropertyOrGetter;
  ObjCMethodDecl *Setter;

  SourceLocation IdLoc;
  
  /// \brief When the receiver in property access is 'super', this is
  /// the location of the 'super' keyword.  When it's an interface,
  /// this is that interface.
  SourceLocation ReceiverLoc;
  llvm::PointerUnion3<Stmt*, const Type*, ObjCInterfaceDecl*> Receiver;
  
public:
  ObjCPropertyRefExpr(ObjCPropertyDecl *PD, QualType t,
                      ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation l, Expr *base)
    : Expr(ObjCPropertyRefExprClass, t, VK, OK,
           /*TypeDependent=*/false, base->isValueDependent(),
           base->isInstantiationDependent(),
           base->containsUnexpandedParameterPack()),
      PropertyOrGetter(PD, false), Setter(0),
      IdLoc(l), ReceiverLoc(), Receiver(base) {
  }
  
  ObjCPropertyRefExpr(ObjCPropertyDecl *PD, QualType t,
                      ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation l, SourceLocation sl, QualType st)
    : Expr(ObjCPropertyRefExprClass, t, VK, OK,
           /*TypeDependent=*/false, false, st->isInstantiationDependentType(),
           st->containsUnexpandedParameterPack()),
      PropertyOrGetter(PD, false), Setter(0),
      IdLoc(l), ReceiverLoc(sl), Receiver(st.getTypePtr()) {
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc, Expr *Base)
    : Expr(ObjCPropertyRefExprClass, T, VK, OK, false,
           Base->isValueDependent(), Base->isInstantiationDependent(),
           Base->containsUnexpandedParameterPack()),
      PropertyOrGetter(Getter, true), Setter(Setter),
      IdLoc(IdLoc), ReceiverLoc(), Receiver(Base) {
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc,
                      SourceLocation SuperLoc, QualType SuperTy)
    : Expr(ObjCPropertyRefExprClass, T, VK, OK, false, false, false, false),
      PropertyOrGetter(Getter, true), Setter(Setter),
      IdLoc(IdLoc), ReceiverLoc(SuperLoc), Receiver(SuperTy.getTypePtr()) {
  }

  ObjCPropertyRefExpr(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter,
                      QualType T, ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation IdLoc,
                      SourceLocation ReceiverLoc, ObjCInterfaceDecl *Receiver)
    : Expr(ObjCPropertyRefExprClass, T, VK, OK, false, false, false, false),
      PropertyOrGetter(Getter, true), Setter(Setter),
      IdLoc(IdLoc), ReceiverLoc(ReceiverLoc), Receiver(Receiver) {
  }

  explicit ObjCPropertyRefExpr(EmptyShell Empty)
    : Expr(ObjCPropertyRefExprClass, Empty) {}

  bool isImplicitProperty() const { return PropertyOrGetter.getInt(); }
  bool isExplicitProperty() const { return !PropertyOrGetter.getInt(); }

  ObjCPropertyDecl *getExplicitProperty() const {
    assert(!isImplicitProperty());
    return cast<ObjCPropertyDecl>(PropertyOrGetter.getPointer());
  }

  ObjCMethodDecl *getImplicitPropertyGetter() const {
    assert(isImplicitProperty());
    return cast_or_null<ObjCMethodDecl>(PropertyOrGetter.getPointer());
  }

  ObjCMethodDecl *getImplicitPropertySetter() const {
    assert(isImplicitProperty());
    return Setter;
  }

  Selector getGetterSelector() const {
    if (isImplicitProperty())
      return getImplicitPropertyGetter()->getSelector();
    return getExplicitProperty()->getGetterName();
  }

  Selector getSetterSelector() const {
    if (isImplicitProperty())
      return getImplicitPropertySetter()->getSelector();
    return getExplicitProperty()->getSetterName();
  }

  const Expr *getBase() const { 
    return cast<Expr>(Receiver.get<Stmt*>()); 
  }
  Expr *getBase() { 
    return cast<Expr>(Receiver.get<Stmt*>()); 
  }

  SourceLocation getLocation() const { return IdLoc; }
  
  SourceLocation getReceiverLocation() const { return ReceiverLoc; }
  QualType getSuperReceiverType() const { 
    return QualType(Receiver.get<const Type*>(), 0); 
  }
  QualType getGetterResultType() const {
    QualType ResultType;
    if (isExplicitProperty()) {
      const ObjCPropertyDecl *PDecl = getExplicitProperty();
      if (const ObjCMethodDecl *Getter = PDecl->getGetterMethodDecl())
        ResultType = Getter->getResultType();
      else
        ResultType = getType();
    } else {
      const ObjCMethodDecl *Getter = getImplicitPropertyGetter();
      ResultType = Getter->getResultType(); // with reference!
    }
    return ResultType;
  }
  
  QualType getSetterArgType() const {
    QualType ArgType;
    if (isImplicitProperty()) {
      const ObjCMethodDecl *Setter = getImplicitPropertySetter();
      ObjCMethodDecl::param_const_iterator P = Setter->param_begin(); 
      ArgType = (*P)->getType();
    } else {
      if (ObjCPropertyDecl *PDecl = getExplicitProperty())
        if (const ObjCMethodDecl *Setter = PDecl->getSetterMethodDecl()) {
          ObjCMethodDecl::param_const_iterator P = Setter->param_begin(); 
          ArgType = (*P)->getType();
        }
      if (ArgType.isNull())
        ArgType = getType();
    }
    return ArgType;
  }
  
  ObjCInterfaceDecl *getClassReceiver() const {
    return Receiver.get<ObjCInterfaceDecl*>();
  }
  bool isObjectReceiver() const { return Receiver.is<Stmt*>(); }
  bool isSuperReceiver() const { return Receiver.is<const Type*>(); }
  bool isClassReceiver() const { return Receiver.is<ObjCInterfaceDecl*>(); }

  SourceRange getSourceRange() const {
    return SourceRange((isObjectReceiver() ? getBase()->getLocStart()
                                           : getReceiverLocation()), 
                       IdLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCPropertyRefExprClass;
  }
  static bool classof(const ObjCPropertyRefExpr *) { return true; }

  // Iterators
  child_range children() {
    if (Receiver.is<Stmt*>()) {
      Stmt **begin = reinterpret_cast<Stmt**>(&Receiver); // hack!
      return child_range(begin, begin+1);
    }
    return child_range();
  }

private:
  friend class ASTStmtReader;
  void setExplicitProperty(ObjCPropertyDecl *D) {
    PropertyOrGetter.setPointer(D);
    PropertyOrGetter.setInt(false);
    Setter = 0;
  }
  void setImplicitProperty(ObjCMethodDecl *Getter, ObjCMethodDecl *Setter) {
    PropertyOrGetter.setPointer(Getter);
    PropertyOrGetter.setInt(true);
    this->Setter = Setter;
  }
  void setBase(Expr *Base) { Receiver = Base; }
  void setSuperReceiver(QualType T) { Receiver = T.getTypePtr(); }
  void setClassReceiver(ObjCInterfaceDecl *D) { Receiver = D; }

  void setLocation(SourceLocation L) { IdLoc = L; }
  void setReceiverLocation(SourceLocation Loc) { ReceiverLoc = Loc; }
};

/// \brief An expression that sends a message to the given Objective-C
/// object or class.
///
/// The following contains two message send expressions:
///
/// \code
///   [[NSString alloc] initWithString:@"Hello"]
/// \endcode
///
/// The innermost message send invokes the "alloc" class method on the
/// NSString class, while the outermost message send invokes the
/// "initWithString" instance method on the object returned from
/// NSString's "alloc". In all, an Objective-C message send can take
/// on four different (although related) forms:
///
///   1. Send to an object instance.
///   2. Send to a class.
///   3. Send to the superclass instance of the current class.
///   4. Send to the superclass of the current class.
///
/// All four kinds of message sends are modeled by the ObjCMessageExpr
/// class, and can be distinguished via \c getReceiverKind(). Example:
///
class ObjCMessageExpr : public Expr {
  /// \brief Stores either the selector that this message is sending
  /// to (when \c HasMethod is zero) or an \c ObjCMethodDecl pointer
  /// referring to the method that we type-checked against.
  uintptr_t SelectorOrMethod;

  enum { NumArgsBitWidth = 16 };

  /// \brief The number of arguments in the message send, not
  /// including the receiver.
  unsigned NumArgs : NumArgsBitWidth;
  
  void setNumArgs(unsigned Num) {
    assert((Num >> NumArgsBitWidth) == 0 && "Num of args is out of range!");
    NumArgs = Num;
  }

  /// \brief The kind of message send this is, which is one of the
  /// ReceiverKind values.
  ///
  /// We pad this out to a byte to avoid excessive masking and shifting.
  unsigned Kind : 8;

  /// \brief Whether we have an actual method prototype in \c
  /// SelectorOrMethod.
  ///
  /// When non-zero, we have a method declaration; otherwise, we just
  /// have a selector.
  unsigned HasMethod : 1;

  /// \brief Whether this message send is a "delegate init call",
  /// i.e. a call of an init method on self from within an init method.
  unsigned IsDelegateInitCall : 1;
  
  /// \brief Whether the locations of the selector identifiers are in a
  /// "standard" position, a enum SelectorLocationsKind.
  unsigned SelLocsKind : 2;

  /// \brief When the message expression is a send to 'super', this is
  /// the location of the 'super' keyword.
  SourceLocation SuperLoc;

  /// \brief The source locations of the open and close square
  /// brackets ('[' and ']', respectively).
  SourceLocation LBracLoc, RBracLoc;

  ObjCMessageExpr(EmptyShell Empty, unsigned NumArgs)
    : Expr(ObjCMessageExprClass, Empty), SelectorOrMethod(0), Kind(0), 
      HasMethod(0), IsDelegateInitCall(0) {
    setNumArgs(NumArgs);
  }

  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  SourceLocation SuperLoc,
                  bool IsInstanceSuper,
                  QualType SuperType,
                  Selector Sel, 
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc);
  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  TypeSourceInfo *Receiver,
                  Selector Sel, 
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc);
  ObjCMessageExpr(QualType T, ExprValueKind VK,
                  SourceLocation LBracLoc,
                  Expr *Receiver,
                  Selector Sel, 
                  ArrayRef<SourceLocation> SelLocs,
                  SelectorLocationsKind SelLocsK,
                  ObjCMethodDecl *Method,
                  ArrayRef<Expr *> Args,
                  SourceLocation RBracLoc);

  void initArgsAndSelLocs(ArrayRef<Expr *> Args,
                          ArrayRef<SourceLocation> SelLocs,
                          SelectorLocationsKind SelLocsK);

  /// \brief Retrieve the pointer value of the message receiver.
  void *getReceiverPointer() const {
    return *const_cast<void **>(
                             reinterpret_cast<const void * const*>(this + 1));
  }

  /// \brief Set the pointer value of the message receiver.
  void setReceiverPointer(void *Value) {
    *reinterpret_cast<void **>(this + 1) = Value;
  }

  SelectorLocationsKind getSelLocsKind() const {
    return (SelectorLocationsKind)SelLocsKind;
  }
  bool hasStandardSelLocs() const {
    return getSelLocsKind() != SelLoc_NonStandard;
  }

  /// \brief Get a pointer to the stored selector identifiers locations array.
  /// No locations will be stored if HasStandardSelLocs is true.
  SourceLocation *getStoredSelLocs() {
    return reinterpret_cast<SourceLocation*>(getArgs() + getNumArgs());
  }
  const SourceLocation *getStoredSelLocs() const {
    return reinterpret_cast<const SourceLocation*>(getArgs() + getNumArgs());
  }

  /// \brief Get the number of stored selector identifiers locations.
  /// No locations will be stored if HasStandardSelLocs is true.
  unsigned getNumStoredSelLocs() const {
    if (hasStandardSelLocs())
      return 0;
    return getNumSelectorLocs();
  }

  static ObjCMessageExpr *alloc(ASTContext &C,
                                ArrayRef<Expr *> Args,
                                SourceLocation RBraceLoc,
                                ArrayRef<SourceLocation> SelLocs,
                                Selector Sel,
                                SelectorLocationsKind &SelLocsK);
  static ObjCMessageExpr *alloc(ASTContext &C,
                                unsigned NumArgs,
                                unsigned NumStoredSelLocs);

public:
  /// \brief The kind of receiver this message is sending to.
  enum ReceiverKind {
    /// \brief The receiver is a class.
    Class = 0,
    /// \brief The receiver is an object instance.
    Instance,
    /// \brief The receiver is a superclass.
    SuperClass,
    /// \brief The receiver is the instance of the superclass object.
    SuperInstance
  };

  /// \brief Create a message send to super.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBrac The location of the open square bracket '['.
  ///
  /// \param SuperLoc The location of the "super" keyword.
  ///
  /// \param IsInstanceSuper Whether this is an instance "super"
  /// message (otherwise, it's a class "super" message).
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be NULL.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param NumArgs The number of arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(ASTContext &Context, QualType T, 
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 SourceLocation SuperLoc,
                                 bool IsInstanceSuper,
                                 QualType SuperType,
                                 Selector Sel, 
                                 ArrayRef<SourceLocation> SelLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc);

  /// \brief Create a class message send.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBrac The location of the open square bracket '['.
  ///
  /// \param Receiver The type of the receiver, including
  /// source-location information.
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be NULL.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param NumArgs The number of arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(ASTContext &Context, QualType T,
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 TypeSourceInfo *Receiver,
                                 Selector Sel, 
                                 ArrayRef<SourceLocation> SelLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc);

  /// \brief Create an instance message send.
  ///
  /// \param Context The ASTContext in which this expression will be created.
  ///
  /// \param T The result type of this message.
  ///
  /// \param VK The value kind of this message.  A message returning
  /// a l-value or r-value reference will be an l-value or x-value,
  /// respectively.
  ///
  /// \param LBrac The location of the open square bracket '['.
  ///
  /// \param Receiver The expression used to produce the object that
  /// will receive this message.
  ///
  /// \param Sel The selector used to determine which method gets called.
  ///
  /// \param Method The Objective-C method against which this message
  /// send was type-checked. May be NULL.
  ///
  /// \param Args The message send arguments.
  ///
  /// \param NumArgs The number of arguments.
  ///
  /// \param RBracLoc The location of the closing square bracket ']'.
  static ObjCMessageExpr *Create(ASTContext &Context, QualType T,
                                 ExprValueKind VK,
                                 SourceLocation LBracLoc,
                                 Expr *Receiver,
                                 Selector Sel, 
                                 ArrayRef<SourceLocation> SeLocs,
                                 ObjCMethodDecl *Method,
                                 ArrayRef<Expr *> Args,
                                 SourceLocation RBracLoc);

  /// \brief Create an empty Objective-C message expression, to be
  /// filled in by subsequent calls.
  ///
  /// \param Context The context in which the message send will be created.
  ///
  /// \param NumArgs The number of message arguments, not including
  /// the receiver.
  static ObjCMessageExpr *CreateEmpty(ASTContext &Context,
                                      unsigned NumArgs,
                                      unsigned NumStoredSelLocs);

  /// \brief Determine the kind of receiver that this message is being
  /// sent to.
  ReceiverKind getReceiverKind() const { return (ReceiverKind)Kind; }

  /// \brief Source range of the receiver.
  SourceRange getReceiverRange() const;

  /// \brief Determine whether this is an instance message to either a
  /// computed object or to super.
  bool isInstanceMessage() const {
    return getReceiverKind() == Instance || getReceiverKind() == SuperInstance;
  }

  /// \brief Determine whether this is an class message to either a
  /// specified class or to super.
  bool isClassMessage() const {
    return getReceiverKind() == Class || getReceiverKind() == SuperClass;
  }

  /// \brief Returns the receiver of an instance message.
  ///
  /// \brief Returns the object expression for an instance message, or
  /// NULL for a message that is not an instance message.
  Expr *getInstanceReceiver() {
    if (getReceiverKind() == Instance)
      return static_cast<Expr *>(getReceiverPointer());

    return 0;
  }
  const Expr *getInstanceReceiver() const {
    return const_cast<ObjCMessageExpr*>(this)->getInstanceReceiver();
  }

  /// \brief Turn this message send into an instance message that
  /// computes the receiver object with the given expression.
  void setInstanceReceiver(Expr *rec) { 
    Kind = Instance;
    setReceiverPointer(rec);
  }
  
  /// \brief Returns the type of a class message send, or NULL if the
  /// message is not a class message.
  QualType getClassReceiver() const { 
    if (TypeSourceInfo *TSInfo = getClassReceiverTypeInfo())
      return TSInfo->getType();

    return QualType();
  }

  /// \brief Returns a type-source information of a class message
  /// send, or NULL if the message is not a class message.
  TypeSourceInfo *getClassReceiverTypeInfo() const {
    if (getReceiverKind() == Class)
      return reinterpret_cast<TypeSourceInfo *>(getReceiverPointer());
    return 0;
  }

  void setClassReceiver(TypeSourceInfo *TSInfo) {
    Kind = Class;
    setReceiverPointer(TSInfo);
  }

  /// \brief Retrieve the location of the 'super' keyword for a class
  /// or instance message to 'super', otherwise an invalid source location.
  SourceLocation getSuperLoc() const { 
    if (getReceiverKind() == SuperInstance || getReceiverKind() == SuperClass)
      return SuperLoc;

    return SourceLocation();
  }

  /// \brief Retrieve the Objective-C interface to which this message
  /// is being directed, if known.
  ///
  /// This routine cross-cuts all of the different kinds of message
  /// sends to determine what the underlying (statically known) type
  /// of the receiver will be; use \c getReceiverKind() to determine
  /// whether the message is a class or an instance method, whether it
  /// is a send to super or not, etc.
  ///
  /// \returns The Objective-C interface if known, otherwise NULL.
  ObjCInterfaceDecl *getReceiverInterface() const;

  /// \brief Retrieve the type referred to by 'super'. 
  ///
  /// The returned type will either be an ObjCInterfaceType (for an
  /// class message to super) or an ObjCObjectPointerType that refers
  /// to a class (for an instance message to super);
  QualType getSuperType() const {
    if (getReceiverKind() == SuperInstance || getReceiverKind() == SuperClass)
      return QualType::getFromOpaquePtr(getReceiverPointer());

    return QualType();
  }

  void setSuper(SourceLocation Loc, QualType T, bool IsInstanceSuper) {
    Kind = IsInstanceSuper? SuperInstance : SuperClass;
    SuperLoc = Loc;
    setReceiverPointer(T.getAsOpaquePtr());
  }

  Selector getSelector() const;

  void setSelector(Selector S) { 
    HasMethod = false;
    SelectorOrMethod = reinterpret_cast<uintptr_t>(S.getAsOpaquePtr());
  }

  const ObjCMethodDecl *getMethodDecl() const { 
    if (HasMethod)
      return reinterpret_cast<const ObjCMethodDecl *>(SelectorOrMethod);

    return 0;
  }

  ObjCMethodDecl *getMethodDecl() { 
    if (HasMethod)
      return reinterpret_cast<ObjCMethodDecl *>(SelectorOrMethod);

    return 0;
  }

  void setMethodDecl(ObjCMethodDecl *MD) { 
    HasMethod = true;
    SelectorOrMethod = reinterpret_cast<uintptr_t>(MD);
  }

  ObjCMethodFamily getMethodFamily() const {
    if (HasMethod) return getMethodDecl()->getMethodFamily();
    return getSelector().getMethodFamily();
  }

  /// \brief Return the number of actual arguments in this message,
  /// not counting the receiver.
  unsigned getNumArgs() const { return NumArgs; }

  /// \brief Retrieve the arguments to this message, not including the
  /// receiver.
  Expr **getArgs() {
    return reinterpret_cast<Expr **>(this + 1) + 1;
  }
  const Expr * const *getArgs() const {
    return reinterpret_cast<const Expr * const *>(this + 1) + 1;
  }

  /// getArg - Return the specified argument.
  Expr *getArg(unsigned Arg) {
    assert(Arg < NumArgs && "Arg access out of range!");
    return cast<Expr>(getArgs()[Arg]);
  }
  const Expr *getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return cast<Expr>(getArgs()[Arg]);
  }
  /// setArg - Set the specified argument.
  void setArg(unsigned Arg, Expr *ArgExpr) {
    assert(Arg < NumArgs && "Arg access out of range!");
    getArgs()[Arg] = ArgExpr;
  }

  /// isDelegateInitCall - Answers whether this message send has been
  /// tagged as a "delegate init call", i.e. a call to a method in the
  /// -init family on self from within an -init method implementation.
  bool isDelegateInitCall() const { return IsDelegateInitCall; }
  void setDelegateInitCall(bool isDelegate) { IsDelegateInitCall = isDelegate; }

  SourceLocation getLeftLoc() const { return LBracLoc; }
  SourceLocation getRightLoc() const { return RBracLoc; }

  SourceLocation getSelectorStartLoc() const { return getSelectorLoc(0); }
  SourceLocation getSelectorLoc(unsigned Index) const {
    assert(Index < getNumSelectorLocs() && "Index out of range!");
    if (hasStandardSelLocs())
      return getStandardSelectorLoc(Index, getSelector(),
                                   getSelLocsKind() == SelLoc_StandardWithSpace,
                               llvm::makeArrayRef(const_cast<Expr**>(getArgs()),
                                                  getNumArgs()),
                                   RBracLoc);
    return getStoredSelLocs()[Index];
  }

  void getSelectorLocs(SmallVectorImpl<SourceLocation> &SelLocs) const;

  unsigned getNumSelectorLocs() const {
    Selector Sel = getSelector();
    if (Sel.isUnarySelector())
      return 1;
    return Sel.getNumArgs();
  }

  void setSourceRange(SourceRange R) {
    LBracLoc = R.getBegin();
    RBracLoc = R.getEnd();
  }
  SourceRange getSourceRange() const {
    return SourceRange(LBracLoc, RBracLoc);
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCMessageExprClass;
  }
  static bool classof(const ObjCMessageExpr *) { return true; }

  // Iterators
  child_range children();

  typedef ExprIterator arg_iterator;
  typedef ConstExprIterator const_arg_iterator;

  arg_iterator arg_begin() { return reinterpret_cast<Stmt **>(getArgs()); }
  arg_iterator arg_end()   { 
    return reinterpret_cast<Stmt **>(getArgs() + NumArgs); 
  }
  const_arg_iterator arg_begin() const { 
    return reinterpret_cast<Stmt const * const*>(getArgs()); 
  }
  const_arg_iterator arg_end() const { 
    return reinterpret_cast<Stmt const * const*>(getArgs() + NumArgs); 
  }

  friend class ASTStmtReader;
  friend class ASTStmtWriter;
};

/// ObjCIsaExpr - Represent X->isa and X.isa when X is an ObjC 'id' type.
/// (similar in spirit to MemberExpr).
class ObjCIsaExpr : public Expr {
  /// Base - the expression for the base object pointer.
  Stmt *Base;

  /// IsaMemberLoc - This is the location of the 'isa'.
  SourceLocation IsaMemberLoc;

  /// IsArrow - True if this is "X->F", false if this is "X.F".
  bool IsArrow;
public:
  ObjCIsaExpr(Expr *base, bool isarrow, SourceLocation l, QualType ty)
    : Expr(ObjCIsaExprClass, ty, VK_LValue, OK_Ordinary,
           /*TypeDependent=*/false, base->isValueDependent(),
           base->isInstantiationDependent(),
           /*ContainsUnexpandedParameterPack=*/false),
      Base(base), IsaMemberLoc(l), IsArrow(isarrow) {}

  /// \brief Build an empty expression.
  explicit ObjCIsaExpr(EmptyShell Empty) : Expr(ObjCIsaExprClass, Empty) { }

  void setBase(Expr *E) { Base = E; }
  Expr *getBase() const { return cast<Expr>(Base); }

  bool isArrow() const { return IsArrow; }
  void setArrow(bool A) { IsArrow = A; }

  /// getMemberLoc - Return the location of the "member", in X->F, it is the
  /// location of 'F'.
  SourceLocation getIsaMemberLoc() const { return IsaMemberLoc; }
  void setIsaMemberLoc(SourceLocation L) { IsaMemberLoc = L; }

  SourceRange getSourceRange() const {
    return SourceRange(getBase()->getLocStart(), IsaMemberLoc);
  }

  SourceLocation getExprLoc() const { return IsaMemberLoc; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCIsaExprClass;
  }
  static bool classof(const ObjCIsaExpr *) { return true; }

  // Iterators
  child_range children() { return child_range(&Base, &Base+1); }
};


/// ObjCIndirectCopyRestoreExpr - Represents the passing of a function
/// argument by indirect copy-restore in ARC.  This is used to support
/// passing indirect arguments with the wrong lifetime, e.g. when
/// passing the address of a __strong local variable to an 'out'
/// parameter.  This expression kind is only valid in an "argument"
/// position to some sort of call expression.
///
/// The parameter must have type 'pointer to T', and the argument must
/// have type 'pointer to U', where T and U agree except possibly in
/// qualification.  If the argument value is null, then a null pointer
/// is passed;  otherwise it points to an object A, and:
/// 1. A temporary object B of type T is initialized, either by
///    zero-initialization (used when initializing an 'out' parameter)
///    or copy-initialization (used when initializing an 'inout'
///    parameter).
/// 2. The address of the temporary is passed to the function.
/// 3. If the call completes normally, A is move-assigned from B.
/// 4. Finally, A is destroyed immediately.
///
/// Currently 'T' must be a retainable object lifetime and must be
/// __autoreleasing;  this qualifier is ignored when initializing
/// the value.
class ObjCIndirectCopyRestoreExpr : public Expr {
  Stmt *Operand;

  // unsigned ObjCIndirectCopyRestoreBits.ShouldCopy : 1;

  friend class ASTReader;
  friend class ASTStmtReader;

  void setShouldCopy(bool shouldCopy) {
    ObjCIndirectCopyRestoreExprBits.ShouldCopy = shouldCopy;
  }

  explicit ObjCIndirectCopyRestoreExpr(EmptyShell Empty)
    : Expr(ObjCIndirectCopyRestoreExprClass, Empty) { }

public:
  ObjCIndirectCopyRestoreExpr(Expr *operand, QualType type, bool shouldCopy)
    : Expr(ObjCIndirectCopyRestoreExprClass, type, VK_LValue, OK_Ordinary,
           operand->isTypeDependent(), operand->isValueDependent(),
           operand->isInstantiationDependent(),
           operand->containsUnexpandedParameterPack()),
      Operand(operand) {
    setShouldCopy(shouldCopy);
  }

  Expr *getSubExpr() { return cast<Expr>(Operand); }
  const Expr *getSubExpr() const { return cast<Expr>(Operand); }

  /// shouldCopy - True if we should do the 'copy' part of the
  /// copy-restore.  If false, the temporary will be zero-initialized.
  bool shouldCopy() const { return ObjCIndirectCopyRestoreExprBits.ShouldCopy; }

  child_range children() { return child_range(&Operand, &Operand+1); }  

  // Source locations are determined by the subexpression.
  SourceRange getSourceRange() const { return Operand->getSourceRange(); }
  SourceLocation getExprLoc() const { return getSubExpr()->getExprLoc(); }

  static bool classof(const Stmt *s) {
    return s->getStmtClass() == ObjCIndirectCopyRestoreExprClass;
  }
  static bool classof(const ObjCIndirectCopyRestoreExpr *) { return true; }
};

/// \brief An Objective-C "bridged" cast expression, which casts between
/// Objective-C pointers and C pointers, transferring ownership in the process.
///
/// \code
/// NSString *str = (__bridge_transfer NSString *)CFCreateString();
/// \endcode
class ObjCBridgedCastExpr : public ExplicitCastExpr {
  SourceLocation LParenLoc;
  SourceLocation BridgeKeywordLoc;
  unsigned Kind : 2;
  
  friend class ASTStmtReader;
  friend class ASTStmtWriter;
  
public:
  ObjCBridgedCastExpr(SourceLocation LParenLoc, ObjCBridgeCastKind Kind,
                      CastKind CK, SourceLocation BridgeKeywordLoc,
                      TypeSourceInfo *TSInfo, Expr *Operand)
    : ExplicitCastExpr(ObjCBridgedCastExprClass, TSInfo->getType(), VK_RValue,
                       CK, Operand, 0, TSInfo),
      LParenLoc(LParenLoc), BridgeKeywordLoc(BridgeKeywordLoc), Kind(Kind) { }
  
  /// \brief Construct an empty Objective-C bridged cast.
  explicit ObjCBridgedCastExpr(EmptyShell Shell)
    : ExplicitCastExpr(ObjCBridgedCastExprClass, Shell, 0) { }

  SourceLocation getLParenLoc() const { return LParenLoc; }

  /// \brief Determine which kind of bridge is being performed via this cast.
  ObjCBridgeCastKind getBridgeKind() const { 
    return static_cast<ObjCBridgeCastKind>(Kind); 
  }
  
  /// \brief Retrieve the kind of bridge being performed as a string.
  StringRef getBridgeKindName() const;
  
  /// \brief The location of the bridge keyword.
  SourceLocation getBridgeKeywordLoc() const { return BridgeKeywordLoc; }
  
  SourceRange getSourceRange() const {
    return SourceRange(LParenLoc, getSubExpr()->getLocEnd());
  }
  
  static bool classof(const Stmt *T) {
    return T->getStmtClass() == ObjCBridgedCastExprClass;
  }
  static bool classof(const ObjCBridgedCastExpr *) { return true; }
 
};
  
}  // end namespace clang

#endif
