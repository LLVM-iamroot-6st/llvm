//===-- SystemZFrameLowering.h - Frame lowering for SystemZ -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SYSTEMZFRAMELOWERING_H
#define SYSTEMZFRAMELOWERING_H

#include "SystemZSubtarget.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/Target/TargetFrameLowering.h"

namespace llvm {
class SystemZTargetMachine;
class SystemZSubtarget;

class SystemZFrameLowering : public TargetFrameLowering {
  IndexedMap<unsigned> RegSpillOffsets;

protected:
  const SystemZTargetMachine &TM;
  const SystemZSubtarget &STI;

public:
  SystemZFrameLowering(const SystemZTargetMachine &tm,
                       const SystemZSubtarget &sti);

  // Override TargetFrameLowering.
  bool isFPCloseToIncomingSP() const override { return false; }
  const SpillSlot *getCalleeSavedSpillSlots(unsigned &NumEntries) const
    override;
  void processFunctionBeforeCalleeSavedScan(MachineFunction &MF,
                                            RegScavenger *RS) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBII,
                                   const std::vector<CalleeSavedInfo> &CSI,
                                   const TargetRegisterInfo *TRI) const
    override;
  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;
  void emitPrologue(MachineFunction &MF) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  bool hasFP(const MachineFunction &MF) const override;
  int getFrameIndexOffset(const MachineFunction &MF, int FI) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  void eliminateCallFramePseudoInstr(MachineFunction &MF,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI) const
    override;

  // Return the number of bytes in the callee-allocated part of the frame.
  uint64_t getAllocatedStackSize(const MachineFunction &MF) const;

  // Return the byte offset from the incoming stack pointer of Reg's
  // ABI-defined save slot.  Return 0 if no slot is defined for Reg.
  unsigned getRegSpillOffset(unsigned Reg) const {
    return RegSpillOffsets[Reg];
  }
};
} // end namespace llvm

#endif
