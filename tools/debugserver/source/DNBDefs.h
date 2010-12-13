//===-- DNBDefs.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/26/07.
//
//===----------------------------------------------------------------------===//

#ifndef __DNBDefs_h__
#define __DNBDefs_h__

#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syslimits.h>
#include <unistd.h>

//----------------------------------------------------------------------
// Define nub_addr_t and the invalid address value from the architecture
//----------------------------------------------------------------------
#if defined (__x86_64__) || defined (__ppc64__)

//----------------------------------------------------------------------
// 64 bit address architectures
//----------------------------------------------------------------------
typedef uint64_t        nub_addr_t;
#define INVALID_NUB_ADDRESS     ((nub_addr_t)~0ull)

#elif defined (__i386__) || defined (__powerpc__) || defined (__ppc__) || defined (__arm__)

//----------------------------------------------------------------------
// 32 bit address architectures
//----------------------------------------------------------------------

typedef uint32_t        nub_addr_t;
#define INVALID_NUB_ADDRESS     ((nub_addr_t)~0ul)

#else

//----------------------------------------------------------------------
// Default to 64 bit address for unrecognized architectures.
//----------------------------------------------------------------------

#warning undefined architecture, defaulting to 8 byte addresses
typedef uint64_t        nub_addr_t;
#define INVALID_NUB_ADDRESS     ((nub_addr_t)~0ull)


#endif

typedef size_t          nub_size_t;
typedef ssize_t         nub_ssize_t;
typedef uint32_t        nub_break_t;
typedef uint32_t        nub_watch_t;
typedef uint32_t        nub_index_t;
typedef pid_t           nub_process_t;
typedef unsigned int    nub_thread_t;
typedef uint32_t        nub_event_t;
typedef uint32_t        nub_bool_t;

#define INVALID_NUB_BREAK_ID    ((nub_break_t)0)
#define INVALID_NUB_PROCESS     ((nub_process_t)0)
#define INVALID_NUB_THREAD      ((nub_thread_t)0)
#define INVALID_NUB_HW_INDEX    UINT32_MAX
#define INVALID_NUB_REGNUM      UINT32_MAX
#define NUB_GENERIC_ERROR       UINT32_MAX

#define NUB_BREAK_ID_IS_VALID(breakID)    ((breakID) != (INVALID_NUB_BREAK_ID))

// Watchpoint types
#define WATCH_TYPE_READ     (1u << 0)
#define WATCH_TYPE_WRITE    (1u << 1)

typedef enum
{
    eStateInvalid = 0,
    eStateUnloaded,
    eStateAttaching,
    eStateLaunching,
    eStateStopped,
    eStateRunning,
    eStateStepping,
    eStateCrashed,
    eStateDetached,
    eStateExited,
    eStateSuspended
} nub_state_t;

typedef enum
{
    eLaunchFlavorDefault = 0,
    eLaunchFlavorPosixSpawn,
    eLaunchFlavorForkExec,
#if defined (__arm__)
    eLaunchFlavorSpringBoard,
#endif
} nub_launch_flavor_t;

#define NUB_STATE_IS_RUNNING(s) ((s) == eStateAttaching ||\
                                 (s) == eStateLaunching ||\
                                 (s) == eStateRunning ||\
                                 (s) == eStateStepping ||\
                                 (s) == eStateDetached)

#define NUB_STATE_IS_STOPPED(s) ((s) == eStateUnloaded ||\
                                 (s) == eStateStopped ||\
                                 (s) == eStateCrashed ||\
                                 (s) == eStateExited)

enum
{
    eEventProcessRunningStateChanged = 1 << 0,  // The process has changed state to running
    eEventProcessStoppedStateChanged = 1 << 1,  // The process has changed state to stopped
    eEventSharedLibsStateChange = 1 << 2,       // Shared libraries loaded/unloaded state has changed
    eEventStdioAvailable = 1 << 3,              // Something is available on stdout/stderr
    eEventProcessAsyncInterrupt = 1 << 4,               // Gives the ability for any infinite wait calls to be interrupted
    kAllEventsMask = eEventProcessRunningStateChanged |
                     eEventProcessStoppedStateChanged |
                     eEventSharedLibsStateChange |
                     eEventStdioAvailable |
                     eEventProcessAsyncInterrupt
};

#define LOG_VERBOSE             (1u << 0)
#define LOG_PROCESS             (1u << 1)
#define LOG_THREAD              (1u << 2)
#define LOG_EXCEPTIONS          (1u << 3)
#define LOG_SHLIB               (1u << 4)
#define LOG_MEMORY              (1u << 5)    // Log memory reads/writes calls
#define LOG_MEMORY_DATA_SHORT   (1u << 6)    // Log short memory reads/writes bytes
#define LOG_MEMORY_DATA_LONG    (1u << 7)    // Log all memory reads/writes bytes
#define LOG_MEMORY_PROTECTIONS  (1u << 8)    // Log memory protection changes
#define LOG_BREAKPOINTS         (1u << 9)
#define LOG_EVENTS              (1u << 10)
#define LOG_WATCHPOINTS         (1u << 11)
#define LOG_STEP                (1u << 12)
#define LOG_TASK                (1u << 13)
#define LOG_LO_USER             (1u << 16)
#define LOG_HI_USER             (1u << 31)
#define LOG_ALL                 0xFFFFFFFFu
#define LOG_DEFAULT             ((LOG_PROCESS) |\
                                 (LOG_TASK) |\
                                 (LOG_THREAD) |\
                                 (LOG_EXCEPTIONS) |\
                                 (LOG_SHLIB) |\
                                 (LOG_MEMORY) |\
                                 (LOG_BREAKPOINTS) |\
                                 (LOG_WATCHPOINTS) |\
                                 (LOG_STEP))


#define REGISTER_SET_ALL        0
// Generic Register set to be defined by each architecture for access to common
// register values.
#define REGISTER_SET_GENERIC    ((uint32_t)0xFFFFFFFFu)
#define GENERIC_REGNUM_PC       0   // Program Counter
#define GENERIC_REGNUM_SP       1   // Stack Pointer
#define GENERIC_REGNUM_FP       2   // Frame Pointer
#define GENERIC_REGNUM_RA       3   // Return Address
#define GENERIC_REGNUM_FLAGS    4   // Processor flags register

enum DNBRegisterType
{
    InvalidRegType = 0,
    Uint,               // unsigned integer
    Sint,               // signed integer
    IEEE754,            // float
    Vector              // vector registers
};

enum DNBRegisterFormat
{
    InvalidRegFormat = 0,
    Binary,
    Decimal,
    Hex,
    Float,
    VectorOfSInt8,
    VectorOfUInt8,
    VectorOfSInt16,
    VectorOfUInt16,
    VectorOfSInt32,
    VectorOfUInt32,
    VectorOfFloat32,
    VectorOfUInt128
};

struct DNBRegisterInfo
{
    uint32_t    set;            // Register set
    uint32_t    reg;            // Register number
    const char *name;           // Name of this register
    const char *alt;            // Alternate name
    uint16_t    type;           // Type of the register bits (DNBRegisterType)
    uint16_t    format;         // Default format for display (DNBRegisterFormat),
    uint32_t    size;           // Size in bytes of the register
    uint32_t    offset;         // Offset from the beginning of the register context
    uint32_t    reg_gcc;        // GCC register number (INVALID_NUB_REGNUM when none)
    uint32_t    reg_dwarf;      // DWARF register number (INVALID_NUB_REGNUM when none)
    uint32_t    reg_generic;    // Generic register number (INVALID_NUB_REGNUM when none)
    uint32_t    reg_gdb;        // The GDB register number (INVALID_NUB_REGNUM when none)
};

struct DNBRegisterSetInfo
{
    const char *name;                           // Name of this register set
    const struct DNBRegisterInfo *registers;    // An array of register descriptions
    nub_size_t num_registers;                   // The number of registers in REGISTERS array above
};

struct DNBThreadResumeAction
{
    nub_thread_t tid;   // The thread ID that this action applies to, INVALID_NUB_THREAD for the default thread action
    nub_state_t state;  // Valid values are eStateStopped/eStateSuspended, eStateRunning, and eStateStepping.
    int signal;         // When resuming this thread, resume it with this signal
    nub_addr_t addr;    // If not INVALID_NUB_ADDRESS, then set the PC for the thread to ADDR before resuming/stepping
};

enum DNBThreadStopType
{
    eStopTypeInvalid = 0,
    eStopTypeSignal,
    eStopTypeException
};

enum DNBMemoryPermissions
{
    eMemoryPermissionsWritable    = (1 << 0),
    eMemoryPermissionsReadable    = (1 << 1),
    eMemoryPermissionsExecutable  = (1 << 2)
};

#define DNB_THREAD_STOP_INFO_MAX_DESC_LENGTH    256
#define DNB_THREAD_STOP_INFO_MAX_EXC_DATA       8

//----------------------------------------------------------------------
// DNBThreadStopInfo
//
// Describes the reason a thread stopped.
//----------------------------------------------------------------------
struct DNBThreadStopInfo
{
    DNBThreadStopType reason;
    char description[DNB_THREAD_STOP_INFO_MAX_DESC_LENGTH];
    union
    {
        // eStopTypeSignal
        struct
        {
            uint32_t signo;
        } signal;

        // eStopTypeException
        struct
        {
            uint32_t type;
            nub_size_t data_count;
            nub_addr_t data[DNB_THREAD_STOP_INFO_MAX_EXC_DATA];
        } exception;
    } details;
};


struct DNBRegisterValue
{
    struct DNBRegisterInfo info;    // Register information for this register
    union
    {
        int8_t      sint8;
        int16_t     sint16;
        int32_t     sint32;
        int64_t     sint64;
        uint8_t     uint8;
        uint16_t    uint16;
        uint32_t    uint32;
        uint64_t    uint64;
        float       float32;
        double      float64;
        int8_t      v_sint8[16];
        int16_t     v_sint16[8];
        int32_t     v_sint32[4];
        int64_t     v_sint64[2];
        uint8_t     v_uint8[16];
        uint16_t    v_uint16[8];
        uint32_t    v_uint32[4];
        uint64_t    v_uint64[2];
        float       v_float32[4];
        double      v_float64[2];
        void        *pointer;
        char        *c_str;
    } value;
};

enum DNBSharedLibraryState
{
    eShlibStateUnloaded    = 0,
    eShlibStateLoaded    = 1
};

#ifndef DNB_MAX_SEGMENT_NAME_LENGTH
#define DNB_MAX_SEGMENT_NAME_LENGTH    32
#endif

struct DNBSegment
{
    char        name[DNB_MAX_SEGMENT_NAME_LENGTH];
    nub_addr_t  addr;
    nub_addr_t  size;
};

struct DNBExecutableImageInfo
{
    char        name[PATH_MAX]; // Name of the executable image (usually a full path)
    uint32_t    state;          // State of the executable image (see enum DNBSharedLibraryState)
    nub_addr_t  header_addr;    // Executable header address
    uuid_t      uuid;           // Unique indentifier for matching with symbols
    uint32_t    num_segments;   // Number of contiguous memory segments to in SEGMENTS array
    DNBSegment  *segments;      // Array of contiguous memory segments in executable
};

typedef nub_bool_t (*DNBCallbackBreakpointHit)(nub_process_t pid, nub_thread_t tid, nub_break_t breakID, void *baton);
typedef nub_addr_t (*DNBCallbackNameToAddress)(nub_process_t pid, const char *name, const char *shlib_regex, void *baton);
typedef nub_size_t (*DNBCallbackCopyExecutableImageInfos)(nub_process_t pid, struct DNBExecutableImageInfo **image_infos, nub_bool_t only_changed, void *baton);
typedef void (*DNBCallbackLog)(void *baton, uint32_t flags, const char *format, va_list args);

#endif    // #ifndef __DNBDefs_h__
