//===-- UnwindMacOSXFrameBackchain.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_UnwindMacOSXFrameBackchain_h_
#define lldb_UnwindMacOSXFrameBackchain_h_

// C Includes
// C++ Includes
#include <vector>

// Other libraries and framework includes

// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Target/Unwind.h"

class UnwindMacOSXFrameBackchain : public lldb_private::Unwind
{
public: 
    UnwindMacOSXFrameBackchain (lldb_private::Thread &thread);
    
    virtual
    ~UnwindMacOSXFrameBackchain()
    {
    }
    
    virtual void
    Clear()
    {
        m_cursors.clear();
    }

    virtual uint32_t
    GetFrameCount();

    bool
    GetFrameInfoAtIndex (uint32_t frame_idx,
                         lldb::addr_t& cfa, 
                         lldb::addr_t& pc);
    
    lldb_private::RegisterContext *
    CreateRegisterContextForFrame (lldb_private::StackFrame *frame);

    lldb_private::Thread &
    GetThread();

protected:
    friend class RegisterContextMacOSXFrameBackchain;

    struct Cursor
    {
        lldb::addr_t pc;    // Program counter
        lldb::addr_t fp;    // Frame pointer for us with backchain
    };

private:
    std::vector<Cursor> m_cursors;

    size_t
    GetStackFrameData_i386 (lldb_private::StackFrame *first_frame);

    size_t
    GetStackFrameData_x86_64 (lldb_private::StackFrame *first_frame);

    //------------------------------------------------------------------
    // For UnwindMacOSXFrameBackchain only
    //------------------------------------------------------------------
    DISALLOW_COPY_AND_ASSIGN (UnwindMacOSXFrameBackchain);
};

#endif  // lldb_UnwindMacOSXFrameBackchain_h_
