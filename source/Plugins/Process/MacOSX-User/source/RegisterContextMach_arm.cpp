//===-- RegisterContextMach_arm.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RegisterContextMach_arm.h"

// C Includes
#include <mach/mach_types.h>
#include <mach/thread_act.h>

// C++ Includes
// Other libraries and framework includes
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Scalar.h"

// Project includes
#include "ARM_GCC_Registers.h"
#include "ARM_DWARF_Registers.h"
#include "ProcessMacOSXLog.h"

using namespace lldb;
using namespace lldb_private;

enum
{
    gpr_r0 = 0,
    gpr_r1,
    gpr_r2,
    gpr_r3,
    gpr_r4,
    gpr_r5,
    gpr_r6,
    gpr_r7,
    gpr_r8,
    gpr_r9,
    gpr_r10,
    gpr_r11,
    gpr_r12,
    gpr_r13, gpr_sp = gpr_r13,
    gpr_r14, gpr_lr = gpr_r14,
    gpr_r15, gpr_pc = gpr_r15,
    gpr_cpsr,

    fpu_s0,
    fpu_s1,
    fpu_s2,
    fpu_s3,
    fpu_s4,
    fpu_s5,
    fpu_s6,
    fpu_s7,
    fpu_s8,
    fpu_s9,
    fpu_s10,
    fpu_s11,
    fpu_s12,
    fpu_s13,
    fpu_s14,
    fpu_s15,
    fpu_s16,
    fpu_s17,
    fpu_s18,
    fpu_s19,
    fpu_s20,
    fpu_s21,
    fpu_s22,
    fpu_s23,
    fpu_s24,
    fpu_s25,
    fpu_s26,
    fpu_s27,
    fpu_s28,
    fpu_s29,
    fpu_s30,
    fpu_s31,
    fpu_fpscr,

    exc_exception,
    exc_fsr,
    exc_far,

    dbg_bvr0,
    dbg_bvr1,
    dbg_bvr2,
    dbg_bvr3,
    dbg_bvr4,
    dbg_bvr5,
    dbg_bvr6,
    dbg_bvr7,
    dbg_bvr8,
    dbg_bvr9,
    dbg_bvr10,
    dbg_bvr11,
    dbg_bvr12,
    dbg_bvr13,
    dbg_bvr14,
    dbg_bvr15,

    dbg_bcr0,
    dbg_bcr1,
    dbg_bcr2,
    dbg_bcr3,
    dbg_bcr4,
    dbg_bcr5,
    dbg_bcr6,
    dbg_bcr7,
    dbg_bcr8,
    dbg_bcr9,
    dbg_bcr10,
    dbg_bcr11,
    dbg_bcr12,
    dbg_bcr13,
    dbg_bcr14,
    dbg_bcr15,

    dbg_wvr0,
    dbg_wvr1,
    dbg_wvr2,
    dbg_wvr3,
    dbg_wvr4,
    dbg_wvr5,
    dbg_wvr6,
    dbg_wvr7,
    dbg_wvr8,
    dbg_wvr9,
    dbg_wvr10,
    dbg_wvr11,
    dbg_wvr12,
    dbg_wvr13,
    dbg_wvr14,
    dbg_wvr15,

    dbg_wcr0,
    dbg_wcr1,
    dbg_wcr2,
    dbg_wcr3,
    dbg_wcr4,
    dbg_wcr5,
    dbg_wcr6,
    dbg_wcr7,
    dbg_wcr8,
    dbg_wcr9,
    dbg_wcr10,
    dbg_wcr11,
    dbg_wcr12,
    dbg_wcr13,
    dbg_wcr14,
    dbg_wcr15,

    k_num_registers
};


RegisterContextMach_arm::RegisterContextMach_arm(Thread &thread, uint32_t concrete_frame_idx) :
    RegisterContext(thread, concrete_frame_idx),
    gpr(),
    fpu(),
    exc()
{
    uint32_t i;
    for (i=0; i<kNumErrors; i++)
    {
        gpr_errs[i] = -1;
        fpu_errs[i] = -1;
        exc_errs[i] = -1;
    }
}

RegisterContextMach_arm::~RegisterContextMach_arm()
{
}


#define GPR_OFFSET(idx) ((idx) * 4)
#define FPU_OFFSET(idx) ((idx) * 4 + sizeof (RegisterContextMach_arm::GPR))
#define EXC_OFFSET(idx) ((idx) * 4 + sizeof (RegisterContextMach_arm::GPR) + sizeof (RegisterContextMach_arm::FPU))
#define DBG_OFFSET(reg) (offsetof (RegisterContextMach_arm::DBG, reg) + sizeof (RegisterContextMach_arm::GPR) + sizeof (RegisterContextMach_arm::FPU) + sizeof (RegisterContextMach_arm::EXC))

#define DEFINE_DBG(reg, i)  #reg, NULL, sizeof(((RegisterContextMach_arm::DBG *)NULL)->reg[i]), DBG_OFFSET(reg[i]), eEncodingUint, eFormatHex, { LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, dbg_##reg##i }
#define REG_CONTEXT_SIZE (sizeof (RegisterContextMach_arm::GPR) + sizeof (RegisterContextMach_arm::FPU) + sizeof (RegisterContextMach_arm::EXC))
// General purpose registers
static lldb::RegisterInfo
g_register_infos[] =
{
//  NAME        ALT     SZ  OFFSET              ENCODING        FORMAT          COMPILER            DWARF               GENERIC                     LLDB NATIVE
//  ======      ======= ==  =============       =============   ============    ===============     ===============     =========                   ==========
{   "r0",       NULL,   4,  GPR_OFFSET(0),      eEncodingUint,  eFormatHex,     { gcc_r0,               dwarf_r0,           LLDB_INVALID_REGNUM,    gpr_r0 }},
{   "r1",       NULL,   4,  GPR_OFFSET(1),      eEncodingUint,  eFormatHex,     { gcc_r1,               dwarf_r1,           LLDB_INVALID_REGNUM,    gpr_r1 }},
{   "r2",       NULL,   4,  GPR_OFFSET(2),      eEncodingUint,  eFormatHex,     { gcc_r2,               dwarf_r2,           LLDB_INVALID_REGNUM,    gpr_r2 }},
{   "r3",       NULL,   4,  GPR_OFFSET(3),      eEncodingUint,  eFormatHex,     { gcc_r3,               dwarf_r3,           LLDB_INVALID_REGNUM,    gpr_r3 }},
{   "r4",       NULL,   4,  GPR_OFFSET(4),      eEncodingUint,  eFormatHex,     { gcc_r4,               dwarf_r4,           LLDB_INVALID_REGNUM,    gpr_r4 }},
{   "r5",       NULL,   4,  GPR_OFFSET(5),      eEncodingUint,  eFormatHex,     { gcc_r5,               dwarf_r5,           LLDB_INVALID_REGNUM,    gpr_r5 }},
{   "r6",       NULL,   4,  GPR_OFFSET(6),      eEncodingUint,  eFormatHex,     { gcc_r6,               dwarf_r6,           LLDB_INVALID_REGNUM,    gpr_r6 }},
{   "r7",       NULL,   4,  GPR_OFFSET(7),      eEncodingUint,  eFormatHex,     { gcc_r7,               dwarf_r7,           LLDB_REGNUM_GENERIC_FP, gpr_r7 }},
{   "r8",       NULL,   4,  GPR_OFFSET(8),      eEncodingUint,  eFormatHex,     { gcc_r8,               dwarf_r8,           LLDB_INVALID_REGNUM,    gpr_r8 }},
{   "r9",       NULL,   4,  GPR_OFFSET(9),      eEncodingUint,  eFormatHex,     { gcc_r9,               dwarf_r9,           LLDB_INVALID_REGNUM,    gpr_r9 }},
{   "r10",      NULL,   4,  GPR_OFFSET(10),     eEncodingUint,  eFormatHex,     { gcc_r10,              dwarf_r10,          LLDB_INVALID_REGNUM,    gpr_r10 }},
{   "r11",      NULL,   4,  GPR_OFFSET(11),     eEncodingUint,  eFormatHex,     { gcc_r11,              dwarf_r11,          LLDB_INVALID_REGNUM,    gpr_r11 }},
{   "r12",      NULL,   4,  GPR_OFFSET(12),     eEncodingUint,  eFormatHex,     { gcc_r12,              dwarf_r12,          LLDB_INVALID_REGNUM,    gpr_r12 }},
{   "sp",       "r13",  4,  GPR_OFFSET(13),     eEncodingUint,  eFormatHex,     { gcc_sp,               dwarf_sp,           LLDB_REGNUM_GENERIC_SP, gpr_sp }},
{   "lr",       "r14",  4,  GPR_OFFSET(14),     eEncodingUint,  eFormatHex,     { gcc_lr,               dwarf_lr,           LLDB_REGNUM_GENERIC_RA, gpr_lr }},
{   "pc",       "r15",  4,  GPR_OFFSET(15),     eEncodingUint,  eFormatHex,     { gcc_pc,               dwarf_pc,           LLDB_REGNUM_GENERIC_PC, gpr_pc }},
{   "cpsr",     "psr",  4,  GPR_OFFSET(16),     eEncodingUint,  eFormatHex,     { gcc_cpsr,             dwarf_cpsr,         LLDB_REGNUM_GENERIC_FLAGS, gpr_cpsr }},

{   "s0",       NULL,   4,  FPU_OFFSET(0),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s0,           LLDB_INVALID_REGNUM,    fpu_s0 }},
{   "s1",       NULL,   4,  FPU_OFFSET(1),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s1,           LLDB_INVALID_REGNUM,    fpu_s1 }},
{   "s2",       NULL,   4,  FPU_OFFSET(2),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s2,           LLDB_INVALID_REGNUM,    fpu_s2 }},
{   "s3",       NULL,   4,  FPU_OFFSET(3),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s3,           LLDB_INVALID_REGNUM,    fpu_s3 }},
{   "s4",       NULL,   4,  FPU_OFFSET(4),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s4,           LLDB_INVALID_REGNUM,    fpu_s4 }},
{   "s5",       NULL,   4,  FPU_OFFSET(5),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s5,           LLDB_INVALID_REGNUM,    fpu_s5 }},
{   "s6",       NULL,   4,  FPU_OFFSET(6),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s6,           LLDB_INVALID_REGNUM,    fpu_s6 }},
{   "s7",       NULL,   4,  FPU_OFFSET(7),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s7,           LLDB_INVALID_REGNUM,    fpu_s7 }},
{   "s8",       NULL,   4,  FPU_OFFSET(8),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s8,           LLDB_INVALID_REGNUM,    fpu_s8 }},
{   "s9",       NULL,   4,  FPU_OFFSET(9),      eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s9,           LLDB_INVALID_REGNUM,    fpu_s9 }},
{   "s10",      NULL,   4,  FPU_OFFSET(10),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s10,          LLDB_INVALID_REGNUM,    fpu_s10 }},
{   "s11",      NULL,   4,  FPU_OFFSET(11),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s11,          LLDB_INVALID_REGNUM,    fpu_s11 }},
{   "s12",      NULL,   4,  FPU_OFFSET(12),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s12,          LLDB_INVALID_REGNUM,    fpu_s12 }},
{   "s13",      NULL,   4,  FPU_OFFSET(13),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s13,          LLDB_INVALID_REGNUM,    fpu_s13 }},
{   "s14",      NULL,   4,  FPU_OFFSET(14),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s14,          LLDB_INVALID_REGNUM,    fpu_s14 }},
{   "s15",      NULL,   4,  FPU_OFFSET(15),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s15,          LLDB_INVALID_REGNUM,    fpu_s15 }},
{   "s16",      NULL,   4,  FPU_OFFSET(16),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s16,          LLDB_INVALID_REGNUM,    fpu_s16 }},
{   "s17",      NULL,   4,  FPU_OFFSET(17),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s17,          LLDB_INVALID_REGNUM,    fpu_s17 }},
{   "s18",      NULL,   4,  FPU_OFFSET(18),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s18,          LLDB_INVALID_REGNUM,    fpu_s18 }},
{   "s19",      NULL,   4,  FPU_OFFSET(19),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s19,          LLDB_INVALID_REGNUM,    fpu_s19 }},
{   "s20",      NULL,   4,  FPU_OFFSET(20),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s20,          LLDB_INVALID_REGNUM,    fpu_s20 }},
{   "s21",      NULL,   4,  FPU_OFFSET(21),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s21,          LLDB_INVALID_REGNUM,    fpu_s21 }},
{   "s22",      NULL,   4,  FPU_OFFSET(22),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s22,          LLDB_INVALID_REGNUM,    fpu_s22 }},
{   "s23",      NULL,   4,  FPU_OFFSET(23),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s23,          LLDB_INVALID_REGNUM,    fpu_s23 }},
{   "s24",      NULL,   4,  FPU_OFFSET(24),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s24,          LLDB_INVALID_REGNUM,    fpu_s24 }},
{   "s25",      NULL,   4,  FPU_OFFSET(25),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s25,          LLDB_INVALID_REGNUM,    fpu_s25 }},
{   "s26",      NULL,   4,  FPU_OFFSET(26),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s26,          LLDB_INVALID_REGNUM,    fpu_s26 }},
{   "s27",      NULL,   4,  FPU_OFFSET(27),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s27,          LLDB_INVALID_REGNUM,    fpu_s27 }},
{   "s28",      NULL,   4,  FPU_OFFSET(28),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s28,          LLDB_INVALID_REGNUM,    fpu_s28 }},
{   "s29",      NULL,   4,  FPU_OFFSET(29),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s29,          LLDB_INVALID_REGNUM,    fpu_s29 }},
{   "s30",      NULL,   4,  FPU_OFFSET(30),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s30,          LLDB_INVALID_REGNUM,    fpu_s30 }},
{   "s31",      NULL,   4,  FPU_OFFSET(31),     eEncodingIEEE754,eFormatFloat,  { LLDB_INVALID_REGNUM,  dwarf_s31,          LLDB_INVALID_REGNUM,    fpu_s31 }},
{   "fpscr",    NULL,   4,  FPU_OFFSET(32),     eEncodingUint,  eFormatHex,     { LLDB_INVALID_REGNUM,  LLDB_INVALID_REGNUM,    LLDB_INVALID_REGNUM, fpu_fpscr }},

{   "exception",NULL,   4,  EXC_OFFSET(0),      eEncodingUint,  eFormatHex,     { LLDB_INVALID_REGNUM,    LLDB_INVALID_REGNUM,    LLDB_INVALID_REGNUM, exc_exception   }},
{   "fsr",      NULL,   4,  EXC_OFFSET(1),      eEncodingUint,  eFormatHex,     { LLDB_INVALID_REGNUM,  LLDB_INVALID_REGNUM,    LLDB_INVALID_REGNUM, exc_fsr }},
{   "far",      NULL,   4,  EXC_OFFSET(2),      eEncodingUint,  eFormatHex,     { LLDB_INVALID_REGNUM,  LLDB_INVALID_REGNUM,    LLDB_INVALID_REGNUM, exc_far }},

{   DEFINE_DBG (bvr, 0) },
{   DEFINE_DBG (bvr, 0) },
{   DEFINE_DBG (bvr, 1) },
{   DEFINE_DBG (bvr, 2) },
{   DEFINE_DBG (bvr, 3) },
{   DEFINE_DBG (bvr, 4) },
{   DEFINE_DBG (bvr, 5) },
{   DEFINE_DBG (bvr, 6) },
{   DEFINE_DBG (bvr, 7) },
{   DEFINE_DBG (bvr, 8) },
{   DEFINE_DBG (bvr, 9) },
{   DEFINE_DBG (bvr, 10) },
{   DEFINE_DBG (bvr, 11) },
{   DEFINE_DBG (bvr, 12) },
{   DEFINE_DBG (bvr, 13) },
{   DEFINE_DBG (bvr, 14) },
{   DEFINE_DBG (bvr, 15) },

{   DEFINE_DBG (bcr, 0) },
{   DEFINE_DBG (bcr, 0) },
{   DEFINE_DBG (bcr, 1) },
{   DEFINE_DBG (bcr, 2) },
{   DEFINE_DBG (bcr, 3) },
{   DEFINE_DBG (bcr, 4) },
{   DEFINE_DBG (bcr, 5) },
{   DEFINE_DBG (bcr, 6) },
{   DEFINE_DBG (bcr, 7) },
{   DEFINE_DBG (bcr, 8) },
{   DEFINE_DBG (bcr, 9) },
{   DEFINE_DBG (bcr, 10) },
{   DEFINE_DBG (bcr, 11) },
{   DEFINE_DBG (bcr, 12) },
{   DEFINE_DBG (bcr, 13) },
{   DEFINE_DBG (bcr, 14) },
{   DEFINE_DBG (bcr, 15) },

{   DEFINE_DBG (wvr, 0) },
{   DEFINE_DBG (wvr, 0) },
{   DEFINE_DBG (wvr, 1) },
{   DEFINE_DBG (wvr, 2) },
{   DEFINE_DBG (wvr, 3) },
{   DEFINE_DBG (wvr, 4) },
{   DEFINE_DBG (wvr, 5) },
{   DEFINE_DBG (wvr, 6) },
{   DEFINE_DBG (wvr, 7) },
{   DEFINE_DBG (wvr, 8) },
{   DEFINE_DBG (wvr, 9) },
{   DEFINE_DBG (wvr, 10) },
{   DEFINE_DBG (wvr, 11) },
{   DEFINE_DBG (wvr, 12) },
{   DEFINE_DBG (wvr, 13) },
{   DEFINE_DBG (wvr, 14) },
{   DEFINE_DBG (wvr, 15) },

{   DEFINE_DBG (wcr, 0) },
{   DEFINE_DBG (wcr, 0) },
{   DEFINE_DBG (wcr, 1) },
{   DEFINE_DBG (wcr, 2) },
{   DEFINE_DBG (wcr, 3) },
{   DEFINE_DBG (wcr, 4) },
{   DEFINE_DBG (wcr, 5) },
{   DEFINE_DBG (wcr, 6) },
{   DEFINE_DBG (wcr, 7) },
{   DEFINE_DBG (wcr, 8) },
{   DEFINE_DBG (wcr, 9) },
{   DEFINE_DBG (wcr, 10) },
{   DEFINE_DBG (wcr, 11) },
{   DEFINE_DBG (wcr, 12) },
{   DEFINE_DBG (wcr, 13) },
{   DEFINE_DBG (wcr, 14) },
{   DEFINE_DBG (wcr, 15) }
};

// General purpose registers
static uint32_t
g_gpr_regnums[] =
{
    gpr_r0,
    gpr_r1,
    gpr_r2,
    gpr_r3,
    gpr_r4,
    gpr_r5,
    gpr_r6,
    gpr_r7,
    gpr_r8,
    gpr_r9,
    gpr_r10,
    gpr_r11,
    gpr_r12,
    gpr_sp,
    gpr_lr,
    gpr_pc,
    gpr_cpsr
};

// Floating point registers
static uint32_t
g_fpu_regnums[] =
{
    fpu_s0,
    fpu_s1,
    fpu_s2,
    fpu_s3,
    fpu_s4,
    fpu_s5,
    fpu_s6,
    fpu_s7,
    fpu_s8,
    fpu_s9,
    fpu_s10,
    fpu_s11,
    fpu_s12,
    fpu_s13,
    fpu_s14,
    fpu_s15,
    fpu_s16,
    fpu_s17,
    fpu_s18,
    fpu_s19,
    fpu_s20,
    fpu_s21,
    fpu_s22,
    fpu_s23,
    fpu_s24,
    fpu_s25,
    fpu_s26,
    fpu_s27,
    fpu_s28,
    fpu_s29,
    fpu_s30,
    fpu_s31,
    fpu_fpscr,
};

// Exception registers

static uint32_t
g_exc_regnums[] =
{
    exc_exception,
    exc_fsr,
    exc_far,
};

static size_t k_num_register_infos = (sizeof(g_register_infos)/sizeof(RegisterInfo));

void
RegisterContextMach_arm::InvalidateAllRegisters ()
{
    InvalidateAllRegisterStates();
}


size_t
RegisterContextMach_arm::GetRegisterCount ()
{
    assert(k_num_register_infos == k_num_registers);
    return k_num_registers;
}

const RegisterInfo *
RegisterContextMach_arm::GetRegisterInfoAtIndex (uint32_t reg)
{
    assert(k_num_register_infos == k_num_registers);
    if (reg < k_num_registers)
        return &g_register_infos[reg];
    return NULL;
}

size_t
RegisterContextMach_arm::GetRegisterInfosCount ()
{
    return k_num_register_infos;
}

const RegisterInfo *
RegisterContextMach_arm::GetRegisterInfos ()
{
    return g_register_infos;
}


// Number of registers in each register set
const size_t k_num_gpr_registers = sizeof(g_gpr_regnums) / sizeof(uint32_t);
const size_t k_num_fpu_registers = sizeof(g_fpu_regnums) / sizeof(uint32_t);
const size_t k_num_exc_registers = sizeof(g_exc_regnums) / sizeof(uint32_t);

//----------------------------------------------------------------------
// Register set definitions. The first definitions at register set index
// of zero is for all registers, followed by other registers sets. The
// register information for the all register set need not be filled in.
//----------------------------------------------------------------------
static const RegisterSet g_reg_sets[] =
{
    { "General Purpose Registers",  "gpr",  k_num_gpr_registers,    g_gpr_regnums,      },
    { "Floating Point Registers",   "fpu",  k_num_fpu_registers,    g_fpu_regnums       },
    { "Exception State Registers",  "exc",  k_num_exc_registers,    g_exc_regnums       }
};

const size_t k_num_regsets = sizeof(g_reg_sets) / sizeof(RegisterSet);


size_t
RegisterContextMach_arm::GetRegisterSetCount ()
{
    return k_num_regsets;
}

const RegisterSet *
RegisterContextMach_arm::GetRegisterSet (uint32_t reg_set)
{
    if (reg_set < k_num_regsets)
        return &g_reg_sets[reg_set];
    return NULL;
}


//----------------------------------------------------------------------
// Register information defintions for 32 bit i386.
//----------------------------------------------------------------------
int
RegisterContextMach_arm::GetSetForNativeRegNum (int reg)
{
    if (reg < fpu_s0)
        return GPRRegSet;
    else if (reg < exc_exception)
        return FPURegSet;
    else if (reg < k_num_registers)
        return EXCRegSet;
    return -1;
}

int
RegisterContextMach_arm::ReadGPR (bool force)
{
    int set = GPRRegSet;
    if (force || !RegisterSetIsCached(set))
    {
        mach_msg_type_number_t count = GPRWordCount;
        SetError(GPRRegSet, Read, ::thread_get_state(GetThreadID(), set, (thread_state_t)&gpr, &count));
    }
    return GetError(GPRRegSet, Read);
}

int
RegisterContextMach_arm::ReadFPU (bool force)
{
    int set = FPURegSet;
    if (force || !RegisterSetIsCached(set))
    {
        mach_msg_type_number_t count = FPUWordCount;
        SetError(FPURegSet, Read, ::thread_get_state(GetThreadID(), set, (thread_state_t)&fpu, &count));
    }
    return GetError(FPURegSet, Read);
}

int
RegisterContextMach_arm::ReadEXC (bool force)
{
    int set = EXCRegSet;
    if (force || !RegisterSetIsCached(set))
    {
        mach_msg_type_number_t count = EXCWordCount;
        SetError(EXCRegSet, Read, ::thread_get_state(GetThreadID(), set, (thread_state_t)&exc, &count));
    }
    return GetError(EXCRegSet, Read);
}

int
RegisterContextMach_arm::ReadDBG (bool force)
{
    int set = DBGRegSet;
    if (force || !RegisterSetIsCached(set))
    {
        mach_msg_type_number_t count = DBGWordCount;
        SetError(DBGRegSet, Read, ::thread_get_state(GetThreadID(), set, (thread_state_t)&dbg, &count));
    }
    return GetError(DBGRegSet, Read);
}

int
RegisterContextMach_arm::WriteGPR ()
{
    int set = GPRRegSet;
    if (!RegisterSetIsCached(set))
    {
        SetError (set, Write, -1);
        return KERN_INVALID_ARGUMENT;
    }
    SetError(GPRRegSet, Write, ::thread_set_state(GetThreadID(), set, (thread_state_t)&gpr, GPRWordCount));
    return GetError(GPRRegSet, Write);
}

int
RegisterContextMach_arm::WriteFPU ()
{
    int set = FPURegSet;
    if (!RegisterSetIsCached(set))
    {
        SetError (set, Write, -1);
        return KERN_INVALID_ARGUMENT;
    }
    SetError(FPURegSet, Write, ::thread_set_state(GetThreadID(), set, (thread_state_t)&fpu, FPUWordCount));
    return GetError(FPURegSet, Write);
}

int
RegisterContextMach_arm::WriteEXC ()
{
    int set = EXCRegSet;
    if (!RegisterSetIsCached(set))
    {
        SetError (set, Write, -1);
        return KERN_INVALID_ARGUMENT;
    }
    SetError(EXCRegSet, Write, ::thread_set_state(GetThreadID(), set, (thread_state_t)&exc, EXCWordCount));
    return GetError(EXCRegSet, Write);
}

int
RegisterContextMach_arm::WriteDBG ()
{
    int set = DBGRegSet;
    if (!RegisterSetIsCached(set))
    {
        SetError (set, Write, -1);
        return KERN_INVALID_ARGUMENT;
    }
    SetError(DBGRegSet, Write, ::thread_set_state(GetThreadID(), set, (thread_state_t)&dbg, DBGWordCount));
    return GetError(DBGRegSet, Write);
}


int
RegisterContextMach_arm::ReadRegisterSet (uint32_t set, bool force)
{
    switch (set)
    {
    case GPRRegSet:    return ReadGPR(force);
    case FPURegSet:    return ReadFPU(force);
    case EXCRegSet:    return ReadEXC(force);
    case DBGRegSet:    return ReadDBG(force);
    default: break;
    }
    return KERN_INVALID_ARGUMENT;
}

int
RegisterContextMach_arm::WriteRegisterSet (uint32_t set)
{
    // Make sure we have a valid context to set.
    if (RegisterSetIsCached(set))
    {
        switch (set)
        {
        case GPRRegSet:    return WriteGPR();
        case FPURegSet:    return WriteFPU();
        case EXCRegSet:    return WriteEXC();
        case DBGRegSet:    return WriteDBG();
        default: break;
        }
    }
    return KERN_INVALID_ARGUMENT;
}

void
RegisterContextMach_arm::LogDBGRegisters (Log *log, const DBG& dbg)
{
    if (log)
    {
        for (uint32_t i=0; i<16; i++)
            log->Printf("BVR%-2u/BCR%-2u = { 0x%8.8x, 0x%8.8x } WVR%-2u/WCR%-2u = { 0x%8.8x, 0x%8.8x }",
                i, i, dbg.bvr[i], dbg.bcr[i],
                i, i, dbg.wvr[i], dbg.wcr[i]);
    }
}


bool
RegisterContextMach_arm::ReadRegisterValue (uint32_t reg, Scalar &value)
{
    int set = RegisterContextMach_arm::GetSetForNativeRegNum (reg);

    if (set == -1)
        return false;

    if (ReadRegisterSet(set, false) != KERN_SUCCESS)
        return false;

    switch (reg)
    {
    case gpr_r0:
    case gpr_r1:
    case gpr_r2:
    case gpr_r3:
    case gpr_r4:
    case gpr_r5:
    case gpr_r6:
    case gpr_r7:
    case gpr_r8:
    case gpr_r9:
    case gpr_r10:
    case gpr_r11:
    case gpr_r12:
    case gpr_sp:
    case gpr_lr:
    case gpr_pc:
    case gpr_cpsr:
        value = gpr.r[reg - gpr_r0];
        break;

    case fpu_s0:
    case fpu_s1:
    case fpu_s2:
    case fpu_s3:
    case fpu_s4:
    case fpu_s5:
    case fpu_s6:
    case fpu_s7:
    case fpu_s8:
    case fpu_s9:
    case fpu_s10:
    case fpu_s11:
    case fpu_s12:
    case fpu_s13:
    case fpu_s14:
    case fpu_s15:
    case fpu_s16:
    case fpu_s17:
    case fpu_s18:
    case fpu_s19:
    case fpu_s20:
    case fpu_s21:
    case fpu_s22:
    case fpu_s23:
    case fpu_s24:
    case fpu_s25:
    case fpu_s26:
    case fpu_s27:
    case fpu_s28:
    case fpu_s29:
    case fpu_s30:
    case fpu_s31:
        value = fpu.floats.s[reg];
        break;

    case fpu_fpscr:
        value = fpu.fpscr;
        break;

    case exc_exception:
        value = exc.exception;
        break;
    case exc_fsr:
        value = exc.fsr;
        break;
    case exc_far:
        value = exc.far;
        break;

    default:
        return false;

    }
    return true;
}


bool
RegisterContextMach_arm::WriteRegisterValue (uint32_t reg, const Scalar &value)
{
    int set = GetSetForNativeRegNum (reg);

    if (set == -1)
        return false;

    if (ReadRegisterSet(set, false) != KERN_SUCCESS)
        return false;

    switch (reg)
    {
    case gpr_r0:
    case gpr_r1:
    case gpr_r2:
    case gpr_r3:
    case gpr_r4:
    case gpr_r5:
    case gpr_r6:
    case gpr_r7:
    case gpr_r8:
    case gpr_r9:
    case gpr_r10:
    case gpr_r11:
    case gpr_r12:
    case gpr_sp:
    case gpr_lr:
    case gpr_pc:
    case gpr_cpsr:
        gpr.r[reg - gpr_r0] = value.UInt(0);
        break;

    case fpu_s0:
    case fpu_s1:
    case fpu_s2:
    case fpu_s3:
    case fpu_s4:
    case fpu_s5:
    case fpu_s6:
    case fpu_s7:
    case fpu_s8:
    case fpu_s9:
    case fpu_s10:
    case fpu_s11:
    case fpu_s12:
    case fpu_s13:
    case fpu_s14:
    case fpu_s15:
    case fpu_s16:
    case fpu_s17:
    case fpu_s18:
    case fpu_s19:
    case fpu_s20:
    case fpu_s21:
    case fpu_s22:
    case fpu_s23:
    case fpu_s24:
    case fpu_s25:
    case fpu_s26:
    case fpu_s27:
    case fpu_s28:
    case fpu_s29:
    case fpu_s30:
    case fpu_s31:
        fpu.floats.s[reg] = value.UInt(0);
        break;

    case fpu_fpscr:
        fpu.fpscr = value.UInt(0);
        break;

    case exc_exception:
        exc.exception = value.UInt(0);
        break;
    case exc_fsr:
        exc.fsr = value.UInt(0);
        break;
    case exc_far:
        exc.far = value.UInt(0);
        break;

    default:
        return false;

    }
    return WriteRegisterSet(set) == KERN_SUCCESS;
}

bool
RegisterContextMach_arm::ReadRegisterBytes (uint32_t reg, DataExtractor &data)
{
    int set = RegisterContextMach_arm::GetSetForNativeRegNum (reg);
    if (set == -1)
        return false;

    if (ReadRegisterSet(set, false) != KERN_SUCCESS)
        return false;

    const RegisterInfo * reg_info = GetRegisterInfoAtIndex (reg);
    if (reg_info == NULL)
        return false;

    switch (reg)
    {
    case gpr_r0:
    case gpr_r1:
    case gpr_r2:
    case gpr_r3:
    case gpr_r4:
    case gpr_r5:
    case gpr_r6:
    case gpr_r7:
    case gpr_r8:
    case gpr_r9:
    case gpr_r10:
    case gpr_r11:
    case gpr_r12:
    case gpr_sp:
    case gpr_lr:
    case gpr_pc:
    case gpr_cpsr:
        data.SetData(&gpr.r[reg - gpr_r0], reg_info->byte_size, eByteOrderHost);
        break;

    case fpu_s0:
    case fpu_s1:
    case fpu_s2:
    case fpu_s3:
    case fpu_s4:
    case fpu_s5:
    case fpu_s6:
    case fpu_s7:
    case fpu_s8:
    case fpu_s9:
    case fpu_s10:
    case fpu_s11:
    case fpu_s12:
    case fpu_s13:
    case fpu_s14:
    case fpu_s15:
    case fpu_s16:
    case fpu_s17:
    case fpu_s18:
    case fpu_s19:
    case fpu_s20:
    case fpu_s21:
    case fpu_s22:
    case fpu_s23:
    case fpu_s24:
    case fpu_s25:
    case fpu_s26:
    case fpu_s27:
    case fpu_s28:
    case fpu_s29:
    case fpu_s30:
    case fpu_s31:
        data.SetData(&fpu.floats.s[reg - fpu_s0], reg_info->byte_size, eByteOrderHost);
        break;

    case fpu_fpscr:
        data.SetData(&fpu.fpscr, reg_info->byte_size, eByteOrderHost);
        break;

    case exc_exception:
        data.SetData(&exc.exception, reg_info->byte_size, eByteOrderHost);
        break;

    case exc_fsr:
        data.SetData(&exc.fsr, reg_info->byte_size, eByteOrderHost);
        break;

    case exc_far:
        data.SetData(&exc.far, reg_info->byte_size, eByteOrderHost);
        break;

    default:
        return false;
    }
    return true;
}

bool
RegisterContextMach_arm::WriteRegisterBytes (uint32_t reg, DataExtractor &data, uint32_t data_offset)
{
    int set = GetSetForNativeRegNum (reg);

    if (set == -1)
        return false;

    if (ReadRegisterSet(set, false) != KERN_SUCCESS)
        return false;


    const RegisterInfo * reg_info = GetRegisterInfoAtIndex (reg);
    if (reg_info == NULL && data.ValidOffsetForDataOfSize(data_offset, reg_info->byte_size))
        return false;

    uint32_t offset = data_offset;
    switch (reg)
    {
    case gpr_r0:
    case gpr_r1:
    case gpr_r2:
    case gpr_r3:
    case gpr_r4:
    case gpr_r5:
    case gpr_r6:
    case gpr_r7:
    case gpr_r8:
    case gpr_r9:
    case gpr_r10:
    case gpr_r11:
    case gpr_r12:
    case gpr_sp:
    case gpr_lr:
    case gpr_pc:
    case gpr_cpsr:
        gpr.r[reg - gpr_r0] = data.GetU32 (&offset);
        break;

    case fpu_s0:
    case fpu_s1:
    case fpu_s2:
    case fpu_s3:
    case fpu_s4:
    case fpu_s5:
    case fpu_s6:
    case fpu_s7:
    case fpu_s8:
    case fpu_s9:
    case fpu_s10:
    case fpu_s11:
    case fpu_s12:
    case fpu_s13:
    case fpu_s14:
    case fpu_s15:
    case fpu_s16:
    case fpu_s17:
    case fpu_s18:
    case fpu_s19:
    case fpu_s20:
    case fpu_s21:
    case fpu_s22:
    case fpu_s23:
    case fpu_s24:
    case fpu_s25:
    case fpu_s26:
    case fpu_s27:
    case fpu_s28:
    case fpu_s29:
    case fpu_s30:
    case fpu_s31:
        fpu.floats.s[reg - fpu_s0] = data.GetU32 (&offset);
        break;

    case fpu_fpscr:
        fpu.fpscr = data.GetU32 (&offset);
        break;

    case exc_exception:
        fpu.fpscr = data.GetU32 (&offset);
        break;

    case exc_fsr:
        exc.fsr = data.GetU32 (&offset);
        break;

    case exc_far:
        exc.far = data.GetU32 (&offset);
        break;

    default:
        return false;
    }
    return WriteRegisterSet(set) == KERN_SUCCESS;
}

bool
RegisterContextMach_arm::ReadAllRegisterValues (lldb::DataBufferSP &data_sp)
{
    data_sp.reset (new DataBufferHeap (REG_CONTEXT_SIZE, 0));
    if (data_sp &&
        ReadGPR (false) == KERN_SUCCESS &&
        ReadFPU (false) == KERN_SUCCESS &&
        ReadEXC (false) == KERN_SUCCESS)
    {
        uint8_t *dst = data_sp->GetBytes();
        ::memcpy (dst, &gpr, sizeof(gpr));
        dst += sizeof(gpr);

        ::memcpy (dst, &fpu, sizeof(fpu));
        dst += sizeof(gpr);

        ::memcpy (dst, &exc, sizeof(exc));
        return true;
    }
    return false;
}

bool
RegisterContextMach_arm::WriteAllRegisterValues (const lldb::DataBufferSP &data_sp)
{
    if (data_sp && data_sp->GetByteSize() == REG_CONTEXT_SIZE)
    {
        const uint8_t *src = data_sp->GetBytes();
        ::memcpy (&gpr, src, sizeof(gpr));
        src += sizeof(gpr);

        ::memcpy (&fpu, src, sizeof(fpu));
        src += sizeof(gpr);

        ::memcpy (&exc, src, sizeof(exc));
        uint32_t success_count = 0;
        if (WriteGPR() == KERN_SUCCESS)
            ++success_count;
        if (WriteFPU() == KERN_SUCCESS)
            ++success_count;
        if (WriteEXC() == KERN_SUCCESS)
            ++success_count;
        return success_count == 3;
    }
    return false;
}

uint32_t
RegisterContextMach_arm::ConvertRegisterKindToRegisterNumber (uint32_t kind, uint32_t reg)
{
    if (kind == eRegisterKindGeneric)
    {
        switch (reg)
        {
        case LLDB_REGNUM_GENERIC_PC: return gpr_pc;
        case LLDB_REGNUM_GENERIC_SP: return gpr_sp;
        case LLDB_REGNUM_GENERIC_FP: return gpr_r7;
        case LLDB_REGNUM_GENERIC_RA: return gpr_lr;
        case LLDB_REGNUM_GENERIC_FLAGS: return gpr_cpsr;
        default:
            break;
        }
    }
    else if (kind == eRegisterKindDWARF)
    {
        switch (reg)
        {
        case dwarf_r0:  return gpr_r0;
        case dwarf_r1:  return gpr_r1;
        case dwarf_r2:  return gpr_r2;
        case dwarf_r3:  return gpr_r3;
        case dwarf_r4:  return gpr_r4;
        case dwarf_r5:  return gpr_r5;
        case dwarf_r6:  return gpr_r6;
        case dwarf_r7:  return gpr_r7;
        case dwarf_r8:  return gpr_r8;
        case dwarf_r9:  return gpr_r9;
        case dwarf_r10: return gpr_r10;
        case dwarf_r11: return gpr_r11;
        case dwarf_r12: return gpr_r12;
        case dwarf_sp:  return gpr_sp;
        case dwarf_lr:  return gpr_lr;
        case dwarf_pc:  return gpr_pc;
        case dwarf_spsr: return gpr_cpsr;

        case dwarf_s0:  return fpu_s0;
        case dwarf_s1:  return fpu_s1;
        case dwarf_s2:  return fpu_s2;
        case dwarf_s3:  return fpu_s3;
        case dwarf_s4:  return fpu_s4;
        case dwarf_s5:  return fpu_s5;
        case dwarf_s6:  return fpu_s6;
        case dwarf_s7:  return fpu_s7;
        case dwarf_s8:  return fpu_s8;
        case dwarf_s9:  return fpu_s9;
        case dwarf_s10: return fpu_s10;
        case dwarf_s11: return fpu_s11;
        case dwarf_s12: return fpu_s12;
        case dwarf_s13: return fpu_s13;
        case dwarf_s14: return fpu_s14;
        case dwarf_s15: return fpu_s15;
        case dwarf_s16: return fpu_s16;
        case dwarf_s17: return fpu_s17;
        case dwarf_s18: return fpu_s18;
        case dwarf_s19: return fpu_s19;
        case dwarf_s20: return fpu_s20;
        case dwarf_s21: return fpu_s21;
        case dwarf_s22: return fpu_s22;
        case dwarf_s23: return fpu_s23;
        case dwarf_s24: return fpu_s24;
        case dwarf_s25: return fpu_s25;
        case dwarf_s26: return fpu_s26;
        case dwarf_s27: return fpu_s27;
        case dwarf_s28: return fpu_s28;
        case dwarf_s29: return fpu_s29;
        case dwarf_s30: return fpu_s30;
        case dwarf_s31: return fpu_s31;

        default:
            break;
        }
    }
    else if (kind == eRegisterKindGCC)
    {
        switch (reg)
        {
        case gcc_r0:    return gpr_r0;
        case gcc_r1:    return gpr_r1;
        case gcc_r2:    return gpr_r2;
        case gcc_r3:    return gpr_r3;
        case gcc_r4:    return gpr_r4;
        case gcc_r5:    return gpr_r5;
        case gcc_r6:    return gpr_r6;
        case gcc_r7:    return gpr_r7;
        case gcc_r8:    return gpr_r8;
        case gcc_r9:    return gpr_r9;
        case gcc_r10:   return gpr_r10;
        case gcc_r11:   return gpr_r11;
        case gcc_r12:   return gpr_r12;
        case gcc_sp:    return gpr_sp;
        case gcc_lr:    return gpr_lr;
        case gcc_pc:    return gpr_pc;
        case gcc_cpsr:  return gpr_cpsr;
        }
    }
    else if (kind == eRegisterKindLLDB)
    {
        return reg;
    }
    return LLDB_INVALID_REGNUM;
}


uint32_t
RegisterContextMach_arm::NumSupportedHardwareBreakpoints ()
{
#if defined (__arm__)
    // Set the init value to something that will let us know that we need to
    // autodetect how many breakpoints are supported dynamically...
    static uint32_t g_num_supported_hw_breakpoints = UINT32_MAX;
    if (g_num_supported_hw_breakpoints == UINT32_MAX)
    {
        // Set this to zero in case we can't tell if there are any HW breakpoints
        g_num_supported_hw_breakpoints = 0;

        uint32_t register_DBGDIDR;

        asm("mrc p14, 0, %0, c0, c0, 0" : "=r" (register_DBGDIDR));
        g_num_supported_hw_breakpoints = bits(register_DBGDIDR, 27, 24);
        // Zero is reserved for the BRP count, so don't increment it if it is zero
        if (g_num_supported_hw_breakpoints > 0)
            g_num_supported_hw_breakpoints++;
        ProcessMacOSXLog::LogIf(PD_LOG_THREAD, "DBGDIDR=0x%8.8x (number BRP pairs = %u)", register_DBGDIDR, g_num_supported_hw_breakpoints);

    }
    return g_num_supported_hw_breakpoints;
#else
    // TODO: figure out remote case here!
    return 6;
#endif
}

uint32_t
RegisterContextMach_arm::SetHardwareBreakpoint (lldb::addr_t addr, size_t size)
{
    // Make sure our address isn't bogus
    if (addr & 1)
        return LLDB_INVALID_INDEX32;

    int kret = ReadDBG (false);

    if (kret == KERN_SUCCESS)
    {
        const uint32_t num_hw_breakpoints = NumSupportedHardwareBreakpoints();
        uint32_t i;
        for (i=0; i<num_hw_breakpoints; ++i)
        {
            if ((dbg.bcr[i] & BCR_ENABLE) == 0)
                break; // We found an available hw breakpoint slot (in i)
        }

        // See if we found an available hw breakpoint slot above
        if (i < num_hw_breakpoints)
        {
            // Make sure bits 1:0 are clear in our address
            dbg.bvr[i] = addr & ~((lldb::addr_t)3);

            if (size == 2 || addr & 2)
            {
                uint32_t byte_addr_select = (addr & 2) ? BAS_IMVA_2_3 : BAS_IMVA_0_1;

                // We have a thumb breakpoint
                // We have an ARM breakpoint
                dbg.bcr[i] =  BCR_M_IMVA_MATCH |    // Stop on address mismatch
                                        byte_addr_select |  // Set the correct byte address select so we only trigger on the correct opcode
                                        S_USER |            // Which modes should this breakpoint stop in?
                                        BCR_ENABLE;         // Enable this hardware breakpoint
                ProcessMacOSXLog::LogIf(PD_LOG_BREAKPOINTS, "RegisterContextMach_arm::EnableHardwareBreakpoint( addr = %8.8p, size = %u ) - BVR%u/BCR%u = 0x%8.8x / 0x%8.8x (Thumb)",
                        addr,
                        size,
                        i,
                        i,
                        dbg.bvr[i],
                        dbg.bcr[i]);
            }
            else if (size == 4)
            {
                // We have an ARM breakpoint
                dbg.bcr[i] =  BCR_M_IMVA_MATCH |    // Stop on address mismatch
                                        BAS_IMVA_ALL |      // Stop on any of the four bytes following the IMVA
                                        S_USER |            // Which modes should this breakpoint stop in?
                                        BCR_ENABLE;         // Enable this hardware breakpoint
                ProcessMacOSXLog::LogIf(PD_LOG_BREAKPOINTS, "RegisterContextMach_arm::EnableHardwareBreakpoint( addr = %8.8p, size = %u ) - BVR%u/BCR%u = 0x%8.8x / 0x%8.8x (ARM)",
                        addr,
                        size,
                        i,
                        i,
                        dbg.bvr[i],
                        dbg.bcr[i]);
            }

            kret = WriteDBG();
            ProcessMacOSXLog::LogIf(PD_LOG_BREAKPOINTS, "RegisterContextMach_arm::EnableHardwareBreakpoint() WriteDBG() => 0x%8.8x.", kret);

            if (kret == KERN_SUCCESS)
                return i;
        }
        else
        {
            ProcessMacOSXLog::LogIf(PD_LOG_BREAKPOINTS, "RegisterContextMach_arm::EnableHardwareBreakpoint(addr = %8.8p, size = %u) => all hardware breakpoint resources are being used.", addr, size);
        }
    }

    return LLDB_INVALID_INDEX32;
}

bool
RegisterContextMach_arm::ClearHardwareBreakpoint (uint32_t hw_index)
{
    int kret = ReadDBG (false);

    const uint32_t num_hw_points = NumSupportedHardwareBreakpoints();
    if (kret == KERN_SUCCESS)
    {
        if (hw_index < num_hw_points)
        {
            dbg.bcr[hw_index] = 0;
            ProcessMacOSXLog::LogIf(PD_LOG_BREAKPOINTS, "RegisterContextMach_arm::SetHardwareBreakpoint( %u ) - BVR%u = 0x%8.8x  BCR%u = 0x%8.8x",
                    hw_index,
                    hw_index,
                    dbg.bvr[hw_index],
                    hw_index,
                    dbg.bcr[hw_index]);

            kret = WriteDBG();

            if (kret == KERN_SUCCESS)
                return true;
        }
    }
    return false;
}

uint32_t
RegisterContextMach_arm::NumSupportedHardwareWatchpoints ()
{
#if defined (__arm__)
    // Set the init value to something that will let us know that we need to
    // autodetect how many watchpoints are supported dynamically...
    static uint32_t g_num_supported_hw_watchpoints = UINT32_MAX;
    if (g_num_supported_hw_watchpoints == UINT32_MAX)
    {
        // Set this to zero in case we can't tell if there are any HW breakpoints
        g_num_supported_hw_watchpoints = 0;

        uint32_t register_DBGDIDR;
        asm("mrc p14, 0, %0, c0, c0, 0" : "=r" (register_DBGDIDR));
        g_num_supported_hw_watchpoints = bits(register_DBGDIDR, 31, 28) + 1;
        ProcessMacOSXLog::LogIf(PD_LOG_THREAD, "DBGDIDR=0x%8.8x (number WRP pairs = %u)", register_DBGDIDR, g_num_supported_hw_watchpoints);
    }
    return g_num_supported_hw_watchpoints;
#else
    // TODO: figure out remote case here!
    return 2;
#endif
}


uint32_t
RegisterContextMach_arm::SetHardwareWatchpoint (lldb::addr_t addr, size_t size, bool read, bool write)
{
    ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::EnableHardwareWatchpoint(addr = %8.8p, size = %u, read = %u, write = %u)", addr, size, read, write);

    const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

    // Can't watch zero bytes
    if (size == 0)
        return LLDB_INVALID_INDEX32;

    // We must watch for either read or write
    if (read == false && write == false)
        return LLDB_INVALID_INDEX32;

    // Can't watch more than 4 bytes per WVR/WCR pair
    if (size > 4)
        return LLDB_INVALID_INDEX32;

    // We can only watch up to four bytes that follow a 4 byte aligned address
    // per watchpoint register pair. Since we have at most so we can only watch
    // until the next 4 byte boundary and we need to make sure we can properly
    // encode this.
    uint32_t addr_word_offset = addr % 4;
    ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::EnableHardwareWatchpoint() - addr_word_offset = 0x%8.8x", addr_word_offset);

    uint32_t byte_mask = ((1u << size) - 1u) << addr_word_offset;
    ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::EnableHardwareWatchpoint() - byte_mask = 0x%8.8x", byte_mask);
    if (byte_mask > 0xfu)
        return LLDB_INVALID_INDEX32;

    // Read the debug state
    int kret = ReadDBG (false);

    if (kret == KERN_SUCCESS)
    {
        // Check to make sure we have the needed hardware support
        uint32_t i = 0;

        for (i=0; i<num_hw_watchpoints; ++i)
        {
            if ((dbg.wcr[i] & WCR_ENABLE) == 0)
                break; // We found an available hw breakpoint slot (in i)
        }

        // See if we found an available hw breakpoint slot above
        if (i < num_hw_watchpoints)
        {
            // Make the byte_mask into a valid Byte Address Select mask
            uint32_t byte_address_select = byte_mask << 5;
            // Make sure bits 1:0 are clear in our address
            dbg.wvr[i] = addr & ~((lldb::addr_t)3);
            dbg.wcr[i] =  byte_address_select |       // Which bytes that follow the IMVA that we will watch
                                    S_USER |                    // Stop only in user mode
                                    (read ? WCR_LOAD : 0) |     // Stop on read access?
                                    (write ? WCR_STORE : 0) |   // Stop on write access?
                                    WCR_ENABLE;                 // Enable this watchpoint;

            kret = WriteDBG();
            ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::EnableHardwareWatchpoint() WriteDBG() => 0x%8.8x.", kret);

            if (kret == KERN_SUCCESS)
                return i;
        }
        else
        {
            ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::EnableHardwareWatchpoint(): All hardware resources (%u) are in use.", num_hw_watchpoints);
        }
    }
    return LLDB_INVALID_INDEX32;
}

bool
RegisterContextMach_arm::ClearHardwareWatchpoint (uint32_t hw_index)
{
    int kret = ReadDBG (false);

    const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
    if (kret == KERN_SUCCESS)
    {
        if (hw_index < num_hw_points)
        {
            dbg.wcr[hw_index] = 0;
            ProcessMacOSXLog::LogIf(PD_LOG_WATCHPOINTS, "RegisterContextMach_arm::ClearHardwareWatchpoint( %u ) - WVR%u = 0x%8.8x  WCR%u = 0x%8.8x",
                    hw_index,
                    hw_index,
                    dbg.wvr[hw_index],
                    hw_index,
                    dbg.wcr[hw_index]);

            kret = WriteDBG();

            if (kret == KERN_SUCCESS)
                return true;
        }
    }
    return false;
}


