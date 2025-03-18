#include "llvm/Transforms/IPO/WelComeToLLVMMSVC.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/IRPrinter/IRAutoGeneratorPass.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <fstream>
#include <iostream>

using namespace llvm;

// Implementation of the run() function for the WelcomeToLLVMMSVCPass
// class
PreservedAnalyses WelcomeToLLVMMSVCPass::run(Module &M,
                                             ModuleAnalysisManager &AM) {

  bool Changed = false;

  if (Enable) {
    if (!M.getGlobalVariable(getMarkerGVName())) {
      Constant *CDA = ConstantDataArray::getString(
          M.getContext(), "Welcome to llvm-msvc!", false);
      GlobalVariable *GV = new GlobalVariable(M, CDA->getType(), true,
                                              GlobalValue::LinkOnceODRLinkage,
                                              CDA, getMarkerGVName());
#if !defined(__APPLE__)
      GV->setComdat(M.getOrInsertComdat(GV->getName()));
#endif
      GV->setVolatileAndAppendToUsed();
      Changed = true;
    }
  }

  /**
   * This statement returns a PreservedAnalyses object based on whether there
   * were any changes during the pass execution. If `Changed` is true,
   * indicating that changes occurred, it returns
   * `llvm::PreservedAnalyses::none()`. This means that some analyses may not be
   * preserved and should be re-run as the pass has potentially invalidated
   * their results. If `Changed` is false, indicating that no changes occurred,
   * it returns `llvm::PreservedAnalyses::all()`. This means that all analyses
   * are preserved and their results are still valid after the pass execution.
   */
  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}
