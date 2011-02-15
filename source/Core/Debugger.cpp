//===-- Debugger.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"
#include "lldb/Core/ConnectionFileDescriptor.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/InputReader.h"
#include "lldb/Core/State.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Core/Timer.h"
#include "lldb/Host/Terminal.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Thread.h"


using namespace lldb;
using namespace lldb_private;


static uint32_t g_shared_debugger_refcount = 0;
static lldb::user_id_t g_unique_id = 1;

#pragma mark Static Functions

static Mutex &
GetDebuggerListMutex ()
{
    static Mutex g_mutex(Mutex::eMutexTypeRecursive);
    return g_mutex;
}

typedef std::vector<DebuggerSP> DebuggerList;

static DebuggerList &
GetDebuggerList()
{
    // hide the static debugger list inside a singleton accessor to avoid
    // global init contructors
    static DebuggerList g_list;
    return g_list;
}


#pragma mark Debugger

UserSettingsControllerSP &
Debugger::GetSettingsController ()
{
    static UserSettingsControllerSP g_settings_controller;
    return g_settings_controller;
}

int
Debugger::TestDebuggerRefCount ()
{
    return g_shared_debugger_refcount;
}

void
Debugger::Initialize ()
{
    if (g_shared_debugger_refcount == 0)
    {
        UserSettingsControllerSP &usc = GetSettingsController();
        usc.reset (new SettingsController);
        UserSettingsController::InitializeSettingsController (usc,
                                                              SettingsController::global_settings_table,
                                                              SettingsController::instance_settings_table);
        lldb_private::Initialize();
    }
    g_shared_debugger_refcount++;

}

void
Debugger::Terminate ()
{
    if (g_shared_debugger_refcount > 0)
    {
        g_shared_debugger_refcount--;
        if (g_shared_debugger_refcount == 0)
        {
            lldb_private::WillTerminate();
            lldb_private::Terminate();
            UserSettingsControllerSP &usc = GetSettingsController();
            UserSettingsController::FinalizeSettingsController (usc);
            usc.reset();

            // Clear our master list of debugger objects
            Mutex::Locker locker (GetDebuggerListMutex ());
            GetDebuggerList().clear();
        }
    }
}

DebuggerSP
Debugger::CreateInstance ()
{
    DebuggerSP debugger_sp (new Debugger);
    // Scope for locker
    {
        Mutex::Locker locker (GetDebuggerListMutex ());
        GetDebuggerList().push_back(debugger_sp);
    }
    return debugger_sp;
}

void
Debugger::Destroy (lldb::DebuggerSP &debugger_sp)
{
    if (debugger_sp.get() == NULL)
        return;
        
    Mutex::Locker locker (GetDebuggerListMutex ());
    DebuggerList &debugger_list = GetDebuggerList ();
    DebuggerList::iterator pos, end = debugger_list.end();
    for (pos = debugger_list.begin (); pos != end; ++pos)
    {
        if ((*pos).get() == debugger_sp.get())
        {
            debugger_list.erase (pos);
            return;
        }
    }

}

lldb::DebuggerSP
Debugger::GetSP ()
{
    lldb::DebuggerSP debugger_sp;
    
    Mutex::Locker locker (GetDebuggerListMutex ());
    DebuggerList &debugger_list = GetDebuggerList();
    DebuggerList::iterator pos, end = debugger_list.end();
    for (pos = debugger_list.begin(); pos != end; ++pos)
    {
        if ((*pos).get() == this)
        {
            debugger_sp = *pos;
            break;
        }
    }
    return debugger_sp;
}

lldb::DebuggerSP
Debugger::FindDebuggerWithInstanceName (const ConstString &instance_name)
{
    lldb::DebuggerSP debugger_sp;
   
    Mutex::Locker locker (GetDebuggerListMutex ());
    DebuggerList &debugger_list = GetDebuggerList();
    DebuggerList::iterator pos, end = debugger_list.end();

    for (pos = debugger_list.begin(); pos != end; ++pos)
    {
        if ((*pos).get()->m_instance_name == instance_name)
        {
            debugger_sp = *pos;
            break;
        }
    }
    return debugger_sp;
}

TargetSP
Debugger::FindTargetWithProcessID (lldb::pid_t pid)
{
    lldb::TargetSP target_sp;
    Mutex::Locker locker (GetDebuggerListMutex ());
    DebuggerList &debugger_list = GetDebuggerList();
    DebuggerList::iterator pos, end = debugger_list.end();
    for (pos = debugger_list.begin(); pos != end; ++pos)
    {
        target_sp = (*pos)->GetTargetList().FindTargetWithProcessID (pid);
        if (target_sp)
            break;
    }
    return target_sp;
}


Debugger::Debugger () :
    UserID (g_unique_id++),
    DebuggerInstanceSettings (*GetSettingsController()),
    m_input_comm("debugger.input"),
    m_input_file (),
    m_output_file (),
    m_error_file (),
    m_target_list (),
    m_listener ("lldb.Debugger"),
    m_source_manager (),
    m_command_interpreter_ap (new CommandInterpreter (*this, eScriptLanguageDefault, false)),
    m_exe_ctx (),
    m_input_readers (),
    m_input_reader_data ()
{
    m_input_comm.SetCloseOnEOF(false);
    m_command_interpreter_ap->Initialize ();
}

Debugger::~Debugger ()
{
    CleanUpInputReaders();
    int num_targets = m_target_list.GetNumTargets();
    for (int i = 0; i < num_targets; i++)
    {
        ProcessSP process_sp (m_target_list.GetTargetAtIndex (i)->GetProcessSP());
        if (process_sp)
            process_sp->Destroy();
    }
    DisconnectInput();
}


bool
Debugger::GetAsyncExecution ()
{
    return !m_command_interpreter_ap->GetSynchronous();
}

void
Debugger::SetAsyncExecution (bool async_execution)
{
    m_command_interpreter_ap->SetSynchronous (!async_execution);
}

void
Debugger::DisconnectInput()
{
    m_input_comm.Clear ();
}
    
void
Debugger::SetInputFileHandle (FILE *fh, bool tranfer_ownership)
{
    File &in_file = GetInputFile();
    in_file.SetStream (fh, tranfer_ownership);
    if (in_file.IsValid() == false)
        in_file.SetStream (stdin, true);

    // Disconnect from any old connection if we had one
    m_input_comm.Disconnect ();
    m_input_comm.SetConnection (new ConnectionFileDescriptor (in_file.GetDescriptor(), true));
    m_input_comm.SetReadThreadBytesReceivedCallback (Debugger::DispatchInputCallback, this);

    Error error;
    if (m_input_comm.StartReadThread (&error) == false)
    {
        File &err_file = GetErrorFile();

        err_file.Printf ("error: failed to main input read thread: %s", error.AsCString() ? error.AsCString() : "unkown error");
        exit(1);
    }
}

void
Debugger::SetOutputFileHandle (FILE *fh, bool tranfer_ownership)
{
    File &out_file = GetOutputFile();
    out_file.SetStream (fh, tranfer_ownership);
    if (out_file.IsValid() == false)
        out_file.SetStream (stdout, false);
    
    GetCommandInterpreter().GetScriptInterpreter()->ResetOutputFileHandle (fh);
}

void
Debugger::SetErrorFileHandle (FILE *fh, bool tranfer_ownership)
{
    File &err_file = GetErrorFile();
    err_file.SetStream (fh, tranfer_ownership);
    if (err_file.IsValid() == false)
        err_file.SetStream (stderr, false);
}

CommandInterpreter &
Debugger::GetCommandInterpreter ()
{
    assert (m_command_interpreter_ap.get());
    return *m_command_interpreter_ap;
}

Listener &
Debugger::GetListener ()
{
    return m_listener;
}


TargetSP
Debugger::GetSelectedTarget ()
{
    return m_target_list.GetSelectedTarget ();
}

ExecutionContext
Debugger::GetSelectedExecutionContext ()
{
    ExecutionContext exe_ctx;
    exe_ctx.Clear();
    
    lldb::TargetSP target_sp = GetSelectedTarget();
    exe_ctx.target = target_sp.get();
    
    if (target_sp)
    {
        exe_ctx.process = target_sp->GetProcessSP().get();
        if (exe_ctx.process && exe_ctx.process->IsRunning() == false)
        {
            exe_ctx.thread = exe_ctx.process->GetThreadList().GetSelectedThread().get();
            if (exe_ctx.thread == NULL)
                exe_ctx.thread = exe_ctx.process->GetThreadList().GetThreadAtIndex(0).get();
            if (exe_ctx.thread)
            {
                exe_ctx.frame = exe_ctx.thread->GetSelectedFrame().get();
                if (exe_ctx.frame == NULL)
                    exe_ctx.frame = exe_ctx.thread->GetStackFrameAtIndex (0).get();
            }
        }
    }
    return exe_ctx;

}

SourceManager &
Debugger::GetSourceManager ()
{
    return m_source_manager;
}


TargetList&
Debugger::GetTargetList ()
{
    return m_target_list;
}

InputReaderSP 
Debugger::GetCurrentInputReader ()
{
    InputReaderSP reader_sp;
    
    if (!m_input_readers.empty())
    {
        // Clear any finished readers from the stack
        while (CheckIfTopInputReaderIsDone()) ;
        
        if (!m_input_readers.empty())
            reader_sp = m_input_readers.top();
    }
    
    return reader_sp;
}

void
Debugger::DispatchInputCallback (void *baton, const void *bytes, size_t bytes_len)
{
    if (bytes_len > 0)
        ((Debugger *)baton)->DispatchInput ((char *)bytes, bytes_len);
    else
        ((Debugger *)baton)->DispatchInputEndOfFile ();
}   


void
Debugger::DispatchInput (const char *bytes, size_t bytes_len)
{
    if (bytes == NULL || bytes_len == 0)
        return;

    WriteToDefaultReader (bytes, bytes_len);
}

void
Debugger::DispatchInputInterrupt ()
{
    m_input_reader_data.clear();
    
    InputReaderSP reader_sp (GetCurrentInputReader ());
    if (reader_sp)
    {
        reader_sp->Notify (eInputReaderInterrupt);
        
        // If notifying the reader of the interrupt finished the reader, we should pop it off the stack.
        while (CheckIfTopInputReaderIsDone ()) ;
    }
}

void
Debugger::DispatchInputEndOfFile ()
{
    m_input_reader_data.clear();
    
    InputReaderSP reader_sp (GetCurrentInputReader ());
    if (reader_sp)
    {
        reader_sp->Notify (eInputReaderEndOfFile);
        
        // If notifying the reader of the end-of-file finished the reader, we should pop it off the stack.
        while (CheckIfTopInputReaderIsDone ()) ;
    }
}

void
Debugger::CleanUpInputReaders ()
{
    m_input_reader_data.clear();
    
    // The bottom input reader should be the main debugger input reader.  We do not want to close that one here.
    while (m_input_readers.size() > 1)
    {
        InputReaderSP reader_sp (GetCurrentInputReader ());
        if (reader_sp)
        {
            reader_sp->Notify (eInputReaderEndOfFile);
            reader_sp->SetIsDone (true);
        }
    }
}

void
Debugger::WriteToDefaultReader (const char *bytes, size_t bytes_len)
{
    if (bytes && bytes_len)
        m_input_reader_data.append (bytes, bytes_len);

    if (m_input_reader_data.empty())
        return;

    while (!m_input_readers.empty() && !m_input_reader_data.empty())
    {
        // Get the input reader from the top of the stack
        InputReaderSP reader_sp (GetCurrentInputReader ());
        if (!reader_sp)
            break;

        size_t bytes_handled = reader_sp->HandleRawBytes (m_input_reader_data.c_str(), 
                                                          m_input_reader_data.size());
        if (bytes_handled)
        {
            m_input_reader_data.erase (0, bytes_handled);
        }
        else
        {
            // No bytes were handled, we might not have reached our 
            // granularity, just return and wait for more data
            break;
        }
    }
    
    // Flush out any input readers that are done.
    while (CheckIfTopInputReaderIsDone ())
        /* Do nothing. */;

}

void
Debugger::PushInputReader (const InputReaderSP& reader_sp)
{
    if (!reader_sp)
        return;
 
    // Deactivate the old top reader
    InputReaderSP top_reader_sp (GetCurrentInputReader ());
    
    if (top_reader_sp)
        top_reader_sp->Notify (eInputReaderDeactivate);

    m_input_readers.push (reader_sp);
    reader_sp->Notify (eInputReaderActivate);
    ActivateInputReader (reader_sp);
}

bool
Debugger::PopInputReader (const lldb::InputReaderSP& pop_reader_sp)
{
    bool result = false;

    // The reader on the stop of the stack is done, so let the next
    // read on the stack referesh its prompt and if there is one...
    if (!m_input_readers.empty())
    {
        // Cannot call GetCurrentInputReader here, as that would cause an infinite loop.
        InputReaderSP reader_sp(m_input_readers.top());
        
        if (!pop_reader_sp || pop_reader_sp.get() == reader_sp.get())
        {
            m_input_readers.pop ();
            reader_sp->Notify (eInputReaderDeactivate);
            reader_sp->Notify (eInputReaderDone);
            result = true;

            if (!m_input_readers.empty())
            {
                reader_sp = m_input_readers.top();
                if (reader_sp)
                {
                    ActivateInputReader (reader_sp);
                    reader_sp->Notify (eInputReaderReactivate);
                }
            }
        }
    }
    return result;
}

bool
Debugger::CheckIfTopInputReaderIsDone ()
{
    bool result = false;
    if (!m_input_readers.empty())
    {
        // Cannot call GetCurrentInputReader here, as that would cause an infinite loop.
        InputReaderSP reader_sp(m_input_readers.top());
        
        if (reader_sp && reader_sp->IsDone())
        {
            result = true;
            PopInputReader (reader_sp);
        }
    }
    return result;
}

void
Debugger::ActivateInputReader (const InputReaderSP &reader_sp)
{
    int input_fd = m_input_file.GetFile().GetDescriptor();

    if (input_fd >= 0)
    {
        Terminal tty(input_fd);
        
        tty.SetEcho(reader_sp->GetEcho());
                
        switch (reader_sp->GetGranularity())
        {
        case eInputReaderGranularityByte:
        case eInputReaderGranularityWord:
            tty.SetCanonical (false);
            break;

        case eInputReaderGranularityLine:
        case eInputReaderGranularityAll:
            tty.SetCanonical (true);
            break;

        default:
            break;
        }
    }
}

void
Debugger::UpdateExecutionContext (ExecutionContext *override_context)
{
    m_exe_ctx.Clear();

    if (override_context != NULL)
    {
        m_exe_ctx.target = override_context->target;
        m_exe_ctx.process = override_context->process;
        m_exe_ctx.thread = override_context->thread;
        m_exe_ctx.frame = override_context->frame;
    }
    else
    {
        TargetSP target_sp (GetSelectedTarget());
        if (target_sp)
        {
            m_exe_ctx.target = target_sp.get();
            m_exe_ctx.process = target_sp->GetProcessSP().get();
            if (m_exe_ctx.process && m_exe_ctx.process->IsAlive() && !m_exe_ctx.process->IsRunning())
            {
                m_exe_ctx.thread = m_exe_ctx.process->GetThreadList().GetSelectedThread().get();
                if (m_exe_ctx.thread == NULL)
                {
                    m_exe_ctx.thread = m_exe_ctx.process->GetThreadList().GetThreadAtIndex(0).get();
                    // If we didn't have a selected thread, select one here.
                    if (m_exe_ctx.thread != NULL)
                        m_exe_ctx.process->GetThreadList().SetSelectedThreadByID(m_exe_ctx.thread->GetID());
                }
                if (m_exe_ctx.thread)
                {
                    m_exe_ctx.frame = m_exe_ctx.thread->GetSelectedFrame().get();
                    if (m_exe_ctx.frame == NULL)
                    {
                        m_exe_ctx.frame = m_exe_ctx.thread->GetStackFrameAtIndex (0).get();
                        // If we didn't have a selected frame select one here.
                        if (m_exe_ctx.frame != NULL)
                            m_exe_ctx.thread->SetSelectedFrame(m_exe_ctx.frame);
                    }
                }
            }
        }
    }
}

DebuggerSP
Debugger::FindDebuggerWithID (lldb::user_id_t id)
{
    lldb::DebuggerSP debugger_sp;

    Mutex::Locker locker (GetDebuggerListMutex ());
    DebuggerList &debugger_list = GetDebuggerList();
    DebuggerList::iterator pos, end = debugger_list.end();
    for (pos = debugger_list.begin(); pos != end; ++pos)
    {
        if ((*pos).get()->GetID() == id)
        {
            debugger_sp = *pos;
            break;
        }
    }
    return debugger_sp;
}

static void
TestPromptFormats (StackFrame *frame)
{
    if (frame == NULL)
        return;

    StreamString s;
    const char *prompt_format =         
    "{addr = '${addr}'\n}"
    "{process.id = '${process.id}'\n}"
    "{process.name = '${process.name}'\n}"
    "{process.file.basename = '${process.file.basename}'\n}"
    "{process.file.fullpath = '${process.file.fullpath}'\n}"
    "{thread.id = '${thread.id}'\n}"
    "{thread.index = '${thread.index}'\n}"
    "{thread.name = '${thread.name}'\n}"
    "{thread.queue = '${thread.queue}'\n}"
    "{thread.stop-reason = '${thread.stop-reason}'\n}"
    "{target.arch = '${target.arch}'\n}"
    "{module.file.basename = '${module.file.basename}'\n}"
    "{module.file.fullpath = '${module.file.fullpath}'\n}"
    "{file.basename = '${file.basename}'\n}"
    "{file.fullpath = '${file.fullpath}'\n}"
    "{frame.index = '${frame.index}'\n}"
    "{frame.pc = '${frame.pc}'\n}"
    "{frame.sp = '${frame.sp}'\n}"
    "{frame.fp = '${frame.fp}'\n}"
    "{frame.flags = '${frame.flags}'\n}"
    "{frame.reg.rdi = '${frame.reg.rdi}'\n}"
    "{frame.reg.rip = '${frame.reg.rip}'\n}"
    "{frame.reg.rsp = '${frame.reg.rsp}'\n}"
    "{frame.reg.rbp = '${frame.reg.rbp}'\n}"
    "{frame.reg.rflags = '${frame.reg.rflags}'\n}"
    "{frame.reg.xmm0 = '${frame.reg.xmm0}'\n}"
    "{frame.reg.carp = '${frame.reg.carp}'\n}"
    "{function.id = '${function.id}'\n}"
    "{function.name = '${function.name}'\n}"
    "{function.addr-offset = '${function.addr-offset}'\n}"
    "{function.line-offset = '${function.line-offset}'\n}"
    "{function.pc-offset = '${function.pc-offset}'\n}"
    "{line.file.basename = '${line.file.basename}'\n}"
    "{line.file.fullpath = '${line.file.fullpath}'\n}"
    "{line.number = '${line.number}'\n}"
    "{line.start-addr = '${line.start-addr}'\n}"
    "{line.end-addr = '${line.end-addr}'\n}"
;

    SymbolContext sc (frame->GetSymbolContext(eSymbolContextEverything));
    ExecutionContext exe_ctx;
    frame->CalculateExecutionContext(exe_ctx);
    const char *end = NULL;
    if (Debugger::FormatPrompt (prompt_format, &sc, &exe_ctx, &sc.line_entry.range.GetBaseAddress(), s, &end))
    {
        printf("%s\n", s.GetData());
    }
    else
    {
        printf ("error: at '%s'\n", end);
        printf ("what we got: %s\n", s.GetData());
    }
}

bool
Debugger::FormatPrompt 
(
    const char *format,
    const SymbolContext *sc,
    const ExecutionContext *exe_ctx,
    const Address *addr,
    Stream &s,
    const char **end
)
{
    bool success = true;
    const char *p;
    for (p = format; *p != '\0'; ++p)
    {
        size_t non_special_chars = ::strcspn (p, "${}\\");
        if (non_special_chars > 0)
        {
            if (success)
                s.Write (p, non_special_chars);
            p += non_special_chars;            
        }

        if (*p == '\0')
        {
            break;
        }
        else if (*p == '{')
        {
            // Start a new scope that must have everything it needs if it is to
            // to make it into the final output stream "s". If you want to make
            // a format that only prints out the function or symbol name if there
            // is one in the symbol context you can use:
            //      "{function =${function.name}}"
            // The first '{' starts a new scope that end with the matching '}' at
            // the end of the string. The contents "function =${function.name}"
            // will then be evaluated and only be output if there is a function
            // or symbol with a valid name. 
            StreamString sub_strm;

            ++p;  // Skip the '{'
            
            if (FormatPrompt (p, sc, exe_ctx, addr, sub_strm, &p))
            {
                // The stream had all it needed
                s.Write(sub_strm.GetData(), sub_strm.GetSize());
            }
            if (*p != '}')
            {
                success = false;
                break;
            }
        }
        else if (*p == '}')
        {
            // End of a enclosing scope
            break;
        }
        else if (*p == '$')
        {
            // We have a prompt variable to print
            ++p;
            if (*p == '{')
            {
                ++p;
                const char *var_name_begin = p;
                const char *var_name_end = ::strchr (p, '}');

                if (var_name_end && var_name_begin < var_name_end)
                {
                    // if we have already failed to parse, skip this variable
                    if (success)
                    {
                        const char *cstr = NULL;
                        Address format_addr;
                        bool calculate_format_addr_function_offset = false;
                        // Set reg_kind and reg_num to invalid values
                        RegisterKind reg_kind = kNumRegisterKinds; 
                        uint32_t reg_num = LLDB_INVALID_REGNUM;
                        FileSpec format_file_spec;
                        const lldb::RegisterInfo *reg_info = NULL;
                        RegisterContext *reg_ctx = NULL;

                        // Each variable must set success to true below...
                        bool var_success = false;
                        switch (var_name_begin[0])
                        {
                        case 'a':
                            if (::strncmp (var_name_begin, "addr}", strlen("addr}")) == 0)
                            {
                                if (addr && addr->IsValid())
                                {
                                    var_success = true;
                                    format_addr = *addr;
                                }
                            }
                            break;

                        case 'p':
                            if (::strncmp (var_name_begin, "process.", strlen("process.")) == 0)
                            {
                                if (exe_ctx && exe_ctx->process != NULL)
                                {
                                    var_name_begin += ::strlen ("process.");
                                    if (::strncmp (var_name_begin, "id}", strlen("id}")) == 0)
                                    {
                                        s.Printf("%i", exe_ctx->process->GetID());
                                        var_success = true;
                                    }
                                    else if ((::strncmp (var_name_begin, "name}", strlen("name}")) == 0) ||
                                             (::strncmp (var_name_begin, "file.basename}", strlen("file.basename}")) == 0) ||
                                             (::strncmp (var_name_begin, "file.fullpath}", strlen("file.fullpath}")) == 0))
                                    {
                                        ModuleSP exe_module_sp (exe_ctx->process->GetTarget().GetExecutableModule());
                                        if (exe_module_sp)
                                        {
                                            if (var_name_begin[0] == 'n' || var_name_begin[5] == 'f')
                                            {
                                                format_file_spec.GetFilename() = exe_module_sp->GetFileSpec().GetFilename();
                                                var_success = format_file_spec;
                                            }
                                            else
                                            {
                                                format_file_spec = exe_module_sp->GetFileSpec();
                                                var_success = format_file_spec;
                                            }
                                        }
                                    }
                                }                                        
                            }
                            break;
                        
                        case 't':
                            if (::strncmp (var_name_begin, "thread.", strlen("thread.")) == 0)
                            {
                                if (exe_ctx && exe_ctx->thread)
                                {
                                    var_name_begin += ::strlen ("thread.");
                                    if (::strncmp (var_name_begin, "id}", strlen("id}")) == 0)
                                    {
                                        s.Printf("0x%4.4x", exe_ctx->thread->GetID());
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "index}", strlen("index}")) == 0)
                                    {
                                        s.Printf("%u", exe_ctx->thread->GetIndexID());
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "name}", strlen("name}")) == 0)
                                    {
                                        cstr = exe_ctx->thread->GetName();
                                        var_success = cstr && cstr[0];
                                        if (var_success)
                                            s.PutCString(cstr);
                                    }
                                    else if (::strncmp (var_name_begin, "queue}", strlen("queue}")) == 0)
                                    {
                                        cstr = exe_ctx->thread->GetQueueName();
                                        var_success = cstr && cstr[0];
                                        if (var_success)
                                            s.PutCString(cstr);
                                    }
                                    else if (::strncmp (var_name_begin, "stop-reason}", strlen("stop-reason}")) == 0)
                                    {
                                        StopInfoSP stop_info_sp = exe_ctx->thread->GetStopInfo ();
                                        if (stop_info_sp)
                                        {
                                            cstr = stop_info_sp->GetDescription();
                                            if (cstr && cstr[0])
                                            {
                                                s.PutCString(cstr);
                                                var_success = true;
                                            }
                                        }
                                    }
                                }
                            }
                            else if (::strncmp (var_name_begin, "target.", strlen("target.")) == 0)
                            {
                                Target *target = Target::GetTargetFromContexts (exe_ctx, sc);
                                if (target)
                                {
                                    var_name_begin += ::strlen ("target.");
                                    if (::strncmp (var_name_begin, "arch}", strlen("arch}")) == 0)
                                    {
                                        ArchSpec arch (target->GetArchitecture ());
                                        if (arch.IsValid())
                                        {
                                            s.PutCString (arch.AsCString());
                                            var_success = true;
                                        }
                                    }
                                }                                        
                            }
                            break;
                            
                            
                        case 'm':
                            if (::strncmp (var_name_begin, "module.", strlen("module.")) == 0)
                            {
                                if (sc && sc->module_sp.get())
                                {
                                    Module *module = sc->module_sp.get();
                                    var_name_begin += ::strlen ("module.");
                                    
                                    if (::strncmp (var_name_begin, "file.", strlen("file.")) == 0)
                                    {
                                        if (module->GetFileSpec())
                                        {
                                            var_name_begin += ::strlen ("file.");
                                            
                                            if (::strncmp (var_name_begin, "basename}", strlen("basename}")) == 0)
                                            {
                                                format_file_spec.GetFilename() = module->GetFileSpec().GetFilename();
                                                var_success = format_file_spec;
                                            }
                                            else if (::strncmp (var_name_begin, "fullpath}", strlen("fullpath}")) == 0)
                                            {
                                                format_file_spec = module->GetFileSpec();
                                                var_success = format_file_spec;
                                            }
                                        }
                                    }
                                }
                            }
                            break;
                            
                        
                        case 'f':
                            if (::strncmp (var_name_begin, "file.", strlen("file.")) == 0)
                            {
                                if (sc && sc->comp_unit != NULL)
                                {
                                    var_name_begin += ::strlen ("file.");
                                    
                                    if (::strncmp (var_name_begin, "basename}", strlen("basename}")) == 0)
                                    {
                                        format_file_spec.GetFilename() = sc->comp_unit->GetFilename();
                                        var_success = format_file_spec;
                                    }
                                    else if (::strncmp (var_name_begin, "fullpath}", strlen("fullpath}")) == 0)
                                    {
                                        format_file_spec = *sc->comp_unit;
                                        var_success = format_file_spec;
                                    }
                                }
                            }
                            else if (::strncmp (var_name_begin, "frame.", strlen("frame.")) == 0)
                            {
                                if (exe_ctx && exe_ctx->frame)
                                {
                                    var_name_begin += ::strlen ("frame.");
                                    if (::strncmp (var_name_begin, "index}", strlen("index}")) == 0)
                                    {
                                        s.Printf("%u", exe_ctx->frame->GetFrameIndex());
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "pc}", strlen("pc}")) == 0)
                                    {
                                        reg_kind = eRegisterKindGeneric;
                                        reg_num = LLDB_REGNUM_GENERIC_PC;
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "sp}", strlen("sp}")) == 0)
                                    {
                                        reg_kind = eRegisterKindGeneric;
                                        reg_num = LLDB_REGNUM_GENERIC_SP;
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "fp}", strlen("fp}")) == 0)
                                    {
                                        reg_kind = eRegisterKindGeneric;
                                        reg_num = LLDB_REGNUM_GENERIC_FP;
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "flags}", strlen("flags}")) == 0)
                                    {
                                        reg_kind = eRegisterKindGeneric;
                                        reg_num = LLDB_REGNUM_GENERIC_FLAGS;
                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "reg.", strlen ("reg.")) == 0)
                                    {
                                        reg_ctx = exe_ctx->frame->GetRegisterContext().get();
                                        if (reg_ctx)
                                        {
                                            var_name_begin += ::strlen ("reg.");
                                            if (var_name_begin < var_name_end)
                                            {
                                                std::string reg_name (var_name_begin, var_name_end);
                                                reg_info = reg_ctx->GetRegisterInfoByName (reg_name.c_str());
                                                if (reg_info)
                                                    var_success = true;
                                            }
                                        }
                                    }
                                }
                            }
                            else if (::strncmp (var_name_begin, "function.", strlen("function.")) == 0)
                            {
                                if (sc && (sc->function != NULL || sc->symbol != NULL))
                                {
                                    var_name_begin += ::strlen ("function.");
                                    if (::strncmp (var_name_begin, "id}", strlen("id}")) == 0)
                                    {
                                        if (sc->function)
                                            s.Printf("function{0x%8.8x}", sc->function->GetID());
                                        else
                                            s.Printf("symbol[%u]", sc->symbol->GetID());

                                        var_success = true;
                                    }
                                    else if (::strncmp (var_name_begin, "name}", strlen("name}")) == 0)
                                    {
                                        if (sc->function)
                                            cstr = sc->function->GetName().AsCString (NULL);
                                        else if (sc->symbol)
                                            cstr = sc->symbol->GetName().AsCString (NULL);
                                        if (cstr)
                                        {
                                            s.PutCString(cstr);
                                            
                                            if (sc->block)
                                            {
                                                Block *inline_block = sc->block->GetContainingInlinedBlock ();
                                                if (inline_block)
                                                {
                                                    const InlineFunctionInfo *inline_info = sc->block->GetInlinedFunctionInfo();
                                                    if (inline_info)
                                                    {
                                                        s.PutCString(" [inlined] ");
                                                        inline_info->GetName().Dump(&s);
                                                    }
                                                }
                                            }
                                            var_success = true;
                                        }
                                    }
                                    else if (::strncmp (var_name_begin, "addr-offset}", strlen("addr-offset}")) == 0)
                                    {
                                        var_success = addr != NULL;
                                        if (var_success)
                                        {
                                            format_addr = *addr;
                                            calculate_format_addr_function_offset = true;
                                        }
                                    }
                                    else if (::strncmp (var_name_begin, "line-offset}", strlen("line-offset}")) == 0)
                                    {
                                        var_success = sc->line_entry.range.GetBaseAddress().IsValid();
                                        if (var_success)
                                        {
                                            format_addr = sc->line_entry.range.GetBaseAddress();
                                            calculate_format_addr_function_offset = true;
                                        }
                                    }
                                    else if (::strncmp (var_name_begin, "pc-offset}", strlen("pc-offset}")) == 0)
                                    {
                                        var_success = exe_ctx->frame;
                                        if (var_success)
                                        {
                                            format_addr = exe_ctx->frame->GetFrameCodeAddress();
                                            calculate_format_addr_function_offset = true;
                                        }
                                    }
                                }
                            }
                            break;

                        case 'l':
                            if (::strncmp (var_name_begin, "line.", strlen("line.")) == 0)
                            {
                                if (sc && sc->line_entry.IsValid())
                                {
                                    var_name_begin += ::strlen ("line.");
                                    if (::strncmp (var_name_begin, "file.", strlen("file.")) == 0)
                                    {
                                        var_name_begin += ::strlen ("file.");
                                        
                                        if (::strncmp (var_name_begin, "basename}", strlen("basename}")) == 0)
                                        {
                                            format_file_spec.GetFilename() = sc->line_entry.file.GetFilename();
                                            var_success = format_file_spec;
                                        }
                                        else if (::strncmp (var_name_begin, "fullpath}", strlen("fullpath}")) == 0)
                                        {
                                            format_file_spec = sc->line_entry.file;
                                            var_success = format_file_spec;
                                        }
                                    }
                                    else if (::strncmp (var_name_begin, "number}", strlen("number}")) == 0)
                                    {
                                        var_success = true;
                                        s.Printf("%u", sc->line_entry.line);
                                    }
                                    else if ((::strncmp (var_name_begin, "start-addr}", strlen("start-addr}")) == 0) ||
                                             (::strncmp (var_name_begin, "end-addr}", strlen("end-addr}")) == 0))
                                    {
                                        var_success = sc && sc->line_entry.range.GetBaseAddress().IsValid();
                                        if (var_success)
                                        {
                                            format_addr = sc->line_entry.range.GetBaseAddress();
                                            if (var_name_begin[0] == 'e')
                                                format_addr.Slide (sc->line_entry.range.GetByteSize());
                                        }
                                    }
                                }
                            }
                            break;
                        }
                        
                        if (var_success)
                        {
                            // If format addr is valid, then we need to print an address
                            if (reg_num != LLDB_INVALID_REGNUM)
                            {
                                // We have a register value to display...
                                if (reg_num == LLDB_REGNUM_GENERIC_PC && reg_kind == eRegisterKindGeneric)
                                {
                                    format_addr = exe_ctx->frame->GetFrameCodeAddress();
                                }
                                else
                                {
                                    if (reg_ctx == NULL)
                                        reg_ctx = exe_ctx->frame->GetRegisterContext().get();

                                    if (reg_ctx)
                                    {
                                        if (reg_kind != kNumRegisterKinds)
                                            reg_num = reg_ctx->ConvertRegisterKindToRegisterNumber(reg_kind, reg_num);
                                        reg_info = reg_ctx->GetRegisterInfoAtIndex (reg_num);
                                        var_success = reg_info != NULL;
                                    }
                                }
                            }
                            
                            if (reg_info != NULL)
                            {
                                DataExtractor reg_data;
                                var_success = reg_ctx->ReadRegisterBytes (reg_info->kinds[eRegisterKindLLDB], reg_data);
                                {
                                    reg_data.Dump(&s, 0, reg_info->format, reg_info->byte_size, 1, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0);
                                }
                            }                            
                            
                            if (format_file_spec)
                            {
                                s << format_file_spec;
                            }

                            // If format addr is valid, then we need to print an address
                            if (format_addr.IsValid())
                            {
                                var_success = false;

                                if (calculate_format_addr_function_offset)
                                {
                                    Address func_addr;
                                    
                                    if (sc)
                                    {
                                        if (sc->function)
                                        {
                                            func_addr = sc->function->GetAddressRange().GetBaseAddress();
                                            if (sc->block)
                                            {
                                                // Check to make sure we aren't in an inline
                                                // function. If we are, use the inline block
                                                // range that contains "format_addr" since
                                                // blocks can be discontiguous.
                                                Block *inline_block = sc->block->GetContainingInlinedBlock ();
                                                AddressRange inline_range;
                                                if (inline_block && inline_block->GetRangeContainingAddress (format_addr, inline_range))
                                                    func_addr = inline_range.GetBaseAddress();
                                            }
                                        }
                                        else if (sc->symbol && sc->symbol->GetAddressRangePtr())
                                            func_addr = sc->symbol->GetAddressRangePtr()->GetBaseAddress();
                                    }
                                    
                                    if (func_addr.IsValid())
                                    {
                                        if (func_addr.GetSection() == format_addr.GetSection())
                                        {
                                            addr_t func_file_addr = func_addr.GetFileAddress();
                                            addr_t addr_file_addr = format_addr.GetFileAddress();
                                            if (addr_file_addr > func_file_addr)
                                                s.Printf(" + %llu", addr_file_addr - func_file_addr);
                                            else if (addr_file_addr < func_file_addr)
                                                s.Printf(" - %llu", func_file_addr - addr_file_addr);
                                            var_success = true;
                                        }
                                        else
                                        {
                                            Target *target = Target::GetTargetFromContexts (exe_ctx, sc);
                                            if (target)
                                            {
                                                addr_t func_load_addr = func_addr.GetLoadAddress (target);
                                                addr_t addr_load_addr = format_addr.GetLoadAddress (target);
                                                if (addr_load_addr > func_load_addr)
                                                    s.Printf(" + %llu", addr_load_addr - func_load_addr);
                                                else if (addr_load_addr < func_load_addr)
                                                    s.Printf(" - %llu", func_load_addr - addr_load_addr);
                                                var_success = true;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    Target *target = Target::GetTargetFromContexts (exe_ctx, sc);
                                    addr_t vaddr = LLDB_INVALID_ADDRESS;
                                    if (exe_ctx && !target->GetSectionLoadList().IsEmpty())
                                        vaddr = format_addr.GetLoadAddress (target);
                                    if (vaddr == LLDB_INVALID_ADDRESS)
                                        vaddr = format_addr.GetFileAddress ();

                                    if (vaddr != LLDB_INVALID_ADDRESS)
                                    {
                                        int addr_width = target->GetArchitecture().GetAddressByteSize() * 2;
                                        if (addr_width == 0)
                                            addr_width = 16;
                                        s.Printf("0x%*.*llx", addr_width, addr_width, vaddr);
                                        var_success = true;
                                    }
                                }
                            }
                        }

                        if (var_success == false)
                            success = false;
                    }
                    p = var_name_end;
                }
                else
                    break;
            }
            else
            {
                // We got a dollar sign with no '{' after it, it must just be a dollar sign
                s.PutChar(*p);
            }
        }
        else if (*p == '\\')
        {
            ++p; // skip the slash
            switch (*p)
            {
            case 'a': s.PutChar ('\a'); break;
            case 'b': s.PutChar ('\b'); break;
            case 'f': s.PutChar ('\f'); break;
            case 'n': s.PutChar ('\n'); break;
            case 'r': s.PutChar ('\r'); break;
            case 't': s.PutChar ('\t'); break;
            case 'v': s.PutChar ('\v'); break;
            case '\'': s.PutChar ('\''); break; 
            case '\\': s.PutChar ('\\'); break; 
            case '0':
                // 1 to 3 octal chars
                {
                    // Make a string that can hold onto the initial zero char,
                    // up to 3 octal digits, and a terminating NULL.
                    char oct_str[5] = { 0, 0, 0, 0, 0 };

                    int i;
                    for (i=0; (p[i] >= '0' && p[i] <= '7') && i<4; ++i)
                        oct_str[i] = p[i];

                    // We don't want to consume the last octal character since
                    // the main for loop will do this for us, so we advance p by
                    // one less than i (even if i is zero)
                    p += i - 1;
                    unsigned long octal_value = ::strtoul (oct_str, NULL, 8);
                    if (octal_value <= UINT8_MAX)
                    {
                        char octal_char = octal_value;
                        s.Write (&octal_char, 1);
                    }
                }
                break;

            case 'x':
                // hex number in the format 
                if (isxdigit(p[1]))
                {
                    ++p;    // Skip the 'x'

                    // Make a string that can hold onto two hex chars plus a
                    // NULL terminator
                    char hex_str[3] = { 0,0,0 };
                    hex_str[0] = *p;
                    if (isxdigit(p[1]))
                    {
                        ++p; // Skip the first of the two hex chars
                        hex_str[1] = *p;
                    }

                    unsigned long hex_value = strtoul (hex_str, NULL, 16);                    
                    if (hex_value <= UINT8_MAX)
                        s.PutChar (hex_value);
                }
                else
                {
                    s.PutChar('x');
                }
                break;
                
            default:
                // Just desensitize any other character by just printing what
                // came after the '\'
                s << *p;
                break;
            
            }

        }
    }
    if (end) 
        *end = p;
    return success;
}

#pragma mark Debugger::SettingsController

//--------------------------------------------------
// class Debugger::SettingsController
//--------------------------------------------------

Debugger::SettingsController::SettingsController () :
    UserSettingsController ("", lldb::UserSettingsControllerSP())
{
    m_default_settings.reset (new DebuggerInstanceSettings (*this, false, 
                                                            InstanceSettings::GetDefaultName().AsCString()));
}

Debugger::SettingsController::~SettingsController ()
{
}


lldb::InstanceSettingsSP
Debugger::SettingsController::CreateInstanceSettings (const char *instance_name)
{
    DebuggerInstanceSettings *new_settings = new DebuggerInstanceSettings (*GetSettingsController(),
                                                                           false, instance_name);
    lldb::InstanceSettingsSP new_settings_sp (new_settings);
    return new_settings_sp;
}

#pragma mark DebuggerInstanceSettings
//--------------------------------------------------
//  class DebuggerInstanceSettings
//--------------------------------------------------

DebuggerInstanceSettings::DebuggerInstanceSettings 
(
    UserSettingsController &owner, 
    bool live_instance,
    const char *name
) :
    InstanceSettings (owner, name ? name : InstanceSettings::InvalidName().AsCString(), live_instance),
    m_term_width (80),
    m_prompt (),
    m_frame_format (),
    m_thread_format (),    
    m_script_lang (),
    m_use_external_editor (false),
    m_auto_confirm_on (false)
{
    // CopyInstanceSettings is a pure virtual function in InstanceSettings; it therefore cannot be called
    // until the vtables for DebuggerInstanceSettings are properly set up, i.e. AFTER all the initializers.
    // For this reason it has to be called here, rather than in the initializer or in the parent constructor.
    // The same is true of CreateInstanceName().

    if (GetInstanceName() == InstanceSettings::InvalidName())
    {
        ChangeInstanceName (std::string (CreateInstanceName().AsCString()));
        m_owner.RegisterInstanceSettings (this);
    }

    if (live_instance)
    {
        const lldb::InstanceSettingsSP &pending_settings = m_owner.FindPendingSettings (m_instance_name);
        CopyInstanceSettings (pending_settings, false);
    }
}

DebuggerInstanceSettings::DebuggerInstanceSettings (const DebuggerInstanceSettings &rhs) :
    InstanceSettings (*Debugger::GetSettingsController(), CreateInstanceName ().AsCString()),
    m_prompt (rhs.m_prompt),
    m_frame_format (rhs.m_frame_format),
    m_thread_format (rhs.m_thread_format),
    m_script_lang (rhs.m_script_lang),
    m_use_external_editor (rhs.m_use_external_editor),
    m_auto_confirm_on(rhs.m_auto_confirm_on)
{
    const lldb::InstanceSettingsSP &pending_settings = m_owner.FindPendingSettings (m_instance_name);
    CopyInstanceSettings (pending_settings, false);
    m_owner.RemovePendingSettings (m_instance_name);
}

DebuggerInstanceSettings::~DebuggerInstanceSettings ()
{
}

DebuggerInstanceSettings&
DebuggerInstanceSettings::operator= (const DebuggerInstanceSettings &rhs)
{
    if (this != &rhs)
    {
        m_term_width = rhs.m_term_width;
        m_prompt = rhs.m_prompt;
        m_frame_format = rhs.m_frame_format;
        m_thread_format = rhs.m_thread_format;
        m_script_lang = rhs.m_script_lang;
        m_use_external_editor = rhs.m_use_external_editor;
        m_auto_confirm_on = rhs.m_auto_confirm_on;
    }

    return *this;
}

bool
DebuggerInstanceSettings::ValidTermWidthValue (const char *value, Error err)
{
    bool valid = false;

    // Verify we have a value string.
    if (value == NULL || value[0] == '\0')
    {
        err.SetErrorString ("Missing value. Can't set terminal width without a value.\n");
    }
    else
    {
        char *end = NULL;
        const uint32_t width = ::strtoul (value, &end, 0);
        
        if (end && end[0] == '\0')
        {
            if (width >= 10 && width <= 1024)
                valid = true;
            else
                err.SetErrorString ("Invalid term-width value; value must be between 10 and 1024.\n");
        }
        else
            err.SetErrorStringWithFormat ("'%s' is not a valid unsigned integer string.\n", value);
    }

    return valid;
}


void
DebuggerInstanceSettings::UpdateInstanceSettingsVariable (const ConstString &var_name,
                                                          const char *index_value,
                                                          const char *value,
                                                          const ConstString &instance_name,
                                                          const SettingEntry &entry,
                                                          lldb::VarSetOperationType op,
                                                          Error &err,
                                                          bool pending)
{

    if (var_name == TermWidthVarName())
    {
        if (ValidTermWidthValue (value, err))
        {
            m_term_width = ::strtoul (value, NULL, 0);
        }
    }
    else if (var_name == PromptVarName())
    {
        UserSettingsController::UpdateStringVariable (op, m_prompt, value, err);
        if (!pending)
        {
            // 'instance_name' is actually (probably) in the form '[<instance_name>]';  if so, we need to
            // strip off the brackets before passing it to BroadcastPromptChange.

            std::string tmp_instance_name (instance_name.AsCString());
            if ((tmp_instance_name[0] == '[') 
                && (tmp_instance_name[instance_name.GetLength() - 1] == ']'))
                tmp_instance_name = tmp_instance_name.substr (1, instance_name.GetLength() - 2);
            ConstString new_name (tmp_instance_name.c_str());

            BroadcastPromptChange (new_name, m_prompt.c_str());
        }
    }
    else if (var_name == GetFrameFormatName())
    {
        UserSettingsController::UpdateStringVariable (op, m_frame_format, value, err);
    }
    else if (var_name == GetThreadFormatName())
    {
        UserSettingsController::UpdateStringVariable (op, m_thread_format, value, err);
    }
    else if (var_name == ScriptLangVarName())
    {
        bool success;
        m_script_lang = Args::StringToScriptLanguage (value, eScriptLanguageDefault,
                                                      &success);
    }
    else if (var_name == UseExternalEditorVarName ())
    {
        UserSettingsController::UpdateBooleanVariable (op, m_use_external_editor, value, err);
    }
    else if (var_name == AutoConfirmName ())
    {
        UserSettingsController::UpdateBooleanVariable (op, m_auto_confirm_on, value, err);
    }
}

bool
DebuggerInstanceSettings::GetInstanceSettingsValue (const SettingEntry &entry,
                                                    const ConstString &var_name,
                                                    StringList &value,
                                                    Error *err)
{
    if (var_name == PromptVarName())
    {
        value.AppendString (m_prompt.c_str(), m_prompt.size());
        
    }
    else if (var_name == ScriptLangVarName())
    {
        value.AppendString (ScriptInterpreter::LanguageToString (m_script_lang).c_str());
    }
    else if (var_name == TermWidthVarName())
    {
        StreamString width_str;
        width_str.Printf ("%d", m_term_width);
        value.AppendString (width_str.GetData());
    }
    else if (var_name == GetFrameFormatName ())
    {
        value.AppendString(m_frame_format.c_str(), m_frame_format.size());
    }
    else if (var_name == GetThreadFormatName ())
    {
        value.AppendString(m_thread_format.c_str(), m_thread_format.size());
    }
    else if (var_name == UseExternalEditorVarName())
    {
        if (m_use_external_editor)
            value.AppendString ("true");
        else
            value.AppendString ("false");
    }
    else if (var_name == AutoConfirmName())
    {
        if (m_auto_confirm_on)
            value.AppendString ("true");
        else
            value.AppendString ("false");
    }
    else
    {
        if (err)
            err->SetErrorStringWithFormat ("unrecognized variable name '%s'", var_name.AsCString());
        return false;
    }
    return true;
}

void
DebuggerInstanceSettings::CopyInstanceSettings (const lldb::InstanceSettingsSP &new_settings,
                                                bool pending)
{
    if (new_settings.get() == NULL)
        return;

    DebuggerInstanceSettings *new_debugger_settings = (DebuggerInstanceSettings *) new_settings.get();

    m_prompt = new_debugger_settings->m_prompt;
    if (!pending)
    {
        // 'instance_name' is actually (probably) in the form '[<instance_name>]';  if so, we need to
        // strip off the brackets before passing it to BroadcastPromptChange.

        std::string tmp_instance_name (m_instance_name.AsCString());
        if ((tmp_instance_name[0] == '[')
            && (tmp_instance_name[m_instance_name.GetLength() - 1] == ']'))
            tmp_instance_name = tmp_instance_name.substr (1, m_instance_name.GetLength() - 2);
        ConstString new_name (tmp_instance_name.c_str());

        BroadcastPromptChange (new_name, m_prompt.c_str());
    }
    m_frame_format = new_debugger_settings->m_frame_format;
    m_thread_format = new_debugger_settings->m_thread_format;
    m_term_width = new_debugger_settings->m_term_width;
    m_script_lang = new_debugger_settings->m_script_lang;
    m_use_external_editor = new_debugger_settings->m_use_external_editor;
    m_auto_confirm_on = new_debugger_settings->m_auto_confirm_on;
}


bool
DebuggerInstanceSettings::BroadcastPromptChange (const ConstString &instance_name, const char *new_prompt)
{
    std::string tmp_prompt;
    
    if (new_prompt != NULL)
    {
        tmp_prompt = new_prompt ;
        int len = tmp_prompt.size();
        if (len > 1
            && (tmp_prompt[0] == '\'' || tmp_prompt[0] == '"')
            && (tmp_prompt[len-1] == tmp_prompt[0]))
        {
            tmp_prompt = tmp_prompt.substr(1,len-2);
        }
        len = tmp_prompt.size();
        if (tmp_prompt[len-1] != ' ')
            tmp_prompt.append(" ");
    }
    EventSP new_event_sp;
    new_event_sp.reset (new Event(CommandInterpreter::eBroadcastBitResetPrompt, 
                                  new EventDataBytes (tmp_prompt.c_str())));

    if (instance_name.GetLength() != 0)
    {
        // Set prompt for a particular instance.
        Debugger *dbg = Debugger::FindDebuggerWithInstanceName (instance_name).get();
        if (dbg != NULL)
        {
            dbg->GetCommandInterpreter().BroadcastEvent (new_event_sp);
        }
    }

    return true;
}

const ConstString
DebuggerInstanceSettings::CreateInstanceName ()
{
    static int instance_count = 1;
    StreamString sstr;

    sstr.Printf ("debugger_%d", instance_count);
    ++instance_count;

    const ConstString ret_val (sstr.GetData());

    return ret_val;
}

const ConstString &
DebuggerInstanceSettings::PromptVarName ()
{
    static ConstString prompt_var_name ("prompt");

    return prompt_var_name;
}

const ConstString &
DebuggerInstanceSettings::GetFrameFormatName ()
{
    static ConstString prompt_var_name ("frame-format");

    return prompt_var_name;
}

const ConstString &
DebuggerInstanceSettings::GetThreadFormatName ()
{
    static ConstString prompt_var_name ("thread-format");

    return prompt_var_name;
}

const ConstString &
DebuggerInstanceSettings::ScriptLangVarName ()
{
    static ConstString script_lang_var_name ("script-lang");

    return script_lang_var_name;
}

const ConstString &
DebuggerInstanceSettings::TermWidthVarName ()
{
    static ConstString term_width_var_name ("term-width");

    return term_width_var_name;
}

const ConstString &
DebuggerInstanceSettings::UseExternalEditorVarName ()
{
    static ConstString use_external_editor_var_name ("use-external-editor");

    return use_external_editor_var_name;
}

const ConstString &
DebuggerInstanceSettings::AutoConfirmName ()
{
    static ConstString use_external_editor_var_name ("auto-confirm");

    return use_external_editor_var_name;
}

//--------------------------------------------------
// SettingsController Variable Tables
//--------------------------------------------------


SettingEntry
Debugger::SettingsController::global_settings_table[] =
{
  //{ "var-name",    var-type,      "default", enum-table, init'd, hidden, "help-text"},
  // The Debugger level global table should always be empty; all Debugger settable variables should be instance
  // variables.
    {  NULL, eSetVarTypeNone, NULL, NULL, 0, 0, NULL }
};

#define MODULE_WITH_FUNC "{ ${module.file.basename}{`${function.name}${function.pc-offset}}}"
#define FILE_AND_LINE "{ at ${line.file.basename}:${line.number}}"

#define DEFAULT_THREAD_FORMAT "thread #${thread.index}: tid = ${thread.id}"\
    "{, ${frame.pc}}"\
    MODULE_WITH_FUNC\
    FILE_AND_LINE\
    "{, stop reason = ${thread.stop-reason}}"\
    "\\n"

//#define DEFAULT_THREAD_FORMAT "thread #${thread.index}: tid = ${thread.id}"\
//    "{, ${frame.pc}}"\
//    MODULE_WITH_FUNC\
//    FILE_AND_LINE\
//    "{, stop reason = ${thread.stop-reason}}"\
//    "{, name = ${thread.name}}"\
//    "{, queue = ${thread.queue}}"\
//    "\\n"

#define DEFAULT_FRAME_FORMAT "frame #${frame.index}: ${frame.pc}"\
    MODULE_WITH_FUNC\
    FILE_AND_LINE\
    "\\n"

SettingEntry
Debugger::SettingsController::instance_settings_table[] =
{
//  NAME                    Setting variable type   Default                 Enum  Init'd Hidden Help
//  ======================= ======================= ======================  ====  ====== ====== ======================
{   "frame-format",         eSetVarTypeString,      DEFAULT_FRAME_FORMAT,   NULL, false, false, "The default frame format string to use when displaying thread information." },
{   "prompt",               eSetVarTypeString,      "(lldb) ",              NULL, false, false, "The debugger command line prompt displayed for the user." },
{   "script-lang",          eSetVarTypeString,      "python",               NULL, false, false, "The script language to be used for evaluating user-written scripts." },
{   "term-width",           eSetVarTypeInt,         "80"    ,               NULL, false, false, "The maximum number of columns to use for displaying text." },
{   "thread-format",        eSetVarTypeString,      DEFAULT_THREAD_FORMAT,  NULL, false, false, "The default thread format string to use when displaying thread information." },
{   "use-external-editor",  eSetVarTypeBoolean,        "false",                NULL, false, false, "Whether to use an external editor or not." },
{   "auto-confirm",         eSetVarTypeBoolean,        "false",                NULL, false, false, "If true all confirmation prompts will receive their default reply." },
{   NULL,                   eSetVarTypeNone,        NULL,                   NULL, false, false, NULL }
};
