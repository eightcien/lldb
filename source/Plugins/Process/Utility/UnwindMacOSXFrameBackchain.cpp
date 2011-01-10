//===-- UnwindMacOSXFrameBackchain.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/ArchSpec.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

#include "RegisterContextMacOSXFrameBackchain.h"

using namespace lldb;
using namespace lldb_private;

UnwindMacOSXFrameBackchain::UnwindMacOSXFrameBackchain (Thread &thread) :
    Unwind (thread),
    m_cursors()
{
}

uint32_t
UnwindMacOSXFrameBackchain::GetFrameCount()
{
    if (m_cursors.empty())
    {
        const ArchSpec target_arch (m_thread.GetProcess().GetTarget().GetArchitecture ());
        // Frame zero should always be supplied by the thread...
        StackFrameSP frame_sp (m_thread.GetStackFrameAtIndex (0));
        if (target_arch == ArchSpec("x86_64"))
            GetStackFrameData_x86_64 (frame_sp.get());
        else if (target_arch == ArchSpec("i386"))
            GetStackFrameData_i386 (frame_sp.get());

    }
    return m_cursors.size();
}

bool
UnwindMacOSXFrameBackchain::GetFrameInfoAtIndex (uint32_t idx, addr_t& cfa, addr_t& pc)
{
    const uint32_t frame_count = GetFrameCount();
    if (idx < frame_count)
    {
        if (m_cursors[idx].pc == LLDB_INVALID_ADDRESS)
            return false;
        if (m_cursors[idx].fp == LLDB_INVALID_ADDRESS)
            return false;

        pc = m_cursors[idx].pc;
        cfa = m_cursors[idx].fp;

        return true;
    }
    return false;
}
    
lldb::RegisterContextSP
UnwindMacOSXFrameBackchain::CreateRegisterContextForFrame (StackFrame *frame)
{
    lldb::RegisterContextSP reg_ctx_sp;
    uint32_t concrete_idx = frame->GetConcreteFrameIndex ();
    const uint32_t frame_count = GetFrameCount();
    if (concrete_idx < frame_count)
        reg_ctx_sp.reset (new RegisterContextMacOSXFrameBackchain (m_thread, concrete_idx, m_cursors[concrete_idx]));
    return reg_ctx_sp;
}

size_t
UnwindMacOSXFrameBackchain::GetStackFrameData_i386 (StackFrame *first_frame)
{
    m_cursors.clear();

    std::pair<lldb::addr_t, lldb::addr_t> fp_pc_pair;

    struct Frame_i386
    {
        uint32_t fp;
        uint32_t pc;
    };

    RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
    assert (reg_ctx);

    Cursor cursor;
    cursor.pc = reg_ctx->GetPC (LLDB_INVALID_ADDRESS);
    cursor.fp = reg_ctx->GetFP (0);
    
    Frame_i386 frame = { cursor.fp, cursor.pc };

    m_cursors.push_back(cursor);

    const size_t k_frame_size = sizeof(frame);
    Error error;
    while (frame.fp != 0 && frame.pc != 0 && ((frame.fp & 7) == 0))
    {
        // Read both the FP and PC (8 bytes)
        if (m_thread.GetProcess().ReadMemory (frame.fp, &frame.fp, k_frame_size, error) != k_frame_size)
            break;
        if (frame.pc >= 0x1000)
        {
            cursor.pc = frame.pc;
            cursor.fp = frame.fp;
            m_cursors.push_back (cursor);
        }
    }
    if (!m_cursors.empty())
    {
        lldb::addr_t first_frame_pc = m_cursors.front().pc;
        if (first_frame_pc != LLDB_INVALID_ADDRESS)
        {
            const uint32_t resolve_scope = eSymbolContextModule |
                                           eSymbolContextCompUnit |
                                           eSymbolContextFunction |
                                           eSymbolContextSymbol;

            SymbolContext first_frame_sc (first_frame->GetSymbolContext(resolve_scope));
            const AddressRange *addr_range_ptr = NULL;
            if (first_frame_sc.function)
                addr_range_ptr = &first_frame_sc.function->GetAddressRange();
            else if (first_frame_sc.symbol)
                addr_range_ptr = first_frame_sc.symbol->GetAddressRangePtr();

            if (addr_range_ptr)
            {
                if (first_frame->GetFrameCodeAddress() == addr_range_ptr->GetBaseAddress())
                {
                    // We are at the first instruction, so we can recover the
                    // previous PC by dereferencing the SP
                    lldb::addr_t first_frame_sp = reg_ctx->GetSP (0);
                    // Read the real second frame return address into frame.pc
                    if (first_frame_sp && m_thread.GetProcess().ReadMemory (first_frame_sp, &frame.pc, sizeof(frame.pc), error) == sizeof(frame.pc))
                    {
                        cursor.fp = m_cursors.front().fp;
                        cursor.pc = frame.pc;           // Set the new second frame PC

                        // Insert the second frame
                        m_cursors.insert(m_cursors.begin()+1, cursor);
                        
                        m_cursors.front().fp = first_frame_sp;
                    }
                }
            }
        }
    }
//    uint32_t i=0;
//    printf("      PC                 FP\n");
//    printf("      ------------------ ------------------ \n");
//    for (i=0; i<m_cursors.size(); ++i)
//    {
//        printf("[%3u] 0x%16.16llx 0x%16.16llx\n", i, m_cursors[i].pc, m_cursors[i].fp);
//    }
    return m_cursors.size();
}


size_t
UnwindMacOSXFrameBackchain::GetStackFrameData_x86_64 (StackFrame *first_frame)
{
    m_cursors.clear();

    std::pair<lldb::addr_t, lldb::addr_t> fp_pc_pair;

    struct Frame_x86_64
    {
        uint64_t fp;
        uint64_t pc;
    };

    RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
    assert (reg_ctx);

    Cursor cursor;
    cursor.pc = reg_ctx->GetPC (LLDB_INVALID_ADDRESS);
    cursor.fp = reg_ctx->GetFP (0);
    
    Frame_x86_64 frame = { cursor.fp, cursor.pc };

    m_cursors.push_back(cursor);
    Error error;
    const size_t k_frame_size = sizeof(frame);
    while (frame.fp != 0 && frame.pc != 0 && ((frame.fp & 7) == 0))
    {
        // Read both the FP and PC (16 bytes)
        if (m_thread.GetProcess().ReadMemory (frame.fp, &frame.fp, k_frame_size, error) != k_frame_size)
            break;

        if (frame.pc >= 0x1000)
        {
            cursor.pc = frame.pc;
            cursor.fp = frame.fp;
            m_cursors.push_back (cursor);
        }
    }
    if (!m_cursors.empty())
    {
        lldb::addr_t first_frame_pc = m_cursors.front().pc;
        if (first_frame_pc != LLDB_INVALID_ADDRESS)
        {
            const uint32_t resolve_scope = eSymbolContextModule |
                                           eSymbolContextCompUnit |
                                           eSymbolContextFunction |
                                           eSymbolContextSymbol;

            SymbolContext first_frame_sc(first_frame->GetSymbolContext(resolve_scope));
            const AddressRange *addr_range_ptr = NULL;
            if (first_frame_sc.function)
                addr_range_ptr = &first_frame_sc.function->GetAddressRange();
            else if (first_frame_sc.symbol)
                addr_range_ptr = first_frame_sc.symbol->GetAddressRangePtr();

            if (addr_range_ptr)
            {
                if (first_frame->GetFrameCodeAddress() == addr_range_ptr->GetBaseAddress())
                {
                    // We are at the first instruction, so we can recover the
                    // previous PC by dereferencing the SP
                    lldb::addr_t first_frame_sp = reg_ctx->GetSP (0);
                    // Read the real second frame return address into frame.pc
                    if (m_thread.GetProcess().ReadMemory (first_frame_sp, &frame.pc, sizeof(frame.pc), error) == sizeof(frame.pc))
                    {
                        cursor.fp = m_cursors.front().fp;
                        cursor.pc = frame.pc;           // Set the new second frame PC

                        // Insert the second frame
                        m_cursors.insert(m_cursors.begin()+1, cursor);
                        
                        m_cursors.front().fp = first_frame_sp;
                    }
                }
            }
        }
    }
    return m_cursors.size();
}

