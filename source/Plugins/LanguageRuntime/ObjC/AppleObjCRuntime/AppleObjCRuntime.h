//===-- AppleObjCRuntime.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AppleObjCRuntime_h_
#define liblldb_AppleObjCRuntime_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Core/ValueObject.h"
#include "AppleObjCTrampolineHandler.h"
#include "AppleThreadPlanStepThroughObjCTrampoline.h"

namespace lldb_private {
    
class AppleObjCRuntime :
        public lldb_private::ObjCLanguageRuntime
{
public:
    
    ~AppleObjCRuntime() { }
    
    // These are generic runtime functions:
    virtual bool
    GetObjectDescription (Stream &str, Value &value, ExecutionContextScope *exe_scope);
    
    virtual bool
    GetObjectDescription (Stream &str, ValueObject &object, ExecutionContextScope *exe_scope);
    
    virtual lldb::ValueObjectSP
    GetDynamicValue (lldb::ValueObjectSP in_value, ExecutionContextScope *exe_scope);

    // These are the ObjC specific functions.
    
    virtual bool
    IsModuleObjCLibrary (const lldb::ModuleSP &module_sp);
    
    virtual bool
    ReadObjCLibrary (const lldb::ModuleSP &module_sp);

    virtual bool
    HasReadObjCLibrary ()
    {
        return m_read_objc_library;
    }
    
    virtual lldb::ThreadPlanSP
    GetStepThroughTrampolinePlan (Thread &thread, bool stop_others);
    
    //------------------------------------------------------------------
    // Static Functions
    //------------------------------------------------------------------
    // Note there is no CreateInstance, Initialize & Terminate functions here, because
    // you can't make an instance of this generic runtime.
    
protected:
    static bool
    AppleIsModuleObjCLibrary (const lldb::ModuleSP &module_sp);

    static enum lldb::ObjCRuntimeVersions
    GetObjCVersion (Process *process, ModuleSP &objc_module_sp);

    //------------------------------------------------------------------
    // PluginInterface protocol
    //------------------------------------------------------------------
public:
    virtual void
    GetPluginCommandHelp (const char *command, lldb_private::Stream *strm);
    
    virtual lldb_private::Error
    ExecutePluginCommand (lldb_private::Args &command, lldb_private::Stream *strm);
    
    virtual lldb_private::Log *
    EnablePluginLogging (lldb_private::Stream *strm, lldb_private::Args &command);
    
    virtual void
    ClearExceptionBreakpoints ();
    
    virtual bool
    ExceptionBreakpointsExplainStop (lldb::StopInfoSP stop_reason);
protected:
    Address *
    GetPrintForDebuggerAddr();
    
    std::auto_ptr<Address>  m_PrintForDebugger_addr;
    bool m_read_objc_library;
    std::auto_ptr<lldb_private::AppleObjCTrampolineHandler> m_objc_trampoline_handler_ap;
    lldb::BreakpointSP m_objc_exception_bp_sp;

    AppleObjCRuntime(Process *process) : 
        lldb_private::ObjCLanguageRuntime(process),
        m_read_objc_library (false),
        m_objc_trampoline_handler_ap(NULL)
     { } // Call CreateInstance instead.
};
    
} // namespace lldb_private

#endif  // liblldb_AppleObjCRuntime_h_
