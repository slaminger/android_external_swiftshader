//===- subzero/src/IceCfgNode.cpp - Basic block (node) implementation -----===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CfgNode class, including the complexities
// of instruction insertion and in-edge calculation.
//
//===----------------------------------------------------------------------===//

#include "assembler.h"
#include "IceCfg.h"
#include "IceCfgNode.h"
#include "IceInst.h"
#include "IceLiveness.h"
#include "IceOperand.h"
#include "IceTargetLowering.h"

namespace Ice {

CfgNode::CfgNode(Cfg *Func, SizeT LabelNumber, IceString Name)
    : Func(Func), Number(LabelNumber), Name(Name), HasReturn(false),
      NeedsPlacement(false), InstCountEstimate(0) {}

// Returns the name the node was created with.  If no name was given,
// it synthesizes a (hopefully) unique name.
IceString CfgNode::getName() const {
  if (!Name.empty())
    return Name;
  return "__" + std::to_string(getIndex());
}

// Adds an instruction to either the Phi list or the regular
// instruction list.  Validates that all Phis are added before all
// regular instructions.
void CfgNode::appendInst(Inst *Inst) {
  ++InstCountEstimate;
  if (InstPhi *Phi = llvm::dyn_cast<InstPhi>(Inst)) {
    if (!Insts.empty()) {
      Func->setError("Phi instruction added to the middle of a block");
      return;
    }
    Phis.push_back(Phi);
  } else {
    Insts.push_back(Inst);
  }
}

// Renumbers the non-deleted instructions in the node.  This needs to
// be done in preparation for live range analysis.  The instruction
// numbers in a block must be monotonically increasing.  The range of
// instruction numbers in a block, from lowest to highest, must not
// overlap with the range of any other block.
void CfgNode::renumberInstructions() {
  InstNumberT FirstNumber = Func->getNextInstNumber();
  for (InstPhi *I : Phis)
    I->renumber(Func);
  for (Inst *I : Insts)
    I->renumber(Func);
  InstCountEstimate = Func->getNextInstNumber() - FirstNumber;
}

// When a node is created, the OutEdges are immediately known, but the
// InEdges have to be built up incrementally.  After the CFG has been
// constructed, the computePredecessors() pass finalizes it by
// creating the InEdges list.
void CfgNode::computePredecessors() {
  OutEdges = (*Insts.rbegin())->getTerminatorEdges();
  for (CfgNode *Succ : OutEdges)
    Succ->InEdges.push_back(this);
}

// This does part 1 of Phi lowering, by creating a new dest variable
// for each Phi instruction, replacing the Phi instruction's dest with
// that variable, and adding an explicit assignment of the old dest to
// the new dest.  For example,
//   a=phi(...)
// changes to
//   "a_phi=phi(...); a=a_phi".
//
// This is in preparation for part 2 which deletes the Phi
// instructions and appends assignment instructions to predecessor
// blocks.  Note that this transformation preserves SSA form.
void CfgNode::placePhiLoads() {
  for (InstPhi *I : Phis)
    Insts.insert(Insts.begin(), I->lower(Func));
}

// This does part 2 of Phi lowering.  For each Phi instruction at each
// out-edge, create a corresponding assignment instruction, and add
// all the assignments near the end of this block.  They need to be
// added before any branch instruction, and also if the block ends
// with a compare instruction followed by a branch instruction that we
// may want to fuse, it's better to insert the new assignments before
// the compare instruction. The tryOptimizedCmpxchgCmpBr() method
// assumes this ordering of instructions.
//
// Note that this transformation takes the Phi dest variables out of
// SSA form, as there may be assignments to the dest variable in
// multiple blocks.
//
// TODO: Defer this pass until after register allocation, then split
// critical edges, add the assignments, and lower them.  This should
// reduce the amount of shuffling at the end of each block.
void CfgNode::placePhiStores() {
  // Find the insertion point.
  InstList::iterator InsertionPoint = Insts.end();
  // Every block must end in a terminator instruction, and therefore
  // must have at least one instruction, so it's valid to decrement
  // InsertionPoint (but assert just in case).
  assert(InsertionPoint != Insts.begin());
  --InsertionPoint;
  // Confirm that InsertionPoint is a terminator instruction.  Calling
  // getTerminatorEdges() on a non-terminator instruction will cause
  // an llvm_unreachable().
  (void)(*InsertionPoint)->getTerminatorEdges();
  // SafeInsertionPoint is always immediately before the terminator
  // instruction.  If the block ends in a compare and conditional
  // branch, it's better to place the Phi store before the compare so
  // as not to interfere with compare/branch fusing.  However, if the
  // compare instruction's dest operand is the same as the new
  // assignment statement's source operand, this can't be done due to
  // data dependences, so we need to fall back to the
  // SafeInsertionPoint.  To illustrate:
  //   ; <label>:95
  //   %97 = load i8* %96, align 1
  //   %98 = icmp ne i8 %97, 0
  //   br i1 %98, label %99, label %2132
  //   ; <label>:99
  //   %100 = phi i8 [ %97, %95 ], [ %110, %108 ]
  //   %101 = phi i1 [ %98, %95 ], [ %111, %108 ]
  // would be Phi-lowered as:
  //   ; <label>:95
  //   %97 = load i8* %96, align 1
  //   %100_phi = %97 ; can be at InsertionPoint
  //   %98 = icmp ne i8 %97, 0
  //   %101_phi = %98 ; must be at SafeInsertionPoint
  //   br i1 %98, label %99, label %2132
  //   ; <label>:99
  //   %100 = %100_phi
  //   %101 = %101_phi
  //
  // TODO(stichnot): It may be possible to bypass this whole
  // SafeInsertionPoint mechanism.  If a source basic block ends in a
  // conditional branch:
  //   labelSource:
  //   ...
  //   br i1 %foo, label %labelTrue, label %labelFalse
  // and a branch target has a Phi involving the branch operand:
  //   labelTrue:
  //   %bar = phi i1 [ %foo, %labelSource ], ...
  // then we actually know the constant i1 value of the Phi operand:
  //   labelTrue:
  //   %bar = phi i1 [ true, %labelSource ], ...
  // It seems that this optimization should be done by clang or opt,
  // but we could also do it here.
  InstList::iterator SafeInsertionPoint = InsertionPoint;
  // Keep track of the dest variable of a compare instruction, so that
  // we insert the new instruction at the SafeInsertionPoint if the
  // compare's dest matches the Phi-lowered assignment's source.
  Variable *CmpInstDest = NULL;
  // If the current insertion point is at a conditional branch
  // instruction, and the previous instruction is a compare
  // instruction, then we move the insertion point before the compare
  // instruction so as not to interfere with compare/branch fusing.
  if (InstBr *Branch = llvm::dyn_cast<InstBr>(*InsertionPoint)) {
    if (!Branch->isUnconditional()) {
      if (InsertionPoint != Insts.begin()) {
        --InsertionPoint;
        if (llvm::isa<InstIcmp>(*InsertionPoint) ||
            llvm::isa<InstFcmp>(*InsertionPoint)) {
          CmpInstDest = (*InsertionPoint)->getDest();
        } else {
          ++InsertionPoint;
        }
      }
    }
  }

  // Consider every out-edge.
  for (CfgNode *Succ : OutEdges) {
    // Consider every Phi instruction at the out-edge.
    for (InstPhi *I : Succ->Phis) {
      Operand *Operand = I->getOperandForTarget(this);
      assert(Operand);
      Variable *Dest = I->getDest();
      assert(Dest);
      InstAssign *NewInst = InstAssign::create(Func, Dest, Operand);
      if (CmpInstDest == Operand)
        Insts.insert(SafeInsertionPoint, NewInst);
      else
        Insts.insert(InsertionPoint, NewInst);
    }
  }
}

// Deletes the phi instructions after the loads and stores are placed.
void CfgNode::deletePhis() {
  for (InstPhi *I : Phis)
    I->setDeleted();
}

// Splits the edge from Pred to this node by creating a new node and
// hooking up the in and out edges appropriately.  (The EdgeIndex
// parameter is only used to make the new node's name unique when
// there are multiple edges between the same pair of nodes.)  The new
// node's instruction list is initialized to the empty list, with no
// terminator instruction.  If there are multiple edges from Pred to
// this node, only one edge is split, and the particular choice of
// edge is undefined.  This could happen with a switch instruction, or
// a conditional branch that weirdly has both branches to the same
// place.  TODO(stichnot,kschimpf): Figure out whether this is legal
// in the LLVM IR or the PNaCl bitcode, and if so, we need to
// establish a strong relationship among the ordering of Pred's
// out-edge list, this node's in-edge list, and the Phi instruction's
// operand list.
CfgNode *CfgNode::splitIncomingEdge(CfgNode *Pred, SizeT EdgeIndex) {
  CfgNode *NewNode =
      Func->makeNode("split_" + Pred->getName() + "_" + getName() + "_" +
                     std::to_string(EdgeIndex));
  // The new node is added to the end of the node list, and will later
  // need to be sorted into a reasonable topological order.
  NewNode->setNeedsPlacement(true);
  // Repoint Pred's out-edge.
  bool Found = false;
  for (auto I = Pred->OutEdges.begin(), E = Pred->OutEdges.end();
       !Found && I != E; ++I) {
    if (*I == this) {
      *I = NewNode;
      NewNode->InEdges.push_back(Pred);
      Found = true;
    }
  }
  assert(Found);
  // Repoint this node's in-edge.
  Found = false;
  for (auto I = InEdges.begin(), E = InEdges.end(); !Found && I != E; ++I) {
    if (*I == Pred) {
      *I = NewNode;
      NewNode->OutEdges.push_back(this);
      Found = true;
    }
  }
  assert(Found);
  // Repoint a suitable branch instruction's target.
  Found = false;
  for (auto I = Pred->getInsts().rbegin(), E = Pred->getInsts().rend();
       !Found && I != E; ++I) {
    if (!(*I)->isDeleted()) {
      Found = (*I)->repointEdge(this, NewNode);
    }
  }
  assert(Found);
  return NewNode;
}

namespace {

// Helper function used by advancedPhiLowering().
bool sameVarOrReg(const Variable *Var, const Operand *Opnd) {
  if (Var == Opnd)
    return true;
  if (const auto Var2 = llvm::dyn_cast<Variable>(Opnd)) {
    if (Var->hasReg() && Var->getRegNum() == Var2->getRegNum())
      return true;
  }
  return false;
}

} // end of anonymous namespace

// This the "advanced" version of Phi lowering for a basic block, in
// contrast to the simple version that lowers through assignments
// involving temporaries.
//
// All Phi instructions in a basic block are conceptually executed in
// parallel.  However, if we lower Phis early and commit to a
// sequential ordering, we may end up creating unnecessary
// interferences which lead to worse register allocation.  Delaying
// Phi scheduling until after register allocation can help unless
// there are no free registers for shuffling registers or stack slots
// and spilling becomes necessary.
//
// The advanced Phi lowering starts by finding a topological sort of
// the Phi instructions, where "A=B" comes before "B=C" due to the
// anti-dependence on B.  If a topological sort is not possible due to
// a cycle, the cycle is broken by introducing a non-parallel
// temporary.  For example, a cycle arising from a permutation like
// "A=B;B=C;C=A" can become "T=A;A=B;B=C;C=T".  All else being equal,
// prefer to schedule assignments with register-allocated Src operands
// earlier, in case that register becomes free afterwards, and prefer
// to schedule assignments with register-allocated Dest variables
// later, to keep that register free for longer.
//
// Once the ordering is determined, the Cfg edge is split and the
// assignment list is lowered by the target lowering layer.  The
// specific placement of the new node within the Cfg node list is
// deferred until later, including after empty node contraction.
void CfgNode::advancedPhiLowering() {
  if (getPhis().empty())
    return;

  // Count the number of non-deleted Phi instructions.
  struct {
    InstPhi *Phi;
    Variable *Dest;
    Operand *Src;
    bool Processed;
    size_t NumPred; // number of entries whose Src is this Dest
    int32_t Weight; // preference for topological order
  } Desc[getPhis().size()];

  size_t NumPhis = 0;
  for (InstPhi *Inst : getPhis()) {
    if (!Inst->isDeleted()) {
      Desc[NumPhis].Phi = Inst;
      Desc[NumPhis].Dest = Inst->getDest();
      ++NumPhis;
    }
  }
  if (NumPhis == 0)
    return;

  SizeT InEdgeIndex = 0;
  for (CfgNode *Pred : InEdges) {
    CfgNode *Split = splitIncomingEdge(Pred, InEdgeIndex++);
    AssignList Assignments;
    SizeT Remaining = NumPhis;

    // First pass computes Src and initializes NumPred.
    for (size_t I = 0; I < NumPhis; ++I) {
      Variable *Dest = Desc[I].Dest;
      Operand *Src = Desc[I].Phi->getOperandForTarget(Pred);
      Desc[I].Src = Src;
      Desc[I].Processed = false;
      Desc[I].NumPred = 0;
      // Cherry-pick any trivial assignments, so that they don't
      // contribute to the running complexity of the topological sort.
      if (sameVarOrReg(Dest, Src)) {
        Desc[I].Processed = true;
        --Remaining;
        if (Dest != Src)
          // If Dest and Src are syntactically the same, don't bother
          // adding the assignment, because in all respects it would
          // be redundant, and if Dest/Src are on the stack, the
          // target lowering may naively decide to lower it using a
          // temporary register.
          Assignments.push_back(InstAssign::create(Func, Dest, Src));
      }
    }
    // Second pass computes NumPred by comparing every pair of Phi
    // instructions.
    for (size_t I = 0; I < NumPhis; ++I) {
      if (Desc[I].Processed)
        continue;
      const Variable *Dest = Desc[I].Dest;
      for (size_t J = 0; J < NumPhis; ++J) {
        if (Desc[J].Processed)
          continue;
        if (I != J) {
          // There shouldn't be two Phis with the same Dest variable
          // or register.
          assert(!sameVarOrReg(Dest, Desc[J].Dest));
        }
        const Operand *Src = Desc[J].Src;
        if (sameVarOrReg(Dest, Src))
          ++Desc[I].NumPred;
      }
    }

    // Another pass to compute initial Weight values.

    // Always pick NumPred=0 over NumPred>0.
    const int32_t WeightNoPreds = 4;
    // Prefer Src as a register because the register might free up.
    const int32_t WeightSrcIsReg = 2;
    // Prefer Dest not as a register because the register stays free
    // longer.
    const int32_t WeightDestNotReg = 1;

    for (size_t I = 0; I < NumPhis; ++I) {
      if (Desc[I].Processed)
        continue;
      int32_t Weight = 0;
      if (Desc[I].NumPred == 0)
        Weight += WeightNoPreds;
      if (auto Var = llvm::dyn_cast<Variable>(Desc[I].Src))
        if (Var->hasReg())
          Weight += WeightSrcIsReg;
      if (!Desc[I].Dest->hasReg())
        Weight += WeightDestNotReg;
      Desc[I].Weight = Weight;
    }

    // Repeatedly choose and process the best candidate in the
    // topological sort, until no candidates remain.  This
    // implementation is O(N^2) where N is the number of Phi
    // instructions, but with a small constant factor compared to a
    // likely implementation of O(N) topological sort.
    for (; Remaining; --Remaining) {
      size_t BestIndex = 0;
      int32_t BestWeight = -1;
      // Find the best candidate.
      for (size_t I = 0; I < NumPhis; ++I) {
        if (Desc[I].Processed)
          continue;
        int32_t Weight = 0;
        Weight = Desc[I].Weight;
        if (Weight > BestWeight) {
          BestIndex = I;
          BestWeight = Weight;
        }
      }
      assert(BestWeight >= 0);
      assert(Desc[BestIndex].NumPred <= 1);
      Variable *Dest = Desc[BestIndex].Dest;
      Operand *Src = Desc[BestIndex].Src;
      assert(!sameVarOrReg(Dest, Src));
      // Break a cycle by introducing a temporary.
      if (Desc[BestIndex].NumPred) {
        bool Found = false;
        // If the target instruction "A=B" is part of a cycle, find
        // the "X=A" assignment in the cycle because it will have to
        // be rewritten as "X=tmp".
        for (size_t J = 0; !Found && J < NumPhis; ++J) {
          if (Desc[J].Processed)
            continue;
          Operand *OtherSrc = Desc[J].Src;
          if (Desc[J].NumPred && sameVarOrReg(Dest, OtherSrc)) {
            SizeT VarNum = Func->getNumVariables();
            Variable *Tmp = Func->makeVariable(
                OtherSrc->getType(), "__split_" + std::to_string(VarNum));
            Tmp->setNeedsStackSlot();
            Assignments.push_back(InstAssign::create(Func, Tmp, OtherSrc));
            Desc[J].Src = Tmp;
            Found = true;
          }
        }
        assert(Found);
      }
      // Now that a cycle (if any) has been broken, create the actual
      // assignment.
      Assignments.push_back(InstAssign::create(Func, Dest, Src));
      // Update NumPred for all Phi assignments using this Phi's Src
      // as their Dest variable.  Also update Weight if NumPred
      // dropped from 1 to 0.
      if (auto Var = llvm::dyn_cast<Variable>(Src)) {
        for (size_t I = 0; I < NumPhis; ++I) {
          if (Desc[I].Processed)
            continue;
          if (sameVarOrReg(Var, Desc[I].Dest)) {
            if (--Desc[I].NumPred == 0)
              Desc[I].Weight += WeightNoPreds;
          }
        }
      }
      Desc[BestIndex].Processed = true;
    }

    Func->getTarget()->lowerPhiAssignments(Split, Assignments);

    // Renumber the instructions to be monotonically increasing so
    // that addNode() doesn't assert when multi-definitions are added
    // out of order.
    Split->renumberInstructions();
    Func->getVMetadata()->addNode(Split);
  }

  for (InstPhi *Inst : getPhis())
    Inst->setDeleted();
}

// Does address mode optimization.  Pass each instruction to the
// TargetLowering object.  If it returns a new instruction
// (representing the optimized address mode), then insert the new
// instruction and delete the old.
void CfgNode::doAddressOpt() {
  TargetLowering *Target = Func->getTarget();
  LoweringContext &Context = Target->getContext();
  Context.init(this);
  while (!Context.atEnd()) {
    Target->doAddressOpt();
  }
}

void CfgNode::doNopInsertion() {
  TargetLowering *Target = Func->getTarget();
  LoweringContext &Context = Target->getContext();
  Context.init(this);
  while (!Context.atEnd()) {
    Target->doNopInsertion();
    // Ensure Cur=Next, so that the nops are inserted before the current
    // instruction rather than after.
    Context.advanceNext();
    Context.advanceCur();
  }
  // Insert before all instructions.
  Context.setInsertPoint(getInsts().begin());
  Context.advanceNext();
  Context.advanceCur();
  Target->doNopInsertion();
}

// Drives the target lowering.  Passes the current instruction and the
// next non-deleted instruction for target lowering.
void CfgNode::genCode() {
  TargetLowering *Target = Func->getTarget();
  LoweringContext &Context = Target->getContext();
  // Lower the regular instructions.
  Context.init(this);
  while (!Context.atEnd()) {
    InstList::iterator Orig = Context.getCur();
    if (llvm::isa<InstRet>(*Orig))
      setHasReturn();
    Target->lower();
    // Ensure target lowering actually moved the cursor.
    assert(Context.getCur() != Orig);
  }
  // Do preliminary lowering of the Phi instructions.
  Target->prelowerPhis();
}

void CfgNode::livenessLightweight() {
  SizeT NumVars = Func->getNumVariables();
  LivenessBV Live(NumVars);
  // Process regular instructions in reverse order.
  // TODO(stichnot): Use llvm::make_range with LLVM 3.5.
  for (auto I = Insts.rbegin(), E = Insts.rend(); I != E; ++I) {
    if ((*I)->isDeleted())
      continue;
    (*I)->livenessLightweight(Func, Live);
  }
  for (InstPhi *I : Phis) {
    if (I->isDeleted())
      continue;
    I->livenessLightweight(Func, Live);
  }
}

// Performs liveness analysis on the block.  Returns true if the
// incoming liveness changed from before, false if it stayed the same.
// (If it changes, the node's predecessors need to be processed
// again.)
bool CfgNode::liveness(Liveness *Liveness) {
  SizeT NumVars = Liveness->getNumVarsInNode(this);
  LivenessBV Live(NumVars);
  LiveBeginEndMap *LiveBegin = NULL;
  LiveBeginEndMap *LiveEnd = NULL;
  // Mark the beginning and ending of each variable's live range
  // with the sentinel instruction number 0.
  if (Liveness->getMode() == Liveness_Intervals) {
    LiveBegin = Liveness->getLiveBegin(this);
    LiveEnd = Liveness->getLiveEnd(this);
    LiveBegin->clear();
    LiveEnd->clear();
    // Guess that the number of live ranges beginning is roughly the
    // number of instructions, and same for live ranges ending.
    LiveBegin->reserve(getInstCountEstimate());
    LiveEnd->reserve(getInstCountEstimate());
  }
  // Initialize Live to be the union of all successors' LiveIn.
  for (CfgNode *Succ : OutEdges) {
    Live |= Liveness->getLiveIn(Succ);
    // Mark corresponding argument of phis in successor as live.
    for (InstPhi *I : Succ->Phis)
      I->livenessPhiOperand(Live, this, Liveness);
  }
  Liveness->getLiveOut(this) = Live;

  // Process regular instructions in reverse order.
  for (auto I = Insts.rbegin(), E = Insts.rend(); I != E; ++I) {
    if ((*I)->isDeleted())
      continue;
    (*I)->liveness((*I)->getNumber(), Live, Liveness, LiveBegin, LiveEnd);
  }
  // Process phis in forward order so that we can override the
  // instruction number to be that of the earliest phi instruction in
  // the block.
  SizeT NumNonDeadPhis = 0;
  InstNumberT FirstPhiNumber = Inst::NumberSentinel;
  for (InstPhi *I : Phis) {
    if (I->isDeleted())
      continue;
    if (FirstPhiNumber == Inst::NumberSentinel)
      FirstPhiNumber = I->getNumber();
    if (I->liveness(FirstPhiNumber, Live, Liveness, LiveBegin, LiveEnd))
      ++NumNonDeadPhis;
  }

  // When using the sparse representation, after traversing the
  // instructions in the block, the Live bitvector should only contain
  // set bits for global variables upon block entry.  We validate this
  // by shrinking the Live vector and then testing it against the
  // pre-shrunk version.  (The shrinking is required, but the
  // validation is not.)
  LivenessBV LiveOrig = Live;
  Live.resize(Liveness->getNumGlobalVars());
  // Non-global arguments in the entry node are allowed to be live on
  // entry.
  bool IsEntry = (Func->getEntryNode() == this);
  if (!(IsEntry || Live == LiveOrig)) {
    // This is a fatal liveness consistency error.  Print some
    // diagnostics and abort.
    Ostream &Str = Func->getContext()->getStrDump();
    Func->resetCurrentNode();
    Str << "LiveOrig-Live =";
    for (SizeT i = Live.size(); i < LiveOrig.size(); ++i) {
      if (LiveOrig.test(i)) {
        Str << " ";
        Liveness->getVariable(i, this)->dump(Func);
      }
    }
    Str << "\n";
    llvm_unreachable("Fatal inconsistency in liveness analysis");
  }

  bool Changed = false;
  LivenessBV &LiveIn = Liveness->getLiveIn(this);
  // Add in current LiveIn
  Live |= LiveIn;
  // Check result, set LiveIn=Live
  SizeT &PrevNumNonDeadPhis = Liveness->getNumNonDeadPhis(this);
  bool LiveInChanged = (Live != LiveIn);
  Changed = (NumNonDeadPhis != PrevNumNonDeadPhis || LiveInChanged);
  if (LiveInChanged)
    LiveIn = Live;
  PrevNumNonDeadPhis = NumNonDeadPhis;
  return Changed;
}

// Now that basic liveness is complete, remove dead instructions that
// were tentatively marked as dead, and compute actual live ranges.
// It is assumed that within a single basic block, a live range begins
// at most once and ends at most once.  This is certainly true for
// pure SSA form.  It is also true once phis are lowered, since each
// assignment to the phi-based temporary is in a different basic
// block, and there is a single read that ends the live in the basic
// block that contained the actual phi instruction.
void CfgNode::livenessPostprocess(LivenessMode Mode, Liveness *Liveness) {
  InstNumberT FirstInstNum = Inst::NumberSentinel;
  InstNumberT LastInstNum = Inst::NumberSentinel;
  // Process phis in any order.  Process only Dest operands.
  for (InstPhi *I : Phis) {
    I->deleteIfDead();
    if (I->isDeleted())
      continue;
    if (FirstInstNum == Inst::NumberSentinel)
      FirstInstNum = I->getNumber();
    assert(I->getNumber() > LastInstNum);
    LastInstNum = I->getNumber();
  }
  // Process instructions
  for (Inst *I : Insts) {
    I->deleteIfDead();
    if (I->isDeleted())
      continue;
    if (FirstInstNum == Inst::NumberSentinel)
      FirstInstNum = I->getNumber();
    assert(I->getNumber() > LastInstNum);
    LastInstNum = I->getNumber();
    // Create fake live ranges for a Kill instruction, but only if the
    // linked instruction is still alive.
    if (Mode == Liveness_Intervals) {
      if (InstFakeKill *Kill = llvm::dyn_cast<InstFakeKill>(I)) {
        if (!Kill->getLinked()->isDeleted()) {
          SizeT NumSrcs = I->getSrcSize();
          for (SizeT Src = 0; Src < NumSrcs; ++Src) {
            Variable *Var = llvm::cast<Variable>(I->getSrc(Src));
            InstNumberT InstNumber = I->getNumber();
            Var->addLiveRange(InstNumber, InstNumber, 1);
          }
        }
      }
    }
  }
  if (Mode != Liveness_Intervals)
    return;
  TimerMarker T1(TimerStack::TT_liveRangeCtor, Func);

  SizeT NumVars = Liveness->getNumVarsInNode(this);
  LivenessBV &LiveIn = Liveness->getLiveIn(this);
  LivenessBV &LiveOut = Liveness->getLiveOut(this);
  LiveBeginEndMap &MapBegin = *Liveness->getLiveBegin(this);
  LiveBeginEndMap &MapEnd = *Liveness->getLiveEnd(this);
  std::sort(MapBegin.begin(), MapBegin.end());
  std::sort(MapEnd.begin(), MapEnd.end());
  // Verify there are no duplicates.
  struct ComparePair {
    bool operator()(const LiveBeginEndMapEntry &A,
                    const LiveBeginEndMapEntry &B) {
      return A.first == B.first;
    }
  };
  assert(std::adjacent_find(MapBegin.begin(), MapBegin.end(), ComparePair()) ==
         MapBegin.end());
  assert(std::adjacent_find(MapEnd.begin(), MapEnd.end(), ComparePair()) ==
         MapEnd.end());

  LivenessBV LiveInAndOut = LiveIn;
  LiveInAndOut &= LiveOut;

  // Iterate in parallel across the sorted MapBegin[] and MapEnd[].
  auto IBB = MapBegin.begin(), IEB = MapEnd.begin();
  auto IBE = MapBegin.end(), IEE = MapEnd.end();
  while (IBB != IBE || IEB != IEE) {
    SizeT i1 = IBB == IBE ? NumVars : IBB->first;
    SizeT i2 = IEB == IEE ? NumVars : IEB->first;
    SizeT i = std::min(i1, i2);
    // i1 is the Variable number of the next MapBegin entry, and i2 is
    // the Variable number of the next MapEnd entry.  If i1==i2, then
    // the Variable's live range begins and ends in this block.  If
    // i1<i2, then i1's live range begins at instruction IBB->second
    // and extends through the end of the block.  If i1>i2, then i2's
    // live range begins at the first instruction of the block and
    // ends at IEB->second.  In any case, we choose the lesser of i1
    // and i2 and proceed accordingly.
    InstNumberT LB = i == i1 ? IBB->second : FirstInstNum;
    InstNumberT LE = i == i2 ? IEB->second : LastInstNum + 1;

    Variable *Var = Liveness->getVariable(i, this);
    if (!Var->getIgnoreLiveness()) {
      if (LB > LE) {
        Var->addLiveRange(FirstInstNum, LE, 1);
        Var->addLiveRange(LB, LastInstNum + 1, 1);
        // Assert that Var is a global variable by checking that its
        // liveness index is less than the number of globals.  This
        // ensures that the LiveInAndOut[] access is valid.
        assert(i < Liveness->getNumGlobalVars());
        LiveInAndOut[i] = false;
      } else {
        Var->addLiveRange(LB, LE, 1);
      }
    }
    if (i == i1)
      ++IBB;
    if (i == i2)
      ++IEB;
  }
  // Process the variables that are live across the entire block.
  for (int i = LiveInAndOut.find_first(); i != -1;
       i = LiveInAndOut.find_next(i)) {
    Variable *Var = Liveness->getVariable(i, this);
    Var->addLiveRange(FirstInstNum, LastInstNum + 1, 1);
  }
}

// If this node contains only deleted instructions, and ends in an
// unconditional branch, contract the node by repointing all its
// in-edges to its successor.
void CfgNode::contractIfEmpty() {
  if (InEdges.size() == 0)
    return;
  Inst *Branch = NULL;
  for (Inst *I : Insts) {
    if (I->isDeleted())
      continue;
    if (I->isUnconditionalBranch())
      Branch = I;
    else if (!I->isRedundantAssign())
      return;
  }
  Branch->setDeleted();
  assert(OutEdges.size() == 1);
  // Repoint all this node's in-edges to this node's successor.
  for (CfgNode *Pred : InEdges) {
    for (auto I = Pred->OutEdges.begin(), E = Pred->OutEdges.end(); I != E;
         ++I) {
      if (*I == this) {
        *I = OutEdges[0];
        OutEdges[0]->InEdges.push_back(Pred);
      }
    }
    for (Inst *I : Pred->getInsts()) {
      if (!I->isDeleted())
        I->repointEdge(this, OutEdges[0]);
    }
  }
  InEdges.clear();
  // Don't bother removing the single out-edge, which would also
  // require finding the corresponding in-edge in the successor and
  // removing it.
}

void CfgNode::doBranchOpt(const CfgNode *NextNode) {
  TargetLowering *Target = Func->getTarget();
  // Check every instruction for a branch optimization opportunity.
  // It may be more efficient to iterate in reverse and stop after the
  // first opportunity, unless there is some target lowering where we
  // have the possibility of multiple such optimizations per block
  // (currently not the case for x86 lowering).
  for (Inst *I : Insts) {
    if (!I->isDeleted()) {
      Target->doBranchOpt(I, NextNode);
    }
  }
}

// ======================== Dump routines ======================== //

namespace {

// Helper functions for emit().

void emitRegisterUsage(Ostream &Str, const Cfg *Func, const CfgNode *Node,
                       bool IsLiveIn, std::vector<SizeT> &LiveRegCount) {
  Liveness *Liveness = Func->getLiveness();
  const LivenessBV *Live;
  if (IsLiveIn) {
    Live = &Liveness->getLiveIn(Node);
    Str << "\t\t\t\t# LiveIn=";
  } else {
    Live = &Liveness->getLiveOut(Node);
    Str << "\t\t\t\t# LiveOut=";
  }
  if (!Live->empty()) {
    bool First = true;
    for (SizeT i = 0; i < Live->size(); ++i) {
      if ((*Live)[i]) {
        Variable *Var = Liveness->getVariable(i, Node);
        if (Var->hasReg()) {
          if (IsLiveIn)
            ++LiveRegCount[Var->getRegNum()];
          if (!First)
            Str << ",";
          First = false;
          Var->emit(Func);
        }
      }
    }
  }
  Str << "\n";
}

void emitLiveRangesEnded(Ostream &Str, const Cfg *Func, const Inst *Instr,
                         std::vector<SizeT> &LiveRegCount) {
  bool First = true;
  Variable *Dest = Instr->getDest();
  if (Dest && Dest->hasReg())
    ++LiveRegCount[Dest->getRegNum()];
  for (SizeT I = 0; I < Instr->getSrcSize(); ++I) {
    Operand *Src = Instr->getSrc(I);
    SizeT NumVars = Src->getNumVars();
    for (SizeT J = 0; J < NumVars; ++J) {
      const Variable *Var = Src->getVar(J);
      if (Var->hasReg()) {
        if (Instr->isLastUse(Var) &&
            --LiveRegCount[Var->getRegNum()] == 0) {
          if (First)
            Str << " \t# END=";
          else
            Str << ",";
          Var->emit(Func);
          First = false;
        }
      }
    }
  }
}

} // end of anonymous namespace

void CfgNode::emit(Cfg *Func) const {
  Func->setCurrentNode(this);
  Ostream &Str = Func->getContext()->getStrEmit();
  Liveness *Liveness = Func->getLiveness();
  bool DecorateAsm = Liveness && Func->getContext()->getFlags().DecorateAsm;
  if (Func->getEntryNode() == this) {
    Str << Func->getContext()->mangleName(Func->getFunctionName()) << ":\n";
  }
  Str << getAsmName() << ":\n";
  if (Func->useIntegratedAssembler()) {
    Assembler *Asm = Func->getAssembler<Assembler>();
    Asm->BindCfgNodeLabel(getIndex());
  }
  std::vector<SizeT> LiveRegCount(Func->getTarget()->getNumRegisters());
  if (DecorateAsm)
    emitRegisterUsage(Str, Func, this, true, LiveRegCount);

  for (InstPhi *Phi : Phis) {
    if (Phi->isDeleted())
      continue;
    // Emitting a Phi instruction should cause an error.
    Inst *Instr = Phi;
    Instr->emit(Func);
  }
  for (Inst *I : Insts) {
    if (I->isDeleted())
      continue;
    if (I->isRedundantAssign()) {
      Variable *Dest = I->getDest();
      if (DecorateAsm && Dest->hasReg() && !I->isLastUse(I->getSrc(0)))
        ++LiveRegCount[Dest->getRegNum()];
      continue;
    }
    if (Func->useIntegratedAssembler()) {
      I->emitIAS(Func);
    } else {
      I->emit(Func);
      if (DecorateAsm)
        emitLiveRangesEnded(Str, Func, I, LiveRegCount);
      Str << "\n";
    }
    // Update emitted instruction count, plus fill/spill count for
    // Variable operands without a physical register.
    if (uint32_t Count = I->getEmitInstCount()) {
      Func->getContext()->statsUpdateEmitted(Count);
      if (Variable *Dest = I->getDest()) {
        if (!Dest->hasReg())
          Func->getContext()->statsUpdateFills();
      }
      for (SizeT S = 0; S < I->getSrcSize(); ++S) {
        if (Variable *Src = llvm::dyn_cast<Variable>(I->getSrc(S))) {
          if (!Src->hasReg())
            Func->getContext()->statsUpdateSpills();
        }
      }
    }
  }
  if (DecorateAsm)
    emitRegisterUsage(Str, Func, this, false, LiveRegCount);
}

void CfgNode::dump(Cfg *Func) const {
  Func->setCurrentNode(this);
  Ostream &Str = Func->getContext()->getStrDump();
  Liveness *Liveness = Func->getLiveness();
  if (Func->getContext()->isVerbose(IceV_Instructions)) {
    Str << getName() << ":\n";
  }
  // Dump list of predecessor nodes.
  if (Func->getContext()->isVerbose(IceV_Preds) && !InEdges.empty()) {
    Str << "    // preds = ";
    bool First = true;
    for (CfgNode *I : InEdges) {
      if (!First)
        Str << ", ";
      First = false;
      Str << "%" << I->getName();
    }
    Str << "\n";
  }
  // Dump the live-in variables.
  LivenessBV LiveIn;
  if (Liveness)
    LiveIn = Liveness->getLiveIn(this);
  if (Func->getContext()->isVerbose(IceV_Liveness) && !LiveIn.empty()) {
    Str << "    // LiveIn:";
    for (SizeT i = 0; i < LiveIn.size(); ++i) {
      if (LiveIn[i]) {
        Variable *Var = Liveness->getVariable(i, this);
        Str << " %" << Var->getName();
        if (Func->getContext()->isVerbose(IceV_RegOrigins) && Var->hasReg()) {
          Str << ":" << Func->getTarget()->getRegName(Var->getRegNum(),
                                                      Var->getType());
        }
      }
    }
    Str << "\n";
  }
  // Dump each instruction.
  if (Func->getContext()->isVerbose(IceV_Instructions)) {
    for (InstPhi *I : Phis)
      I->dumpDecorated(Func);
    for (Inst *I : Insts)
      I->dumpDecorated(Func);
  }
  // Dump the live-out variables.
  LivenessBV LiveOut;
  if (Liveness)
    LiveOut = Liveness->getLiveOut(this);
  if (Func->getContext()->isVerbose(IceV_Liveness) && !LiveOut.empty()) {
    Str << "    // LiveOut:";
    for (SizeT i = 0; i < LiveOut.size(); ++i) {
      if (LiveOut[i]) {
        Variable *Var = Liveness->getVariable(i, this);
        Str << " %" << Var->getName();
        if (Func->getContext()->isVerbose(IceV_RegOrigins) && Var->hasReg()) {
          Str << ":" << Func->getTarget()->getRegName(Var->getRegNum(),
                                                      Var->getType());
        }
      }
    }
    Str << "\n";
  }
  // Dump list of successor nodes.
  if (Func->getContext()->isVerbose(IceV_Succs)) {
    Str << "    // succs = ";
    bool First = true;
    for (CfgNode *I : OutEdges) {
      if (!First)
        Str << ", ";
      First = false;
      Str << "%" << I->getName();
    }
    Str << "\n";
  }
}

} // end of namespace Ice
