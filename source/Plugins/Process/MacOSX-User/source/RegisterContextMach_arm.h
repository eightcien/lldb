//===-- RegisterContextMach_arm.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_RegisterContextMach_arm_h_
#define liblldb_RegisterContextMach_arm_h_

// C Includes
#include <mach/mach_types.h>

// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Target/RegisterContext.h"

// BCR address match type
#define BCR_M_IMVA_MATCH        ((uint32_t)(0u << 21))
#define BCR_M_CONTEXT_ID_MATCH  ((uint32_t)(1u << 21))
#define BCR_M_IMVA_MISMATCH     ((uint32_t)(2u << 21))
#define BCR_M_RESERVED          ((uint32_t)(3u << 21))

// Link a BVR/BCR or WVR/WCR pair to another
#define E_ENABLE_LINKING        ((uint32_t)(1u << 20))

// Byte Address Select
#define BAS_IMVA_PLUS_0         ((uint32_t)(1u << 5))
#define BAS_IMVA_PLUS_1         ((uint32_t)(1u << 6))
#define BAS_IMVA_PLUS_2         ((uint32_t)(1u << 7))
#define BAS_IMVA_PLUS_3         ((uint32_t)(1u << 8))
#define BAS_IMVA_0_1            ((uint32_t)(3u << 5))
#define BAS_IMVA_2_3            ((uint32_t)(3u << 7))
#define BAS_IMVA_ALL            ((uint32_t)(0xfu << 5))

// Break only in privileged or user mode
#define S_RSVD                  ((uint32_t)(0u << 1))
#define S_PRIV                  ((uint32_t)(1u << 1))
#define S_USER                  ((uint32_t)(2u << 1))
#define S_PRIV_USER             ((S_PRIV) | (S_USER))

#define BCR_ENABLE              ((uint32_t)(1u))
#define WCR_ENABLE              ((uint32_t)(1u))

// Watchpoint load/store
#define WCR_LOAD                ((uint32_t)(1u << 3))
#define WCR_STORE               ((uint32_t)(1u << 4))

class RegisterContextMach_arm : public lldb_private::RegisterContext
{
public:

    RegisterContextMach_arm(lldb_private::Thread &thread, uint32_t concrete_frame_idx);

    virtual
    ~RegisterContextMach_arm();

    virtual void
    Invalidate ();

    virtual size_t
    GetRegisterCount ();

    virtual const lldb::RegisterInfo *
    GetRegisterInfoAtIndex (uint32_t reg);

    virtual size_t
    GetRegisterSetCount ();

    virtual const lldb::RegisterSet *
    GetRegisterSet (uint32_t set);

    virtual bool
    ReadRegisterValue (uint32_t reg, lldb_private::Scalar &value);

    virtual bool
    ReadRegisterBytes (uint32_t reg, lldb_private::DataExtractor &data);

    virtual bool
    ReadAllRegisterValues (lldb::DataBufferSP &data_sp);

    virtual bool
    WriteRegisterValue (uint32_t reg, const lldb_private::Scalar &value);

    virtual bool
    WriteRegisterBytes (uint32_t reg, lldb_private::DataExtractor &data, uint32_t data_offset = 0);

    virtual bool
    WriteAllRegisterValues (const lldb::DataBufferSP &data_sp);

    virtual uint32_t
    ConvertRegisterKindToRegisterNumber (uint32_t kind, uint32_t num);

    virtual uint32_t
    NumSupportedHardwareBreakpoints ();

    virtual uint32_t
    SetHardwareBreakpoint (lldb::addr_t addr, size_t size);

    virtual bool
    ClearHardwareBreakpoint (uint32_t hw_idx);

    virtual uint32_t
    NumSupportedHardwareWatchpoints ();

    virtual uint32_t
    SetHardwareWatchpoint (lldb::addr_t addr, size_t size, bool read, bool write);

    virtual bool
    ClearHardwareWatchpoint (uint32_t hw_index);

    struct GPR
    {
        uint32_t    r[16];  // R0-R15
        uint32_t    cpsr;   // CPSR
    };


    struct FPU
    {
        union {
            uint32_t s[32];
            uint64_t d[16];
        } floats;
        uint32_t fpscr;
    };

//  struct NeonReg
//  {
//      uint8_t bytes[16];
//  };
//
//  struct VFPv3
//  {
//      union {
//          uint32_t s[32];
//          uint64_t d[32];
//          NeonReg  q[16];
//      } v3;
//      uint32_t fpscr;
//  };

    struct EXC
    {
        uint32_t    exception;
        uint32_t    fsr; /* Fault status */
        uint32_t    far; /* Virtual Fault Address */
    };

    struct DBG
    {
        uint32_t bvr[16];
        uint32_t bcr[16];
        uint32_t wvr[16];
        uint32_t wcr[16];
    };

    static void
    LogDBGRegisters (lldb_private::Log *log, const DBG& dbg);

protected:

    enum
    {
        GPRRegSet = 1,
        FPURegSet = 2,
        EXCRegSet = 3,
        DBGRegSet = 4,
    };

    enum
    {
        GPRWordCount = sizeof(GPR)/sizeof(uint32_t),
        FPUWordCount = sizeof(FPU)/sizeof(uint32_t),
        EXCWordCount = sizeof(EXC)/sizeof(uint32_t),
        DBGWordCount = sizeof(DBG)/sizeof(uint32_t)
    };

    enum
    {
        Read = 0,
        Write = 1,
        kNumErrors = 2
    };

    GPR gpr;
    FPU fpu;
    EXC exc;
    DBG dbg;
    int gpr_errs[2]; // Read/Write errors
    int fpu_errs[2]; // Read/Write errors
    int exc_errs[2]; // Read/Write errors
    int dbg_errs[2]; // Read/Write errors

    void
    InvalidateAllRegisterStates()
    {
        SetError (GPRRegSet, Read, -1);
        SetError (FPURegSet, Read, -1);
        SetError (EXCRegSet, Read, -1);
    }

    int
    GetError (int flavor, uint32_t err_idx) const
    {
        if (err_idx < kNumErrors)
        {
            switch (flavor)
            {
            // When getting all errors, just OR all values together to see if
            // we got any kind of error.
            case GPRRegSet:    return gpr_errs[err_idx];
            case FPURegSet:    return fpu_errs[err_idx];
            case EXCRegSet:    return exc_errs[err_idx];
            case DBGRegSet:    return dbg_errs[err_idx];
            default: break;
            }
        }
        return -1;
    }

    bool
    SetError (int flavor, uint32_t err_idx, int err)
    {
        if (err_idx < kNumErrors)
        {
            switch (flavor)
            {
            case GPRRegSet:
                gpr_errs[err_idx] = err;
                return true;

            case FPURegSet:
                fpu_errs[err_idx] = err;
                return true;

            case EXCRegSet:
                exc_errs[err_idx] = err;
                return true;

            case DBGRegSet:
                exc_errs[err_idx] = err;
                return true;

            default: break;
            }
        }
        return false;
    }

    bool
    RegisterSetIsCached (int set) const
    {
        return GetError(set, Read) == 0;
    }

    int
    ReadGPR (bool force);

    int
    ReadFPU (bool force);

    int
    ReadEXC (bool force);

    int
    ReadDBG (bool force);

    int
    WriteGPR ();

    int
    WriteFPU ();

    int
    WriteEXC ();

    int
    WriteDBG ();

    int
    ReadRegisterSet (uint32_t set, bool force);

    int
    WriteRegisterSet (uint32_t set);

    static uint32_t
    GetRegisterNumber (uint32_t reg_kind, uint32_t reg_num);

    static int
    GetSetForNativeRegNum (int reg_num);

    static size_t
    GetRegisterInfosCount ();

    static const lldb::RegisterInfo *
    GetRegisterInfos ();
};

#endif  // liblldb_RegisterContextMach_arm_h_
