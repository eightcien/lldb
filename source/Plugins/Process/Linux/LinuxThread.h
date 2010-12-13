//===-- LinuxThread.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LinuxThread_H_
#define liblldb_LinuxThread_H_

// C Includes
// C++ Includes
#include <memory>

// Other libraries and framework includes
#include "lldb/Target/Thread.h"
#include "RegisterContextLinux.h"

class ProcessMonitor;

//------------------------------------------------------------------------------
// @class LinuxThread
// @brief Abstraction of a linux process (thread).
class LinuxThread
    : public lldb_private::Thread
{
public:
    LinuxThread(lldb_private::Process &process, lldb::tid_t tid);

    void
    RefreshStateAfterStop();

    bool
    WillResume(lldb::StateType resume_state);

    const char *
    GetInfo();

    uint32_t
    GetStackFrameCount();

    lldb::StackFrameSP
    GetStackFrameAtIndex(uint32_t idx);

    RegisterContextLinux *
    GetRegisterContext();

    bool
    SaveFrameZeroState(RegisterCheckpoint &checkpoint);

    bool
    RestoreSaveFrameZero(const RegisterCheckpoint &checkpoint);

    RegisterContextLinux *
    CreateRegisterContextForFrame(lldb_private::StackFrame *frame);

    bool
    GetRawStopReason(StopInfo *stop_info);

    //--------------------------------------------------------------------------
    // These methods form a specialized interface to linux threads.
    //
    bool Resume();

    void BreakNotify();
    void TraceNotify();
    void ExitNotify();

private:
    std::auto_ptr<lldb_private::StackFrame> m_frame_ap;
    std::auto_ptr<RegisterContextLinux> m_register_ap;

    lldb::BreakpointSiteSP m_breakpoint;

    enum Notification {
        eNone,
        eBreak,
        eTrace,
        eExit
    };

    Notification m_note;

    ProcessMonitor &GetMonitor();
};

#endif // #ifndef liblldb_LinuxThread_H_
