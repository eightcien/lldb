//===-- DNBArchImplI386.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/25/07.
//
//===----------------------------------------------------------------------===//

#if defined (__i386__) || defined (__x86_64__)

#include <sys/cdefs.h>

#include "MacOSX/i386/DNBArchImplI386.h"
#include "DNBLog.h"
#include "MachThread.h"
#include "MachProcess.h"

enum
{
    gpr_eax         = 0,
    gpr_ebx         = 1,
    gpr_ecx         = 2,
    gpr_edx         = 3,
    gpr_edi         = 4,
    gpr_esi         = 5,
    gpr_ebp         = 6,
    gpr_esp         = 7,
    gpr_ss          = 8,
    gpr_eflags      = 9,
    gpr_eip         = 10,
    gpr_cs          = 11,
    gpr_ds          = 12,
    gpr_es          = 13,
    gpr_fs          = 14,
    gpr_gs          = 15,
    k_num_gpr_regs
};

enum {
    fpu_fcw,
    fpu_fsw,
    fpu_ftw,
    fpu_fop,
    fpu_ip,
    fpu_cs,
    fpu_dp,
    fpu_ds,
    fpu_mxcsr,
    fpu_mxcsrmask,
    fpu_stmm0,
    fpu_stmm1,
    fpu_stmm2,
    fpu_stmm3,
    fpu_stmm4,
    fpu_stmm5,
    fpu_stmm6,
    fpu_stmm7,
    fpu_xmm0,
    fpu_xmm1,
    fpu_xmm2,
    fpu_xmm3,
    fpu_xmm4,
    fpu_xmm5,
    fpu_xmm6,
    fpu_xmm7,
    k_num_fpu_regs,

    // Aliases
    fpu_fctrl = fpu_fcw,
    fpu_fstat = fpu_fsw,
    fpu_ftag  = fpu_ftw,
    fpu_fiseg = fpu_cs,
    fpu_fioff = fpu_ip,
    fpu_foseg = fpu_ds,
    fpu_fooff = fpu_dp
};

enum {
    exc_trapno,
    exc_err,
    exc_faultvaddr,
    k_num_exc_regs,
};


enum
{
    gcc_eax = 0,
    gcc_ecx,
    gcc_edx,
    gcc_ebx,
    gcc_ebp,
    gcc_esp,
    gcc_esi,
    gcc_edi,
    gcc_eip,
    gcc_eflags
};

enum
{
    dwarf_eax = 0,
    dwarf_ecx,
    dwarf_edx,
    dwarf_ebx,
    dwarf_esp,
    dwarf_ebp,
    dwarf_esi,
    dwarf_edi,
    dwarf_eip,
    dwarf_eflags,
    dwarf_stmm0 = 11,
    dwarf_stmm1,
    dwarf_stmm2,
    dwarf_stmm3,
    dwarf_stmm4,
    dwarf_stmm5,
    dwarf_stmm6,
    dwarf_stmm7,
    dwarf_xmm0 = 21,
    dwarf_xmm1,
    dwarf_xmm2,
    dwarf_xmm3,
    dwarf_xmm4,
    dwarf_xmm5,
    dwarf_xmm6,
    dwarf_xmm7
};

enum
{
    gdb_eax        =  0,
    gdb_ecx        =  1,
    gdb_edx        =  2,
    gdb_ebx        =  3,
    gdb_esp        =  4,
    gdb_ebp        =  5,
    gdb_esi        =  6,
    gdb_edi        =  7,
    gdb_eip        =  8,
    gdb_eflags     =  9,
    gdb_cs         = 10,
    gdb_ss         = 11,
    gdb_ds         = 12,
    gdb_es         = 13,
    gdb_fs         = 14,
    gdb_gs         = 15,
    gdb_stmm0      = 16,
    gdb_stmm1      = 17,
    gdb_stmm2      = 18,
    gdb_stmm3      = 19,
    gdb_stmm4      = 20,
    gdb_stmm5      = 21,
    gdb_stmm6      = 22,
    gdb_stmm7      = 23,
    gdb_fctrl      = 24,    gdb_fcw     = gdb_fctrl,
    gdb_fstat      = 25,    gdb_fsw     = gdb_fstat,
    gdb_ftag       = 26,    gdb_ftw     = gdb_ftag,
    gdb_fiseg      = 27,    gdb_fpu_cs  = gdb_fiseg,
    gdb_fioff      = 28,    gdb_ip      = gdb_fioff,
    gdb_foseg      = 29,    gdb_fpu_ds  = gdb_foseg,
    gdb_fooff      = 30,    gdb_dp      = gdb_fooff,
    gdb_fop        = 31,
    gdb_xmm0       = 32,
    gdb_xmm1       = 33,
    gdb_xmm2       = 34,
    gdb_xmm3       = 35,
    gdb_xmm4       = 36,
    gdb_xmm5       = 37,
    gdb_xmm6       = 38,
    gdb_xmm7       = 39,
    gdb_mxcsr      = 40,
    gdb_mm0        = 41,
    gdb_mm1        = 42,
    gdb_mm2        = 43,
    gdb_mm3        = 44,
    gdb_mm4        = 45,
    gdb_mm5        = 46,
    gdb_mm6        = 47,
    gdb_mm7        = 48
};

uint64_t
DNBArchImplI386::GetPC(uint64_t failValue)
{
    // Get program counter
    if (GetGPRState(false) == KERN_SUCCESS)
        return m_state.context.gpr.__eip;
    return failValue;
}

kern_return_t
DNBArchImplI386::SetPC(uint64_t value)
{
    // Get program counter
    kern_return_t err = GetGPRState(false);
    if (err == KERN_SUCCESS)
    {
        m_state.context.gpr.__eip = value;
        err = SetGPRState();
    }
    return err == KERN_SUCCESS;
}

uint64_t
DNBArchImplI386::GetSP(uint64_t failValue)
{
    // Get stack pointer
    if (GetGPRState(false) == KERN_SUCCESS)
        return m_state.context.gpr.__esp;
    return failValue;
}

// Uncomment the value below to verify the values in the debugger.
//#define DEBUG_GPR_VALUES 1    // DO NOT CHECK IN WITH THIS DEFINE ENABLED
//#define SET_GPR(reg) m_state.context.gpr.__##reg = gpr_##reg

kern_return_t
DNBArchImplI386::GetGPRState(bool force)
{
    if (force || m_state.GetError(e_regSetGPR, Read))
    {
#if DEBUG_GPR_VALUES
        SET_GPR(eax);
        SET_GPR(ebx);
        SET_GPR(ecx);
        SET_GPR(edx);
        SET_GPR(edi);
        SET_GPR(esi);
        SET_GPR(ebp);
        SET_GPR(esp);
        SET_GPR(ss);
        SET_GPR(eflags);
        SET_GPR(eip);
        SET_GPR(cs);
        SET_GPR(ds);
        SET_GPR(es);
        SET_GPR(fs);
        SET_GPR(gs);
        m_state.SetError(e_regSetGPR, Read, 0);
#else
        mach_msg_type_number_t count = e_regSetWordSizeGPR;
        m_state.SetError(e_regSetGPR, Read, ::thread_get_state(m_thread->ThreadID(), x86_THREAD_STATE32, (thread_state_t)&m_state.context.gpr, &count));
#endif
    }
    return m_state.GetError(e_regSetGPR, Read);
}

// Uncomment the value below to verify the values in the debugger.
//#define DEBUG_FPU_VALUES 1    // DO NOT CHECK IN WITH THIS DEFINE ENABLED

kern_return_t
DNBArchImplI386::GetFPUState(bool force)
{
    if (force || m_state.GetError(e_regSetFPU, Read))
    {
#if DEBUG_FPU_VALUES
    m_state.context.fpu.__fpu_reserved[0] = -1;
    m_state.context.fpu.__fpu_reserved[1] = -1;
    *(uint16_t *)&(m_state.context.fpu.__fpu_fcw) = 0x1234;
    *(uint16_t *)&(m_state.context.fpu.__fpu_fsw) = 0x5678;
    m_state.context.fpu.__fpu_ftw = 1;
    m_state.context.fpu.__fpu_rsrv1 = UINT8_MAX;
    m_state.context.fpu.__fpu_fop = 2;
    m_state.context.fpu.__fpu_ip = 3;
    m_state.context.fpu.__fpu_cs = 4;
    m_state.context.fpu.__fpu_rsrv2 = 5;
    m_state.context.fpu.__fpu_dp = 6;
    m_state.context.fpu.__fpu_ds = 7;
    m_state.context.fpu.__fpu_rsrv3 = UINT16_MAX;
    m_state.context.fpu.__fpu_mxcsr = 8;
    m_state.context.fpu.__fpu_mxcsrmask = 9;
    int i;
    for (i=0; i<16; ++i)
    {
        if (i<10)
        {
            m_state.context.fpu.__fpu_stmm0.__mmst_reg[i] = 'a';
            m_state.context.fpu.__fpu_stmm1.__mmst_reg[i] = 'b';
            m_state.context.fpu.__fpu_stmm2.__mmst_reg[i] = 'c';
            m_state.context.fpu.__fpu_stmm3.__mmst_reg[i] = 'd';
            m_state.context.fpu.__fpu_stmm4.__mmst_reg[i] = 'e';
            m_state.context.fpu.__fpu_stmm5.__mmst_reg[i] = 'f';
            m_state.context.fpu.__fpu_stmm6.__mmst_reg[i] = 'g';
            m_state.context.fpu.__fpu_stmm7.__mmst_reg[i] = 'h';
        }
        else
        {
            m_state.context.fpu.__fpu_stmm0.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm1.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm2.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm3.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm4.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm5.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm6.__mmst_reg[i] = INT8_MIN;
            m_state.context.fpu.__fpu_stmm7.__mmst_reg[i] = INT8_MIN;
        }

        m_state.context.fpu.__fpu_xmm0.__xmm_reg[i] = '0';
        m_state.context.fpu.__fpu_xmm1.__xmm_reg[i] = '1';
        m_state.context.fpu.__fpu_xmm2.__xmm_reg[i] = '2';
        m_state.context.fpu.__fpu_xmm3.__xmm_reg[i] = '3';
        m_state.context.fpu.__fpu_xmm4.__xmm_reg[i] = '4';
        m_state.context.fpu.__fpu_xmm5.__xmm_reg[i] = '5';
        m_state.context.fpu.__fpu_xmm6.__xmm_reg[i] = '6';
        m_state.context.fpu.__fpu_xmm7.__xmm_reg[i] = '7';
    }
    for (i=0; i<sizeof(m_state.context.fpu.__fpu_rsrv4); ++i)
        m_state.context.fpu.__fpu_rsrv4[i] = INT8_MIN;
    m_state.context.fpu.__fpu_reserved1 = -1;
    m_state.SetError(e_regSetFPU, Read, 0);
#else
        mach_msg_type_number_t count = e_regSetWordSizeFPR;
        m_state.SetError(e_regSetFPU, Read, ::thread_get_state(m_thread->ThreadID(), x86_FLOAT_STATE32, (thread_state_t)&m_state.context.fpu, &count));
#endif
    }
    return m_state.GetError(e_regSetFPU, Read);
}

kern_return_t
DNBArchImplI386::GetEXCState(bool force)
{
    if (force || m_state.GetError(e_regSetEXC, Read))
    {
        mach_msg_type_number_t count = e_regSetWordSizeEXC;
        m_state.SetError(e_regSetEXC, Read, ::thread_get_state(m_thread->ThreadID(), x86_EXCEPTION_STATE32, (thread_state_t)&m_state.context.exc, &count));
    }
    return m_state.GetError(e_regSetEXC, Read);
}

kern_return_t
DNBArchImplI386::SetGPRState()
{
    m_state.SetError(e_regSetGPR, Write, ::thread_set_state(m_thread->ThreadID(), x86_THREAD_STATE32, (thread_state_t)&m_state.context.gpr, e_regSetWordSizeGPR));
    return m_state.GetError(e_regSetGPR, Write);
}

kern_return_t
DNBArchImplI386::SetFPUState()
{
    m_state.SetError(e_regSetFPU, Write, ::thread_set_state(m_thread->ThreadID(), x86_FLOAT_STATE32, (thread_state_t)&m_state.context.fpu, e_regSetWordSizeFPR));
    return m_state.GetError(e_regSetFPU, Write);
}

kern_return_t
DNBArchImplI386::SetEXCState()
{
    m_state.SetError(e_regSetEXC, Write, ::thread_set_state(m_thread->ThreadID(), x86_EXCEPTION_STATE32, (thread_state_t)&m_state.context.exc, e_regSetWordSizeEXC));
    return m_state.GetError(e_regSetEXC, Write);
}

void
DNBArchImplI386::ThreadWillResume()
{
    // Do we need to step this thread? If so, let the mach thread tell us so.
    if (m_thread->IsStepping())
    {
        // This is the primary thread, let the arch do anything it needs
        EnableHardwareSingleStep(true) == KERN_SUCCESS;
    }
}

bool
DNBArchImplI386::ThreadDidStop()
{
    bool success = true;

    m_state.InvalidateAllRegisterStates();

    // Are we stepping a single instruction?
    if (GetGPRState(true) == KERN_SUCCESS)
    {
        // We are single stepping, was this the primary thread?
        if (m_thread->IsStepping())
        {
            // This was the primary thread, we need to clear the trace
            // bit if so.
            success = EnableHardwareSingleStep(false) == KERN_SUCCESS;
        }
        else
        {
            // The MachThread will automatically restore the suspend count
            // in ThreadDidStop(), so we don't need to do anything here if
            // we weren't the primary thread the last time
        }
    }
    return success;
}

bool
DNBArchImplI386::NotifyException(MachException::Data& exc)
{
    switch (exc.exc_type)
    {
    case EXC_BAD_ACCESS:
        break;
    case EXC_BAD_INSTRUCTION:
        break;
    case EXC_ARITHMETIC:
        break;
    case EXC_EMULATION:
        break;
    case EXC_SOFTWARE:
        break;
    case EXC_BREAKPOINT:
        if (exc.exc_data.size() >= 2 && exc.exc_data[0] == 2)
        {
            nub_addr_t pc = GetPC(INVALID_NUB_ADDRESS);
            if (pc != INVALID_NUB_ADDRESS && pc > 0)
            {
                pc -= 1;
                // Check for a breakpoint at one byte prior to the current PC value
                // since the PC will be just past the trap.

                nub_break_t breakID = m_thread->Process()->Breakpoints().FindIDByAddress(pc);
                if (NUB_BREAK_ID_IS_VALID(breakID))
                {
                    // Backup the PC for i386 since the trap was taken and the PC
                    // is at the address following the single byte trap instruction.
                    if (m_state.context.gpr.__eip > 0)
                    {
                        m_state.context.gpr.__eip = pc;
                        // Write the new PC back out
                        SetGPRState ();
                    }
                }
                return true;
            }
        }
        break;
    case EXC_SYSCALL:
        break;
    case EXC_MACH_SYSCALL:
        break;
    case EXC_RPC_ALERT:
        break;
    }
    return false;
}


// Set the single step bit in the processor status register.
kern_return_t
DNBArchImplI386::EnableHardwareSingleStep (bool enable)
{
    if (GetGPRState(false) == KERN_SUCCESS)
    {
        const uint32_t trace_bit = 0x100u;
        if (enable)
            m_state.context.gpr.__eflags |= trace_bit;
        else
            m_state.context.gpr.__eflags &= ~trace_bit;
        return SetGPRState();
    }
    return m_state.GetError(e_regSetGPR, Read);
}


//----------------------------------------------------------------------
// Register information defintions
//----------------------------------------------------------------------


#define GPR_OFFSET(reg) (offsetof (DNBArchImplI386::GPR, __##reg))
#define FPU_OFFSET(reg) (offsetof (DNBArchImplI386::FPU, __fpu_##reg) + offsetof (DNBArchImplI386::Context, fpu))
#define EXC_OFFSET(reg) (offsetof (DNBArchImplI386::EXC, __##reg)     + offsetof (DNBArchImplI386::Context, exc))

#define GPR_SIZE(reg)       (sizeof(((DNBArchImplI386::GPR *)NULL)->__##reg))
#define FPU_SIZE_UINT(reg)  (sizeof(((DNBArchImplI386::FPU *)NULL)->__fpu_##reg))
#define FPU_SIZE_MMST(reg)  (sizeof(((DNBArchImplI386::FPU *)NULL)->__fpu_##reg.__mmst_reg))
#define FPU_SIZE_XMM(reg)   (sizeof(((DNBArchImplI386::FPU *)NULL)->__fpu_##reg.__xmm_reg))
#define EXC_SIZE(reg)       (sizeof(((DNBArchImplI386::EXC *)NULL)->__##reg))

// These macros will auto define the register name, alt name, register size,
// register offset, encoding, format and native register. This ensures that
// the register state structures are defined correctly and have the correct
// sizes and offsets.

// General purpose registers for 64 bit
const DNBRegisterInfo
DNBArchImplI386::g_gpr_registers[] =
{
{ e_regSetGPR, gpr_eax,     "eax"   , NULL      , Uint, Hex, GPR_SIZE(eax),     GPR_OFFSET(eax)     , gcc_eax   , dwarf_eax     , -1                    , gdb_eax   },
{ e_regSetGPR, gpr_ebx,     "ebx"   , NULL      , Uint, Hex, GPR_SIZE(ebx),     GPR_OFFSET(ebx)     , gcc_ebx   , dwarf_ebx     , -1                    , gdb_ebx   },
{ e_regSetGPR, gpr_ecx,     "ecx"   , NULL      , Uint, Hex, GPR_SIZE(ecx),     GPR_OFFSET(ecx)     , gcc_ecx   , dwarf_ecx     , -1                    , gdb_ecx   },
{ e_regSetGPR, gpr_edx,     "edx"   , NULL      , Uint, Hex, GPR_SIZE(edx),     GPR_OFFSET(edx)     , gcc_edx   , dwarf_edx     , -1                    , gdb_edx   },
{ e_regSetGPR, gpr_edi,     "edi"   , NULL      , Uint, Hex, GPR_SIZE(edi),     GPR_OFFSET(edi)     , gcc_edi   , dwarf_edi     , -1                    , gdb_edi   },
{ e_regSetGPR, gpr_esi,     "esi"   , NULL      , Uint, Hex, GPR_SIZE(esi),     GPR_OFFSET(esi)     , gcc_esi   , dwarf_esi     , -1                    , gdb_esi   },
{ e_regSetGPR, gpr_ebp,     "ebp"   , "fp"      , Uint, Hex, GPR_SIZE(ebp),     GPR_OFFSET(ebp)     , gcc_ebp   , dwarf_ebp     , GENERIC_REGNUM_FP     , gdb_ebp   },
{ e_regSetGPR, gpr_esp,     "esp"   , "sp"      , Uint, Hex, GPR_SIZE(esp),     GPR_OFFSET(esp)     , gcc_esp   , dwarf_esp     , GENERIC_REGNUM_SP     , gdb_esp   },
{ e_regSetGPR, gpr_ss,      "ss"    , NULL      , Uint, Hex, GPR_SIZE(ss),      GPR_OFFSET(ss)      , -1        , -1            , -1                    , gdb_ss    },
{ e_regSetGPR, gpr_eflags,  "eflags", "flags"   , Uint, Hex, GPR_SIZE(eflags),  GPR_OFFSET(eflags)  , gcc_eflags, dwarf_eflags  , GENERIC_REGNUM_FLAGS  , gdb_eflags},
{ e_regSetGPR, gpr_eip,     "eip"   , "pc"      , Uint, Hex, GPR_SIZE(eip),     GPR_OFFSET(eip)     , gcc_eip   , dwarf_eip     , GENERIC_REGNUM_PC     , gdb_eip   },
{ e_regSetGPR, gpr_cs,      "cs"    , NULL      , Uint, Hex, GPR_SIZE(cs),      GPR_OFFSET(cs)      , -1        , -1            , -1                    , gdb_cs    },
{ e_regSetGPR, gpr_ds,      "ds"    , NULL      , Uint, Hex, GPR_SIZE(ds),      GPR_OFFSET(ds)      , -1        , -1            , -1                    , gdb_ds    },
{ e_regSetGPR, gpr_es,      "es"    , NULL      , Uint, Hex, GPR_SIZE(es),      GPR_OFFSET(es)      , -1        , -1            , -1                    , gdb_es    },
{ e_regSetGPR, gpr_fs,      "fs"    , NULL      , Uint, Hex, GPR_SIZE(fs),      GPR_OFFSET(fs)      , -1        , -1            , -1                    , gdb_fs    },
{ e_regSetGPR, gpr_gs,      "gs"    , NULL      , Uint, Hex, GPR_SIZE(gs),      GPR_OFFSET(gs)      , -1        , -1            , -1                    , gdb_gs    }
};


const DNBRegisterInfo
DNBArchImplI386::g_fpu_registers[] =
{
{ e_regSetFPU, fpu_fcw      , "fctrl"       , NULL, Uint, Hex, FPU_SIZE_UINT(fcw)       , FPU_OFFSET(fcw)       , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_fsw      , "fstat"       , NULL, Uint, Hex, FPU_SIZE_UINT(fsw)       , FPU_OFFSET(fsw)       , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_ftw      , "ftag"        , NULL, Uint, Hex, FPU_SIZE_UINT(ftw)       , FPU_OFFSET(ftw)       , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_fop      , "fop"         , NULL, Uint, Hex, FPU_SIZE_UINT(fop)       , FPU_OFFSET(fop)       , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_ip       , "fioff"       , NULL, Uint, Hex, FPU_SIZE_UINT(ip)        , FPU_OFFSET(ip)        , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_cs       , "fiseg"       , NULL, Uint, Hex, FPU_SIZE_UINT(cs)        , FPU_OFFSET(cs)        , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_dp       , "fooff"       , NULL, Uint, Hex, FPU_SIZE_UINT(dp)        , FPU_OFFSET(dp)        , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_ds       , "foseg"       , NULL, Uint, Hex, FPU_SIZE_UINT(ds)        , FPU_OFFSET(ds)        , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_mxcsr    , "mxcsr"       , NULL, Uint, Hex, FPU_SIZE_UINT(mxcsr)     , FPU_OFFSET(mxcsr)     , -1, -1, -1, -1 },
{ e_regSetFPU, fpu_mxcsrmask, "mxcsrmask"   , NULL, Uint, Hex, FPU_SIZE_UINT(mxcsrmask) , FPU_OFFSET(mxcsrmask) , -1, -1, -1, -1 },

{ e_regSetFPU, fpu_stmm0, "stmm0", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm0), FPU_OFFSET(stmm0), -1, dwarf_stmm0, -1, gdb_stmm0 },
{ e_regSetFPU, fpu_stmm1, "stmm1", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm1), FPU_OFFSET(stmm1), -1, dwarf_stmm1, -1, gdb_stmm1 },
{ e_regSetFPU, fpu_stmm2, "stmm2", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm2), FPU_OFFSET(stmm2), -1, dwarf_stmm2, -1, gdb_stmm2 },
{ e_regSetFPU, fpu_stmm3, "stmm3", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm3), FPU_OFFSET(stmm3), -1, dwarf_stmm3, -1, gdb_stmm3 },
{ e_regSetFPU, fpu_stmm4, "stmm4", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm4), FPU_OFFSET(stmm4), -1, dwarf_stmm4, -1, gdb_stmm4 },
{ e_regSetFPU, fpu_stmm5, "stmm5", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm5), FPU_OFFSET(stmm5), -1, dwarf_stmm5, -1, gdb_stmm5 },
{ e_regSetFPU, fpu_stmm6, "stmm6", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm6), FPU_OFFSET(stmm6), -1, dwarf_stmm6, -1, gdb_stmm6 },
{ e_regSetFPU, fpu_stmm7, "stmm7", NULL, Vector, VectorOfUInt8, FPU_SIZE_MMST(stmm7), FPU_OFFSET(stmm7), -1, dwarf_stmm7, -1, gdb_stmm7 },

{ e_regSetFPU, fpu_xmm0, "xmm0", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm0), FPU_OFFSET(xmm0), -1, dwarf_xmm0, -1, gdb_xmm0 },
{ e_regSetFPU, fpu_xmm1, "xmm1", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm1), FPU_OFFSET(xmm1), -1, dwarf_xmm1, -1, gdb_xmm1 },
{ e_regSetFPU, fpu_xmm2, "xmm2", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm2), FPU_OFFSET(xmm2), -1, dwarf_xmm2, -1, gdb_xmm2 },
{ e_regSetFPU, fpu_xmm3, "xmm3", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm3), FPU_OFFSET(xmm3), -1, dwarf_xmm3, -1, gdb_xmm3 },
{ e_regSetFPU, fpu_xmm4, "xmm4", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm4), FPU_OFFSET(xmm4), -1, dwarf_xmm4, -1, gdb_xmm4 },
{ e_regSetFPU, fpu_xmm5, "xmm5", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm5), FPU_OFFSET(xmm5), -1, dwarf_xmm5, -1, gdb_xmm5 },
{ e_regSetFPU, fpu_xmm6, "xmm6", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm6), FPU_OFFSET(xmm6), -1, dwarf_xmm6, -1, gdb_xmm6 },
{ e_regSetFPU, fpu_xmm7, "xmm7", NULL, Vector, VectorOfUInt8, FPU_SIZE_XMM(xmm7), FPU_OFFSET(xmm7), -1, dwarf_xmm7, -1, gdb_xmm7 }
};



const DNBRegisterInfo
DNBArchImplI386::g_exc_registers[] =
{
{ e_regSetEXC, exc_trapno,      "trapno"    , NULL, Uint, Hex, EXC_SIZE (trapno)    , EXC_OFFSET (trapno)       , -1, -1, -1, -1 },
{ e_regSetEXC, exc_err,         "err"       , NULL, Uint, Hex, EXC_SIZE (err)       , EXC_OFFSET (err)          , -1, -1, -1, -1 },
{ e_regSetEXC, exc_faultvaddr,  "faultvaddr", NULL, Uint, Hex, EXC_SIZE (faultvaddr), EXC_OFFSET (faultvaddr)   , -1, -1, -1, -1 }
};

// Number of registers in each register set
const size_t DNBArchImplI386::k_num_gpr_registers = sizeof(g_gpr_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchImplI386::k_num_fpu_registers = sizeof(g_fpu_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchImplI386::k_num_exc_registers = sizeof(g_exc_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchImplI386::k_num_all_registers = k_num_gpr_registers + k_num_fpu_registers + k_num_exc_registers;

//----------------------------------------------------------------------
// Register set definitions. The first definitions at register set index
// of zero is for all registers, followed by other registers sets. The
// register information for the all register set need not be filled in.
//----------------------------------------------------------------------
const DNBRegisterSetInfo
DNBArchImplI386::g_reg_sets[] =
{
    { "i386 Registers",             NULL,               k_num_all_registers },
    { "General Purpose Registers",  g_gpr_registers,    k_num_gpr_registers },
    { "Floating Point Registers",   g_fpu_registers,    k_num_fpu_registers },
    { "Exception State Registers",  g_exc_registers,    k_num_exc_registers }
};
// Total number of register sets for this architecture
const size_t DNBArchImplI386::k_num_register_sets = sizeof(g_reg_sets)/sizeof(DNBRegisterSetInfo);


DNBArchProtocol *
DNBArchImplI386::Create (MachThread *thread)
{
    return new DNBArchImplI386 (thread);
}

const uint8_t * const
DNBArchImplI386::SoftwareBreakpointOpcode (nub_size_t byte_size)
{
    static const uint8_t g_breakpoint_opcode[] = { 0xCC };
    if (byte_size == 1)
        return g_breakpoint_opcode;
    return NULL;
}

const DNBRegisterSetInfo *
DNBArchImplI386::GetRegisterSetInfo(nub_size_t *num_reg_sets)
{
    *num_reg_sets = k_num_register_sets;
    return g_reg_sets;
}


void
DNBArchImplI386::Initialize()
{
    DNBArchPluginInfo arch_plugin_info = 
    {
        CPU_TYPE_I386, 
        DNBArchImplI386::Create, 
        DNBArchImplI386::GetRegisterSetInfo,
        DNBArchImplI386::SoftwareBreakpointOpcode
    };
    
    // Register this arch plug-in with the main protocol class
    DNBArchProtocol::RegisterArchPlugin (arch_plugin_info);
}

bool
DNBArchImplI386::GetRegisterValue(int set, int reg, DNBRegisterValue *value)
{
    if (set == REGISTER_SET_GENERIC)
    {
        switch (reg)
        {
        case GENERIC_REGNUM_PC:     // Program Counter
            set = e_regSetGPR;
            reg = gpr_eip;
            break;

        case GENERIC_REGNUM_SP:     // Stack Pointer
            set = e_regSetGPR;
            reg = gpr_esp;
            break;

        case GENERIC_REGNUM_FP:     // Frame Pointer
            set = e_regSetGPR;
            reg = gpr_ebp;
            break;

        case GENERIC_REGNUM_FLAGS:  // Processor flags register
            set = e_regSetGPR;
            reg = gpr_eflags;
            break;

        case GENERIC_REGNUM_RA:     // Return Address
        default:
            return false;
        }
    }

    if (GetRegisterState(set, false) != KERN_SUCCESS)
        return false;

    const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
    if (regInfo)
    {
        value->info = *regInfo;
        switch (set)
        {
        case e_regSetGPR:
            if (reg < k_num_gpr_registers)
            {
                value->value.uint32 = ((uint32_t*)(&m_state.context.gpr))[reg];
                return true;
            }
            break;

        case e_regSetFPU:
            switch (reg)
            {
            case fpu_fcw:       value->value.uint16 = *((uint16_t *)(&m_state.context.fpu.__fpu_fcw));    return true;
            case fpu_fsw:       value->value.uint16 = *((uint16_t *)(&m_state.context.fpu.__fpu_fsw));    return true;
            case fpu_ftw:       value->value.uint8  = m_state.context.fpu.__fpu_ftw;                      return true;
            case fpu_fop:       value->value.uint16 = m_state.context.fpu.__fpu_fop;                      return true;
            case fpu_ip:        value->value.uint32 = m_state.context.fpu.__fpu_ip;                       return true;
            case fpu_cs:        value->value.uint16 = m_state.context.fpu.__fpu_cs;                       return true;
            case fpu_dp:        value->value.uint32 = m_state.context.fpu.__fpu_dp;                       return true;
            case fpu_ds:        value->value.uint16 = m_state.context.fpu.__fpu_ds;                       return true;
            case fpu_mxcsr:     value->value.uint32 = m_state.context.fpu.__fpu_mxcsr;                    return true;
            case fpu_mxcsrmask: value->value.uint32 = m_state.context.fpu.__fpu_mxcsrmask;                return true;

            case fpu_stmm0:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm0.__mmst_reg, 10);    return true;
            case fpu_stmm1:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm1.__mmst_reg, 10);    return true;
            case fpu_stmm2:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm2.__mmst_reg, 10);    return true;
            case fpu_stmm3:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm3.__mmst_reg, 10);    return true;
            case fpu_stmm4:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm4.__mmst_reg, 10);    return true;
            case fpu_stmm5:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm5.__mmst_reg, 10);    return true;
            case fpu_stmm6:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm6.__mmst_reg, 10);    return true;
            case fpu_stmm7:     memcpy(&value->value.uint8, m_state.context.fpu.__fpu_stmm7.__mmst_reg, 10);    return true;

            case fpu_xmm0:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm0.__xmm_reg, 16);    return true;
            case fpu_xmm1:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm1.__xmm_reg, 16);    return true;
            case fpu_xmm2:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm2.__xmm_reg, 16);    return true;
            case fpu_xmm3:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm3.__xmm_reg, 16);    return true;
            case fpu_xmm4:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm4.__xmm_reg, 16);    return true;
            case fpu_xmm5:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm5.__xmm_reg, 16);    return true;
            case fpu_xmm6:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm6.__xmm_reg, 16);    return true;
            case fpu_xmm7:      memcpy(&value->value.uint8, m_state.context.fpu.__fpu_xmm7.__xmm_reg, 16);    return true;
            }
            break;

        case e_regSetEXC:
            if (reg < k_num_exc_registers)
            {
                value->value.uint32 = (&m_state.context.exc.__trapno)[reg];
                return true;
            }
            break;
        }
    }
    return false;
}


bool
DNBArchImplI386::SetRegisterValue(int set, int reg, const DNBRegisterValue *value)
{
    if (set == REGISTER_SET_GENERIC)
    {
        switch (reg)
        {
        case GENERIC_REGNUM_PC:     // Program Counter
            set = e_regSetGPR;
            reg = gpr_eip;
            break;

        case GENERIC_REGNUM_SP:     // Stack Pointer
            set = e_regSetGPR;
            reg = gpr_esp;
            break;

        case GENERIC_REGNUM_FP:     // Frame Pointer
            set = e_regSetGPR;
            reg = gpr_ebp;
            break;

        case GENERIC_REGNUM_FLAGS:  // Processor flags register
            set = e_regSetGPR;
            reg = gpr_eflags;
            break;

        case GENERIC_REGNUM_RA:     // Return Address
        default:
            return false;
        }
    }

    if (GetRegisterState(set, false) != KERN_SUCCESS)
        return false;

    bool success = false;
    const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
    if (regInfo)
    {
        switch (set)
        {
        case e_regSetGPR:
            if (reg < k_num_gpr_registers)
            {
                ((uint32_t*)(&m_state.context.gpr))[reg] = value->value.uint32;
                success = true;
            }
            break;

        case e_regSetFPU:
            switch (reg)
            {
            case fpu_fcw:           *((uint16_t *)(&m_state.context.fpu.__fpu_fcw)) = value->value.uint16;    success = true; break;
            case fpu_fsw:           *((uint16_t *)(&m_state.context.fpu.__fpu_fsw)) = value->value.uint16;    success = true; break;
            case fpu_ftw:           m_state.context.fpu.__fpu_ftw = value->value.uint8;                       success = true; break;
            case fpu_fop:           m_state.context.fpu.__fpu_fop = value->value.uint16;                      success = true; break;
            case fpu_ip:            m_state.context.fpu.__fpu_ip = value->value.uint32;                       success = true; break;
            case fpu_cs:            m_state.context.fpu.__fpu_cs = value->value.uint16;                       success = true; break;
            case fpu_dp:            m_state.context.fpu.__fpu_dp = value->value.uint32;                       success = true; break;
            case fpu_ds:            m_state.context.fpu.__fpu_ds = value->value.uint16;                       success = true; break;
            case fpu_mxcsr:         m_state.context.fpu.__fpu_mxcsr = value->value.uint32;                    success = true; break;
            case fpu_mxcsrmask:     m_state.context.fpu.__fpu_mxcsrmask = value->value.uint32;                success = true; break;

            case fpu_stmm0:     memcpy (m_state.context.fpu.__fpu_stmm0.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm1:     memcpy (m_state.context.fpu.__fpu_stmm1.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm2:     memcpy (m_state.context.fpu.__fpu_stmm2.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm3:     memcpy (m_state.context.fpu.__fpu_stmm3.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm4:     memcpy (m_state.context.fpu.__fpu_stmm4.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm5:     memcpy (m_state.context.fpu.__fpu_stmm5.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm6:     memcpy (m_state.context.fpu.__fpu_stmm6.__mmst_reg, &value->value.uint8, 10);    success = true; break;
            case fpu_stmm7:     memcpy (m_state.context.fpu.__fpu_stmm7.__mmst_reg, &value->value.uint8, 10);    success = true; break;

            case fpu_xmm0:     memcpy(m_state.context.fpu.__fpu_xmm0.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm1:     memcpy(m_state.context.fpu.__fpu_xmm1.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm2:     memcpy(m_state.context.fpu.__fpu_xmm2.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm3:     memcpy(m_state.context.fpu.__fpu_xmm3.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm4:     memcpy(m_state.context.fpu.__fpu_xmm4.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm5:     memcpy(m_state.context.fpu.__fpu_xmm5.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm6:     memcpy(m_state.context.fpu.__fpu_xmm6.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            case fpu_xmm7:     memcpy(m_state.context.fpu.__fpu_xmm7.__xmm_reg, &value->value.uint8, 16);    success = true; break;
            }
            break;

        case e_regSetEXC:
            if (reg < k_num_exc_registers)
            {
                (&m_state.context.exc.__trapno)[reg] = value->value.uint32;
                success = true;
            }
            break;
        }
    }

    if (success)
        return SetRegisterState(set) == KERN_SUCCESS;
    return false;
}


nub_size_t
DNBArchImplI386::GetRegisterContext (void *buf, nub_size_t buf_len)
{
    nub_size_t size = sizeof (m_state.context);
    
    if (buf && buf_len)
    {
        if (size > buf_len)
            size = buf_len;

        bool force = false;
        if (GetGPRState(force) | GetFPUState(force) | GetEXCState(force))
            return 0;
        ::memcpy (buf, &m_state.context, size);
    }
    DNBLogThreadedIf (LOG_THREAD, "DNBArchImplI386::GetRegisterContext (buf = %p, len = %zu) => %zu", buf, buf_len, size);
    // Return the size of the register context even if NULL was passed in
    return size;
}

nub_size_t
DNBArchImplI386::SetRegisterContext (const void *buf, nub_size_t buf_len)
{
    nub_size_t size = sizeof (m_state.context);
    if (buf == NULL || buf_len == 0)
        size = 0;
    
    if (size)
    {
        if (size > buf_len)
            size = buf_len;

        ::memcpy (&m_state.context, buf, size);
        SetGPRState();
        SetFPUState();
        SetEXCState();
    }
    DNBLogThreadedIf (LOG_THREAD, "DNBArchImplI386::SetRegisterContext (buf = %p, len = %zu) => %zu", buf, buf_len, size);
    return size;
}



kern_return_t
DNBArchImplI386::GetRegisterState(int set, bool force)
{
    switch (set)
    {
    case e_regSetALL:    return GetGPRState(force) | GetFPUState(force) | GetEXCState(force);
    case e_regSetGPR:    return GetGPRState(force);
    case e_regSetFPU:    return GetFPUState(force);
    case e_regSetEXC:    return GetEXCState(force);
    default: break;
    }
    return KERN_INVALID_ARGUMENT;
}

kern_return_t
DNBArchImplI386::SetRegisterState(int set)
{
    // Make sure we have a valid context to set.
    if (RegisterSetStateIsValid(set))
    {
        switch (set)
        {
        case e_regSetALL:    return SetGPRState() | SetFPUState() | SetEXCState();
        case e_regSetGPR:    return SetGPRState();
        case e_regSetFPU:    return SetFPUState();
        case e_regSetEXC:    return SetEXCState();
        default: break;
        }
    }
    return KERN_INVALID_ARGUMENT;
}

bool
DNBArchImplI386::RegisterSetStateIsValid (int set) const
{
    return m_state.RegsAreValid(set);
}



#endif    // #if defined (__i386__)
