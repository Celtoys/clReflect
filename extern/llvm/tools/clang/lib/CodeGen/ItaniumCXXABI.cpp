//===------- ItaniumCXXABI.cpp - Emit LLVM Code from ASTs for a Module ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides C++ code generation targeting the Itanium C++ ABI.  The class
// in this file generates structures that follow the Itanium C++ ABI, which is
// documented at:
//  http://www.codesourcery.com/public/cxx-abi/abi.html
//  http://www.codesourcery.com/public/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM ABI, documented at:
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0041c/IHI0041C_cppabi.pdf
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include <clang/AST/Mangle.h>
#include <clang/AST/Type.h>
#include <llvm/Intrinsics.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Value.h>

using namespace clang;
using namespace CodeGen;

namespace {
class ItaniumCXXABI : public CodeGen::CGCXXABI {
private:
  llvm::IntegerType *PtrDiffTy;
protected:
  bool IsARM;

  // It's a little silly for us to cache this.
  llvm::IntegerType *getPtrDiffTy() {
    if (!PtrDiffTy) {
      QualType T = getContext().getPointerDiffType();
      llvm::Type *Ty = CGM.getTypes().ConvertType(T);
      PtrDiffTy = cast<llvm::IntegerType>(Ty);
    }
    return PtrDiffTy;
  }

  bool NeedsArrayCookie(const CXXNewExpr *expr);
  bool NeedsArrayCookie(const CXXDeleteExpr *expr,
                        QualType elementType);

public:
  ItaniumCXXABI(CodeGen::CodeGenModule &CGM, bool IsARM = false) :
    CGCXXABI(CGM), PtrDiffTy(0), IsARM(IsARM) { }

  bool isZeroInitializable(const MemberPointerType *MPT);

  llvm::Type *ConvertMemberPointerType(const MemberPointerType *MPT);

  llvm::Value *EmitLoadOfMemberFunctionPointer(CodeGenFunction &CGF,
                                               llvm::Value *&This,
                                               llvm::Value *MemFnPtr,
                                               const MemberPointerType *MPT);

  llvm::Value *EmitMemberDataPointerAddress(CodeGenFunction &CGF,
                                            llvm::Value *Base,
                                            llvm::Value *MemPtr,
                                            const MemberPointerType *MPT);

  llvm::Value *EmitMemberPointerConversion(CodeGenFunction &CGF,
                                           const CastExpr *E,
                                           llvm::Value *Src);

  llvm::Constant *EmitMemberPointerConversion(llvm::Constant *C,
                                              const CastExpr *E);

  llvm::Constant *EmitNullMemberPointer(const MemberPointerType *MPT);

  llvm::Constant *EmitMemberPointer(const CXXMethodDecl *MD);
  llvm::Constant *EmitMemberDataPointer(const MemberPointerType *MPT,
                                        CharUnits offset);

  llvm::Value *EmitMemberPointerComparison(CodeGenFunction &CGF,
                                           llvm::Value *L,
                                           llvm::Value *R,
                                           const MemberPointerType *MPT,
                                           bool Inequality);

  llvm::Value *EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                          llvm::Value *Addr,
                                          const MemberPointerType *MPT);

  void BuildConstructorSignature(const CXXConstructorDecl *Ctor,
                                 CXXCtorType T,
                                 CanQualType &ResTy,
                                 SmallVectorImpl<CanQualType> &ArgTys);

  void BuildDestructorSignature(const CXXDestructorDecl *Dtor,
                                CXXDtorType T,
                                CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys);

  void BuildInstanceFunctionParams(CodeGenFunction &CGF,
                                   QualType &ResTy,
                                   FunctionArgList &Params);

  void EmitInstanceFunctionProlog(CodeGenFunction &CGF);

  CharUnits GetArrayCookieSize(const CXXNewExpr *expr);
  llvm::Value *InitializeArrayCookie(CodeGenFunction &CGF,
                                     llvm::Value *NewPtr,
                                     llvm::Value *NumElements,
                                     const CXXNewExpr *expr,
                                     QualType ElementType);
  void ReadArrayCookie(CodeGenFunction &CGF, llvm::Value *Ptr,
                       const CXXDeleteExpr *expr,
                       QualType ElementType, llvm::Value *&NumElements,
                       llvm::Value *&AllocPtr, CharUnits &CookieSize);

  void EmitGuardedInit(CodeGenFunction &CGF, const VarDecl &D,
                       llvm::GlobalVariable *DeclPtr);
};

class ARMCXXABI : public ItaniumCXXABI {
public:
  ARMCXXABI(CodeGen::CodeGenModule &CGM) : ItaniumCXXABI(CGM, /*ARM*/ true) {}

  void BuildConstructorSignature(const CXXConstructorDecl *Ctor,
                                 CXXCtorType T,
                                 CanQualType &ResTy,
                                 SmallVectorImpl<CanQualType> &ArgTys);

  void BuildDestructorSignature(const CXXDestructorDecl *Dtor,
                                CXXDtorType T,
                                CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys);

  void BuildInstanceFunctionParams(CodeGenFunction &CGF,
                                   QualType &ResTy,
                                   FunctionArgList &Params);

  void EmitInstanceFunctionProlog(CodeGenFunction &CGF);

  void EmitReturnFromThunk(CodeGenFunction &CGF, RValue RV, QualType ResTy);

  CharUnits GetArrayCookieSize(const CXXNewExpr *expr);
  llvm::Value *InitializeArrayCookie(CodeGenFunction &CGF,
                                     llvm::Value *NewPtr,
                                     llvm::Value *NumElements,
                                     const CXXNewExpr *expr,
                                     QualType ElementType);
  void ReadArrayCookie(CodeGenFunction &CGF, llvm::Value *Ptr,
                       const CXXDeleteExpr *expr,
                       QualType ElementType, llvm::Value *&NumElements,
                       llvm::Value *&AllocPtr, CharUnits &CookieSize);

private:
  /// \brief Returns true if the given instance method is one of the
  /// kinds that the ARM ABI says returns 'this'.
  static bool HasThisReturn(GlobalDecl GD) {
    const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());
    return ((isa<CXXDestructorDecl>(MD) && GD.getDtorType() != Dtor_Deleting) ||
            (isa<CXXConstructorDecl>(MD)));
  }
};
}

CodeGen::CGCXXABI *CodeGen::CreateItaniumCXXABI(CodeGenModule &CGM) {
  return new ItaniumCXXABI(CGM);
}

CodeGen::CGCXXABI *CodeGen::CreateARMCXXABI(CodeGenModule &CGM) {
  return new ARMCXXABI(CGM);
}

llvm::Type *
ItaniumCXXABI::ConvertMemberPointerType(const MemberPointerType *MPT) {
  if (MPT->isMemberDataPointer())
    return getPtrDiffTy();
  return llvm::StructType::get(getPtrDiffTy(), getPtrDiffTy(), NULL);
}

/// In the Itanium and ARM ABIs, method pointers have the form:
///   struct { ptrdiff_t ptr; ptrdiff_t adj; } memptr;
///
/// In the Itanium ABI:
///  - method pointers are virtual if (memptr.ptr & 1) is nonzero
///  - the this-adjustment is (memptr.adj)
///  - the virtual offset is (memptr.ptr - 1)
///
/// In the ARM ABI:
///  - method pointers are virtual if (memptr.adj & 1) is nonzero
///  - the this-adjustment is (memptr.adj >> 1)
///  - the virtual offset is (memptr.ptr)
/// ARM uses 'adj' for the virtual flag because Thumb functions
/// may be only single-byte aligned.
///
/// If the member is virtual, the adjusted 'this' pointer points
/// to a vtable pointer from which the virtual offset is applied.
///
/// If the member is non-virtual, memptr.ptr is the address of
/// the function to call.
llvm::Value *
ItaniumCXXABI::EmitLoadOfMemberFunctionPointer(CodeGenFunction &CGF,
                                               llvm::Value *&This,
                                               llvm::Value *MemFnPtr,
                                               const MemberPointerType *MPT) {
  CGBuilderTy &Builder = CGF.Builder;

  const FunctionProtoType *FPT = 
    MPT->getPointeeType()->getAs<FunctionProtoType>();
  const CXXRecordDecl *RD = 
    cast<CXXRecordDecl>(MPT->getClass()->getAs<RecordType>()->getDecl());

  llvm::FunctionType *FTy = 
    CGM.getTypes().GetFunctionType(CGM.getTypes().getFunctionInfo(RD, FPT),
                                   FPT->isVariadic());

  llvm::IntegerType *ptrdiff = getPtrDiffTy();
  llvm::Constant *ptrdiff_1 = llvm::ConstantInt::get(ptrdiff, 1);

  llvm::BasicBlock *FnVirtual = CGF.createBasicBlock("memptr.virtual");
  llvm::BasicBlock *FnNonVirtual = CGF.createBasicBlock("memptr.nonvirtual");
  llvm::BasicBlock *FnEnd = CGF.createBasicBlock("memptr.end");

  // Extract memptr.adj, which is in the second field.
  llvm::Value *RawAdj = Builder.CreateExtractValue(MemFnPtr, 1, "memptr.adj");

  // Compute the true adjustment.
  llvm::Value *Adj = RawAdj;
  if (IsARM)
    Adj = Builder.CreateAShr(Adj, ptrdiff_1, "memptr.adj.shifted");

  // Apply the adjustment and cast back to the original struct type
  // for consistency.
  llvm::Value *Ptr = Builder.CreateBitCast(This, Builder.getInt8PtrTy());
  Ptr = Builder.CreateInBoundsGEP(Ptr, Adj);
  This = Builder.CreateBitCast(Ptr, This->getType(), "this.adjusted");
  
  // Load the function pointer.
  llvm::Value *FnAsInt = Builder.CreateExtractValue(MemFnPtr, 0, "memptr.ptr");
  
  // If the LSB in the function pointer is 1, the function pointer points to
  // a virtual function.
  llvm::Value *IsVirtual;
  if (IsARM)
    IsVirtual = Builder.CreateAnd(RawAdj, ptrdiff_1);
  else
    IsVirtual = Builder.CreateAnd(FnAsInt, ptrdiff_1);
  IsVirtual = Builder.CreateIsNotNull(IsVirtual, "memptr.isvirtual");
  Builder.CreateCondBr(IsVirtual, FnVirtual, FnNonVirtual);

  // In the virtual path, the adjustment left 'This' pointing to the
  // vtable of the correct base subobject.  The "function pointer" is an
  // offset within the vtable (+1 for the virtual flag on non-ARM).
  CGF.EmitBlock(FnVirtual);

  // Cast the adjusted this to a pointer to vtable pointer and load.
  llvm::Type *VTableTy = Builder.getInt8PtrTy();
  llvm::Value *VTable = Builder.CreateBitCast(This, VTableTy->getPointerTo());
  VTable = Builder.CreateLoad(VTable, "memptr.vtable");

  // Apply the offset.
  llvm::Value *VTableOffset = FnAsInt;
  if (!IsARM) VTableOffset = Builder.CreateSub(VTableOffset, ptrdiff_1);
  VTable = Builder.CreateGEP(VTable, VTableOffset);

  // Load the virtual function to call.
  VTable = Builder.CreateBitCast(VTable, FTy->getPointerTo()->getPointerTo());
  llvm::Value *VirtualFn = Builder.CreateLoad(VTable, "memptr.virtualfn");
  CGF.EmitBranch(FnEnd);

  // In the non-virtual path, the function pointer is actually a
  // function pointer.
  CGF.EmitBlock(FnNonVirtual);
  llvm::Value *NonVirtualFn =
    Builder.CreateIntToPtr(FnAsInt, FTy->getPointerTo(), "memptr.nonvirtualfn");
  
  // We're done.
  CGF.EmitBlock(FnEnd);
  llvm::PHINode *Callee = Builder.CreatePHI(FTy->getPointerTo(), 2);
  Callee->addIncoming(VirtualFn, FnVirtual);
  Callee->addIncoming(NonVirtualFn, FnNonVirtual);
  return Callee;
}

/// Compute an l-value by applying the given pointer-to-member to a
/// base object.
llvm::Value *ItaniumCXXABI::EmitMemberDataPointerAddress(CodeGenFunction &CGF,
                                                         llvm::Value *Base,
                                                         llvm::Value *MemPtr,
                                           const MemberPointerType *MPT) {
  assert(MemPtr->getType() == getPtrDiffTy());

  CGBuilderTy &Builder = CGF.Builder;

  unsigned AS = cast<llvm::PointerType>(Base->getType())->getAddressSpace();

  // Cast to char*.
  Base = Builder.CreateBitCast(Base, Builder.getInt8Ty()->getPointerTo(AS));

  // Apply the offset, which we assume is non-null.
  llvm::Value *Addr = Builder.CreateInBoundsGEP(Base, MemPtr, "memptr.offset");

  // Cast the address to the appropriate pointer type, adopting the
  // address space of the base pointer.
  llvm::Type *PType
    = CGF.ConvertTypeForMem(MPT->getPointeeType())->getPointerTo(AS);
  return Builder.CreateBitCast(Addr, PType);
}

/// Perform a derived-to-base or base-to-derived member pointer conversion.
///
/// Obligatory offset/adjustment diagram:
///         <-- offset -->          <-- adjustment -->
///   |--------------------------|----------------------|--------------------|
///   ^Derived address point     ^Base address point    ^Member address point
///
/// So when converting a base member pointer to a derived member pointer,
/// we add the offset to the adjustment because the address point has
/// decreased;  and conversely, when converting a derived MP to a base MP
/// we subtract the offset from the adjustment because the address point
/// has increased.
///
/// The standard forbids (at compile time) conversion to and from
/// virtual bases, which is why we don't have to consider them here.
///
/// The standard forbids (at run time) casting a derived MP to a base
/// MP when the derived MP does not point to a member of the base.
/// This is why -1 is a reasonable choice for null data member
/// pointers.
llvm::Value *
ItaniumCXXABI::EmitMemberPointerConversion(CodeGenFunction &CGF,
                                           const CastExpr *E,
                                           llvm::Value *Src) {
  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer);

  if (isa<llvm::Constant>(Src))
    return EmitMemberPointerConversion(cast<llvm::Constant>(Src), E);

  CGBuilderTy &Builder = CGF.Builder;

  const MemberPointerType *SrcTy =
    E->getSubExpr()->getType()->getAs<MemberPointerType>();
  const MemberPointerType *DestTy = E->getType()->getAs<MemberPointerType>();

  const CXXRecordDecl *SrcDecl = SrcTy->getClass()->getAsCXXRecordDecl();
  const CXXRecordDecl *DestDecl = DestTy->getClass()->getAsCXXRecordDecl();

  bool DerivedToBase =
    E->getCastKind() == CK_DerivedToBaseMemberPointer;

  const CXXRecordDecl *DerivedDecl;
  if (DerivedToBase)
    DerivedDecl = SrcDecl;
  else
    DerivedDecl = DestDecl;

  llvm::Constant *Adj = 
    CGF.CGM.GetNonVirtualBaseClassOffset(DerivedDecl,
                                         E->path_begin(),
                                         E->path_end());
  if (!Adj) return Src;

  // For member data pointers, this is just a matter of adding the
  // offset if the source is non-null.
  if (SrcTy->isMemberDataPointer()) {
    llvm::Value *Dst;
    if (DerivedToBase)
      Dst = Builder.CreateNSWSub(Src, Adj, "adj");
    else
      Dst = Builder.CreateNSWAdd(Src, Adj, "adj");

    // Null check.
    llvm::Value *Null = llvm::Constant::getAllOnesValue(Src->getType());
    llvm::Value *IsNull = Builder.CreateICmpEQ(Src, Null, "memptr.isnull");
    return Builder.CreateSelect(IsNull, Src, Dst);
  }

  // The this-adjustment is left-shifted by 1 on ARM.
  if (IsARM) {
    uint64_t Offset = cast<llvm::ConstantInt>(Adj)->getZExtValue();
    Offset <<= 1;
    Adj = llvm::ConstantInt::get(Adj->getType(), Offset);
  }

  llvm::Value *SrcAdj = Builder.CreateExtractValue(Src, 1, "src.adj");
  llvm::Value *DstAdj;
  if (DerivedToBase)
    DstAdj = Builder.CreateNSWSub(SrcAdj, Adj, "adj");
  else
    DstAdj = Builder.CreateNSWAdd(SrcAdj, Adj, "adj");

  return Builder.CreateInsertValue(Src, DstAdj, 1);
}

llvm::Constant *
ItaniumCXXABI::EmitMemberPointerConversion(llvm::Constant *C,
                                           const CastExpr *E) {
  const MemberPointerType *SrcTy = 
    E->getSubExpr()->getType()->getAs<MemberPointerType>();
  const MemberPointerType *DestTy = 
    E->getType()->getAs<MemberPointerType>();

  bool DerivedToBase =
    E->getCastKind() == CK_DerivedToBaseMemberPointer;

  const CXXRecordDecl *DerivedDecl;
  if (DerivedToBase)
    DerivedDecl = SrcTy->getClass()->getAsCXXRecordDecl();
  else
    DerivedDecl = DestTy->getClass()->getAsCXXRecordDecl();

  // Calculate the offset to the base class.
  llvm::Constant *Offset = 
    CGM.GetNonVirtualBaseClassOffset(DerivedDecl,
                                     E->path_begin(),
                                     E->path_end());
  // If there's no offset, we're done.
  if (!Offset) return C;

  // If the source is a member data pointer, we have to do a null
  // check and then add the offset.  In the common case, we can fold
  // away the offset.
  if (SrcTy->isMemberDataPointer()) {
    assert(C->getType() == getPtrDiffTy());

    // If it's a constant int, just create a new constant int.
    if (llvm::ConstantInt *CI = dyn_cast<llvm::ConstantInt>(C)) {
      int64_t Src = CI->getSExtValue();

      // Null converts to null.
      if (Src == -1) return CI;

      // Otherwise, just add the offset.
      int64_t OffsetV = cast<llvm::ConstantInt>(Offset)->getSExtValue();
      int64_t Dst = (DerivedToBase ? Src - OffsetV : Src + OffsetV);
      return llvm::ConstantInt::get(CI->getType(), Dst, /*signed*/ true);
    }

    // Otherwise, we have to form a constant select expression.
    llvm::Constant *Null = llvm::Constant::getAllOnesValue(C->getType());

    llvm::Constant *IsNull =
      llvm::ConstantExpr::getICmp(llvm::ICmpInst::ICMP_EQ, C, Null);

    llvm::Constant *Dst;
    if (DerivedToBase)
      Dst = llvm::ConstantExpr::getNSWSub(C, Offset);
    else
      Dst = llvm::ConstantExpr::getNSWAdd(C, Offset);

    return llvm::ConstantExpr::getSelect(IsNull, Null, Dst);
  }

  // The this-adjustment is left-shifted by 1 on ARM.
  if (IsARM) {
    int64_t OffsetV = cast<llvm::ConstantInt>(Offset)->getSExtValue();
    OffsetV <<= 1;
    Offset = llvm::ConstantInt::get(Offset->getType(), OffsetV);
  }

  llvm::ConstantStruct *CS = cast<llvm::ConstantStruct>(C);

  llvm::Constant *Values[2] = { CS->getOperand(0), 0 };
  if (DerivedToBase)
    Values[1] = llvm::ConstantExpr::getSub(CS->getOperand(1), Offset);
  else
    Values[1] = llvm::ConstantExpr::getAdd(CS->getOperand(1), Offset);

  return llvm::ConstantStruct::get(CS->getType(), Values);
}        


llvm::Constant *
ItaniumCXXABI::EmitNullMemberPointer(const MemberPointerType *MPT) {
  llvm::Type *ptrdiff_t = getPtrDiffTy();

  // Itanium C++ ABI 2.3:
  //   A NULL pointer is represented as -1.
  if (MPT->isMemberDataPointer()) 
    return llvm::ConstantInt::get(ptrdiff_t, -1ULL, /*isSigned=*/true);

  llvm::Constant *Zero = llvm::ConstantInt::get(ptrdiff_t, 0);
  llvm::Constant *Values[2] = { Zero, Zero };
  return llvm::ConstantStruct::getAnon(Values);
}

llvm::Constant *
ItaniumCXXABI::EmitMemberDataPointer(const MemberPointerType *MPT,
                                     CharUnits offset) {
  // Itanium C++ ABI 2.3:
  //   A pointer to data member is an offset from the base address of
  //   the class object containing it, represented as a ptrdiff_t
  return llvm::ConstantInt::get(getPtrDiffTy(), offset.getQuantity());
}

llvm::Constant *ItaniumCXXABI::EmitMemberPointer(const CXXMethodDecl *MD) {
  assert(MD->isInstance() && "Member function must not be static!");
  MD = MD->getCanonicalDecl();

  CodeGenTypes &Types = CGM.getTypes();
  llvm::Type *ptrdiff_t = getPtrDiffTy();

  // Get the function pointer (or index if this is a virtual function).
  llvm::Constant *MemPtr[2];
  if (MD->isVirtual()) {
    uint64_t Index = CGM.getVTableContext().getMethodVTableIndex(MD);

    const ASTContext &Context = getContext();
    CharUnits PointerWidth =
      Context.toCharUnitsFromBits(Context.getTargetInfo().getPointerWidth(0));
    uint64_t VTableOffset = (Index * PointerWidth.getQuantity());

    if (IsARM) {
      // ARM C++ ABI 3.2.1:
      //   This ABI specifies that adj contains twice the this
      //   adjustment, plus 1 if the member function is virtual. The
      //   least significant bit of adj then makes exactly the same
      //   discrimination as the least significant bit of ptr does for
      //   Itanium.
      MemPtr[0] = llvm::ConstantInt::get(ptrdiff_t, VTableOffset);
      MemPtr[1] = llvm::ConstantInt::get(ptrdiff_t, 1);
    } else {
      // Itanium C++ ABI 2.3:
      //   For a virtual function, [the pointer field] is 1 plus the
      //   virtual table offset (in bytes) of the function,
      //   represented as a ptrdiff_t.
      MemPtr[0] = llvm::ConstantInt::get(ptrdiff_t, VTableOffset + 1);
      MemPtr[1] = llvm::ConstantInt::get(ptrdiff_t, 0);
    }
  } else {
    const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
    llvm::Type *Ty;
    // Check whether the function has a computable LLVM signature.
    if (Types.isFuncTypeConvertible(FPT)) {
      // The function has a computable LLVM signature; use the correct type.
      Ty = Types.GetFunctionType(Types.getFunctionInfo(MD),
                                 FPT->isVariadic());
    } else {
      // Use an arbitrary non-function type to tell GetAddrOfFunction that the
      // function type is incomplete.
      Ty = ptrdiff_t;
    }
    llvm::Constant *addr = CGM.GetAddrOfFunction(MD, Ty);

    MemPtr[0] = llvm::ConstantExpr::getPtrToInt(addr, ptrdiff_t);
    MemPtr[1] = llvm::ConstantInt::get(ptrdiff_t, 0);
  }
  
  return llvm::ConstantStruct::getAnon(MemPtr);
}

/// The comparison algorithm is pretty easy: the member pointers are
/// the same if they're either bitwise identical *or* both null.
///
/// ARM is different here only because null-ness is more complicated.
llvm::Value *
ItaniumCXXABI::EmitMemberPointerComparison(CodeGenFunction &CGF,
                                           llvm::Value *L,
                                           llvm::Value *R,
                                           const MemberPointerType *MPT,
                                           bool Inequality) {
  CGBuilderTy &Builder = CGF.Builder;

  llvm::ICmpInst::Predicate Eq;
  llvm::Instruction::BinaryOps And, Or;
  if (Inequality) {
    Eq = llvm::ICmpInst::ICMP_NE;
    And = llvm::Instruction::Or;
    Or = llvm::Instruction::And;
  } else {
    Eq = llvm::ICmpInst::ICMP_EQ;
    And = llvm::Instruction::And;
    Or = llvm::Instruction::Or;
  }

  // Member data pointers are easy because there's a unique null
  // value, so it just comes down to bitwise equality.
  if (MPT->isMemberDataPointer())
    return Builder.CreateICmp(Eq, L, R);

  // For member function pointers, the tautologies are more complex.
  // The Itanium tautology is:
  //   (L == R) <==> (L.ptr == R.ptr && (L.ptr == 0 || L.adj == R.adj))
  // The ARM tautology is:
  //   (L == R) <==> (L.ptr == R.ptr &&
  //                  (L.adj == R.adj ||
  //                   (L.ptr == 0 && ((L.adj|R.adj) & 1) == 0)))
  // The inequality tautologies have exactly the same structure, except
  // applying De Morgan's laws.
  
  llvm::Value *LPtr = Builder.CreateExtractValue(L, 0, "lhs.memptr.ptr");
  llvm::Value *RPtr = Builder.CreateExtractValue(R, 0, "rhs.memptr.ptr");

  // This condition tests whether L.ptr == R.ptr.  This must always be
  // true for equality to hold.
  llvm::Value *PtrEq = Builder.CreateICmp(Eq, LPtr, RPtr, "cmp.ptr");

  // This condition, together with the assumption that L.ptr == R.ptr,
  // tests whether the pointers are both null.  ARM imposes an extra
  // condition.
  llvm::Value *Zero = llvm::Constant::getNullValue(LPtr->getType());
  llvm::Value *EqZero = Builder.CreateICmp(Eq, LPtr, Zero, "cmp.ptr.null");

  // This condition tests whether L.adj == R.adj.  If this isn't
  // true, the pointers are unequal unless they're both null.
  llvm::Value *LAdj = Builder.CreateExtractValue(L, 1, "lhs.memptr.adj");
  llvm::Value *RAdj = Builder.CreateExtractValue(R, 1, "rhs.memptr.adj");
  llvm::Value *AdjEq = Builder.CreateICmp(Eq, LAdj, RAdj, "cmp.adj");

  // Null member function pointers on ARM clear the low bit of Adj,
  // so the zero condition has to check that neither low bit is set.
  if (IsARM) {
    llvm::Value *One = llvm::ConstantInt::get(LPtr->getType(), 1);

    // Compute (l.adj | r.adj) & 1 and test it against zero.
    llvm::Value *OrAdj = Builder.CreateOr(LAdj, RAdj, "or.adj");
    llvm::Value *OrAdjAnd1 = Builder.CreateAnd(OrAdj, One);
    llvm::Value *OrAdjAnd1EqZero = Builder.CreateICmp(Eq, OrAdjAnd1, Zero,
                                                      "cmp.or.adj");
    EqZero = Builder.CreateBinOp(And, EqZero, OrAdjAnd1EqZero);
  }

  // Tie together all our conditions.
  llvm::Value *Result = Builder.CreateBinOp(Or, EqZero, AdjEq);
  Result = Builder.CreateBinOp(And, PtrEq, Result,
                               Inequality ? "memptr.ne" : "memptr.eq");
  return Result;
}

llvm::Value *
ItaniumCXXABI::EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                          llvm::Value *MemPtr,
                                          const MemberPointerType *MPT) {
  CGBuilderTy &Builder = CGF.Builder;

  /// For member data pointers, this is just a check against -1.
  if (MPT->isMemberDataPointer()) {
    assert(MemPtr->getType() == getPtrDiffTy());
    llvm::Value *NegativeOne =
      llvm::Constant::getAllOnesValue(MemPtr->getType());
    return Builder.CreateICmpNE(MemPtr, NegativeOne, "memptr.tobool");
  }
  
  // In Itanium, a member function pointer is not null if 'ptr' is not null.
  llvm::Value *Ptr = Builder.CreateExtractValue(MemPtr, 0, "memptr.ptr");

  llvm::Constant *Zero = llvm::ConstantInt::get(Ptr->getType(), 0);
  llvm::Value *Result = Builder.CreateICmpNE(Ptr, Zero, "memptr.tobool");

  // On ARM, a member function pointer is also non-null if the low bit of 'adj'
  // (the virtual bit) is set.
  if (IsARM) {
    llvm::Constant *One = llvm::ConstantInt::get(Ptr->getType(), 1);
    llvm::Value *Adj = Builder.CreateExtractValue(MemPtr, 1, "memptr.adj");
    llvm::Value *VirtualBit = Builder.CreateAnd(Adj, One, "memptr.virtualbit");
    llvm::Value *IsVirtual = Builder.CreateICmpNE(VirtualBit, Zero,
                                                  "memptr.isvirtual");
    Result = Builder.CreateOr(Result, IsVirtual);
  }

  return Result;
}

/// The Itanium ABI requires non-zero initialization only for data
/// member pointers, for which '0' is a valid offset.
bool ItaniumCXXABI::isZeroInitializable(const MemberPointerType *MPT) {
  return MPT->getPointeeType()->isFunctionType();
}

/// The generic ABI passes 'this', plus a VTT if it's initializing a
/// base subobject.
void ItaniumCXXABI::BuildConstructorSignature(const CXXConstructorDecl *Ctor,
                                              CXXCtorType Type,
                                              CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys) {
  ASTContext &Context = getContext();

  // 'this' is already there.

  // Check if we need to add a VTT parameter (which has type void **).
  if (Type == Ctor_Base && Ctor->getParent()->getNumVBases() != 0)
    ArgTys.push_back(Context.getPointerType(Context.VoidPtrTy));
}

/// The ARM ABI does the same as the Itanium ABI, but returns 'this'.
void ARMCXXABI::BuildConstructorSignature(const CXXConstructorDecl *Ctor,
                                          CXXCtorType Type,
                                          CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys) {
  ItaniumCXXABI::BuildConstructorSignature(Ctor, Type, ResTy, ArgTys);
  ResTy = ArgTys[0];
}

/// The generic ABI passes 'this', plus a VTT if it's destroying a
/// base subobject.
void ItaniumCXXABI::BuildDestructorSignature(const CXXDestructorDecl *Dtor,
                                             CXXDtorType Type,
                                             CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys) {
  ASTContext &Context = getContext();

  // 'this' is already there.

  // Check if we need to add a VTT parameter (which has type void **).
  if (Type == Dtor_Base && Dtor->getParent()->getNumVBases() != 0)
    ArgTys.push_back(Context.getPointerType(Context.VoidPtrTy));
}

/// The ARM ABI does the same as the Itanium ABI, but returns 'this'
/// for non-deleting destructors.
void ARMCXXABI::BuildDestructorSignature(const CXXDestructorDecl *Dtor,
                                         CXXDtorType Type,
                                         CanQualType &ResTy,
                                SmallVectorImpl<CanQualType> &ArgTys) {
  ItaniumCXXABI::BuildDestructorSignature(Dtor, Type, ResTy, ArgTys);

  if (Type != Dtor_Deleting)
    ResTy = ArgTys[0];
}

void ItaniumCXXABI::BuildInstanceFunctionParams(CodeGenFunction &CGF,
                                                QualType &ResTy,
                                                FunctionArgList &Params) {
  /// Create the 'this' variable.
  BuildThisParam(CGF, Params);

  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());
  assert(MD->isInstance());

  // Check if we need a VTT parameter as well.
  if (CodeGenVTables::needsVTTParameter(CGF.CurGD)) {
    ASTContext &Context = getContext();

    // FIXME: avoid the fake decl
    QualType T = Context.getPointerType(Context.VoidPtrTy);
    ImplicitParamDecl *VTTDecl
      = ImplicitParamDecl::Create(Context, 0, MD->getLocation(),
                                  &Context.Idents.get("vtt"), T);
    Params.push_back(VTTDecl);
    getVTTDecl(CGF) = VTTDecl;
  }
}

void ARMCXXABI::BuildInstanceFunctionParams(CodeGenFunction &CGF,
                                            QualType &ResTy,
                                            FunctionArgList &Params) {
  ItaniumCXXABI::BuildInstanceFunctionParams(CGF, ResTy, Params);

  // Return 'this' from certain constructors and destructors.
  if (HasThisReturn(CGF.CurGD))
    ResTy = Params[0]->getType();
}

void ItaniumCXXABI::EmitInstanceFunctionProlog(CodeGenFunction &CGF) {
  /// Initialize the 'this' slot.
  EmitThisParam(CGF);

  /// Initialize the 'vtt' slot if needed.
  if (getVTTDecl(CGF)) {
    getVTTValue(CGF)
      = CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(getVTTDecl(CGF)),
                               "vtt");
  }
}

void ARMCXXABI::EmitInstanceFunctionProlog(CodeGenFunction &CGF) {
  ItaniumCXXABI::EmitInstanceFunctionProlog(CGF);

  /// Initialize the return slot to 'this' at the start of the
  /// function.
  if (HasThisReturn(CGF.CurGD))
    CGF.Builder.CreateStore(CGF.LoadCXXThis(), CGF.ReturnValue);
}

void ARMCXXABI::EmitReturnFromThunk(CodeGenFunction &CGF,
                                    RValue RV, QualType ResultType) {
  if (!isa<CXXDestructorDecl>(CGF.CurGD.getDecl()))
    return ItaniumCXXABI::EmitReturnFromThunk(CGF, RV, ResultType);

  // Destructor thunks in the ARM ABI have indeterminate results.
  llvm::Type *T =
    cast<llvm::PointerType>(CGF.ReturnValue->getType())->getElementType();
  RValue Undef = RValue::get(llvm::UndefValue::get(T));
  return ItaniumCXXABI::EmitReturnFromThunk(CGF, Undef, ResultType);
}

/************************** Array allocation cookies **************************/

bool ItaniumCXXABI::NeedsArrayCookie(const CXXNewExpr *expr) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  // Automatic Reference Counting:
  //   We need an array cookie for pointers with strong or weak lifetime.
  QualType AllocatedType = expr->getAllocatedType();
  if (getContext().getLangOptions().ObjCAutoRefCount &&
      AllocatedType->isObjCLifetimeType()) {
    switch (AllocatedType.getObjCLifetime()) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      return false;
      
    case Qualifiers::OCL_Strong:
    case Qualifiers::OCL_Weak:
      return true;
    }
  }

  // Otherwise, if the class has a non-trivial destructor, it always
  // needs a cookie.
  const CXXRecordDecl *record =
    AllocatedType->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
  return (record && !record->hasTrivialDestructor());
}

bool ItaniumCXXABI::NeedsArrayCookie(const CXXDeleteExpr *expr,
                                     QualType elementType) {
  // If the class's usual deallocation function takes two arguments,
  // it needs a cookie.
  if (expr->doesUsualArrayDeleteWantSize())
    return true;

  return elementType.isDestructedType();
}

CharUnits ItaniumCXXABI::GetArrayCookieSize(const CXXNewExpr *expr) {
  if (!NeedsArrayCookie(expr))
    return CharUnits::Zero();
  
  // Padding is the maximum of sizeof(size_t) and alignof(elementType)
  ASTContext &Ctx = getContext();
  return std::max(Ctx.getTypeSizeInChars(Ctx.getSizeType()),
                  Ctx.getTypeAlignInChars(expr->getAllocatedType()));
}

llvm::Value *ItaniumCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                                  llvm::Value *NewPtr,
                                                  llvm::Value *NumElements,
                                                  const CXXNewExpr *expr,
                                                  QualType ElementType) {
  assert(NeedsArrayCookie(expr));

  unsigned AS = cast<llvm::PointerType>(NewPtr->getType())->getAddressSpace();

  ASTContext &Ctx = getContext();
  QualType SizeTy = Ctx.getSizeType();
  CharUnits SizeSize = Ctx.getTypeSizeInChars(SizeTy);

  // The size of the cookie.
  CharUnits CookieSize =
    std::max(SizeSize, Ctx.getTypeAlignInChars(ElementType));

  // Compute an offset to the cookie.
  llvm::Value *CookiePtr = NewPtr;
  CharUnits CookieOffset = CookieSize - SizeSize;
  if (!CookieOffset.isZero())
    CookiePtr = CGF.Builder.CreateConstInBoundsGEP1_64(CookiePtr,
                                                 CookieOffset.getQuantity());

  // Write the number of elements into the appropriate slot.
  llvm::Value *NumElementsPtr
    = CGF.Builder.CreateBitCast(CookiePtr,
                                CGF.ConvertType(SizeTy)->getPointerTo(AS));
  CGF.Builder.CreateStore(NumElements, NumElementsPtr);

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  return CGF.Builder.CreateConstInBoundsGEP1_64(NewPtr,
                                                CookieSize.getQuantity());  
}

void ItaniumCXXABI::ReadArrayCookie(CodeGenFunction &CGF,
                                    llvm::Value *Ptr,
                                    const CXXDeleteExpr *expr,
                                    QualType ElementType,
                                    llvm::Value *&NumElements,
                                    llvm::Value *&AllocPtr,
                                    CharUnits &CookieSize) {
  // Derive a char* in the same address space as the pointer.
  unsigned AS = cast<llvm::PointerType>(Ptr->getType())->getAddressSpace();
  llvm::Type *CharPtrTy = CGF.Builder.getInt8Ty()->getPointerTo(AS);

  // If we don't need an array cookie, bail out early.
  if (!NeedsArrayCookie(expr, ElementType)) {
    AllocPtr = CGF.Builder.CreateBitCast(Ptr, CharPtrTy);
    NumElements = 0;
    CookieSize = CharUnits::Zero();
    return;
  }

  QualType SizeTy = getContext().getSizeType();
  CharUnits SizeSize = getContext().getTypeSizeInChars(SizeTy);
  llvm::Type *SizeLTy = CGF.ConvertType(SizeTy);
  
  CookieSize
    = std::max(SizeSize, getContext().getTypeAlignInChars(ElementType));

  CharUnits NumElementsOffset = CookieSize - SizeSize;

  // Compute the allocated pointer.
  AllocPtr = CGF.Builder.CreateBitCast(Ptr, CharPtrTy);
  AllocPtr = CGF.Builder.CreateConstInBoundsGEP1_64(AllocPtr,
                                                    -CookieSize.getQuantity());

  llvm::Value *NumElementsPtr = AllocPtr;
  if (!NumElementsOffset.isZero())
    NumElementsPtr =
      CGF.Builder.CreateConstInBoundsGEP1_64(NumElementsPtr,
                                             NumElementsOffset.getQuantity());
  NumElementsPtr = 
    CGF.Builder.CreateBitCast(NumElementsPtr, SizeLTy->getPointerTo(AS));
  NumElements = CGF.Builder.CreateLoad(NumElementsPtr);
}

CharUnits ARMCXXABI::GetArrayCookieSize(const CXXNewExpr *expr) {
  if (!NeedsArrayCookie(expr))
    return CharUnits::Zero();

  // On ARM, the cookie is always:
  //   struct array_cookie {
  //     std::size_t element_size; // element_size != 0
  //     std::size_t element_count;
  //   };
  // TODO: what should we do if the allocated type actually wants
  // greater alignment?
  return getContext().getTypeSizeInChars(getContext().getSizeType()) * 2;
}

llvm::Value *ARMCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                              llvm::Value *NewPtr,
                                              llvm::Value *NumElements,
                                              const CXXNewExpr *expr,
                                              QualType ElementType) {
  assert(NeedsArrayCookie(expr));

  // NewPtr is a char*.

  unsigned AS = cast<llvm::PointerType>(NewPtr->getType())->getAddressSpace();

  ASTContext &Ctx = getContext();
  CharUnits SizeSize = Ctx.getTypeSizeInChars(Ctx.getSizeType());
  llvm::IntegerType *SizeTy =
    cast<llvm::IntegerType>(CGF.ConvertType(Ctx.getSizeType()));

  // The cookie is always at the start of the buffer.
  llvm::Value *CookiePtr = NewPtr;

  // The first element is the element size.
  CookiePtr = CGF.Builder.CreateBitCast(CookiePtr, SizeTy->getPointerTo(AS));
  llvm::Value *ElementSize = llvm::ConstantInt::get(SizeTy,
                          Ctx.getTypeSizeInChars(ElementType).getQuantity());
  CGF.Builder.CreateStore(ElementSize, CookiePtr);

  // The second element is the element count.
  CookiePtr = CGF.Builder.CreateConstInBoundsGEP1_32(CookiePtr, 1);
  CGF.Builder.CreateStore(NumElements, CookiePtr);

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  CharUnits CookieSize = 2 * SizeSize;
  return CGF.Builder.CreateConstInBoundsGEP1_64(NewPtr,
                                                CookieSize.getQuantity());
}

void ARMCXXABI::ReadArrayCookie(CodeGenFunction &CGF,
                                llvm::Value *Ptr,
                                const CXXDeleteExpr *expr,
                                QualType ElementType,
                                llvm::Value *&NumElements,
                                llvm::Value *&AllocPtr,
                                CharUnits &CookieSize) {
  // Derive a char* in the same address space as the pointer.
  unsigned AS = cast<llvm::PointerType>(Ptr->getType())->getAddressSpace();
  llvm::Type *CharPtrTy = CGF.Builder.getInt8Ty()->getPointerTo(AS);

  // If we don't need an array cookie, bail out early.
  if (!NeedsArrayCookie(expr, ElementType)) {
    AllocPtr = CGF.Builder.CreateBitCast(Ptr, CharPtrTy);
    NumElements = 0;
    CookieSize = CharUnits::Zero();
    return;
  }

  QualType SizeTy = getContext().getSizeType();
  CharUnits SizeSize = getContext().getTypeSizeInChars(SizeTy);
  llvm::Type *SizeLTy = CGF.ConvertType(SizeTy);
  
  // The cookie size is always 2 * sizeof(size_t).
  CookieSize = 2 * SizeSize;

  // The allocated pointer is the input ptr, minus that amount.
  AllocPtr = CGF.Builder.CreateBitCast(Ptr, CharPtrTy);
  AllocPtr = CGF.Builder.CreateConstInBoundsGEP1_64(AllocPtr,
                                               -CookieSize.getQuantity());

  // The number of elements is at offset sizeof(size_t) relative to that.
  llvm::Value *NumElementsPtr
    = CGF.Builder.CreateConstInBoundsGEP1_64(AllocPtr,
                                             SizeSize.getQuantity());
  NumElementsPtr = 
    CGF.Builder.CreateBitCast(NumElementsPtr, SizeLTy->getPointerTo(AS));
  NumElements = CGF.Builder.CreateLoad(NumElementsPtr);
}

/*********************** Static local initialization **************************/

static llvm::Constant *getGuardAcquireFn(CodeGenModule &CGM,
                                         llvm::PointerType *GuardPtrTy) {
  // int __cxa_guard_acquire(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.getTypes().ConvertType(CGM.getContext().IntTy),
                            GuardPtrTy, /*isVarArg=*/false);
  
  return CGM.CreateRuntimeFunction(FTy, "__cxa_guard_acquire");
}

static llvm::Constant *getGuardReleaseFn(CodeGenModule &CGM,
                                         llvm::PointerType *GuardPtrTy) {
  // void __cxa_guard_release(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(llvm::Type::getVoidTy(CGM.getLLVMContext()),
                            GuardPtrTy, /*isVarArg=*/false);
  
  return CGM.CreateRuntimeFunction(FTy, "__cxa_guard_release");
}

static llvm::Constant *getGuardAbortFn(CodeGenModule &CGM,
                                       llvm::PointerType *GuardPtrTy) {
  // void __cxa_guard_abort(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(llvm::Type::getVoidTy(CGM.getLLVMContext()),
                            GuardPtrTy, /*isVarArg=*/false);
  
  return CGM.CreateRuntimeFunction(FTy, "__cxa_guard_abort");
}

namespace {
  struct CallGuardAbort : EHScopeStack::Cleanup {
    llvm::GlobalVariable *Guard;
    CallGuardAbort(llvm::GlobalVariable *Guard) : Guard(Guard) {}

    void Emit(CodeGenFunction &CGF, Flags flags) {
      CGF.Builder.CreateCall(getGuardAbortFn(CGF.CGM, Guard->getType()), Guard)
        ->setDoesNotThrow();
    }
  };
}

/// The ARM code here follows the Itanium code closely enough that we
/// just special-case it at particular places.
void ItaniumCXXABI::EmitGuardedInit(CodeGenFunction &CGF,
                                    const VarDecl &D,
                                    llvm::GlobalVariable *GV) {
  CGBuilderTy &Builder = CGF.Builder;

  // We only need to use thread-safe statics for local variables;
  // global initialization is always single-threaded.
  bool threadsafe =
    (getContext().getLangOptions().ThreadsafeStatics && D.isLocalVarDecl());

  llvm::IntegerType *GuardTy;

  // If we have a global variable with internal linkage and thread-safe statics
  // are disabled, we can just let the guard variable be of type i8.
  bool useInt8GuardVariable = !threadsafe && GV->hasInternalLinkage();
  if (useInt8GuardVariable) {
    GuardTy = CGF.Int8Ty;
  } else {
    // Guard variables are 64 bits in the generic ABI and 32 bits on ARM.
    GuardTy = (IsARM ? CGF.Int32Ty : CGF.Int64Ty);
  }
  llvm::PointerType *GuardPtrTy = GuardTy->getPointerTo();

  // Create the guard variable.
  llvm::SmallString<256> GuardVName;
  llvm::raw_svector_ostream Out(GuardVName);
  getMangleContext().mangleItaniumGuardVariable(&D, Out);
  Out.flush();

  // Just absorb linkage and visibility from the variable.
  llvm::GlobalVariable *GuardVariable =
    new llvm::GlobalVariable(CGM.getModule(), GuardTy,
                             false, GV->getLinkage(),
                             llvm::ConstantInt::get(GuardTy, 0),
                             GuardVName.str());
  GuardVariable->setVisibility(GV->getVisibility());

  // Test whether the variable has completed initialization.
  llvm::Value *IsInitialized;

  // ARM C++ ABI 3.2.3.1:
  //   To support the potential use of initialization guard variables
  //   as semaphores that are the target of ARM SWP and LDREX/STREX
  //   synchronizing instructions we define a static initialization
  //   guard variable to be a 4-byte aligned, 4- byte word with the
  //   following inline access protocol.
  //     #define INITIALIZED 1
  //     if ((obj_guard & INITIALIZED) != INITIALIZED) {
  //       if (__cxa_guard_acquire(&obj_guard))
  //         ...
  //     }
  if (IsARM && !useInt8GuardVariable) {
    llvm::Value *V = Builder.CreateLoad(GuardVariable);
    V = Builder.CreateAnd(V, Builder.getInt32(1));
    IsInitialized = Builder.CreateIsNull(V, "guard.uninitialized");

  // Itanium C++ ABI 3.3.2:
  //   The following is pseudo-code showing how these functions can be used:
  //     if (obj_guard.first_byte == 0) {
  //       if ( __cxa_guard_acquire (&obj_guard) ) {
  //         try {
  //           ... initialize the object ...;
  //         } catch (...) {
  //            __cxa_guard_abort (&obj_guard);
  //            throw;
  //         }
  //         ... queue object destructor with __cxa_atexit() ...;
  //         __cxa_guard_release (&obj_guard);
  //       }
  //     }
  } else {
    // Load the first byte of the guard variable.
    llvm::Type *PtrTy = Builder.getInt8PtrTy();
    llvm::LoadInst *LI = 
      Builder.CreateLoad(Builder.CreateBitCast(GuardVariable, PtrTy));
    LI->setAlignment(1);

    // Itanium ABI:
    //   An implementation supporting thread-safety on multiprocessor
    //   systems must also guarantee that references to the initialized
    //   object do not occur before the load of the initialization flag.
    //
    // In LLVM, we do this by marking the load Acquire.
    if (threadsafe)
      LI->setAtomic(llvm::Acquire);

    IsInitialized = Builder.CreateIsNull(LI, "guard.uninitialized");
  }

  llvm::BasicBlock *InitCheckBlock = CGF.createBasicBlock("init.check");
  llvm::BasicBlock *EndBlock = CGF.createBasicBlock("init.end");

  // Check if the first byte of the guard variable is zero.
  Builder.CreateCondBr(IsInitialized, InitCheckBlock, EndBlock);

  CGF.EmitBlock(InitCheckBlock);

  // Variables used when coping with thread-safe statics and exceptions.
  if (threadsafe) {    
    // Call __cxa_guard_acquire.
    llvm::Value *V
      = Builder.CreateCall(getGuardAcquireFn(CGM, GuardPtrTy), GuardVariable);
               
    llvm::BasicBlock *InitBlock = CGF.createBasicBlock("init");
  
    Builder.CreateCondBr(Builder.CreateIsNotNull(V, "tobool"),
                         InitBlock, EndBlock);
  
    // Call __cxa_guard_abort along the exceptional edge.
    CGF.EHStack.pushCleanup<CallGuardAbort>(EHCleanup, GuardVariable);
    
    CGF.EmitBlock(InitBlock);
  }

  // Emit the initializer and add a global destructor if appropriate.
  CGF.EmitCXXGlobalVarDeclInit(D, GV);

  if (threadsafe) {
    // Pop the guard-abort cleanup if we pushed one.
    CGF.PopCleanupBlock();

    // Call __cxa_guard_release.  This cannot throw.
    Builder.CreateCall(getGuardReleaseFn(CGM, GuardPtrTy), GuardVariable);
  } else {
    Builder.CreateStore(llvm::ConstantInt::get(GuardTy, 1), GuardVariable);
  }

  CGF.EmitBlock(EndBlock);
}
