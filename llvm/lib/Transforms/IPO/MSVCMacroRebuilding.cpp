#include "llvm/Transforms/IPO/MSVCMacroRebuilding.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

using namespace llvm;

/// Replace all occurrences of MarkerPrefix + "(Line:" + digits + ")" in Input
/// with Replacement. This is equivalent to std::regex_replace but ~100x faster.
static std::string replaceMarkerPattern(const std::string &Input,
                                        StringRef Prefix,
                                        const std::string &Replacement) {
  std::string Result;
  Result.reserve(Input.size());
  size_t Pos = 0;
  const size_t PrefixLen = Prefix.size();
  constexpr StringLiteral LineTag("(Line:");

  while (Pos < Input.size()) {
    size_t FoundPos = Input.find(Prefix.data(), Pos, PrefixLen);
    if (FoundPos == std::string::npos) {
      Result.append(Input, Pos, Input.size() - Pos);
      break;
    }

    Result.append(Input, Pos, FoundPos - Pos);

    size_t AfterPrefix = FoundPos + PrefixLen;
    if (AfterPrefix + LineTag.size() <= Input.size() &&
        Input.compare(AfterPrefix, LineTag.size(), LineTag.data()) == 0) {
      size_t DigitStart = AfterPrefix + LineTag.size();
      size_t DigitEnd = DigitStart;
      while (DigitEnd < Input.size() && Input[DigitEnd] >= '0' &&
             Input[DigitEnd] <= '9')
        ++DigitEnd;

      if (DigitEnd > DigitStart && DigitEnd < Input.size() &&
          Input[DigitEnd] == ')') {
        Result.append(Replacement);
        Pos = DigitEnd + 1;
        continue;
      }
    }

    Result += Input[FoundPos];
    Pos = FoundPos + 1;
  }

  return Result;
}

PreservedAnalyses MSVCMacroRebuildingPass::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  bool Changed = false;

  StringRef MarkerName = MSVCMacroRebuildingPass::get__FUNCTION__MarkerName();
  std::string Prefix__FUNCTION__ =
      (Twine(MarkerName) + M.getSourceFileName()).str();

  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer())
      continue;

    ConstantDataArray *CDA = dyn_cast<ConstantDataArray>(GV.getInitializer());
    if (!CDA || !CDA->isString())
      continue;

    std::string OriginalStr = CDA->getAsCString().str();
    if (OriginalStr.length() < 20)
      continue;

    if (OriginalStr.find(MarkerName.data(), 0, MarkerName.size()) ==
        std::string::npos)
      continue;

    SmallVector<std::pair<Instruction *, unsigned int>, 32> Users;
    for (auto UserIt = GV.users().begin(); UserIt != GV.users().end();
         ++UserIt) {
      if (Instruction *I = dyn_cast<Instruction>(*UserIt)) {
        if (!I->getParent())
          continue;
        Function *F = I->getParent()->getParent();
        if (!F)
          continue;
        for (unsigned int OperandIndex = 0; OperandIndex < I->getNumOperands();
             OperandIndex++) {
          if (I->getOperand(OperandIndex) == &GV) {
            Users.push_back({I, OperandIndex});
            break;
          }
        }
      }
    }

    for (auto &Pair : Users) {
      Instruction *I = Pair.first;
      unsigned int OperandIndex = Pair.second;
      Function &F = *I->getParent()->getParent();

      std::string NewStr = replaceMarkerPattern(
          OriginalStr, Prefix__FUNCTION__,
          demangleGetFunctionName(F.getName()));

      if (NewStr.empty())
        continue;

      std::string NewGVName = (Twine(MarkerName) + NewStr).str();
      GlobalVariable *NewGV = M.getNamedGlobal(NewGVName);
      if (!NewGV) {
        Constant *NewCDA = ConstantDataArray::getString(M.getContext(), NewStr);
        NewGV =
            new GlobalVariable(M, NewCDA->getType(), true,
                               GlobalValue::PrivateLinkage, NewCDA, NewGVName);
      }
      I->setOperand(OperandIndex, NewGV);
      Changed = true;
    }
  }

  return (Changed ? llvm::PreservedAnalyses::none()
                  : llvm::PreservedAnalyses::all());
}
