//===-- MachThread.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/19/07.
//
//===----------------------------------------------------------------------===//

#ifndef __MachThread_h__
#define __MachThread_h__

#include <string>
#include <vector>
#include <tr1/memory> // for std::tr1::shared_ptr

#include <libproc.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/signal.h>

#include "PThreadCondition.h"
#include "PThreadMutex.h"
#include "MachException.h"
#include "DNBArch.h"
#include "DNBRegisterInfo.h"

class DNBBreakpoint;
class MachProcess;

class MachThread
{
public:

                    MachThread (MachProcess *process, thread_t thread = 0);
                    ~MachThread ();

    MachProcess *   Process() { return m_process; }
    const MachProcess *
                    Process() const { return m_process; }
    nub_process_t   ProcessID() const;
    void            Dump(uint32_t index);
    thread_t        ThreadID() const { return m_tid; }
    thread_t        InferiorThreadID() const;

    uint32_t        SequenceID() const { return m_seq_id; }
    static bool     ThreadIDIsValid(thread_t thread);
    uint32_t        Resume();
    uint32_t        Suspend();
    uint32_t        SuspendCount() const { return m_suspendCount; }
    bool            RestoreSuspendCount();

    bool            GetRegisterState(int flavor, bool force);
    bool            SetRegisterState(int flavor);
    uint64_t        GetPC(uint64_t failValue = INVALID_NUB_ADDRESS);    // Get program counter
    bool            SetPC(uint64_t value);                              // Set program counter
    uint64_t        GetSP(uint64_t failValue = INVALID_NUB_ADDRESS);    // Get stack pointer

    nub_break_t     CurrentBreakpoint() const { return m_breakID; }
    void            SetCurrentBreakpoint(nub_break_t breakID) { m_breakID = breakID; }
    uint32_t        EnableHardwareBreakpoint (const DNBBreakpoint *breakpoint);
    uint32_t        EnableHardwareWatchpoint (const DNBBreakpoint *watchpoint);
    bool            DisableHardwareBreakpoint (const DNBBreakpoint *breakpoint);
    bool            DisableHardwareWatchpoint (const DNBBreakpoint *watchpoint);

    nub_state_t     GetState();
    void            SetState(nub_state_t state);

    void            ThreadWillResume (const DNBThreadResumeAction *thread_action);
    bool            ShouldStop(bool &step_more);
    bool            IsStepping();
    bool            ThreadDidStop();
    bool            NotifyException(MachException::Data& exc);
    const MachException::Data& GetStopException() { return m_stop_exception; }

    uint32_t        GetNumRegistersInSet(int regSet) const;
    const char *    GetRegisterSetName(int regSet) const;
    const DNBRegisterInfo *
                    GetRegisterInfo(int regSet, int regIndex) const;
    void            DumpRegisterState(int regSet);
    const DNBRegisterSetInfo *
                    GetRegisterSetInfo(nub_size_t *num_reg_sets ) const;
    bool            GetRegisterValue ( uint32_t reg_set_idx, uint32_t reg_idx, DNBRegisterValue *reg_value );
    bool            SetRegisterValue ( uint32_t reg_set_idx, uint32_t reg_idx, const DNBRegisterValue *reg_value );
    nub_size_t      GetRegisterContext (void *buf, nub_size_t buf_len);
    nub_size_t      SetRegisterContext (const void *buf, nub_size_t buf_len);
    void            NotifyBreakpointChanged (const DNBBreakpoint *bp);

    bool            IsUserReady();
    struct thread_basic_info *
                    GetBasicInfo ();
    const char *    GetBasicInfoAsString () const;
    const char *    GetName ();
    
    DNBArchProtocol* 
    GetArchProtocol()
    {
        return m_arch_ap.get();
    }

protected:
    static bool     GetBasicInfo(thread_t threadID, struct thread_basic_info *basic_info);

    bool
    GetIdentifierInfo ();

//    const char *
//    GetDispatchQueueName();
//
    MachProcess *                   m_process;      // The process that owns this thread
    thread_t                        m_tid;          // The thread port for this thread
    uint32_t                        m_seq_id;       // A Sequential ID that increments with each new thread
    nub_state_t                     m_state;        // The state of our process
    PThreadMutex                    m_state_mutex;  // Multithreaded protection for m_state
    nub_break_t                     m_breakID;      // Breakpoint that this thread is (stopped)/was(running) at (NULL for none)
    struct thread_basic_info        m_basicInfo;    // Basic information for a thread used to see if a thread is valid
    uint32_t                        m_suspendCount; // The current suspend count
    MachException::Data             m_stop_exception; // The best exception that describes why this thread is stopped
    std::auto_ptr<DNBArchProtocol>  m_arch_ap;      // Arch specific information for register state and more
    const DNBRegisterSetInfo *const m_reg_sets;      // Register set information for this thread
    nub_size_t                      n_num_reg_sets;
#ifdef THREAD_IDENTIFIER_INFO_COUNT
    thread_identifier_info_data_t   m_ident_info;
    struct proc_threadinfo          m_proc_threadinfo;
    std::string                     m_dispatch_queue_name;
#endif

};

typedef std::tr1::shared_ptr<MachThread> MachThreadSP;

#endif
