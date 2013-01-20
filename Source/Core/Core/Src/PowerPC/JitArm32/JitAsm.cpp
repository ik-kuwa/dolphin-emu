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


#include "../../HW/Memmap.h"

#include "../PowerPC.h"
#include "../../CoreTiming.h"
#include "MemoryUtil.h"

#include "Jit.h"
#include "../JitCommon/JitCache.h"

#include "../../HW/GPFifo.h"
#include "../../Core.h"
#include "JitAsm.h"
#include "ArmABI.h"
#include "ArmEmitter.h"

using namespace ArmGen;

//TODO - make an option
//#if _DEBUG
//	bool enableDebug = false; 
//#else
//	bool enableDebug = false; 
//#endif

JitArmAsmRoutineManager asm_routines;

void JitArmAsmRoutineManager::Generate()
{
	enterCode = GetCodePtr();
	PUSH(2, R11, _LR); // R11 is frame pointer in Debug.

	ARMABI_MOVI2R(R0, (u32)&CoreTiming::downcount);

	FixupBranch skipToRealDispatcher = B();
	dispatcher = GetCodePtr();	
		printf("Dispatcher is %p\n", dispatcher);

		// Downcount Check	
		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bail = B_CC(CC_MI);

		SetJumpTarget(skipToRealDispatcher); 
		dispatcherNoCheck = GetCodePtr();

		// This block of code gets the address of the compiled block of code
		// It runs though to the compiling portion if it isn't found
			ARMABI_MOVI2R(R9, (u32)&PC);
			LDR(R9, R9);// Load the current PC into R9

			ARMABI_MOVI2R(R10, JIT_ICACHE_MASK); // Potential for optimization
			AND(R9, R9, R10); // R9 contains PC & JIT_ICACHE_MASK here.
			// Confirmed good to this point 08-03-12

			ARMABI_MOVI2R(R10, (u32)jit->GetBlockCache()->GetICache());
			// Confirmed That this loads the base iCache Location correctly 08-04-12

			LDR(R9, R10, R9, true, true); // R9 contains iCache[PC & JIT_ICACHE_MASK] here
			// R9 Confirmed this is the correct iCache Location loaded.
			TST(R9, 0xFC); // Test  to see if it is a JIT block.

			SetCC(CC_EQ); // Only run next part if R9 is zero
			// Success, it is our Jitblock.
			ARMABI_MOVI2R(R10, (u32)jit->GetBlockCache()->GetCodePointers());
			// LDR R10 right here to get CodePointers()[0] pointer.
			REV(R9, R9); // Reversing this gives us our JITblock.
			LSL(R9, R9, 2); // Multiply by four because address locations are u32 in size 
			LDR(R10, R10, R9, true, true); // Load the block address in to R10 

			B(R10);
			
			FixupBranch NextBlock = B(); // Jump to end so we can start a new block
			SetCC(); // Return to always executing codes

		// If we get to this point, that means that we don't have the block cached to execute
		// So call ArmJit to compile the block and then execute it.
		ARMABI_MOVI2R(R14, (u32)&Jit);	
		ARMABI_MOVI2R(R0, (u32)&PC);
		LDR(R0, R0);
		BL(R14);
			
		B(dispatcherNoCheck);

		// fpException()
		// Floating Point Exception Check, Jumped to if false
		fpException = GetCodePtr();
			ARMABI_MOVI2R(R0, (u32)&PowerPC::ppcState.Exceptions);
			ARMABI_MOVI2R(R2, EXCEPTION_FPU_UNAVAILABLE); // Potentially can be optimized
			LDREX(R1, R0);
			ORR(R1, R1, R2);
			STREX(R2, R0, R1);
				ARMABI_CallFunction((void*)&PowerPC::CheckExceptions);
			ARMABI_MOVI2R(R0, (u32)&NPC);
			ARMABI_MOVI2R(R1, (u32)&PC);
			LDR(R0, R0);
			STR(R1, R0);
		B(dispatcher);

		SetJumpTarget(bail);
		doTiming = GetCodePtr();			
		// XXX: In JIT64, Advance() gets called /after/ the exception checking
		// once it jumps back to the start of outerLoop 
		ARMABI_CallFunction((void*)&CoreTiming::Advance);

		// Does exception checking 
		testExceptions = GetCodePtr();
			ARMABI_MOVI2R(R0, (u32)&PC);
			ARMABI_MOVI2R(R1, (u32)&NPC);
			LDR(R0, R0);
			STR(R1, R0);
				ARMABI_CallFunction((void*)&PowerPC::CheckExceptions);
			ARMABI_MOVI2R(R0, (u32)&PC);
			ARMABI_MOVI2R(R1, (u32)&NPC);
			LDR(R1, R1);
			STR(R0, R1);
		// Check the state pointer to see if we are exiting
		// Gets checked on every exception check
			ARMABI_MOVI2R(R0, Mem((void*)PowerPC::GetStatePtr()));
			ARMABI_MOVI2R(R1, IMM(0xFFFFFFFF));
			LDR(R0, R0);
			TST(R0, R1);
			FixupBranch Exit = B_CC(CC_NEQ);

		SetJumpTarget(NextBlock);

		UpdateAPSR(true, 0, true, 0); // Clear our host register flags out.
	B(dispatcher);
	
	SetJumpTarget(Exit);

	POP(2, R11, _PC);
	Flush();
}

void JitArmAsmRoutineManager::GenerateCommon()
{
/*	fifoDirectWrite8 = AlignCode4();
	GenFifoWrite(8);
	fifoDirectWrite16 = AlignCode4();
	GenFifoWrite(16);
	fifoDirectWrite32 = AlignCode4();
	GenFifoWrite(32);
	fifoDirectWriteFloat = AlignCode4();
	GenFifoFloatWrite();
	fifoDirectWriteXmm64 = AlignCode4(); 
	GenFifoXmm64Write();

	GenQuantizedLoads();
	GenQuantizedStores();
	GenQuantizedSingleStores();
*/
	//CMPSD(R(XMM0), M(&zero), 
	// TODO

	// Fast write routines - special case the most common hardware write
	// TODO: use this.
	// Even in x86, the param values will be in the right registers.
	/*
	const u8 *fastMemWrite8 = AlignCode16();
	CMP(32, R(ABI_PARAM2), Imm32(0xCC008000));
	FixupBranch skip_fast_write = J_CC(CC_NE, false);
	MOV(32, EAX, M(&m_gatherPipeCount));
	MOV(8, MDisp(EAX, (u32)&m_gatherPipe), ABI_PARAM1);
	ADD(32, 1, M(&m_gatherPipeCount));
	RET();
	SetJumpTarget(skip_fast_write);
	CALL((void *)&Memory::Write_U8);*/
}