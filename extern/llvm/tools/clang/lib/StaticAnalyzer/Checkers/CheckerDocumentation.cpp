//= CheckerDocumentation.cpp - Documentation checker ---------------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker lists all the checker callbacks and provides documentation for
// checker writers.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

// All checkers should be placed into anonymous namespace.
// We place the CheckerDocumentation inside ento namespace to make the
// it visible in doxygen.
namespace ento {

/// This checker documents the callback functions checkers can use to implement
/// the custom handling of the specific events during path exploration as well
/// as reporting bugs. Most of the callbacks are targeted at path-sensitive
/// checking.
///
/// \sa CheckerContext
class CheckerDocumentation : public Checker< check::PreStmt<DeclStmt>,
                                       check::PostStmt<CallExpr>,
                                       check::PreObjCMessage,
                                       check::PostObjCMessage,
                                       check::BranchCondition,
                                       check::Location,
                                       check::Bind,
                                       check::DeadSymbols,
                                       check::EndPath,
                                       check::EndAnalysis,
                                       check::EndOfTranslationUnit,
                                       eval::Call,
                                       eval::Assume,
                                       check::LiveSymbols,
                                       check::RegionChanges,
                                       check::Event<ImplicitNullDerefEvent>,
                                       check::ASTDecl<FunctionDecl> > {
public:

  /// \brief Pre-visit the Statement.
  ///
  /// The method will be called before the analyzer core processes the
  /// statement. The notification is performed for every explored CFGElement,
  /// which does not include the control flow statements such as IfStmt. The
  /// callback can be specialized to be called with any subclass of Stmt.
  ///
  /// See checkBranchCondition() callback for performing custom processing of
  /// the branching statements.
  ///
  /// check::PreStmt<DeclStmt>
  void checkPreStmt(const DeclStmt *DS, CheckerContext &C) const {}

  /// \brief Post-visit the Statement.
  ///
  /// The method will be called after the analyzer core processes the
  /// statement. The notification is performed for every explored CFGElement,
  /// which does not include the control flow statements such as IfStmt. The
  /// callback can be specialized to be called with any subclass of Stmt.
  ///
  /// check::PostStmt<DeclStmt>
  void checkPostStmt(const CallExpr *DS, CheckerContext &C) const;

  /// \brief Pre-visit the Objective C messages.
  void checkPreObjCMessage(const ObjCMessage &Msg, CheckerContext &C) const {}

  /// \brief Post-visit the Objective C messages.
  void checkPostObjCMessage(const ObjCMessage &Msg, CheckerContext &C) const {}

  /// \brief Pre-visit of the condition statement of a branch (such as IfStmt).
  void checkBranchCondition(const Stmt *Condition, CheckerContext &Ctx) const {}

  /// \brief Called on a load from and a store to a location.
  ///
  /// The method will be called each time a location (pointer) value is
  /// accessed.
  /// \param Loc    The value of the location (pointer).
  /// \param IsLoad The flag specifying if the location is a store or a load.
  /// \param S      The load is performed while processing the statement.
  ///
  /// check::Location
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const {}

  /// \brief Called on binding of a value to a location.
  ///
  /// \param Loc The value of the location (pointer).
  /// \param Val The value which will be stored at the location Loc.
  /// \param S   The bind is performed while processing the statement S.
  ///
  /// check::Bind
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const {}


  /// \brief Called whenever a symbol becomes dead.
  ///
  /// This callback should be used by the checkers to aggressively clean
  /// up/reduce the checker state, which is important for reducing the overall
  /// memory usage. Specifically, if a checker keeps symbol specific information
  /// in the sate, it can and should be dropped after the symbol becomes dead.
  /// In addition, reporting a bug as soon as the checker becomes dead leads to
  /// more precise diagnostics. (For example, one should report that a malloced
  /// variable is not freed right after it goes out of scope.)
  ///
  /// \param SR The SymbolReaper object can be queried to determine which
  ///           symbols are dead.
  ///
  /// check::DeadSymbols
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const {}

  /// \brief Called when an end of path is reached in the ExplodedGraph.
  ///
  /// This callback should be used to check if the allocated resources are freed.
  ///
  /// check::EndPath
  void checkEndPath(CheckerContext &Ctx) const {}

  /// \brief Called after all the paths in the ExplodedGraph reach end of path
  /// - the symbolic execution graph is fully explored.
  ///
  /// This callback should be used in cases when a checker needs to have a
  /// global view of the information generated on all paths. For example, to
  /// compare execution summary/result several paths.
  /// See IdempotentOperationChecker for a usage example.
  ///
  /// check::EndAnalysis
  void checkEndAnalysis(ExplodedGraph &G,
                        BugReporter &BR,
                        ExprEngine &Eng) const {}

  /// \brief Called after analysis of a TranslationUnit is complete.
  ///
  /// check::EndOfTranslationUnit
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                 AnalysisManager &Mgr,
                                 BugReporter &BR) const {}


  /// \brief Evaluates function call.
  ///
  /// The analysis core threats all function calls in the same way. However, some
  /// functions have special meaning, which should be reflected in the program
  /// state. This callback allows a checker to provide domain specific knowledge
  /// about the particular functions it knows about.
  ///
  /// \returns true if the call has been successfully evaluated
  /// and false otherwise. Note, that only one checker can evaluate a call. If
  /// more then one checker claim that they can evaluate the same call the
  /// first one wins.
  ///
  /// eval::Call
  bool evalCall(const CallExpr *CE, CheckerContext &C) const { return true; }

  /// \brief Handles assumptions on symbolic values.
  ///
  /// This method is called when a symbolic expression is assumed to be true or
  /// false. For example, the assumptions are performed when evaluating a
  /// condition at a branch. The callback allows checkers track the assumptions
  /// performed on the symbols of interest and change the state accordingly.
  ///
  /// eval::Assume
  ProgramStateRef evalAssume(ProgramStateRef State,
                                 SVal Cond,
                                 bool Assumption) const { return State; }

  /// Allows modifying SymbolReaper object. For example, checkers can explicitly
  /// register symbols of interest as live. These symbols will not be marked
  /// dead and removed.
  ///
  /// check::LiveSymbols
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const {}


  bool wantsRegionChangeUpdate(ProgramStateRef St) const { return true; }
  
  /// check::RegionChanges
  /// Allows tracking regions which get invalidated.
  /// \param state The current program state.
  /// \param invalidated A set of all symbols potentially touched by the change.
  /// \param ExplicitRegions The regions explicitly requested for invalidation.
  ///   For example, in the case of a function call, these would be arguments.
  /// \param Regions The transitive closure of accessible regions,
  ///   i.e. all regions that may have been touched by this change.
  /// \param The call expression wrapper if the regions are invalidated by a
  ///   call, 0 otherwise.
  /// Note, in order to be notified, the checker should also implement 
  /// wantsRegionChangeUpdate callback.
  ProgramStateRef 
    checkRegionChanges(ProgramStateRef State,
                       const StoreManager::InvalidatedSymbols *,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const CallOrObjCMessage *Call) const {
    return State;
  }

  /// check::Event<ImplicitNullDerefEvent>
  void checkEvent(ImplicitNullDerefEvent Event) const {}

  /// \brief Check every declaration in the AST.
  ///
  /// An AST traversal callback, which should only be used when the checker is
  /// not path sensitive. It will be called for every Declaration in the AST and
  /// can be specialized to only be called on subclasses of Decl, for example,
  /// FunctionDecl.
  ///
  /// check::ASTDecl<FunctionDecl>
  void checkASTDecl(const FunctionDecl *D,
                    AnalysisManager &Mgr,
                    BugReporter &BR) const {}

};

void CheckerDocumentation::checkPostStmt(const CallExpr *DS,
                                         CheckerContext &C) const {
  return;
}

} // end namespace
