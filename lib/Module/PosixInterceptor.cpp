//===-- PosixInterceptorPass.cpp --------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Passes.h"

#include "klee/Support/ErrorHandling.h"
#include "klee/Support/OptionCategories.h"

#if LLVM_VERSION_CODE < LLVM_VERSION(8, 0)
#include "llvm/IR/CallSite.h"
#endif
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>

using namespace llvm;

static const char *posixFunctions[] = {
    "read",
};

#define NoPosixFunctions (sizeof(posixFunctions) / sizeof(posixFunctions[0]))

namespace klee {

bool PosixInterceptorPass::runOnModule(Module &M) {
  bool modified = false;

  // POSIX functions/system calls to be replaced with klee_* functions
  std::set<std::string> posixFuns(posixFunctions,
                                  posixFunctions + NoPosixFunctions);

  // maps functions to be replaced to their klee_* replacement functions, e.g.,
  // read -> klee_read
  std::map<std::string, GlobalValue *> replacementFuns;

  for (const auto &fun : posixFuns) {
    std::string replacementName = "klee_" + fun;

    // double-check that the replacement function exists
    GlobalValue *replacementValue = M.getNamedValue(replacementName);
    if (!isFunctionOrGlobalFunctionAlias(replacementValue))
      klee_error(
          "POSIX interceptor: replacement function %s() could not be found",
          replacementName.c_str());

    replacementFuns[fun] = replacementValue;
  }

  /* Gather all calls to be replaced */
  std::vector<llvm::Instruction *> callsToReplace;
  for (auto &f : M.getFunctionList()) {
    if (!f.getName().startswith("klee_"))
      for (auto &bb : f) {
        for (auto &i : bb) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
          if (auto cb = dyn_cast<CallBase>(&i)) {
            Function *fun = getCalledFunction(cb);
#else
          CallSite cs(&i);
          if (cs) {
            Function *fun = getCalledFunction(&cs);
#endif
            if (fun && posixFuns.count(fun->getName())) {
              klee_message("Will replace %s\n", fun->getName().data());
              callsToReplace.push_back(&i);
            }
          }
        }
      }
  }

  /* Replace all the calls in callsToReplace */
  for (auto *i : callsToReplace) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
    CallBase *cb = dyn_cast<CallBase>(i);
    assert(cb);
    Function *fun = getCalledFunction(cb);
#else
    CallSite cb(i);
    assert(cb);
    Function *fun = getCalledFunction(&cb);
#endif
    GlobalValue *replacement = replacementFuns[fun->getName()];
    assert(replacement);

    auto replacementFun = M.getOrInsertFunction(replacement->getName(),
                                                getFunctionType(replacement));

#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
    SmallVector<Value *, 8> Args(cb->arg_begin(), cb->arg_end());
#else
    SmallVector<Value *, 8> Args(cb.arg_begin(), cb.arg_end());
#endif
    llvm::errs() << "#args = " << Args.size() << "\n";

    if (!checkType(fun, replacement))
      klee_error("%s() is called with the incorrect type.  Most likely you "
                 "need to include the correct header files.\n",
                 fun->getName().data());

    CallInst *NewCI = CallInst::Create(replacementFun, Args);

    if (!cb->use_empty())
      cb->replaceAllUsesWith(NewCI);
    ReplaceInstWithInst(i, NewCI);

    modified = true;
    klee_message("POSIX interceptor: replaced @%s with @%s",
                 fun->getName().data(), replacement->getName().data());
  }

  return modified;
}

FunctionType *PosixInterceptorPass::getFunctionType(const GlobalValue *gv) {
  Type *type = gv->getType();
  while (type->isPointerTy()) {
    const PointerType *ptr = cast<PointerType>(type);
    type = ptr->getElementType();
  }
  return cast<FunctionType>(type);
}

#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
Function *PosixInterceptorPass::getCalledFunction(CallBase *cb) {
#else
Function *PosixInterceptorPass::getCalledFunction(CallSite *cb) {
#endif
  Value *Callee = cb->getCalledValue()->stripPointerCasts();
  if (auto f = dyn_cast<Function>(Callee))
    return f;
  return nullptr;
}

bool PosixInterceptorPass::checkType(const GlobalValue *match,
                                     const GlobalValue *replacement) {
  const FunctionType *MFT = getFunctionType(match);
  const FunctionType *RFT = getFunctionType(replacement);

  if (MFT == nullptr || RFT == nullptr)
    return false;

  if (MFT->getReturnType() != RFT->getReturnType())
    return false;

  if (MFT->isVarArg() != RFT->isVarArg())
    return false;

  if (MFT->getNumParams() != RFT->getNumParams())
    return false;

  std::size_t numParams = MFT->getNumParams();
  for (std::size_t i = 0; i < numParams; ++i)
    if (MFT->getParamType(i) != RFT->getParamType(i))
      return false;
  
  return true;
}

bool PosixInterceptorPass::isFunctionOrGlobalFunctionAlias(
    const GlobalValue *gv) {
  if (gv == nullptr)
    return false;

  if (isa<Function>(gv))
    return true;

  if (const auto *ga = dyn_cast<GlobalAlias>(gv)) {
    const auto *aliasee = dyn_cast<GlobalValue>(ga->getAliasee());
    if (!aliasee) {
      // check if GlobalAlias is alias bitcast
      const auto *cexpr = dyn_cast<ConstantExpr>(ga->getAliasee());
      if (!cexpr || !cexpr->isCast())
        return false;
      aliasee = dyn_cast<GlobalValue>(cexpr->getOperand(0));
    }
    return isFunctionOrGlobalFunctionAlias(aliasee);
  }

  return false;
}

char PosixInterceptorPass::ID = 0;

} // namespace klee
