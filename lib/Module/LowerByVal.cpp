//===-- LowerByVal.cpp - Eliminate byval calls -------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Replaces byval calls with code that allocates a copy of
// the argument on the stack and then does a regular call
//
//===----------------------------------------------------------------------===//

#include "Passes.h"
#include "klee/Config/Version.h"

#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(10, 0)
#include "llvm/Support/TypeSize.h"
#else
typedef unsigned TypeSize;
#endif

#include <algorithm>

using namespace llvm;

namespace klee {

char LowerByValPass::ID = 0;

bool LowerByValPass::runOnModule(Module &m) {
  bool changed = false;
  //for (Module::iterator f = M.begin(), fe = M.end(); f != fe; ++f)
  for (auto &f : m)
    for (BasicBlock &bb : f)
      changed |= runOnBasicBlock(bb, m);

  return changed;
}

bool LowerByValPass::runOnBasicBlock(BasicBlock &bb, Module &m) {
  bool changed = false;

  for (Instruction &i : bb) {
    if ((i.getOpcode() == Instruction::Call) ||
        (i.getOpcode() == Instruction::Invoke)) {
      // Ignore debug intrinsic calls
      if (isa<DbgInfoIntrinsic>(i))
        continue;

      CallSite cs(&i);
      unsigned numArgs = cs.arg_size();
      Function *f = cs.getCalledFunction();
      if (f)
        llvm::errs() << "Processing " << f->getName() << "\n";
      else
        llvm::errs() << "Processing indirect call\n";

      for (unsigned k = 0; k < numArgs; k++) {
        if (cs.isByValArgument(k)) {
          auto arg = cs.getArgument(k);
          llvm::errs() << "---Argument " << *arg << " is byval\n";
          cs.removeParamAttr(k, Attribute::ByVal);
          assert(!cs.isByValArgument(k));

#if LLVM_VERSION_CODE >= LLVM_VERSION(9, 0)
          Type *t = cs.getParamByValType(k);
#else
          Type *t = arg->getType();
          assert(t->isPointerTy());
          t = t->getPointerElementType();
#endif
          TypeSize bits = DataLayout.getTypeSizeInBits(t);
          TypeSize bytes = (bits + 7) / 8;
          llvm::errs() << "Bit width  = " << bits << "\n";
          llvm::errs() << "Byte width = " << bytes << "\n";

          /* Create a copy of the argument */
          IRBuilder<> builder(&i);
          AllocaInst* ai = builder.CreateAlloca(t);
          builder.CreateMemCpy(ai, ai->getAlignment(), arg, ai->getAlignment(),
                               bytes);
/*
          auto &ctx = m.getContext();
          auto memcpyFun = m.getOrInsertFunction("memcpy",
          Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx),
          Type::getInt8PtrTy(ctx), Type::getInt32Ty(ctx) KLEE_LLVM_GOIF_TERMINATOR);
          assert(memcpyFun);
          auto ai_CharPtr = builder.CreateBitCast(ai, Type::getInt8PtrTy(ctx));
          auto arg_CharPtr = builder.CreateBitCast(arg, Type::getInt8PtrTy(ctx));
          auto size_Int = ConstantInt::get(Type::getInt32Ty(ctx), bytes);
          Value* args[] = { ai_CharPtr, arg_CharPtr, size_Int };
          builder.CreateCall(memcpyFun, args);
*/

/*
          auto &ctx = m.getContext();
          Value* args[] = { ConstantInt::get(Type::getInt64Ty(ctx), 0) };
          auto gep = builder.CreateGEP(t, ai, args );
          cs.setArgument(k, gep);
*/
          cs.setArgument(k, ai);

          changed = true;
        }
      }
    }
  }

  return changed;
}

}
