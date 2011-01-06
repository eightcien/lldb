//===-- ThreadPlan.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlan.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/State.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// ThreadPlan constructor
//----------------------------------------------------------------------
ThreadPlan::ThreadPlan(ThreadPlanKind kind, const char *name, Thread &thread, Vote stop_vote, Vote run_vote) :
    m_thread (thread),
    m_stop_vote (stop_vote),
    m_run_vote (run_vote),
    m_kind (kind),
    m_name (name),
    m_plan_complete_mutex (Mutex::eMutexTypeRecursive),
    m_plan_complete (false),
    m_plan_private (false),
    m_okay_to_discard (false)
{
    SetID (GetNextID());
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ThreadPlan::~ThreadPlan()
{
}

const char *
ThreadPlan::GetName () const
{
    return m_name.c_str();
}

Thread &
ThreadPlan::GetThread()
{
    return m_thread;
}


const Thread &
ThreadPlan::GetThread() const
{
    return m_thread;
}

bool
ThreadPlan::IsPlanComplete ()
{
    Mutex::Locker locker(m_plan_complete_mutex);
    return m_plan_complete;
}

void
ThreadPlan::SetPlanComplete ()
{
    Mutex::Locker locker(m_plan_complete_mutex);
    m_plan_complete = true;
}

bool
ThreadPlan::MischiefManaged ()
{
    Mutex::Locker locker(m_plan_complete_mutex);
    m_plan_complete = true;
    return true;
}

Vote
ThreadPlan::ShouldReportStop (Event *event_ptr)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

    if (m_stop_vote == eVoteNoOpinion)
    {
        ThreadPlan *prev_plan = GetPreviousPlan ();
        if (prev_plan)
        {
            Vote prev_vote = prev_plan->ShouldReportStop (event_ptr);
            if (log)
                log->Printf ("ThreadPlan::ShouldReportStop() returning previous thread plan vote: %s", GetVoteAsCString (prev_vote));
            return prev_vote;
        }
    }
    if (log)
        log->Printf ("ThreadPlan::ShouldReportStop() returning vote: %s", GetVoteAsCString (m_stop_vote));
    return m_stop_vote;
}

Vote
ThreadPlan::ShouldReportRun (Event *event_ptr)
{
    if (m_run_vote == eVoteNoOpinion)
    {
        ThreadPlan *prev_plan = GetPreviousPlan ();
        if (prev_plan)
            return prev_plan->ShouldReportRun (event_ptr);
    }
    return m_run_vote;
}

bool
ThreadPlan::StopOthers ()
{
    ThreadPlan *prev_plan;
    prev_plan = GetPreviousPlan ();
    if (prev_plan == NULL)
        return false;
    else
        return prev_plan->StopOthers();
}

void
ThreadPlan::SetStopOthers (bool new_value)
{
	// SetStopOthers doesn't work up the hierarchy.  You have to set the 
    // explicit ThreadPlan you want to affect.
}

bool
ThreadPlan::WillResume (StateType resume_state, bool current_plan)
{
    if (current_plan)
    {
        LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

        if (log)
        {
            RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
            addr_t pc = reg_ctx->GetPC();
            addr_t sp = reg_ctx->GetSP();
            addr_t fp = reg_ctx->GetFP();
            log->Printf("%s Thread #%u: tid = 0x%4.4x, pc = 0x%8.8llx, sp = 0x%8.8llx, fp = 0x%8.8llx, plan = '%s', state = %s, stop others = %d", 
                        __FUNCTION__,
                        m_thread.GetIndexID(), 
                        m_thread.GetID(),  
                        (uint64_t)pc,
                        (uint64_t)sp,
                        (uint64_t)fp,
                        m_name.c_str(), 
                        StateAsCString(resume_state), 
                        StopOthers());
        }
    }
    return true;
}

lldb::user_id_t
ThreadPlan::GetNextID()
{
    static uint32_t g_nextPlanID = 0;
    return ++g_nextPlanID;
}

void
ThreadPlan::DidPush()
{
}

void
ThreadPlan::WillPop()
{
}

void
ThreadPlan::PushPlan (ThreadPlanSP &thread_plan_sp)
{
    m_thread.PushPlan (thread_plan_sp);
}

ThreadPlan *
ThreadPlan::GetPreviousPlan ()
{
    return m_thread.GetPreviousPlan (this);
}

void
ThreadPlan::SetPrivate (bool input)
{
    m_plan_private = input;
}

bool
ThreadPlan::GetPrivate (void)
{
    return m_plan_private;
}

bool
ThreadPlan::OkayToDiscard()
{
    if (!IsMasterPlan())
        return true;
    else
        return m_okay_to_discard;
}

lldb::StateType
ThreadPlan::RunState ()
{
    if (m_tracer_sp && m_tracer_sp->TracingEnabled() && m_tracer_sp->SingleStepEnabled())
        return eStateStepping;
    else
        return GetPlanRunState();
}
