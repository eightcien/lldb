//===-- StoppointLocation.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StoppointLocation_h_
#define liblldb_StoppointLocation_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Core/UserID.h"
// #include "lldb/Breakpoint/BreakpointOptions.h"

namespace lldb_private {

class StoppointLocation
{
public:
    //------------------------------------------------------------------
    // Constructors and Destructors
    //------------------------------------------------------------------
    StoppointLocation (lldb::break_id_t bid,
                       lldb::addr_t m_addr,
                       bool hardware);

    StoppointLocation (lldb::break_id_t bid,
                       lldb::addr_t m_addr,
                       size_t size,
                       bool hardware);

    virtual
    ~StoppointLocation ();

    //------------------------------------------------------------------
    // Operators
    //------------------------------------------------------------------

    //------------------------------------------------------------------
    // Methods
    //------------------------------------------------------------------
    virtual lldb::addr_t
    GetLoadAddress () const;

    size_t
    GetByteSize () const;

    uint32_t
    GetHitCount () const;

    void
    IncrementHitCount ();

    uint32_t
    GetHardwareIndex () const;

    bool
    HardwarePreferred () const;

    bool
    IsHardware () const;

    virtual bool
    ShouldStop (StoppointCallbackContext *context);

    virtual void
    Dump (Stream *stream) const;

    void
    SetHardwareIndex (uint32_t index);

    lldb::break_id_t
    GetID () const;

protected:
    //------------------------------------------------------------------
    // Classes that inherit from StoppointLocation can see and modify these
    //------------------------------------------------------------------
    lldb::break_id_t  m_loc_id;     // Break ID
    lldb::addr_t      m_addr;       // The load address of this stop point. The base Stoppoint doesn't
                                    // store a full Address since that's not needed for the breakpoint sites.
    bool        m_hw_preferred;     // 1 if this point has been requested to be set using hardware (which may fail due to lack of resources)
    uint32_t    m_hw_index;         // The hardware resource index for this breakpoint/watchpoint
    uint32_t    m_byte_size;        // The size in bytes of stop location.  e.g. the length of the trap opcode for
                                    // software breakpoints, or the optional length in bytes for
                                    // hardware breakpoints, or the length of the watchpoint.
    uint32_t    m_hit_count;        // Number of times this breakpoint has been hit

private:
    //------------------------------------------------------------------
    // For StoppointLocation only
    //------------------------------------------------------------------
    DISALLOW_COPY_AND_ASSIGN(StoppointLocation);
    StoppointLocation(); // Disallow default constructor
};

} // namespace lldb_private

#endif  // liblldb_StoppointLocation_h_
