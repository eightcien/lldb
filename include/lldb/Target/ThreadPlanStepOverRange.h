//===-- ThreadPlanStepOverRange.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlanStepOverRange_h_
#define liblldb_ThreadPlanStepOverRange_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/AddressRange.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanStepRange.h"

namespace lldb_private {

class ThreadPlanStepOverRange : public ThreadPlanStepRange
{
public:
    virtual ~ThreadPlanStepOverRange ();

    virtual void GetDescription (Stream *s, lldb::DescriptionLevel level);
    virtual bool ShouldStop (Event *event_ptr);
    virtual bool
    IsMasterPlan()
    {
        return true;
    }

protected:

    ThreadPlanStepOverRange (Thread &thread, const AddressRange &range, const SymbolContext &addr_context, lldb::RunMode stop_others, bool okay_to_discard = false);

private:

    friend ThreadPlan *
    Thread::QueueThreadPlanForStepRange (bool abort_other_plans,
                                         lldb::StepType type,
                                         const AddressRange &range,
                                         const SymbolContext &addr_context,
                                         lldb::RunMode stop_others,
                                         bool avoid_code_without_debug_info);

    DISALLOW_COPY_AND_ASSIGN (ThreadPlanStepOverRange);

};

} // namespace lldb_private

#endif  // liblldb_ThreadPlanStepOverRange_h_
