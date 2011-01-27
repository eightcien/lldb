//===-- ThreadPlan.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadPlan_h_
#define liblldb_ThreadPlan_h_

// C Includes
// C++ Includes
#include <string>
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Core/UserID.h"
#include "lldb/Host/Mutex.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanTracer.h"
#include "lldb/Target/StopInfo.h"

namespace lldb_private {

//------------------------------------------------------------------
//  ThreadPlan:
//  This is the pure virtual base class for thread plans.
//
//  The thread plans provide the "atoms" of behavior that
//  all the logical process control, either directly from commands or through
//  more complex composite plans will rely on.
//
//  Plan Stack:
//
//  The thread maintaining a thread plan stack, and you program the actions of a particular thread
//  by pushing plans onto the plan stack.
//  There is always a "Current" plan, which is the head of the plan stack, though in some cases
//  a plan may defer to plans higher in the stack for some piece of information.
//
//  The plan stack is never empty, there is always a Base Plan which persists through the life
//  of the running process.
//
//
//  Creating Plans:
//
//  The thread plan is generally created and added to the plan stack through the QueueThreadPlanFor... API
//  in lldb::Thread.  Those API's will return the plan that performs the named operation in a manner
//  appropriate for the current process.  The plans in lldb/source/Target are generic
//  implementations, but a Process plugin can override them.
//
//  ValidatePlan is then called.  If it returns false, the plan is unshipped.  This is a little
//  convenience which keeps us from having to error out of the constructor.
//
//  Then the plan is added to the plan stack.  When the plan is added to the plan stack its DidPush
//  will get called.  This is useful if a plan wants to push any additional plans as it is constructed,
//  since you need to make sure you're already on the stack before you push additional plans.
//
//  Completed Plans:
//
//  When the target process stops the plans are queried, among other things, for whether their job is done.
//  If it is they are moved from the plan stack to the Completed Plan stack in reverse order from their position
//  on the plan stack (since multiple plans may be done at a given stop.)  This is used primarily so that
//  the lldb::Thread::StopInfo for the thread can be set properly.  If one plan pushes another to achieve part of
//  its job, but it doesn't want that sub-plan to be the one that sets the StopInfo, then call SetPrivate on the
//  sub-plan when you create it, and the Thread will pass over that plan in reporting the reason for the stop.
//
//  Discarded plans:
//
//  Your plan may also get discarded, i.e. moved from the plan stack to the "discarded plan stack".  This can
//  happen, for instance, if the plan is calling a function and the function call crashes and you want
//  to unwind the attempt to call.  So don't assume that your plan will always successfully stop.  Which leads to:
//
//  Cleaning up after your plans:
//
//  When the plan is moved from the plan stack its WillPop method is always called, no matter why.  Once it is
//  moved off the plan stack it is done, and won't get a chance to run again.  So you should
//  undo anything that affects target state in this method.  But be sure to leave the plan able to correctly 
//  fill the StopInfo, however.
//  N.B. Don't wait to do clean up target state till the destructor, since that will usually get called when 
//  the target resumes, and you want to leave the target state correct for new plans in the time between when
//  your plan gets unshipped and the next resume.
//
//  Over the lifetime of the plan, various methods of the ThreadPlan are then called in response to changes of state in
//  the process we are debugging as follows:
//
//  Resuming:
//
//  When the target process is about to be restarted, the plan's WillResume method is called,
//  giving the plan a chance to prepare for the run.  If WillResume returns false, then the
//  process is not restarted.  Be sure to set an appropriate error value in the Process if
//  you have to do this.
//  Next the "StopOthers" method of all the threads are polled, and if one thread's Current plan
//  returns "true" then only that thread gets to run.  If more than one returns "true" the threads that want to run solo
//  get run one by one round robin fashion.  Otherwise all are let to run.
//
//  Note, the way StopOthers is implemented, the base class implementation just asks the previous plan.  So if your plan
//  has no opinion about whether it should run stopping others or not, just don't implement StopOthers, and the parent
//  will be asked.
//
//  Finally, for each thread that is running, it run state is set to the return of RunState from the
//  thread's Current plan.
//
//  Responding to a stop:
//
//  When the target process stops, the plan is called in the following stages:
//
//  First the thread asks the Current Plan if it can handle this stop by calling PlanExplainsStop.
//  If the Current plan answers "true" then it is asked if the stop should percolate all the way to the
//  user by calling the ShouldStop method.  If the current plan doesn't explain the stop, then we query down
//  the plan stack for a plan that does explain the stop.  The plan that does explain the stop then needs to
//  figure out what to do about the plans below it in the stack.  If the stop is recoverable, then the plan that
//  understands it can just do what it needs to set up to restart, and then continue.
//  Otherwise, the plan that understood the stop should call DiscardPlanStack to clean up the stack below it.
//  In the normal case, this will just collapse the plan stack up to the point of the plan that understood
//  the stop reason.  However, if a plan wishes to stay on the stack after an event it didn't directly handle
//  it can designate itself a "Master" plan by responding true to IsMasterPlan, and then if it wants not to be
//  discarded, it can return true to OkayToDiscard, and it and all its dependent plans will be preserved when
//  we resume execution.
//
//  Actually Stopping:
//
//  If a plan says responds "true" to ShouldStop, then it is asked if it's job is complete by calling
//  MischiefManaged.  If that returns true, the thread is popped from the plan stack and added to the
//  Completed Plan Stack.  Then the next plan in the stack is asked if it ShouldStop, and  it returns "true",
//  it is asked if it is done, and if yes popped, and so on till we reach a plan that is not done.
//
//  Since you often know in the ShouldStop method whether your plan is complete, as a convenience you can call
//  SetPlanComplete and the ThreadPlan implementation of MischiefManaged will return "true", without your having
//  to redo the calculation when your sub-classes MischiefManaged is called.  If you call SetPlanComplete, you can
//  later use IsPlanComplete to determine whether the plan is complete.  This is only a convenience for sub-classes,
//  the logic in lldb::Thread will only call MischiefManaged.
//
//  One slightly tricky point is you have to be careful using SetPlanComplete in PlanExplainsStop because you
//  are not guaranteed that PlanExplainsStop for a plan will get called before ShouldStop gets called.  If your sub-plan
//  explained the stop and then popped itself, only your ShouldStop will get called.
//
//  If ShouldStop for any thread returns "true", then the WillStop method of the Current plan of
//  all threads will be called, the stop event is placed on the Process's public broadcaster, and
//  control returns to the upper layers of the debugger.
//
//  Automatically Resuming:
//
//  If ShouldStop for all threads returns "false", then the target process will resume.  This then cycles back to
//  Resuming above.
//
//  Reporting eStateStopped events when the target is restarted:
//
//  If a plan decides to auto-continue the target by returning "false" from ShouldStop, then it will be asked
//  whether the Stopped event should still be reported.  For instance, if you hit a breakpoint that is a User set
//  breakpoint, but the breakpoint callback said to continue the target process, you might still want to inform
//  the upper layers of lldb that the stop had happened.
//  The way this works is every thread gets to vote on whether to report the stop.  If all votes are eVoteNoOpinion,
//  then the thread list will decide what to do (at present it will pretty much always suppress these stopped events.)
//  If there is an eVoteYes, then the event will be reported regardless of the other votes.  If there is an eVoteNo
//  and no eVoteYes's, then the event won't be reported.
//
//  One other little detail here, sometimes a plan will push another plan onto the plan stack to do some part of
//  the first plan's job, and it would be convenient to tell that plan how it should respond to ShouldReportStop.
//  You can do that by setting the stop_vote in the child plan when you create it.
//
//  Suppressing the initial eStateRunning event:
//
//  The private process running thread will take care of ensuring that only one "eStateRunning" event will be
//  delivered to the public Process broadcaster per public eStateStopped event.  However there are some cases
//  where the public state of this process is eStateStopped, but a thread plan needs to restart the target, but
//  doesn't want the running event to be publically broadcast.  The obvious example of this is running functions
//  by hand as part of expression evaluation.  To suppress the running event return eVoteNo from ShouldReportStop,
//  to force a running event to be reported return eVoteYes, in general though you should return eVoteNoOpinion
//  which will allow the ThreadList to figure out the right thing to do.
//  The run_vote argument to the constructor works like stop_vote, and is a way for a plan to instruct a sub-plan
//  on how to respond to ShouldReportStop.
//
//------------------------------------------------------------------

class ThreadPlan :
    public UserID
{
public:
    typedef enum
    {
        eAllThreads,
        eSomeThreads,
        eThisThread
    } ThreadScope;

    // We use these enums so that we can cast a base thread plan to it's real type without having to resort
    // to dynamic casting.
    typedef enum
    {
        eKindGeneric,
        eKindBase,
        eKindCallFunction,
        eKindStepInstruction,
        eKindStepOut,
        eKindStepOverBreakpoint,
        eKindStepOverRange,
        eKindStepInRange,
        eKindRunToAddress,
        eKindStepThrough,
        eKindStepUntil,
        eKindTestCondition
        
    } ThreadPlanKind;
    
    //------------------------------------------------------------------
    // Constructors and Destructors
    //------------------------------------------------------------------
    ThreadPlan (ThreadPlanKind kind,
                const char *name,
                Thread &thread,
                lldb::Vote stop_vote,
                lldb::Vote run_vote);

    virtual
    ~ThreadPlan();

    //------------------------------------------------------------------
    /// Returns the name of this thread plan.
    ///
    /// @return
    ///   A const char * pointer to the thread plan's name.
    //------------------------------------------------------------------
    const char *
    GetName () const;

    //------------------------------------------------------------------
    /// Returns the Thread that is using this thread plan.
    ///
    /// @return
    ///   A  pointer to the thread plan's owning thread.
    //------------------------------------------------------------------
    Thread &
    GetThread();

    const Thread &
    GetThread() const;

    //------------------------------------------------------------------
    /// Print a description of this thread to the stream \a s.
    /// \a thread.
    ///
    /// @param[in] s
    ///    The stream to which to print the description.
    ///
    /// @param[in] level
    ///    The level of description desired.  Note that eDescriptionLevelBrief
    ///    will be used in the stop message printed when the plan is complete.
    //------------------------------------------------------------------
    virtual void
    GetDescription (Stream *s,
                    lldb::DescriptionLevel level) = 0;

    //------------------------------------------------------------------
    /// Returns whether this plan could be successfully created.
    ///
    /// @param[in] error
    ///    A stream to which to print some reason why the plan could not be created.
    ///    Can be NULL.
    ///
    /// @return
    ///   \b true if the plan should be queued, \b false otherwise.
    //------------------------------------------------------------------
    virtual bool
    ValidatePlan (Stream *error) = 0;

    virtual bool
    PlanExplainsStop () = 0;
    
    bool
    TracerExplainsStop ()
    {
        if (!m_tracer_sp)
            return false;
        else
            return m_tracer_sp->TracerExplainsStop();
    }


    lldb::StateType
    RunState ();

    virtual bool
    ShouldStop (Event *event_ptr) = 0;
    
    virtual bool
    ShouldAutoContinue (Event *event_ptr)
    {
        return false;
    }

    // Whether a "stop class" event should be reported to the "outside world".  In general
    // if a thread plan is active, events should not be reported.

    virtual lldb::Vote
    ShouldReportStop (Event *event_ptr);

    virtual lldb::Vote
    ShouldReportRun (Event *event_ptr);

    virtual void
    SetStopOthers (bool new_value);
    
    virtual bool
    StopOthers ();

    virtual bool
    WillResume (lldb::StateType resume_state, bool current_plan);

    virtual bool
    WillStop () = 0;

    virtual bool
    IsMasterPlan()
    {
        return false;
    }

    virtual bool
    OkayToDiscard();

    void
    SetOkayToDiscard (bool value)
    {
        m_okay_to_discard = value;
    }
    
    // The base class MischiefManaged does some cleanup - so you have to call it
    // in your MischiefManaged derived class.
    virtual bool
    MischiefManaged ();

    bool
    GetPrivate ();

    void
    SetPrivate (bool input);

    virtual void
    DidPush();

    virtual void
    WillPop();

    // This pushes \a plan onto the plan stack of the current plan's thread.
    void
    PushPlan (lldb::ThreadPlanSP &thread_plan_sp);
    
    ThreadPlanKind GetKind() const
    {
        return m_kind;
    }
    
    bool
    IsPlanComplete();
    
    void
    SetPlanComplete ();
    
    lldb::ThreadPlanTracerSP &
    GetThreadPlanTracer()
    {
        return m_tracer_sp;
    }
    
    void
    SetThreadPlanTracer (lldb::ThreadPlanTracerSP new_tracer_sp)
    {
        m_tracer_sp = new_tracer_sp;
    }
    
    void
    DoTraceLog ()
    {
        if (m_tracer_sp && m_tracer_sp->TracingEnabled())
            m_tracer_sp->Log();
    }

protected:
    //------------------------------------------------------------------
    // Classes that inherit from ThreadPlan can see and modify these
    //------------------------------------------------------------------

    // This gets the previous plan to the current plan (for forwarding requests).
    // This is mostly a formal requirement, it allows us to make the Thread's
    // GetPreviousPlan protected, but only friend ThreadPlan to thread.

    ThreadPlan *
    GetPreviousPlan ();
    
    // This forwards the private Thread::GetPrivateStopReason which is generally what
    // ThreadPlan's need to know.
    
    lldb::StopInfoSP 
    GetPrivateStopReason()
    {
        return m_thread.GetPrivateStopReason();
    }
    
    void
    SetStopInfo (lldb::StopInfoSP stop_reason_sp)
    {
        m_thread.SetStopInfo (stop_reason_sp);
    }
    
    virtual lldb::StateType
    GetPlanRunState () = 0;


    Thread &m_thread;
    lldb::Vote m_stop_vote;
    lldb::Vote m_run_vote;

private:
    //------------------------------------------------------------------
    // For ThreadPlan only
    //------------------------------------------------------------------
    static lldb::user_id_t GetNextID ();

    ThreadPlanKind m_kind;
    std::string m_name;
    Mutex m_plan_complete_mutex;
    bool m_plan_complete;
    bool m_plan_private;
    bool m_okay_to_discard;
    
    lldb::ThreadPlanTracerSP m_tracer_sp;

private:
    DISALLOW_COPY_AND_ASSIGN(ThreadPlan);
};


} // namespace lldb_private

#endif  // liblldb_ThreadPlan_h_
