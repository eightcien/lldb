//===-- EmulateInstructionARM.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "EmulateInstructionARM.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/ConstString.h"

#include "Plugins/Process/Utility/ARMDefines.h"
#include "Plugins/Process/Utility/ARMUtils.h"
#include "Utility/ARM_DWARF_Registers.h"

#include "llvm/Support/MathExtras.h" // for SignExtend32 template function
                                     // and CountTrailingZeros_32 function

using namespace lldb;
using namespace lldb_private;

// Convenient macro definitions.
#define APSR_C Bit32(m_inst_cpsr, CPSR_C_POS)
#define APSR_V Bit32(m_inst_cpsr, CPSR_V_POS)

#define AlignPC(pc_val) (pc_val & 0xFFFFFFFC)

//----------------------------------------------------------------------
//
// ITSession implementation
//
//----------------------------------------------------------------------

// A8.6.50
// Valid return values are {1, 2, 3, 4}, with 0 signifying an error condition.
static unsigned short CountITSize(unsigned ITMask) {
    // First count the trailing zeros of the IT mask.
    unsigned TZ = llvm::CountTrailingZeros_32(ITMask);
    if (TZ > 3)
    {
        printf("Encoding error: IT Mask '0000'\n");
        return 0;
    }
    return (4 - TZ);
}

// Init ITState.  Note that at least one bit is always 1 in mask.
bool ITSession::InitIT(unsigned short bits7_0)
{
    ITCounter = CountITSize(Bits32(bits7_0, 3, 0));
    if (ITCounter == 0)
        return false;

    // A8.6.50 IT
    unsigned short FirstCond = Bits32(bits7_0, 7, 4);
    if (FirstCond == 0xF)
    {
        printf("Encoding error: IT FirstCond '1111'\n");
        return false;
    }
    if (FirstCond == 0xE && ITCounter != 1)
    {
        printf("Encoding error: IT FirstCond '1110' && Mask != '1000'\n");
        return false;
    }

    ITState = bits7_0;
    return true;
}

// Update ITState if necessary.
void ITSession::ITAdvance()
{
    assert(ITCounter);
    --ITCounter;
    if (ITCounter == 0)
        ITState = 0;
    else
    {
        unsigned short NewITState4_0 = Bits32(ITState, 4, 0) << 1;
        SetBits32(ITState, 4, 0, NewITState4_0);
    }
}

// Return true if we're inside an IT Block.
bool ITSession::InITBlock()
{
    return ITCounter != 0;
}

// Return true if we're the last instruction inside an IT Block.
bool ITSession::LastInITBlock()
{
    return ITCounter == 1;
}

// Get condition bits for the current thumb instruction.
uint32_t ITSession::GetCond()
{
    if (InITBlock())
        return Bits32(ITState, 7, 4);
    else
        return COND_AL;
}

// ARM constants used during decoding
#define REG_RD          0
#define LDM_REGLIST     1
#define SP_REG          13
#define LR_REG          14
#define PC_REG          15
#define PC_REGLIST_BIT  0x8000

#define ARMv4     (1u << 0)
#define ARMv4T    (1u << 1)
#define ARMv5T    (1u << 2)
#define ARMv5TE   (1u << 3)
#define ARMv5TEJ  (1u << 4)
#define ARMv6     (1u << 5)
#define ARMv6K    (1u << 6)
#define ARMv6T2   (1u << 7)
#define ARMv7     (1u << 8)
#define ARMv8     (1u << 9)
#define ARMvAll   (0xffffffffu)

#define ARMV4T_ABOVE  (ARMv4T|ARMv5T|ARMv5TE|ARMv5TEJ|ARMv6|ARMv6K|ARMv6T2|ARMv7|ARMv8)
#define ARMV5_ABOVE   (ARMv5T|ARMv5TE|ARMv5TEJ|ARMv6|ARMv6K|ARMv6T2|ARMv7|ARMv8)
#define ARMV6T2_ABOVE (ARMv6T2|ARMv7|ARMv8)

//----------------------------------------------------------------------
//
// EmulateInstructionARM implementation
//
//----------------------------------------------------------------------

void
EmulateInstructionARM::Initialize ()
{
}

void
EmulateInstructionARM::Terminate ()
{
}

// Write "bits (32) UNKNOWN" to memory address "address".  Helper function for many ARM instructions.
bool
EmulateInstructionARM::WriteBits32UnknownToMemory (addr_t address)
{
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextWriteMemoryRandomBits;
    context.SetNoArgs ();

    uint32_t random_data = rand ();
    const uint32_t addr_byte_size = GetAddressByteSize();
    
    if (!MemAWrite (context, address, random_data, addr_byte_size))
        return false;
    
    return true;
}

// Write "bits (32) UNKNOWN" to register n.  Helper function for many ARM instructions.
bool
EmulateInstructionARM::WriteBits32Unknown (int n)
{
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextWriteRegisterRandomBits;
    context.SetNoArgs ();

    bool success;
    uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
                  
    if (!success)
        return false;
   
    if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, data))
        return false;
    
    return true;
}

// Push Multiple Registers stores multiple registers to the stack, storing to
// consecutive memory locations ending just below the address in SP, and updates
// SP to point to the start of the stored data.
bool 
EmulateInstructionARM::EmulatePUSH (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); 
        NullCheckIfThumbEE(13); 
        address = SP - 4*BitCount(registers);

        for (i = 0 to 14)
        {
            if (registers<i> == ’1’)
            {
                if i == 13 && i != LowestSetBit(registers) // Only possible for encoding A1 
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
            }
        }

        if (registers<15> == ’1’) // Only possible for encoding A1 or A2 
            MemA[address,4] = PCStoreValue();
        
        SP = SP - 4*BitCount(registers);
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t registers = 0;
        uint32_t Rt; // the source register
        switch (encoding) {
        case eEncodingT1:
            registers = Bits32(opcode, 7, 0);
            // The M bit represents LR.
            if (Bit32(opcode, 8))
                registers |= (1u << 14);
            // if BitCount(registers) < 1 then UNPREDICTABLE;
            if (BitCount(registers) < 1)
                return false;
            break;
        case eEncodingT2:
            // Ignore bits 15 & 13.
            registers = Bits32(opcode, 15, 0) & ~0xa000;
            // if BitCount(registers) < 2 then UNPREDICTABLE;
            if (BitCount(registers) < 2)
                return false;
            break;
        case eEncodingT3:
            Rt = Bits32(opcode, 15, 12);
            // if BadReg(t) then UNPREDICTABLE;
            if (BadReg(Rt))
                return false;
            registers = (1u << Rt);
            break;
        case eEncodingA1:
            registers = Bits32(opcode, 15, 0);
            // Instead of return false, let's handle the following case as well,
            // which amounts to pushing one reg onto the full descending stacks.
            // if BitCount(register_list) < 2 then SEE STMDB / STMFD;
            break;
        case eEncodingA2:
            Rt = Bits32(opcode, 15, 12);
            // if t == 13 then UNPREDICTABLE;
            if (Rt == dwarf_sp)
                return false;
            registers = (1u << Rt);
            break;
        default:
            return false;
        }
        addr_t sp_offset = addr_byte_size * BitCount (registers);
        addr_t addr = sp - sp_offset;
        uint32_t i;
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextPushRegisterOnStack;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, 0);
        for (i=0; i<15; ++i)
        {
            if (BitIsSet (registers, i))
            {
                dwarf_reg.num = dwarf_r0 + i;
                context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
                uint32_t reg_value = ReadCoreReg(i, &success);
                if (!success)
                    return false;
                if (!MemAWrite (context, addr, reg_value, addr_byte_size))
                    return false;
                addr += addr_byte_size;
            }
        }
        
        if (BitIsSet (registers, 15))
        {
            dwarf_reg.num = dwarf_pc;
            context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
            const uint32_t pc = ReadCoreReg(PC_REG, &success);
            if (!success)
                return false;
            if (!MemAWrite (context, addr, pc, addr_byte_size))
                return false;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (-sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

// Pop Multiple Registers loads multiple registers from the stack, loading from
// consecutive memory locations staring at the address in SP, and updates
// SP to point just above the loaded data.
bool 
EmulateInstructionARM::EmulatePOP (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(13);
        address = SP;
        for i = 0 to 14
            if registers<i> == ‘1’ then
                R[i} = if UnalignedAllowed then MemU[address,4] else MemA[address,4]; address = address + 4;
        if registers<15> == ‘1’ then
            if UnalignedAllowed then
                LoadWritePC(MemU[address,4]);
            else 
                LoadWritePC(MemA[address,4]);
        if registers<13> == ‘0’ then SP = SP + 4*BitCount(registers);
        if registers<13> == ‘1’ then SP = bits(32) UNKNOWN;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t registers = 0;
        uint32_t Rt; // the destination register
        switch (encoding) {
        case eEncodingT1:
            registers = Bits32(opcode, 7, 0);
            // The P bit represents PC.
            if (Bit32(opcode, 8))
                registers |= (1u << 15);
            // if BitCount(registers) < 1 then UNPREDICTABLE;
            if (BitCount(registers) < 1)
                return false;
            break;
        case eEncodingT2:
            // Ignore bit 13.
            registers = Bits32(opcode, 15, 0) & ~0x2000;
            // if BitCount(registers) < 2 || (P == '1' && M == '1') then UNPREDICTABLE;
            if (BitCount(registers) < 2 || (Bit32(opcode, 15) && Bit32(opcode, 14)))
                return false;
            // if registers<15> == '1' && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
            if (BitIsSet(registers, 15) && InITBlock() && !LastInITBlock())
                return false;
            break;
        case eEncodingT3:
            Rt = Bits32(opcode, 15, 12);
            // if t == 13 || (t == 15 && InITBlock() && !LastInITBlock()) then UNPREDICTABLE;
            if (Rt == 13)
                return false;
            if (Rt == 15 && InITBlock() && !LastInITBlock())
                return false;
            registers = (1u << Rt);
            break;
        case eEncodingA1:
            registers = Bits32(opcode, 15, 0);
            // Instead of return false, let's handle the following case as well,
            // which amounts to popping one reg from the full descending stacks.
            // if BitCount(register_list) < 2 then SEE LDM / LDMIA / LDMFD;

            // if registers<13> == ‘1’ && ArchVersion() >= 7 then UNPREDICTABLE;
            if (BitIsSet(opcode, 13) && ArchVersion() >= ARMv7)
                return false;
            break;
        case eEncodingA2:
            Rt = Bits32(opcode, 15, 12);
            // if t == 13 then UNPREDICTABLE;
            if (Rt == dwarf_sp)
                return false;
            registers = (1u << Rt);
            break;
        default:
            return false;
        }
        addr_t sp_offset = addr_byte_size * BitCount (registers);
        addr_t addr = sp;
        uint32_t i, data;
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextPopRegisterOffStack;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, 0);
        for (i=0; i<15; ++i)
        {
            if (BitIsSet (registers, i))
            {
                dwarf_reg.num = dwarf_r0 + i;
                context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
                data = MemARead(context, addr, 4, 0, &success);
                if (!success)
                    return false;    
                if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_reg.num, data))
                    return false;
                addr += addr_byte_size;
            }
        }
        
        if (BitIsSet (registers, 15))
        {
            dwarf_reg.num = dwarf_pc;
            context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
            data = MemARead(context, addr, 4, 0, &success);
            if (!success)
                return false;
            // In ARMv5T and above, this is an interworking branch.
            if (!LoadWritePC(context, data))
                return false;
            addr += addr_byte_size;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp + sp_offset))
            return false;
    }
    return true;
}

// Set r7 or ip to point to saved value residing within the stack.
// ADD (SP plus immediate)
bool
EmulateInstructionARM::EmulateADDRdSPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, imm32, ‘0’);
        if d == 15 then
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t Rd; // the destination register
        uint32_t imm32;
        switch (encoding) {
        case eEncodingT1:
            Rd = 7;
            imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32)
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp + sp_offset; // a pointer to the stack area
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register sp_reg;
        sp_reg.SetRegister (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
        context.SetRegisterPlusOffset (sp_reg, sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, addr))
            return false;
    }
    return true;
}

// Set r7 or ip to the current stack pointer.
// MOV (register)
bool
EmulateInstructionARM::EmulateMOVRdSP (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

    bool success = false;
    //const uint32_t opcode = OpcodeAsUnsigned (&success);
    //if (!success)
    //    return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t Rd; // the destination register
        switch (encoding) {
        case eEncodingT1:
            Rd = 7;
            break;
        case eEncodingA1:
            Rd = 12;
            break;
        default:
            return false;
        }
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register sp_reg;
        sp_reg.SetRegister (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
        context.SetRegisterPlusOffset (sp_reg, 0);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, sp))
            return false;
    }
    return true;
}

// Move from high register (r8-r15) to low register (r0-r7).
// MOV (register)
bool
EmulateInstructionARM::EmulateMOVLowHigh (ARMEncoding encoding)
{
    return EmulateMOVRdRm (encoding);
}

// Move from register to register.
// MOV (register)
bool
EmulateInstructionARM::EmulateMOVRdRm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rm; // the source register
        uint32_t Rd; // the destination register
        bool setflags;
        switch (encoding) {
        case eEncodingT1:
            Rd = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 6, 3);
            setflags = false;
            if (Rd == 15 && InITBlock() && !LastInITBlock())
                return false;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = true;
            if (InITBlock())
                return false;
            break;
        case eEncodingT3:
            Rd = Bits32(opcode, 11, 8);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            // if setflags && (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
            if (setflags && (BadReg(Rd) || BadReg(Rm)))
                return false;
            // if !setflags && (d == 15 || m == 15 || (d == 13 && m == 13)) then UNPREDICTABLE;
            if (!setflags && (Rd == 15 || Rm == 15 || (Rd == 13 && Rm == 13)))
                return false;
            break;
        default:
            return false;
        }
        uint32_t result = ReadCoreReg(Rm, &success);
        if (!success)
            return false;
        
        // The context specifies that Rm is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + Rm);
        context.SetRegisterPlusOffset (dwarf_reg, 0);

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags))
            return false;
    }
    return true;
}

// Move (immediate) writes an immediate value to the destination register.  It
// can optionally update the condition flags based on the value.
// MOV (immediate)
bool
EmulateInstructionARM::EmulateMOVRdImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd; // the destination register
        uint32_t imm32; // the immediate value to be written to Rd
        uint32_t carry; // the carry bit after ThumbExpandImm_C or ARMExpandImm_C.
        bool setflags;
        switch (encoding) {
        case eEncodingT1:
            Rd = Bits32(opcode, 10, 8);
            setflags = !InITBlock();
            imm32 = Bits32(opcode, 7, 0); // imm32 = ZeroExtend(imm8, 32)
            carry = APSR_C;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry);
            if (BadReg(Rd))
                return false;
            break;
        default:
            return false;
        }
        uint32_t result = imm32;

        // The context specifies that an immediate is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Bitwise NOT (immediate) writes the bitwise inverse of an immediate value to the destination register.
// It can optionally update the condition flags based on the value.
bool
EmulateInstructionARM::EmulateMVNImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = NOT(imm32);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd; // the destination register
        uint32_t imm32; // the output after ThumbExpandImm_C or ARMExpandImm_C
        uint32_t carry; // the carry bit after ThumbExpandImm_C or ARMExpandImm_C
        bool setflags;
        switch (encoding) {
        case eEncodingT1:
            Rd = Bits32(opcode, 11, 8);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry);
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry);
            // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }
        uint32_t result = ~imm32;
        
        // The context specifies that an immediate is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Bitwise NOT (register) writes the bitwise inverse of a register value to the destination register.
// It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateMVNReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = NOT(shifted);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rm; // the source register
        uint32_t Rd; // the destination register
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        uint32_t carry; // the carry bit after the shift operation
        switch (encoding) {
        case eEncodingT1:
            Rd = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            if (InITBlock())
                return false;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            // if (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
            if (BadReg(Rd) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            break;
        default:
            return false;
        }
        uint32_t value = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(value, shift_t, shift_n, APSR_C, carry);
        uint32_t result = ~shifted;
        
        // The context specifies that an immediate is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// PC relative immediate load into register, possibly followed by ADD (SP plus register).
// LDR (literal)
bool
EmulateInstructionARM::EmulateLDRRtPCRelative (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(15);
        base = Align(PC,4);
        address = if add then (base + imm32) else (base - imm32);
        data = MemU[address,4];
        if t == 15 then
            if address<1:0> == ‘00’ then LoadWritePC(data); else UNPREDICTABLE;
        elsif UnalignedSupport() || address<1:0> = ‘00’ then
            R[t] = data;
        else // Can only apply before ARMv7
            if CurrentInstrSet() == InstrSet_ARM then
                R[t] = ROR(data, 8*UInt(address<1:0>));
            else
                R[t] = bits(32) UNKNOWN;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t pc = ReadCoreReg(PC_REG, &success);
        if (!success)
            return false;

        // PC relative immediate load context
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register pc_reg;
        pc_reg.SetRegister (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
        context.SetRegisterPlusOffset (pc_reg, 0);                             
                                               
        uint32_t Rt;    // the destination register
        uint32_t imm32; // immediate offset from the PC
        bool add;       // +imm32 or -imm32?
        addr_t base;    // the base address
        addr_t address; // the PC relative address
        uint32_t data;  // the literal data value from the PC relative load
        switch (encoding) {
        case eEncodingT1:
            Rt = Bits32(opcode, 10, 8);
            imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32);
            add = true;
            break;
        case eEncodingT2:
            Rt = Bits32(opcode, 15, 12);
            imm32 = Bits32(opcode, 11, 0) << 2; // imm32 = ZeroExtend(imm12, 32);
            add = BitIsSet(opcode, 23);
            if (Rt == 15 && InITBlock() && !LastInITBlock())
                return false;
            break;
        default:
            return false;
        }

        base = Align(pc, 4);
        if (add)
            address = base + imm32;
        else
            address = base - imm32;

        context.SetRegisterPlusOffset(pc_reg, address - base);
        data = MemURead(context, address, 4, 0, &success);
        if (!success)
            return false;    

        if (Rt == 15)
        {
            if (Bits32(address, 1, 0) == 0)
            {
                // In ARMv5T and above, this is an interworking branch.
                if (!LoadWritePC(context, data))
                    return false;
            }
            else
                return false;
        }
        else if (UnalignedSupport() || Bits32(address, 1, 0) == 0)
        {
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rt, data))
                return false;
        }
        else // We don't handle ARM for now.
            return false;

        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rt, data))
            return false;
    }
    return true;
}

// An add operation to adjust the SP.
// ADD (SP plus immediate)
bool
EmulateInstructionARM::EmulateADDSPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, imm32, ‘0’);
        if d == 15 then // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t imm32; // the immediate operand
        switch (encoding) {
        case eEncodingT2:
            imm32 = ThumbImmScaled(opcode); // imm32 = ZeroExtend(imm7:'00', 32)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp + sp_offset; // the adjusted stack pointer value
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, addr))
            return false;
    }
    return true;
}

// An add operation to adjust the SP.
// ADD (SP plus register)
bool
EmulateInstructionARM::EmulateADDSPRm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(SP, shifted, ‘0’);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t Rm; // the second operand
        switch (encoding) {
        case eEncodingT2:
            Rm = Bits32(opcode, 6, 3);
            break;
        default:
            return false;
        }
        int32_t reg_value = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        addr_t addr = (int32_t)sp + reg_value; // the adjusted stack pointer value
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (reg_value);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, addr))
            return false;
    }
    return true;
}

// Branch with Link and Exchange Instruction Sets (immediate) calls a subroutine
// at a PC-relative address, and changes instruction set from ARM to Thumb, or
// from Thumb to ARM.
// BLX (immediate)
bool
EmulateInstructionARM::EmulateBLXImmediate (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        if CurrentInstrSet() == InstrSet_ARM then
            LR = PC - 4;
        else
            LR = PC<31:1> : '1';
        if targetInstrSet == InstrSet_ARM then
            targetAddress = Align(PC,4) + imm32;
        else
            targetAddress = PC + imm32;
        SelectInstrSet(targetInstrSet);
        BranchWritePC(targetAddress);
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRelativeBranchImmediate;
        const uint32_t pc = ReadCoreReg(PC_REG, &success);
        if (!success)
            return false;
        addr_t lr; // next instruction address
        addr_t target; // target address
        int32_t imm32; // PC-relative offset
        switch (encoding) {
        case eEncodingT1:
            {
            lr = pc | 1u; // return address
            uint32_t S = Bit32(opcode, 26);
            uint32_t imm10 = Bits32(opcode, 25, 16);
            uint32_t J1 = Bit32(opcode, 13);
            uint32_t J2 = Bit32(opcode, 11);
            uint32_t imm11 = Bits32(opcode, 10, 0);
            uint32_t I1 = !(J1 ^ S);
            uint32_t I2 = !(J2 ^ S);
            uint32_t imm25 = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
            imm32 = llvm::SignExtend32<25>(imm25);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
            if (InITBlock() && !LastInITBlock())
                return false;
            break;
            }
        case eEncodingT2:
            {
            lr = pc | 1u; // return address
            uint32_t S = Bit32(opcode, 26);
            uint32_t imm10H = Bits32(opcode, 25, 16);
            uint32_t J1 = Bit32(opcode, 13);
            uint32_t J2 = Bit32(opcode, 11);
            uint32_t imm10L = Bits32(opcode, 10, 1);
            uint32_t I1 = !(J1 ^ S);
            uint32_t I2 = !(J2 ^ S);
            uint32_t imm25 = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10H << 12) | (imm10L << 2);
            imm32 = llvm::SignExtend32<25>(imm25);
            target = Align(pc, 4) + imm32;
            context.SetModeAndImmediateSigned (eModeARM, 4 + imm32);
            if (InITBlock() && !LastInITBlock())
                return false;
            break;
            }
        case eEncodingA1:
            lr = pc + 4; // return address
            imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2);
            target = Align(pc, 4) + imm32;
            context.SetModeAndImmediateSigned (eModeARM, 8 + imm32);
            break;
        case eEncodingA2:
            lr = pc + 4; // return address
            imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2 | Bits32(opcode, 24, 24) << 1);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 8 + imm32);
            break;
        default:
            return false;
        }
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA, lr))
            return false;
        if (!BranchWritePC(context, target))
            return false;
    }
    return true;
}

// Branch with Link and Exchange (register) calls a subroutine at an address and
// instruction set specified by a register.
// BLX (register)
bool
EmulateInstructionARM::EmulateBLXRm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        target = R[m];
        if CurrentInstrSet() == InstrSet_ARM then
            next_instr_addr = PC - 4;
            LR = next_instr_addr;
        else
            next_instr_addr = PC - 2;
            LR = next_instr_addr<31:1> : ‘1’;
        BXWritePC(target);
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextAbsoluteBranchRegister;
        const uint32_t pc = ReadCoreReg(PC_REG, &success);
        addr_t lr; // next instruction address
        if (!success)
            return false;
        uint32_t Rm; // the register with the target address
        switch (encoding) {
        case eEncodingT1:
            lr = (pc - 2) | 1u; // return address
            Rm = Bits32(opcode, 6, 3);
            // if m == 15 then UNPREDICTABLE;
            if (Rm == 15)
                return false;
            if (InITBlock() && !LastInITBlock())
                return false;
            break;
        case eEncodingA1:
            lr = pc - 4; // return address
            Rm = Bits32(opcode, 3, 0);
            // if m == 15 then UNPREDICTABLE;
            if (Rm == 15)
                return false;
            break;
        default:
            return false;
        }
        addr_t target = ReadCoreReg (Rm, &success);
        if (!success)
            return false;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + Rm);
        context.SetRegister (dwarf_reg);
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA, lr))
            return false;
        if (!BXWritePC(context, target))
            return false;
    }
    return true;
}

// Branch and Exchange causes a branch to an address and instruction set specified by a register.
// BX
bool
EmulateInstructionARM::EmulateBXRm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        BXWritePC(R[m]);
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextAbsoluteBranchRegister;
        uint32_t Rm; // the register with the target address
        switch (encoding) {
        case eEncodingT1:
            Rm = Bits32(opcode, 6, 3);
            if (InITBlock() && !LastInITBlock())
                return false;
            break;
        case eEncodingA1:
            Rm = Bits32(opcode, 3, 0);
            break;
        default:
            return false;
        }
        addr_t target = ReadCoreReg (Rm, &success);
        if (!success)
            return false;
                  
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + Rm);
        context.SetRegister (dwarf_reg);
        if (!BXWritePC(context, target))
            return false;
    }
    return true;
}

// Set r7 to point to some ip offset.
// SUB (immediate)
bool
EmulateInstructionARM::EmulateSUBR7IPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t ip = ReadCoreReg (12, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t ip_offset = imm32;
        addr_t addr = ip - ip_offset; // the adjusted ip value
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r12);
        context.SetRegisterPlusOffset (dwarf_reg, -ip_offset);                             
    
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r7, addr))
            return false;
    }
    return true;
}

// Set ip to point to some stack offset.
// SUB (SP minus immediate)
bool
EmulateInstructionARM::EmulateSUBIPSPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset; // the adjusted stack pointer value
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);
        context.SetRegisterPlusOffset (dwarf_reg, -sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r12, addr))
            return false;
    }
    return true;
}

// A sub operation to adjust the SP -- allocate space for local storage.
bool
EmulateInstructionARM::EmulateSUBSPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), ‘1’);
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t imm32;
        switch (encoding) {
        case eEncodingT1:
            imm32 = ThumbImmScaled(opcode); // imm32 = ZeroExtend(imm7:'00', 32)
            break;
        case eEncodingT2:
            imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
            break;
        case eEncodingT3:
            imm32 = ThumbImm12(opcode); // imm32 = ZeroExtend(i:imm3:imm8, 32)
            break;
        case eEncodingA1:
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset; // the adjusted stack pointer value
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (-sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, addr))
            return false;
    }
    return true;
}

// A store operation to the stack that also updates the SP.
bool
EmulateInstructionARM::EmulateSTRRtSP (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemU[address,4] = if t == 15 then PCStoreValue() else R[t];
        if wback then R[n] = offset_addr;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        uint32_t Rt; // the source register
        uint32_t imm12;
        switch (encoding) {
        case eEncodingA1:
            Rt = Bits32(opcode, 15, 12);
            imm12 = Bits32(opcode, 11, 0);
            break;
        default:
            return false;
        }
        addr_t sp_offset = imm12;
        addr_t addr = sp - sp_offset;
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextPushRegisterOnStack;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, 0);
        if (Rt != 15)
        {
            dwarf_reg.num = dwarf_r0 + Rt;
            context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
            uint32_t reg_value = ReadCoreReg(Rt, &success);
            if (!success)
                return false;
            if (!MemUWrite (context, addr, reg_value, addr_byte_size))
                return false;
        }
        else
        {
            dwarf_reg.num = dwarf_pc;
            context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
            const uint32_t pc = ReadCoreReg(PC_REG, &success);
            if (!success)
                return false;
            if (!MemUWrite (context, addr, pc + 8, addr_byte_size))
                return false;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (-sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

// Vector Push stores multiple extension registers to the stack.
// It also updates SP to point to the start of the stored data.
bool 
EmulateInstructionARM::EmulateVPUSH (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(13);
        address = SP - imm32;
        SP = SP - imm32;
        if single_regs then
            for r = 0 to regs-1
                MemA[address,4] = S[d+r]; address = address+4;
        else
            for r = 0 to regs-1
                // Store as two word-aligned words in the correct order for current endianness.
                MemA[address,4] = if BigEndian() then D[d+r]<63:32> else D[d+r]<31:0>;
                MemA[address+4,4] = if BigEndian() then D[d+r]<31:0> else D[d+r]<63:32>;
                address = address+8;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        bool single_regs;
        uint32_t d;     // UInt(D:Vd) or UInt(Vd:D) starting register
        uint32_t imm32; // stack offset
        uint32_t regs;  // number of registers
        switch (encoding) {
        case eEncodingT1:
        case eEncodingA1:
            single_regs = false;
            d = Bit32(opcode, 22) << 4 | Bits32(opcode, 15, 12);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            // If UInt(imm8) is odd, see "FSTMX".
            regs = Bits32(opcode, 7, 0) / 2;
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        case eEncodingT2:
        case eEncodingA2:
            single_regs = true;
            d = Bits32(opcode, 15, 12) << 1 | Bit32(opcode, 22);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            regs = Bits32(opcode, 7, 0);
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        default:
            return false;
        }
        uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;
        uint32_t reg_byte_size = single_regs ? addr_byte_size : addr_byte_size * 2;
        addr_t sp_offset = imm32;
        addr_t addr = sp - sp_offset;
        uint32_t i;
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextPushRegisterOnStack;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, 0);
        for (i=d; i<regs; ++i)
        {
            dwarf_reg.num = start_reg + i;
            context.SetRegisterPlusOffset ( dwarf_reg, addr - sp);
            // uint64_t to accommodate 64-bit registers.
            uint64_t reg_value = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_reg.num, 0, &success);
            if (!success)
                return false;
            if (!MemAWrite (context, addr, reg_value, reg_byte_size))
                return false;
            addr += reg_byte_size;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (-sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
            return false;
    }
    return true;
}

// Vector Pop loads multiple extension registers from the stack.
// It also updates SP to point just above the loaded data.
bool 
EmulateInstructionARM::EmulateVPOP (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(13);
        address = SP;
        SP = SP + imm32;
        if single_regs then
            for r = 0 to regs-1
                S[d+r] = MemA[address,4]; address = address+4;
        else
            for r = 0 to regs-1
                word1 = MemA[address,4]; word2 = MemA[address+4,4]; address = address+8;
                // Combine the word-aligned words in the correct order for current endianness.
                D[d+r] = if BigEndian() then word1:word2 else word2:word1;
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
        const addr_t sp = ReadCoreReg (SP_REG, &success);
        if (!success)
            return false;
        bool single_regs;
        uint32_t d;     // UInt(D:Vd) or UInt(Vd:D) starting register
        uint32_t imm32; // stack offset
        uint32_t regs;  // number of registers
        switch (encoding) {
        case eEncodingT1:
        case eEncodingA1:
            single_regs = false;
            d = Bit32(opcode, 22) << 4 | Bits32(opcode, 15, 12);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            // If UInt(imm8) is odd, see "FLDMX".
            regs = Bits32(opcode, 7, 0) / 2;
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        case eEncodingT2:
        case eEncodingA2:
            single_regs = true;
            d = Bits32(opcode, 15, 12) << 1 | Bit32(opcode, 22);
            imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
            regs = Bits32(opcode, 7, 0);
            // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
            if (regs == 0 || regs > 16 || (d + regs) > 32)
                return false;
            break;
        default:
            return false;
        }
        uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;
        uint32_t reg_byte_size = single_regs ? addr_byte_size : addr_byte_size * 2;
        addr_t sp_offset = imm32;
        addr_t addr = sp;
        uint32_t i;
        uint64_t data; // uint64_t to accomodate 64-bit registers.
        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextPopRegisterOffStack;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, 0);
        for (i=d; i<regs; ++i)
        {
            dwarf_reg.num = start_reg + i;
            context.SetRegisterPlusOffset (dwarf_reg, addr - sp);
            data = MemARead(context, addr, reg_byte_size, 0, &success);
            if (!success)
                return false;    
            if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_reg.num, data))
                return false;
            addr += reg_byte_size;
        }
        
        context.type = EmulateInstruction::eContextAdjustStackPointer;
        context.SetImmediateSigned (sp_offset);
    
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, sp + sp_offset))
            return false;
    }
    return true;
}

// SVC (previously SWI)
bool
EmulateInstructionARM::EmulateSVC (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        CallSupervisor();
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        const uint32_t pc = ReadCoreReg(PC_REG, &success);
        addr_t lr; // next instruction address
        if (!success)
            return false;
        uint32_t imm32; // the immediate constant
        uint32_t mode;  // ARM or Thumb mode
        switch (encoding) {
        case eEncodingT1:
            lr = (pc + 2) | 1u; // return address
            imm32 = Bits32(opcode, 7, 0);
            mode = eModeThumb;
            break;
        case eEncodingA1:
            lr = pc + 4; // return address
            imm32 = Bits32(opcode, 23, 0);
            mode = eModeARM;
            break;
        default:
            return false;
        }
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextSupervisorCall;
        context.SetModeAndImmediate (mode, imm32);
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_RA, lr))
            return false;
    }
    return true;
}

// If Then makes up to four following instructions (the IT block) conditional.
bool
EmulateInstructionARM::EmulateIT (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations();
    ITSTATE.IT<7:0> = firstcond:mask;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    m_it_session.InitIT(Bits32(opcode, 7, 0));
    return true;
}

// Branch causes a branch to a target address.
bool
EmulateInstructionARM::EmulateB (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        BranchWritePC(PC + imm32);
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRelativeBranchImmediate;
        const uint32_t pc = ReadCoreReg(PC_REG, &success);
        if (!success)
            return false;
        addr_t target; // target address
        int32_t imm32; // PC-relative offset
        switch (encoding) {
        case eEncodingT1:
            // The 'cond' field is handled in EmulateInstructionARM::CurrentCond().
            imm32 = llvm::SignExtend32<9>(Bits32(opcode, 7, 0) << 1);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
            break;
        case eEncodingT2:
            imm32 = llvm::SignExtend32<12>(Bits32(opcode, 10, 0));
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
            break;
        case eEncodingT3:
            // The 'cond' field is handled in EmulateInstructionARM::CurrentCond().
            {
            uint32_t S = Bit32(opcode, 26);
            uint32_t imm6 = Bits32(opcode, 21, 16);
            uint32_t J1 = Bit32(opcode, 13);
            uint32_t J2 = Bit32(opcode, 11);
            uint32_t imm11 = Bits32(opcode, 10, 0);
            uint32_t imm21 = (S << 20) | (J2 << 19) | (J1 << 18) | (imm6 << 12) | (imm11 << 1);
            imm32 = llvm::SignExtend32<21>(imm21);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
            break;
            }
        case eEncodingT4:
            {
            uint32_t S = Bit32(opcode, 26);
            uint32_t imm10 = Bits32(opcode, 25, 16);
            uint32_t J1 = Bit32(opcode, 13);
            uint32_t J2 = Bit32(opcode, 11);
            uint32_t imm11 = Bits32(opcode, 10, 0);
            uint32_t I1 = !(J1 ^ S);
            uint32_t I2 = !(J2 ^ S);
            uint32_t imm25 = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
            imm32 = llvm::SignExtend32<25>(imm25);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
            break;
            }
        case eEncodingA1:
            imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2);
            target = pc + imm32;
            context.SetModeAndImmediateSigned (eModeARM, 8 + imm32);
            break;
        default:
            return false;
        }
        if (!BranchWritePC(context, target))
            return false;
    }
    return true;
}

// Compare and Branch on Nonzero and Compare and Branch on Zero compare the value in a register with
// zero and conditionally branch forward a constant value.  They do not affect the condition flags.
// CBNZ, CBZ
bool
EmulateInstructionARM::EmulateCB (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations();
    if nonzero ^ IsZero(R[n]) then
        BranchWritePC(PC + imm32);
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Bits32(opcode, 2, 0), &success);
    if (!success)
        return false;
                  
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRelativeBranchImmediate;
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
        return false;

    addr_t target;  // target address
    uint32_t imm32; // PC-relative offset to branch forward
    bool nonzero;
    switch (encoding) {
    case eEncodingT1:
        imm32 = Bit32(opcode, 9) << 6 | Bits32(opcode, 7, 3) << 1;
        nonzero = BitIsSet(opcode, 11);
        target = pc + imm32;
        context.SetModeAndImmediateSigned (eModeThumb, 4 + imm32);
        break;
    default:
        return false;
    }
    if (nonzero ^ (reg_val == 0))
        if (!BranchWritePC(context, target))
            return false;

    return true;
}

// Table Branch Byte causes a PC-relative forward branch using a table of single byte offsets.
// A base register provides a pointer to the table, and a second register supplies an index into the table.
// The branch length is twice the value of the byte returned from the table.
//
// Table Branch Halfword causes a PC-relative forward branch using a table of single halfword offsets.
// A base register provides a pointer to the table, and a second register supplies an index into the table.
// The branch length is twice the value of the halfword returned from the table.
// TBB, TBH
bool
EmulateInstructionARM::EmulateTB (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    if is_tbh then
        halfwords = UInt(MemU[R[n]+LSL(R[m],1), 2]);
    else
        halfwords = UInt(MemU[R[n]+R[m], 1]);
    BranchWritePC(PC + 2*halfwords);
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rn;     // the base register which contains the address of the table of branch lengths
    uint32_t Rm;     // the index register which contains an integer pointing to a byte/halfword in the table
    bool is_tbh;     // true if table branch halfword
    switch (encoding) {
    case eEncodingT1:
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        is_tbh = BitIsSet(opcode, 4);
        if (Rn == 13 || BadReg(Rm))
            return false;
        if (InITBlock() && !LastInITBlock())
            return false;
        break;
    default:
        return false;
    }

    // Read the address of the table from the operand register Rn.
    // The PC can be used, in which case the table immediately follows this instruction.
    uint32_t base = ReadCoreReg(Rm, &success);
    if (!success)
        return false;

    // the table index
    uint32_t index = ReadCoreReg(Rm, &success);
    if (!success)
        return false;

    // the offsetted table address
    addr_t addr = base + (is_tbh ? index*2 : index);

    // PC-relative offset to branch forward
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextTableBranchReadMemory;
    uint32_t offset = MemURead(context, addr, is_tbh ? 2 : 1, 0, &success) * 2;
    if (!success)
        return false;

    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
        return false;

    // target address
    addr_t target = pc + offset;
    context.type = EmulateInstruction::eContextRelativeBranchImmediate;
    context.SetModeAndImmediateSigned (eModeThumb, 4 + offset);

    if (!BranchWritePC(context, target))
        return false;

    return true;
}

// This instruction adds an immediate value to a register value, and writes the result to the destination
// register.  It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateADDImmARM (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn;
        uint32_t imm32; // the immediate value to be added to the value obtained from Rn
        bool setflags;
        switch (encoding)
        {
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        AddWithCarryResult res = AddWithCarry(val1, imm32, 0);

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
            return false;
    }
    return true;
}

// This instruction adds a register value and an optionally-shifted register value, and writes the result
// to the destination register. It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateADDReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Bits32(opcode, 2, 0);
            Rn = Bits32(opcode, 5, 3);
            Rm = Bits32(opcode, 8, 6);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rd = Rn = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 6, 3);
            setflags = false;
            shift_t = SRType_LSL;
            shift_n = 0;
            if (Rn == 15 && Rm == 15)
                return false;
            if (Rd == 15 && InITBlock() && !LastInITBlock())
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
        AddWithCarryResult res = AddWithCarry(val1, shifted, 0);

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
            return false;
    }
    return true;
}

// Compare Negative (immediate) adds a register value and an immediate value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateCMNImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rn; // the first operand
    uint32_t imm32; // the immediate value to be compared with
    switch (encoding) {
    case eEncodingT1:
        Rn = Bits32(opcode, 19, 16);
        imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
        if (Rn == 15)
            return false;
        break;
    case eEncodingA1:
        Rn = Bits32(opcode, 19, 16);
        imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
        break;
    default:
        return false;
    }
    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Rn, &success);
    if (!success)
        return false;
                  
    AddWithCarryResult res = AddWithCarry(reg_val, imm32, 0);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs ();
    if (!WriteFlags(context, res.result, res.carry_out, res.overflow))
        return false;

    return true;
}

// Compare Negative (register) adds a register value and an optionally-shifted register value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateCMNReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rn; // the first operand
    uint32_t Rm; // the second operand
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    switch (encoding) {
    case eEncodingT1:
        Rn = Bits32(opcode, 2, 0);
        Rm = Bits32(opcode, 5, 3);
        shift_t = SRType_LSL;
        shift_n = 0;
        break;
    case eEncodingT2:
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        shift_n = DecodeImmShiftThumb(opcode, shift_t);
        // if n == 15 || BadReg(m) then UNPREDICTABLE;
        if (Rn == 15 || BadReg(Rm))
            return false;
        break;
    case eEncodingA1:
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        shift_n = DecodeImmShiftARM(opcode, shift_t);
        break;
    default:
        return false;
    }
    // Read the register value from register Rn.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
        return false;

    // Read the register value from register Rm.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
        return false;
                  
    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
    AddWithCarryResult res = AddWithCarry(val1, shifted, 0);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();
    if (!WriteFlags(context, res.result, res.carry_out, res.overflow))
        return false;

    return true;
}

// Compare (immediate) subtracts an immediate value from a register value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateCMPImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), '1');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rn; // the first operand
    uint32_t imm32; // the immediate value to be compared with
    switch (encoding) {
    case eEncodingT1:
        Rn = Bits32(opcode, 10, 8);
        imm32 = Bits32(opcode, 7, 0);
        break;
    case eEncodingT2:
        Rn = Bits32(opcode, 19, 16);
        imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
        if (Rn == 15)
            return false;
        break;
    case eEncodingA1:
        Rn = Bits32(opcode, 19, 16);
        imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
        break;
    default:
        return false;
    }
    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Rn, &success);
    if (!success)
        return false;
                  
    AddWithCarryResult res = AddWithCarry(reg_val, ~imm32, 1);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs ();
    if (!WriteFlags(context, res.result, res.carry_out, res.overflow))
        return false;

    return true;
}

// Compare (register) subtracts an optionally-shifted register value from a register value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateCMPReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), '1');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rn; // the first operand
    uint32_t Rm; // the second operand
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    switch (encoding) {
    case eEncodingT1:
        Rn = Bits32(opcode, 2, 0);
        Rm = Bits32(opcode, 5, 3);
        shift_t = SRType_LSL;
        shift_n = 0;
        break;
    case eEncodingT2:
        Rn = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
        Rm = Bits32(opcode, 6, 3);
        shift_t = SRType_LSL;
        shift_n = 0;
        if (Rn < 8 && Rm < 8)
            return false;
        if (Rn == 15 || Rm == 15)
            return false;
        break;
    case eEncodingA1:
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        shift_n = DecodeImmShiftARM(opcode, shift_t);
        break;
    default:
        return false;
    }
    // Read the register value from register Rn.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
        return false;

    // Read the register value from register Rm.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
        return false;
                  
    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
    AddWithCarryResult res = AddWithCarry(val1, ~shifted, 1);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();
    if (!WriteFlags(context, res.result, res.carry_out, res.overflow))
        return false;

    return true;
}

// Arithmetic Shift Right (immediate) shifts a register value right by an immediate number of bits,
// shifting in copies of its sign bit, and writes the result to the destination register.  It can
// optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateASRImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_ASR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    return EmulateShiftImm(encoding, SRType_ASR);
}

// Arithmetic Shift Right (register) shifts a register value right by a variable number of bits,
// shifting in copies of its sign bit, and writes the result to the destination register.
// The variable number of bits is read from the bottom byte of a register. It can optionally update
// the condition flags based on the result.
bool
EmulateInstructionARM::EmulateASRReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_ASR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

    return EmulateShiftReg(encoding, SRType_ASR);
}

// Logical Shift Left (immediate) shifts a register value left by an immediate number of bits,
// shifting in zeros, and writes the result to the destination register.  It can optionally
// update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateLSLImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_LSL, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    return EmulateShiftImm(encoding, SRType_LSL);
}

// Logical Shift Left (register) shifts a register value left by a variable number of bits,
// shifting in zeros, and writes the result to the destination register.  The variable number
// of bits is read from the bottom byte of a register. It can optionally update the condition
// flags based on the result.
bool
EmulateInstructionARM::EmulateLSLReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_LSL, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

    return EmulateShiftReg(encoding, SRType_LSL);
}

// Logical Shift Right (immediate) shifts a register value right by an immediate number of bits,
// shifting in zeros, and writes the result to the destination register.  It can optionally
// update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateLSRImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_LSR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    return EmulateShiftImm(encoding, SRType_LSR);
}

// Logical Shift Right (register) shifts a register value right by a variable number of bits,
// shifting in zeros, and writes the result to the destination register.  The variable number
// of bits is read from the bottom byte of a register. It can optionally update the condition
// flags based on the result.
bool
EmulateInstructionARM::EmulateLSRReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_LSR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

    return EmulateShiftReg(encoding, SRType_LSR);
}

// Rotate Right (immediate) provides the value of the contents of a register rotated by a constant value.
// The bits that are rotated off the right end are inserted into the vacated bit positions on the left.
// It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateRORImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_ROR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    return EmulateShiftImm(encoding, SRType_ROR);
}

// Rotate Right (register) provides the value of the contents of a register rotated by a variable number of bits.
// The bits that are rotated off the right end are inserted into the vacated bit positions on the left.
// The variable number of bits is read from the bottom byte of a register. It can optionally update the condition
// flags based on the result.
bool
EmulateInstructionARM::EmulateRORReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_ROR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

    return EmulateShiftReg(encoding, SRType_ROR);
}

// Rotate Right with Extend provides the value of the contents of a register shifted right by one place,
// with the carry flag shifted into bit [31].
//
// RRX can optionally update the condition flags based on the result.
// In that case, bit [0] is shifted into the carry flag.
bool
EmulateInstructionARM::EmulateRRX (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_RRX, 1, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    return EmulateShiftImm(encoding, SRType_RRX);
}

bool
EmulateInstructionARM::EmulateShiftImm (ARMEncoding encoding, ARM_ShifterType shift_type)
{
    assert(shift_type == SRType_ASR || shift_type == SRType_LSL || shift_type == SRType_LSR);

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd;    // the destination register
        uint32_t Rm;    // the first operand register
        uint32_t imm5;  // encoding for the shift amount
        uint32_t carry; // the carry bit after the shift operation
        bool setflags;

        // Special case handling!
        // A8.6.139 ROR (immediate) -- Encoding T1
        if (shift_type == SRType_ROR && encoding == eEncodingT1)
        {
            // Morph the T1 encoding from the ARM Architecture Manual into T2 encoding to
            // have the same decoding of bit fields as the other Thumb2 shift operations.
            encoding = eEncodingT2;
        }

        switch (encoding) {
        case eEncodingT1:
            // Due to the above special case handling!
            assert(shift_type != SRType_ROR);

            Rd = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            imm5 = Bits32(opcode, 10, 6);
            break;
        case eEncodingT2:
            // A8.6.141 RRX
            assert(shift_type != SRType_RRX);

            Rd = Bits32(opcode, 11, 8);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            imm5 = Bits32(opcode, 14, 12) << 2 | Bits32(opcode, 7, 6);
            if (BadReg(Rd) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            imm5 = Bits32(opcode, 11, 7);
            break;
        default:
            return false;
        }

        // A8.6.139 ROR (immediate)
        if (shift_type == SRType_ROR && imm5 == 0)
            shift_type = SRType_RRX;

        // Get the first operand.
        uint32_t value = ReadCoreReg (Rm, &success);
        if (!success)
            return false;

        // Decode the shift amount if not RRX.
        uint32_t amt = (shift_type == SRType_RRX ? 1 : DecodeImmShift(shift_type, imm5));

        uint32_t result = Shift_C(value, shift_type, amt, APSR_C, carry);

        // The context specifies that an immediate is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();
     
        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

bool
EmulateInstructionARM::EmulateShiftReg (ARMEncoding encoding, ARM_ShifterType shift_type)
{
    assert(shift_type == SRType_ASR || shift_type == SRType_LSL || shift_type == SRType_LSR);

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd;    // the destination register
        uint32_t Rn;    // the first operand register
        uint32_t Rm;    // the register whose bottom byte contains the amount to shift by
        uint32_t carry; // the carry bit after the shift operation
        bool setflags;
        switch (encoding) {
        case eEncodingT1:
            Rd = Bits32(opcode, 2, 0);
            Rn = Rd;
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 3, 0);
            Rm = Bits32(opcode, 11, 8);
            setflags = BitIsSet(opcode, 20);
            if (Rd == 15 || Rn == 15 || Rm == 15)
                return false;
            break;
        default:
            return false;
        }

        // Get the first operand.
        uint32_t value = ReadCoreReg (Rn, &success);
        if (!success)
            return false;
        // Get the Rm register content.
        uint32_t val = ReadCoreReg (Rm, &success);
        if (!success)
            return false;

        // Get the shift amount.
        uint32_t amt = Bits32(val, 7, 0);

        uint32_t result = Shift_C(value, shift_type, amt, APSR_C, carry);

        // The context specifies that an immediate is to be moved into Rd.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();
     
        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// LDM loads multiple registers from consecutive memory locations, using an
// address from a base register.  Optionally the address just above the highest of those locations
// can be written back to the base register.
bool
EmulateInstructionARM::EmulateLDM (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed()
        EncodingSpecificOperations(); NullCheckIfThumbEE (n);
        address = R[n];
                  
        for i = 0 to 14
            if registers<i> == '1' then
                R[i] = MemA[address, 4]; address = address + 4;
        if registers<15> == '1' then
            LoadWritePC (MemA[address, 4]);
                  
        if wback && registers<n> == '0' then R[n] = R[n] + 4 * BitCount (registers);
        if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1

#endif
            
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
            
    if (ConditionPassed())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
        switch (encoding)
        {
            case eEncodingT1:
                // n = UInt(Rn); registers = ’00000000’:register_list; wback = (registers<n> == ’0’);
                n = Bits32 (opcode, 10, 8);
                registers = Bits32 (opcode, 7, 0);
                registers = registers & 0x00ff;  // Make sure the top 8 bits are zeros.
                wback = BitIsClear (registers, n);
                // if BitCount(registers) < 1 then UNPREDICTABLE;
                if (BitCount(registers) < 1)
                    return false;
                break;
            case eEncodingT2:
                // if W == ’1’ && Rn == ’1101’ then SEE POP; 
                // n = UInt(Rn); registers = P:M:’0’:register_list; wback = (W == ’1’); 
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                registers = registers & 0xdfff; // Make sure bit 13 is zero.
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 2 || (P == ’1’ && M == ’1’) then UNPREDICTABLE; 
                if ((n == 15)
                    || (BitCount (registers) < 2)
                    || (BitIsSet (opcode, 14) && BitIsSet (opcode, 15)))
                    return false;
                  
                // if registers<15> == ’1’ && InITBlock() && !LastInITBlock() then UNPREDICTABLE; 
                if (BitIsSet (registers, 15) && InITBlock() && !LastInITBlock())
                    return false;

                // if wback && registers<n> == ’1’ then UNPREDICTABLE;
                if (wback
                    && BitIsSet (registers, n))
                    return false;
                break;
                  
            case eEncodingA1:
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                if ((n == 15)
                    || (BitCount (registers) < 1))
                    return false;
                break;
            default:
                return false;
        }
        
        int32_t offset = 0;
        const addr_t base_address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        context.SetRegisterPlusOffset (dwarf_reg, offset);
                  
        for (int i = 0; i < 14; ++i)
        {
            if (BitIsSet (registers, i))
            {
                context.type = EmulateInstruction::eContextRegisterPlusOffset;
                context.SetRegisterPlusOffset (dwarf_reg, offset);
                if (wback && (n == 13)) // Pop Instruction
                    context.type = EmulateInstruction::eContextPopRegisterOffStack;

                // R[i] = MemA [address, 4]; address = address + 4;
                uint32_t data = MemARead (context, base_address + offset, addr_byte_size, 0, &success);
                if (!success)
                    return false;
                  
                if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + i, data))
                    return false;

                offset += addr_byte_size;
            }
        }
                
        if (BitIsSet (registers, 15))
        {
            //LoadWritePC (MemA [address, 4]);
            context.type = EmulateInstruction::eContextRegisterPlusOffset;
            context.SetRegisterPlusOffset (dwarf_reg, offset);
            uint32_t data = MemARead (context, base_address + offset, addr_byte_size, 0, &success);
            if (!success)
                return false;
            // In ARMv5T and above, this is an interworking branch.
            if (!LoadWritePC(context, data))
                return false;
        }
                             
        if (wback && BitIsClear (registers, n))
        {
            // R[n] = R[n] + 4 * BitCount (registers)
            int32_t offset = addr_byte_size * BitCount (registers);
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetRegisterPlusOffset (dwarf_reg, offset);
                
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, base_address + offset))
                return false;
        }
        if (wback && BitIsSet (registers, n))
            // R[n] bits(32) UNKNOWN;
            return WriteBits32Unknown (n);
    }
    return true;
}
                
// LDMDA loads multiple registers from consecutive memory locations using an address from a base registers.
// The consecutive memorty locations end at this address and the address just below the lowest of those locations
// can optionally be written back tot he base registers.
bool
EmulateInstructionARM::EmulateLDMDA (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        address = R[n] - 4*BitCount(registers) + 4;
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                  R[i] = MemA[address,4]; address = address + 4; 
                  
        if registers<15> == ’1’ then
            LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == ’0’ then R[n] = R[n] - 4*BitCount(registers); 
        if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        // EncodingSpecificOperations(); 
        switch (encoding)
        {
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 1))
                    return false;
                  
                break;

            default:
                return false;
        }
        // address = R[n] - 4*BitCount(registers) + 4;
                  
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
                  
        if (!success)
            return false;
            
        address = address - (addr_byte_size * BitCount (registers)) + addr_byte_size;
                                                        
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        context.SetRegisterPlusOffset (dwarf_reg, offset);
                  
        // for i = 0 to 14 
        for (int i = 0; i < 14; ++i)
        {
            // if registers<i> == ’1’ then
            if (BitIsSet (registers, i))
            {
                  // R[i] = MemA[address,4]; address = address + 4; 
                  context.SetRegisterPlusOffset (dwarf_reg, offset);
                  uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
                  if (!success)
                      return false;
                  if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + i, data))
                      return false;
                  offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then
        //     LoadWritePC(MemA[address,4]);
        if (BitIsSet (registers, 15))
        {
            context.SetRegisterPlusOffset (dwarf_reg, offset);
            uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
            if (!success)
                return false;
            // In ARMv5T and above, this is an interworking branch.
            if (!LoadWritePC(context, data))
                return false;
        }
                  
        // if wback && registers<n> == ’0’ then R[n] = R[n] - 4*BitCount(registers); 
        if (wback && BitIsClear (registers, n))
        {
            addr_t addr = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
            if (!success)
                return false;

            offset = (addr_byte_size * BitCount (registers)) * -1;
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);      
            addr = addr + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, addr))
                return false;
        }
                  
        // if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN;
        if (wback && BitIsSet (registers, n))
            return WriteBits32Unknown (n);
    }
    return true;
}
  
// LDMDB loads multiple registers from consecutive memory locations using an address from a base register.  The 
// consecutive memory lcoations end just below this address, and the address of the lowest of those locations can 
// be optionally written back to the base register.
bool
EmulateInstructionARM::EmulateLDMDB (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n] - 4*BitCount(registers);
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                  R[i] = MemA[address,4]; address = address + 4; 
        if registers<15> == ’1’ then
                  LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == ’0’ then R[n] = R[n] - 4*BitCount(registers); 
        if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
        switch (encoding)
        {
            case eEncodingT1:
                // n = UInt(Rn); registers = P:M:’0’:register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                registers = registers & 0xdfff;  // Make sure bit 13 is a zero.
                wback = BitIsSet (opcode, 21);

                // if n == 15 || BitCount(registers) < 2 || (P == ’1’ && M == ’1’) then UNPREDICTABLE;
                if ((n == 15)
                    || (BitCount (registers) < 2)
                    || (BitIsSet (opcode, 14) && BitIsSet (opcode, 15)))
                    return false;

                // if registers<15> == ’1’ && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
                if (BitIsSet (registers, 15) && InITBlock() && !LastInITBlock())
                    return false;

                // if wback && registers<n> == ’1’ then UNPREDICTABLE;
                if (wback && BitIsSet (registers, n))
                    return false;
                  
                break;
                  
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 1))
                    return false;
                  
                break;
                  
            default:
                return false;
        }
                  
        // address = R[n] - 4*BitCount(registers);
                  
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
                  
        if (!success)
            return false;
                  
        address = address - (addr_byte_size * BitCount (registers));
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        context.SetRegisterPlusOffset (dwarf_reg, offset);
                  
        for (int i = 0; i < 14; ++i)
        {
            if (BitIsSet (registers, i))
            {
                // R[i] = MemA[address,4]; address = address + 4;
                context.SetRegisterPlusOffset (dwarf_reg, offset);
                uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
                if (!success)
                    return false;
                  
                if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + i, data))
                    return false;
                  
                offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then
        //     LoadWritePC(MemA[address,4]);
        if (BitIsSet (registers, 15))
        {
            context.SetRegisterPlusOffset (dwarf_reg, offset);
            uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
            if (!success)
                return false;
            // In ARMv5T and above, this is an interworking branch.
            if (!LoadWritePC(context, data))
                return false;
        }
                  
        // if wback && registers<n> == ’0’ then R[n] = R[n] - 4*BitCount(registers);
        if (wback && BitIsClear (registers, n))
        {
            addr_t addr = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
            if (!success)
                return false;
            
            offset = (addr_byte_size * BitCount (registers)) * -1;
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr = addr + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, addr))
                return false;
        }
                  
        // if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1
        if (wback && BitIsSet (registers, n))
            return WriteBits32Unknown (n);
    }
    return true;
}

// LDMIB loads multiple registers from consecutive memory locations using an address from a base register.  The 
// consecutive memory locations start just above this address, and thea ddress of the last of those locations can 
// optinoally be written back to the base register.
bool
EmulateInstructionARM::EmulateLDMIB (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); 
        address = R[n] + 4;
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                  R[i] = MemA[address,4]; address = address + 4; 
        if registers<15> == ’1’ then
            LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == ’0’ then R[n] = R[n] + 4*BitCount(registers); 
        if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
        switch (encoding)
        {
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 1))
                    return false;
                  
                break;
            default:
                return false;
        }
        // address = R[n] + 4;
                  
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
                  
        if (!success)
            return false;
                  
        address = address + addr_byte_size;
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        Register dwarf_reg;
        dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        context.SetRegisterPlusOffset (dwarf_reg, offset);

        for (int i = 0; i < 14; ++i)
        {
            if (BitIsSet (registers, i))
            {
                // R[i] = MemA[address,4]; address = address + 4;
                
                context.SetRegisterPlusOffset (dwarf_reg, offset);
                uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
                if (!success)
                    return false;
                  
                if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + i, data))
                    return false;
                  
                offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then
        //     LoadWritePC(MemA[address,4]);
        if (BitIsSet (registers, 15))
        {
            context.SetRegisterPlusOffset (dwarf_reg, offset);
            uint32_t data = MemARead (context, address + offset, addr_byte_size, 0, &success);
            if (!success)
                return false;
            // In ARMv5T and above, this is an interworking branch.
            if (!LoadWritePC(context, data))
                return false;
        }
                  
        // if wback && registers<n> == ’0’ then R[n] = R[n] + 4*BitCount(registers);
        if (wback && BitIsClear (registers, n))
        {
            addr_t addr = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
            if (!success)
                return false;

            offset = addr_byte_size * BitCount (registers);
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr = addr + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, addr))
                return false;
        }
                  
        // if wback && registers<n> == ’1’ then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1
        if (wback && BitIsSet (registers, n))
            return WriteBits32Unknown (n);
    }
    return true;
}
                  
// Load Register (immediate) calculates an address from a base register value and
// an immediate offset, loads a word from memory, and writes to a register.
// LDR (immediate, Thumb)
bool
EmulateInstructionARM::EmulateLDRRtRnImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(15);
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        data = MemU[address,4];
        if wback then R[n] = offset_addr;
        if t == 15 then
            if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE;
        elsif UnalignedSupport() || address<1:0> = '00' then
            R[t] = data;
        else R[t] = bits(32) UNKNOWN; // Can only apply before ARMv7
    }
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rt; // the destination register
        uint32_t Rn; // the base register
        uint32_t imm32; // the immediate offset used to form the address
        addr_t offset_addr; // the offset address
        addr_t address; // the calculated address
        uint32_t data; // the literal data value from memory load
        bool add, index, wback;
        switch (encoding) {
        case eEncodingT1:
            Rt = Bits32(opcode, 5, 3);
            Rn = Bits32(opcode, 2, 0);
            imm32 = Bits32(opcode, 10, 6) << 2; // imm32 = ZeroExtend(imm5:'00', 32);
            // index = TRUE; add = TRUE; wback = FALSE
            add = true;
            index = true;
            wback = false;
            break;
        default:
            return false;
        }
        uint32_t base = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + Rn, 0, &success);
        if (!success)
            return false;
        if (add)
            offset_addr = base + imm32;
        else
            offset_addr = base - imm32;

        address = (index ? offset_addr : base);

        if (wback)
        {
            EmulateInstruction::Context ctx;
            ctx.type = EmulateInstruction::eContextRegisterPlusOffset;
            Register dwarf_reg;
            dwarf_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + Rn);
            ctx.SetRegisterPlusOffset (dwarf_reg, (int32_t) (offset_addr - base));

            if (!WriteRegisterUnsigned (ctx, eRegisterKindDWARF, dwarf_r0 + Rn, offset_addr))
                return false;
        }

        // Prepare to write to the Rt register.
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        // Read memory from the address.
        data = MemURead(context, address, 4, 0, &success);
        if (!success)
            return false;    

        if (Rt == 15)
        {
            if (Bits32(address, 1, 0) == 0)
            {
                if (!LoadWritePC(context, data))
                    return false;
            }
            else
                return false;
        }
        else if (UnalignedSupport() || Bits32(address, 1, 0) == 0)
        {
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rt, data))
                return false;
        }
        else
            return false;
    }
    return true;
}

// STM (Store Multiple Increment After) stores multiple registers to consecutive memory locations using an address 
// from a base register.  The consecutive memory locations start at this address, and teh address just above the last
// of those locations can optionally be written back to the base register.
bool
EmulateInstructionARM::EmulateSTM (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n];
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN; // Only possible for encodings T1 and A1
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == ’1’ then // Only possible for encoding A1 
            MemA[address,4] = PCStoreValue();
        if wback then R[n] = R[n] + 4*BitCount(registers);
#endif
    
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        // EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        switch (encoding)
        {
            case eEncodingT1:
                // n = UInt(Rn); registers = ’00000000’:register_list; wback = TRUE;
                n = Bits32 (opcode, 10, 8);
                registers = Bits32 (opcode, 7, 0);
                registers = registers & 0x00ff;  // Make sure the top 8 bits are zeros.
                wback = true;
                  
                // if BitCount(registers) < 1 then UNPREDICTABLE;
                if (BitCount (registers) < 1)
                    return false;
                  
                break;
                  
            case eEncodingT2:
                // n = UInt(Rn); registers = ’0’:M:’0’:register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                registers = registers & 0x5fff; // Make sure bits 15 & 13 are zeros.
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 2 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 2))
                    return false;
                  
                // if wback && registers<n> == ’1’ then UNPREDICTABLE;
                if (wback && BitIsSet (registers, n))
                    return false;
                  
                break;
                  
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 1))
                    return false;
                  
                break;
                  
            default:
                return false;
        }
        
        // address = R[n];
        int32_t offset = 0;
        const addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterStore;
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                  
        // for i = 0 to 14
        for (int i = 0; i < 14; ++i)
        {
            int lowest_set_bit = 14;
            // if registers<i> == ’1’ then
            if (BitIsSet (registers, i))
            {
                  if (i < lowest_set_bit)
                      lowest_set_bit = i;
                  // if i == n && wback && i != LowestSetBit(registers) then 
                  if ((i == n) && wback && (i != lowest_set_bit))
                      // MemA[address,4] = bits(32) UNKNOWN; // Only possible for encodings T1 and A1
                      WriteBits32UnknownToMemory (address + offset);
                  else
                  {
                     // MemA[address,4] = R[i];
                      uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + i, 0, &success);
                      if (!success)
                          return false;
                  
                      Register data_reg;
                      data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + i);
                      context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, offset);
                      if (!MemAWrite (context, address + offset, data, addr_byte_size))
                          return false;
                  }
                  
                  // address = address + 4;
                  offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then // Only possible for encoding A1 
        //     MemA[address,4] = PCStoreValue();
        if (BitIsSet (registers, 15))
        {
            Register pc_reg;
            pc_reg.SetRegister (eRegisterKindDWARF, dwarf_pc);
            context.SetRegisterPlusOffset (pc_reg, 8);
            const uint32_t pc = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
                return false;
                  
            if (!MemAWrite (context, address + offset, pc + 8, addr_byte_size))
                return false;
        }
                  
        // if wback then R[n] = R[n] + 4*BitCount(registers);
        if (wback)
        {
            offset = addr_byte_size * BitCount (registers);
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr_t data = address + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, data))
                return false;
        }
    }
    return true;
}

// STMDA (Store Multiple Decrement After) stores multiple registers to consecutive memory locations using an address
// from a base register.  The consecutive memory locations end at this address, and the address just below the lowest
// of those locations can optionally be written back to the base register.
bool
EmulateInstructionARM::EmulateSTMDA (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations();                   
        address = R[n] - 4*BitCount(registers) + 4;
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == ’1’ then 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] - 4*BitCount(registers);
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        // EncodingSpecificOperations();
        switch (encoding)
        {
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || (BitCount (registers) < 1))
                    return false;
                break;
            default:
                return false;
        }
                  
        // address = R[n] - 4*BitCount(registers) + 4;
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        address = address - (addr_byte_size * BitCount (registers)) + 4;
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterStore;
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                  
        // for i = 0 to 14 
        for (int i = 0; i < 14; ++i)
        {
            int lowest_bit_set = 14;
            // if registers<i> == ’1’ then
            if (BitIsSet (registers, i))
            {
                if (i < lowest_bit_set)
                    lowest_bit_set = i;
                //if i == n && wback && i != LowestSetBit(registers) then
                if ((i == n) && wback && (i != lowest_bit_set))
                    // MemA[address,4] = bits(32) UNKNOWN;
                    WriteBits32UnknownToMemory (address + offset);
                else
                {
                    // MemA[address,4] = R[i];
                    uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + i, 0, &success);
                    if (!success)
                        return false;
                  
                    Register data_reg;
                    data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + i);
                    context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, offset);
                    if (!MemAWrite (context, address + offset, data, addr_byte_size))
                        return false;
                }
                  
                // address = address + 4;
                offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then 
        //    MemA[address,4] = PCStoreValue();
        if (BitIsSet (registers, 15))
        {
            Register pc_reg;
            pc_reg.SetRegister (eRegisterKindDWARF, dwarf_pc);
            context.SetRegisterPlusOffset (pc_reg, 8);
            const uint32_t pc = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
                return false;
                  
            if (!MemAWrite (context, address + offset, pc + 8, addr_byte_size))
                return false;
        }
                  
        // if wback then R[n] = R[n] - 4*BitCount(registers);
        if (wback)
        {
            offset = (addr_byte_size * BitCount (registers)) * -1;
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr_t data = address + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, data))
                return false;
        }
    }
    return true;
}
                  
// STMDB (Store Multiple Decrement Before) stores multiple registers to consecutive memory locations using an address
// from a base register.  The consecutive memory locations end just below this address, and the address of the first of
// those locations can optionally be written back to the base register.
bool
EmulateInstructionARM::EmulateSTMDB (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n] - 4*BitCount(registers);
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN; // Only possible for encoding A1
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == ’1’ then // Only possible for encoding A1 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] - 4*BitCount(registers);
#endif

                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
        switch (encoding)
        {
            case eEncodingT1:
                // if W == ’1’ && Rn == ’1101’ then SEE PUSH;
                if ((BitIsSet (opcode, 21)) && (Bits32 (opcode, 19, 16) == 13))
                { 
                    // See PUSH 
                }
                // n = UInt(Rn); registers = ’0’:M:’0’:register_list; wback = (W == ’1’); 
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                registers = registers & 0x5fff;  // Make sure bits 15 & 13 are zeros.
                wback = BitIsSet (opcode, 21);
                // if n == 15 || BitCount(registers) < 2 then UNPREDICTABLE;
                if ((n == 15) || BitCount (registers) < 2)
                    return false;
                // if wback && registers<n> == ’1’ then UNPREDICTABLE;
                if (wback && BitIsSet (registers, n))
                    return false;
                break;
                  
            case eEncodingA1:
                // if W == ’1’ && Rn == ’1101’ && BitCount(register_list) >= 2 then SEE PUSH; 
                if (BitIsSet (opcode, 21) && (Bits32 (opcode, 19, 16) == 13) && BitCount (Bits32 (opcode, 15, 0)) >= 2)
                {
                    // See Push
                }
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’);
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) || BitCount (registers) < 1)
                    return false;
                break;
                  
            default:
                return false;
        }
                  
        // address = R[n] - 4*BitCount(registers);
                  
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
        return false;
                  
        address = address - (addr_byte_size * BitCount (registers));
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterStore;
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                  
        // for i = 0 to 14
        for (int i = 0; i < 14; ++i)
        {
            uint32_t lowest_set_bit = 14;
            // if registers<i> == ’1’ then
            if (BitIsSet (registers, i))
            {
                if (i < lowest_set_bit)
                    lowest_set_bit = i;
                // if i == n && wback && i != LowestSetBit(registers) then 
                if ((i == n) && wback && (i != lowest_set_bit))
                    // MemA[address,4] = bits(32) UNKNOWN; // Only possible for encoding A1
                    WriteBits32UnknownToMemory (address + offset);
                else
                {
                    // MemA[address,4] = R[i];
                    uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + i, 0, &success);
                    if (!success)
                        return false;
                  
                    Register data_reg;
                    data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + i);
                    context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, offset);
                    if (!MemAWrite (context, address + offset, data, addr_byte_size))
                        return false;
                }
                  
                // address = address + 4;
                offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then // Only possible for encoding A1 
        //     MemA[address,4] = PCStoreValue();
        if (BitIsSet (registers, 15))
        {
            Register pc_reg;
            pc_reg.SetRegister (eRegisterKindDWARF, dwarf_pc);
            context.SetRegisterPlusOffset (pc_reg, 8);
            const uint32_t pc = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
                return false;
                  
            if (!MemAWrite (context, address + offset, pc + 8, addr_byte_size))
                return false;
        }
                  
        // if wback then R[n] = R[n] - 4*BitCount(registers);
        if (wback)
        {
            offset = (addr_byte_size * BitCount (registers)) * -1;
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr_t data = address + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, data))
                return false;
        }
    }
    return true;
}
                  
// STMIB (Store Multiple Increment Before) stores multiple registers to consecutive memory locations using an address
// from a base register.  The consecutive memory locations start just above this address, and the address of the last
// of those locations can optionally be written back to the base register.
bool
EmulateInstructionARM::EmulateSTMIB (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        address = R[n] + 4;
                  
        for i = 0 to 14 
            if registers<i> == ’1’ then
                if i == n && wback && i != LowestSetBit(registers) then
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == ’1’ then 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] + 4*BitCount(registers);
#endif   
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        uint32_t n;
        uint32_t registers = 0;
        bool wback;
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        // EncodingSpecificOperations(); 
        switch (encoding)
        {
            case eEncodingA1:
                // n = UInt(Rn); registers = register_list; wback = (W == ’1’); 
                n = Bits32 (opcode, 19, 16);
                registers = Bits32 (opcode, 15, 0);
                wback = BitIsSet (opcode, 21);
                  
                // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
                if ((n == 15) && (BitCount (registers) < 1))
                    return false;
                break;
            default:
                return false;
        }
        // address = R[n] + 4;
                  
        int32_t offset = 0;
        addr_t address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        address = address + addr_byte_size;
                  
        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextRegisterStore;
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                
        uint32_t lowest_set_bit = 14;
        // for i = 0 to 14
        for (int i = 0; i < 14; ++i)
        {
            // if registers<i> == ’1’ then
            if (BitIsSet (registers, i))
            {
                if (i < lowest_set_bit)
                    lowest_set_bit = i;
                // if i == n && wback && i != LowestSetBit(registers) then
                if ((i == n) && wback && (i != lowest_set_bit))
                    // MemA[address,4] = bits(32) UNKNOWN;
                    WriteBits32UnknownToMemory (address + offset);
                // else
                else
                {
                    // MemA[address,4] = R[i];
                    uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + i, 0, &success);
                    if (!success)
                        return false;
                  
                    Register data_reg;
                    data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + i);
                    context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, offset);
                    if (!MemAWrite (context, address + offset, data, addr_byte_size))
                        return false;
                }
                  
                // address = address + 4;
                offset += addr_byte_size;
            }
        }
                  
        // if registers<15> == ’1’ then 
            // MemA[address,4] = PCStoreValue();
        if (BitIsSet (registers, 15))
        {
            Register pc_reg;
            pc_reg.SetRegister (eRegisterKindDWARF, dwarf_pc);
            context.SetRegisterPlusOffset (pc_reg, 8);
            const uint32_t pc = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
            if (!success)
            return false;
                  
            if (!MemAWrite (context, address + offset, pc + 8, addr_byte_size))
                return false;
        }
                  
        // if wback then R[n] = R[n] + 4*BitCount(registers);
        if (wback)
        {
            offset = addr_byte_size * BitCount (registers);
            context.type = EmulateInstruction::eContextAdjustBaseRegister;
            context.SetImmediateSigned (offset);
            addr_t data = address + offset;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, data))
                return false;
        }
    }
    return true;
}

// STR (store immediate) calcualtes an address from a base register value and an immediate offset, and stores a word
// from a register to memory.  It can use offset, post-indexed, or pre-indexed addressing.
bool
EmulateInstructionARM::EmulateSTRThumb (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        if UnalignedSupport() || address<1:0> == ’00’ then
            MemU[address,4] = R[t]; 
        else // Can only occur before ARMv7
            MemU[address,4] = bits(32) UNKNOWN; 
        if wback then R[n] = offset_addr;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        uint32_t t;
        uint32_t n;
        uint32_t imm32;
        bool index;
        bool add;
        bool wback;
        // EncodingSpecificOperations (); NullCheckIfThumbEE(n);
        switch (encoding)
        {
            case eEncodingT1:
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5:’00’, 32);
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                imm32 = Bits32 (opcode, 10, 6) << 2;
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = false;
                wback = false;
                break;
                  
            case eEncodingT2:
                // t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:’00’, 32);
                t = Bits32 (opcode, 10, 8);
                n = 13;
                imm32 = Bits32 (opcode, 7, 0) << 2;
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                break;
                  
            case eEncodingT3:
                // if Rn == ’1111’ then UNDEFINED;
                if (Bits32 (opcode, 19, 16) == 15)
                    return false;
                  
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 11, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                  
                // if t == 15 then UNPREDICTABLE;
                if (t == 15)
                    return false;
                break;
                  
            case eEncodingT4:
                // if P == ’1’ && U == ’1’ && W == ’0’ then SEE STRT; 
                // if Rn == ’1101’ && P == ’1’ && U == ’0’ && W == ’1’ && imm8 == ’00000100’ then SEE PUSH;
                // if Rn == ’1111’ || (P == ’0’ && W == ’0’) then UNDEFINED;
                if ((Bits32 (opcode, 19, 16) == 15)
                      || (BitIsClear (opcode, 10) && BitIsClear (opcode, 8)))
                    return false;
                  
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 7, 0);
                  
                // index = (P == ’1’); add = (U == ’1’); wback = (W == ’1’); 
                index = BitIsSet (opcode, 10);
                add = BitIsSet (opcode, 9);
                wback = BitIsSet (opcode, 8);
                  
                // if t == 15 || (wback && n == t) then UNPREDICTABLE;
                if ((t == 15) || (wback && (n == t)))
                    return false;
                break;
                  
            default:
                return false;
        }
    
        addr_t offset_addr;
        addr_t address;
                  
        // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        uint32_t base_address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        if (add)
            offset_addr = base_address + imm32;
        else
            offset_addr = base_address - imm32;

        // address = if index then offset_addr else R[n]; 
        if (index)
            address = offset_addr;
        else
            address = base_address;
                  
        EmulateInstruction::Context context;
        context.type = eContextRegisterStore;
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 +  n);
                  
        // if UnalignedSupport() || address<1:0> == ’00’ then
        if (UnalignedSupport () || (BitIsClear (address, 1) && BitIsClear (address, 0)))
        {
            // MemU[address,4] = R[t]; 
            uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
            if (!success)
                return false;
                  
            Register data_reg;
            data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + t);
            int32_t offset = address - base_address;
            context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, offset);
            if (!MemUWrite (context, address, data, addr_byte_size))
                return false;
        }
        else
        {
            // MemU[address,4] = bits(32) UNKNOWN; 
            WriteBits32UnknownToMemory (address);
        }
                  
        // if wback then R[n] = offset_addr;
        if (wback)
        {
            context.type = eContextRegisterLoad;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
    }
    return true;
}
                  
// STR (Store Register) calculates an address from a base register value and an offset register value, stores a 
// word from a register to memory.   The offset register value can optionally be shifted.
bool
EmulateInstructionARM::EmulateSTRRegister (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        if t == 15 then // Only possible for encoding A1
            data = PCStoreValue(); 
        else
            data = R[t]; 
        if UnalignedSupport() || address<1:0> == ’00’ || CurrentInstrSet() == InstrSet_ARM then
            MemU[address,4] = data; 
        else // Can only occur before ARMv7
            MemU[address,4] = bits(32) UNKNOWN; 
        if wback then R[n] = offset_addr;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        uint32_t t;
        uint32_t n;
        uint32_t m;
        ARM_ShifterType shift_t;
        uint32_t shift_n;
        bool index;
        bool add;
        bool wback;
                  
        // EncodingSpecificOperations (); NullCheckIfThumbEE(n);
        switch (encoding)
        {
            case eEncodingT1:
                // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation in ThumbEE";
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                m = Bits32 (opcode, 8, 6);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, 0);
                shift_t = SRType_LSL;
                shift_n = 0;
                break;
                  
            case eEncodingT2:
                // if Rn == ’1111’ then UNDEFINED; 
                if (Bits32 (opcode, 19, 16) == 15)
                    return false;
                  
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE; 
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
                shift_t = SRType_LSL;
                shift_n = Bits32 (opcode, 5, 4);
                  
                // if t == 15 || BadReg(m) then UNPREDICTABLE;
                if ((t == 15) || (BadReg (m)))
                    return false;
                break;
                  
            case eEncodingA1:
            {
                // if P == ’0’ && W == ’1’ then SEE STRT; 
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = (P == ’1’);	add = (U == ’1’);	wback = (P == ’0’) || (W == ’1’);
                index = BitIsSet (opcode, 24);
                add = BitIsSet (opcode, 23);
                wback = (BitIsClear (opcode, 24) || BitIsSet (opcode, 21));
                           
                // (shift_t, shift_n) = DecodeImmShift(type, imm5);
                uint32_t typ = Bits32 (opcode, 6, 5);
                uint32_t imm5 = Bits32 (opcode, 11, 7);
                shift_n = DecodeImmShift(typ, imm5, shift_t);
                         
                // if m == 15 then UNPREDICTABLE; 
                if (m == 15)
                    return false;
                         
                // if wback && (n == 15 || n == t) then UNPREDICTABLE;
                if (wback && ((n == 15) || (n == t)))
                    return false;

                break;
            }                  
            default:
                return false;
        }
                         
        addr_t offset_addr;
        addr_t address;
        int32_t offset = 0;
        
        addr_t base_address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                         
        uint32_t Rm_data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
        if (!success)
            return false;
                           
        // offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset = Shift (Rm_data, shift_t, shift_n, APSR_C);
                         
        // offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        if (add)
            offset_addr = base_address + offset;
        else
            offset_addr = base_address - offset;
                         
        // address = if index then offset_addr else R[n]; 
        if (index)
            address = offset_addr;
        else
            address = base_address;
                    
        uint32_t data;
        // if t == 15 then // Only possible for encoding A1
        if (t == 15)
            // data = PCStoreValue(); 
            data = ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
        else
            // data = R[t]; 
            data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
                         
        if (!success)
            return false;
                         
        EmulateInstruction::Context context;
        context.type = eContextRegisterStore;

        // if UnalignedSupport() || address<1:0> == ’00’ || CurrentInstrSet() == InstrSet_ARM then
        if (UnalignedSupport () 
            || (BitIsClear (address, 1) && BitIsClear (address, 0)) 
            || CurrentInstrSet() == eModeARM)
        {
            // MemU[address,4] = data; 
            
            Register base_reg;
            base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 +  n);
            
            Register data_reg;
            data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + t);
            
            context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, address - base_address);
            if (!MemUWrite (context, address, data, addr_byte_size))
                return false;
            
        }
        else
            // MemU[address,4] = bits(32) UNKNOWN; 
            WriteBits32UnknownToMemory (address);
                         
        // if wback then R[n] = offset_addr;
        if (wback)
        {
            context.type = eContextRegisterLoad;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }

    }
    return true;
}
               
bool
EmulateInstructionARM::EmulateSTRBThumb (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        MemU[address,1] = R[t]<7:0>; 
        if wback then R[n] = offset_addr;
#endif

                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t t;
        uint32_t n;
        uint32_t imm32;
        bool index;
        bool add;
        bool wback;
        // EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        switch (encoding)
        {
            case eEncodingT1:
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5, 32);
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                imm32 = Bits32 (opcode, 10, 6);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                break;
                  
            case eEncodingT2:
                // if Rn == ’1111’ then UNDEFINED; 
                if (Bits32 (opcode, 19, 16) == 15)
                    return false;
                  
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 11, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                  
                // if BadReg(t) then UNPREDICTABLE;
                if (BadReg (t))
                    return false;
                break;
                  
            case eEncodingT3:
                // if P == ’1’ && U == ’1’ && W == ’0’ then SEE STRBT; 
                // if Rn == ’1111’ || (P == ’0’ && W == ’0’) then UNDEFINED; 
                if (Bits32 (opcode, 19, 16) == 15)
                    return false;
                  
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 7, 0);
                  
                // index = (P == ’1’); add = (U == ’1’); wback = (W == ’1’);
                index = BitIsSet (opcode, 10);
                add = BitIsSet (opcode, 9);
                wback = BitIsSet (opcode, 8);
                  
                // if BadReg(t) || (wback && n == t) then UNPREDICTABLE
                if ((BadReg (t)) || (wback && (n == t)))
                    return false;
                break;
                  
            default:
                return false;
        }
                  
        addr_t offset_addr;
        addr_t address;
        addr_t base_address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        if (add)
            offset_addr = base_address + imm32;
        else
            offset_addr = base_address - imm32;
                  
        // address = if index then offset_addr else R[n];
        if (index)
            address = offset_addr;
        else
            address = base_address;
                  
        // MemU[address,1] = R[t]<7:0>
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                  
        Register data_reg;
        data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + t);
                  
        EmulateInstruction::Context context;
        context.type = eContextRegisterStore;
        context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, address - base_address);
                  
        uint32_t data = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
        if (!success)
            return false;
                  
        data = Bits32 (data, 7, 0);

        if (!MemUWrite (context, address, data, 1))
            return false;
                  
        // if wback then R[n] = offset_addr;
        if (wback)
        {
            context.type = eContextRegisterLoad;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
            
    }

    return true;
}
                  
// Add with Carry (immediate) adds an immediate value and the carry flag value to a register value,
// and writes the result to the destination register.  It can optionally update the condition flags
// based on the result.
bool
EmulateInstructionARM::EmulateADCImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn;
        uint32_t imm32; // the immediate value to be added to the value obtained from Rn
        bool setflags;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
            if (BadReg(Rd) || BadReg(Rn))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        int32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        AddWithCarryResult res = AddWithCarry(val1, imm32, APSR_C);

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
            return false;
    }
    return true;
}

// Add with Carry (register) adds a register value, the carry flag value, and an optionally-shifted
// register value, and writes the result to the destination register.  It can optionally update the
// condition flags based on the result.
bool
EmulateInstructionARM::EmulateADCReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Rn = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        int32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        int32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
        AddWithCarryResult res = AddWithCarry(val1, shifted, APSR_C);

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
            return false;
    }
    return true;
}

// This instruction performs a bitwise AND of a register value and an immediate value, and writes the result
// to the destination register.  It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateANDImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] AND imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn;
        uint32_t imm32; // the immediate value to be ANDed to the value obtained from Rn
        bool setflags;
        uint32_t carry; // the carry bit after ARM/Thumb Expand operation
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
            // if Rd == '1111' && S == '1' then SEE TST (immediate);
            if (Rd == 15 && setflags)
                return EmulateTSTImm(eEncodingT1);
            if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        uint32_t result = val1 & imm32;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// This instruction performs a bitwise AND of a register value and an optionally-shifted register value,
// and writes the result to the destination register.  It can optionally update the condition flags
// based on the result.
bool
EmulateInstructionARM::EmulateANDReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] AND shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        uint32_t carry;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Rn = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            // if Rd == '1111' && S == '1' then SEE TST (register);
            if (Rd == 15 && setflags)
                return EmulateTSTReg(eEncodingT2);
            if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry);
        uint32_t result = val1 & shifted;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// LDR (immediate, ARM) calculates an address from a base register value and an immediate offset, loads a word 
// from memory, and writes it to a register.  It can use offset, post-indexed, or pre-indexed addressing.
bool
EmulateInstructionARM::EmulateLDRImmediateARM (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,4]; 
        if wback then R[n] = offset_addr; 
        if t == 15 then
            if address<1:0> == ’00’ then LoadWritePC(data); else UNPREDICTABLE; 
        elsif UnalignedSupport() || address<1:0> = ’00’ then
            R[t] = data; 
        else // Can only apply before ARMv7
            R[t] = ROR(data, 8*UInt(address<1:0>));
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                
    if (ConditionPassed ())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        uint32_t t;
        uint32_t n;
        uint32_t imm32;
        bool index;
        bool add;
        bool wback;
                  
        switch (encoding)
        {
            case eEncodingA1:
                // if Rn == ’1111’ then SEE LDR (literal);
                // if P == ’0’ && W == ’1’ then SEE LDRT; 
                // if Rn == ’1101’ && P == ’0’ && U == ’1’ && W == ’0’ && imm12 == ’000000000100’ then SEE POP; 
                // t == UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 11, 0);
                  
                // index = (P == ’1’);	add = (U == ’1’);	wback = (P == ’0’) || (W == ’1’);
                  index = BitIsSet (opcode, 24);
                  add = BitIsSet (opcode, 23);
                  wback = (BitIsClear (opcode, 24) || BitIsSet (opcode, 21));
                  
                // if wback && n == t then UNPREDICTABLE;
                if (wback && (n == t))
                    return false;
                  
                break;
                  
            default:
                return false;
        }
               
        addr_t address;
        addr_t offset_addr;
        addr_t base_address = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        if (add)
                  offset_addr = base_address + imm32;
        else
            offset_addr = base_address - imm32;
                  
        // address = if index then offset_addr else R[n]; 
        if (index)
            address = offset_addr;
        else
            address = base_address;
                  
        // data = MemU[address,4]; 
                  
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                 
        EmulateInstruction::Context context;
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset (base_reg, address - base_address);
                  
        uint64_t data = MemURead (context, address, addr_byte_size, 0, &success);
        if (!success)
            return false;
                  
        // if wback then R[n] = offset_addr; 
        if (wback)
        {
            context.type = eContextAdjustBaseRegister;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
                  
        // if t == 15 then
        if (t == 15)
        {
            // if address<1:0> == ’00’ then LoadWritePC(data); else UNPREDICTABLE; 
            if (BitIsClear (address, 1) && BitIsClear (address, 0))
            {
                // LoadWritePC (data);
                context.type = eContextRegisterLoad;
                context.SetRegisterPlusOffset (base_reg, address - base_address);
                LoadWritePC (context, data);
            }
            else
                  return false;
        }
        // elsif UnalignedSupport() || address<1:0> = ’00’ then
        else if (UnalignedSupport() || (BitIsClear (address, 1) && BitIsClear (address, 0)))
        {
            // R[t] = data; 
            context.type = eContextRegisterLoad;
            context.SetRegisterPlusOffset (base_reg, address - base_address);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
                return false;
        }
        // else // Can only apply before ARMv7
        else
        {
            // R[t] = ROR(data, 8*UInt(address<1:0>));
            data = ROR (data, Bits32 (address, 1, 0));
            context.type = eContextRegisterLoad;
            context.SetImmediate (data);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
                return false;
        }

    }
    return true;
}
                  
// LDR (register) calculates an address from a base register value and an offset register value, loads a word 
// from memory, and writes it to a resgister.  The offset register value can optionally be shifted.  
bool
EmulateInstructionARM::EmulateLDRRegister (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,4]; 
        if wback then R[n] = offset_addr; 
        if t == 15 then
            if address<1:0> == ’00’ then LoadWritePC(data); else UNPREDICTABLE; 
        elsif UnalignedSupport() || address<1:0> = ’00’ then
            R[t] = data; 
        else // Can only apply before ARMv7
            if CurrentInstrSet() == InstrSet_ARM then 
                R[t] = ROR(data, 8*UInt(address<1:0>));
            else 
                R[t] = bits(32) UNKNOWN;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        const uint32_t addr_byte_size = GetAddressByteSize();
                  
        uint32_t t;
        uint32_t n;
        uint32_t m;
        bool index;
        bool add;
        bool wback;
        ARM_ShifterType shift_t;
        uint32_t shift_n;
                  
        switch (encoding)
        {
            case eEncodingT1:
                // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation in ThumbEE"; 
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); 
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                m = Bits32 (opcode, 8, 6);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, 0);
                shift_t = SRType_LSL;
                shift_n = 0;
                  
                break;
                  
            case eEncodingT2:
                // if Rn == ’1111’ then SEE LDR (literal);
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE; 
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, UInt(imm2)); 
                shift_t = SRType_LSL;
                shift_n = Bits32 (opcode, 5, 4);
                  
                // if BadReg(m) then UNPREDICTABLE; 
                if (BadReg (m))
                    return false;
                  
                // if t == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
                if ((t == 15) && InITBlock() && !LastInITBlock())
                    return false;
                  
                break;
                  
            case eEncodingA1:
            {
                // if P == ’0’ && W == ’1’ then SEE LDRT; 
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = (P == ’1’);	add = (U == ’1’);	wback = (P == ’0’) || (W == ’1’); 
                index = BitIsSet (opcode, 24);
                add = BitIsSet (opcode, 23);
                wback = (BitIsClear (opcode, 24) || BitIsSet (opcode, 21));
                  
                // (shift_t, shift_n) = DecodeImmShift(type, imm5); 
                uint32_t type = Bits32 (opcode, 6, 5);
                uint32_t imm5 = Bits32 (opcode, 11, 7);
                shift_n = DecodeImmShift (type, imm5, shift_t);
                  
                // if m == 15 then UNPREDICTABLE; 
                if (m == 15)
                    return false;
                  
                // if wback && (n == 15 || n == t) then UNPREDICTABLE;
                if (wback && ((n == 15) || (n == t)))
                    return false;
            }
                break;
                  
                  
            default:
                return false;
        }
         
        uint32_t Rm = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
        if (!success)
            return false;
                  
        uint32_t Rn = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
            
        addr_t offset_addr;
        addr_t address;
                  
        // offset = Shift(R[m], shift_t, shift_n, APSR.C);   -- Note "The APSR is an application level alias for the CPSR".
        addr_t offset = Shift (Rm, shift_t, shift_n, Bit32 (m_inst_cpsr, APSR_C));
                  
        // offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        if (add)
            offset_addr = Rn + offset;
        else
            offset_addr = Rn - offset;
                  
        // address = if index then offset_addr else R[n]; 
            if (index)
                address = offset_addr;
            else
                address = Rn;
                  
        // data = MemU[address,4]; 
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
                  
        EmulateInstruction::Context context;
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset (base_reg, address - Rn);
                  
        uint64_t data = MemURead (context, address, addr_byte_size, 0, &success);
        if (!success)
            return false;
                  
        // if wback then R[n] = offset_addr; 
        if (wback)
        {
            context.type = eContextAdjustBaseRegister;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
            
        // if t == 15 then
        if (t == 15)
        {
            // if address<1:0> == ’00’ then LoadWritePC(data); else UNPREDICTABLE;
            if (BitIsClear (address, 1) && BitIsClear (address, 0))
            {
                context.type = eContextRegisterLoad;
                context.SetRegisterPlusOffset (base_reg, address - Rn);
                LoadWritePC (context, data);
            }
            else
                return false;
        }
        // elsif UnalignedSupport() || address<1:0> = ’00’ then
        else if (UnalignedSupport () || (BitIsClear (address, 1) && BitIsClear (address, 0)))
        {
            // R[t] = data; 
            context.type = eContextRegisterLoad;
            context.SetRegisterPlusOffset (base_reg, address - Rn);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
                return false;
        }
        else // Can only apply before ARMv7
        {
            // if CurrentInstrSet() == InstrSet_ARM then 
            if (CurrentInstrSet () == eModeARM)
            {
                // R[t] = ROR(data, 8*UInt(address<1:0>));
                data = ROR (data, Bits32 (address, 1, 0));
                context.type = eContextRegisterLoad;
                context.SetImmediate (data);
                if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
                    return false;
            }
            else
            {
                // R[t] = bits(32) UNKNOWN;
                WriteBits32Unknown (t);
            }
        }
    }
    return true;
}

// LDRB (immediate, Thumb)
bool
EmulateInstructionARM::EmulateLDRBImmediate (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        R[t] = ZeroExtend(MemU[address,1], 32); 
        if wback then R[n] = offset_addr;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t t;
        uint32_t n;
        uint32_t imm32;
        bool index;
        bool add;
        bool wback;
                  
        // EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        switch (encoding)
        {
            case eEncodingT1:
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5, 32); 
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                imm32 = Bits32 (opcode, 10, 6);
                  
                // index = TRUE; add = TRUE; wback = FALSE;
                index = true;
                add = true;
                wback= false;
                  
                break;
                  
            case eEncodingT2:
                // if Rt == ’1111’ then SEE PLD; 
                // if Rn == ’1111’ then SEE LDRB (literal); 
                // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 11, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE; 
                index = true;
                add = true;
                wback = false;
                  
                // if t == 13 then UNPREDICTABLE;
                if (t == 13)
                    return false;
                  
                break;
                  
            case eEncodingT3:
                // if Rt == ’1111’ && P == ’1’ && U == ’0’ && W == ’0’ then SEE PLD; 
                // if Rn == ’1111’ then SEE LDRB (literal); 
                // if P == ’1’ && U == ’1’ && W == ’0’ then SEE LDRBT; 
                // if P == ’0’ && W == ’0’ then UNDEFINED;
                if (BitIsClear (opcode, 10) && BitIsClear (opcode, 8))
                    return false;
                  
                  // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                imm32 = Bits32 (opcode, 7, 0);
                  
                // index = (P == ’1’); add = (U == ’1’); wback = (W == ’1’); 
                index = BitIsSet (opcode, 10);
                add = BitIsSet (opcode, 9);
                wback = BitIsSet (opcode, 8);
                  
                // if BadReg(t) || (wback && n == t) then UNPREDICTABLE;
                if (BadReg (t) || (wback && (n == t)))
                    return false;
                  
                break;
                  
            default:
                return false;
        }
                  
        uint32_t Rn = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        addr_t address;
        addr_t offset_addr;
                  
        // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        if (add)
            offset_addr = Rn + imm32;
        else
            offset_addr = Rn - imm32;
                  
        // address = if index then offset_addr else R[n]; 
        if (index)
            address = offset_addr;
        else
            address = Rn;
                  
        // R[t] = ZeroExtend(MemU[address,1], 32); 
        Register base_reg;
        Register data_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        data_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + t);
                  
        EmulateInstruction::Context context;
        context.type = eContextRegisterLoad;
        context.SetRegisterToRegisterPlusOffset (data_reg, base_reg, address - Rn);
                  
        uint64_t data = MemURead (context, address, 1, 0, &success);
        if (!success)
            return false;
            
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
            return false;
                  
        // if wback then R[n] = offset_addr;
        if (wback)
        {
            context.type = eContextAdjustBaseRegister;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
    }
    return true;
}
                
// LDRB (literal) calculates an address from the PC value and an immediate offset, loads a byte from memory, 
// zero-extends it to form a 32-bit word and writes it to a register.
bool
EmulateInstructionARM::EmulateLDRBLiteral (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(15); 
        base = Align(PC,4); 
        address = if add then (base + imm32) else (base - imm32); 
        R[t] = ZeroExtend(MemU[address,1], 32);
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                  
    if (ConditionPassed ())
    {
        uint32_t t;
        uint32_t imm32;
        bool add;
        switch (encoding)
        {
            case eEncodingT1:
                // if Rt == ’1111’ then SEE PLD; 
                // t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == ’1’); 
                t = Bits32 (opcode, 15, 12);
                imm32 = Bits32 (opcode, 11, 0);
                add = BitIsSet (opcode, 23);
                  
                // if t == 13 then UNPREDICTABLE;
                if (t == 13)
                    return false;
                  
                break;
                  
            case eEncodingA1:
                // t == UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == ’1’); 
                t = Bits32 (opcode, 15, 12);
                imm32 = Bits32 (opcode, 11, 0);
                add = BitIsSet (opcode, 23);
                  
                // if t == 15 then UNPREDICTABLE;
                if (t == 15)
                    return false;
                break;
                  
            default:
                return false;
        }
                  
        // base = Align(PC,4); 
        uint32_t pc_val = ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, 0, &success);
        if (!success)
            return false;
                  
        uint32_t base = AlignPC (pc_val);
                  
        addr_t address;
        // address = if add then (base + imm32) else (base - imm32); 
        if (add)
            address = base + imm32;
        else
            address = base - imm32;
                  
        // R[t] = ZeroExtend(MemU[address,1], 32);
        EmulateInstruction::Context context;
        context.type = eContextRelativeBranchImmediate;
        context.SetImmediate (address - base);
                  
        uint64_t data = MemURead (context, address, 1, 0, &success);
        if (!success)
            return false;
                  
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
            return false;
    }
    return true;
}
  
// LDRB (register) calculates an address from a base register value and an offset rigister value, loads a byte from
// memory, zero-extends it to form a 32-bit word, and writes it to a register.  The offset register value can 
// optionally be shifted.
bool
EmulateInstructionARM::EmulateLDRBRegister (ARMEncoding encoding)
{
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        R[t] = ZeroExtend(MemU[address,1],32); 
        if wback then R[n] = offset_addr;
#endif
                  
    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;
                
    if (ConditionPassed ())
    {
        uint32_t t;
        uint32_t n;
        uint32_t m;
        bool index;
        bool add;
        bool wback;
        ARM_ShifterType shift_t;
        uint32_t shift_n;

        // EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        switch (encoding)
        {
            case eEncodingT1:
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); 
                t = Bits32 (opcode, 2, 0);
                n = Bits32 (opcode, 5, 3);
                m = Bits32 (opcode, 8, 6);
                  
                // index = TRUE; add = TRUE; wback = FALSE; 
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, 0);
                shift_t = SRType_LSL;
                shift_n = 0;
                break;
                  
            case eEncodingT2:
                // if Rt == ’1111’ then SEE PLD; 
                // if Rn == ’1111’ then SEE LDRB (literal); 
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = TRUE; add = TRUE; wback = FALSE; 
                index = true;
                add = true;
                wback = false;
                  
                // (shift_t, shift_n) = (SRType_LSL, UInt(imm2)); 
                shift_t = SRType_LSL;
                shift_n = Bits32 (opcode, 5, 4);
                  
                // if t == 13 || BadReg(m) then UNPREDICTABLE;
                if ((t == 13) || BadReg (m))
                    return false;
                break;
                  
            case eEncodingA1:
            {
                // if P == ’0’ && W == ’1’ then SEE LDRBT; 
                // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); 
                t = Bits32 (opcode, 15, 12);
                n = Bits32 (opcode, 19, 16);
                m = Bits32 (opcode, 3, 0);
                  
                // index = (P == ’1’);	add = (U == ’1’);	wback = (P == ’0’) || (W == ’1’); 
                index = BitIsSet (opcode, 24);
                add = BitIsSet (opcode, 23);
                wback = (BitIsClear (opcode, 24) || BitIsSet (opcode, 21));
                  
                // (shift_t, shift_n) = DecodeImmShift(type, imm5); 
                uint32_t type = Bits32 (opcode, 6, 5);
                uint32_t imm5 = Bits32 (opcode, 11, 7);
                shift_n = DecodeImmShift (type, imm5, shift_t);
                  
                // if t == 15 || m == 15 then UNPREDICTABLE; 
                if ((t == 15) || (m == 15))
                    return false;
                  
                // if wback && (n == 15 || n == t) then UNPREDICTABLE;
                if (wback && ((n == 15) || (n == t)))
                    return false;
            }
                break;
                  
            default:
                return false;
        }
        
        addr_t offset_addr;
        addr_t address;
        
        // offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        uint32_t Rm = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
        if (!success)
            return false;
                  
        addr_t offset = Shift (Rm, shift_t, shift_n, APSR_C);
                  
        // offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        uint32_t Rn = ReadRegisterUnsigned (eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
        if (!success)
            return false;
                  
        if (add)
            offset_addr = Rn + offset;
        else
            offset_addr = Rn - offset;
                  
        // address = if index then offset_addr else R[n]; 
        if (index)
            address = offset_addr;
        else
            address = Rn;
                  
        // R[t] = ZeroExtend(MemU[address,1],32); 
        Register base_reg;
        base_reg.SetRegister (eRegisterKindDWARF, dwarf_r0 + n);
        
        EmulateInstruction::Context context;
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset (base_reg, address - Rn);
                  
        uint64_t data = MemURead (context, address, 1, 0, &success);
        if (!success)
            return false;
                  
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, data))
            return false;
                  
        // if wback then R[n] = offset_addr;
        if (wback)
        {
            context.type = eContextAdjustBaseRegister;
            context.SetAddress (offset_addr);
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + n, offset_addr))
                return false;
        }
    }
    return true;
}
                  
// Bitwise Exclusive OR (immediate) performs a bitwise exclusive OR of a register value and an immediate value,
// and writes the result to the destination register.  It can optionally update the condition flags based on
// the result.
bool
EmulateInstructionARM::EmulateEORImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] EOR imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn;
        uint32_t imm32; // the immediate value to be ORed to the value obtained from Rn
        bool setflags;
        uint32_t carry; // the carry bit after ARM/Thumb Expand operation
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
            // if Rd == '1111' && S == '1' then SEE TEQ (immediate);
            if (Rd == 15 && setflags)
                return EmulateTEQImm(eEncodingT1);
            if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
            // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        uint32_t result = val1 ^ imm32;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Bitwise Exclusive OR (register) performs a bitwise exclusive OR of a register value and an
// optionally-shifted register value, and writes the result to the destination register.
// It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateEORReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] EOR shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        uint32_t carry;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Rn = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            // if Rd == '1111' && S == '1' then SEE TEQ (register);
            if (Rd == 15 && setflags)
                return EmulateTEQReg(eEncodingT1);
            if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry);
        uint32_t result = val1 ^ shifted;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Bitwise OR (immediate) performs a bitwise (inclusive) OR of a register value and an immediate value, and
// writes the result to the destination register.  It can optionally update the condition flags based
// on the result.
bool
EmulateInstructionARM::EmulateORRImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] OR imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn;
        uint32_t imm32; // the immediate value to be ORed to the value obtained from Rn
        bool setflags;
        uint32_t carry; // the carry bit after ARM/Thumb Expand operation
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
            // if Rn == ‘1111’ then SEE MOV (immediate);
            if (Rn == 15)
                return EmulateMOVRdImm(eEncodingT2);
            if (BadReg(Rd) || Rn == 13)
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            setflags = BitIsSet(opcode, 20);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        uint32_t result = val1 | imm32;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Bitwise OR (register) performs a bitwise (inclusive) OR of a register value and an optionally-shifted register
// value, and writes the result to the destination register.  It can optionally update the condition flags based
// on the result.
bool
EmulateInstructionARM::EmulateORRReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] OR shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rd, Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        bool setflags;
        uint32_t carry;
        switch (encoding)
        {
        case eEncodingT1:
            Rd = Rn = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            setflags = !InITBlock();
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rd = Bits32(opcode, 11, 8);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            // if Rn == '1111' then SEE MOV (register);
            if (Rn == 15)
                return EmulateMOVRdRm(eEncodingT3);
            if (BadReg(Rd) || Rn == 13 || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rd = Bits32(opcode, 15, 12);
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            setflags = BitIsSet(opcode, 20);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            // TODO: Emulate SUBS PC, LR and related instructions.
            if (Rd == 15 && setflags)
                return false;
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry);
        uint32_t result = val1 | shifted;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
            return false;
    }
    return true;
}

// Reverse Subtract (immediate) subtracts a register value from an immediate value, and writes the result to
// the destination register. It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateRSBImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), imm32, '1');
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand
    bool setflags;
    uint32_t imm32; // the immediate value to be added to the value obtained from Rn
    switch (encoding) {
    case eEncodingT1:
        Rd = Bits32(opcode, 2, 0);
        Rn = Bits32(opcode, 5, 3);
        setflags = !InITBlock();
        imm32 = 0;
        break;
    case eEncodingT2:
        Rd = Bits32(opcode, 11, 8);
        Rn = Bits32(opcode, 19, 16);
        setflags = BitIsSet(opcode, 20);
        imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
        if (BadReg(Rd) || BadReg(Rn))
            return false;
        break;
    case eEncodingA1:
        Rd = Bits32(opcode, 15, 12);
        Rn = Bits32(opcode, 19, 16);
        setflags = BitIsSet(opcode, 20);
        imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
        // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
        // TODO: Emulate SUBS PC, LR and related instructions.
        if (Rd == 15 && setflags)
            return false;
        break;
    default:
        return false;
    }
    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Rn, &success);
    if (!success)
        return false;
                  
    AddWithCarryResult res = AddWithCarry(~reg_val, imm32, 1);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs ();

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
        return false;

    return true;
}

// Reverse Subtract (register) subtracts a register value from an optionally-shifted register value, and writes the
// result to the destination register. It can optionally update the condition flags based on the result.
bool
EmulateInstructionARM::EmulateRSBReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), shifted, '1');
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand
    uint32_t Rm; // the second operand
    bool setflags;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    switch (encoding) {
    case eEncodingT1:
        Rd = Bits32(opcode, 11, 8);
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        setflags = BitIsSet(opcode, 20);
        shift_n = DecodeImmShiftThumb(opcode, shift_t);
        // if (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
        if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
            return false;
        break;
    case eEncodingA1:
        Rd = Bits32(opcode, 15, 12);
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        setflags = BitIsSet(opcode, 20);
        shift_n = DecodeImmShiftARM(opcode, shift_t);
        // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
        // TODO: Emulate SUBS PC, LR and related instructions.
        if (Rd == 15 && setflags)
            return false;
        break;
    default:
        return false;
    }
    // Read the register value from register Rn.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
        return false;

    // Read the register value from register Rm.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
        return false;
                  
    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
    AddWithCarryResult res = AddWithCarry(~val1, shifted, 1);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();
    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
        return false;

    return true;
}

// Reverse Subtract with Carry (immediate) subtracts a register value and the value of NOT (Carry flag) from
// an immediate value, and writes the result to the destination register. It can optionally update the condition
// flags based on the result.
bool
EmulateInstructionARM::EmulateRSCImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), imm32, APSR.C);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand
    bool setflags;
    uint32_t imm32; // the immediate value to be added to the value obtained from Rn
    switch (encoding) {
    case eEncodingA1:
        Rd = Bits32(opcode, 15, 12);
        Rn = Bits32(opcode, 19, 16);
        setflags = BitIsSet(opcode, 20);
        imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
        // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
        // TODO: Emulate SUBS PC, LR and related instructions.
        if (Rd == 15 && setflags)
            return false;
        break;
    default:
        return false;
    }
    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Rn, &success);
    if (!success)
        return false;
                  
    AddWithCarryResult res = AddWithCarry(~reg_val, imm32, APSR_C);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs ();

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
        return false;

    return true;
}

// Reverse Subtract with Carry (register) subtracts a register value and the value of NOT (Carry flag) from an
// optionally-shifted register value, and writes the result to the destination register. It can optionally update the
// condition flags based on the result.
bool
EmulateInstructionARM::EmulateRSCReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), shifted, APSR.C);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand
    uint32_t Rm; // the second operand
    bool setflags;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    switch (encoding) {
    case eEncodingA1:
        Rd = Bits32(opcode, 15, 12);
        Rn = Bits32(opcode, 19, 16);
        Rm = Bits32(opcode, 3, 0);
        setflags = BitIsSet(opcode, 20);
        shift_n = DecodeImmShiftARM(opcode, shift_t);
        // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related instructions;
        // TODO: Emulate SUBS PC, LR and related instructions.
        if (Rd == 15 && setflags)
            return false;
        break;
    default:
        return false;
    }
    // Read the register value from register Rn.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
        return false;

    // Read the register value from register Rm.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
        return false;
                  
    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C);
    AddWithCarryResult res = AddWithCarry(~val1, shifted, APSR_C);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();
    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags, res.carry_out, res.overflow))
        return false;

    return true;
}

// Test Equivalence (immediate) performs a bitwise exclusive OR operation on a register value and an
// immediate value.  It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateTEQImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] EOR imm32;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rn;
        uint32_t imm32; // the immediate value to be ANDed to the value obtained from Rn
        uint32_t carry; // the carry bit after ARM/Thumb Expand operation
        switch (encoding)
        {
        case eEncodingT1:
            Rn = Bits32(opcode, 19, 16);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
            if (BadReg(Rn))
                return false;
            break;
        case eEncodingA1:
            Rn = Bits32(opcode, 19, 16);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        uint32_t result = val1 ^ imm32;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteFlags(context, result, carry))
            return false;
    }
    return true;
}

// Test Equivalence (register) performs a bitwise exclusive OR operation on a register value and an
// optionally-shifted register value.  It updates the condition flags based on the result, and discards
// the result.
bool
EmulateInstructionARM::EmulateTEQReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] EOR shifted;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        uint32_t carry;
        switch (encoding)
        {
        case eEncodingT1:
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            if (BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry);
        uint32_t result = val1 ^ shifted;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteFlags(context, result, carry))
            return false;
    }
    return true;
}

// Test (immediate) performs a bitwise AND operation on a register value and an immediate value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateTSTImm (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] AND imm32;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rn;
        uint32_t imm32; // the immediate value to be ANDed to the value obtained from Rn
        uint32_t carry; // the carry bit after ARM/Thumb Expand operation
        switch (encoding)
        {
        case eEncodingT1:
            Rn = Bits32(opcode, 19, 16);
            imm32 = ThumbExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
            if (BadReg(Rn))
                return false;
            break;
        case eEncodingA1:
            Rn = Bits32(opcode, 19, 16);
            imm32 = ARMExpandImm_C(opcode, APSR_C, carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        uint32_t result = val1 & imm32;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteFlags(context, result, carry))
            return false;
    }
    return true;
}

// Test (register) performs a bitwise AND operation on a register value and an optionally-shifted register value.
// It updates the condition flags based on the result, and discards the result.
bool
EmulateInstructionARM::EmulateTSTReg (ARMEncoding encoding)
{
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] AND shifted;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

    bool success = false;
    const uint32_t opcode = OpcodeAsUnsigned (&success);
    if (!success)
        return false;

    if (ConditionPassed())
    {
        uint32_t Rn, Rm;
        ARM_ShifterType shift_t;
        uint32_t shift_n; // the shift applied to the value read from Rm
        uint32_t carry;
        switch (encoding)
        {
        case eEncodingT1:
            Rn = Bits32(opcode, 2, 0);
            Rm = Bits32(opcode, 5, 3);
            shift_t = SRType_LSL;
            shift_n = 0;
            break;
        case eEncodingT2:
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            shift_n = DecodeImmShiftThumb(opcode, shift_t);
            if (BadReg(Rn) || BadReg(Rm))
                return false;
            break;
        case eEncodingA1:
            Rn = Bits32(opcode, 19, 16);
            Rm = Bits32(opcode, 3, 0);
            shift_n = DecodeImmShiftARM(opcode, shift_t);
            break;
        default:
            return false;
        }

        // Read the first operand.
        uint32_t val1 = ReadCoreReg(Rn, &success);
        if (!success)
            return false;

        // Read the second operand.
        uint32_t val2 = ReadCoreReg(Rm, &success);
        if (!success)
            return false;

        uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry);
        uint32_t result = val1 & shifted;

        EmulateInstruction::Context context;
        context.type = EmulateInstruction::eContextImmediate;
        context.SetNoArgs ();

        if (!WriteFlags(context, result, carry))
            return false;
    }
    return true;
}

EmulateInstructionARM::ARMOpcode*
EmulateInstructionARM::GetARMOpcodeForInstruction (const uint32_t opcode)
{
    static ARMOpcode 
    g_arm_opcodes[] = 
    {
        //----------------------------------------------------------------------
        // Prologue instructions
        //----------------------------------------------------------------------

        // push register(s)
        { 0x0fff0000, 0x092d0000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulatePUSH, "push <registers>" },
        { 0x0fff0fff, 0x052d0004, ARMvAll,       eEncodingA2, eSize32, &EmulateInstructionARM::EmulatePUSH, "push <register>" },

        // set r7 to point to a stack offset
        { 0x0ffff000, 0x028d7000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADDRdSPImm, "add r7, sp, #<const>" },
        { 0x0ffff000, 0x024c7000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSUBR7IPImm, "sub r7, ip, #<const>"},
        // copy the stack pointer to ip
        { 0x0fffffff, 0x01a0c00d, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateMOVRdSP, "mov ip, sp" },
        { 0x0ffff000, 0x028dc000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADDRdSPImm, "add ip, sp, #<const>" },
        { 0x0ffff000, 0x024dc000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSUBIPSPImm, "sub ip, sp, #<const>"},

        // adjust the stack pointer
        { 0x0ffff000, 0x024dd000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSUBSPImm, "sub sp, sp, #<const>"},

        // push one register
        // if Rn == '1101' && imm12 == '000000000100' then SEE PUSH;
        { 0x0fff0000, 0x052d0000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTRRtSP, "str Rt, [sp, #-imm12]!" },

        // vector push consecutive extension register(s)
        { 0x0fbf0f00, 0x0d2d0b00, ARMV6T2_ABOVE, eEncodingA1, eSize32, &EmulateInstructionARM::EmulateVPUSH, "vpush.64 <list>"},
        { 0x0fbf0f00, 0x0d2d0a00, ARMV6T2_ABOVE, eEncodingA2, eSize32, &EmulateInstructionARM::EmulateVPUSH, "vpush.32 <list>"},

        //----------------------------------------------------------------------
        // Epilogue instructions
        //----------------------------------------------------------------------

        { 0x0fff0000, 0x08bd0000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulatePOP, "pop <registers>"},
        { 0x0fff0fff, 0x049d0004, ARMvAll,       eEncodingA2, eSize32, &EmulateInstructionARM::EmulatePOP, "pop <register>"},
        { 0x0fbf0f00, 0x0cbd0b00, ARMV6T2_ABOVE, eEncodingA1, eSize32, &EmulateInstructionARM::EmulateVPOP, "vpop.64 <list>"},
        { 0x0fbf0f00, 0x0cbd0a00, ARMV6T2_ABOVE, eEncodingA2, eSize32, &EmulateInstructionARM::EmulateVPOP, "vpop.32 <list>"},

        //----------------------------------------------------------------------
        // Supervisor Call (previously Software Interrupt)
        //----------------------------------------------------------------------
        { 0x0f000000, 0x0f000000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSVC, "svc #imm24"},

        //----------------------------------------------------------------------
        // Branch instructions
        //----------------------------------------------------------------------
        { 0x0f000000, 0x0a000000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSVC, "b #imm24"},
        // To resolve ambiguity, "blx <label>" should come before "bl <label>".
        { 0xfe000000, 0xfa000000, ARMV5_ABOVE,   eEncodingA2, eSize32, &EmulateInstructionARM::EmulateBLXImmediate, "blx <label>"},
        { 0x0f000000, 0x0b000000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateBLXImmediate, "bl <label>"},
        { 0x0ffffff0, 0x012fff30, ARMV5_ABOVE,   eEncodingA1, eSize32, &EmulateInstructionARM::EmulateBLXRm, "blx <Rm>"},
        // for example, "bx lr"
        { 0x0ffffff0, 0x012fff10, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateBXRm, "bx <Rm>"},

        //----------------------------------------------------------------------
        // Data-processing instructions
        //----------------------------------------------------------------------
        // adc (immediate)
        { 0x0fe00000, 0x02a00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADCImm, "adc{s}<c> <Rd>, <Rn>, #const"},
        // adc (register)
        { 0x0fe00010, 0x00a00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADCReg, "adc{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // add (immediate)
        { 0x0fe00000, 0x02800000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADDImmARM, "add{s}<c> <Rd>, <Rn>, #const"},
        // add (register)
        { 0x0fe00010, 0x00800000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateADDReg, "add{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // and (immediate)
        { 0x0fe00000, 0x02000000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateANDImm, "and{s}<c> <Rd>, <Rn>, #const"},
        // and (register)
        { 0x0fe00010, 0x00000000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateANDReg, "and{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // eor (immediate)
        { 0x0fe00000, 0x02200000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateEORImm, "eor{s}<c> <Rd>, <Rn>, #const"},
        // eor (register)
        { 0x0fe00010, 0x00200000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateEORReg, "eor{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // orr (immediate)
        { 0x0fe00000, 0x03800000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateORRImm, "orr{s}<c> <Rd>, <Rn>, #const"},
        // orr (register)
        { 0x0fe00010, 0x01800000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateORRReg, "orr{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // rsb (immediate)
        { 0x0fe00000, 0x02600000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRSBImm, "rsb{s}<c> <Rd>, <Rn>, #<const>"},
        // rsb (register)
        { 0x0fe00010, 0x00600000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRSBReg, "rsb{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // rsc (immediate)
        { 0x0fe00000, 0x02e00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRSCImm, "rsc{s}<c> <Rd>, <Rn>, #<const>"},
        // rsc (register)
        { 0x0fe00010, 0x00e00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRSCReg, "rsc{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
        // teq (immediate)
        { 0x0ff0f000, 0x03300000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateTEQImm, "teq<c> <Rn>, #const"},
        // teq (register)
        { 0x0ff0f010, 0x01300000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateTEQReg, "teq<c> <Rn>, <Rm> {,<shift>}"},
        // tst (immediate)
        { 0x0ff0f000, 0x03100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateTSTImm, "tst<c> <Rn>, #const"},
        // tst (register)
        { 0x0ff0f010, 0x01100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateTSTReg, "tst<c> <Rn>, <Rm> {,<shift>}"},


        // mvn (immediate)
        { 0x0fef0000, 0x03e00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateMVNImm, "mvn{s}<c> <Rd>, #<const>"},
        // mvn (register)
        { 0x0fef0010, 0x01e00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateMVNReg, "mvn{s}<c> <Rd>, <Rm> {,<shift>}"},
        // cmn (immediate)
        { 0x0ff0f000, 0x03700000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateCMNImm, "cmn<c> <Rn>, #<const>"},
        // cmn (register)
        { 0x0ff0f010, 0x01700000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm> {,<shift>}"},
        // cmp (immediate)
        { 0x0ff0f000, 0x03500000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateCMPImm, "cmp<c> <Rn>, #<const>"},
        // cmp (register)
        { 0x0ff0f010, 0x01500000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm> {,<shift>}"},
        // asr (immediate)
        { 0x0fef0070, 0x01a00040, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateASRImm, "asr{s}<c> <Rd>, <Rm>, #imm"},
        // asr (register)
        { 0x0fef00f0, 0x01a00050, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateASRReg, "asr{s}<c> <Rd>, <Rn>, <Rm>"},
        // lsl (immediate)
        { 0x0fef0070, 0x01a00000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLSLImm, "lsl{s}<c> <Rd>, <Rm>, #imm"},
        // lsl (register)
        { 0x0fef00f0, 0x01a00010, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLSLReg, "lsl{s}<c> <Rd>, <Rn>, <Rm>"},
        // lsr (immediate)
        { 0x0fef0070, 0x01a00020, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLSRImm, "lsr{s}<c> <Rd>, <Rm>, #imm"},
        // lsr (register)
        { 0x0fef00f0, 0x01a00050, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLSRReg, "lsr{s}<c> <Rd>, <Rn>, <Rm>"},
        // rrx is a special case encoding of ror (immediate)
        { 0x0fef0ff0, 0x01a00060, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRRX, "rrx{s}<c> <Rd>, <Rm>"},
        // ror (immediate)
        { 0x0fef0070, 0x01a00060, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRORImm, "ror{s}<c> <Rd>, <Rm>, #imm"},
        // ror (register)
        { 0x0fef00f0, 0x01a00070, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateRORReg, "ror{s}<c> <Rd>, <Rn>, <Rm>"},

        //----------------------------------------------------------------------
        // Load instructions
        //----------------------------------------------------------------------
        { 0x0fd00000, 0x08900000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDM, "ldm<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x08100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDMDA, "ldmda<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x09100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDMDB, "ldmdb<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x09900000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDMIB, "ldmib<c> <Rn<{!} <registers>" },
        { 0x0e500000, 0x04100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDRImmediateARM, "ldr<c> <Rt> [<Rn> {#+/-<imm12>}]" },
        { 0x0e500010, 0x06100000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDRRegister, "ldr<c> <Rt> [<Rn> +/-<Rm> {<shift>}] {!}" },
        { 0x0e5f0000, 0x045f0000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDRBLiteral, "ldrb<c> <Rt>, [...]"},
        { 0xfe500010, 0x06500000, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateLDRBRegister, "ldrb<c> <Rt>, [<Rn>,+/-<Rm>{, <shift>}]{!}" }, 
                  
        //----------------------------------------------------------------------
        // Store instructions
        //----------------------------------------------------------------------
        { 0x0fd00000, 0x08800000, ARMvAll,      eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTM, "stm<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x08000000, ARMvAll,      eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTMDA, "stmda<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x09000000, ARMvAll,      eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTMDB, "stmdb<c> <Rn>{!} <registers>" },
        { 0x0fd00000, 0x09800000, ARMvAll,      eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTMIB, "stmib<c> <Rn>{!} <registers>" },
        { 0x0e500010, 0x06000000, ARMvAll,      eEncodingA1, eSize32, &EmulateInstructionARM::EmulateSTRRegister, "str<c> <Rt> [<Rn> +/-<Rm> {<shift>}]{!}" }
                  
        
    };
    static const size_t k_num_arm_opcodes = sizeof(g_arm_opcodes)/sizeof(ARMOpcode);
                  
    for (size_t i=0; i<k_num_arm_opcodes; ++i)
    {
        if ((g_arm_opcodes[i].mask & opcode) == g_arm_opcodes[i].value)
            return &g_arm_opcodes[i];
    }
    return NULL;
}

    
EmulateInstructionARM::ARMOpcode*
EmulateInstructionARM::GetThumbOpcodeForInstruction (const uint32_t opcode)
{

    static ARMOpcode 
    g_thumb_opcodes[] =
    {
        //----------------------------------------------------------------------
        // Prologue instructions
        //----------------------------------------------------------------------

        // push register(s)
        { 0xfffffe00, 0x0000b400, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulatePUSH, "push <registers>" },
        { 0xffff0000, 0xe92d0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulatePUSH, "push.w <registers>" },
        { 0xffff0fff, 0xf84d0d04, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulatePUSH, "push.w <register>" },

        // set r7 to point to a stack offset
        { 0xffffff00, 0x0000af00, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateADDRdSPImm, "add r7, sp, #imm" },
        // copy the stack pointer to r7
        { 0xffffffff, 0x0000466f, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateMOVRdSP, "mov r7, sp" },
        // move from high register to low register (comes after "mov r7, sp" to resolve ambiguity)
        { 0xffffffc0, 0x00004640, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateMOVLowHigh, "mov r0-r7, r8-r15" },

        // PC-relative load into register (see also EmulateADDSPRm)
        { 0xfffff800, 0x00004800, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDRRtPCRelative, "ldr <Rt>, [PC, #imm]"},

        // adjust the stack pointer
        { 0xffffff87, 0x00004485, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateADDSPRm, "add sp, <Rm>"},
        { 0xffffff80, 0x0000b080, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSUBSPImm, "add sp, sp, #imm"},
        { 0xfbef8f00, 0xf1ad0d00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateSUBSPImm, "sub.w sp, sp, #<const>"},
        { 0xfbff8f00, 0xf2ad0d00, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateSUBSPImm, "subw sp, sp, #imm12"},

        // vector push consecutive extension register(s)
        { 0xffbf0f00, 0xed2d0b00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateVPUSH, "vpush.64 <list>"},
        { 0xffbf0f00, 0xed2d0a00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateVPUSH, "vpush.32 <list>"},

        //----------------------------------------------------------------------
        // Epilogue instructions
        //----------------------------------------------------------------------

        { 0xffffff80, 0x0000b000, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateADDSPImm, "add sp, #imm"},
        { 0xfffffe00, 0x0000bc00, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulatePOP, "pop <registers>"},
        { 0xffff0000, 0xe8bd0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulatePOP, "pop.w <registers>" },
        { 0xffff0fff, 0xf85d0d04, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulatePOP, "pop.w <register>" },
        { 0xffbf0f00, 0xecbd0b00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateVPOP, "vpop.64 <list>"},
        { 0xffbf0f00, 0xecbd0a00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateVPOP, "vpop.32 <list>"},

        //----------------------------------------------------------------------
        // Supervisor Call (previously Software Interrupt)
        //----------------------------------------------------------------------
        { 0xffffff00, 0x0000df00, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSVC, "svc #imm8"},

        //----------------------------------------------------------------------
        // If Then makes up to four following instructions conditional.
        //----------------------------------------------------------------------
        { 0xffffff00, 0x0000bf00, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateIT, "it{<x>{<y>{<z>}}} <firstcond>"},

        //----------------------------------------------------------------------
        // Branch instructions
        //----------------------------------------------------------------------
        // To resolve ambiguity, "b<c> #imm8" should come after "svc #imm8".
        { 0xfffff000, 0x0000d000, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateB, "b<c> #imm8 (outside IT)"},
        { 0xffff8000, 0x0000e000, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateB, "b #imm11 (outside or last in IT)"},
        { 0xf800d000, 0xf0008000, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateB, "b<c>.w #imm8 (outside IT)"},
        { 0xf800d000, 0xf0009000, ARMV6T2_ABOVE, eEncodingT4, eSize32, &EmulateInstructionARM::EmulateB, "b.w #imm8 (outside or last in IT)"},
        // J1 == J2 == 1
        { 0xf800f800, 0xf000f800, ARMV4T_ABOVE,  eEncodingT1, eSize32, &EmulateInstructionARM::EmulateBLXImmediate, "bl <label>"},
        // J1 == J2 == 1
        { 0xf800e800, 0xf000e800, ARMV5_ABOVE,   eEncodingT2, eSize32, &EmulateInstructionARM::EmulateBLXImmediate, "blx <label>"},
        { 0xffffff87, 0x00004780, ARMV5_ABOVE,   eEncodingT1, eSize16, &EmulateInstructionARM::EmulateBLXRm, "blx <Rm>"},
        // for example, "bx lr"
        { 0xffffff87, 0x00004700, ARMvAll,       eEncodingA1, eSize32, &EmulateInstructionARM::EmulateBXRm, "bx <Rm>"},
        // compare and branch
        { 0xfffff500, 0x0000b100, ARMV6T2_ABOVE, eEncodingT1, eSize16, &EmulateInstructionARM::EmulateCB, "cb{n}z <Rn>, <label>"},
        // table branch byte
        { 0xfff0fff0, 0xe8d0f000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateTB, "tbb<c> <Rn>, <Rm>"},
        // table branch halfword
        { 0xfff0fff0, 0xe8d0f010, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateTB, "tbh<c> <Rn>, <Rm>, lsl #1"},

        //----------------------------------------------------------------------
        // Data-processing instructions
        //----------------------------------------------------------------------
        // adc (immediate)
        { 0xfbe08000, 0xf1400000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateADCImm, "adc{s}<c> <Rd>, <Rn>, #<const>"},
        // adc (register)
        { 0xffffffc0, 0x00004140, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateADCReg, "adcs|adc<c> <Rdn>, <Rm>"},
        { 0xffe08000, 0xeb400000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateADCReg, "adc{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
        // add (register)
        { 0xfffffe00, 0x00001800, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateADDReg, "adds|add<c> <Rd>, <Rn>, <Rm>"},
        // Make sure "add sp, <Rm>" comes before this instruction, so there's no ambiguity decoding the two.
        { 0xffffff00, 0x00004400, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateADDReg, "add<c> <Rdn>, <Rm>"},
        // and (immediate)
        { 0xfbe08000, 0xf0000000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateANDImm, "and{s}<c> <Rd>, <Rn>, #<const>"},
        // and (register)
        { 0xffffffc0, 0x00004000, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateANDReg, "ands|and<c> <Rdn>, <Rm>"},
        { 0xffe08000, 0xea000000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateANDReg, "and{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
        // eor (immediate)
        { 0xfbe08000, 0xf0800000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateEORImm, "eor{s}<c> <Rd>, <Rn>, #<const>"},
        // eor (register)
        { 0xffffffc0, 0x00004040, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateEORReg, "eors|eor<c> <Rdn>, <Rm>"},
        { 0xffe08000, 0xea800000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateEORReg, "eor{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
        // orr (immediate)
        { 0xfbe08000, 0xf0400000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateORRImm, "orr{s}<c> <Rd>, <Rn>, #<const>"},
        // orr (register)
        { 0xffffffc0, 0x00004300, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateORRReg, "orrs|orr<c> <Rdn>, <Rm>"},
        { 0xffe08000, 0xea400000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateORRReg, "orr{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
        // rsb (immediate)
        { 0xffffffc0, 0x00004240, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateRSBImm, "rsbs|rsb<c> <Rd>, <Rn>, #0"},
        { 0xfbe08000, 0xf1c00000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateRSBImm, "rsb{s}<c>.w <Rd>, <Rn>, #<const>"},
        // rsb (register)
        { 0xffe08000, 0xea400000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateRSBReg, "rsb{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
        // teq (immediate)
        { 0xfbf08f00, 0xf0900f00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateTEQImm, "teq<c> <Rn>, #<const>"},
        // teq (register)
        { 0xfff08f00, 0xea900f00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateTEQReg, "teq<c> <Rn>, <Rm> {,<shift>}"},
        // tst (immediate)
        { 0xfbf08f00, 0xf0100f00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateTSTImm, "tst<c> <Rn>, #<const>"},
        // tst (register)
        { 0xffffffc0, 0x00004200, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateTSTReg, "tst<c> <Rdn>, <Rm>"},
        { 0xfff08f00, 0xea100f00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateTSTReg, "tst<c>.w <Rn>, <Rm> {,<shift>}"},


        // move from high register to high register
        { 0xffffff00, 0x00004600, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateMOVRdRm, "mov<c> <Rd>, <Rm>"},
        // move from low register to low register
        { 0xffffffc0, 0x00000000, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateMOVRdRm, "movs <Rd>, <Rm>"},
        // mov{s}<c>.w <Rd>, <Rm>
        { 0xffeff0f0, 0xea4f0000, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateMOVRdRm, "mov{s}<c>.w <Rd>, <Rm>"},
        // move immediate
        { 0xfffff800, 0x00002000, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateMOVRdImm, "movs|mov<c> <Rd>, #imm8"},
        { 0xfbef8000, 0xf04f0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateMOVRdImm, "mov{s}<c>.w <Rd>, #<const>"},
        // mvn (immediate)
        { 0xfbef8000, 0xf06f0000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateMVNImm, "mvn{s} <Rd>, #<const>"},
        // mvn (register)
        { 0xffffffc0, 0x000043c0, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateMVNReg, "mvns|mvn<c> <Rd>, <Rm>"},
        { 0xffef8000, 0xea6f0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateMVNReg, "mvn{s}<c>.w <Rd>, <Rm> {,<shift>}"},
        // cmn (immediate)
        { 0xfbf08f00, 0xf1100f00, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateCMNImm, "cmn<c> <Rn>, #<const>"},
        // cmn (register)
        { 0xffffffc0, 0x000042c0, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm>"},
        { 0xfff08f00, 0xeb100f00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm> {,<shift>}"},
        // cmp (immediate)
        { 0xfffff800, 0x00002800, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateCMPImm, "cmp<c> <Rn>, #imm8"},
        { 0xfbf08f00, 0xf1b00f00, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateCMPImm, "cmp<c>.w <Rn>, #<const>"},
        // cmp (register) (Rn and Rm both from r0-r7)
        { 0xffffffc0, 0x00004280, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm>"},
        // cmp (register) (Rn and Rm not both from r0-r7)
        { 0xffffff00, 0x00004500, ARMvAll,       eEncodingT2, eSize16, &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm>"},
        // asr (immediate)
        { 0xfffff800, 0x00001000, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateASRImm, "asrs|asr<c> <Rd>, <Rm>, #imm"},
        { 0xffef8030, 0xea4f0020, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateASRImm, "asr{s}<c>.w <Rd>, <Rm>, #imm"},
        // asr (register)
        { 0xffffffc0, 0x00004100, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateASRReg, "asrs|asr<c> <Rdn>, <Rm>"},
        { 0xffe0f0f0, 0xfa40f000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateASRReg, "asr{s}<c>.w <Rd>, <Rn>, <Rm>"},
        // lsl (immediate)
        { 0xfffff800, 0x00000000, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLSLImm, "lsls|lsl<c> <Rd>, <Rm>, #imm"},
        { 0xffef8030, 0xea4f0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLSLImm, "lsl{s}<c>.w <Rd>, <Rm>, #imm"},
        // lsl (register)
        { 0xffffffc0, 0x00004080, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLSLReg, "lsls|lsl<c> <Rdn>, <Rm>"},
        { 0xffe0f0f0, 0xfa00f000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLSLReg, "lsl{s}<c>.w <Rd>, <Rn>, <Rm>"},
        // lsr (immediate)
        { 0xfffff800, 0x00000800, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLSRImm, "lsrs|lsr<c> <Rd>, <Rm>, #imm"},
        { 0xffef8030, 0xea4f0010, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLSRImm, "lsr{s}<c>.w <Rd>, <Rm>, #imm"},
        // lsr (register)
        { 0xffffffc0, 0x000040c0, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLSRReg, "lsrs|lsr<c> <Rdn>, <Rm>"},
        { 0xffe0f0f0, 0xfa20f000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLSRReg, "lsr{s}<c>.w <Rd>, <Rn>, <Rm>"},
        // rrx is a special case encoding of ror (immediate)
        { 0xffeff0f0, 0xea4f0030, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateRRX, "rrx{s}<c>.w <Rd>, <Rm>"},
        // ror (immediate)
        { 0xffef8030, 0xea4f0030, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateRORImm, "ror{s}<c>.w <Rd>, <Rm>, #imm"},
        // ror (register)
        { 0xffffffc0, 0x000041c0, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateRORReg, "rors|ror<c> <Rdn>, <Rm>"},
        { 0xffe0f0f0, 0xfa60f000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateRORReg, "ror{s}<c>.w <Rd>, <Rn>, <Rm>"},

        //----------------------------------------------------------------------
        // Load instructions
        //----------------------------------------------------------------------
        { 0xfffff800, 0x0000c800, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDM, "ldm<c> <Rn>{!} <registers>" },
        { 0xffd02000, 0xe8900000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLDM, "ldm<c>.w <Rn>{!} <registers>" },
        { 0xffd00000, 0xe9100000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateLDMDB, "ldmdb<c> <Rn>{!} <registers>" },
        { 0xfffff800, 0x00006800, ARMvAll,       eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDRRtRnImm, "ldr<c> <Rt>, [<Rn>{,#imm}]"},
        // Thumb2 PC-relative load into register
        { 0xff7f0000, 0xf85f0000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLDRRtPCRelative, "ldr<c>.w <Rt>, [PC, +/-#imm}]"},
        { 0xfffffe00, 0x00005800, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDRRegister, "ldr<c> <Rt>, [<Rn>, <Rm>]" }, 
        { 0xfff00fc0, 0xf8500000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLDRRegister, "ldr<c>.w <Rt>, [<Rn>,<Rm>{,LSL #<imm2>}]" },
        { 0xfffff800, 0x00007800, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDRBImmediate, "ldrb<c> <Rt>,[<Rn>{,#<imm5>}]" },
        { 0xfff00000, 0xf8900000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateLDRBImmediate, "ldrb<c>.w <Rt>,[<Rn>{,#<imm12>}]" },
        { 0xfff00800, 0xf8100800, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateLDRBImmediate, "ldrb<c> <Rt>,[>Rn>, #+/-<imm8>]{!}" },
        { 0xff7f0000, 0xf81f0000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateLDRBLiteral, "ldrb<c> <Rt>,[...]" },
        { 0xfffffe00, 0x00005c00, ARMV6T2_ABOVE, eEncodingT1, eSize16, &EmulateInstructionARM::EmulateLDRBRegister, "ldrb<c> <Rt>,[<Rn>,<Rm>]" },
        {  0xfff00fc0, 0xf8100000, ARMV6T2_ABOVE, eEncodingT2, eSize32,&EmulateInstructionARM::EmulateLDRBRegister, "ldrb<c>.w <Rt>,[<Rn>,<Rm>{,LSL #imm2>}]" },
                  
        //----------------------------------------------------------------------
        // Store instructions
        //----------------------------------------------------------------------
        { 0xfffff800, 0x0000c000, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSTM, "stm<c> <Rn>{!} <registers>" },
        { 0xffd00000, 0xe8800000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateSTM, "stm<c>.w <Rn>{!} <registers>" },
        { 0xffd00000, 0xe9000000, ARMV6T2_ABOVE, eEncodingT1, eSize32, &EmulateInstructionARM::EmulateSTMDB, "stmdb<c> <Rn>{!} <registers>" },
        { 0xfffff800, 0x00006000, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSTRThumb, "str<c> <Rt>, [<Rn>{,#<imm>}]" },
        { 0xfffff800, 0x00009000, ARMV4T_ABOVE,  eEncodingT2, eSize16, &EmulateInstructionARM::EmulateSTRThumb, "str<c> <Rt>, [SP,#<imm>]" },
        { 0xfff00000, 0xf8c00000, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateSTRThumb, "str<c>.w <Rt>, [<Rn>,#<imm12>]" },
        { 0xfff00800, 0xf8400800, ARMV6T2_ABOVE, eEncodingT4, eSize32, &EmulateInstructionARM::EmulateSTRThumb, "str<c> <Rt>, [<Rn>,#+/-<imm8>]" },
        { 0xfffffe00, 0x00005000, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSTRRegister, "str<c> <Rt> ,{<Rn>, <Rm>]" },
        { 0xfff00fc0, 0xf8400000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateSTRRegister, "str<c>.w <Rt>, [<Rn>, <Rm> {lsl #imm2>}]" },
        { 0xfffff800, 0x00007000, ARMV4T_ABOVE,  eEncodingT1, eSize16, &EmulateInstructionARM::EmulateSTRBThumb, "strb<c> <Rt>, [<Rn>, #<imm5>]" },
        { 0xfff00000, 0xf8800000, ARMV6T2_ABOVE, eEncodingT2, eSize32, &EmulateInstructionARM::EmulateSTRBThumb, "strb<c>.w <Rt>, [<Rn>, #<imm12>]" },
        { 0xfff00800, 0xf8000800, ARMV6T2_ABOVE, eEncodingT3, eSize32, &EmulateInstructionARM::EmulateSTRBThumb, "strb<c> <Rt> ,[<Rn>, #+/-<imm8>]{!}" }
    };

    const size_t k_num_thumb_opcodes = sizeof(g_thumb_opcodes)/sizeof(ARMOpcode);
    for (size_t i=0; i<k_num_thumb_opcodes; ++i)
    {
        if ((g_thumb_opcodes[i].mask & opcode) == g_thumb_opcodes[i].value)
            return &g_thumb_opcodes[i];
    }
    return NULL;
}

bool
EmulateInstructionARM::SetArchitecture (const ArchSpec &arch)
{
    m_arm_isa = 0;
    const char *arch_cstr = arch.GetArchitectureName ();
    if (arch_cstr)
    {
        if      (0 == ::strcasecmp(arch_cstr, "armv4t"))    m_arm_isa = ARMv4T;
        else if (0 == ::strcasecmp(arch_cstr, "armv4"))     m_arm_isa = ARMv4;
        else if (0 == ::strcasecmp(arch_cstr, "armv5tej"))  m_arm_isa = ARMv5TEJ;
        else if (0 == ::strcasecmp(arch_cstr, "armv5te"))   m_arm_isa = ARMv5TE;
        else if (0 == ::strcasecmp(arch_cstr, "armv5t"))    m_arm_isa = ARMv5T;
        else if (0 == ::strcasecmp(arch_cstr, "armv6k"))    m_arm_isa = ARMv6K;
        else if (0 == ::strcasecmp(arch_cstr, "armv6"))     m_arm_isa = ARMv6;
        else if (0 == ::strcasecmp(arch_cstr, "armv6t2"))   m_arm_isa = ARMv6T2;
        else if (0 == ::strcasecmp(arch_cstr, "armv7"))     m_arm_isa = ARMv7;
        else if (0 == ::strcasecmp(arch_cstr, "armv8"))     m_arm_isa = ARMv8;
    }
    return m_arm_isa != 0;
}


bool 
EmulateInstructionARM::ReadInstruction ()
{
    bool success = false;
    m_inst_cpsr = ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS, 0, &success);
    if (success)
    {
        addr_t pc = ReadRegisterUnsigned (eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_ADDRESS, &success);
        if (success)
        {
            Context read_inst_context;
            read_inst_context.type = eContextReadOpcode;
            read_inst_context.SetNoArgs ();
                  
            if (m_inst_cpsr & MASK_CPSR_T)
            {
                m_inst_mode = eModeThumb;
                uint32_t thumb_opcode = MemARead(read_inst_context, pc, 2, 0, &success);
                
                if (success)
                {
                    if ((m_inst.opcode.inst16 & 0xe000) != 0xe000 || ((m_inst.opcode.inst16 & 0x1800u) == 0))
                    {
                        m_inst.opcode_type = eOpcode16;
                        m_inst.opcode.inst16 = thumb_opcode;
                    }
                    else
                    {
                        m_inst.opcode_type = eOpcode32;
                        m_inst.opcode.inst32 = (thumb_opcode << 16) | MemARead(read_inst_context, pc + 2, 2, 0, &success);
                    }
                }
            }
            else
            {
                m_inst_mode = eModeARM;
                m_inst.opcode_type = eOpcode32;
                m_inst.opcode.inst32 = MemARead(read_inst_context, pc, 4, 0, &success);
            }
        }
    }
    if (!success)
    {
        m_inst_mode = eModeInvalid;
        m_inst_pc = LLDB_INVALID_ADDRESS;
    }
    return success;
}

uint32_t
EmulateInstructionARM::ArchVersion ()
{
    return m_arm_isa;
}

bool
EmulateInstructionARM::ConditionPassed ()
{
    if (m_inst_cpsr == 0)
        return false;

    const uint32_t cond = CurrentCond ();
    
    if (cond == UINT32_MAX)
        return false;
    
    bool result = false;
    switch (UnsignedBits(cond, 3, 1))
    {
    case 0: result = (m_inst_cpsr & MASK_CPSR_Z) != 0; break;
    case 1: result = (m_inst_cpsr & MASK_CPSR_C) != 0; break;
    case 2: result = (m_inst_cpsr & MASK_CPSR_N) != 0; break;
    case 3: result = (m_inst_cpsr & MASK_CPSR_V) != 0; break;
    case 4: result = ((m_inst_cpsr & MASK_CPSR_C) != 0) && ((m_inst_cpsr & MASK_CPSR_Z) == 0); break;
    case 5: 
        {
            bool n = (m_inst_cpsr & MASK_CPSR_N);
            bool v = (m_inst_cpsr & MASK_CPSR_V);
            result = n == v;
        }
        break;
    case 6: 
        {
            bool n = (m_inst_cpsr & MASK_CPSR_N);
            bool v = (m_inst_cpsr & MASK_CPSR_V);
            result = n == v && ((m_inst_cpsr & MASK_CPSR_Z) == 0);
        }
        break;
    case 7: 
        result = true; 
        break;
    }

    if (cond & 1)
        result = !result;
    return result;
}

uint32_t
EmulateInstructionARM::CurrentCond ()
{
    switch (m_inst_mode)
    {
    default:
    case eModeInvalid:
        break;

    case eModeARM:
        return UnsignedBits(m_inst.opcode.inst32, 31, 28);
    
    case eModeThumb:
        // For T1 and T3 encodings of the Branch instruction, it returns the 4-bit
        // 'cond' field of the encoding.
        if (m_inst.opcode_type == eOpcode16 &&
            Bits32(m_inst.opcode.inst16, 15, 12) == 0x0d &&
            Bits32(m_inst.opcode.inst16, 11, 7) != 0x0f)
        {
            return Bits32(m_inst.opcode.inst16, 11, 7);
        }
        else if (m_inst.opcode_type == eOpcode32 &&
                 Bits32(m_inst.opcode.inst32, 31, 27) == 0x1e &&
                 Bits32(m_inst.opcode.inst32, 15, 14) == 0x02 &&
                 Bits32(m_inst.opcode.inst32, 12, 12) == 0x00 &&
                 Bits32(m_inst.opcode.inst32, 25, 22) <= 0x0d)
        {
            return Bits32(m_inst.opcode.inst32, 25, 22);
        }
        
        return m_it_session.GetCond();
    }
    return UINT32_MAX;  // Return invalid value
}

bool
EmulateInstructionARM::InITBlock()
{
    return CurrentInstrSet() == eModeThumb && m_it_session.InITBlock();
}

bool
EmulateInstructionARM::LastInITBlock()
{
    return CurrentInstrSet() == eModeThumb && m_it_session.LastInITBlock();
}

bool
EmulateInstructionARM::BranchWritePC (const Context &context, uint32_t addr)
{
    addr_t target;

    // Check the current instruction set.
    if (CurrentInstrSet() == eModeARM)
        target = addr & 0xfffffffc;
    else
        target = addr & 0xfffffffe;

    if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, target))
        return false;

    return true;
}

// As a side effect, BXWritePC sets context.arg2 to eModeARM or eModeThumb by inspecting addr.
bool
EmulateInstructionARM::BXWritePC (Context &context, uint32_t addr)
{
    addr_t target;
    // If the CPSR is changed due to switching between ARM and Thumb ISETSTATE,
    // we want to record it and issue a WriteRegister callback so the clients
    // can track the mode changes accordingly.
    bool cpsr_changed = false;

    if (BitIsSet(addr, 0))
    {
        if (CurrentInstrSet() != eModeThumb)
        {
            SelectInstrSet(eModeThumb);
            cpsr_changed = true;
        }
        target = addr & 0xfffffffe;
        context.SetMode (eModeThumb);
    }
    else if (BitIsClear(addr, 1))
    {
        if (CurrentInstrSet() != eModeARM)
        {
            SelectInstrSet(eModeARM);
            cpsr_changed = true;
        }
        target = addr & 0xfffffffc;
        context.SetMode (eModeARM);
    }
    else
        return false; // address<1:0> == '10' => UNPREDICTABLE

    if (cpsr_changed)
    {
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
            return false;
    }
    if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, target))
        return false;

    return true;
}

// Dispatches to either BXWritePC or BranchWritePC based on architecture versions.
bool
EmulateInstructionARM::LoadWritePC (Context &context, uint32_t addr)
{
    if (ArchVersion() >= ARMv5T)
        return BXWritePC(context, addr);
    else
        return BranchWritePC((const Context)context, addr);
}

// Dispatches to either BXWritePC or BranchWritePC based on architecture versions and current instruction set.
bool
EmulateInstructionARM::ALUWritePC (Context &context, uint32_t addr)
{
    if (ArchVersion() >= ARMv7 && CurrentInstrSet() == eModeARM)
        return BXWritePC(context, addr);
    else
        return BranchWritePC((const Context)context, addr);
}

EmulateInstructionARM::Mode
EmulateInstructionARM::CurrentInstrSet ()
{
    return m_inst_mode;
}

// Set the 'T' bit of our CPSR.  The m_inst_mode gets updated when the next
// ReadInstruction() is performed.  This function has a side effect of updating
// the m_new_inst_cpsr member variable if necessary.
bool
EmulateInstructionARM::SelectInstrSet (Mode arm_or_thumb)
{
    m_new_inst_cpsr = m_inst_cpsr;
    switch (arm_or_thumb)
    {
    default:
        return false;
    eModeARM:
        // Clear the T bit.
        m_new_inst_cpsr &= ~MASK_CPSR_T;
        break;
    eModeThumb:
        // Set the T bit.
        m_new_inst_cpsr |= MASK_CPSR_T;
        break;
    }
    return true;
}

// This function returns TRUE if the processor currently provides support for
// unaligned memory accesses, or FALSE otherwise. This is always TRUE in ARMv7,
// controllable by the SCTLR.U bit in ARMv6, and always FALSE before ARMv6.
bool
EmulateInstructionARM::UnalignedSupport()
{
    return (ArchVersion() >= ARMv7);
}

// The main addition and subtraction instructions can produce status information
// about both unsigned carry and signed overflow conditions.  This status
// information can be used to synthesize multi-word additions and subtractions.
EmulateInstructionARM::AddWithCarryResult
EmulateInstructionARM::AddWithCarry (uint32_t x, uint32_t y, uint8_t carry_in)
{
    uint32_t result;
    uint8_t carry_out;
    uint8_t overflow;

    uint64_t unsigned_sum = x + y + carry_in;
    int64_t signed_sum = (int32_t)x + (int32_t)y + (int32_t)carry_in;
    
    result = UnsignedBits(unsigned_sum, 31, 0);
    carry_out = (result == unsigned_sum ? 0 : 1);
    overflow = ((int32_t)result == signed_sum ? 0 : 1);
    
    AddWithCarryResult res = { result, carry_out, overflow };
    return res;
}

uint32_t
EmulateInstructionARM::ReadCoreReg(uint32_t num, bool *success)
{
    uint32_t reg_kind, reg_num;
    switch (num)
    {
    case SP_REG:
        reg_kind = eRegisterKindGeneric;
        reg_num  = LLDB_REGNUM_GENERIC_SP;
        break;
    case LR_REG:
        reg_kind = eRegisterKindGeneric;
        reg_num  = LLDB_REGNUM_GENERIC_RA;
        break;
    case PC_REG:
        reg_kind = eRegisterKindGeneric;
        reg_num  = LLDB_REGNUM_GENERIC_PC;
        break;
    default:
        if (0 <= num && num < SP_REG)
        {
            reg_kind = eRegisterKindDWARF;
            reg_num  = dwarf_r0 + num;
        }
        else
        {
            assert(0 && "Invalid register number");
            *success = false;
            return ~0u;
        }
        break;
    }

    // Read our register.
    uint32_t val = ReadRegisterUnsigned (reg_kind, reg_num, 0, success);

    // When executing an ARM instruction , PC reads as the address of the current
    // instruction plus 8.
    // When executing a Thumb instruction , PC reads as the address of the current
    // instruction plus 4.
    if (num == 15)
    {
        if (CurrentInstrSet() == eModeARM)
            val += 8;
        else
            val += 4;
    }

    return val;
}

// Write the result to the ARM core register Rd, and optionally update the
// condition flags based on the result.
//
// This helper method tries to encapsulate the following pseudocode from the
// ARM Architecture Reference Manual:
//
// if d == 15 then         // Can only occur for encoding A1
//     ALUWritePC(result); // setflags is always FALSE here
// else
//     R[d] = result;
//     if setflags then
//         APSR.N = result<31>;
//         APSR.Z = IsZeroBit(result);
//         APSR.C = carry;
//         // APSR.V unchanged
//
// In the above case, the API client does not pass in the overflow arg, which
// defaults to ~0u.
bool
EmulateInstructionARM::WriteCoreRegOptionalFlags (Context &context,
                                                  const uint32_t result,
                                                  const uint32_t Rd,
                                                  bool setflags,
                                                  const uint32_t carry,
                                                  const uint32_t overflow)
{
    if (Rd == 15)
    {
        if (!ALUWritePC (context, result))
            return false;
    }
    else
    {
        if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + Rd, result))
            return false;
        if (setflags)
            return WriteFlags (context, result, carry, overflow);
    }
    return true;
}

// This helper method tries to encapsulate the following pseudocode from the
// ARM Architecture Reference Manual:
//
// APSR.N = result<31>;
// APSR.Z = IsZeroBit(result);
// APSR.C = carry;
// APSR.V = overflow
//
// Default arguments can be specified for carry and overflow parameters, which means
// not to update the respective flags.
bool
EmulateInstructionARM::WriteFlags (Context &context,
                                   const uint32_t result,
                                   const uint32_t carry,
                                   const uint32_t overflow)
{
    m_new_inst_cpsr = m_inst_cpsr;
    SetBit32(m_new_inst_cpsr, CPSR_N_POS, Bit32(result, CPSR_N_POS));
    SetBit32(m_new_inst_cpsr, CPSR_Z_POS, result == 0 ? 1 : 0);
    if (carry != ~0u)
        SetBit32(m_new_inst_cpsr, CPSR_C_POS, carry);
    if (overflow != ~0u)
        SetBit32(m_new_inst_cpsr, CPSR_V_POS, overflow);
    if (m_new_inst_cpsr != m_inst_cpsr)
    {
        if (!WriteRegisterUnsigned (context, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
            return false;
    }
    return true;
}

bool
EmulateInstructionARM::EvaluateInstruction ()
{
    // Advance the ITSTATE bits to their values for the next instruction.
    if (m_inst_mode == eModeThumb && m_it_session.InITBlock())
        m_it_session.ITAdvance();

    return false;
}
