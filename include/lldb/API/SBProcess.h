//===-- SBProcess.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBProcess_h_
#define LLDB_SBProcess_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBTarget.h"
#include <stdio.h>

namespace lldb {

class SBEvent;

class SBProcess
{
public:
    //------------------------------------------------------------------
    /// Broadcaster event bits definitions.
    //------------------------------------------------------------------
    enum
    {
        eBroadcastBitStateChanged   = (1 << 0),
        eBroadcastBitInterrupt      = (1 << 1),
        eBroadcastBitSTDOUT         = (1 << 2),
        eBroadcastBitSTDERR         = (1 << 3)
    };

    SBProcess ();

    SBProcess (const lldb::SBProcess& rhs);

#ifndef SWIG
    const lldb::SBProcess&
    operator = (const lldb::SBProcess& rhs);
#endif

    ~SBProcess();

    void
    Clear ();

    bool
    IsValid() const;

    lldb::SBTarget
    GetTarget() const;

    size_t
    PutSTDIN (const char *src, size_t src_len);

    size_t
    GetSTDOUT (char *dst, size_t dst_len) const;

    size_t
    GetSTDERR (char *dst, size_t dst_len) const;

    void
    ReportEventState (const lldb::SBEvent &event, FILE *out) const;

    void
    AppendEventStateReport (const lldb::SBEvent &event, lldb::SBCommandReturnObject &result);

    //------------------------------------------------------------------
    // Thread related functions
    //------------------------------------------------------------------
    uint32_t
    GetNumThreads ();

    lldb::SBThread
    GetThreadAtIndex (size_t index);

    lldb::SBThread
    GetThreadByID (lldb::tid_t sb_thread_id);

    lldb::SBThread
    GetSelectedThread () const;

    bool
    SetSelectedThread (const lldb::SBThread &thread);

    bool
    SetSelectedThreadByID (uint32_t tid);

    //------------------------------------------------------------------
    // Stepping related functions
    //------------------------------------------------------------------

    lldb::StateType
    GetState ();

    int
    GetExitStatus ();

    const char *
    GetExitDescription ();

    lldb::pid_t
    GetProcessID ();

    uint32_t
    GetAddressByteSize() const;

    lldb::SBError
    Destroy ();

    lldb::pid_t
    AttachByPID (lldb::pid_t pid);  // DEPRECATED

    // DEPRECATED: relocated to "SBProcess SBTarget::AttachToProcess (lldb::pid_t pid, lldb::SBError& error)"
    lldb::SBError
    Attach (lldb::pid_t pid);

    // DEPRECATED: relocated to "SBProcess SBTarget::AttachToProcess (const char *name, bool wait_for_launch, lldb::SBError& error)"
    lldb::SBError
    AttachByName (const char *name, bool wait_for_launch);

    lldb::SBError
    Continue ();

    lldb::SBError
    Stop ();

    lldb::SBError
    Kill ();

    lldb::SBError
    Detach ();

    lldb::SBError
    Signal (int signal);

    size_t
    ReadMemory (addr_t addr, void *buf, size_t size, lldb::SBError &error);

    size_t
    WriteMemory (addr_t addr, const void *buf, size_t size, lldb::SBError &error);

    // Events
    static lldb::StateType
    GetStateFromEvent (const lldb::SBEvent &event);

    static bool
    GetRestartedFromEvent (const lldb::SBEvent &event);

    static lldb::SBProcess
    GetProcessFromEvent (const lldb::SBEvent &event);

    lldb::SBBroadcaster
    GetBroadcaster () const;

    bool
    GetDescription (lldb::SBStream &description);

    uint32_t
    LoadImage (lldb::SBFileSpec &image_spec, lldb::SBError &error);
    
    lldb::SBError
    UnloadImage (uint32_t image_token);

protected:
    friend class SBAddress;
    friend class SBBreakpoint;
    friend class SBBreakpointLocation;
    friend class SBCommandInterpreter;
    friend class SBDebugger;
    friend class SBFunction;
    friend class SBTarget;
    friend class SBThread;
    friend class SBValue;

#ifndef SWIG

    lldb_private::Process *
    operator->() const;

    // Mimic shared pointer...
    lldb_private::Process *
    get() const;

#endif


    SBProcess (const lldb::ProcessSP &process_sp);

    void
    SetProcess (const lldb::ProcessSP &process_sp);

    lldb::ProcessSP m_opaque_sp;
};

}  // namespace lldb

#endif  // LLDB_SBProcess_h_
