//===-- AnalysisManager.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/Index/Entity.h"
#include "clang/Index/Indexer.h"

using namespace clang;
using namespace ento;

AnalysisManager::AnalysisManager(ASTContext &ctx, DiagnosticsEngine &diags,
                                 const LangOptions &lang,
                                 PathDiagnosticConsumer *pd,
                                 StoreManagerCreator storemgr,
                                 ConstraintManagerCreator constraintmgr, 
                                 CheckerManager *checkerMgr,
                                 idx::Indexer *idxer,
                                 unsigned maxnodes, unsigned maxvisit,
                                 bool vizdot, bool vizubi,
                                 AnalysisPurgeMode purge,
                                 bool eager, bool trim,
                                 bool inlinecall, bool useUnoptimizedCFG,
                                 bool addImplicitDtors, bool addInitializers,
                                 bool eagerlyTrimEGraph)
  : AnaCtxMgr(useUnoptimizedCFG, addImplicitDtors, addInitializers),
    Ctx(ctx), Diags(diags), LangInfo(lang), PD(pd),
    CreateStoreMgr(storemgr), CreateConstraintMgr(constraintmgr),
    CheckerMgr(checkerMgr), Idxer(idxer),
    AScope(ScopeDecl), MaxNodes(maxnodes), MaxVisit(maxvisit),
    VisualizeEGDot(vizdot), VisualizeEGUbi(vizubi), PurgeDead(purge),
    EagerlyAssume(eager), TrimGraph(trim), InlineCall(inlinecall),
    EagerlyTrimEGraph(eagerlyTrimEGraph)
{
  AnaCtxMgr.getCFGBuildOptions().setAllAlwaysAdd();
}

AnalysisManager::AnalysisManager(ASTContext &ctx, DiagnosticsEngine &diags,
                                 AnalysisManager &ParentAM)
  : AnaCtxMgr(ParentAM.AnaCtxMgr.getUseUnoptimizedCFG(),
              ParentAM.AnaCtxMgr.getCFGBuildOptions().AddImplicitDtors,
              ParentAM.AnaCtxMgr.getCFGBuildOptions().AddInitializers),
    Ctx(ctx), Diags(diags),
    LangInfo(ParentAM.LangInfo), PD(ParentAM.getPathDiagnosticConsumer()),
    CreateStoreMgr(ParentAM.CreateStoreMgr),
    CreateConstraintMgr(ParentAM.CreateConstraintMgr),
    CheckerMgr(ParentAM.CheckerMgr),
    Idxer(ParentAM.Idxer),
    AScope(ScopeDecl),
    MaxNodes(ParentAM.MaxNodes),
    MaxVisit(ParentAM.MaxVisit),
    VisualizeEGDot(ParentAM.VisualizeEGDot),
    VisualizeEGUbi(ParentAM.VisualizeEGUbi),
    PurgeDead(ParentAM.PurgeDead),
    EagerlyAssume(ParentAM.EagerlyAssume),
    TrimGraph(ParentAM.TrimGraph),
    InlineCall(ParentAM.InlineCall),
    EagerlyTrimEGraph(ParentAM.EagerlyTrimEGraph)
{
  AnaCtxMgr.getCFGBuildOptions().setAllAlwaysAdd();
}


AnalysisContext *
AnalysisManager::getAnalysisContextInAnotherTU(const Decl *D) {
  idx::Entity Ent = idx::Entity::get(const_cast<Decl *>(D), 
                                     Idxer->getProgram());
  FunctionDecl *FuncDef;
  idx::TranslationUnit *TU;
  llvm::tie(FuncDef, TU) = Idxer->getDefinitionFor(Ent);

  if (FuncDef == 0)
    return 0;

  // This AnalysisContext wraps function definition in another translation unit.
  // But it is still owned by the AnalysisManager associated with the current
  // translation unit.
  return AnaCtxMgr.getContext(FuncDef, TU);
}
