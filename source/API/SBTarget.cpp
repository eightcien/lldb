//===-- SBTarget.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTarget.h"

#include "lldb/lldb-include.h"

#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBStream.h"
#include "lldb/Breakpoint/BreakpointID.h"
#include "lldb/Breakpoint/BreakpointIDList.h"
#include "lldb/Breakpoint/BreakpointList.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/AddressResolver.h"
#include "lldb/Core/AddressResolverName.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/FileSpec.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/STLUtils.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"

#include "lldb/Interpreter/CommandReturnObject.h"
#include "../source/Commands/CommandObjectBreakpoint.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBBreakpoint.h"

using namespace lldb;
using namespace lldb_private;

#define DEFAULT_DISASM_BYTE_SIZE 32

//----------------------------------------------------------------------
// SBTarget constructor
//----------------------------------------------------------------------
SBTarget::SBTarget () :
    m_opaque_sp ()
{
}

SBTarget::SBTarget (const SBTarget& rhs) :
    m_opaque_sp (rhs.m_opaque_sp)
{
}

SBTarget::SBTarget(const TargetSP& target_sp) :
    m_opaque_sp (target_sp)
{
}

const SBTarget&
SBTarget::operator = (const SBTarget& rhs)
{
    if (this != &rhs)
        m_opaque_sp = rhs.m_opaque_sp;
    return *this;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
SBTarget::~SBTarget()
{
}

bool
SBTarget::IsValid () const
{
    return m_opaque_sp.get() != NULL;
}

SBProcess
SBTarget::GetProcess ()
{

    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        sb_process.SetProcess (m_opaque_sp->GetProcessSP());
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        log->Printf ("SBTarget(%p)::GetProcess () => SBProcess(%p)", 
                     m_opaque_sp.get(), sb_process.get());
    }

    return sb_process;
}

SBDebugger
SBTarget::GetDebugger () const
{
    SBDebugger debugger;
    if (m_opaque_sp)
        debugger.reset (m_opaque_sp->GetDebugger().GetSP());
    return debugger;
}


SBProcess
SBTarget::Launch 
(
    SBListener &listener, 
    char const **argv,
    char const **envp,
    const char *stdin_path,
    const char *stdout_path,
    const char *stderr_path,
    const char *working_directory,
    uint32_t launch_flags,   // See LaunchFlags
    bool stop_at_entry,
    lldb::SBError& error
)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
    {
        log->Printf ("SBTarget(%p)::Launch (argv=%p, envp=%p, stdin=%s, stdout=%s, stderr=%s, working-dir=%s, launch_flags=0x%x, stop_at_entry=%i, &error (%p))...",
                     m_opaque_sp.get(), 
                     argv, 
                     envp, 
                     stdin_path ? stdin_path : "NULL", 
                     stdout_path ? stdout_path : "NULL", 
                     stderr_path ? stderr_path : "NULL", 
                     working_directory ? working_directory : "NULL",
                     launch_flags, 
                     stop_at_entry, 
                     error.get());
    }
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());

        if (getenv("LLDB_LAUNCH_FLAG_DISABLE_ASLR"))
            launch_flags |= eLaunchFlagDisableASLR;

        static const char *g_launch_tty = NULL;
        static bool g_got_launch_tty = false;
        if (!g_got_launch_tty)
        {
            // Get the LLDB_LAUNCH_FLAG_LAUNCH_IN_TTY only once
            g_got_launch_tty = true;
            g_launch_tty = ::getenv ("LLDB_LAUNCH_FLAG_LAUNCH_IN_TTY");
            if (g_launch_tty)
            {
                // LLDB_LAUNCH_FLAG_LAUNCH_IN_TTY is a path to a terminal to reuse
                // if the first character is '/', else it is a boolean value.
                if (g_launch_tty[0] != '/')
                {
                    if (Args::StringToBoolean(g_launch_tty, false, NULL))
                        g_launch_tty = "";
                    else
                        g_launch_tty = NULL;
                }
            }
        }
        
        if ((launch_flags & eLaunchFlagLaunchInTTY) || g_launch_tty)
        {
            ArchSpec arch (m_opaque_sp->GetArchitecture ());
            
            Module *exe_module = m_opaque_sp->GetExecutableModule().get();
            if (exe_module)
            {
                char exec_file_path[PATH_MAX];
                exe_module->GetFileSpec().GetPath(exec_file_path, sizeof(exec_file_path));
                if (exe_module->GetFileSpec().Exists())
                {
                    // Make a new argument vector
                    std::vector<const char *> exec_path_plus_argv;
                    // Append the resolved executable path
                    exec_path_plus_argv.push_back (exec_file_path);
                        
                    // Push all args if there are any
                    if (argv)
                    {
                        for (int i = 0; argv[i]; ++i)
                            exec_path_plus_argv.push_back(argv[i]);
                    }
                        
                    // Push a NULL to terminate the args.
                    exec_path_plus_argv.push_back(NULL);
                        

                    const char *tty_name = NULL;
                    if (g_launch_tty && g_launch_tty[0] == '/')
                        tty_name = g_launch_tty;
                    
                    lldb::pid_t pid = Host::LaunchInNewTerminal (tty_name,
                                                                 &exec_path_plus_argv[0],
                                                                 envp,
                                                                 working_directory,
                                                                 &arch,
                                                                 true,
                                                                 launch_flags & eLaunchFlagDisableASLR);

                    if (pid != LLDB_INVALID_PROCESS_ID)
                    {
                        sb_process = AttachToProcessWithID(listener, pid, error);
                    }
                    else
                    {
                        error.SetErrorStringWithFormat("failed to launch process in terminal");
                    }
                }
                else
                {
                    error.SetErrorStringWithFormat("executable doesn't exist: \"%s\"", exec_file_path);
                }
            }            
            else
            {
                error.SetErrorStringWithFormat("invalid executable");
            }
        }
        else
        {
            if (listener.IsValid())
                sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
            else
                sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));

            if (sb_process.IsValid())
            {

                if (getenv("LLDB_LAUNCH_FLAG_DISABLE_STDIO"))
                    launch_flags |= eLaunchFlagDisableSTDIO;
                

                error.SetError (sb_process->Launch (argv, envp, launch_flags, stdin_path, stdout_path, stderr_path, working_directory));
                if (error.Success())
                {
                    // We we are stopping at the entry point, we can return now!
                    if (stop_at_entry)
                        return sb_process;
                    
                    // Make sure we are stopped at the entry
                    StateType state = sb_process->WaitForProcessToStop (NULL);
                    if (state == eStateStopped)
                    {
                        // resume the process to skip the entry point
                        error.SetError (sb_process->Resume());
                        if (error.Success())
                        {
                            // If we are doing synchronous mode, then wait for the
                            // process to stop yet again!
                            if (m_opaque_sp->GetDebugger().GetAsyncExecution () == false)
                                sb_process->WaitForProcessToStop (NULL);
                        }
                    }
                }
            }
            else
            {
                error.SetErrorString ("unable to create lldb_private::Process");
            }
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }

    log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API);
    if (log)
    {
        log->Printf ("SBTarget(%p)::Launch (...) => SBProceess(%p)", 
                     m_opaque_sp.get(), sb_process.get());
    }

    return sb_process;
}


lldb::SBProcess
SBTarget::AttachToProcessWithID 
(
    SBListener &listener, 
    lldb::pid_t pid,// The process ID to attach to
    SBError& error  // An error explaining what went wrong if attach fails
)
{
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        if (listener.IsValid())
            sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
        else
            sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));
        

        if (sb_process.IsValid())
        {
            error.SetError (sb_process->Attach (pid));
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }
    return sb_process;

}

lldb::SBProcess
SBTarget::AttachToProcessWithName 
(
    SBListener &listener, 
    const char *name,   // basename of process to attach to
    bool wait_for,      // if true wait for a new instance of "name" to be launched
    SBError& error      // An error explaining what went wrong if attach fails
)
{
    SBProcess sb_process;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());

        if (listener.IsValid())
            sb_process.SetProcess (m_opaque_sp->CreateProcess (listener.ref()));
        else
            sb_process.SetProcess (m_opaque_sp->CreateProcess (m_opaque_sp->GetDebugger().GetListener()));

        if (sb_process.IsValid())
        {
            error.SetError (sb_process->Attach (name, wait_for));
        }
        else
        {
            error.SetErrorString ("unable to create lldb_private::Process");
        }
    }
    else
    {
        error.SetErrorString ("SBTarget is invalid");
    }
    return sb_process;

}

SBFileSpec
SBTarget::GetExecutable ()
{

    SBFileSpec exe_file_spec;
    if (m_opaque_sp)
    {
        ModuleSP exe_module_sp (m_opaque_sp->GetExecutableModule ());
        if (exe_module_sp)
            exe_file_spec.SetFileSpec (exe_module_sp->GetFileSpec());
    }

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
    {
        log->Printf ("SBTarget(%p)::GetExecutable () => SBFileSpec(%p)", 
                     m_opaque_sp.get(), exe_file_spec.get());
    }

    return exe_file_spec;
}


bool
SBTarget::DeleteTargetFromList (TargetList *list)
{
    if (m_opaque_sp)
        return list->DeleteTarget (m_opaque_sp);
    else
        return false;
}

bool
SBTarget::operator == (const SBTarget &rhs) const
{
    return m_opaque_sp.get() == rhs.m_opaque_sp.get();
}

bool
SBTarget::operator != (const SBTarget &rhs) const
{
    return m_opaque_sp.get() != rhs.m_opaque_sp.get();
}

lldb_private::Target *
SBTarget::operator ->() const
{
    return m_opaque_sp.get();
}

lldb_private::Target *
SBTarget::get() const
{
    return m_opaque_sp.get();
}

void
SBTarget::reset (const lldb::TargetSP& target_sp)
{
    m_opaque_sp = target_sp;
}

bool
SBTarget::ResolveLoadAddress (lldb::addr_t vm_addr, 
                              lldb::SBAddress& addr)
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        return m_opaque_sp->GetSectionLoadList().ResolveLoadAddress (vm_addr, *addr);
    }

    addr->Clear();
    return false;    
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation (const char *file, uint32_t line)
{
    return SBBreakpoint(BreakpointCreateByLocation (SBFileSpec (file, false), line));
}

SBBreakpoint
SBTarget::BreakpointCreateByLocation (const SBFileSpec &sb_file_spec, uint32_t line)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && line != 0)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_bp = m_opaque_sp->CreateBreakpoint (NULL, *sb_file_spec, line, true, false);
    }

    if (log)
    {
        SBStream sstr;
        sb_bp.GetDescription (sstr);
        char path[PATH_MAX];
        sb_file_spec->GetPath (path, sizeof(path));
        log->Printf ("SBTarget(%p)::BreakpointCreateByLocation ( %s:%u ) => SBBreakpoint(%p): %s", 
                     m_opaque_sp.get(), 
                     path,
                     line, 
                     sb_bp.get(),
                     sstr.GetData());
    }

    return sb_bp;
}

SBBreakpoint
SBTarget::BreakpointCreateByName (const char *symbol_name, const char *module_name)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && symbol_name && symbol_name[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        if (module_name && module_name[0])
        {
            FileSpec module_file_spec(module_name, false);
            *sb_bp = m_opaque_sp->CreateBreakpoint (&module_file_spec, symbol_name, eFunctionNameTypeFull | eFunctionNameTypeBase, false);
        }
        else
        {
            *sb_bp = m_opaque_sp->CreateBreakpoint (NULL, symbol_name, eFunctionNameTypeFull | eFunctionNameTypeBase, false);
        }
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByName (symbol=\"%s\", module=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name, module_name, sb_bp.get());
    }

    return sb_bp;
}

SBBreakpoint
SBTarget::BreakpointCreateByRegex (const char *symbol_name_regex, const char *module_name)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get() && symbol_name_regex && symbol_name_regex[0])
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        RegularExpression regexp(symbol_name_regex);
        
        if (module_name && module_name[0])
        {
            FileSpec module_file_spec(module_name, false);
            
            *sb_bp = m_opaque_sp->CreateBreakpoint (&module_file_spec, regexp, false);
        }
        else
        {
            *sb_bp = m_opaque_sp->CreateBreakpoint (NULL, regexp, false);
        }
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByRegex (symbol_regex=\"%s\", module_name=\"%s\") => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), symbol_name_regex, module_name, sb_bp.get());
    }

    return sb_bp;
}



SBBreakpoint
SBTarget::BreakpointCreateByAddress (addr_t address)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_bp;
    if (m_opaque_sp.get())
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_bp = m_opaque_sp->CreateBreakpoint (address, false);
    }
    
    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointCreateByAddress (%p, address=%p) => SBBreakpoint(%p)", m_opaque_sp.get(), address, sb_bp.get());
    }

    return sb_bp;
}

SBBreakpoint
SBTarget::FindBreakpointByID (break_id_t bp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBreakpoint sb_breakpoint;
    if (m_opaque_sp && bp_id != LLDB_INVALID_BREAK_ID)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        *sb_breakpoint = m_opaque_sp->GetBreakpointByID (bp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::FindBreakpointByID (bp_id=%d) => SBBreakpoint(%p)", 
                     m_opaque_sp.get(), (uint32_t) bp_id, sb_breakpoint.get());
    }

    return sb_breakpoint;
}

uint32_t
SBTarget::GetNumBreakpoints () const
{
    if (m_opaque_sp)
    {
        // The breakpoint list is thread safe, no need to lock
        return m_opaque_sp->GetBreakpointList().GetSize();
    }
    return 0;
}

SBBreakpoint
SBTarget::GetBreakpointAtIndex (uint32_t idx) const
{
    SBBreakpoint sb_breakpoint;
    if (m_opaque_sp)
    {
        // The breakpoint list is thread safe, no need to lock
        *sb_breakpoint = m_opaque_sp->GetBreakpointList().GetBreakpointAtIndex(idx);
    }
    return sb_breakpoint;
}

bool
SBTarget::BreakpointDelete (break_id_t bp_id)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    bool result = false;
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        result = m_opaque_sp->RemoveBreakpointByID (bp_id);
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::BreakpointDelete (bp_id=%d) => %i", m_opaque_sp.get(), (uint32_t) bp_id, result);
    }

    return result;
}

bool
SBTarget::EnableAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->EnableAllBreakpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DisableAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->DisableAllBreakpoints ();
        return true;
    }
    return false;
}

bool
SBTarget::DeleteAllBreakpoints ()
{
    if (m_opaque_sp)
    {
        Mutex::Locker api_locker (m_opaque_sp->GetAPIMutex());
        m_opaque_sp->RemoveAllBreakpoints ();
        return true;
    }
    return false;
}


uint32_t
SBTarget::GetNumModules () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    uint32_t num = 0;
    if (m_opaque_sp)
    {
        // The module list is thread safe, no need to lock
        num = m_opaque_sp->GetImages().GetSize();
    }

    if (log)
        log->Printf ("SBTarget(%p)::GetNumModules () => %d", m_opaque_sp.get(), num);

    return num;
}

void
SBTarget::Clear ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBTarget(%p)::Clear ()", m_opaque_sp.get());

    m_opaque_sp.reset();
}


SBModule
SBTarget::FindModule (const SBFileSpec &sb_file_spec)
{
    SBModule sb_module;
    if (m_opaque_sp && sb_file_spec.IsValid())
    {
        // The module list is thread safe, no need to lock
        sb_module.SetModule (m_opaque_sp->GetImages().FindFirstModuleForFileSpec (*sb_file_spec, NULL));
    }
    return sb_module;
}

SBModule
SBTarget::GetModuleAtIndex (uint32_t idx)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBModule sb_module;
    if (m_opaque_sp)
    {
        // The module list is thread safe, no need to lock
        sb_module.SetModule(m_opaque_sp->GetImages().GetModuleAtIndex(idx));
    }

    if (log)
    {
        log->Printf ("SBTarget(%p)::GetModuleAtIndex (idx=%d) => SBModule(%p)", 
                     m_opaque_sp.get(), idx, sb_module.get());
    }

    return sb_module;
}


SBBroadcaster
SBTarget::GetBroadcaster () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBroadcaster broadcaster(m_opaque_sp.get(), false);
    
    if (log)
        log->Printf ("SBTarget(%p)::GetBroadcaster () => SBBroadcaster(%p)", 
                     m_opaque_sp.get(), broadcaster.get());

    return broadcaster;
}


bool
SBTarget::GetDescription (SBStream &description, lldb::DescriptionLevel description_level)
{
    if (m_opaque_sp)
    {
        description.ref();
        m_opaque_sp->Dump (description.get(), description_level);
    }
    else
        description.Printf ("No value");
    
    return true;
}

bool
SBTarget::GetDescription (SBStream &description, lldb::DescriptionLevel description_level) const
{
    if (m_opaque_sp)
    {
        description.ref();
        m_opaque_sp->Dump (description.get(), description_level);
    }
    else
        description.Printf ("No value");
    
    return true;
}
