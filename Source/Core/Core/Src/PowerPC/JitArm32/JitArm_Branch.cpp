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
#include "../../CoreTiming.h"
#include "../PPCTables.h"
#include "ArmEmitter.h"

#include "Jit.h"
#include "JitRegCache.h"
#include "JitAsm.h"

// The branches are known good, or at least reasonably good.
// No need for a disable-mechanism.

// If defined, clears CR0 at blr and bl-s. If the assumption that
// flags never carry over between functions holds, then the task for 
// an optimizer becomes much easier.

// #define ACID_TEST

// Zelda and many more games seem to pass the Acid Test. 


using namespace ArmGen;
void JitArm::sc(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

	gpr.Flush();
//	fpr.Flush(FLUSH_ALL);
	ARMABI_MOVI2M((u32)&PC, js.compilerPC + 4); // Destroys R12 and R14
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMReg rC = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.Exceptions);
	LDREX(rB, rA);
	ORR(rB, rB, EXCEPTION_SYSCALL);
	STREX(rC, rA, rB);
	DMB();
	gpr.Unlock(rA, rB, rC);

	WriteExceptionExit();
}

void JitArm::rfi(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

	gpr.Flush();
//	fpr.Flush(FLUSH_ALL);
 	// See Interpreter rfi for details
	const u32 mask = 0x87C0FFFF;
		const u32 clearMSR13 = 0xFFFBFFFF; // Mask used to clear the bit MSR[13]
	// MSR = ((MSR & ~mask) | (SRR1 & mask)) & clearMSR13;
	// R0 = MSR location
	// R1 = MSR contents
	// R2 = Mask
	// R3 = Mask
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMReg rC = gpr.GetReg();
	ARMReg rD = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&MSR);
	ARMABI_MOVI2R(rB, (~mask) & clearMSR13);
	ARMABI_MOVI2R(rC, mask & clearMSR13);

	LDR(rD, rA);

	AND(rD, rD, rB); // rD = Masked MSR
	STR(rA, rD);

	ARMABI_MOVI2R(rB, (u32)&SRR1);
	LDR(rB, rB); // rB contains SRR1 here

	AND(rB, rB, rC); // rB contains masked SRR1 here
	ORR(rB, rD, rB); // rB = Masked MSR OR masked SRR1

	STR(rA, rB); // STR rB in to rA

	ARMABI_MOVI2R(rA, (u32)&SRR0);
	LDR(rA, rA);
	
	gpr.Unlock(rB, rC, rD);
	WriteRfiExitDestInR(rA); // rA gets unlocked here
	//AND(32, M(&MSR), Imm32((~mask) & clearMSR13));
	//MOV(32, R(EAX), M(&SRR1));
	//AND(32, R(EAX), Imm32(mask & clearMSR13));
	//OR(32, M(&MSR), R(EAX));
	// NPC = SRR0;
	//MOV(32, R(EAX), M(&SRR0));
	//WriteRfiExitDestInEAX();
}

void JitArm::bx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)
	// We must always process the following sentence
	// even if the blocks are merged by PPCAnalyst::Flatten().
	printf("LR address: %08x\n", (u32)&LR);
	if (inst.LK)
		ARMABI_MOVI2M((u32)&LR, js.compilerPC + 4);

	// If this is not the last instruction of a block,
	// we will skip the rest process.
	// Because PPCAnalyst::Flatten() merged the blocks.
	if (!js.isLastInstruction) {
		return;
	}

	gpr.Flush();
	//fpr.Flush(FLUSH_ALL);

	u32 destination;
	if (inst.AA)
		destination = SignExt26(inst.LI << 2);
	else
		destination = js.compilerPC + SignExt26(inst.LI << 2);
#ifdef ACID_TEST
	// TODO: Not implemented yet.
//	if (inst.LK)
//		AND(32, M(&PowerPC::ppcState.cr), Imm32(~(0xFF000000)));
#endif
 	if (destination == js.compilerPC)
	{
 		//PanicAlert("Idle loop detected at %08x", destination);
		//	CALL(ProtectFunction(&CoreTiming::Idle, 0));
		//	JMP(Asm::testExceptions, true);
		// make idle loops go faster
		js.downcountAmount += 8;
	}
	WriteExit(destination, 0);
}

// TODO: Finish these branch instructions
void JitArm::bcx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)
	// USES_CR
	_assert_msg_(DYNA_REC, js.isLastInstruction, "bcx not last instruction of block");

	gpr.Flush();
	//fpr.Flush(FLUSH_ALL);
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	FixupBranch pCTRDontBranch;
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
	{
		ARMABI_MOVI2R(rA, (u32)&CTR);
		LDR(rB, rA);
		SUBS(rB, rB, 1);
		STR(rA, rB);
			
		//SUB(32, M(&CTR), Imm8(1));
		if (inst.BO & BO_BRANCH_IF_CTR_0)
			pCTRDontBranch = B_CC(CC_NEQ);
		else
			pCTRDontBranch = B_CC(CC_EQ);
	}

	FixupBranch pConditionDontBranch;
	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
	{
		printf("Value: %08x, Jmp on %s\n", (8 >> (inst.BI & 3)), (inst.BO & BO_BRANCH_IF_TRUE) ? "True" :
		"False");
		ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[inst.BI >> 2]); 
		LDRB(rA, rA);
		MOV(rB, 8 >> (inst.BI & 3));
		TST(rA, rB);

		//TEST(8, M(&PowerPC::ppcState.cr_fast[inst.BI >> 2]), Imm8(8 >> (inst.BI & 3)));
		if (inst.BO & BO_BRANCH_IF_TRUE)  // Conditional branch 
			pConditionDontBranch = B_CC(CC_EQ); // Zero
		else
			pConditionDontBranch = B_CC(CC_NEQ); // Not Zero
	}
	gpr.Unlock(rA, rB);
	if (inst.LK)
		ARMABI_MOVI2M((u32)&LR, js.compilerPC + 4); // Careful, destroys R14, R12
	
	u32 destination;
	if(inst.AA)
		destination = SignExt16(inst.BD << 2);
	else
		destination = js.compilerPC + SignExt16(inst.BD << 2);
	WriteExit(destination, 0);

	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
		SetJumpTarget( pConditionDontBranch );
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
		SetJumpTarget( pCTRDontBranch );

	WriteExit(js.compilerPC + 4, 1);
}
void JitArm::bcctrx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)

	gpr.Flush();
	//fpr.Flush(FLUSH_ALL);

	// bcctrx doesn't decrement and/or test CTR
	_dbg_assert_msg_(POWERPC, inst.BO_2 & BO_DONT_DECREMENT_FLAG, "bcctrx with decrement and test CTR option is invalid!");

	if (inst.BO_2 & BO_DONT_CHECK_CONDITION)
	{
		// BO_2 == 1z1zz -> b always

		//NPC = CTR & 0xfffffffc;
		if(inst.LK_3)
			ARMABI_MOVI2M((u32)&LR, js.compilerPC + 4);
		ARMReg rA = gpr.GetReg();
		ARMReg rB = gpr.GetReg();
		ARMABI_MOVI2R(rA, (u32)&CTR);
		MVN(rB, 0x3); // 0xFFFFFFFC
		LDR(rA, rA);
		AND(rA, rA, rB);
		gpr.Unlock(rB);
		WriteExitDestInR(rA);
	}
	else
	{
		printf("Rare version not yet implemented\n");
		exit(0x4A4E);
		// Rare condition seen in (just some versions of?) Nintendo's NES Emulator

		// BO_2 == 001zy -> b if false
		// BO_2 == 011zy -> b if true

		// Ripped from bclrx
		/*TEST(8, M(&PowerPC::ppcState.cr_fast[inst.BI >> 2]), Imm8(8 >> (inst.BI & 3)));
		Gen::CCFlags branch;
		if (inst.BO_2 & BO_BRANCH_IF_TRUE)
			branch = CC_Z;
		else
			branch = CC_NZ; 
		FixupBranch b = J_CC(branch, false);
		MOV(32, R(EAX), M(&CTR));
		AND(32, R(EAX), Imm32(0xFFFFFFFC));
		//MOV(32, M(&PC), R(EAX)); => Already done in WriteExitDestInEAX()
		if (inst.LK_3)
			MOV(32, M(&LR), Imm32(js.compilerPC + 4)); //	LR = PC + 4;
		WriteExitDestInEAX();
		// Would really like to continue the block here, but it ends. TODO.
		SetJumpTarget(b);
		WriteExit(js.compilerPC + 4, 1);*/
	}
}
void JitArm::bclrx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Branch)
	if (!js.isLastInstruction &&
		(inst.BO & (1 << 4)) && (inst.BO & (1 << 2))) {
		if (inst.LK)
		{
			ARMABI_MOVI2M((u32)&LR, js.compilerPC + 4);
		}
		return;
	}
	gpr.Flush();
	//fpr.Flush(FLUSH_ALL);
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	FixupBranch pCTRDontBranch;
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)  // Decrement and test CTR
	{
		ARMABI_MOVI2R(rA, (u32)&CTR);
		LDR(rB, rA);
		SUBS(rB, rB, 1);
		STR(rA, rB);
			
		//SUB(32, M(&CTR), Imm8(1));
		if (inst.BO & BO_BRANCH_IF_CTR_0)
			pCTRDontBranch = B_CC(CC_NEQ);
		else
			pCTRDontBranch = B_CC(CC_EQ);
	}

	FixupBranch pConditionDontBranch;
	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)  // Test a CR bit
	{
		ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[inst.BI >> 2]); 
		MOV(rB, 8 >> (inst.BI & 3));
		LDR(rA, rA);
		TST(rA, rB);
		//TEST(8, M(&PowerPC::ppcState.cr_fast[inst.BI >> 2]), Imm8(8 >> (inst.BI & 3)));
		if (inst.BO & BO_BRANCH_IF_TRUE)  // Conditional branch 
			pConditionDontBranch = B_CC(CC_EQ); // Zero
		else
			pConditionDontBranch = B_CC(CC_NEQ); // Not Zero
	}

	// This below line can be used to prove that blr "eats flags" in practice.
	// This observation will let us do a lot of fun observations.
#ifdef ACID_TEST
	// TODO: Not yet implemented
	//	AND(32, M(&PowerPC::ppcState.cr), Imm32(~(0xFF000000)));
#endif

	//MOV(32, R(EAX), M(&LR));	
	//AND(32, R(EAX), Imm32(0xFFFFFFFC));
	ARMABI_MOVI2R(rA, (u32)&LR);
	MVN(rB, 0x3); // 0xFFFFFFFC
	LDR(rA, rA);
	AND(rA, rA, rB);
	if (inst.LK){
		ARMReg rC = gpr.GetReg(false);
		u32 Jumpto = js.compilerPC + 4;
		ARMABI_MOVI2R(rB, (u32)&LR);
		MOVW(rC, Jumpto);
		MOVT(rC, Jumpto, true);
		STR(rB, rC);
		//ARMABI_MOVI2M((u32)&LR, js.compilerPC + 4);
	}
	gpr.Unlock(rB); // rA gets unlocked in WriteExitDestInR
	WriteExitDestInR(rA);

	if ((inst.BO & BO_DONT_CHECK_CONDITION) == 0)
		SetJumpTarget( pConditionDontBranch );
	if ((inst.BO & BO_DONT_DECREMENT_FLAG) == 0)
		SetJumpTarget( pCTRDontBranch );
	WriteExit(js.compilerPC + 4, 1);

}
