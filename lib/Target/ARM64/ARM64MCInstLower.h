//===-- ARM64MCInstLower.h - Lower MachineInstr to MCInst ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef ARM64_MCINSTLOWER_H
#define ARM64_MCINSTLOWER_H

#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class AsmPrinter;
class MCAsmInfo;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;
class MachineModuleInfoMachO;
class MachineOperand;
class Mangler;

/// ARM64MCInstLower - This class is used to lower an MachineInstr
/// into an MCInst.
class LLVM_LIBRARY_VISIBILITY ARM64MCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;
  Triple TargetTriple;

public:
  ARM64MCInstLower(MCContext &ctx, Mangler &mang, AsmPrinter &printer);

  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const;
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

  MCOperand lowerSymbolOperandDarwin(const MachineOperand &MO,
                                     MCSymbol *Sym) const;
  MCOperand lowerSymbolOperandELF(const MachineOperand &MO,
                                  MCSymbol *Sym) const;
  MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
};
}

#endif
