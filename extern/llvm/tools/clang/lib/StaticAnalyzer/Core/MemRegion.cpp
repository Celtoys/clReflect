//== MemRegion.cpp - Abstract memory regions for static analysis --*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines MemRegion and its subclasses.  MemRegion defines a
//  partially-typed abstraction of memory useful for path-sensitive dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/RecordLayout.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// MemRegion Construction.
//===----------------------------------------------------------------------===//

template<typename RegionTy> struct MemRegionManagerTrait;

template <typename RegionTy, typename A1>
RegionTy* MemRegionManager::getRegion(const A1 a1) {

  const typename MemRegionManagerTrait<RegionTy>::SuperRegionTy *superRegion =
  MemRegionManagerTrait<RegionTy>::getSuperRegion(*this, a1);

  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, a1, superRegion);
  void *InsertPos;
  RegionTy* R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID,
                                                                   InsertPos));

  if (!R) {
    R = (RegionTy*) A.Allocate<RegionTy>();
    new (R) RegionTy(a1, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename A1>
RegionTy* MemRegionManager::getSubRegion(const A1 a1,
                                         const MemRegion *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, a1, superRegion);
  void *InsertPos;
  RegionTy* R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID,
                                                                   InsertPos));

  if (!R) {
    R = (RegionTy*) A.Allocate<RegionTy>();
    new (R) RegionTy(a1, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename A1, typename A2>
RegionTy* MemRegionManager::getRegion(const A1 a1, const A2 a2) {

  const typename MemRegionManagerTrait<RegionTy>::SuperRegionTy *superRegion =
  MemRegionManagerTrait<RegionTy>::getSuperRegion(*this, a1, a2);

  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, a1, a2, superRegion);
  void *InsertPos;
  RegionTy* R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID,
                                                                   InsertPos));

  if (!R) {
    R = (RegionTy*) A.Allocate<RegionTy>();
    new (R) RegionTy(a1, a2, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename A1, typename A2>
RegionTy* MemRegionManager::getSubRegion(const A1 a1, const A2 a2,
                                         const MemRegion *superRegion) {

  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, a1, a2, superRegion);
  void *InsertPos;
  RegionTy* R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID,
                                                                   InsertPos));

  if (!R) {
    R = (RegionTy*) A.Allocate<RegionTy>();
    new (R) RegionTy(a1, a2, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename A1, typename A2, typename A3>
RegionTy* MemRegionManager::getSubRegion(const A1 a1, const A2 a2, const A3 a3,
                                         const MemRegion *superRegion) {

  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, a1, a2, a3, superRegion);
  void *InsertPos;
  RegionTy* R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID,
                                                                   InsertPos));

  if (!R) {
    R = (RegionTy*) A.Allocate<RegionTy>();
    new (R) RegionTy(a1, a2, a3, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

//===----------------------------------------------------------------------===//
// Object destruction.
//===----------------------------------------------------------------------===//

MemRegion::~MemRegion() {}

MemRegionManager::~MemRegionManager() {
  // All regions and their data are BumpPtrAllocated.  No need to call
  // their destructors.
}

//===----------------------------------------------------------------------===//
// Basic methods.
//===----------------------------------------------------------------------===//

bool SubRegion::isSubRegionOf(const MemRegion* R) const {
  const MemRegion* r = getSuperRegion();
  while (r != 0) {
    if (r == R)
      return true;
    if (const SubRegion* sr = dyn_cast<SubRegion>(r))
      r = sr->getSuperRegion();
    else
      break;
  }
  return false;
}

MemRegionManager* SubRegion::getMemRegionManager() const {
  const SubRegion* r = this;
  do {
    const MemRegion *superRegion = r->getSuperRegion();
    if (const SubRegion *sr = dyn_cast<SubRegion>(superRegion)) {
      r = sr;
      continue;
    }
    return superRegion->getMemRegionManager();
  } while (1);
}

const StackFrameContext *VarRegion::getStackFrame() const {
  const StackSpaceRegion *SSR = dyn_cast<StackSpaceRegion>(getMemorySpace());
  return SSR ? SSR->getStackFrame() : NULL;
}

//===----------------------------------------------------------------------===//
// Region extents.
//===----------------------------------------------------------------------===//

DefinedOrUnknownSVal DeclRegion::getExtent(SValBuilder &svalBuilder) const {
  ASTContext &Ctx = svalBuilder.getContext();
  QualType T = getDesugaredValueType(Ctx);

  if (isa<VariableArrayType>(T))
    return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
  if (isa<IncompleteArrayType>(T))
    return UnknownVal();

  CharUnits size = Ctx.getTypeSizeInChars(T);
  QualType sizeTy = svalBuilder.getArrayIndexType();
  return svalBuilder.makeIntVal(size.getQuantity(), sizeTy);
}

DefinedOrUnknownSVal FieldRegion::getExtent(SValBuilder &svalBuilder) const {
  DefinedOrUnknownSVal Extent = DeclRegion::getExtent(svalBuilder);

  // A zero-length array at the end of a struct often stands for dynamically-
  // allocated extra memory.
  if (Extent.isZeroConstant()) {
    QualType T = getDesugaredValueType(svalBuilder.getContext());

    if (isa<ConstantArrayType>(T))
      return UnknownVal();
  }

  return Extent;
}

DefinedOrUnknownSVal AllocaRegion::getExtent(SValBuilder &svalBuilder) const {
  return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
}

DefinedOrUnknownSVal SymbolicRegion::getExtent(SValBuilder &svalBuilder) const {
  return nonloc::SymbolVal(svalBuilder.getSymbolManager().getExtentSymbol(this));
}

DefinedOrUnknownSVal StringRegion::getExtent(SValBuilder &svalBuilder) const {
  return svalBuilder.makeIntVal(getStringLiteral()->getByteLength()+1,
                                svalBuilder.getArrayIndexType());
}

QualType CXXBaseObjectRegion::getValueType() const {
  return QualType(decl->getTypeForDecl(), 0);
}

//===----------------------------------------------------------------------===//
// FoldingSet profiling.
//===----------------------------------------------------------------------===//

void MemSpaceRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ID.AddInteger((unsigned)getKind());
}

void StackSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger((unsigned)getKind());
  ID.AddPointer(getStackFrame());
}

void StaticGlobalSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger((unsigned)getKind());
  ID.AddPointer(getCodeRegion());
}

void StringRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                 const StringLiteral* Str,
                                 const MemRegion* superRegion) {
  ID.AddInteger((unsigned) StringRegionKind);
  ID.AddPointer(Str);
  ID.AddPointer(superRegion);
}

void AllocaRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                 const Expr *Ex, unsigned cnt,
                                 const MemRegion *) {
  ID.AddInteger((unsigned) AllocaRegionKind);
  ID.AddPointer(Ex);
  ID.AddInteger(cnt);
}

void AllocaRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ProfileRegion(ID, Ex, Cnt, superRegion);
}

void CompoundLiteralRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  CompoundLiteralRegion::ProfileRegion(ID, CL, superRegion);
}

void CompoundLiteralRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                          const CompoundLiteralExpr *CL,
                                          const MemRegion* superRegion) {
  ID.AddInteger((unsigned) CompoundLiteralRegionKind);
  ID.AddPointer(CL);
  ID.AddPointer(superRegion);
}

void CXXThisRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                  const PointerType *PT,
                                  const MemRegion *sRegion) {
  ID.AddInteger((unsigned) CXXThisRegionKind);
  ID.AddPointer(PT);
  ID.AddPointer(sRegion);
}

void CXXThisRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  CXXThisRegion::ProfileRegion(ID, ThisPointerTy, superRegion);
}

void DeclRegion::ProfileRegion(llvm::FoldingSetNodeID& ID, const Decl *D,
                               const MemRegion* superRegion, Kind k) {
  ID.AddInteger((unsigned) k);
  ID.AddPointer(D);
  ID.AddPointer(superRegion);
}

void DeclRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  DeclRegion::ProfileRegion(ID, D, superRegion, getKind());
}

void VarRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  VarRegion::ProfileRegion(ID, getDecl(), superRegion);
}

void SymbolicRegion::ProfileRegion(llvm::FoldingSetNodeID& ID, SymbolRef sym,
                                   const MemRegion *sreg) {
  ID.AddInteger((unsigned) MemRegion::SymbolicRegionKind);
  ID.Add(sym);
  ID.AddPointer(sreg);
}

void SymbolicRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  SymbolicRegion::ProfileRegion(ID, sym, getSuperRegion());
}

void ElementRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                  QualType ElementType, SVal Idx,
                                  const MemRegion* superRegion) {
  ID.AddInteger(MemRegion::ElementRegionKind);
  ID.Add(ElementType);
  ID.AddPointer(superRegion);
  Idx.Profile(ID);
}

void ElementRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ElementRegion::ProfileRegion(ID, ElementType, Index, superRegion);
}

void FunctionTextRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                       const FunctionDecl *FD,
                                       const MemRegion*) {
  ID.AddInteger(MemRegion::FunctionTextRegionKind);
  ID.AddPointer(FD);
}

void FunctionTextRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  FunctionTextRegion::ProfileRegion(ID, FD, superRegion);
}

void BlockTextRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockDecl *BD, CanQualType,
                                    const AnalysisContext *AC,
                                    const MemRegion*) {
  ID.AddInteger(MemRegion::BlockTextRegionKind);
  ID.AddPointer(BD);
}

void BlockTextRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockTextRegion::ProfileRegion(ID, BD, locTy, AC, superRegion);
}

void BlockDataRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockTextRegion *BC,
                                    const LocationContext *LC,
                                    const MemRegion *sReg) {
  ID.AddInteger(MemRegion::BlockDataRegionKind);
  ID.AddPointer(BC);
  ID.AddPointer(LC);
  ID.AddPointer(sReg);
}

void BlockDataRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockDataRegion::ProfileRegion(ID, BC, LC, getSuperRegion());
}

void CXXTempObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        Expr const *Ex,
                                        const MemRegion *sReg) {
  ID.AddPointer(Ex);
  ID.AddPointer(sReg);
}

void CXXTempObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, Ex, getSuperRegion());
}

void CXXBaseObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        const CXXRecordDecl *decl,
                                        const MemRegion *sReg) {
  ID.AddPointer(decl);
  ID.AddPointer(sReg);
}

void CXXBaseObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, decl, superRegion);
}

//===----------------------------------------------------------------------===//
// Region pretty-printing.
//===----------------------------------------------------------------------===//

void MemRegion::dump() const {
  dumpToStream(llvm::errs());
}

std::string MemRegion::getString() const {
  std::string s;
  llvm::raw_string_ostream os(s);
  dumpToStream(os);
  return os.str();
}

void MemRegion::dumpToStream(raw_ostream &os) const {
  os << "<Unknown Region>";
}

void AllocaRegion::dumpToStream(raw_ostream &os) const {
  os << "alloca{" << (void*) Ex << ',' << Cnt << '}';
}

void FunctionTextRegion::dumpToStream(raw_ostream &os) const {
  os << "code{" << getDecl()->getDeclName().getAsString() << '}';
}

void BlockTextRegion::dumpToStream(raw_ostream &os) const {
  os << "block_code{" << (void*) this << '}';
}

void BlockDataRegion::dumpToStream(raw_ostream &os) const {
  os << "block_data{" << BC << '}';
}

void CompoundLiteralRegion::dumpToStream(raw_ostream &os) const {
  // FIXME: More elaborate pretty-printing.
  os << "{ " << (void*) CL <<  " }";
}

void CXXTempObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "temp_object{" << getValueType().getAsString() << ','
     << (void*) Ex << '}';
}

void CXXBaseObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "base " << decl->getName();
}

void CXXThisRegion::dumpToStream(raw_ostream &os) const {
  os << "this";
}

void ElementRegion::dumpToStream(raw_ostream &os) const {
  os << "element{" << superRegion << ','
     << Index << ',' << getElementType().getAsString() << '}';
}

void FieldRegion::dumpToStream(raw_ostream &os) const {
  os << superRegion << "->" << *getDecl();
}

void NonStaticGlobalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "NonStaticGlobalSpaceRegion";
}

void ObjCIvarRegion::dumpToStream(raw_ostream &os) const {
  os << "ivar{" << superRegion << ',' << *getDecl() << '}';
}

void StringRegion::dumpToStream(raw_ostream &os) const {
  Str->printPretty(os, 0, PrintingPolicy(getContext().getLangOptions()));
}

void SymbolicRegion::dumpToStream(raw_ostream &os) const {
  os << "SymRegion{" << sym << '}';
}

void VarRegion::dumpToStream(raw_ostream &os) const {
  os << *cast<VarDecl>(D);
}

void RegionRawOffset::dump() const {
  dumpToStream(llvm::errs());
}

void RegionRawOffset::dumpToStream(raw_ostream &os) const {
  os << "raw_offset{" << getRegion() << ',' << getOffset().getQuantity() << '}';
}

void StaticGlobalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StaticGlobalsMemSpace{" << CR << '}';
}

//===----------------------------------------------------------------------===//
// MemRegionManager methods.
//===----------------------------------------------------------------------===//

template <typename REG>
const REG *MemRegionManager::LazyAllocate(REG*& region) {
  if (!region) {
    region = (REG*) A.Allocate<REG>();
    new (region) REG(this);
  }

  return region;
}

template <typename REG, typename ARG>
const REG *MemRegionManager::LazyAllocate(REG*& region, ARG a) {
  if (!region) {
    region = (REG*) A.Allocate<REG>();
    new (region) REG(this, a);
  }

  return region;
}

const StackLocalsSpaceRegion*
MemRegionManager::getStackLocalsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackLocalsSpaceRegion *&R = StackLocalsSpaceRegions[STC];

  if (R)
    return R;

  R = A.Allocate<StackLocalsSpaceRegion>();
  new (R) StackLocalsSpaceRegion(this, STC);
  return R;
}

const StackArgumentsSpaceRegion *
MemRegionManager::getStackArgumentsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackArgumentsSpaceRegion *&R = StackArgumentsSpaceRegions[STC];

  if (R)
    return R;

  R = A.Allocate<StackArgumentsSpaceRegion>();
  new (R) StackArgumentsSpaceRegion(this, STC);
  return R;
}

const GlobalsSpaceRegion
*MemRegionManager::getGlobalsRegion(const CodeTextRegion *CR) {
  if (!CR)
    return LazyAllocate(globals);

  StaticGlobalSpaceRegion *&R = StaticsGlobalSpaceRegions[CR];
  if (R)
    return R;

  R = A.Allocate<StaticGlobalSpaceRegion>();
  new (R) StaticGlobalSpaceRegion(this, CR);
  return R;
}

const HeapSpaceRegion *MemRegionManager::getHeapRegion() {
  return LazyAllocate(heap);
}

const MemSpaceRegion *MemRegionManager::getUnknownRegion() {
  return LazyAllocate(unknown);
}

const MemSpaceRegion *MemRegionManager::getCodeRegion() {
  return LazyAllocate(code);
}

//===----------------------------------------------------------------------===//
// Constructing regions.
//===----------------------------------------------------------------------===//

const StringRegion* MemRegionManager::getStringRegion(const StringLiteral* Str){
  return getSubRegion<StringRegion>(Str, getGlobalsRegion());
}

const VarRegion* MemRegionManager::getVarRegion(const VarDecl *D,
                                                const LocationContext *LC) {
  const MemRegion *sReg = 0;

  if (D->hasGlobalStorage() && !D->isStaticLocal())
    sReg = getGlobalsRegion();
  else {
    // FIXME: Once we implement scope handling, we will need to properly lookup
    // 'D' to the proper LocationContext.
    const DeclContext *DC = D->getDeclContext();
    const StackFrameContext *STC = LC->getStackFrameForDeclContext(DC);

    if (!STC)
      sReg = getUnknownRegion();
    else {
      if (D->hasLocalStorage()) {
        sReg = isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D)
               ? static_cast<const MemRegion*>(getStackArgumentsRegion(STC))
               : static_cast<const MemRegion*>(getStackLocalsRegion(STC));
      }
      else {
        assert(D->isStaticLocal());
        const Decl *D = STC->getDecl();
        if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
          sReg = getGlobalsRegion(getFunctionTextRegion(FD));
        else if (const BlockDecl *BD = dyn_cast<BlockDecl>(D)) {
          const BlockTextRegion *BTR =
            getBlockTextRegion(BD,
                     C.getCanonicalType(BD->getSignatureAsWritten()->getType()),
                     STC->getAnalysisContext());
          sReg = getGlobalsRegion(BTR);
        }
        else {
          // FIXME: For ObjC-methods, we need a new CodeTextRegion.  For now
          // just use the main global memspace.
          sReg = getGlobalsRegion();
        }
      }
    }
  }

  return getSubRegion<VarRegion>(D, sReg);
}

const VarRegion *MemRegionManager::getVarRegion(const VarDecl *D,
                                                const MemRegion *superR) {
  return getSubRegion<VarRegion>(D, superR);
}

const BlockDataRegion *
MemRegionManager::getBlockDataRegion(const BlockTextRegion *BC,
                                     const LocationContext *LC) {
  const MemRegion *sReg = 0;

  if (LC) {
    // FIXME: Once we implement scope handling, we want the parent region
    // to be the scope.
    const StackFrameContext *STC = LC->getCurrentStackFrame();
    assert(STC);
    sReg = getStackLocalsRegion(STC);
  }
  else {
    // We allow 'LC' to be NULL for cases where want BlockDataRegions
    // without context-sensitivity.
    sReg = getUnknownRegion();
  }

  return getSubRegion<BlockDataRegion>(BC, LC, sReg);
}

const CompoundLiteralRegion*
MemRegionManager::getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                                           const LocationContext *LC) {

  const MemRegion *sReg = 0;

  if (CL->isFileScope())
    sReg = getGlobalsRegion();
  else {
    const StackFrameContext *STC = LC->getCurrentStackFrame();
    assert(STC);
    sReg = getStackLocalsRegion(STC);
  }

  return getSubRegion<CompoundLiteralRegion>(CL, sReg);
}

const ElementRegion*
MemRegionManager::getElementRegion(QualType elementType, NonLoc Idx,
                                   const MemRegion* superRegion,
                                   ASTContext &Ctx){

  QualType T = Ctx.getCanonicalType(elementType).getUnqualifiedType();

  llvm::FoldingSetNodeID ID;
  ElementRegion::ProfileRegion(ID, T, Idx, superRegion);

  void *InsertPos;
  MemRegion* data = Regions.FindNodeOrInsertPos(ID, InsertPos);
  ElementRegion* R = cast_or_null<ElementRegion>(data);

  if (!R) {
    R = (ElementRegion*) A.Allocate<ElementRegion>();
    new (R) ElementRegion(T, Idx, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

const FunctionTextRegion *
MemRegionManager::getFunctionTextRegion(const FunctionDecl *FD) {
  return getSubRegion<FunctionTextRegion>(FD, getCodeRegion());
}

const BlockTextRegion *
MemRegionManager::getBlockTextRegion(const BlockDecl *BD, CanQualType locTy,
                                     AnalysisContext *AC) {
  return getSubRegion<BlockTextRegion>(BD, locTy, AC, getCodeRegion());
}


/// getSymbolicRegion - Retrieve or create a "symbolic" memory region.
const SymbolicRegion *MemRegionManager::getSymbolicRegion(SymbolRef sym) {
  return getSubRegion<SymbolicRegion>(sym, getUnknownRegion());
}

const FieldRegion*
MemRegionManager::getFieldRegion(const FieldDecl *d,
                                 const MemRegion* superRegion){
  return getSubRegion<FieldRegion>(d, superRegion);
}

const ObjCIvarRegion*
MemRegionManager::getObjCIvarRegion(const ObjCIvarDecl *d,
                                    const MemRegion* superRegion) {
  return getSubRegion<ObjCIvarRegion>(d, superRegion);
}

const CXXTempObjectRegion*
MemRegionManager::getCXXTempObjectRegion(Expr const *E,
                                         LocationContext const *LC) {
  const StackFrameContext *SFC = LC->getCurrentStackFrame();
  assert(SFC);
  return getSubRegion<CXXTempObjectRegion>(E, getStackLocalsRegion(SFC));
}

const CXXBaseObjectRegion *
MemRegionManager::getCXXBaseObjectRegion(const CXXRecordDecl *decl,
                                         const MemRegion *superRegion) {
  return getSubRegion<CXXBaseObjectRegion>(decl, superRegion);
}

const CXXThisRegion*
MemRegionManager::getCXXThisRegion(QualType thisPointerTy,
                                   const LocationContext *LC) {
  const StackFrameContext *STC = LC->getCurrentStackFrame();
  assert(STC);
  const PointerType *PT = thisPointerTy->getAs<PointerType>();
  assert(PT);
  return getSubRegion<CXXThisRegion>(PT, getStackArgumentsRegion(STC));
}

const AllocaRegion*
MemRegionManager::getAllocaRegion(const Expr *E, unsigned cnt,
                                  const LocationContext *LC) {
  const StackFrameContext *STC = LC->getCurrentStackFrame();
  assert(STC);
  return getSubRegion<AllocaRegion>(E, cnt, getStackLocalsRegion(STC));
}

const MemSpaceRegion *MemRegion::getMemorySpace() const {
  const MemRegion *R = this;
  const SubRegion* SR = dyn_cast<SubRegion>(this);

  while (SR) {
    R = SR->getSuperRegion();
    SR = dyn_cast<SubRegion>(R);
  }

  return dyn_cast<MemSpaceRegion>(R);
}

bool MemRegion::hasStackStorage() const {
  return isa<StackSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackNonParametersStorage() const {
  return isa<StackLocalsSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackParametersStorage() const {
  return isa<StackArgumentsSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasGlobalsOrParametersStorage() const {
  const MemSpaceRegion *MS = getMemorySpace();
  return isa<StackArgumentsSpaceRegion>(MS) ||
         isa<GlobalsSpaceRegion>(MS);
}

// getBaseRegion strips away all elements and fields, and get the base region
// of them.
const MemRegion *MemRegion::getBaseRegion() const {
  const MemRegion *R = this;
  while (true) {
    switch (R->getKind()) {
      case MemRegion::ElementRegionKind:
      case MemRegion::FieldRegionKind:
      case MemRegion::ObjCIvarRegionKind:
      case MemRegion::CXXBaseObjectRegionKind:
        R = cast<SubRegion>(R)->getSuperRegion();
        continue;
      default:
        break;
    }
    break;
  }
  return R;
}

//===----------------------------------------------------------------------===//
// View handling.
//===----------------------------------------------------------------------===//

const MemRegion *MemRegion::StripCasts() const {
  const MemRegion *R = this;
  while (true) {
    if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
      // FIXME: generalize.  Essentially we want to strip away ElementRegions
      // that were layered on a symbolic region because of casts.  We only
      // want to strip away ElementRegions, however, where the index is 0.
      SVal index = ER->getIndex();
      if (nonloc::ConcreteInt *CI = dyn_cast<nonloc::ConcreteInt>(&index)) {
        if (CI->getValue().getSExtValue() == 0) {
          R = ER->getSuperRegion();
          continue;
        }
      }
    }
    break;
  }
  return R;
}

// FIXME: Merge with the implementation of the same method in Store.cpp
static bool IsCompleteType(ASTContext &Ctx, QualType Ty) {
  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *D = RT->getDecl();
    if (!D->getDefinition())
      return false;
  }

  return true;
}

RegionRawOffset ElementRegion::getAsArrayOffset() const {
  CharUnits offset = CharUnits::Zero();
  const ElementRegion *ER = this;
  const MemRegion *superR = NULL;
  ASTContext &C = getContext();

  // FIXME: Handle multi-dimensional arrays.

  while (ER) {
    superR = ER->getSuperRegion();

    // FIXME: generalize to symbolic offsets.
    SVal index = ER->getIndex();
    if (nonloc::ConcreteInt *CI = dyn_cast<nonloc::ConcreteInt>(&index)) {
      // Update the offset.
      int64_t i = CI->getValue().getSExtValue();

      if (i != 0) {
        QualType elemType = ER->getElementType();

        // If we are pointing to an incomplete type, go no further.
        if (!IsCompleteType(C, elemType)) {
          superR = ER;
          break;
        }

        CharUnits size = C.getTypeSizeInChars(elemType);
        offset += (i * size);
      }

      // Go to the next ElementRegion (if any).
      ER = dyn_cast<ElementRegion>(superR);
      continue;
    }

    return NULL;
  }

  assert(superR && "super region cannot be NULL");
  return RegionRawOffset(superR, offset);
}

RegionOffset MemRegion::getAsOffset() const {
  const MemRegion *R = this;
  int64_t Offset = 0;

  while (1) {
    switch (R->getKind()) {
    default:
      return RegionOffset(0);
    case SymbolicRegionKind:
    case AllocaRegionKind:
    case CompoundLiteralRegionKind:
    case CXXThisRegionKind:
    case StringRegionKind:
    case VarRegionKind:
    case CXXTempObjectRegionKind:
      goto Finish;
    case ElementRegionKind: {
      const ElementRegion *ER = cast<ElementRegion>(R);
      QualType EleTy = ER->getValueType();

      if (!IsCompleteType(getContext(), EleTy))
        return RegionOffset(0);

      SVal Index = ER->getIndex();
      if (const nonloc::ConcreteInt *CI=dyn_cast<nonloc::ConcreteInt>(&Index)) {
        int64_t i = CI->getValue().getSExtValue();
        CharUnits Size = getContext().getTypeSizeInChars(EleTy);
        Offset += i * Size.getQuantity() * 8;
      } else {
        // We cannot compute offset for non-concrete index.
        return RegionOffset(0);
      }
      R = ER->getSuperRegion();
      break;
    }
    case FieldRegionKind: {
      const FieldRegion *FR = cast<FieldRegion>(R);
      const RecordDecl *RD = FR->getDecl()->getParent();
      if (!RD->isCompleteDefinition())
        // We cannot compute offset for incomplete type.
        return RegionOffset(0);
      // Get the field number.
      unsigned idx = 0;
      for (RecordDecl::field_iterator FI = RD->field_begin(), 
             FE = RD->field_end(); FI != FE; ++FI, ++idx)
        if (FR->getDecl() == *FI)
          break;

      const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
      // This is offset in bits.
      Offset += Layout.getFieldOffset(idx);
      R = FR->getSuperRegion();
      break;
    }
    }
  }

 Finish:
  return RegionOffset(R, Offset);
}

//===----------------------------------------------------------------------===//
// BlockDataRegion
//===----------------------------------------------------------------------===//

void BlockDataRegion::LazyInitializeReferencedVars() {
  if (ReferencedVars)
    return;

  AnalysisContext *AC = getCodeRegion()->getAnalysisContext();
  AnalysisContext::referenced_decls_iterator I, E;
  llvm::tie(I, E) = AC->getReferencedBlockVars(BC->getDecl());

  if (I == E) {
    ReferencedVars = (void*) 0x1;
    return;
  }

  MemRegionManager &MemMgr = *getMemRegionManager();
  llvm::BumpPtrAllocator &A = MemMgr.getAllocator();
  BumpVectorContext BC(A);

  typedef BumpVector<const MemRegion*> VarVec;
  VarVec *BV = (VarVec*) A.Allocate<VarVec>();
  new (BV) VarVec(BC, E - I);

  for ( ; I != E; ++I) {
    const VarDecl *VD = *I;
    const VarRegion *VR = 0;

    if (!VD->getAttr<BlocksAttr>() && VD->hasLocalStorage())
      VR = MemMgr.getVarRegion(VD, this);
    else {
      if (LC)
        VR = MemMgr.getVarRegion(VD, LC);
      else {
        VR = MemMgr.getVarRegion(VD, MemMgr.getUnknownRegion());
      }
    }

    assert(VR);
    BV->push_back(VR, BC);
  }

  ReferencedVars = BV;
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_begin() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  BumpVector<const MemRegion*> *Vec =
    static_cast<BumpVector<const MemRegion*>*>(ReferencedVars);

  return BlockDataRegion::referenced_vars_iterator(Vec == (void*) 0x1 ?
                                                   NULL : Vec->begin());
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_end() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  BumpVector<const MemRegion*> *Vec =
    static_cast<BumpVector<const MemRegion*>*>(ReferencedVars);

  return BlockDataRegion::referenced_vars_iterator(Vec == (void*) 0x1 ?
                                                   NULL : Vec->end());
}
