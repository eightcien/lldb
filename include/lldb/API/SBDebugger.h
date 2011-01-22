//===-- SBDebugger.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBDebugger_h_
#define LLDB_SBDebugger_h_

#include "lldb/API/SBDefines.h"
#include <stdio.h>

namespace lldb {

class SBDebugger
{
public:

    static void
    Initialize();
    
    static void
    Terminate();
    
    static lldb::SBDebugger
    Create();

    static void
    Destroy (lldb::SBDebugger &debugger);

    SBDebugger();

    SBDebugger(const lldb::SBDebugger &rhs);

#ifndef SWIG
    lldb::SBDebugger &
    operator = (const lldb::SBDebugger &rhs);
#endif
    
    ~SBDebugger();

    bool
    IsValid() const;

    void
    Clear ();

    void
    SetAsync (bool b);

    void
    SkipLLDBInitFiles (bool b);

    void
    SetInputFileHandle (FILE *f, bool transfer_ownership);

    void
    SetOutputFileHandle (FILE *f, bool transfer_ownership);

    void
    SetErrorFileHandle (FILE *f, bool transfer_ownership);

    FILE *
    GetInputFileHandle ();

    FILE *
    GetOutputFileHandle ();

    FILE *
    GetErrorFileHandle ();

    lldb::SBCommandInterpreter
    GetCommandInterpreter ();

    void
    HandleCommand (const char *command);

    lldb::SBListener
    GetListener ();

    void
    HandleProcessEvent (const lldb::SBProcess &process,
                        const lldb::SBEvent &event,
                        FILE *out,
                        FILE *err);

    lldb::SBTarget
    CreateTargetWithFileAndTargetTriple (const char *filename,
                                         const char *target_triple);

    lldb::SBTarget
    CreateTargetWithFileAndArch (const char *filename,
                                 const char *archname);

    lldb::SBTarget
    CreateTarget (const char *filename);

    lldb::SBTarget
    GetTargetAtIndex (uint32_t idx);

    lldb::SBTarget
    FindTargetWithProcessID (pid_t pid);

    lldb::SBTarget
    FindTargetWithFileAndArch (const char *filename,
                               const char *arch);

    uint32_t
    GetNumTargets ();

    lldb::SBTarget
    GetSelectedTarget ();

    lldb::SBSourceManager &
    GetSourceManager ();
    
    // FIXME: Once we get the set show stuff in place, the driver won't need
    // an interface to the Set/Get UseExternalEditor.
    bool
    SetUseExternalEditor (bool input);
    
    bool 
    GetUseExternalEditor ();

    static bool
    GetDefaultArchitecture (char *arch_name, size_t arch_name_len);

    static bool
    SetDefaultArchitecture (const char *arch_name);

    lldb::ScriptLanguage
    GetScriptingLanguage (const char *script_language_name);

    static const char *
    GetVersionString ();

    static const char *
    StateAsCString (lldb::StateType state);

    static bool
    StateIsRunningState (lldb::StateType state);

    static bool
    StateIsStoppedState (lldb::StateType state);

    void
    DispatchInput (void *baton, const void *data, size_t data_len);

    void
    DispatchInputInterrupt ();

    void
    DispatchInputEndOfFile ();
    
    void
    PushInputReader (lldb::SBInputReader &reader);

    const char *
    GetInstanceName  ();

    static SBDebugger
    FindDebuggerWithID (int id);

    static lldb::SBError
    SetInternalVariable (const char *var_name, const char *value, const char *debugger_instance_name);

    static lldb::SBStringList
    GetInternalVariableValue (const char *var_name, const char *debugger_instance_name);

    bool
    GetDescription (lldb::SBStream &description);

    uint32_t
    GetTerminalWidth () const;

    void
    SetTerminalWidth (uint32_t term_width);

    lldb::user_id_t
    GetID ();
    
    const char *
    GetPrompt() const;

    void
    SetPrompt (const char *prompt);
        
    lldb::ScriptLanguage 
    GetScriptLanguage() const;

    void
    SetScriptLanguage (lldb::ScriptLanguage script_lang);

private:

#ifndef SWIG

    friend class SBInputReader;
    friend class SBProcess;
    friend class SBTarget;
    
    lldb::SBTarget
    FindTargetWithLLDBProcess (const lldb::ProcessSP &processSP);

    void
    reset (const lldb::DebuggerSP &debugger_sp);

    lldb_private::Debugger *
    get () const;

    lldb_private::Debugger &
    ref () const;

#endif
    
    lldb::DebuggerSP m_opaque_sp;

}; // class SBDebugger


} // namespace lldb

#endif // LLDB_SBDebugger_h_
