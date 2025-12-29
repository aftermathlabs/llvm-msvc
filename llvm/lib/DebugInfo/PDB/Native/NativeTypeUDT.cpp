//===- NativeTypeUDT.cpp - info about class/struct type ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeTypeUDT.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeTypeUDT::NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                             codeview::TypeIndex TI, codeview::ClassRecord CR)
    : NativeRawSymbol(Session, PDB_SymType::UDT, Id), Index(TI),
      Class(std::move(CR)), Tag(&*Class) {}

NativeTypeUDT::NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                             codeview::TypeIndex TI, codeview::Class2Record CR)
    : NativeRawSymbol(Session, PDB_SymType::UDT, Id), Index(TI),
      Class2(std::move(CR)), Tag2(&*Class2) {}

NativeTypeUDT::NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                             codeview::TypeIndex TI, codeview::UnionRecord UR)
    : NativeRawSymbol(Session, PDB_SymType::UDT, Id), Index(TI),
      Union(std::move(UR)), Tag(&*Union) {}

NativeTypeUDT::NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                             NativeTypeUDT &UnmodifiedType,
                             codeview::ModifierRecord Modifier)
    : NativeRawSymbol(Session, PDB_SymType::UDT, Id),
      UnmodifiedType(&UnmodifiedType), Modifiers(std::move(Modifier)) {}

NativeTypeUDT::~NativeTypeUDT() = default;

void NativeTypeUDT::dump(raw_ostream &OS, int Indent,
                         PdbSymbolIdField ShowIdFields,
                         PdbSymbolIdField RecurseIdFields) const {

  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolIdField(OS, "lexicalParentId", 0, Indent, Session,
                    PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);
  if (Modifiers)
    dumpSymbolIdField(OS, "unmodifiedTypeId", getUnmodifiedTypeId(), Indent,
                      Session, PdbSymbolIdField::UnmodifiedType, ShowIdFields,
                      RecurseIdFields);
  if (getUdtKind() != PDB_UdtType::Union)
    dumpSymbolField(OS, "virtualTableShapeId", getVirtualTableShapeId(),
                    Indent);
  dumpSymbolField(OS, "length", getLength(), Indent);
  dumpSymbolField(OS, "udtKind", getUdtKind(), Indent);
  dumpSymbolField(OS, "constructor", hasConstructor(), Indent);
  dumpSymbolField(OS, "constType", isConstType(), Indent);
  dumpSymbolField(OS, "hasAssignmentOperator", hasAssignmentOperator(), Indent);
  dumpSymbolField(OS, "hasCastOperator", hasCastOperator(), Indent);
  dumpSymbolField(OS, "hasNestedTypes", hasNestedTypes(), Indent);
  dumpSymbolField(OS, "overloadedOperator", hasOverloadedOperator(), Indent);
  dumpSymbolField(OS, "isInterfaceUdt", isInterfaceUdt(), Indent);
  dumpSymbolField(OS, "intrinsic", isIntrinsic(), Indent);
  dumpSymbolField(OS, "nested", isNested(), Indent);
  dumpSymbolField(OS, "packed", isPacked(), Indent);
  dumpSymbolField(OS, "isRefUdt", isRefUdt(), Indent);
  dumpSymbolField(OS, "scoped", isScoped(), Indent);
  dumpSymbolField(OS, "unalignedType", isUnalignedType(), Indent);
  dumpSymbolField(OS, "isValueUdt", isValueUdt(), Indent);
  dumpSymbolField(OS, "volatileType", isVolatileType(), Indent);
}

std::string NativeTypeUDT::getName() const {
  if (UnmodifiedType)
    return UnmodifiedType->getName();

  if (Tag2)
    return std::string(Tag2->getName());

  return std::string(Tag->getName());
}

SymIndexId NativeTypeUDT::getLexicalParentId() const { return 0; }

SymIndexId NativeTypeUDT::getUnmodifiedTypeId() const {
  if (UnmodifiedType)
    return UnmodifiedType->getSymIndexId();

  return 0;
}

SymIndexId NativeTypeUDT::getVirtualTableShapeId() const {
  if (UnmodifiedType)
    return UnmodifiedType->getVirtualTableShapeId();

  if (Class2)
    return Session.getSymbolCache().findSymbolByTypeIndex(Class2->VTableShape);

  if (Class)
    return Session.getSymbolCache().findSymbolByTypeIndex(Class->VTableShape);

  return 0;
}

uint64_t NativeTypeUDT::getLength() const {
  if (UnmodifiedType)
    return UnmodifiedType->getLength();

  if (Class2)
    return Class2->getSize();

  if (Class)
    return Class->getSize();

  return Union->getSize();
}

PDB_UdtType NativeTypeUDT::getUdtKind() const {
  if (UnmodifiedType)
    return UnmodifiedType->getUdtKind();

  if (Tag2) {
    switch (Tag2->Kind) {
    case TypeRecordKind::Class2:
      return PDB_UdtType::Class;
    case TypeRecordKind::Struct2:
      return PDB_UdtType::Struct;

    case TypeRecordKind::Class:
      return PDB_UdtType::Class;
    case TypeRecordKind::Union:
      return PDB_UdtType::Union;
    case TypeRecordKind::Struct:
      return PDB_UdtType::Struct;
    case TypeRecordKind::Interface:
      return PDB_UdtType::Interface;
    default:
      llvm_unreachable("Unexpected udt kind");
    }
  }

  if (Tag) {
    switch (Tag->Kind) {
    case TypeRecordKind::Class2:
      return PDB_UdtType::Class;
    case TypeRecordKind::Struct2:
      return PDB_UdtType::Struct;

    case TypeRecordKind::Class:
      return PDB_UdtType::Class;
    case TypeRecordKind::Union:
      return PDB_UdtType::Union;
    case TypeRecordKind::Struct:
      return PDB_UdtType::Struct;
    case TypeRecordKind::Interface:
      return PDB_UdtType::Interface;
    default:
      llvm_unreachable("Unexpected udt kind");
    }
  }
  llvm_unreachable("Unexpected udt kind");
}

bool NativeTypeUDT::hasConstructor() const {
  if (UnmodifiedType)
    return UnmodifiedType->hasConstructor();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::HasConstructorOrDestructor) != ClassOptions2::None;
  }

  return (Tag->Options & ClassOptions::HasConstructorOrDestructor) !=
         ClassOptions::None;
}

bool NativeTypeUDT::isConstType() const {
  if (!Modifiers)
    return false;
  return (Modifiers->Modifiers & ModifierOptions::Const) !=
         ModifierOptions::None;
}

bool NativeTypeUDT::hasAssignmentOperator() const {
  if (UnmodifiedType)
    return UnmodifiedType->hasAssignmentOperator();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::HasOverloadedAssignmentOperator) != ClassOptions2::None;
  }

  return (Tag->Options & ClassOptions::HasOverloadedAssignmentOperator) !=   ClassOptions::None;
}

bool NativeTypeUDT::hasCastOperator() const {
  if (UnmodifiedType)
    return UnmodifiedType->hasCastOperator();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::HasConversionOperator) != ClassOptions2::None;
  }

  return (Tag->Options & ClassOptions::HasConversionOperator) != ClassOptions::None;
}

bool NativeTypeUDT::hasNestedTypes() const {
  if (UnmodifiedType)
    return UnmodifiedType->hasNestedTypes();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::ContainsNestedClass) !=  ClassOptions2::None;
  }

  return (Tag->Options & ClassOptions::ContainsNestedClass) != ClassOptions::None;
}

bool NativeTypeUDT::hasOverloadedOperator() const {
  if (UnmodifiedType)
    return UnmodifiedType->hasOverloadedOperator();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::HasOverloadedOperator) != ClassOptions2::None;
  }

  return (Tag->Options & ClassOptions::HasOverloadedOperator) !=
         ClassOptions::None;
}

bool NativeTypeUDT::isInterfaceUdt() const { return false; }

bool NativeTypeUDT::isIntrinsic() const {
  if (UnmodifiedType)
    return UnmodifiedType->isIntrinsic();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::Intrinsic) != ClassOptions2::None;
  }
  return (Tag->Options & ClassOptions::Intrinsic) != ClassOptions::None;
}

bool NativeTypeUDT::isNested() const {
  if (UnmodifiedType)
    return UnmodifiedType->isNested();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::Nested) != ClassOptions2::None;
  }
  return (Tag->Options & ClassOptions::Nested) != ClassOptions::None;
}

bool NativeTypeUDT::isPacked() const {
  if (UnmodifiedType)
    return UnmodifiedType->isPacked();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::Packed) != ClassOptions2::None;
  }
  return (Tag->Options & ClassOptions::Packed) != ClassOptions::None;
}

bool NativeTypeUDT::isRefUdt() const { return false; }

bool NativeTypeUDT::isScoped() const {
  if (UnmodifiedType)
    return UnmodifiedType->isScoped();

  if (Tag2) {
    return (Tag2->Options & ClassOptions2::Scoped) != ClassOptions2::None;
  }
  return (Tag->Options & ClassOptions::Scoped) != ClassOptions::None;
}

bool NativeTypeUDT::isValueUdt() const { return false; }

bool NativeTypeUDT::isUnalignedType() const {
  if (!Modifiers)
    return false;
  return (Modifiers->Modifiers & ModifierOptions::Unaligned) !=
         ModifierOptions::None;
}

bool NativeTypeUDT::isVolatileType() const {
  if (!Modifiers)
    return false;
  return (Modifiers->Modifiers & ModifierOptions::Volatile) !=
         ModifierOptions::None;
}
