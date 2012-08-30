//===- TemplateDeduction.h - C++ template argument deduction ----*- C++ -*-===/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===/
//
//  This file provides types used with Sema's template argument deduction
// routines.
//
//===----------------------------------------------------------------------===/
#ifndef LLVM_CLANG_SEMA_TEMPLATE_DEDUCTION_H
#define LLVM_CLANG_SEMA_TEMPLATE_DEDUCTION_H

#include "clang/Basic/PartialDiagnostic.h"
#include "clang/AST/DeclTemplate.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class ASTContext;
class TemplateArgumentList;

namespace sema {

/// \brief Provides information about an attempted template argument
/// deduction, whose success or failure was described by a
/// TemplateDeductionResult value.
class TemplateDeductionInfo {
  /// \brief The context in which the template arguments are stored.
  ASTContext &Context;

  /// \brief The deduced template argument list.
  ///
  TemplateArgumentList *Deduced;

  /// \brief The source location at which template argument
  /// deduction is occurring.
  SourceLocation Loc;

  /// \brief Warnings (and follow-on notes) that were suppressed due to
  /// SFINAE while performing template argument deduction.
  SmallVector<PartialDiagnosticAt, 4> SuppressedDiagnostics;

  // do not implement these
  TemplateDeductionInfo(const TemplateDeductionInfo&);
  TemplateDeductionInfo &operator=(const TemplateDeductionInfo&);

public:
  TemplateDeductionInfo(ASTContext &Context, SourceLocation Loc)
    : Context(Context), Deduced(0), Loc(Loc) { }

  ~TemplateDeductionInfo() {
    // FIXME: if (Deduced) Deduced->Destroy(Context);
  }

  /// \brief Returns the location at which template argument is
  /// occurring.
  SourceLocation getLocation() const {
    return Loc;
  }

  /// \brief Take ownership of the deduced template argument list.
  TemplateArgumentList *take() {
    TemplateArgumentList *Result = Deduced;
    Deduced = 0;
    return Result;
  }

  /// \brief Provide a new template argument list that contains the
  /// results of template argument deduction.
  void reset(TemplateArgumentList *NewDeduced) {
    // FIXME: if (Deduced) Deduced->Destroy(Context);
    Deduced = NewDeduced;
  }

  /// \brief Add a new diagnostic to the set of diagnostics
  void addSuppressedDiagnostic(SourceLocation Loc,
                               const PartialDiagnostic &PD) {
    SuppressedDiagnostics.push_back(std::make_pair(Loc, PD));
  }

  /// \brief Iterator over the set of suppressed diagnostics.
  typedef SmallVectorImpl<PartialDiagnosticAt>::const_iterator
    diag_iterator;

  /// \brief Returns an iterator at the beginning of the sequence of suppressed
  /// diagnostics.
  diag_iterator diag_begin() const { return SuppressedDiagnostics.begin(); }

  /// \brief Returns an iterator at the end of the sequence of suppressed
  /// diagnostics.
  diag_iterator diag_end() const { return SuppressedDiagnostics.end(); }

  /// \brief The template parameter to which a template argument
  /// deduction failure refers.
  ///
  /// Depending on the result of template argument deduction, this
  /// template parameter may have different meanings:
  ///
  ///   TDK_Incomplete: this is the first template parameter whose
  ///   corresponding template argument was not deduced.
  ///
  ///   TDK_Inconsistent: this is the template parameter for which
  ///   two different template argument values were deduced.
  TemplateParameter Param;

  /// \brief The first template argument to which the template
  /// argument deduction failure refers.
  ///
  /// Depending on the result of the template argument deduction,
  /// this template argument may have different meanings:
  ///
  ///   TDK_Inconsistent: this argument is the first value deduced
  ///   for the corresponding template parameter.
  ///
  ///   TDK_SubstitutionFailure: this argument is the template
  ///   argument we were instantiating when we encountered an error.
  ///
  ///   TDK_NonDeducedMismatch: this is the template argument
  ///   provided in the source code.
  TemplateArgument FirstArg;

  /// \brief The second template argument to which the template
  /// argument deduction failure refers.
  ///
  /// FIXME: Finish documenting this.
  TemplateArgument SecondArg;
};

}
}

#endif
