//===-- State.cpp -----------------------------------------------*- C++ -*-===//
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
#include "lldb/Core/State.h"
#include <stdio.h>

using namespace lldb;
using namespace lldb_private;

const char *
lldb_private::StateAsCString (StateType state)
{
    switch (state)
    {
    case eStateInvalid:     return "invalid";
    case eStateUnloaded:    return "unloaded";
    case eStateAttaching:   return "attaching";
    case eStateLaunching:   return "launching";
    case eStateStopped:     return "stopped";
    case eStateRunning:     return "running";
    case eStateStepping:    return "stepping";
    case eStateCrashed:     return "crashed";
    case eStateDetached:    return "detached";
    case eStateExited:      return "exited";
    case eStateSuspended:   return "suspended";
    }
    static char unknown_state_string[64];
    snprintf(unknown_state_string, sizeof (unknown_state_string), "StateType = %i", state);
    return unknown_state_string;
}

bool
lldb_private::StateIsRunningState (StateType state)
{
    switch (state)
    {
    case eStateAttaching:
    case eStateLaunching:
    case eStateRunning:
    case eStateStepping:
        return true;

    case eStateDetached:
    case eStateInvalid:
    case eStateUnloaded:
    case eStateStopped:
    case eStateCrashed:
    case eStateExited:
    case eStateSuspended:
    default:
        break;
    }
    return false;
}

bool
lldb_private::StateIsStoppedState (StateType state)
{
    switch (state)
    {
    case eStateInvalid:
    case eStateAttaching:
    case eStateLaunching:
    case eStateRunning:
    case eStateStepping:
    case eStateDetached:
    default:
        break;

    case eStateUnloaded:
    case eStateStopped:
    case eStateCrashed:
    case eStateExited:
    case eStateSuspended:
        return true;
    }
    return false;
}
