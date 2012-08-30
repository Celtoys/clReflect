//=-- ExplodedGraph.h - Local, Path-Sens. "Exploded Graph" -*- C++ -*-------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the template classes ExplodedNode and ExplodedGraph,
//  which represent a path-sensitive, intra-procedural "exploded graph."
//  See "Precise interprocedural dataflow analysis via graph reachability"
//  by Reps, Horwitz, and Sagiv
//  (http://portal.acm.org/citation.cfm?id=199462) for the definition of an
//  exploded graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_GR_EXPLODEDGRAPH
#define LLVM_CLANG_GR_EXPLODEDGRAPH

#include "clang/Analysis/ProgramPoint.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Support/Casting.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include <vector>

namespace clang {

class CFG;

namespace ento {

class ExplodedGraph;

//===----------------------------------------------------------------------===//
// ExplodedGraph "implementation" classes.  These classes are not typed to
// contain a specific kind of state.  Typed-specialized versions are defined
// on top of these classes.
//===----------------------------------------------------------------------===//

// ExplodedNode is not constified all over the engine because we need to add
// successors to it at any time after creating it.

class ExplodedNode : public llvm::FoldingSetNode {
  friend class ExplodedGraph;
  friend class CoreEngine;
  friend class NodeBuilder;
  friend class BranchNodeBuilder;
  friend class IndirectGotoNodeBuilder;
  friend class SwitchNodeBuilder;
  friend class EndOfFunctionNodeBuilder;

  class NodeGroup {
    enum { Size1 = 0x0, SizeOther = 0x1, AuxFlag = 0x2, Mask = 0x3 };
    uintptr_t P;

    unsigned getKind() const {
      return P & 0x1;
    }

    void *getPtr() const {
      assert (!getFlag());
      return reinterpret_cast<void*>(P & ~Mask);
    }

    ExplodedNode *getNode() const {
      return reinterpret_cast<ExplodedNode*>(getPtr());
    }
    
  public:
    NodeGroup() : P(0) {}

    ExplodedNode **begin() const;

    ExplodedNode **end() const;

    unsigned size() const;

    bool empty() const { return (P & ~Mask) == 0; }

    void addNode(ExplodedNode *N, ExplodedGraph &G);

    void replaceNode(ExplodedNode *node);

    void setFlag() {
      assert(P == 0);
      P = AuxFlag;
    }

    bool getFlag() const {
      return P & AuxFlag ? true : false;
    }
  };

  /// Location - The program location (within a function body) associated
  ///  with this node.
  const ProgramPoint Location;

  /// State - The state associated with this node.
  ProgramStateRef State;

  /// Preds - The predecessors of this node.
  NodeGroup Preds;

  /// Succs - The successors of this node.
  NodeGroup Succs;

public:

  explicit ExplodedNode(const ProgramPoint &loc, ProgramStateRef state,
                        bool IsSink)
    : Location(loc), State(state) {
    if (IsSink)
      Succs.setFlag();
  }
  
  ~ExplodedNode() {}

  /// getLocation - Returns the edge associated with the given node.
  ProgramPoint getLocation() const { return Location; }

  const LocationContext *getLocationContext() const {
    return getLocation().getLocationContext();
  }

  const Decl &getCodeDecl() const { return *getLocationContext()->getDecl(); }

  CFG &getCFG() const { return *getLocationContext()->getCFG(); }

  ParentMap &getParentMap() const {return getLocationContext()->getParentMap();}

  template <typename T>
  T &getAnalysis() const {
    return *getLocationContext()->getAnalysis<T>();
  }

  ProgramStateRef getState() const { return State; }

  template <typename T>
  const T* getLocationAs() const { return llvm::dyn_cast<T>(&Location); }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      const ProgramPoint &Loc,
                      ProgramStateRef state,
                      bool IsSink) {
    ID.Add(Loc);
    ID.AddPointer(state.getPtr());
    ID.AddBoolean(IsSink);
  }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, getLocation(), getState(), isSink());
  }

  /// addPredeccessor - Adds a predecessor to the current node, and
  ///  in tandem add this node as a successor of the other node.
  void addPredecessor(ExplodedNode *V, ExplodedGraph &G);

  unsigned succ_size() const { return Succs.size(); }
  unsigned pred_size() const { return Preds.size(); }
  bool succ_empty() const { return Succs.empty(); }
  bool pred_empty() const { return Preds.empty(); }

  bool isSink() const { return Succs.getFlag(); }

   bool hasSinglePred() const {
    return (pred_size() == 1);
  }

  ExplodedNode *getFirstPred() {
    return pred_empty() ? NULL : *(pred_begin());
  }

  const ExplodedNode *getFirstPred() const {
    return const_cast<ExplodedNode*>(this)->getFirstPred();
  }

  // Iterators over successor and predecessor vertices.
  typedef ExplodedNode**       succ_iterator;
  typedef const ExplodedNode* const * const_succ_iterator;
  typedef ExplodedNode**       pred_iterator;
  typedef const ExplodedNode* const * const_pred_iterator;

  pred_iterator pred_begin() { return Preds.begin(); }
  pred_iterator pred_end() { return Preds.end(); }

  const_pred_iterator pred_begin() const {
    return const_cast<ExplodedNode*>(this)->pred_begin();
  }
  const_pred_iterator pred_end() const {
    return const_cast<ExplodedNode*>(this)->pred_end();
  }

  succ_iterator succ_begin() { return Succs.begin(); }
  succ_iterator succ_end() { return Succs.end(); }

  const_succ_iterator succ_begin() const {
    return const_cast<ExplodedNode*>(this)->succ_begin();
  }
  const_succ_iterator succ_end() const {
    return const_cast<ExplodedNode*>(this)->succ_end();
  }

  // For debugging.

public:

  class Auditor {
  public:
    virtual ~Auditor();
    virtual void AddEdge(ExplodedNode *Src, ExplodedNode *Dst) = 0;
  };

  static void SetAuditor(Auditor* A);
  
private:
  void replaceSuccessor(ExplodedNode *node) { Succs.replaceNode(node); }
  void replacePredecessor(ExplodedNode *node) { Preds.replaceNode(node); }
};

// FIXME: Is this class necessary?
class InterExplodedGraphMap {
  virtual void anchor();
  llvm::DenseMap<const ExplodedNode*, ExplodedNode*> M;
  friend class ExplodedGraph;

public:
  ExplodedNode *getMappedNode(const ExplodedNode *N) const;

  InterExplodedGraphMap() {}
  virtual ~InterExplodedGraphMap() {}
};

class ExplodedGraph {
protected:
  friend class CoreEngine;

  // Type definitions.
  typedef std::vector<ExplodedNode *> NodeVector;

  /// The roots of the simulation graph. Usually there will be only
  /// one, but clients are free to establish multiple subgraphs within a single
  /// SimulGraph. Moreover, these subgraphs can often merge when paths from
  /// different roots reach the same state at the same program location.
  NodeVector Roots;

  /// The nodes in the simulation graph which have been
  /// specially marked as the endpoint of an abstract simulation path.
  NodeVector EndNodes;

  /// Nodes - The nodes in the graph.
  llvm::FoldingSet<ExplodedNode> Nodes;

  /// BVC - Allocator and context for allocating nodes and their predecessor
  /// and successor groups.
  BumpVectorContext BVC;

  /// NumNodes - The number of nodes in the graph.
  unsigned NumNodes;
  
  /// A list of recently allocated nodes that can potentially be recycled.
  NodeVector ChangedNodes;
  
  /// A list of nodes that can be reused.
  NodeVector FreeNodes;
  
  /// A flag that indicates whether nodes should be recycled.
  bool reclaimNodes;
  
  /// Counter to determine when to reclaim nodes.
  unsigned reclaimCounter;

public:

  /// \brief Retrieve the node associated with a (Location,State) pair,
  ///  where the 'Location' is a ProgramPoint in the CFG.  If no node for
  ///  this pair exists, it is created. IsNew is set to true if
  ///  the node was freshly created.
  ExplodedNode *getNode(const ProgramPoint &L, ProgramStateRef State,
                        bool IsSink = false,
                        bool* IsNew = 0);

  ExplodedGraph* MakeEmptyGraph() const {
    return new ExplodedGraph();
  }

  /// addRoot - Add an untyped node to the set of roots.
  ExplodedNode *addRoot(ExplodedNode *V) {
    Roots.push_back(V);
    return V;
  }

  /// addEndOfPath - Add an untyped node to the set of EOP nodes.
  ExplodedNode *addEndOfPath(ExplodedNode *V) {
    EndNodes.push_back(V);
    return V;
  }

  ExplodedGraph();

  ~ExplodedGraph();
  
  unsigned num_roots() const { return Roots.size(); }
  unsigned num_eops() const { return EndNodes.size(); }

  bool empty() const { return NumNodes == 0; }
  unsigned size() const { return NumNodes; }

  // Iterators.
  typedef ExplodedNode                        NodeTy;
  typedef llvm::FoldingSet<ExplodedNode>      AllNodesTy;
  typedef NodeVector::iterator                roots_iterator;
  typedef NodeVector::const_iterator          const_roots_iterator;
  typedef NodeVector::iterator                eop_iterator;
  typedef NodeVector::const_iterator          const_eop_iterator;
  typedef AllNodesTy::iterator                node_iterator;
  typedef AllNodesTy::const_iterator          const_node_iterator;

  node_iterator nodes_begin() { return Nodes.begin(); }

  node_iterator nodes_end() { return Nodes.end(); }

  const_node_iterator nodes_begin() const { return Nodes.begin(); }

  const_node_iterator nodes_end() const { return Nodes.end(); }

  roots_iterator roots_begin() { return Roots.begin(); }

  roots_iterator roots_end() { return Roots.end(); }

  const_roots_iterator roots_begin() const { return Roots.begin(); }

  const_roots_iterator roots_end() const { return Roots.end(); }

  eop_iterator eop_begin() { return EndNodes.begin(); }

  eop_iterator eop_end() { return EndNodes.end(); }

  const_eop_iterator eop_begin() const { return EndNodes.begin(); }

  const_eop_iterator eop_end() const { return EndNodes.end(); }

  llvm::BumpPtrAllocator & getAllocator() { return BVC.getAllocator(); }
  BumpVectorContext &getNodeAllocator() { return BVC; }

  typedef llvm::DenseMap<const ExplodedNode*, ExplodedNode*> NodeMap;

  std::pair<ExplodedGraph*, InterExplodedGraphMap*>
  Trim(const NodeTy* const* NBeg, const NodeTy* const* NEnd,
       llvm::DenseMap<const void*, const void*> *InverseMap = 0) const;

  ExplodedGraph* TrimInternal(const ExplodedNode* const * NBeg,
                              const ExplodedNode* const * NEnd,
                              InterExplodedGraphMap *M,
                    llvm::DenseMap<const void*, const void*> *InverseMap) const;

  /// Enable tracking of recently allocated nodes for potential reclamation
  /// when calling reclaimRecentlyAllocatedNodes().
  void enableNodeReclamation() { reclaimNodes = true; }

  /// Reclaim "uninteresting" nodes created since the last time this method
  /// was called.
  void reclaimRecentlyAllocatedNodes();

private:
  bool shouldCollect(const ExplodedNode *node);
  void collectNode(ExplodedNode *node);
};

class ExplodedNodeSet {
  typedef llvm::SmallPtrSet<ExplodedNode*,5> ImplTy;
  ImplTy Impl;

public:
  ExplodedNodeSet(ExplodedNode *N) {
    assert (N && !static_cast<ExplodedNode*>(N)->isSink());
    Impl.insert(N);
  }

  ExplodedNodeSet() {}

  inline void Add(ExplodedNode *N) {
    if (N && !static_cast<ExplodedNode*>(N)->isSink()) Impl.insert(N);
  }

  typedef ImplTy::iterator       iterator;
  typedef ImplTy::const_iterator const_iterator;

  unsigned size() const { return Impl.size();  }
  bool empty()    const { return Impl.empty(); }
  bool erase(ExplodedNode *N) { return Impl.erase(N); }

  void clear() { Impl.clear(); }
  void insert(const ExplodedNodeSet &S) {
    assert(&S != this);
    if (empty())
      Impl = S.Impl;
    else
      Impl.insert(S.begin(), S.end());
  }

  inline iterator begin() { return Impl.begin(); }
  inline iterator end()   { return Impl.end();   }

  inline const_iterator begin() const { return Impl.begin(); }
  inline const_iterator end()   const { return Impl.end();   }
};

} // end GR namespace

} // end clang namespace

// GraphTraits

namespace llvm {
  template<> struct GraphTraits<clang::ento::ExplodedNode*> {
    typedef clang::ento::ExplodedNode NodeType;
    typedef NodeType::succ_iterator  ChildIteratorType;
    typedef llvm::df_iterator<NodeType*>      nodes_iterator;

    static inline NodeType* getEntryNode(NodeType* N) {
      return N;
    }

    static inline ChildIteratorType child_begin(NodeType* N) {
      return N->succ_begin();
    }

    static inline ChildIteratorType child_end(NodeType* N) {
      return N->succ_end();
    }

    static inline nodes_iterator nodes_begin(NodeType* N) {
      return df_begin(N);
    }

    static inline nodes_iterator nodes_end(NodeType* N) {
      return df_end(N);
    }
  };

  template<> struct GraphTraits<const clang::ento::ExplodedNode*> {
    typedef const clang::ento::ExplodedNode NodeType;
    typedef NodeType::const_succ_iterator   ChildIteratorType;
    typedef llvm::df_iterator<NodeType*>       nodes_iterator;

    static inline NodeType* getEntryNode(NodeType* N) {
      return N;
    }

    static inline ChildIteratorType child_begin(NodeType* N) {
      return N->succ_begin();
    }

    static inline ChildIteratorType child_end(NodeType* N) {
      return N->succ_end();
    }

    static inline nodes_iterator nodes_begin(NodeType* N) {
      return df_begin(N);
    }

    static inline nodes_iterator nodes_end(NodeType* N) {
      return df_end(N);
    }
  };

} // end llvm namespace

#endif
