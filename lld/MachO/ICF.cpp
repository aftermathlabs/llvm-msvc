//===- ICF.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ICF.h"
#include "ConcatOutputSection.h"
#include "Config.h"
#include "InputSection.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "UnwindInfoSection.h"

#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/xxhash.h"

#include <atomic>

using namespace llvm;
using namespace lld;
using namespace lld::macho;

static constexpr bool verboseDiagnostics = false;

class ICF {
public:
  ICF(std::vector<ConcatInputSection *> &inputs);
  void run();

  using EqualsFn = bool (ICF::*)(const ConcatInputSection *,
                                 const ConcatInputSection *);
  void segregate(size_t begin, size_t end, EqualsFn);
  size_t findBoundary(size_t begin, size_t end);
  void forEachClassRange(size_t begin, size_t end,
                         llvm::function_ref<void(size_t, size_t)> func);
  void forEachClass(llvm::function_ref<void(size_t, size_t)> func);

  bool equalsConstant(const ConcatInputSection *ia,
                      const ConcatInputSection *ib);
  bool equalsVariable(const ConcatInputSection *ia,
                      const ConcatInputSection *ib);

  std::vector<ConcatInputSection *> icfInputs;

  unsigned icfPass = 0;
  std::atomic<bool> icfRepeat{false};
  std::atomic<uint64_t> equalsConstantCount{0};
  std::atomic<uint64_t> equalsVariableCount{0};
};

ICF::ICF(std::vector<ConcatInputSection *> &inputs) {
  icfInputs.assign(inputs.begin(), inputs.end());
}

// ICF = Identical Code Folding
//
// We only fold __TEXT,__text, so this is really "code" folding, and not
// "COMDAT" folding. String and scalar constant literals are deduplicated
// elsewhere.
//
// Summary of segments & sections:
//
// The __TEXT segment is readonly at the MMU. Some sections are already
// deduplicated elsewhere (__TEXT,__cstring & __TEXT,__literal*) and some are
// synthetic and inherently free of duplicates (__TEXT,__stubs &
// __TEXT,__unwind_info). Note that we don't yet run ICF on __TEXT,__const,
// because doing so induces many test failures.
//
// The __LINKEDIT segment is readonly at the MMU, yet entirely synthetic, and
// thus ineligible for ICF.
//
// The __DATA_CONST segment is read/write at the MMU, but is logically const to
// the application after dyld applies fixups to pointer data. We currently
// fold only the __DATA_CONST,__cfstring section.
//
// The __DATA segment is read/write at the MMU, and as application-writeable
// data, none of its sections are eligible for ICF.
//
// Please see the large block comment in lld/ELF/ICF.cpp for an explanation
// of the segregation algorithm.
//
// FIXME(gkm): implement keep-unique attributes
// FIXME(gkm): implement address-significance tables for MachO object files

// Compare "non-moving" parts of two ConcatInputSections, namely everything
// except references to other ConcatInputSections.
bool ICF::equalsConstant(const ConcatInputSection *ia,
                         const ConcatInputSection *ib) {
  if (verboseDiagnostics)
    ++equalsConstantCount;
  // We can only fold within the same OutputSection.
  if (ia->parent != ib->parent)
    return false;
  if (ia->data.size() != ib->data.size())
    return false;
  if (ia->data != ib->data)
    return false;
  if (ia->relocs.size() != ib->relocs.size())
    return false;
  auto f = [](const Reloc &ra, const Reloc &rb) {
    if (ra.type != rb.type)
      return false;
    if (ra.pcrel != rb.pcrel)
      return false;
    if (ra.length != rb.length)
      return false;
    if (ra.offset != rb.offset)
      return false;
    if (ra.referent.is<Symbol *>() != rb.referent.is<Symbol *>())
      return false;

    InputSection *isecA, *isecB;

    uint64_t valueA = 0;
    uint64_t valueB = 0;
    if (ra.referent.is<Symbol *>()) {
      const auto *sa = ra.referent.get<Symbol *>();
      const auto *sb = rb.referent.get<Symbol *>();
      if (sa->kind() != sb->kind())
        return false;
      // ICF runs before Undefineds are treated (and potentially converted into
      // DylibSymbols).
      if (isa<DylibSymbol>(sa) || isa<Undefined>(sa))
        return sa == sb && ra.addend == rb.addend;
      assert(isa<Defined>(sa));
      const auto *da = cast<Defined>(sa);
      const auto *db = cast<Defined>(sb);
      if (!da->isec || !db->isec) {
        assert(da->isAbsolute() && db->isAbsolute());
        return da->value + ra.addend == db->value + rb.addend;
      }
      isecA = da->isec;
      valueA = da->value;
      isecB = db->isec;
      valueB = db->value;
    } else {
      isecA = ra.referent.get<InputSection *>();
      isecB = rb.referent.get<InputSection *>();
    }

    if (isecA->parent != isecB->parent)
      return false;
    // Sections with identical parents should be of the same kind.
    assert(isecA->kind() == isecB->kind());
    // We will compare ConcatInputSection contents in equalsVariable.
    if (isa<ConcatInputSection>(isecA))
      return ra.addend == rb.addend;
    // Else we have two literal sections. References to them are equal iff their
    // offsets in the output section are equal.
    if (ra.referent.is<Symbol *>())
      // For symbol relocs, we compare the contents at the symbol address. We
      // don't do `getOffset(value + addend)` because value + addend may not be
      // a valid offset in the literal section.
      return isecA->getOffset(valueA) == isecB->getOffset(valueB) &&
             ra.addend == rb.addend;
    else {
      assert(valueA == 0 && valueB == 0);
      // For section relocs, we compare the content at the section offset.
      return isecA->getOffset(ra.addend) == isecB->getOffset(rb.addend);
    }
  };
  return std::equal(ia->relocs.begin(), ia->relocs.end(), ib->relocs.begin(),
                    f);
}

// Compare the "moving" parts of two ConcatInputSections -- i.e. everything not
// handled by equalsConstant().
bool ICF::equalsVariable(const ConcatInputSection *ia,
                         const ConcatInputSection *ib) {
  if (verboseDiagnostics)
    ++equalsVariableCount;
  assert(ia->relocs.size() == ib->relocs.size());
  auto f = [this](const Reloc &ra, const Reloc &rb) {
    // We already filtered out mismatching values/addends in equalsConstant.
    if (ra.referent == rb.referent)
      return true;
    const ConcatInputSection *isecA, *isecB;
    if (ra.referent.is<Symbol *>()) {
      // Matching DylibSymbols are already filtered out by the
      // identical-referent check above. Non-matching DylibSymbols were filtered
      // out in equalsConstant(). So we can safely cast to Defined here.
      const auto *da = cast<Defined>(ra.referent.get<Symbol *>());
      const auto *db = cast<Defined>(rb.referent.get<Symbol *>());
      if (da->isAbsolute())
        return true;
      isecA = dyn_cast<ConcatInputSection>(da->isec);
      if (!isecA)
        return true; // literal sections were checked in equalsConstant.
      isecB = cast<ConcatInputSection>(db->isec);
    } else {
      const auto *sa = ra.referent.get<InputSection *>();
      const auto *sb = rb.referent.get<InputSection *>();
      isecA = dyn_cast<ConcatInputSection>(sa);
      if (!isecA)
        return true;
      isecB = cast<ConcatInputSection>(sb);
    }
    return isecA->icfEqClass[icfPass % 2] == isecB->icfEqClass[icfPass % 2];
  };
  if (!std::equal(ia->relocs.begin(), ia->relocs.end(), ib->relocs.begin(), f))
    return false;

  // If there are symbols with associated unwind info, check that the unwind
  // info matches. For simplicity, we only handle the case where there are only
  // symbols at offset zero within the section (which is typically the case with
  // .subsections_via_symbols.)
  auto hasUnwind = [](Defined *d) { return d->unwindEntry != nullptr; };
  const auto *itA = llvm::find_if(ia->symbols, hasUnwind);
  const auto *itB = llvm::find_if(ib->symbols, hasUnwind);
  if (itA == ia->symbols.end())
    return itB == ib->symbols.end();
  if (itB == ib->symbols.end())
    return false;
  const Defined *da = *itA;
  const Defined *db = *itB;
  if (da->unwindEntry->icfEqClass[icfPass % 2] !=
          db->unwindEntry->icfEqClass[icfPass % 2] ||
      da->value != 0 || db->value != 0)
    return false;
  auto isZero = [](Defined *d) { return d->value == 0; };
  return std::find_if_not(std::next(itA), ia->symbols.end(), isZero) ==
             ia->symbols.end() &&
         std::find_if_not(std::next(itB), ib->symbols.end(), isZero) ==
             ib->symbols.end();
}

size_t ICF::findBoundary(size_t begin, size_t end) {
  uint64_t ec = icfInputs[begin]->icfEqClass[icfPass % 2];
  size_t lo = begin + 1, hi = end;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (icfInputs[mid]->icfEqClass[icfPass % 2] == ec)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

// Invoke FUNC on subranges with matching equivalence class
void ICF::forEachClassRange(size_t begin, size_t end,
                            llvm::function_ref<void(size_t, size_t)> func) {
  while (begin < end) {
    size_t mid = findBoundary(begin, end);
    func(begin, mid);
    begin = mid;
  }
}

// Split icfInputs into shards, then parallelize invocation of FUNC on subranges
// with matching equivalence class
void ICF::forEachClass(llvm::function_ref<void(size_t, size_t)> func) {
  // Only use threads when the benefits outweigh the overhead.
  const size_t threadingThreshold = 1024;
  if (parallel::strategy.ThreadsRequested == 1 ||
      icfInputs.size() < threadingThreshold) {
    forEachClassRange(0, icfInputs.size(), func);
    ++icfPass;
    return;
  }

  // Shard into non-overlapping intervals, and call FUNC in parallel.  The
  // sharding must be completed before any calls to FUNC are made so that FUNC
  // can modify the InputSection in its shard without causing data races.
  size_t shards =
      std::max<size_t>(1, parallel::strategy.compute_thread_count() * 4);
  shards = std::min<size_t>(shards, 256);
  shards = std::min<size_t>(shards, icfInputs.size());
  if (shards <= 1) {
    forEachClassRange(0, icfInputs.size(), func);
    ++icfPass;
    return;
  }

  size_t step = icfInputs.size() / shards;
  SmallVector<size_t, 257> boundaries(shards + 1);
  boundaries[0] = 0;
  boundaries[shards] = icfInputs.size();
  parallelFor(1, shards, [&](size_t i) {
    boundaries[i] = findBoundary((i - 1) * step, icfInputs.size());
  });
  parallelFor(1, shards + 1, [&](size_t i) {
    if (boundaries[i - 1] < boundaries[i]) {
      forEachClassRange(boundaries[i - 1], boundaries[i], func);
    }
  });
  ++icfPass;
}

void ICF::run() {

  size_t hashPass = 0;
  for (;; ++hashPass) {
    const size_t currSlot = hashPass % 2;
    const size_t nextSlot = (hashPass + 1) % 2;
    auto hashOne = [&](ConcatInputSection *isec) {
      uint32_t hash = isec->icfEqClass[currSlot];
      for (const Reloc &r : isec->relocs) {
        if (auto *sym = r.referent.dyn_cast<Symbol *>()) {
          if (auto *defined = dyn_cast<Defined>(sym)) {
            if (defined->isec) {
              uint32_t refHash;
              if (auto *referentIsec =
                      dyn_cast<ConcatInputSection>(defined->isec))
                refHash = defined->value + referentIsec->icfEqClass[currSlot] +
                          static_cast<uint32_t>(r.addend);
              else
                refHash = defined->isec->kind() +
                          defined->isec->getOffset(defined->value) +
                          static_cast<uint32_t>(r.addend);
              hash ^= refHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            } else {
              uint32_t refHash =
                  defined->value + static_cast<uint32_t>(r.addend);
              hash ^= refHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
          } else {
            assert(isa<Undefined>(sym) || isa<DylibSymbol>(sym));
            uint32_t refHash =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sym) >> 4);
            hash ^= refHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
          }
        } else {
          auto *isecRef = r.referent.get<InputSection *>();
          if (auto *concatRef = dyn_cast<ConcatInputSection>(isecRef)) {
            uint32_t refHash = concatRef->icfEqClass[currSlot] +
                               static_cast<uint32_t>(r.addend);
            hash ^= refHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
          }
        }
      }
      uint32_t nextHash = hash | (1U << 31);
      bool changed = nextHash != isec->icfEqClass[currSlot];
      isec->icfEqClass[nextSlot] = nextHash;
      return changed;
    };

    bool changedAny = false;
    static constexpr size_t kParallelHashPassThreshold = 1024;
    if (parallel::strategy.ThreadsRequested != 1 &&
        icfInputs.size() >= kParallelHashPassThreshold) {
      size_t numChunks =
          std::max<size_t>(1, parallel::strategy.compute_thread_count() * 4);
      numChunks = std::min<size_t>(numChunks, icfInputs.size());
      std::vector<uint8_t> changedByChunk(numChunks, 0);
      parallelFor(0, numChunks, [&](size_t chunkIdx) {
        size_t begin = chunkIdx * icfInputs.size() / numChunks;
        size_t end = (chunkIdx + 1) * icfInputs.size() / numChunks;
        bool localChanged = false;
        for (size_t i = begin; i < end; ++i)
          localChanged |= hashOne(icfInputs[i]);
        changedByChunk[chunkIdx] = localChanged ? 1 : 0;
      });
      changedAny = llvm::any_of(changedByChunk, [](uint8_t v) { return v; });
    } else {
      for (ConcatInputSection *isec : icfInputs)
        changedAny |= hashOne(isec);
    }

    if (hashPass >= 1 && !changedAny)
      break;

    if (hashPass >= 7)
      break;
  }
  // forEachClass()/findBoundary() read icfEqClass[icfPass % 2].
  // Keep icfPass consistent with whichever slot holds the latest hashes.
  icfPass = hashPass + 1;
  const size_t hashSlot = icfPass % 2;

  parallelSort(
      icfInputs, [hashSlot](const ConcatInputSection *a,
                            const ConcatInputSection *b) {
        return a->icfEqClass[hashSlot] < b->icfEqClass[hashSlot];
      });
  forEachClass([&](size_t begin, size_t end) {
    if (end - begin > 1)
      segregate(begin, end, &ICF::equalsConstant);
    else
      icfInputs[begin]->icfEqClass[(icfPass + 1) % 2] = end;
  });

  do {
    icfRepeat.store(false, std::memory_order_relaxed);
    forEachClass([&](size_t begin, size_t end) {
      if (end - begin > 1)
        segregate(begin, end, &ICF::equalsVariable);
      else
        icfInputs[begin]->icfEqClass[(icfPass + 1) % 2] = end;
    });
  } while (icfRepeat.load(std::memory_order_relaxed));
  log("ICF needed " + Twine(icfPass) + " iterations");
  if (verboseDiagnostics) {
    log("equalsConstant() called " + Twine(equalsConstantCount) + " times");
    log("equalsVariable() called " + Twine(equalsVariableCount) + " times");
  }

  // Fold sections within equivalence classes
  forEachClass([&](size_t begin, size_t end) {
    if (end - begin < 2)
      return;
    ConcatInputSection *beginIsec = icfInputs[begin];
    for (size_t i = begin + 1; i < end; ++i)
      beginIsec->foldIdentical(icfInputs[i]);
  });
}

// Split an equivalence class into smaller classes.
void ICF::segregate(size_t begin, size_t end, EqualsFn equals) {
  while (begin < end) {
    // Divide [begin, end) into two. Let mid be the start index of the
    // second group.
    auto bound = std::stable_partition(
        icfInputs.begin() + begin + 1, icfInputs.begin() + end,
        [&](ConcatInputSection *isec) {
          return (this->*equals)(icfInputs[begin], isec);
        });
    size_t mid = bound - icfInputs.begin();

    // Split [begin, end) into [begin, mid) and [mid, end). We use mid as an
    // equivalence class ID because every group ends with a unique index.
    for (size_t i = begin; i < mid; ++i)
      icfInputs[i]->icfEqClass[(icfPass + 1) % 2] = mid;

    // If we created a group, we need to iterate the main loop again.
    if (mid != end)
      icfRepeat.store(true, std::memory_order_relaxed);

    begin = mid;
  }
}

void macho::markSymAsAddrSig(Symbol *s) {
  if (auto *d = dyn_cast_or_null<Defined>(s))
    if (d->isec)
      d->isec->keepUnique = true;
}

void macho::markAddrSigSymbols() {
  TimeTraceScope timeScope("Mark addrsig symbols");
  for (InputFile *file : inputFiles) {
    ObjFile *obj = dyn_cast<ObjFile>(file);
    if (!obj)
      continue;

    Section *addrSigSection = obj->addrSigSection;
    if (!addrSigSection)
      continue;
    assert(addrSigSection->subsections.size() == 1);

    const InputSection *isec = addrSigSection->subsections[0].isec;

    for (const Reloc &r : isec->relocs) {
      if (auto *sym = r.referent.dyn_cast<Symbol *>())
        markSymAsAddrSig(sym);
      else
        error(toString(isec) + ": unexpected section relocation");
    }
  }
}

void macho::foldIdenticalSections(bool onlyCfStrings) {
  TimeTraceScope timeScope("Fold Identical Code Sections");
  // The ICF equivalence-class segregation algorithm relies on pre-computed
  // hashes of InputSection::data for the ConcatOutputSection::inputs and all
  // sections referenced by their relocs. We could recursively traverse the
  // relocs to find every referenced InputSection, but that precludes easy
  // parallelization. Therefore, we hash every InputSection here where we have
  // them all accessible as simple vectors.

  // If an InputSection is ineligible for ICF, we give it a unique ID to force
  // it into an unfoldable singleton equivalence class.  Begin the unique-ID
  // space at inputSections.size(), so that it will never intersect with
  // equivalence-class IDs which begin at 0. Since hashes & unique IDs never
  // coexist with equivalence-class IDs, this is not necessary, but might help
  // someone keep the numbers straight in case we ever need to debug the
  // ICF::segregate()
  std::vector<ConcatInputSection *> foldable;
  uint64_t icfUniqueID = inputSections.size();
  for (ConcatInputSection *isec : inputSections) {
    bool isFoldableWithAddendsRemoved = isCfStringSection(isec) ||
                                        isClassRefsSection(isec) ||
                                        isSelRefsSection(isec);
    // NOTE: __objc_selrefs is typically marked as no_dead_strip by MC, but we
    // can still fold it.
    bool hasFoldableFlags = (isSelRefsSection(isec) ||
                             sectionType(isec->getFlags()) == MachO::S_REGULAR);
    // FIXME: consider non-code __text sections as foldable?
    bool isFoldable = (!onlyCfStrings || isCfStringSection(isec)) &&
                      (isCodeSection(isec) || isFoldableWithAddendsRemoved ||
                       isGccExceptTabSection(isec)) &&
                      !isec->keepUnique && !isec->hasAltEntry &&
                      !isec->shouldOmitFromOutput() && hasFoldableFlags;
    if (isFoldable) {
      foldable.push_back(isec);
      for (Defined *d : isec->symbols)
        if (d->unwindEntry)
          foldable.push_back(d->unwindEntry);

      // Some sections have embedded addends that foil ICF's hashing / equality
      // checks. (We can ignore embedded addends when doing ICF because the same
      // information gets recorded in our Reloc structs.) We therefore create a
      // mutable copy of the section data and zero out the embedded addends
      // before performing any hashing / equality checks.
      if (isFoldableWithAddendsRemoved) {
        // We have to do this copying serially as the BumpPtrAllocator is not
        // thread-safe. FIXME: Make a thread-safe allocator.
        MutableArrayRef<uint8_t> copy = isec->data.copy(bAlloc());
        for (const Reloc &r : isec->relocs)
          target->relocateOne(copy.data() + r.offset, r, /*va=*/0,
                              /*relocVA=*/0);
        isec->data = copy;
      }
    } else if (!isEhFrameSection(isec)) {
      // EH frames are gathered as foldables from unwindEntry above; give a
      // unique ID to everything else.
      isec->icfEqClass[0] = ++icfUniqueID;
    }
  }
  parallelForEach(foldable, [](ConcatInputSection *isec) {
    assert(isec->icfEqClass[0] == 0);
    isec->icfEqClass[0] = xxh3_64bits(isec->data) | (1ull << 31);
  });
  // Now that every input section is either hashed or marked as unique, run the
  // segregation algorithm to detect foldable subsections.
  ICF(foldable).run();
}
