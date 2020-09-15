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

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
    assert((M.ifunc_size() == 0) && "Unexpected ifunc");
#endif

#define NELEMS(array) (sizeof(array)/sizeof(array[0]))
    std::set<std::string> posixFuns(posixFunctions,
                                    posixFunctions + NELEMS(posixFunctions));

    std::set<std::string> replacementNames;
    std::map<std::string, GlobalValue*> replacementValues;

    for (const auto &fun : posixFuns) {
            
      std::string replacementName = "klee_" + fun;
      replacementNames.insert(replacementName);

      // double-check that the replacement function exists
      GlobalValue *replacementValue = M.getNamedValue(replacementName);
      if (!isFunctionOrGlobalFunctionAlias(replacementValue))
        klee_error(
            "POSIX interceptor: replacement function @%s could not be found",
            replacementName.c_str());

      //llvm::errs() << "";
      replacementValues[fun] = replacementValue;
    }

    klee_message("Here\n");
    std::vector<llvm::CallBase*> callsToReplace;

    for (auto &f : M.getFunctionList()) {
      for (auto &bb : f) {
        if (!f.getName().startswith("klee_"))
          for (auto &i: bb) {
            if (auto *cb = dyn_cast<CallBase>(&i)) {
              Function* match = cb->getCalledFunction();
              if (match && posixFuns.count(match->getName())) {
                llvm::errs() << "Processing CallBase: " << i << "\n";
                klee_message("Trying to replace %s\n", match->getName().data());
                callsToReplace.push_back(cb);
              }
            }
          }
      }
    }
    
    for (auto *cb : callsToReplace) {
      Function* match = cb->getCalledFunction();
      GlobalValue *replacementValue = replacementValues[cb->getCalledFunction()->getName()];
      assert(replacementValue);

      std::string replacementName = "klee_" + match->getName().str();
      auto replacementFunction =
        M.getOrInsertFunction(replacementName, match->getFunctionType());
      llvm::errs() << "Function type: " << *replacementFunction.getFunctionType() << "\n";
      
      //CallSite CS(Call);
      SmallVector<Value *, 8> Args(cb->arg_begin(), cb->arg_end());
      llvm::errs() << "args = " << Args.size() << "\n";
      CallInst *NewCI = CallInst::Create(replacementFunction, Args);
      //NewCI->setCallingConv(replacementFunction.getCallingConv());
      if (!cb->use_empty())
        cb->replaceAllUsesWith(NewCI);
      ReplaceInstWithInst(cb, NewCI);
      
#if 0
      std::vector<llvm::Value*> args;
      for (auto a = match->arg_begin(); a != match->arg_end(); a++)
        args.push_back(a);
      
      llvm::IRBuilder<> Builder(&i);
      Builder.CreateCall(replacementFunction, args);
      //llvm::ReplaceInstWithInst(i, 
      
      //if (tryToReplace(match, replacementValue)) {
#endif
      modified = true;
      klee_message("POSIX interceptor: replaced @%s with @%s",
                   match->getName().data(), replacementValue->getName().data());
      //continue;
      //}
    }
    
    return modified;
  }

  const FunctionType *PosixInterceptorPass::getFunctionType(
      const GlobalValue *gv) {
    const Type *type = gv->getType();
    while (type->isPointerTy()) {
      const PointerType *ptr = cast<PointerType>(type);
      type = ptr->getElementType();
    }
    return cast<FunctionType>(type);
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
