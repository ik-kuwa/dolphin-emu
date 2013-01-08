// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Common.h"
#include "Thunk.h"

#include "../../Core.h"
#include "../PowerPC.h"
#include "../../ConfigManager.h"
#include "../../CoreTiming.h"
#include "../PPCTables.h"
#include "ArmEmitter.h"
#include "../../HW/Memmap.h"


#include "Jit.h"
#include "JitRegCache.h"
#include "JitAsm.h"

void JitArm::stw(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(LoadStore)

	ARMReg RS = gpr.R(inst.RS);
	ARMReg ValueReg = gpr.GetReg();
	ARMReg Addr = gpr.GetReg();
	ARMReg Function = gpr.GetReg();
	
	MOV(ValueReg, RS);
	if (inst.RA)
	{
		ARMABI_MOVI2R(Addr, inst.SIMM_16);
		ARMReg RA = gpr.R(inst.RA);
		ADD(Addr, Addr, RA);
	}
	else
		ARMABI_MOVI2R(Addr, (u32)inst.SIMM_16);
	
	ARMABI_MOVI2R(Function, (u32)&Memory::Write_U32);	
	PUSH(4, R0, R1, R2, R3);
	MOV(R0, ValueReg);
	MOV(R1, Addr);
	BL(Function);
	POP(4, R0, R1, R2, R3);
	gpr.Unlock(ValueReg, Addr, Function);
}
void JitArm::stwu(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(LoadStore)

	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg ValueReg = gpr.GetReg();
	ARMReg Addr = gpr.GetReg();
	ARMReg Function = gpr.GetReg();
	
	ARMABI_MOVI2R(Addr, inst.SIMM_16);
	ADD(Addr, Addr, RA);

	// Check and set the update before writing since calling a function can
	// mess with the "special registers R11+ which may cause some issues.
	ARMABI_MOVI2R(Function, (u32)&PowerPC::ppcState.Exceptions);
	LDR(Function, Function);
	ARMABI_MOVI2R(ValueReg, EXCEPTION_DSI);
	CMP(Function, ValueReg);
	FixupBranch DoNotWrite = B_CC(CC_EQ);
	MOV(RA, Addr);
	SetJumpTarget(DoNotWrite);

	MOV(ValueReg, RS);
	
	ARMABI_MOVI2R(Function, (u32)&Memory::Write_U32);	
	PUSH(4, R0, R1, R2, R3);
	MOV(R0, ValueReg);
	MOV(R1, Addr);
	BL(Function);
	POP(4, R0, R1, R2, R3);

	gpr.Unlock(ValueReg, Addr, Function);
}
void JitArm::lhz(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(LoadStore)

	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMReg RD = gpr.R(inst.RD);
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.Exceptions);
	LDR(rA, rA);
	ARMABI_MOVI2R(rB, EXCEPTION_DSI);
	CMP(rA, rB);
	FixupBranch DoNotLoad = B_CC(CC_EQ);
	
	if (inst.RA)
	{
		ARMABI_MOVI2R(rB, inst.SIMM_16);
		ARMReg RA = gpr.R(inst.RA);
		ADD(rB, rB, RA);
	}
	else	
		ARMABI_MOVI2R(rB, (u32)inst.SIMM_16);
	
	ARMABI_MOVI2R(rA, (u32)&Memory::Read_U16);	
	PUSH(4, R0, R1, R2, R3);
	MOV(R0, rB);
	BL(rA);
	MOV(rA, R0);
	POP(4, R0, R1, R2, R3);
	MOV(RD, rA);
	SetJumpTarget(DoNotLoad);
	gpr.Unlock(rA, rB);
}
void JitArm::lwz(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(LoadStore)

	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMReg RD = gpr.R(inst.RD);
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.Exceptions);
	LDR(rA, rA);
	ARMABI_MOVI2R(rB, EXCEPTION_DSI);
	CMP(rA, rB);
	FixupBranch DoNotLoad = B_CC(CC_EQ);
	
	if (inst.RA)
	{
		ARMABI_MOVI2R(rB, inst.SIMM_16);
		ARMReg RA = gpr.R(inst.RA);
		ADD(rB, rB, RA);
	}
	else
		ARMABI_MOVI2R(rB, (u32)inst.SIMM_16);
	
	ARMABI_MOVI2R(rA, (u32)&Memory::Read_U32);	
	PUSH(4, R0, R1, R2, R3);
	MOV(R0, rB);
	BL(rA);
	MOV(rA, R0);
	POP(4, R0, R1, R2, R3);
	MOV(RD, rA);
	SetJumpTarget(DoNotLoad);
	gpr.Unlock(rA, rB);
	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bSkipIdle &&
		(inst.hex & 0xFFFF0000) == 0x800D0000 &&
		(Memory::ReadUnchecked_U32(js.compilerPC + 4) == 0x28000000 ||
		(SConfig::GetInstance().m_LocalCoreStartupParameter.bWii && Memory::ReadUnchecked_U32(js.compilerPC + 4) == 0x2C000000)) &&
		Memory::ReadUnchecked_U32(js.compilerPC + 8) == 0x4182fff8)
	{
		gpr.Flush();
		
		// if it's still 0, we can wait until the next event
		TST(RD, RD);
		FixupBranch noIdle = B_CC(CC_NEQ);
		rA = gpr.GetReg();	
		
		ARMABI_MOVI2R(rA, (u32)&PowerPC::OnIdle);
		ARMABI_MOVI2R(R0, PowerPC::ppcState.gpr[inst.RA] + (s32)(s16)inst.SIMM_16); 
		BL(rA);

		gpr.Unlock(rA);
		WriteExceptionExit();

		SetJumpTarget(noIdle);

		//js.compilerPC += 8;
		return;
	}

}
void JitArm::lwzx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(LoadStore)

	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();

	ARMReg RB = gpr.R(inst.RB);
	ARMReg RD = gpr.R(inst.RD);
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.Exceptions);
	LDR(rA, rA);
	ARMABI_MOVI2R(rB, EXCEPTION_DSI);
	CMP(rA, rB);
	FixupBranch DoNotLoad = B_CC(CC_EQ);
	
	if (inst.RA)
	{
		ARMReg RA = gpr.R(inst.RA);
		ADD(rB, RA, RB);
	}
	else
		MOV(rB, RB);
	
	ARMABI_MOVI2R(rA, (u32)&Memory::Read_U32);	
	PUSH(4, R0, R1, R2, R3);
	MOV(R0, rB);
	BL(rA);
	MOV(rA, R0);
	POP(4, R0, R1, R2, R3);
	MOV(RD, rA);
	SetJumpTarget(DoNotLoad);
	gpr.Unlock(rA, rB);
	////	u32 temp = Memory::Read_U32(_inst.RA ? (m_GPR[_inst.RA] + m_GPR[_inst.RB]) : m_GPR[_inst.RB]);
}
void JitArm::icbi(UGeckoInstruction inst)
{
	Default(inst);
	WriteExit(js.compilerPC + 4, 0);
}

