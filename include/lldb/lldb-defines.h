//===-- lldb-defines.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_defines_h_
#define LLDB_defines_h_

#include "lldb/lldb-types.h"

#if !defined(UINT32_MAX)
    #define UINT32_MAX 4294967295U
#endif

#if !defined(UINT64_MAX)
    #define UINT64_MAX 18446744073709551615ULL
#endif

//----------------------------------------------------------------------
// lldb defines
//----------------------------------------------------------------------
#define LLDB_GENERIC_ERROR              UINT32_MAX

//----------------------------------------------------------------------
// Breakpoints
//----------------------------------------------------------------------
#define LLDB_INVALID_BREAK_ID           0
#define LLDB_DEFAULT_BREAK_SIZE         0
#define LLDB_BREAK_ID_IS_VALID(bid)     ((bid) != (LLDB_INVALID_BREAK_ID))
#define LLDB_BREAK_ID_IS_INTERNAL(bid)  ((bid) < 0)

//----------------------------------------------------------------------
// Watchpoints
//----------------------------------------------------------------------
#define LLDB_INVALID_WATCH_ID           0
#define LLDB_WATCH_ID_IS_VALID(uid)     ((uid) != (LLDB_INVALID_WATCH_ID))
#define LLDB_WATCH_TYPE_READ            (1u << 0)
#define LLDB_WATCH_TYPE_WRITE           (1u << 1)

//----------------------------------------------------------------------
// Generic Register Numbers
//----------------------------------------------------------------------
#define LLDB_REGNUM_GENERIC_PC          0   // Program Counter
#define LLDB_REGNUM_GENERIC_SP          1   // Stack Pointer
#define LLDB_REGNUM_GENERIC_FP          2   // Frame Pointer
#define LLDB_REGNUM_GENERIC_RA          3   // Return Address
#define LLDB_REGNUM_GENERIC_FLAGS       4   // Processor flags register

//----------------------------------------------------------------------
/// Invalid value definitions
//----------------------------------------------------------------------
#define LLDB_INVALID_ADDRESS            UINT64_MAX
#define LLDB_INVALID_INDEX32            UINT32_MAX
#define LLDB_INVALID_IMAGE_TOKEN        UINT32_MAX
#define LLDB_INVALID_REGNUM             UINT32_MAX
#define LLDB_INVALID_UID                UINT32_MAX
#define LLDB_INVALID_PROCESS_ID         0
#define LLDB_INVALID_THREAD_ID          0
#define LLDB_INVALID_FRAME_ID           UINT32_MAX
#define LLDB_INVALID_SIGNAL_NUMBER      INT32_MAX

//----------------------------------------------------------------------
/// CPU Type defintions
//----------------------------------------------------------------------
#define LLDB_ARCH_DEFAULT               "systemArch"
#define LLDB_ARCH_DEFAULT_32BIT         "systemArch32"
#define LLDB_ARCH_DEFAULT_64BIT         "systemArch64"
#define LLDB_INVALID_CPUTYPE            (0xFFFFFFFEu)

//----------------------------------------------------------------------
/// Option Set defintions
//----------------------------------------------------------------------
// FIXME: I'm sure there's some #define magic that can create all 32 sets on the
// fly.  That would have the added benefit of making this unreadable.
#define LLDB_MAX_NUM_OPTION_SETS        32
#define LLDB_OPT_SET_ALL                0xFFFFFFFF
#define LLDB_OPT_SET_1                  (1 << 0)
#define LLDB_OPT_SET_2                  (1 << 1)
#define LLDB_OPT_SET_3                  (1 << 2)
#define LLDB_OPT_SET_4                  (1 << 3)
#define LLDB_OPT_SET_5                  (1 << 4)
#define LLDB_OPT_SET_6                  (1 << 5)
#define LLDB_OPT_SET_7                  (1 << 6)
#define LLDB_OPT_SET_8                  (1 << 7)

#if defined(__cplusplus)

//----------------------------------------------------------------------
/// @def DISALLOW_COPY_AND_ASSIGN(TypeName)
///     Macro definition for easily disallowing copy constructor and
///     assignment operators in C++ classes.
//----------------------------------------------------------------------
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&); \
    const TypeName& operator=(const TypeName&)

#endif // #if defined(__cplusplus)

#endif  // LLDB_defines_h_
