//===--- ASTDiagnostic.cpp - Diagnostic Printing Hooks for AST Nodes ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a diagnostic formatting hook for AST elements.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/ASTDiagnostic.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

// Returns a desugared version of the QualType, and marks ShouldAKA as true
// whenever we remove significant sugar from the type.
static QualType Desugar(ASTContext &Context, QualType QT, bool &ShouldAKA) {
  QualifierCollector QC;

  while (true) {
    const Type *Ty = QC.strip(QT);

    // Don't aka just because we saw an elaborated type...
    if (const ElaboratedType *ET = dyn_cast<ElaboratedType>(Ty)) {
      QT = ET->desugar();
      continue;
    }
    // ... or a paren type ...
    if (const ParenType *PT = dyn_cast<ParenType>(Ty)) {
      QT = PT->desugar();
      continue;
    }
    // ...or a substituted template type parameter ...
    if (const SubstTemplateTypeParmType *ST =
          dyn_cast<SubstTemplateTypeParmType>(Ty)) {
      QT = ST->desugar();
      continue;
    }
    // ...or an attributed type...
    if (const AttributedType *AT = dyn_cast<AttributedType>(Ty)) {
      QT = AT->desugar();
      continue;
    }
    // ... or an auto type.
    if (const AutoType *AT = dyn_cast<AutoType>(Ty)) {
      if (!AT->isSugared())
        break;
      QT = AT->desugar();
      continue;
    }

    // Don't desugar template specializations, unless it's an alias template.
    if (const TemplateSpecializationType *TST
          = dyn_cast<TemplateSpecializationType>(Ty))
      if (!TST->isTypeAlias())
        break;

    // Don't desugar magic Objective-C types.
    if (QualType(Ty,0) == Context.getObjCIdType() ||
        QualType(Ty,0) == Context.getObjCClassType() ||
        QualType(Ty,0) == Context.getObjCSelType() ||
        QualType(Ty,0) == Context.getObjCProtoType())
      break;

    // Don't desugar va_list.
    if (QualType(Ty,0) == Context.getBuiltinVaListType())
      break;

    // Otherwise, do a single-step desugar.
    QualType Underlying;
    bool IsSugar = false;
    switch (Ty->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base) \
case Type::Class: { \
const Class##Type *CTy = cast<Class##Type>(Ty); \
if (CTy->isSugared()) { \
IsSugar = true; \
Underlying = CTy->desugar(); \
} \
break; \
}
#include "clang/AST/TypeNodes.def"
    }

    // If it wasn't sugared, we're done.
    if (!IsSugar)
      break;

    // If the desugared type is a vector type, we don't want to expand
    // it, it will turn into an attribute mess. People want their "vec4".
    if (isa<VectorType>(Underlying))
      break;

    // Don't desugar through the primary typedef of an anonymous type.
    if (const TagType *UTT = Underlying->getAs<TagType>())
      if (const TypedefType *QTT = dyn_cast<TypedefType>(QT))
        if (UTT->getDecl()->getTypedefNameForAnonDecl() == QTT->getDecl())
          break;

    // Record that we actually looked through an opaque type here.
    ShouldAKA = true;
    QT = Underlying;
  }

  // If we have a pointer-like type, desugar the pointee as well.
  // FIXME: Handle other pointer-like types.
  if (const PointerType *Ty = QT->getAs<PointerType>()) {
    QT = Context.getPointerType(Desugar(Context, Ty->getPointeeType(),
                                        ShouldAKA));
  } else if (const LValueReferenceType *Ty = QT->getAs<LValueReferenceType>()) {
    QT = Context.getLValueReferenceType(Desugar(Context, Ty->getPointeeType(),
                                                ShouldAKA));
  } else if (const RValueReferenceType *Ty = QT->getAs<RValueReferenceType>()) {
    QT = Context.getRValueReferenceType(Desugar(Context, Ty->getPointeeType(),
                                                ShouldAKA));
  }

  return QC.apply(Context, QT);
}

/// \brief Convert the given type to a string suitable for printing as part of 
/// a diagnostic.
///
/// There are four main criteria when determining whether we should have an
/// a.k.a. clause when pretty-printing a type:
///
/// 1) Some types provide very minimal sugar that doesn't impede the
///    user's understanding --- for example, elaborated type
///    specifiers.  If this is all the sugar we see, we don't want an
///    a.k.a. clause.
/// 2) Some types are technically sugared but are much more familiar
///    when seen in their sugared form --- for example, va_list,
///    vector types, and the magic Objective C types.  We don't
///    want to desugar these, even if we do produce an a.k.a. clause.
/// 3) Some types may have already been desugared previously in this diagnostic.
///    if this is the case, doing another "aka" would just be clutter.
/// 4) Two different types within the same diagnostic have the same output
///    string.  In this case, force an a.k.a with the desugared type when
///    doing so will provide additional information.
///
/// \param Context the context in which the type was allocated
/// \param Ty the type to print
/// \param QualTypeVals pointer values to QualTypes which are used in the
/// diagnostic message
static std::string
ConvertTypeToDiagnosticString(ASTContext &Context, QualType Ty,
                              const DiagnosticsEngine::ArgumentValue *PrevArgs,
                              unsigned NumPrevArgs,
                              ArrayRef<intptr_t> QualTypeVals) {
  // FIXME: Playing with std::string is really slow.
  bool ForceAKA = false;
  QualType CanTy = Ty.getCanonicalType();
  std::string S = Ty.getAsString(Context.getPrintingPolicy());
  std::string CanS = CanTy.getAsString(Context.getPrintingPolicy());

  for (unsigned I = 0, E = QualTypeVals.size(); I != E; ++I) {
    QualType CompareTy =
        QualType::getFromOpaquePtr(reinterpret_cast<void*>(QualTypeVals[I]));
    if (CompareTy.isNull())
      continue;
    if (CompareTy == Ty)
      continue;  // Same types
    QualType CompareCanTy = CompareTy.getCanonicalType();
    if (CompareCanTy == CanTy)
      continue;  // Same canonical types
    std::string CompareS = CompareTy.getAsString(Context.getPrintingPolicy());
    bool aka;
    QualType CompareDesugar = Desugar(Context, CompareTy, aka);
    std::string CompareDesugarStr =
        CompareDesugar.getAsString(Context.getPrintingPolicy());
    if (CompareS != S && CompareDesugarStr != S)
      continue;  // The type string is different than the comparison string
                 // and the desugared comparison string.
    std::string CompareCanS =
        CompareCanTy.getAsString(Context.getPrintingPolicy());
    
    if (CompareCanS == CanS)
      continue;  // No new info from canonical type

    ForceAKA = true;
    break;
  }

  // Check to see if we already desugared this type in this
  // diagnostic.  If so, don't do it again.
  bool Repeated = false;
  for (unsigned i = 0; i != NumPrevArgs; ++i) {
    // TODO: Handle ak_declcontext case.
    if (PrevArgs[i].first == DiagnosticsEngine::ak_qualtype) {
      void *Ptr = (void*)PrevArgs[i].second;
      QualType PrevTy(QualType::getFromOpaquePtr(Ptr));
      if (PrevTy == Ty) {
        Repeated = true;
        break;
      }
    }
  }

  // Consider producing an a.k.a. clause if removing all the direct
  // sugar gives us something "significantly different".
  if (!Repeated) {
    bool ShouldAKA = false;
    QualType DesugaredTy = Desugar(Context, Ty, ShouldAKA);
    if (ShouldAKA || ForceAKA) {
      if (DesugaredTy == Ty) {
        DesugaredTy = Ty.getCanonicalType();
      }
      std::string akaStr = DesugaredTy.getAsString(Context.getPrintingPolicy());
      if (akaStr != S) {
        S = "'" + S + "' (aka '" + akaStr + "')";
        return S;
      }
    }
  }

  S = "'" + S + "'";
  return S;
}

void clang::FormatASTNodeDiagnosticArgument(
    DiagnosticsEngine::ArgumentKind Kind,
    intptr_t Val,
    const char *Modifier,
    unsigned ModLen,
    const char *Argument,
    unsigned ArgLen,
    const DiagnosticsEngine::ArgumentValue *PrevArgs,
    unsigned NumPrevArgs,
    SmallVectorImpl<char> &Output,
    void *Cookie,
    ArrayRef<intptr_t> QualTypeVals) {
  ASTContext &Context = *static_cast<ASTContext*>(Cookie);
  
  std::string S;
  bool NeedQuotes = true;
  
  switch (Kind) {
    default: llvm_unreachable("unknown ArgumentKind");
    case DiagnosticsEngine::ak_qualtype: {
      assert(ModLen == 0 && ArgLen == 0 &&
             "Invalid modifier for QualType argument");
      
      QualType Ty(QualType::getFromOpaquePtr(reinterpret_cast<void*>(Val)));
      S = ConvertTypeToDiagnosticString(Context, Ty, PrevArgs, NumPrevArgs,
                                        QualTypeVals);
      NeedQuotes = false;
      break;
    }
    case DiagnosticsEngine::ak_declarationname: {
      DeclarationName N = DeclarationName::getFromOpaqueInteger(Val);
      S = N.getAsString();
      
      if (ModLen == 9 && !memcmp(Modifier, "objcclass", 9) && ArgLen == 0)
        S = '+' + S;
      else if (ModLen == 12 && !memcmp(Modifier, "objcinstance", 12)
                && ArgLen==0)
        S = '-' + S;
      else
        assert(ModLen == 0 && ArgLen == 0 &&
               "Invalid modifier for DeclarationName argument");
      break;
    }
    case DiagnosticsEngine::ak_nameddecl: {
      bool Qualified;
      if (ModLen == 1 && Modifier[0] == 'q' && ArgLen == 0)
        Qualified = true;
      else {
        assert(ModLen == 0 && ArgLen == 0 &&
               "Invalid modifier for NamedDecl* argument");
        Qualified = false;
      }
      const NamedDecl *ND = reinterpret_cast<const NamedDecl*>(Val);
      ND->getNameForDiagnostic(S, Context.getPrintingPolicy(), Qualified);
      break;
    }
    case DiagnosticsEngine::ak_nestednamespec: {
      llvm::raw_string_ostream OS(S);
      reinterpret_cast<NestedNameSpecifier*>(Val)->print(OS,
                                                        Context.getPrintingPolicy());
      NeedQuotes = false;
      break;
    }
    case DiagnosticsEngine::ak_declcontext: {
      DeclContext *DC = reinterpret_cast<DeclContext *> (Val);
      assert(DC && "Should never have a null declaration context");
      
      if (DC->isTranslationUnit()) {
        // FIXME: Get these strings from some localized place
        if (Context.getLangOpts().CPlusPlus)
          S = "the global namespace";
        else
          S = "the global scope";
      } else if (TypeDecl *Type = dyn_cast<TypeDecl>(DC)) {
        S = ConvertTypeToDiagnosticString(Context, 
                                          Context.getTypeDeclType(Type),
                                          PrevArgs, NumPrevArgs, QualTypeVals);
      } else {
        // FIXME: Get these strings from some localized place
        NamedDecl *ND = cast<NamedDecl>(DC);
        if (isa<NamespaceDecl>(ND))
          S += "namespace ";
        else if (isa<ObjCMethodDecl>(ND))
          S += "method ";
        else if (isa<FunctionDecl>(ND))
          S += "function ";
        
        S += "'";
        ND->getNameForDiagnostic(S, Context.getPrintingPolicy(), true);
        S += "'";
      }
      NeedQuotes = false;
      break;
    }
  }
  
  if (NeedQuotes)
    Output.push_back('\'');
  
  Output.append(S.begin(), S.end());
  
  if (NeedQuotes)
    Output.push_back('\'');
}
