//===- PropagateAndConvertNoAlias.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass moves dependencies on llvm.noalias onto the noalias_sidechannel.
// It also introduces and propagates side.noalias and noalias.arg.guard
// intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/PropagateAndConvertNoAlias.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <map>
#include <type_traits>

using namespace llvm;

#define DEBUG_TYPE "convert-noalias"

namespace {

class PropagateAndConvertNoAliasLegacyPass : public FunctionPass {
public:
  static char ID;
  explicit PropagateAndConvertNoAliasLegacyPass() : FunctionPass(ID), Impl() {
    initializePropagateAndConvertNoAliasLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "Propagate and Convert Noalias intrinsics";
  }

private:
  PropagateAndConvertNoAliasPass Impl;
};
} // namespace

char PropagateAndConvertNoAliasLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(PropagateAndConvertNoAliasLegacyPass, "convert-noalias",
                      "Propagate And Convert llvm.noalias intrinsics", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(PropagateAndConvertNoAliasLegacyPass, "convert-noalias",
                    "Propagate And Convert llvm.noalias intrinsics", false,
                    false)

void PropagateAndConvertNoAliasLegacyPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  AU.addPreserved<GlobalsAAWrapperPass>();
  // FIXME: not sure the CallGraphWrapperPass is needed. It ensures the same
  // pass order is kept as if the PropagateAndConvertNoAlias pass was not there.
  AU.addPreserved<CallGraphWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
}

bool PropagateAndConvertNoAliasLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  return Impl.runImpl(F, getAnalysis<DominatorTreeWrapperPass>().getDomTree());
}

namespace llvm {

bool PropagateAndConvertNoAliasPass::runImpl(Function &F, DominatorTree &DT) {
  bool Changed = false;

  // find all 'llvm.noalias', 'llvm.noalias.gep' and 'llvm.noalias.arg.guard'
  // intrinsics
  Changed |= doit(F, DT);

  return Changed;
}

FunctionPass *createPropagateAndConvertNoAliasPass() {
  return new PropagateAndConvertNoAliasLegacyPass();
}

PropagateAndConvertNoAliasPass::PropagateAndConvertNoAliasPass() {}

PreservedAnalyses
PropagateAndConvertNoAliasPass::run(Function &F, FunctionAnalysisManager &AM) {
  bool Changed = runImpl(F, AM.getResult<DominatorTreeAnalysis>(F));

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<GlobalsAA>();
  // FIXME: not sure this is valid:
  //?? PA.preserve<CallGraphWrapperPass>(); // See above

  return PA;
}

typedef SmallVector<Instruction *, 10> SideChannelWorklist;
typedef SmallVector<Instruction *, 2> DepsVector;
typedef std::map<Instruction *, DepsVector> I2Deps;
typedef SmallSet<Instruction *, 10> InstructionSet;

// Analyse and propagate the instructions that need side channels:
// - InstructionsForSideChannel: instructions that need a side channel
// representation
// - at entry: (A)
// -- llvm.noalias  -> llvm.side.noalias
// -- llvm.noalias.arg.guard a, side_a -> side_a
//
// - during propagation: (B)
// -- select a, b, c  -> select a, side_b, side_c
// -- PHI a, b,... -> PHI side_a, side_b, ...
//
// - Handled: Instructions that have been investigated. The Deps side refers to
// the side channel dependency. (C)
// -- a nullptr indicates that the normal dependency must be used for that
// operand
// -- an I indictates that the side channel representation of I must be used for
// that operand
//
// The algorithm:
// - We start from the llvm.noalias and llvm.noalias.arg.guard instructions
// - We go over their users, and check if they are special or not
// -- special users need a side channel representation and are annotated as such
// in 'Handled' (non-empty Dep)
// -- normal instructions are a passthrough, and are annotated with an empty Dep
// in 'Handled' (I->{})
// -- some instructions stop the recursion:
// --- ICmp
// --- first arg of select
// --- llvm.noalias.side.channel, llvm.noalias
//
// After the analysis, 'Handled' contains an overview of all instructions that
// depend on (A)
// - those instructions that were seen, but ignored otherwise have no
// dependencies (I -> {} )
// - instructions that refer to one ore more side channels have explicit
// dependencies. (I -> { op0, op1, op2, ... })
// -- if opX == nullptr -> not a real side_channel dependency
// -- if opX == someI :
// ---- if someI points to an instruction in Handled, it must be one of the
// instructions that have a side channel representation
// ---- otherwise, it points to a not-handle plain dependency (coming from a
// noalias.arg.guard)
static void propagateInstructionsForSideChannel(
    SideChannelWorklist &InstructionsForSideChannel, I2Deps &Handled,
    SideChannelWorklist &out_CreationList, InstructionSet &SideChannelPHIs) {
  auto updateMatchingOperands = [](Instruction *U, Instruction *I,
                                   DepsVector &Deps, Instruction *I4SC) {
    assert(U->getNumOperands() == Deps.size());
    auto it = Deps.begin();
    for (Value *UOp : U->operands()) {
      if (UOp == I) {
        assert(*it == nullptr || *it == I4SC);
        *it = I4SC;
      }
      ++it;
    }
  };

  while (!InstructionsForSideChannel.empty()) {
    Instruction *I4SC = InstructionsForSideChannel.pop_back_val();
    LLVM_DEBUG(llvm::dbgs()
               << "-- Propagating side channel instruction: " << *I4SC << "\n");
    SmallVector<Instruction *, 10> WorkList = {I4SC};
    if (auto *CB = dyn_cast<CallBase>(I4SC)) {
      if (CB->getIntrinsicID() == Intrinsic::noalias_arg_guard) {
        // llvm.noalias.arg.guard: delegate to side_channel (operand 1)
        Handled.insert(I2Deps::value_type(I4SC, {}));
        // no need to add to out_CreationList

        assert(!isa<UndefValue>(I4SC->getOperand(0)) &&
               !isa<UndefValue>(I4SC->getOperand(1)) &&
               "Degenerated case must have been resolved already");
        assert(I4SC->getOperand(0) != I4SC->getOperand(1) &&
               "Degenerated case must have been resolved already");

        I4SC = dyn_cast<Instruction>(I4SC->getOperand(1));
        if (I4SC == nullptr) {
          // Sidechannel became a constant ? Then the arg guard is not needed
          // any more and there is nothing to propagate
          continue;
        }
      }
    }
    while (!WorkList.empty()) {
      Instruction *I = WorkList.pop_back_val();
      LLVM_DEBUG(llvm::dbgs() << "-- checking:" << *I << "\n");
      bool isPtrToInt = isa<PtrToIntInst>(I);
      for (auto &UOp : I->uses()) {
        auto *U_ = UOp.getUser();
        LLVM_DEBUG(llvm::dbgs() << "--- used by:" << *U_
                                << ", operand:" << UOp.getOperandNo() << "\n");
        Instruction *U = dyn_cast<Instruction>(U_);
        if (U == nullptr)
          continue;

        // Only see through a ptr2int if it used by a int2ptr
        if (isPtrToInt && !isa<IntToPtrInst>(U))
          continue;

        if (isa<SelectInst>(U)) {
          // ======================================== select -> { lhs, rhs }
          bool MatchesOp1 = (U->getOperand(1) == I);
          bool MatchesOp2 = (U->getOperand(2) == I);

          if (MatchesOp1 || MatchesOp2) {
            auto HI = Handled.insert(I2Deps::value_type(U, {nullptr, nullptr}));
            if (HI.second)
              out_CreationList.push_back(U);
            if (MatchesOp1) {
              HI.first->second[0] = I4SC;
            }
            if (MatchesOp2) {
              HI.first->second[1] = I4SC;
            }
            if (HI.second) {
              InstructionsForSideChannel.push_back(U);
            }
          }
        } else if (isa<LoadInst>(U)) {
          // ======================================== load -> { ptr }
          if (UOp.getOperandNo() ==
              LoadInst::getNoaliasSideChannelOperandIndex())
            continue; // tracking on side channel -> ignore

          auto HI = Handled.insert(I2Deps::value_type(U, {I4SC}));
          if (HI.second)
            out_CreationList.push_back(U);
          assert(U->getOperand(0) == I);
          if (HI.second) {
            // continue
          }
        } else if (isa<StoreInst>(U)) {
          // ======================================== store -> { val, ptr }
          if (UOp.getOperandNo() ==
              StoreInst::getNoaliasSideChannelOperandIndex())
            continue; // tracking on side channel -> ignore

          // also track if we are storing a restrict annotated pointer value...
          // This might provide useful information about 'escaping pointers'
          bool MatchesOp0 = (U->getOperand(0) == I);
          bool MatchesOp1 = (U->getOperand(1) == I);

          if (MatchesOp0 || MatchesOp1) {
            auto HI = Handled.insert(I2Deps::value_type(U, {nullptr, nullptr}));
            if (HI.second)
              out_CreationList.push_back(U);
            if (MatchesOp0) {
              HI.first->second[0] = I4SC;
            }
            if (MatchesOp1) {
              HI.first->second[1] = I4SC;
            }
          }
        } else if (isa<InsertValueInst>(U)) {
          // ======================================== insertvalue -> { val }
          // track for injecting llvm.noalias.arg.guard
          assert(U->getOperand(1) == I);
          // need to introduce a guard
          auto HI = Handled.insert(I2Deps::value_type(U, {I4SC}));
          if (HI.second)
            out_CreationList.push_back(U);
        } else if (isa<ReturnInst>(U)) {
          auto HI = Handled.insert(I2Deps::value_type(U, {I4SC}));
          if (HI.second)
            out_CreationList.push_back(U);
        } else if (isa<PHINode>(U)) {
          // ======================================== PHI -> { ..... }
          PHINode *PU = cast<PHINode>(U);
          auto HI = Handled.insert(I2Deps::value_type(U, {}));
          if (HI.second) {
            HI.first->second.resize(U->getNumOperands(), nullptr);
            if (SideChannelPHIs.count(U) == 0) {
              // This is a normal PHI, consider it for propagation
              InstructionsForSideChannel.push_back(U);
            }
            if (U->getNumOperands())
              out_CreationList.push_back(U);
          }
          updateMatchingOperands(PU, I, HI.first->second, I4SC);
        } else if (auto *CS = dyn_cast<CallBase>(U)) {
          // =============================== call/invoke/intrinsic -> { ...... }

          // NOTES:
          // - we always block at a call...
          // - the known intrinsics should not have any extra annotations
          switch (CS->getIntrinsicID()) {
          case Intrinsic::side_noalias:
          case Intrinsic::noalias: {
            bool MatchesOp0 = (U->getOperand(0) == I);
            bool MatchesOpP =
                (U->getOperand(Intrinsic::NoAliasIdentifyPArg) == I);
            static_assert(Intrinsic::NoAliasIdentifyPArg ==
                              Intrinsic::SideNoAliasIdentifyPArg,
                          "those must be identical");

            if (MatchesOp0 || MatchesOpP) {
              auto HI =
                  Handled.insert(I2Deps::value_type(U, {nullptr, nullptr}));
              if (HI.second)
                out_CreationList.push_back(U);
              if (MatchesOp0) {
                HI.first->second[0] = I4SC;
              }
              if (MatchesOpP) {
                HI.first->second[1] = I4SC;
              }
            }
            continue;
          }
          case Intrinsic::noalias_arg_guard: {
            // ignore - should be handled by the outer loop !
            continue;
          }

          default:
            break;
          }
          // if we get here, we need to inject guards for certain arguments.
          // Track which arguments will need one.
          auto HI = Handled.insert(I2Deps::value_type(U, {}));
          if (HI.second) {
            HI.first->second.resize(U->getNumOperands(), nullptr);
            if (U->getNumOperands()) {
              out_CreationList.push_back(U);
            }
          }
          updateMatchingOperands(U, I, HI.first->second, I4SC);
          if (I == CS->getReturnedArgOperand()) {
            // also see through call - this does not omit the need of
            // introducing a noalias_arg_guard
            WorkList.push_back(U);
          }
        } else {
          // ======================================== other -> {}
          // this is the generic case... not sure if we should have a elaborate
          // check for 'all other instructions'. just acknowledge that we saw it
          // and propagate to any users NOTE: if we happen have already handled
          // it, this might indicate something interesting that we should handle
          // separately

          switch (U->getOpcode()) {
          case Instruction::ICmp:
            // restrict pointer used in comparison - do not propagate
            // sidechannel
            continue;
          default:
            break;
          }

          auto HI = Handled.insert(I2Deps::value_type(U, {}));
          // No need to add to out_CreationList
          if (!HI.second) {
            llvm::errs()
                << "WARNING: found an instruction that was already handled:"
                << *U << "\n";
            assert(!HI.second &&
                   "We should not encounter a handled instruction ??");
          }

          if (HI.second) {
            WorkList.push_back(U);
          }
        }
      }
    }
  }
}

typedef SmallDenseMap<std::pair<Value *, Type *>, Value *, 16>
    ValueType2CastMap;
static Value *createBitOrPointerOrAddrSpaceCast(Value *V, Type *T,
                                                ValueType2CastMap &VT2C) {
  if (V->getType() == T)
    return V;

  // Make sure we remember what casts we introduced
  Value *&Entry = VT2C[std::make_pair(V, T)];
  if (Entry == nullptr) {
    Instruction *InsertionPoint = cast<Instruction>(V);
    if (auto *PHI = dyn_cast<PHINode>(V)) {
      InsertionPoint = PHI->getParent()->getFirstNonPHI();
    } else {
      InsertionPoint = InsertionPoint->getNextNode();
    }

    IRBuilder<> Builder(InsertionPoint);
    Entry = Builder.CreateBitOrPointerCast(V, T);
  }
  return Entry;
}

// combine llvm.side.noalias intrinsics as much as possible
void collapseSideNoalias(SideChannelWorklist &CollapseableSideNoaliasIntrinsics,
                         DominatorTree &DT) {
  if (CollapseableSideNoaliasIntrinsics.empty())
    return;

  if (!CollapseableSideNoaliasIntrinsics.empty()) {
    // sweep from back to front, then from front to back etc... until no
    // modifications are done
    do {
      LLVM_DEBUG(llvm::dbgs() << "- Trying to collapse llvm.side.noalias\n");
      SideChannelWorklist NextList;
      bool Changed = false;

      // 1)  side_noaliasA (side_noaliasB (....), ...)  -> side_noaliasB(...)
      while (!CollapseableSideNoaliasIntrinsics.empty()) {
        IntrinsicInst *I =
            cast<IntrinsicInst>(CollapseableSideNoaliasIntrinsics.back());
        assert(I->getIntrinsicID() == Intrinsic::side_noalias);

        CollapseableSideNoaliasIntrinsics.pop_back();

        // side_noalias (side_noalias(....), .... )  -> side_noalias(....)
        if (IntrinsicInst *DepI = dyn_cast<IntrinsicInst>(I->getOperand(0))) {
          // Check if the depending intrinsic is compatible)
          if (DepI->getIntrinsicID() == Intrinsic::side_noalias &&
              areSideNoaliasCompatible(DepI, I)) {
            // similar enough - look through
            LLVM_DEBUG(llvm::dbgs() << "-- Collapsing(1):" << *I << "\n");
            I->replaceAllUsesWith(DepI);
            I->eraseFromParent();
            Changed = true;
            continue;
          }
        }
        //@ FIXME: TODO:
        // side_noalias(PHI (fum, self)) -> PHI(side_noalias(fum), phiselfref)
        NextList.push_back(I);
      }

      // 2)  side_noaliasA (...), side_noaliasB(...)  --> side_noaliasA(...)
      {
        for (Instruction *I : NextList) {
          IntrinsicInst *II = cast<IntrinsicInst>(I);
          Instruction *DominatingUse = II;

          SideChannelWorklist similarSideChannels;
          for (User *U : II->getOperand(0)->users()) {
            if (IntrinsicInst *UII = dyn_cast<IntrinsicInst>(U)) {
              if (UII->getParent() && // still valid - ignore already removed
                                      // instructions
                  UII->getIntrinsicID() == Intrinsic::side_noalias &&
                  areSideNoaliasCompatible(II, UII)) {
                similarSideChannels.push_back(UII);
                if (DT.dominates(UII, DominatingUse))
                  DominatingUse = UII;
              }
            }
          }

          for (Instruction *SI : similarSideChannels) {
            if ((SI != DominatingUse) && DT.dominates(DominatingUse, SI)) {
              LLVM_DEBUG(llvm::dbgs() << "-- Collapsing(2):" << *SI << "\n");
              Changed = true;
              SI->replaceAllUsesWith(DominatingUse);
              SI->removeFromParent(); // do not yet erase !
            }
          }
        }

        if (!Changed)
          break;

        // Now eliminate all removed intrinsics
        llvm::erase_if(NextList, [](Instruction *I) {
          if (I->getParent()) {
            return false;
          } else {
            I->deleteValue();
            return true;
          }
        });
      }

      CollapseableSideNoaliasIntrinsics = NextList;
    } while (CollapseableSideNoaliasIntrinsics.size() > 1);
  }
}


// Look at users of llvm.side.noalias to find PHI nodes that are used as a side
// channel
static void deduceSideChannelPHIs(SideChannelWorklist &SideNoAliasIntrinsics,
                                  InstructionSet &out_SideChannelPHIs) {
  LLVM_DEBUG(llvm::dbgs() << "-- Looking up up sidechannel PHI nodes\n");
  for (Instruction *SNI : SideNoAliasIntrinsics) {
    SideChannelWorklist worklist = {SNI};
    while (!worklist.empty()) {
      Instruction *worker = worklist.pop_back_val();
      for (auto *SNIUser_ : worker->users()) {
        Instruction *SNIUser = dyn_cast<Instruction>(SNIUser_);
        if (SNIUser == nullptr)
          continue;

        if (isa<PHINode>(SNIUser)) {
          // Identify as a sidechannel PHI
          if (out_SideChannelPHIs.insert(cast<PHINode>(SNIUser)).second) {
            LLVM_DEBUG(llvm::dbgs() << "--- " << *SNIUser << "\n");
            // and propagate
            worklist.push_back(SNIUser);
          }
        } else if (isa<SelectInst>(SNIUser) || isa<BitCastInst>(SNIUser) ||
                   isa<AddrSpaceCastInst>(SNIUser)) {
          // look through select/bitcast/addressspacecast
          worklist.push_back(SNIUser);
        } else {
          // load/store/side.noalias/arg.guard -> stop looking
        }
      }
    }
  }
}

bool PropagateAndConvertNoAliasPass::doit(Function &F, DominatorTree &DT) {
  LLVM_DEBUG(llvm::dbgs() << "PropagateAndConvertNoAliasPass:\n");
  LLVM_DEBUG(llvm::dbgs() << "- gathering intrinsics, stores, loads:\n");

  // PHASE 0: find interesting instructions
  // - Find all:
  // -- Propagatable noalias intrinsics
  // -- Load instructions
  // -- Store instructions
  SideChannelWorklist InstructionsForSideChannel;
  SideChannelWorklist LoadStoreIntrinsicInstructions;
  SideChannelWorklist LookThroughIntrinsics;
  SideChannelWorklist CollapseableSideNoaliasIntrinsics;
  ValueType2CastMap VT2C;
  InstructionSet SideChannelPHIs;
  SideChannelWorklist DegeneratedNoAliasAndNoAliasArgGuards;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto CB = dyn_cast<CallBase>(&I)) {
        auto ID = CB->getIntrinsicID();
        if (ID == Intrinsic::noalias || ID == Intrinsic::noalias_arg_guard) {
          LLVM_DEBUG(llvm::dbgs() << "-- found intrinsic:" << I << "\n");
          auto Op0 = I.getOperand(0);
          auto Op1 = I.getOperand(1);
          if (isa<UndefValue>(Op0) ||
              ((ID == Intrinsic::noalias_arg_guard) &&
               ((Op0 == Op1) || isa<UndefValue>(Op1)))) {
            LLVM_DEBUG(llvm::dbgs() << "--- degenerated\n");
            DegeneratedNoAliasAndNoAliasArgGuards.push_back(&I);
          } else {
            InstructionsForSideChannel.push_back(&I);
            LoadStoreIntrinsicInstructions.push_back(&I);
            LookThroughIntrinsics.push_back(&I);
          }
        } else if (ID == Intrinsic::side_noalias) {
          CollapseableSideNoaliasIntrinsics.push_back(&I);
        }
      } else if (auto LI = dyn_cast<LoadInst>(&I)) {
        LLVM_DEBUG(llvm::dbgs() << "-- found load:" << I << "\n");
        LoadStoreIntrinsicInstructions.push_back(LI);
      } else if (auto SI = dyn_cast<StoreInst>(&I)) {
        LLVM_DEBUG(llvm::dbgs() << "-- found store:" << I << "\n");
        LoadStoreIntrinsicInstructions.push_back(SI);
      }
    }
  }

  // When there are no noalias related intrinsics, don't do anything.
  if (DegeneratedNoAliasAndNoAliasArgGuards.empty() &&
      InstructionsForSideChannel.empty() &&
      CollapseableSideNoaliasIntrinsics.empty()) {
    LLVM_DEBUG(llvm::dbgs() << "- Nothing to do\n");
    return false;
  }

  LLVM_DEBUG(
      llvm::dbgs() << "- Looking through degenerated llvm.noalias.arg.guard\n");
  for (Instruction *I : DegeneratedNoAliasAndNoAliasArgGuards) {
    I->replaceAllUsesWith(I->getOperand(0));
    I->eraseFromParent();
  }
  LLVM_DEBUG(llvm::dbgs() << "- Find out what to do:\n");

  deduceSideChannelPHIs(CollapseableSideNoaliasIntrinsics, SideChannelPHIs);

  // PHASE 1: forward pass:
  // - Start with all intrinsics
  // -- Track all users
  // -- Interesting users (noalias intrinsics, select, PHI, load/store)
  // -- Do this recursively for users that we can look through
  I2Deps Handled; // instruction -> { dependencies }
  SideChannelWorklist
      CreationList; // Tracks all keys in Handled, but in a reproducable way
  propagateInstructionsForSideChannel(InstructionsForSideChannel, Handled,
                                      CreationList, SideChannelPHIs);

  // PHASE 2: add missing load/store/intrinsic instructions:
  for (auto *I : LoadStoreIntrinsicInstructions) {
    if (isa<LoadInst>(I)) {
      if (Handled.insert(I2Deps::value_type(I, {nullptr})).second)
        CreationList.push_back(I);
    } else { // Store or llvm.no_alias
      if (Handled.insert(I2Deps::value_type(I, {nullptr, nullptr})).second)
        CreationList.push_back(I);
    }
  }

#if !defined(NDEBUG)
  auto dumpit = [](I2Deps::value_type &H) {
    auto &out = llvm::dbgs();
    out << *H.first << " -> {";
    bool comma = false;
    for (auto D : H.second) {
      if (comma)
        out << ",";
      comma = true;
      if (D == nullptr) {
        out << "nullptr";
      } else {
        out << *D;
      }
    }
    out << "}\n";
  };
#endif

  // PHASE 3: reconstruct alternative tree
  // - detected dependencies: replace them by new instructions
  // - undetected dependencies: use the original dependency
  // NOTE: See explanation in propagateInstructionsForSideChannel for more
  // information !
  LLVM_DEBUG(llvm::dbgs() << "- Reconstructing tree:\n");

  SideChannelWorklist UnresolvedPHI;
  SmallDenseMap<Instruction *, Value *, 16> I2NewV;
  SmallDenseMap<Instruction *, Value *, 16> I2ArgGuard;

  auto getNewIOrOperand = [&](Instruction *DepOp, Value *OrigOp) {
    assert(((!DepOp) || I2NewV.count(DepOp)) &&
           "DepOp should be known");
    return DepOp ? static_cast<Value *>(I2NewV[DepOp]) : OrigOp;
  };

  // We are doing a number of sweeps. This should always end. Normally the
  // amount of sweeps is low. During initial development, a number of bugs where
  // found by putting a hard limit on the the amount.
  unsigned Watchdog = 1000000; // Only used in assertions build
  (void)Watchdog;
  for (auto CloneableInst : CreationList) {
    assert(Handled.count(CloneableInst) &&
           "Entries in CreationList must also be in Handled");
    assert(!Handled[CloneableInst].empty() &&
           "Only non-empty items should be added to the CreationList");

    LLVM_DEBUG(llvm::dbgs() << "- "; dumpit(*Handled.find(CloneableInst)));
    SideChannelWorklist Worklist = {CloneableInst};

    while (!Worklist.empty()) {
      Instruction *I = Worklist.back();

      if (I2NewV.count(I)) {
        // already exists - skip
        Worklist.pop_back();
        continue;
      }

      LLVM_DEBUG(llvm::dbgs() << "-- Reconstructing:" << *I << "\n");

      // Check if we have all the needed arguments
      auto HandledIt = Handled.find(I);
      if (HandledIt == Handled.end()) {
        // This can happen after propagation of a llvm.noalias.arg.guard
        Worklist.pop_back();
        I2NewV[I] = I;
        LLVM_DEBUG(llvm::dbgs() << "--- Connected to an existing path!\n");
        continue;
      }

      // If we are a PHI node, just create it
      if (isa<PHINode>(I)) {
        if (SideChannelPHIs.count(cast<PHINode>(I)) == 0) {
          // But only if it is _not_ a sidechannel PHI node
          // ======================================== PHI -> { ..... }
          IRBuilder<> Builder(I);
          I2NewV[I] = Builder.CreatePHI(I->getType(), I->getNumOperands(),
                                        Twine("side.") + I->getName());

          UnresolvedPHI.push_back(I);
        } else {
          I2NewV[I] = I; // Map already existing SideChannel PHI to itself
        }
        Worklist.pop_back();
        continue;
      }

      LLVM_DEBUG(llvm::dbgs() << "--- "; dumpit(*HandledIt));
      auto &Deps = HandledIt->second;
      assert((!Deps.empty()) &&
             "Any creatable instruction must have some dependent operands");
      bool canCreateInstruction = true;
      for (auto *DepOp : Deps) {
        if (DepOp != nullptr) {
          if (I2NewV.count(DepOp) == 0) {
            canCreateInstruction = false;
            Worklist.push_back(DepOp);
          }
        }
      }
#if !defined(NDEBUG)
      if (--Watchdog == 0) {
        llvm::errs()
            << "PropagateAndConvertNoAlias: ERROR: WATCHDOG TRIGGERED !\n";
        assert(false && "PropagateAndConvertNoAlias: WATCHDOG TRIGGERED");
      }
#endif
      if (canCreateInstruction) {
        Worklist.pop_back();
        IRBuilder<> Builder(I);

        if (isa<SelectInst>(I)) {
          // ======================================== select -> { lhs, rhs }
          I2NewV[I] = Builder.CreateSelect(
              I->getOperand(0),
              createBitOrPointerOrAddrSpaceCast(
                  getNewIOrOperand(Deps[0], I->getOperand(1)), I->getType(),
                  VT2C),
              createBitOrPointerOrAddrSpaceCast(
                  getNewIOrOperand(Deps[1], I->getOperand(2)), I->getType(),
                  VT2C),
              Twine("side.") + I->getName());
        } else if (isa<LoadInst>(I)) {
          // ======================================== load -> { ptr }
          LoadInst *LI = cast<LoadInst>(I);

          if (Deps[0]) {
            if (!LI->hasNoaliasSideChannelOperand() ||
                isa<UndefValue>(LI->getNoaliasSideChannelOperand()) ||
                (LI->getPointerOperand() ==
                 LI->getNoaliasSideChannelOperand())) {
              LI->setNoaliasSideChannelOperand(
                  createBitOrPointerOrAddrSpaceCast(
                      I2NewV[Deps[0]], LI->getPointerOperandType(), VT2C));
            } else {
              // nothing to do - propagation should have happend through the
              // side channel !
              // TODO: we might want to add an extra check that the load
              // sidechannel was updated
            }
          } else {
            if (!LI->hasNoaliasSideChannelOperand()) {
              LI->setNoaliasSideChannelOperand(
                  UndefValue::get(LI->getPointerOperandType()));
            } else {
              // has alrady a side channel - nothing to merge
            }
          }
          I2NewV[I] = I;
        } else if (isa<StoreInst>(I)) {
          // ======================================== store -> { val, ptr }
          StoreInst *SI = cast<StoreInst>(I);

          if (Deps[0]) {
            // We try to store a restrict pointer - restrictness
            // FIXME: introduce a llvm.noalias.arg.guard
            //   although escape analysis is already handling it
          }
          if (Deps[1]) {
            if (!SI->hasNoaliasSideChannelOperand() ||
                isa<UndefValue>(SI->getNoaliasSideChannelOperand()) ||
                (SI->getPointerOperand() ==
                 SI->getNoaliasSideChannelOperand())) {
              SI->setNoaliasSideChannelOperand(
                  createBitOrPointerOrAddrSpaceCast(
                      I2NewV[Deps[1]], SI->getPointerOperandType(), VT2C));
            } else {
              // nothing to do - propagation should have happend through the
              // side channel !
              // TODO: we might want to add an extra check that the store
              // sidechannel was updated
            }
          } else {
            if (!SI->hasNoaliasSideChannelOperand()) {
              SI->setNoaliasSideChannelOperand(
                  UndefValue::get(SI->getPointerOperandType()));
            } else {
              // has already a side channel - nothing to merge
            }
          }
          I2NewV[I] = I;
        } else if (isa<InsertValueInst>(I)) {
          // We try to insert a restrict pointer into a struct - track it.
          // Track generated noalias_arg_guard also in I2NewI
          assert(Deps.size() == 1 &&
                 "InsertValue tracks exactly one dependency");
          Instruction *DepOp = Deps[0];
          auto *SideOp = cast<Instruction>(I2NewV[DepOp]);
          // if we get here, the operand has to be an 'Instruction' (otherwise,
          // DepOp would not be set).
          auto *OpI = cast<Instruction>(I->getOperand(1));
          auto &ArgGuard = I2ArgGuard[OpI];
          if (ArgGuard == nullptr) {
            // create the instruction close to the origin, so that we don't
            // introduce bad dependencies
            auto InsertionPointIt = OpI->getIterator();
            ++InsertionPointIt;
            if (isa<PHINode>(OpI)) {
              auto End = OpI->getParent()->end();
              while (InsertionPointIt != End) {
                if (!isa<PHINode>(*InsertionPointIt))
                  break;
                ++InsertionPointIt;
              }
            }
            IRBuilder<> BuilderForArgs(OpI->getParent(), InsertionPointIt);
            ArgGuard = BuilderForArgs.CreateNoAliasArgGuard(
                OpI,
                createBitOrPointerOrAddrSpaceCast(SideOp, OpI->getType(), VT2C),
                OpI->getName() + ".guard");
          }
          I->setOperand(1, ArgGuard);
        } else {
          // =============================== ret -> { ...... }
          // =============================== call/invoke/intrinsic -> { ...... }
          auto CB = dyn_cast<CallBase>(I);
          if (CB) {
            assert(CB && "If we get here, we should have a Call");
            switch (CB->getIntrinsicID()) {
            case Intrinsic::noalias: {
              // convert
              assert(Deps.size() == 2);
              Value *IdentifyPSideChannel;
              if (Deps[1]) {
                // do the same as with the noalias_sidechannel in the load
                // instruction
                IdentifyPSideChannel = createBitOrPointerOrAddrSpaceCast(
                    I2NewV[Deps[1]],
                    I->getOperand(Intrinsic::NoAliasIdentifyPArg)->getType(),
                    VT2C);
              } else {
                IdentifyPSideChannel = UndefValue::get(
                    I->getOperand(Intrinsic::NoAliasIdentifyPArg)->getType());
              }
              Instruction *NewI = Builder.CreateSideNoAliasPlain(
                  getNewIOrOperand(Deps[0], I->getOperand(0)),
                  I->getOperand(Intrinsic::NoAliasNoAliasDeclArg),
                  I->getOperand(Intrinsic::NoAliasIdentifyPArg),
                  IdentifyPSideChannel,
                  I->getOperand(Intrinsic::NoAliasIdentifyPObjIdArg),
                  I->getOperand(Intrinsic::NoAliasScopeArg));
              I2NewV[I] = NewI;
              CollapseableSideNoaliasIntrinsics.push_back(NewI);

              // Copy over metadata that is related to the 'getOperand(1)' (aka
              // P)
              AAMDNodes AAMetadata;
              I->getAAMetadata(AAMetadata);
              NewI->setAAMetadata(AAMetadata);
              continue;
            }
            case Intrinsic::noalias_arg_guard: {
              // no update needed - depending llvm.side_noalias/gep must have
              // been updated
              continue;
            }
            case Intrinsic::side_noalias: {
              // update
              assert((Deps[0] || Deps[1]) &&
                     "side_noalias update needs a depending operand");
              if (Deps[0])
                I->setOperand(0, createBitOrPointerOrAddrSpaceCast(
                                     I2NewV[Deps[0]], I->getType(), VT2C));
              if (Deps[1])
                I->setOperand(
                    Intrinsic::SideNoAliasIdentifyPSideChannelArg,
                    createBitOrPointerOrAddrSpaceCast(
                        I2NewV[Deps[1]],
                        I->getOperand(Intrinsic::SideNoAliasIdentifyPArg)
                            ->getType(),
                        VT2C));
              I2NewV[I] = I;
              continue;
            }
            default:
              break;
            }
          } else {
            assert(isa<ReturnInst>(I));
          }

          // Introduce a noalias_arg_guard for every argument that is
          // annotated
          assert(I->getNumOperands() == Deps.size());
          for (unsigned i = 0, ci = I->getNumOperands(); i < ci; ++i) {
            Instruction *DepOp = Deps[i];
            if (DepOp) {
              // Track generated noalias_arg_guard also in I2NewI
              auto *SideOp = cast<Instruction>(I2NewV[DepOp]);
              // if we get here, the operand has to be an 'Instruction'
              // (otherwise, DepOp would not be set).
              auto *OpI = cast<Instruction>(I->getOperand(i));
              auto &ArgGuard = I2ArgGuard[OpI];
              if (ArgGuard == nullptr) {
                // create the instruction close to the origin, so that we don't
                // introduce bad dependencies
                auto InsertionPointIt = OpI->getIterator();
                ++InsertionPointIt;
                if (isa<PHINode>(OpI)) {
                  auto End = OpI->getParent()->end();
                  while (InsertionPointIt != End) {
                    if (!isa<PHINode>(*InsertionPointIt))
                      break;
                    ++InsertionPointIt;
                  }
                }
                IRBuilder<> BuilderForArgs(OpI->getParent(), InsertionPointIt);
                ArgGuard = BuilderForArgs.CreateNoAliasArgGuard(
                    OpI,
                    createBitOrPointerOrAddrSpaceCast(SideOp, OpI->getType(),
                                                      VT2C),
                    OpI->getName() + ".guard");
              }
              I->setOperand(i, ArgGuard);
            }
          }
          I2NewV[I] = I;
        }
      }
    }
  }

  // Phase 4: resolve the generated PHI nodes
  LLVM_DEBUG(llvm::dbgs() << "- Resolving " << UnresolvedPHI.size()
                          << " PHI nodes\n");
  for (auto *PHI_ : SideChannelPHIs) {
    PHINode *PHI = cast<PHINode>(PHI_);
    auto it = Handled.find(PHI);
    if (it != Handled.end()) {
      LLVM_DEBUG(llvm::dbgs() << "-- Orig PHI:" << *PHI << "\n");
      auto &Deps = it->second;
      for (unsigned i = 0, ci = Deps.size(); i < ci; ++i) {
        LLVM_DEBUG(if (Deps[i]) llvm::dbgs()
                   << "--- UPDATING:Deps:" << *Deps[i] << "\n");
        Value *IncomingValue = Deps[i] ? I2NewV[Deps[i]] : nullptr;
        if (IncomingValue) {
          if (IncomingValue->getType() != PHI->getType()) {
            IncomingValue = createBitOrPointerOrAddrSpaceCast(
                IncomingValue, PHI->getType(), VT2C);
          }
          LLVM_DEBUG(llvm::dbgs()
                     << "--- IncomingValue:" << *IncomingValue << "\n");
          PHI->setIncomingValue(i, IncomingValue);
        }
      }
      LLVM_DEBUG(llvm::dbgs() << "-- Adapted PHI:" << *PHI << "\n");
    }
  }

  for (auto &PHI : UnresolvedPHI) {
    PHINode *BasePHI = cast<PHINode>(PHI);
    PHINode *NewPHI = cast<PHINode>(I2NewV[PHI]);
    auto &Deps = Handled[PHI];

    LLVM_DEBUG(llvm::dbgs() << "-- Orig PHI:" << *BasePHI << "\n");
    LLVM_DEBUG(llvm::dbgs() << "-- New  PHI:" << *NewPHI << "\n");
    LLVM_DEBUG(llvm::dbgs() << "-- Deps: " << Deps.size() << "\n");
    for (unsigned i = 0, ci = BasePHI->getNumOperands(); i < ci; ++i) {
      auto *BB = BasePHI->getIncomingBlock(i);
      Value *IncomingValue =
          Deps[i] ? I2NewV[Deps[i]] : BasePHI->getIncomingValue(i);
      if (IncomingValue == nullptr) {
        LLVM_DEBUG(llvm::dbgs()
                   << "--- hmm.. operand " << i << " became undef\n");
        IncomingValue = UndefValue::get(NewPHI->getType());
      }
      if (IncomingValue->getType() != NewPHI->getType()) {
        IncomingValue = createBitOrPointerOrAddrSpaceCast(
            IncomingValue, NewPHI->getType(), VT2C);
      }
      NewPHI->addIncoming(IncomingValue, BB);
    }
  }

  // Phase 5: Removing the llvm.noalias
  LLVM_DEBUG(llvm::dbgs() << "- Looking through intrinsics:\n");
  for (Instruction *I : LookThroughIntrinsics) {
    auto CB = dyn_cast<CallBase>(I);
    if (CB->getIntrinsicID() == Intrinsic::noalias ||
        CB->getIntrinsicID() == Intrinsic::noalias_arg_guard) {
      LLVM_DEBUG(llvm::dbgs() << "-- Eliminating: " << *I << "\n");
      I->replaceAllUsesWith(I->getOperand(0));
      I->eraseFromParent();
    } else {
      llvm_unreachable("unhandled lookthrough intrinsic");
    }
  }

  // Phase 6: Collapse llvm.side.noalias where possible...
  // - hmm: should we do this as a complete separate pass ??
  collapseSideNoalias(CollapseableSideNoaliasIntrinsics, DT);

  return true;
}

} // namespace llvm
