//===-- PosixInterceptorPass.cpp -----------------------------------*- C++
//-*-===//
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

#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <map>
#include <set>

using namespace llvm;

static const char *posixFunctions[] = {
  //"open",
  "read",
  //  "write",
};


namespace klee {

  bool PosixInterceptorPass::runOnModule(Module & M) {
    bool modified = false;

#define NELEMS(array) (sizeof(array) / sizeof(array[0]))

    // POSIX functions/system calls to be replaced with klee_* functions
    std::set<std::string> posixFuns(posixFunctions,
                                    posixFunctions + NELEMS(posixFunctions));

    // maps function to be replaced to their klee_* replacement functions, e.g., read -> klee_read
    std::map<std::string, GlobalValue*> replacementFuns;

    for (const auto &fun : posixFuns) {
      std::string replacementName = "klee_" + fun;

      // double-check that the replacement function exists
      GlobalValue *replacementValue = M.getNamedValue(replacementName);
      if (!isFunctionOrGlobalFunctionAlias(replacementValue))
        klee_error(
            "POSIX interceptor: replacement function @%s could not be found",
            replacementName.c_str());

      replacementFuns[fun] = replacementValue;
    }

    std::vector<llvm::CallBase*> callsToReplace;

    for (auto &f : M.getFunctionList()) {
      if (!f.getName().startswith("klee_"))
        for (auto &bb : f) {
          for (auto &i: bb) {
            if (auto *cb = dyn_cast<CallBase>(&i)) {
              Function* fun = getCalledFunction(cb);
              if (fun && posixFuns.count(fun->getName())) {
                llvm::errs() << "Processing CallBase: " << i << "\n";
                klee_message("Will replace %s\n", fun->getName().data());
                callsToReplace.push_back(cb);
              }
            }
          }
      }
    }
    
    for (auto *cb : callsToReplace) {
      Function* fun = getCalledFunction(cb);
      GlobalValue *replacement = replacementFuns[fun->getName()];
      assert(replacement);

      auto replacementFun =
        M.getOrInsertFunction(replacement->getName(), getFunctionType(replacement));
      llvm::errs() << "Function type: " << *replacementFun.getFunctionType() << "\n";
      
      SmallVector<Value *, 8> Args(cb->arg_begin(), cb->arg_end());
      llvm::errs() << "#args = " << Args.size() << "\n";

      if (!checkType(fun, replacement))
        klee_error("%s() is called with the incorrect type.  Most likely you need to include the correct header files.\n", fun->getName().data());
     
      CallInst* NewCI = CallInst::Create(replacementFun, Args);
      
      if (!cb->use_empty())
        cb->replaceAllUsesWith(NewCI);
      ReplaceInstWithInst(cb, NewCI);
      
      modified = true;
      klee_message("POSIX interceptor: replaced @%s with @%s",
                   fun->getName().data(), replacement->getName().data());
    }
    
    return modified;
  }

  FunctionType* PosixInterceptorPass::getFunctionType(
      const GlobalValue *gv) {
    Type *type = gv->getType();
    while (type->isPointerTy()) {
      const PointerType *ptr = cast<PointerType>(type);
      type = ptr->getElementType();
    }
    return cast<FunctionType>(type);
  }

  Function* PosixInterceptorPass::getCalledFunction(CallBase *cb) {
    Value *Callee = cb->getCalledValue()->stripPointerCasts();
    if (auto f = dyn_cast<Function>(Callee))
      return f;
    return nullptr;
  }
  
  bool PosixInterceptorPass::checkType(const GlobalValue *match,
                                       const GlobalValue *replacement) {
    llvm::errs() << "In checkType\n";
    const FunctionType *MFT = getFunctionType(match);
    const FunctionType *RFT = getFunctionType(replacement);
    if (MFT == nullptr) {
      klee_warning("MFT is null\n");
      return false;
    }

    if (RFT == nullptr)	{
      klee_warning("RFT is null\n");
      return false;
    }
    
    assert(MFT != nullptr && RFT != nullptr);

    if (MFT->getReturnType() != RFT->getReturnType()) {
      klee_warning("function-alias: @%s could not be replaced with @%s: "
                   "return type differs",
                   match->getName().str().c_str(),
                   replacement->getName().str().c_str());
      return false;
    }

    if (MFT->isVarArg() != RFT->isVarArg()) {
      klee_warning("function-alias: @%s could not be replaced with @%s: "
                   "one has varargs while the other does not",
                   match->getName().str().c_str(),
                   replacement->getName().str().c_str());
      return false;
    }

    if (MFT->getNumParams() != RFT->getNumParams()) {
      klee_warning("function-alias: @%s could not be replaced with @%s: "
                   "number of parameters differs",
                   match->getName().str().c_str(),
                   replacement->getName().str().c_str());
      return false;
    }

    std::size_t numParams = MFT->getNumParams();
    for (std::size_t i = 0; i < numParams; ++i) {
      if (MFT->getParamType(i) != RFT->getParamType(i)) {
        klee_warning("function-alias: @%s could not be replaced with @%s: "
                     "parameter types differ",
                     match->getName().str().c_str(),
                     replacement->getName().str().c_str());
        return false;
      }
    }
    return true;
  }

  bool PosixInterceptorPass::tryToReplace(GlobalValue * match,
                                          GlobalValue * replacement) {
    if (!checkType(match, replacement))
      return false;
    llvm::errs() << "in tryToReplace\n";


    GlobalAlias *alias = GlobalAlias::create("", replacement);
    match->replaceUsesOfWith(match, alias);
    alias->takeName(match);
    //match->eraseFromParent();

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
