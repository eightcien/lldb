//===-- ProcessGDBRemote.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// C Includes
#include <errno.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/stat.h>

// C++ Includes
#include <algorithm>
#include <map>

// Other libraries and framework includes

#include "lldb/Breakpoint/WatchpointLocation.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSpec.h"
#include "lldb/Core/InputReader.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/State.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Core/Timer.h"
#include "lldb/Host/TimeValue.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Utility/PseudoTerminal.h"

// Project includes
#include "lldb/Host/Host.h"
#include "Utility/StringExtractorGDBRemote.h"
#include "GDBRemoteRegisterContext.h"
#include "ProcessGDBRemote.h"
#include "ProcessGDBRemoteLog.h"
#include "ThreadGDBRemote.h"
#include "StopInfoMachException.h"



#define DEBUGSERVER_BASENAME    "debugserver"
using namespace lldb;
using namespace lldb_private;

static inline uint16_t
get_random_port ()
{
    return (arc4random() % (UINT16_MAX - 1000u)) + 1000u;
}


const char *
ProcessGDBRemote::GetPluginNameStatic()
{
    return "process.gdb-remote";
}

const char *
ProcessGDBRemote::GetPluginDescriptionStatic()
{
    return "GDB Remote protocol based debugging plug-in.";
}

void
ProcessGDBRemote::Terminate()
{
    PluginManager::UnregisterPlugin (ProcessGDBRemote::CreateInstance);
}


Process*
ProcessGDBRemote::CreateInstance (Target &target, Listener &listener)
{
    return new ProcessGDBRemote (target, listener);
}

bool
ProcessGDBRemote::CanDebug(Target &target)
{
    // For now we are just making sure the file exists for a given module
    ModuleSP exe_module_sp(target.GetExecutableModule());
    if (exe_module_sp.get())
        return exe_module_sp->GetFileSpec().Exists();
    // However, if there is no executable module, we return true since we might be preparing to attach.
    return true;
}

//----------------------------------------------------------------------
// ProcessGDBRemote constructor
//----------------------------------------------------------------------
ProcessGDBRemote::ProcessGDBRemote(Target& target, Listener &listener) :
    Process (target, listener),
    m_flags (0),
    m_stdio_mutex (Mutex::eMutexTypeRecursive),
    m_gdb_comm(),
    m_debugserver_pid (LLDB_INVALID_PROCESS_ID),
    m_debugserver_thread (LLDB_INVALID_HOST_THREAD),
    m_last_stop_packet (),
    m_register_info (),
    m_async_broadcaster ("lldb.process.gdb-remote.async-broadcaster"),
    m_async_thread (LLDB_INVALID_HOST_THREAD),
    m_curr_tid (LLDB_INVALID_THREAD_ID),
    m_curr_tid_run (LLDB_INVALID_THREAD_ID),
    m_z0_supported (1),
    m_continue_c_tids (),
    m_continue_C_tids (),
    m_continue_s_tids (),
    m_continue_S_tids (),
    m_dispatch_queue_offsets_addr (LLDB_INVALID_ADDRESS),
    m_packet_timeout (1),
    m_max_memory_size (512),
    m_waiting_for_attach (false),
    m_local_debugserver (true),
    m_thread_observation_bps()
{
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ProcessGDBRemote::~ProcessGDBRemote()
{
    if (IS_VALID_LLDB_HOST_THREAD(m_debugserver_thread))
    {
        Host::ThreadCancel (m_debugserver_thread, NULL);
        thread_result_t thread_result;
        Host::ThreadJoin (m_debugserver_thread, &thread_result, NULL);
        m_debugserver_thread = LLDB_INVALID_HOST_THREAD;
    }
    //  m_mach_process.UnregisterNotificationCallbacks (this);
    Clear();
}

//----------------------------------------------------------------------
// PluginInterface
//----------------------------------------------------------------------
const char *
ProcessGDBRemote::GetPluginName()
{
    return "Process debugging plug-in that uses the GDB remote protocol";
}

const char *
ProcessGDBRemote::GetShortPluginName()
{
    return GetPluginNameStatic();
}

uint32_t
ProcessGDBRemote::GetPluginVersion()
{
    return 1;
}

void
ProcessGDBRemote::GetPluginCommandHelp (const char *command, Stream *strm)
{
    strm->Printf("TODO: fill this in\n");
}

Error
ProcessGDBRemote::ExecutePluginCommand (Args &command, Stream *strm)
{
    Error error;
    error.SetErrorString("No plug-in commands are currently supported.");
    return error;
}

Log *
ProcessGDBRemote::EnablePluginLogging (Stream *strm, Args &command)
{
    return NULL;
}

void
ProcessGDBRemote::BuildDynamicRegisterInfo (bool force)
{
    if (!force && m_register_info.GetNumRegisters() > 0)
        return;

    char packet[128];
    m_register_info.Clear();
    StringExtractorGDBRemote::Type packet_type = StringExtractorGDBRemote::eResponse;
    uint32_t reg_offset = 0;
    uint32_t reg_num = 0;
    for (; packet_type == StringExtractorGDBRemote::eResponse; ++reg_num)
    {
        const int packet_len = ::snprintf (packet, sizeof(packet), "qRegisterInfo%x", reg_num);
        assert (packet_len < sizeof(packet));
        StringExtractorGDBRemote response;
        if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, false))
        {
            packet_type = response.GetType();
            if (packet_type == StringExtractorGDBRemote::eResponse)
            {
                std::string name;
                std::string value;
                ConstString reg_name;
                ConstString alt_name;
                ConstString set_name;
                RegisterInfo reg_info = { NULL,                 // Name
                    NULL,                 // Alt name
                    0,                    // byte size
                    reg_offset,           // offset
                    eEncodingUint,        // encoding
                    eFormatHex,           // formate
                    {
                        LLDB_INVALID_REGNUM, // GCC reg num
                        LLDB_INVALID_REGNUM, // DWARF reg num
                        LLDB_INVALID_REGNUM, // generic reg num
                        reg_num,             // GDB reg num
                        reg_num           // native register number
                    }
                };

                while (response.GetNameColonValue(name, value))
                {
                    if (name.compare("name") == 0)
                    {
                        reg_name.SetCString(value.c_str());
                    }
                    else if (name.compare("alt-name") == 0)
                    {
                        alt_name.SetCString(value.c_str());
                    }
                    else if (name.compare("bitsize") == 0)
                    {
                        reg_info.byte_size = Args::StringToUInt32(value.c_str(), 0, 0) / CHAR_BIT;
                    }
                    else if (name.compare("offset") == 0)
                    {
                        uint32_t offset = Args::StringToUInt32(value.c_str(), UINT32_MAX, 0);
                        if (reg_offset != offset)
                        {
                            reg_offset = offset;
                        }
                    }
                    else if (name.compare("encoding") == 0)
                    {
                        if (value.compare("uint") == 0)
                            reg_info.encoding = eEncodingUint;
                        else if (value.compare("sint") == 0)
                            reg_info.encoding = eEncodingSint;
                        else if (value.compare("ieee754") == 0)
                            reg_info.encoding = eEncodingIEEE754;
                        else if (value.compare("vector") == 0)
                            reg_info.encoding = eEncodingVector;
                    }
                    else if (name.compare("format") == 0)
                    {
                        if (value.compare("binary") == 0)
                            reg_info.format = eFormatBinary;
                        else if (value.compare("decimal") == 0)
                            reg_info.format = eFormatDecimal;
                        else if (value.compare("hex") == 0)
                            reg_info.format = eFormatHex;
                        else if (value.compare("float") == 0)
                            reg_info.format = eFormatFloat;
                        else if (value.compare("vector-sint8") == 0)
                            reg_info.format = eFormatVectorOfSInt8;
                        else if (value.compare("vector-uint8") == 0)
                            reg_info.format = eFormatVectorOfUInt8;
                        else if (value.compare("vector-sint16") == 0)
                            reg_info.format = eFormatVectorOfSInt16;
                        else if (value.compare("vector-uint16") == 0)
                            reg_info.format = eFormatVectorOfUInt16;
                        else if (value.compare("vector-sint32") == 0)
                            reg_info.format = eFormatVectorOfSInt32;
                        else if (value.compare("vector-uint32") == 0)
                            reg_info.format = eFormatVectorOfUInt32;
                        else if (value.compare("vector-float32") == 0)
                            reg_info.format = eFormatVectorOfFloat32;
                        else if (value.compare("vector-uint128") == 0)
                            reg_info.format = eFormatVectorOfUInt128;
                    }
                    else if (name.compare("set") == 0)
                    {
                        set_name.SetCString(value.c_str());
                    }
                    else if (name.compare("gcc") == 0)
                    {
                        reg_info.kinds[eRegisterKindGCC] = Args::StringToUInt32(value.c_str(), LLDB_INVALID_REGNUM, 0);
                    }
                    else if (name.compare("dwarf") == 0)
                    {
                        reg_info.kinds[eRegisterKindDWARF] = Args::StringToUInt32(value.c_str(), LLDB_INVALID_REGNUM, 0);
                    }
                    else if (name.compare("generic") == 0)
                    {
                        if (value.compare("pc") == 0)
                            reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
                        else if (value.compare("sp") == 0)
                            reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
                        else if (value.compare("fp") == 0)
                            reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
                        else if (value.compare("ra") == 0)
                            reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
                        else if (value.compare("flags") == 0)
                            reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
                    }
                }

                reg_info.byte_offset = reg_offset;
                assert (reg_info.byte_size != 0);
                reg_offset += reg_info.byte_size;
                m_register_info.AddRegister(reg_info, reg_name, alt_name, set_name);
            }
        }
        else
        {
            packet_type = StringExtractorGDBRemote::eError;
        }
    }

    if (reg_num == 0)
    {
        // We didn't get anything. See if we are debugging ARM and fill with
        // a hard coded register set until we can get an updated debugserver
        // down on the devices.
        if (GetTarget().GetArchitecture().GetMachine() == llvm::Triple::arm)
            m_register_info.HardcodeARMRegisters();
    }
    m_register_info.Finalize ();
}

Error
ProcessGDBRemote::WillLaunch (Module* module)
{
    return WillLaunchOrAttach ();
}

Error
ProcessGDBRemote::WillAttachToProcessWithID (lldb::pid_t pid)
{
    return WillLaunchOrAttach ();
}

Error
ProcessGDBRemote::WillAttachToProcessWithName (const char *process_name, bool wait_for_launch)
{
    return WillLaunchOrAttach ();
}

Error
ProcessGDBRemote::DoConnectRemote (const char *remote_url)
{
    Error error (WillLaunchOrAttach ());
    
    if (error.Fail())
        return error;

    if (strncmp (remote_url, "connect://", strlen ("connect://")) == 0)
    {
        error = ConnectToDebugserver (remote_url);
    }
    else
    {
        error.SetErrorStringWithFormat ("unsupported remote url: %s", remote_url);
    }

    if (error.Fail())
        return error;
    StartAsyncThread ();

    lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID (m_packet_timeout);
    if (pid == LLDB_INVALID_PROCESS_ID)
    {
        // We don't have a valid process ID, so note that we are connected
        // and could now request to launch or attach, or get remote process 
        // listings...
        SetPrivateState (eStateConnected);
    }
    else
    {
        // We have a valid process
        SetID (pid);
        StringExtractorGDBRemote response;
        if (m_gdb_comm.SendPacketAndWaitForResponse("?", 1, response, m_packet_timeout, false))
        {
            const StateType state = SetThreadStopInfo (response);
            if (state == eStateStopped)
            {
                SetPrivateState (state);
            }
            else
                error.SetErrorStringWithFormat ("Process %i was reported after connecting to '%s', but state was not stopped: %s", pid, remote_url, StateAsCString (state));
        }
        else
            error.SetErrorStringWithFormat ("Process %i was reported after connecting to '%s', but no stop reply packet was received", pid, remote_url);
    }
    return error;
}

Error
ProcessGDBRemote::WillLaunchOrAttach ()
{
    Error error;
    m_stdio_communication.Clear ();
    return error;
}

//----------------------------------------------------------------------
// Process Control
//----------------------------------------------------------------------
Error
ProcessGDBRemote::DoLaunch
(
    Module* module,
    char const *argv[],
    char const *envp[],
    uint32_t launch_flags,
    const char *stdin_path,
    const char *stdout_path,
    const char *stderr_path,
    const char *working_dir
)
{
    Error error;
    //  ::LogSetBitMask (GDBR_LOG_DEFAULT);
    //  ::LogSetOptions (LLDB_LOG_OPTION_THREADSAFE | LLDB_LOG_OPTION_PREPEND_TIMESTAMP | LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD);
    //  ::LogSetLogFile ("/dev/stdout");

    ObjectFile * object_file = module->GetObjectFile();
    if (object_file)
    {
        ArchSpec inferior_arch(module->GetArchitecture());
        char host_port[128];
        snprintf (host_port, sizeof(host_port), "localhost:%u", get_random_port ());
        char connect_url[128];
        snprintf (connect_url, sizeof(connect_url), "connect://%s", host_port);

        // Make sure we aren't already connected?
        if (!m_gdb_comm.IsConnected())
        {
            error = StartDebugserverProcess (host_port,
                                             NULL,
                                             NULL,
                                             LLDB_INVALID_PROCESS_ID,
                                             NULL, 
                                             false,
                                             inferior_arch);
            if (error.Fail())
                return error;

            error = ConnectToDebugserver (connect_url);
        }
        
        if (error.Success())
        {
            lldb_utility::PseudoTerminal pty;
            const bool disable_stdio = (launch_flags & eLaunchFlagDisableSTDIO) != 0;

            // If the debugserver is local and we aren't disabling STDIO, lets use
            // a pseudo terminal to instead of relying on the 'O' packets for stdio
            // since 'O' packets can really slow down debugging if the inferior 
            // does a lot of output.
            if (m_local_debugserver && !disable_stdio)
            {
                const char *slave_name = NULL;
                if (stdin_path == NULL || stdout_path == NULL || stderr_path == NULL)
                {
                    if (pty.OpenFirstAvailableMaster(O_RDWR|O_NOCTTY, NULL, 0))
                        slave_name = pty.GetSlaveName (NULL, 0);
                }
                if (stdin_path == NULL) 
                    stdin_path = slave_name;

                if (stdout_path == NULL)
                    stdout_path = slave_name;

                if (stderr_path == NULL)
                    stderr_path = slave_name;
            }

            // Set STDIN to /dev/null if we want STDIO disabled or if either
            // STDOUT or STDERR have been set to something and STDIN hasn't
            if (disable_stdio || (stdin_path == NULL && (stdout_path || stderr_path)))
                stdin_path = "/dev/null";
            
            // Set STDOUT to /dev/null if we want STDIO disabled or if either
            // STDIN or STDERR have been set to something and STDOUT hasn't
            if (disable_stdio || (stdout_path == NULL && (stdin_path || stderr_path)))
                stdout_path = "/dev/null";
            
            // Set STDERR to /dev/null if we want STDIO disabled or if either
            // STDIN or STDOUT have been set to something and STDERR hasn't
            if (disable_stdio || (stderr_path == NULL && (stdin_path || stdout_path)))
                stderr_path = "/dev/null";

            if (stdin_path) 
                m_gdb_comm.SetSTDIN (stdin_path);
            if (stdout_path)
                m_gdb_comm.SetSTDOUT (stdout_path);
            if (stderr_path)
                m_gdb_comm.SetSTDERR (stderr_path);

            m_gdb_comm.SetDisableASLR (launch_flags & eLaunchFlagDisableASLR);

            
            if (working_dir && working_dir[0])
            {
                m_gdb_comm.SetWorkingDir (working_dir);
            }

            // Send the environment and the program + arguments after we connect
            if (envp)
            {
                const char *env_entry;
                for (int i=0; (env_entry = envp[i]); ++i)
                {
                    if (m_gdb_comm.SendEnvironmentPacket(env_entry, m_packet_timeout) != 0)
                        break;
                }
            }

            const uint32_t arg_timeout_seconds = 10;
            int arg_packet_err = m_gdb_comm.SendArgumentsPacket (argv, arg_timeout_seconds);
            if (arg_packet_err == 0)
            {
                std::string error_str;
                if (m_gdb_comm.GetLaunchSuccess (m_packet_timeout, error_str))
                {
                    SetID (m_gdb_comm.GetCurrentProcessID (m_packet_timeout));
                }
                else
                {
                    error.SetErrorString (error_str.c_str());
                }
            }
            else
            {
                error.SetErrorStringWithFormat("'A' packet returned an error: %i.\n", arg_packet_err);
            }
                
            if (GetID() == LLDB_INVALID_PROCESS_ID)
            {
                KillDebugserverProcess ();
                return error;
            }

            StringExtractorGDBRemote response;
            if (m_gdb_comm.SendPacketAndWaitForResponse("?", 1, response, m_packet_timeout, false))
            {
                SetPrivateState (SetThreadStopInfo (response));
                
                if (!disable_stdio)
                {
                    if (pty.GetMasterFileDescriptor() != lldb_utility::PseudoTerminal::invalid_fd)
                        SetUpProcessInputReader (pty.ReleaseMasterFileDescriptor());
                }
            }
        }
    }
    else
    {
        // Set our user ID to an invalid process ID.
        SetID(LLDB_INVALID_PROCESS_ID);
        error.SetErrorStringWithFormat("Failed to get object file from '%s' for arch %s.\n", 
                                       module->GetFileSpec().GetFilename().AsCString(), 
                                       module->GetArchitecture().GetArchitectureName());
    }
    return error;

}


Error
ProcessGDBRemote::ConnectToDebugserver (const char *connect_url)
{
    Error error;
    // Sleep and wait a bit for debugserver to start to listen...
    std::auto_ptr<ConnectionFileDescriptor> conn_ap(new ConnectionFileDescriptor());
    if (conn_ap.get())
    {
        const uint32_t max_retry_count = 50;
        uint32_t retry_count = 0;
        while (!m_gdb_comm.IsConnected())
        {
            if (conn_ap->Connect(connect_url, &error) == eConnectionStatusSuccess)
            {
                m_gdb_comm.SetConnection (conn_ap.release());
                break;
            }
            retry_count++;

            if (retry_count >= max_retry_count)
                break;

            usleep (100000);
        }
    }

    if (!m_gdb_comm.IsConnected())
    {
        if (error.Success())
            error.SetErrorString("not connected to remote gdb server");
        return error;
    }

    if (m_gdb_comm.StartReadThread(&error))
    {
        // Send an initial ack
        m_gdb_comm.SendAck();

        if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID)
            m_debugserver_thread = Host::StartMonitoringChildProcess (MonitorDebugserverProcess,
                                                                      this,
                                                                      m_debugserver_pid,
                                                                      false);
        
        m_gdb_comm.ResetDiscoverableSettings();
        m_gdb_comm.GetSendAcks ();
        m_gdb_comm.GetThreadSuffixSupported ();
        m_gdb_comm.GetHostInfo ();
        m_gdb_comm.GetVContSupported ('c');
    }
    return error;
}

void
ProcessGDBRemote::DidLaunchOrAttach ()
{
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::DidLaunch()");
    if (GetID() != LLDB_INVALID_PROCESS_ID)
    {
        m_dispatch_queue_offsets_addr = LLDB_INVALID_ADDRESS;

        BuildDynamicRegisterInfo (false);

        m_target.GetArchitecture().SetByteOrder (m_gdb_comm.GetByteOrder());

        StreamString strm;

        // See if the GDB server supports the qHostInfo information
        const char *vendor = m_gdb_comm.GetVendorString().AsCString();
        const char *os_type = m_gdb_comm.GetOSString().AsCString();
        ArchSpec target_arch (GetTarget().GetArchitecture());
        ArchSpec gdb_remote_arch (m_gdb_comm.GetHostArchitecture());

        // If the remote host is ARM and we have apple as the vendor, then 
        // ARM executables and shared libraries can have mixed ARM architectures.
        // You can have an armv6 executable, and if the host is armv7, then the
        // system will load the best possible architecture for all shared libraries
        // it has, so we really need to take the remote host architecture as our
        // defacto architecture in this case.

        if (gdb_remote_arch.GetMachine() == llvm::Triple::arm &&
            gdb_remote_arch.GetTriple().getVendor() == llvm::Triple::Apple)
        {
            GetTarget().SetArchitecture (gdb_remote_arch);
            target_arch = gdb_remote_arch;
        }
        
        if (vendor)
            m_target.GetArchitecture().GetTriple().setVendorName(vendor);
        if (os_type)
            m_target.GetArchitecture().GetTriple().setOSName(os_type);
    }
}

void
ProcessGDBRemote::DidLaunch ()
{
    DidLaunchOrAttach ();
}

Error
ProcessGDBRemote::DoAttachToProcessWithID (lldb::pid_t attach_pid)
{
    Error error;
    // Clear out and clean up from any current state
    Clear();
    const ArchSpec &arch_spec = GetTarget().GetArchitecture();

    if (attach_pid != LLDB_INVALID_PROCESS_ID)
    {
        // Make sure we aren't already connected?
        if (!m_gdb_comm.IsConnected())
        {
            char host_port[128];
            snprintf (host_port, sizeof(host_port), "localhost:%u", get_random_port ());
            char connect_url[128];
            snprintf (connect_url, sizeof(connect_url), "connect://%s", host_port);

            error = StartDebugserverProcess (host_port,                 // debugserver_url
                                             NULL,                      // inferior_argv
                                             NULL,                      // inferior_envp
                                             LLDB_INVALID_PROCESS_ID,   // Don't send any attach to pid options to debugserver
                                             NULL,                      // Don't send any attach by process name option to debugserver
                                             false,                     // Don't send any attach wait_for_launch flag as an option to debugserver
                                             arch_spec);
            
            if (error.Fail())
            {
                const char *error_string = error.AsCString();
                if (error_string == NULL)
                    error_string = "unable to launch " DEBUGSERVER_BASENAME;

                SetExitStatus (-1, error_string);
            }
            else
            {
                error = ConnectToDebugserver (connect_url);
            }
        }
    
        if (error.Success())
        {
            char packet[64];
            const int packet_len = ::snprintf (packet, sizeof(packet), "vAttach;%x", attach_pid);
            
            m_async_broadcaster.BroadcastEvent (eBroadcastBitAsyncContinue, new EventDataBytes (packet, packet_len));
        }
    }
    return error;
}

size_t
ProcessGDBRemote::AttachInputReaderCallback
(
    void *baton, 
    InputReader *reader, 
    lldb::InputReaderAction notification,
    const char *bytes, 
    size_t bytes_len
)
{
    if (notification == eInputReaderGotToken)
    {
        ProcessGDBRemote *gdb_process = (ProcessGDBRemote *)baton;
        if (gdb_process->m_waiting_for_attach)
            gdb_process->m_waiting_for_attach = false;
        reader->SetIsDone(true);
        return 1;
    }
    return 0;
}

Error
ProcessGDBRemote::DoAttachToProcessWithName (const char *process_name, bool wait_for_launch)
{
    Error error;
    // Clear out and clean up from any current state
    Clear();

    if (process_name && process_name[0])
    {
        // Make sure we aren't already connected?
        if (!m_gdb_comm.IsConnected())
        {

            const ArchSpec &arch_spec = GetTarget().GetArchitecture();

            char host_port[128];
            snprintf (host_port, sizeof(host_port), "localhost:%u", get_random_port ());
            char connect_url[128];
            snprintf (connect_url, sizeof(connect_url), "connect://%s", host_port);

            error = StartDebugserverProcess (host_port,                 // debugserver_url
                                             NULL,                      // inferior_argv
                                             NULL,                      // inferior_envp
                                             LLDB_INVALID_PROCESS_ID,   // Don't send any attach to pid options to debugserver
                                             NULL,                      // Don't send any attach by process name option to debugserver
                                             false,                     // Don't send any attach wait_for_launch flag as an option to debugserver
                                             arch_spec);
            if (error.Fail())
            {
                const char *error_string = error.AsCString();
                if (error_string == NULL)
                    error_string = "unable to launch " DEBUGSERVER_BASENAME;

                SetExitStatus (-1, error_string);
            }
            else
            {
                error = ConnectToDebugserver (connect_url);
            }
        }

        if (error.Success())
        {
            StreamString packet;
            
            if (wait_for_launch)
                packet.PutCString("vAttachWait");
            else
                packet.PutCString("vAttachName");
            packet.PutChar(';');
            packet.PutBytesAsRawHex8(process_name, strlen(process_name), lldb::endian::InlHostByteOrder(), lldb::endian::InlHostByteOrder());
            
            m_async_broadcaster.BroadcastEvent (eBroadcastBitAsyncContinue, new EventDataBytes (packet.GetData(), packet.GetSize()));

        }
    }
    return error;
}


void
ProcessGDBRemote::DidAttach ()
{
    DidLaunchOrAttach ();
}

Error
ProcessGDBRemote::WillResume ()
{
    m_continue_c_tids.clear();
    m_continue_C_tids.clear();
    m_continue_s_tids.clear();
    m_continue_S_tids.clear();
    return Error();
}

Error
ProcessGDBRemote::DoResume ()
{
    Error error;
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::Resume()");
    
    Listener listener ("gdb-remote.resume-packet-sent");
    if (listener.StartListeningForEvents (&m_gdb_comm, GDBRemoteCommunication::eBroadcastBitRunPacketSent))
    {
        StreamString continue_packet;
        bool continue_packet_error = false;
        if (m_gdb_comm.HasAnyVContSupport ())
        {
            continue_packet.PutCString ("vCont");
        
            if (!m_continue_c_tids.empty())
            {
                if (m_gdb_comm.GetVContSupported ('c'))
                {
                    for (tid_collection::const_iterator t_pos = m_continue_c_tids.begin(), t_end = m_continue_c_tids.end(); t_pos != t_end; ++t_pos)
                        continue_packet.Printf(";c:%4.4x", *t_pos);
                }
                else 
                    continue_packet_error = true;
            }
            
            if (!continue_packet_error && !m_continue_C_tids.empty())
            {
                if (m_gdb_comm.GetVContSupported ('C'))
                {
                    for (tid_sig_collection::const_iterator s_pos = m_continue_C_tids.begin(), s_end = m_continue_C_tids.end(); s_pos != s_end; ++s_pos)
                        continue_packet.Printf(";C%2.2x:%4.4x", s_pos->second, s_pos->first);
                }
                else 
                    continue_packet_error = true;
            }

            if (!continue_packet_error && !m_continue_s_tids.empty())
            {
                if (m_gdb_comm.GetVContSupported ('s'))
                {
                    for (tid_collection::const_iterator t_pos = m_continue_s_tids.begin(), t_end = m_continue_s_tids.end(); t_pos != t_end; ++t_pos)
                        continue_packet.Printf(";s:%4.4x", *t_pos);
                }
                else 
                    continue_packet_error = true;
            }
            
            if (!continue_packet_error && !m_continue_S_tids.empty())
            {
                if (m_gdb_comm.GetVContSupported ('S'))
                {
                    for (tid_sig_collection::const_iterator s_pos = m_continue_S_tids.begin(), s_end = m_continue_S_tids.end(); s_pos != s_end; ++s_pos)
                        continue_packet.Printf(";S%2.2x:%4.4x", s_pos->second, s_pos->first);
                }
                else
                    continue_packet_error = true;
            }
            
            if (continue_packet_error)
                continue_packet.GetString().clear();
        }
        else
            continue_packet_error = true;
        
        if (continue_packet_error)
        {
            continue_packet_error = false;
            // Either no vCont support, or we tried to use part of the vCont
            // packet that wasn't supported by the remote GDB server.
            // We need to try and make a simple packet that can do our continue
            const size_t num_threads = GetThreadList().GetSize();
            const size_t num_continue_c_tids = m_continue_c_tids.size();
            const size_t num_continue_C_tids = m_continue_C_tids.size();
            const size_t num_continue_s_tids = m_continue_s_tids.size();
            const size_t num_continue_S_tids = m_continue_S_tids.size();
            if (num_continue_c_tids > 0)
            {
                if (num_continue_c_tids == num_threads)
                {
                    // All threads are resuming...
                    SetCurrentGDBRemoteThreadForRun (-1);
                    continue_packet.PutChar ('c');                
                }
                else if (num_continue_c_tids == 1 &&
                         num_continue_C_tids == 0 && 
                         num_continue_s_tids == 0 && 
                         num_continue_S_tids == 0 )
                {
                    // Only one thread is continuing
                    SetCurrentGDBRemoteThreadForRun (m_continue_c_tids.front());
                    continue_packet.PutChar ('c');                
                }
                else
                {
                    // We can't represent this continue packet....
                    continue_packet_error = true;
                }
            }

            if (!continue_packet_error && num_continue_C_tids > 0)
            {
                if (num_continue_C_tids == num_threads)
                {
                    const int continue_signo = m_continue_C_tids.front().second;
                    if (num_continue_C_tids > 1)
                    {
                        for (size_t i=1; i<num_threads; ++i)
                        {
                            if (m_continue_C_tids[i].second != continue_signo)
                                continue_packet_error = true;
                        }
                    }
                    if (!continue_packet_error)
                    {
                        // Add threads continuing with the same signo...
                        SetCurrentGDBRemoteThreadForRun (-1);
                        continue_packet.Printf("C%2.2x", continue_signo);
                    }
                }
                else if (num_continue_c_tids == 0 &&
                         num_continue_C_tids == 1 && 
                         num_continue_s_tids == 0 && 
                         num_continue_S_tids == 0 )
                {
                    // Only one thread is continuing with signal
                    SetCurrentGDBRemoteThreadForRun (m_continue_C_tids.front().first);
                    continue_packet.Printf("C%2.2x", m_continue_C_tids.front().second);
                }
                else
                {
                    // We can't represent this continue packet....
                    continue_packet_error = true;
                }
            }

            if (!continue_packet_error && num_continue_s_tids > 0)
            {
                if (num_continue_s_tids == num_threads)
                {
                    // All threads are resuming...
                    SetCurrentGDBRemoteThreadForRun (-1);
                    continue_packet.PutChar ('s');                
                }
                else if (num_continue_c_tids == 0 &&
                         num_continue_C_tids == 0 && 
                         num_continue_s_tids == 1 && 
                         num_continue_S_tids == 0 )
                {
                    // Only one thread is stepping
                    SetCurrentGDBRemoteThreadForRun (m_continue_s_tids.front());
                    continue_packet.PutChar ('s');                
                }
                else
                {
                    // We can't represent this continue packet....
                    continue_packet_error = true;
                }
            }

            if (!continue_packet_error && num_continue_S_tids > 0)
            {
                if (num_continue_S_tids == num_threads)
                {
                    const int step_signo = m_continue_S_tids.front().second;
                    // Are all threads trying to step with the same signal?
                    if (num_continue_S_tids > 1)
                    {
                        for (size_t i=1; i<num_threads; ++i)
                        {
                            if (m_continue_S_tids[i].second != step_signo)
                                continue_packet_error = true;
                        }
                    }
                    if (!continue_packet_error)
                    {
                        // Add threads stepping with the same signo...
                        SetCurrentGDBRemoteThreadForRun (-1);
                        continue_packet.Printf("S%2.2x", step_signo);
                    }
                }
                else if (num_continue_c_tids == 0 &&
                         num_continue_C_tids == 0 && 
                         num_continue_s_tids == 0 && 
                         num_continue_S_tids == 1 )
                {
                    // Only one thread is stepping with signal
                    SetCurrentGDBRemoteThreadForRun (m_continue_S_tids.front().first);
                    continue_packet.Printf("S%2.2x", m_continue_S_tids.front().second);
                }
                else
                {
                    // We can't represent this continue packet....
                    continue_packet_error = true;
                }
            }
        }

        if (continue_packet_error)
        {
            error.SetErrorString ("can't make continue packet for this resume");
        }
        else
        {
            EventSP event_sp;
            TimeValue timeout;
            timeout = TimeValue::Now();
            timeout.OffsetWithSeconds (5);
            m_async_broadcaster.BroadcastEvent (eBroadcastBitAsyncContinue, new EventDataBytes (continue_packet.GetData(), continue_packet.GetSize()));

            if (listener.WaitForEvent (&timeout, event_sp) == false)
                error.SetErrorString("Resume timed out.");
        }
    }

    return error;
}

size_t
ProcessGDBRemote::GetSoftwareBreakpointTrapOpcode (BreakpointSite* bp_site)
{
    const uint8_t *trap_opcode = NULL;
    uint32_t trap_opcode_size = 0;

    static const uint8_t g_arm_breakpoint_opcode[] = { 0xFE, 0xDE, 0xFF, 0xE7 };
    //static const uint8_t g_thumb_breakpooint_opcode[] = { 0xFE, 0xDE };
    static const uint8_t g_ppc_breakpoint_opcode[] = { 0x7F, 0xC0, 0x00, 0x08 };
    static const uint8_t g_i386_breakpoint_opcode[] = { 0xCC };

    const llvm::Triple::ArchType machine = GetTarget().GetArchitecture().GetMachine();
    switch (machine)
    {
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
        trap_opcode = g_i386_breakpoint_opcode;
        trap_opcode_size = sizeof(g_i386_breakpoint_opcode);
        break;
    
    case llvm::Triple::arm:
        // TODO: fill this in for ARM. We need to dig up the symbol for
        // the address in the breakpoint locaiton and figure out if it is
        // an ARM or Thumb breakpoint.
        trap_opcode = g_arm_breakpoint_opcode;
        trap_opcode_size = sizeof(g_arm_breakpoint_opcode);
        break;
    
    case llvm::Triple::ppc:
    case llvm::Triple::ppc64:
        trap_opcode = g_ppc_breakpoint_opcode;
        trap_opcode_size = sizeof(g_ppc_breakpoint_opcode);
        break;

    default:
        assert(!"Unhandled architecture in ProcessMacOSX::GetSoftwareBreakpointTrapOpcode()");
        break;
    }

    if (trap_opcode && trap_opcode_size)
    {
        if (bp_site->SetTrapOpcode(trap_opcode, trap_opcode_size))
            return trap_opcode_size;
    }
    return 0;
}

uint32_t
ProcessGDBRemote::UpdateThreadListIfNeeded ()
{
    // locker will keep a mutex locked until it goes out of scope
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_THREAD));
    if (log && log->GetMask().Test(GDBR_LOG_VERBOSE))
        log->Printf ("ProcessGDBRemote::%s (pid = %i)", __FUNCTION__, GetID());

    Mutex::Locker locker (m_thread_list.GetMutex ());
    const uint32_t stop_id = GetStopID();
    if (m_thread_list.GetSize(false) == 0 || stop_id != m_thread_list.GetStopID())
    {
        // Update the thread list's stop id immediately so we don't recurse into this function.
        ThreadList curr_thread_list (this);
        curr_thread_list.SetStopID(stop_id);

        Error err;
        StringExtractorGDBRemote response;
        for (m_gdb_comm.SendPacketAndWaitForResponse("qfThreadInfo", response, 1, false);
             response.IsNormalPacket();
             m_gdb_comm.SendPacketAndWaitForResponse("qsThreadInfo", response, 1, false))
        {
            char ch = response.GetChar();
            if (ch == 'l')
                break;
            if (ch == 'm')
            {
                do
                {
                    tid_t tid = response.GetHexMaxU32(false, LLDB_INVALID_THREAD_ID);

                    if (tid != LLDB_INVALID_THREAD_ID)
                    {
                        ThreadSP thread_sp (GetThreadList().FindThreadByID (tid, false));
                        if (!thread_sp)
                            thread_sp.reset (new ThreadGDBRemote (*this, tid));
                        curr_thread_list.AddThread(thread_sp);
                    }

                    ch = response.GetChar();
                } while (ch == ',');
            }
        }

        m_thread_list = curr_thread_list;

        SetThreadStopInfo (m_last_stop_packet);
    }
    return GetThreadList().GetSize(false);
}


StateType
ProcessGDBRemote::SetThreadStopInfo (StringExtractor& stop_packet)
{
    const char stop_type = stop_packet.GetChar();
    switch (stop_type)
    {
    case 'T':
    case 'S':
        {
            if (GetStopID() == 0)
            {
                // Our first stop, make sure we have a process ID, and also make
                // sure we know about our registers
                if (GetID() == LLDB_INVALID_PROCESS_ID)
                {
                    lldb::pid_t pid = m_gdb_comm.GetCurrentProcessID (1);
                    if (pid != LLDB_INVALID_PROCESS_ID)
                        SetID (pid);
                }
                BuildDynamicRegisterInfo (true);
            }
            // Stop with signal and thread info
            const uint8_t signo = stop_packet.GetHexU8();
            std::string name;
            std::string value;
            std::string thread_name;
            uint32_t exc_type = 0;
            std::vector<addr_t> exc_data;
            uint32_t tid = LLDB_INVALID_THREAD_ID;
            addr_t thread_dispatch_qaddr = LLDB_INVALID_ADDRESS;
            uint32_t exc_data_count = 0;
            ThreadSP thread_sp;

            while (stop_packet.GetNameColonValue(name, value))
            {
                if (name.compare("metype") == 0)
                {
                    // exception type in big endian hex
                    exc_type = Args::StringToUInt32 (value.c_str(), 0, 16);
                }
                else if (name.compare("mecount") == 0)
                {
                    // exception count in big endian hex
                    exc_data_count = Args::StringToUInt32 (value.c_str(), 0, 16);
                }
                else if (name.compare("medata") == 0)
                {
                    // exception data in big endian hex
                    exc_data.push_back(Args::StringToUInt64 (value.c_str(), 0, 16));
                }
                else if (name.compare("thread") == 0)
                {
                    // thread in big endian hex
                    tid = Args::StringToUInt32 (value.c_str(), 0, 16);
                    Mutex::Locker locker (m_thread_list.GetMutex ());
                    thread_sp = m_thread_list.FindThreadByID(tid, false);
                    if (!thread_sp)
                    {
                        // Create the thread if we need to
                        thread_sp.reset (new ThreadGDBRemote (*this, tid));
                        m_thread_list.AddThread(thread_sp);
                    }
                }
                else if (name.compare("hexname") == 0)
                {
                    StringExtractor name_extractor;
                    // Swap "value" over into "name_extractor"
                    name_extractor.GetStringRef().swap(value);
                    // Now convert the HEX bytes into a string value
                    name_extractor.GetHexByteString (value);
                    thread_name.swap (value);
                }
                else if (name.compare("name") == 0)
                {
                    thread_name.swap (value);
                }
                else if (name.compare("qaddr") == 0)
                {
                    thread_dispatch_qaddr = Args::StringToUInt64 (value.c_str(), 0, 16);
                }
                else if (name.size() == 2 && ::isxdigit(name[0]) && ::isxdigit(name[1]))
                {
                    // We have a register number that contains an expedited
                    // register value. Lets supply this register to our thread
                    // so it won't have to go and read it.
                    if (thread_sp)
                    {
                        uint32_t reg = Args::StringToUInt32 (name.c_str(), UINT32_MAX, 16);

                        if (reg != UINT32_MAX)
                        {
                            StringExtractor reg_value_extractor;
                            // Swap "value" over into "reg_value_extractor"
                            reg_value_extractor.GetStringRef().swap(value);
                            if (!static_cast<ThreadGDBRemote *> (thread_sp.get())->PrivateSetRegisterValue (reg, reg_value_extractor))
                            {
                                Host::SetCrashDescriptionWithFormat("Setting thread register '%s' (decoded to %u (0x%x)) with value '%s' for stop packet: '%s'", 
                                                                    name.c_str(), 
                                                                    reg, 
                                                                    reg, 
                                                                    reg_value_extractor.GetStringRef().c_str(), 
                                                                    stop_packet.GetStringRef().c_str());
                            }
                        }
                    }
                }
            }

            if (thread_sp)
            {
                ThreadGDBRemote *gdb_thread = static_cast<ThreadGDBRemote *> (thread_sp.get());

                gdb_thread->SetThreadDispatchQAddr (thread_dispatch_qaddr);
                gdb_thread->SetName (thread_name.empty() ? NULL : thread_name.c_str());
                if (exc_type != 0)
                {
                    const size_t exc_data_size = exc_data.size();

                    gdb_thread->SetStopInfo (StopInfoMachException::CreateStopReasonWithMachException (*thread_sp,
                                                                                                       exc_type, 
                                                                                                       exc_data_size,
                                                                                                       exc_data_size >= 1 ? exc_data[0] : 0,
                                                                                                       exc_data_size >= 2 ? exc_data[1] : 0));
                }
                else if (signo)
                {
                    gdb_thread->SetStopInfo (StopInfo::CreateStopReasonWithSignal (*thread_sp, signo));
                }
                else
                {
                    StopInfoSP invalid_stop_info_sp;
                    gdb_thread->SetStopInfo (invalid_stop_info_sp);
                }
            }
            return eStateStopped;
        }
        break;

    case 'W':
        // process exited
        return eStateExited;

    default:
        break;
    }
    return eStateInvalid;
}

void
ProcessGDBRemote::RefreshStateAfterStop ()
{
    // FIXME - add a variable to tell that we're in the middle of attaching if we
    // need to know that.
    // We must be attaching if we don't already have a valid architecture
//    if (!GetTarget().GetArchitecture().IsValid())
//    {
//        Module *exe_module = GetTarget().GetExecutableModule().get();
//        if (exe_module)
//            m_arch_spec = exe_module->GetArchitecture();
//    }
    
    // Let all threads recover from stopping and do any clean up based
    // on the previous thread state (if any).
    m_thread_list.RefreshStateAfterStop();

    // Discover new threads:
    UpdateThreadListIfNeeded ();
}

Error
ProcessGDBRemote::DoHalt (bool &caused_stop)
{
    Error error;

    bool timed_out = false;
    Mutex::Locker locker;
    
    if (m_public_state.GetValue() == eStateAttaching)
    {
        // We are being asked to halt during an attach. We need to just close
        // our file handle and debugserver will go away, and we can be done...
        m_gdb_comm.Disconnect();
    }
    else
    {
        if (!m_gdb_comm.SendInterrupt (locker, 2, caused_stop, timed_out))
        {
            if (timed_out)
                error.SetErrorString("timed out sending interrupt packet");
            else
                error.SetErrorString("unknown error sending interrupt packet");
        }
    }
    return error;
}

Error
ProcessGDBRemote::InterruptIfRunning 
(
    bool discard_thread_plans, 
    bool catch_stop_event, 
    EventSP &stop_event_sp
)
{
    Error error;

    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    
    bool paused_private_state_thread = false;
    const bool is_running = m_gdb_comm.IsRunning();
    if (log)
        log->Printf ("ProcessGDBRemote::InterruptIfRunning(discard_thread_plans=%i, catch_stop_event=%i) is_running=%i", 
                     discard_thread_plans, 
                     catch_stop_event,
                     is_running);

    if (discard_thread_plans)
    {
        if (log)
            log->Printf ("ProcessGDBRemote::InterruptIfRunning() discarding all thread plans");
        m_thread_list.DiscardThreadPlans();
    }
    if (is_running)
    {
        if (catch_stop_event)
        {
            if (log)
                log->Printf ("ProcessGDBRemote::InterruptIfRunning() pausing private state thread");
            PausePrivateStateThread();
            paused_private_state_thread = true;
        }

        bool timed_out = false;
        bool sent_interrupt = false;
        Mutex::Locker locker;
        
        if (!m_gdb_comm.SendInterrupt (locker, 1, sent_interrupt, timed_out))
        {
            if (timed_out)
                error.SetErrorString("timed out sending interrupt packet");
            else
                error.SetErrorString("unknown error sending interrupt packet");
            if (paused_private_state_thread)
                ResumePrivateStateThread();
            return error;
        }
        
        if (catch_stop_event)
        {
            // LISTEN HERE
            TimeValue timeout_time;
            timeout_time = TimeValue::Now();
            timeout_time.OffsetWithSeconds(5);
            StateType state = WaitForStateChangedEventsPrivate (&timeout_time, stop_event_sp);
    
            timed_out = state == eStateInvalid;
            if (log)
                log->Printf ("ProcessGDBRemote::InterruptIfRunning() catch stop event: state = %s, timed-out=%i", StateAsCString(state), timed_out);

            if (timed_out)
                error.SetErrorString("unable to verify target stopped");
        }
        
        if (paused_private_state_thread)
        {
            if (log)
                log->Printf ("ProcessGDBRemote::InterruptIfRunning() resuming private state thread");
            ResumePrivateStateThread();
        }
    }
    return error;
}

Error
ProcessGDBRemote::WillDetach ()
{
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::WillDetach()");

    bool discard_thread_plans = true; 
    bool catch_stop_event = true;
    EventSP event_sp;
    return InterruptIfRunning (discard_thread_plans, catch_stop_event, event_sp);
}

Error
ProcessGDBRemote::DoDetach()
{
    Error error;
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::DoDetach()");

    DisableAllBreakpointSites ();

    m_thread_list.DiscardThreadPlans();

    size_t response_size = m_gdb_comm.SendPacket ("D", 1);
    if (log)
    {
        if (response_size)
            log->PutCString ("ProcessGDBRemote::DoDetach() detach packet sent successfully");
        else
            log->PutCString ("ProcessGDBRemote::DoDetach() detach packet send failed");
    }
    // Sleep for one second to let the process get all detached...
    StopAsyncThread ();

    m_gdb_comm.StopReadThread();
    m_gdb_comm.Disconnect();    // Disconnect from the debug server.

    SetPrivateState (eStateDetached);
    ResumePrivateStateThread();

    //KillDebugserverProcess ();
    return error;
}

Error
ProcessGDBRemote::DoDestroy ()
{
    Error error;
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::DoDestroy()");

    // Interrupt if our inferior is running...
    if (m_gdb_comm.IsConnected())
    {
        if (m_public_state.GetValue() == eStateAttaching)
        {
            // We are being asked to halt during an attach. We need to just close
            // our file handle and debugserver will go away, and we can be done...
            m_gdb_comm.Disconnect();
        }
        else
        {

            StringExtractorGDBRemote response;
            bool send_async = true;
            if (m_gdb_comm.SendPacketAndWaitForResponse("k", 1, response, 2, send_async))
            {
                char packet_cmd = response.GetChar(0);

                if (packet_cmd == 'W' || packet_cmd == 'X')
                {
                    m_last_stop_packet = response;
                    SetExitStatus(response.GetHexU8(), NULL);
                }
            }
            else
            {
                SetExitStatus(SIGABRT, NULL);
                //error.SetErrorString("kill packet failed");
            }
        }
    }
    StopAsyncThread ();
    m_gdb_comm.StopReadThread();
    KillDebugserverProcess ();
    m_gdb_comm.Disconnect();    // Disconnect from the debug server.
    return error;
}

//------------------------------------------------------------------
// Process Queries
//------------------------------------------------------------------

bool
ProcessGDBRemote::IsAlive ()
{
    return m_gdb_comm.IsConnected() && m_private_state.GetValue() != eStateExited;
}

addr_t
ProcessGDBRemote::GetImageInfoAddress()
{
    if (!m_gdb_comm.IsRunning())
    {
        StringExtractorGDBRemote response;
        if (m_gdb_comm.SendPacketAndWaitForResponse("qShlibInfoAddr", ::strlen ("qShlibInfoAddr"), response, 2, false))
        {
            if (response.IsNormalPacket())
                return response.GetHexMaxU64(false, LLDB_INVALID_ADDRESS);
        }
    }
    return LLDB_INVALID_ADDRESS;
}

//------------------------------------------------------------------
// Process Memory
//------------------------------------------------------------------
size_t
ProcessGDBRemote::DoReadMemory (addr_t addr, void *buf, size_t size, Error &error)
{
    if (size > m_max_memory_size)
    {
        // Keep memory read sizes down to a sane limit. This function will be
        // called multiple times in order to complete the task by 
        // lldb_private::Process so it is ok to do this.
        size = m_max_memory_size;
    }

    char packet[64];
    const int packet_len = ::snprintf (packet, sizeof(packet), "m%llx,%zx", (uint64_t)addr, size);
    assert (packet_len + 1 < sizeof(packet));
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, true))
    {
        if (response.IsNormalPacket())
        {
            error.Clear();
            return response.GetHexBytes(buf, size, '\xdd');
        }
        else if (response.IsErrorPacket())
            error.SetErrorStringWithFormat("gdb remote returned an error: %s", response.GetStringRef().c_str());
        else if (response.IsUnsupportedPacket())
            error.SetErrorStringWithFormat("'%s' packet unsupported", packet);
        else
            error.SetErrorStringWithFormat("unexpected response to '%s': '%s'", packet, response.GetStringRef().c_str());
    }
    else
    {
        error.SetErrorStringWithFormat("failed to sent packet: '%s'", packet);
    }
    return 0;
}

size_t
ProcessGDBRemote::DoWriteMemory (addr_t addr, const void *buf, size_t size, Error &error)
{
    StreamString packet;
    packet.Printf("M%llx,%zx:", addr, size);
    packet.PutBytesAsRawHex8(buf, size, lldb::endian::InlHostByteOrder(), lldb::endian::InlHostByteOrder());
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet.GetData(), packet.GetSize(), response, 2, true))
    {
        if (response.IsOKPacket())
        {
            error.Clear();
            return size;
        }
        else if (response.IsErrorPacket())
            error.SetErrorStringWithFormat("gdb remote returned an error: %s", response.GetStringRef().c_str());
        else if (response.IsUnsupportedPacket())
            error.SetErrorStringWithFormat("'%s' packet unsupported", packet.GetString().c_str());
        else
            error.SetErrorStringWithFormat("unexpected response to '%s': '%s'", packet.GetString().c_str(), response.GetStringRef().c_str());
    }
    else
    {
        error.SetErrorStringWithFormat("failed to sent packet: '%s'", packet.GetString().c_str());
    }
    return 0;
}

lldb::addr_t
ProcessGDBRemote::DoAllocateMemory (size_t size, uint32_t permissions, Error &error)
{
    addr_t allocated_addr = m_gdb_comm.AllocateMemory (size, permissions, m_packet_timeout);
    if (allocated_addr == LLDB_INVALID_ADDRESS)
        error.SetErrorStringWithFormat("unable to allocate %zu bytes of memory with permissions %u", size, permissions);
    else
        error.Clear();
    return allocated_addr;
}

Error
ProcessGDBRemote::DoDeallocateMemory (lldb::addr_t addr)
{
    Error error; 
    if (!m_gdb_comm.DeallocateMemory (addr, m_packet_timeout))
        error.SetErrorStringWithFormat("unable to deallocate memory at 0x%llx", addr);
    return error;
}


//------------------------------------------------------------------
// Process STDIO
//------------------------------------------------------------------

size_t
ProcessGDBRemote::GetSTDOUT (char *buf, size_t buf_size, Error &error)
{
    Mutex::Locker locker(m_stdio_mutex);
    size_t bytes_available = m_stdout_data.size();
    if (bytes_available > 0)
    {
        LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));
        if (log)
            log->Printf ("ProcessGDBRemote::%s (&%p[%u]) ...", __FUNCTION__, buf, buf_size);
        if (bytes_available > buf_size)
        {
            memcpy(buf, m_stdout_data.c_str(), buf_size);
            m_stdout_data.erase(0, buf_size);
            bytes_available = buf_size;
        }
        else
        {
            memcpy(buf, m_stdout_data.c_str(), bytes_available);
            m_stdout_data.clear();

            //ResetEventBits(eBroadcastBitSTDOUT);
        }
    }
    return bytes_available;
}

size_t
ProcessGDBRemote::GetSTDERR (char *buf, size_t buf_size, Error &error)
{
    // Can we get STDERR through the remote protocol?
    return 0;
}

size_t
ProcessGDBRemote::PutSTDIN (const char *src, size_t src_len, Error &error)
{
    if (m_stdio_communication.IsConnected())
    {
        ConnectionStatus status;
        m_stdio_communication.Write(src, src_len, status, NULL);
    }
    return 0;
}

Error
ProcessGDBRemote::EnableBreakpoint (BreakpointSite *bp_site)
{
    Error error;
    assert (bp_site != NULL);

    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_BREAKPOINTS));
    user_id_t site_id = bp_site->GetID();
    const addr_t addr = bp_site->GetLoadAddress();
    if (log)
        log->Printf ("ProcessGDBRemote::EnableBreakpoint (size_id = %d) address = 0x%llx", site_id, (uint64_t)addr);

    if (bp_site->IsEnabled())
    {
        if (log)
            log->Printf ("ProcessGDBRemote::EnableBreakpoint (size_id = %d) address = 0x%llx -- SUCCESS (already enabled)", site_id, (uint64_t)addr);
        return error;
    }
    else
    {
        const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode (bp_site);

        if (bp_site->HardwarePreferred())
        {
            // Try and set hardware breakpoint, and if that fails, fall through
            // and set a software breakpoint?
        }

        if (m_z0_supported)
        {
            char packet[64];
            const int packet_len = ::snprintf (packet, sizeof(packet), "Z0,%llx,%zx", addr, bp_op_size);
            assert (packet_len + 1 < sizeof(packet));
            StringExtractorGDBRemote response;
            if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, true))
            {
                if (response.IsUnsupportedPacket())
                {
                    // Disable z packet support and try again
                    m_z0_supported = 0;
                    return EnableBreakpoint (bp_site);
                }
                else if (response.IsOKPacket())
                {
                    bp_site->SetEnabled(true);
                    bp_site->SetType (BreakpointSite::eExternal);
                    return error;
                }
                else
                {
                    uint8_t error_byte = response.GetError();
                    if (error_byte)
                        error.SetErrorStringWithFormat("%x packet failed with error: %i (0x%2.2x).\n", packet, error_byte, error_byte);
                }
            }
        }
        else
        {
            return EnableSoftwareBreakpoint (bp_site);
        }
    }

    if (log)
    {
        const char *err_string = error.AsCString();
        log->Printf ("ProcessGDBRemote::EnableBreakpoint() error for breakpoint at 0x%8.8llx: %s",
                     bp_site->GetLoadAddress(),
                     err_string ? err_string : "NULL");
    }
    // We shouldn't reach here on a successful breakpoint enable...
    if (error.Success())
        error.SetErrorToGenericError();
    return error;
}

Error
ProcessGDBRemote::DisableBreakpoint (BreakpointSite *bp_site)
{
    Error error;
    assert (bp_site != NULL);
    addr_t addr = bp_site->GetLoadAddress();
    user_id_t site_id = bp_site->GetID();
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_BREAKPOINTS));
    if (log)
        log->Printf ("ProcessGDBRemote::DisableBreakpoint (site_id = %d) addr = 0x%8.8llx", site_id, (uint64_t)addr);

    if (bp_site->IsEnabled())
    {
        const size_t bp_op_size = GetSoftwareBreakpointTrapOpcode (bp_site);

        if (bp_site->IsHardware())
        {
            // TODO: disable hardware breakpoint...
        }
        else
        {
            if (m_z0_supported)
            {
                char packet[64];
                const int packet_len = ::snprintf (packet, sizeof(packet), "z0,%llx,%zx", addr, bp_op_size);
                assert (packet_len + 1 < sizeof(packet));
                StringExtractorGDBRemote response;
                if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, true))
                {
                    if (response.IsUnsupportedPacket())
                    {
                        error.SetErrorString("Breakpoint site was set with Z packet, yet remote debugserver states z packets are not supported.");
                    }
                    else if (response.IsOKPacket())
                    {
                        if (log)
                            log->Printf ("ProcessGDBRemote::DisableBreakpoint (site_id = %d) addr = 0x%8.8llx -- SUCCESS", site_id, (uint64_t)addr);
                        bp_site->SetEnabled(false);
                        return error;
                    }
                    else
                    {
                        uint8_t error_byte = response.GetError();
                        if (error_byte)
                            error.SetErrorStringWithFormat("%x packet failed with error: %i (0x%2.2x).\n", packet, error_byte, error_byte);
                    }
                }
            }
            else
            {
                return DisableSoftwareBreakpoint (bp_site);
            }
        }
    }
    else
    {
        if (log)
            log->Printf ("ProcessGDBRemote::DisableBreakpoint (site_id = %d) addr = 0x%8.8llx -- SUCCESS (already disabled)", site_id, (uint64_t)addr);
        return error;
    }

    if (error.Success())
        error.SetErrorToGenericError();
    return error;
}

Error
ProcessGDBRemote::EnableWatchpoint (WatchpointLocation *wp)
{
    Error error;
    if (wp)
    {
        user_id_t watchID = wp->GetID();
        addr_t addr = wp->GetLoadAddress();
        LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_WATCHPOINTS));
        if (log)
            log->Printf ("ProcessGDBRemote::EnableWatchpoint(watchID = %d)", watchID);
        if (wp->IsEnabled())
        {
            if (log)
                log->Printf("ProcessGDBRemote::EnableWatchpoint(watchID = %d) addr = 0x%8.8llx: watchpoint already enabled.", watchID, (uint64_t)addr);
            return error;
        }
        else
        {
            // Pass down an appropriate z/Z packet...
            error.SetErrorString("watchpoints not supported");
        }
    }
    else
    {
        error.SetErrorString("Watchpoint location argument was NULL.");
    }
    if (error.Success())
        error.SetErrorToGenericError();
    return error;
}

Error
ProcessGDBRemote::DisableWatchpoint (WatchpointLocation *wp)
{
    Error error;
    if (wp)
    {
        user_id_t watchID = wp->GetID();

        LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_WATCHPOINTS));

        addr_t addr = wp->GetLoadAddress();
        if (log)
            log->Printf ("ProcessGDBRemote::DisableWatchpoint (watchID = %d) addr = 0x%8.8llx", watchID, (uint64_t)addr);

        if (wp->IsHardware())
        {
            // Pass down an appropriate z/Z packet...
            error.SetErrorString("watchpoints not supported");
        }
        // TODO: clear software watchpoints if we implement them
    }
    else
    {
        error.SetErrorString("Watchpoint location argument was NULL.");
    }
    if (error.Success())
        error.SetErrorToGenericError();
    return error;
}

void
ProcessGDBRemote::Clear()
{
    m_flags = 0;
    m_thread_list.Clear();
    {
        Mutex::Locker locker(m_stdio_mutex);
        m_stdout_data.clear();
    }
}

Error
ProcessGDBRemote::DoSignal (int signo)
{
    Error error;
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::DoSignal (signal = %d)", signo);

    if (!m_gdb_comm.SendAsyncSignal (signo))
        error.SetErrorStringWithFormat("failed to send signal %i", signo);
    return error;
}

Error
ProcessGDBRemote::StartDebugserverProcess
(
    const char *debugserver_url,    // The connection string to use in the spawned debugserver ("localhost:1234" or "/dev/tty...")
    char const *inferior_argv[],    // Arguments for the inferior program including the path to the inferior itself as the first argument
    char const *inferior_envp[],    // Environment to pass along to the inferior program
    lldb::pid_t attach_pid,         // If inferior inferior_argv == NULL, and attach_pid != LLDB_INVALID_PROCESS_ID send this pid as an argument to debugserver
    const char *attach_name,        // Wait for the next process to launch whose basename matches "attach_name"
    bool wait_for_launch,           // Wait for the process named "attach_name" to launch
    const ArchSpec& inferior_arch   // The arch of the inferior that we will launch
)
{
    Error error;
    if (m_debugserver_pid == LLDB_INVALID_PROCESS_ID)
    {
        // If we locate debugserver, keep that located version around
        static FileSpec g_debugserver_file_spec;

        FileSpec debugserver_file_spec;
        char debugserver_path[PATH_MAX];

        // Always check to see if we have an environment override for the path
        // to the debugserver to use and use it if we do.
        const char *env_debugserver_path = getenv("LLDB_DEBUGSERVER_PATH");
        if (env_debugserver_path)
            debugserver_file_spec.SetFile (env_debugserver_path, false);
        else
            debugserver_file_spec = g_debugserver_file_spec;
        bool debugserver_exists = debugserver_file_spec.Exists();
        if (!debugserver_exists)
        {
            // The debugserver binary is in the LLDB.framework/Resources
            // directory. 
            if (Host::GetLLDBPath (ePathTypeSupportExecutableDir, debugserver_file_spec))
            {
                debugserver_file_spec.GetFilename().SetCString(DEBUGSERVER_BASENAME);
                debugserver_exists = debugserver_file_spec.Exists();
                if (debugserver_exists)
                {
                    g_debugserver_file_spec = debugserver_file_spec;
                }
                else
                {
                    g_debugserver_file_spec.Clear();
                    debugserver_file_spec.Clear();
                }
            }
        }

        if (debugserver_exists)
        {
            debugserver_file_spec.GetPath (debugserver_path, sizeof(debugserver_path));

            m_stdio_communication.Clear();
            posix_spawnattr_t attr;

            LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));

            Error local_err;    // Errors that don't affect the spawning.
            if (log)
                log->Printf ("%s ( path='%s', argv=%p, envp=%p, arch=%s )", 
                             __FUNCTION__, 
                             debugserver_path, 
                             inferior_argv, 
                             inferior_envp, 
                             inferior_arch.GetArchitectureName());
            error.SetError( ::posix_spawnattr_init (&attr), eErrorTypePOSIX);
            if (error.Fail() || log)
                error.PutToLog(log.get(), "::posix_spawnattr_init ( &attr )");
            if (error.Fail())
                return error;

            Args debugserver_args;
            char arg_cstr[PATH_MAX];

            // Start args with "debugserver /file/path -r --"
            debugserver_args.AppendArgument(debugserver_path);
            debugserver_args.AppendArgument(debugserver_url);
            // use native registers, not the GDB registers
            debugserver_args.AppendArgument("--native-regs");   
            // make debugserver run in its own session so signals generated by 
            // special terminal key sequences (^C) don't affect debugserver
            debugserver_args.AppendArgument("--setsid");

            const char *env_debugserver_log_file = getenv("LLDB_DEBUGSERVER_LOG_FILE");
            if (env_debugserver_log_file)
            {
                ::snprintf (arg_cstr, sizeof(arg_cstr), "--log-file=%s", env_debugserver_log_file);
                debugserver_args.AppendArgument(arg_cstr);
            }

            const char *env_debugserver_log_flags = getenv("LLDB_DEBUGSERVER_LOG_FLAGS");
            if (env_debugserver_log_flags)
            {
                ::snprintf (arg_cstr, sizeof(arg_cstr), "--log-flags=%s", env_debugserver_log_flags);
                debugserver_args.AppendArgument(arg_cstr);
            }
//            debugserver_args.AppendArgument("--log-file=/tmp/debugserver.txt");
//            debugserver_args.AppendArgument("--log-flags=0x802e0e");

            // Now append the program arguments
            if (inferior_argv)
            {
                // Terminate the debugserver args so we can now append the inferior args
                debugserver_args.AppendArgument("--");

                for (int i = 0; inferior_argv[i] != NULL; ++i)
                    debugserver_args.AppendArgument (inferior_argv[i]);
            }
            else if (attach_pid != LLDB_INVALID_PROCESS_ID)
            {
                ::snprintf (arg_cstr, sizeof(arg_cstr), "--attach=%u", attach_pid);
                debugserver_args.AppendArgument (arg_cstr);
            }
            else if (attach_name && attach_name[0])
            {
                if (wait_for_launch)
                    debugserver_args.AppendArgument ("--waitfor");
                else
                    debugserver_args.AppendArgument ("--attach");
                debugserver_args.AppendArgument (attach_name);
            }

            Error file_actions_err;
            posix_spawn_file_actions_t file_actions;
#if DONT_CLOSE_DEBUGSERVER_STDIO
            file_actions_err.SetErrorString ("Remove this after uncommenting the code block below.");
#else
            file_actions_err.SetError( ::posix_spawn_file_actions_init (&file_actions), eErrorTypePOSIX);
            if (file_actions_err.Success())
            {
                ::posix_spawn_file_actions_addclose (&file_actions, STDIN_FILENO);
                ::posix_spawn_file_actions_addclose (&file_actions, STDOUT_FILENO);
                ::posix_spawn_file_actions_addclose (&file_actions, STDERR_FILENO);
            }
#endif

            if (log)
            {
                StreamString strm;
                debugserver_args.Dump (&strm);
                log->Printf("%s arguments:\n%s", debugserver_args.GetArgumentAtIndex(0), strm.GetData());
            }

            error.SetError (::posix_spawnp (&m_debugserver_pid,
                                            debugserver_path,
                                            file_actions_err.Success() ? &file_actions : NULL,
                                            &attr,
                                            debugserver_args.GetArgumentVector(),
                                            (char * const*)inferior_envp),
                            eErrorTypePOSIX);
            

            ::posix_spawnattr_destroy (&attr);

            if (file_actions_err.Success())
                ::posix_spawn_file_actions_destroy (&file_actions);

            // We have seen some cases where posix_spawnp was returning a valid
            // looking pid even when an error was returned, so clear it out
            if (error.Fail())
                m_debugserver_pid = LLDB_INVALID_PROCESS_ID;

            if (error.Fail() || log)
                error.PutToLog(log.get(), "::posix_spawnp ( pid => %i, path = '%s', file_actions = %p, attr = %p, argv = %p, envp = %p )", m_debugserver_pid, debugserver_path, NULL, &attr, inferior_argv, inferior_envp);

        }
        else
        {
            error.SetErrorStringWithFormat ("Unable to locate " DEBUGSERVER_BASENAME ".\n");
        }

        if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID)
            StartAsyncThread ();
    }
    return error;
}

bool
ProcessGDBRemote::MonitorDebugserverProcess
(
    void *callback_baton,
    lldb::pid_t debugserver_pid,
    int signo,          // Zero for no signal
    int exit_status     // Exit value of process if signal is zero
)
{
    // We pass in the ProcessGDBRemote inferior process it and name it
    // "gdb_remote_pid". The process ID is passed in the "callback_baton"
    // pointer value itself, thus we need the double cast...

    // "debugserver_pid" argument passed in is the process ID for
    // debugserver that we are tracking...

    ProcessGDBRemote *process = (ProcessGDBRemote *)callback_baton;

    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::MonitorDebugserverProcess (baton=%p, pid=%i, signo=%i (0x%x), exit_status=%i)", callback_baton, debugserver_pid, signo, signo, exit_status);

    if (process)
    {
        // Sleep for a half a second to make sure our inferior process has
        // time to set its exit status before we set it incorrectly when
        // both the debugserver and the inferior process shut down.
        usleep (500000);
        // If our process hasn't yet exited, debugserver might have died.
        // If the process did exit, the we are reaping it.
        const StateType state = process->GetState();
        
        if (process->m_debugserver_pid != LLDB_INVALID_PROCESS_ID &&
            state != eStateInvalid &&
            state != eStateUnloaded &&
            state != eStateExited &&
            state != eStateDetached)
        {
            char error_str[1024];
            if (signo)
            {
                const char *signal_cstr = process->GetUnixSignals().GetSignalAsCString (signo);
                if (signal_cstr)
                    ::snprintf (error_str, sizeof (error_str), DEBUGSERVER_BASENAME " died with signal %s", signal_cstr);
                else
                    ::snprintf (error_str, sizeof (error_str), DEBUGSERVER_BASENAME " died with signal %i", signo);
            }
            else
            {
                ::snprintf (error_str, sizeof (error_str), DEBUGSERVER_BASENAME " died with an exit status of 0x%8.8x", exit_status);
            }

            process->SetExitStatus (-1, error_str);
        }
        // Debugserver has exited we need to let our ProcessGDBRemote
        // know that it no longer has a debugserver instance
        process->m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
        // We are returning true to this function below, so we can
        // forget about the monitor handle.
        process->m_debugserver_thread = LLDB_INVALID_HOST_THREAD;
    }
    return true;
}

void
ProcessGDBRemote::KillDebugserverProcess ()
{
    if (m_debugserver_pid != LLDB_INVALID_PROCESS_ID)
    {
        ::kill (m_debugserver_pid, SIGINT);
        m_debugserver_pid = LLDB_INVALID_PROCESS_ID;
    }
}

void
ProcessGDBRemote::Initialize()
{
    static bool g_initialized = false;

    if (g_initialized == false)
    {
        g_initialized = true;
        PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                       GetPluginDescriptionStatic(),
                                       CreateInstance);

        Log::Callbacks log_callbacks = {
            ProcessGDBRemoteLog::DisableLog,
            ProcessGDBRemoteLog::EnableLog,
            ProcessGDBRemoteLog::ListLogCategories
        };

        Log::RegisterLogChannel (ProcessGDBRemote::GetPluginNameStatic(), log_callbacks);
    }
}

bool
ProcessGDBRemote::SetCurrentGDBRemoteThread (int tid)
{
    if (m_curr_tid == tid)
        return true;

    char packet[32];
    int packet_len;
    if (tid <= 0)
        packet_len = ::snprintf (packet, sizeof(packet), "Hg%i", tid);
    else
        packet_len = ::snprintf (packet, sizeof(packet), "Hg%x", tid);
    assert (packet_len + 1 < sizeof(packet));
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, false))
    {
        if (response.IsOKPacket())
        {
            m_curr_tid = tid;
            return true;
        }
    }
    return false;
}

bool
ProcessGDBRemote::SetCurrentGDBRemoteThreadForRun (int tid)
{
    if (m_curr_tid_run == tid)
        return true;

    char packet[32];
    int packet_len;
    if (tid <= 0)
        packet_len = ::snprintf (packet, sizeof(packet), "Hc%i", tid);
    else
        packet_len = ::snprintf (packet, sizeof(packet), "Hc%x", tid);

    assert (packet_len + 1 < sizeof(packet));
    StringExtractorGDBRemote response;
    if (m_gdb_comm.SendPacketAndWaitForResponse(packet, packet_len, response, 2, false))
    {
        if (response.IsOKPacket())
        {
            m_curr_tid_run = tid;
            return true;
        }
    }
    return false;
}

void
ProcessGDBRemote::ResetGDBRemoteState ()
{
    // Reset and GDB remote state
    m_curr_tid = LLDB_INVALID_THREAD_ID;
    m_curr_tid_run = LLDB_INVALID_THREAD_ID;
    m_z0_supported = 1;
}


bool
ProcessGDBRemote::StartAsyncThread ()
{
    ResetGDBRemoteState ();

    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

    if (log)
        log->Printf ("ProcessGDBRemote::%s ()", __FUNCTION__);

    // Create a thread that watches our internal state and controls which
    // events make it to clients (into the DCProcess event queue).
    m_async_thread = Host::ThreadCreate ("<lldb.process.gdb-remote.async>", ProcessGDBRemote::AsyncThread, this, NULL);
    return IS_VALID_LLDB_HOST_THREAD(m_async_thread);
}

void
ProcessGDBRemote::StopAsyncThread ()
{
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

    if (log)
        log->Printf ("ProcessGDBRemote::%s ()", __FUNCTION__);

    m_async_broadcaster.BroadcastEvent (eBroadcastBitAsyncThreadShouldExit);

    // Stop the stdio thread
    if (IS_VALID_LLDB_HOST_THREAD(m_async_thread))
    {
        Host::ThreadJoin (m_async_thread, NULL, NULL);
    }
}


void *
ProcessGDBRemote::AsyncThread (void *arg)
{
    ProcessGDBRemote *process = (ProcessGDBRemote*) arg;

    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (GDBR_LOG_PROCESS));
    if (log)
        log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) thread starting...", __FUNCTION__, arg, process->GetID());

    Listener listener ("ProcessGDBRemote::AsyncThread");
    EventSP event_sp;
    const uint32_t desired_event_mask = eBroadcastBitAsyncContinue |
                                        eBroadcastBitAsyncThreadShouldExit;

    if (listener.StartListeningForEvents (&process->m_async_broadcaster, desired_event_mask) == desired_event_mask)
    {
        listener.StartListeningForEvents (&process->m_gdb_comm, Communication::eBroadcastBitReadThreadDidExit);
    
        bool done = false;
        while (!done)
        {
            if (log)
                log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) listener.WaitForEvent (NULL, event_sp)...", __FUNCTION__, arg, process->GetID());
            if (listener.WaitForEvent (NULL, event_sp))
            {
                const uint32_t event_type = event_sp->GetType();
                if (event_sp->BroadcasterIs (&process->m_async_broadcaster))
                {
                    if (log)
                        log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) Got an event of type: %d...", __FUNCTION__, arg, process->GetID(), event_type);

                    switch (event_type)
                    {
                        case eBroadcastBitAsyncContinue:
                            {
                                const EventDataBytes *continue_packet = EventDataBytes::GetEventDataFromEvent(event_sp.get());

                                if (continue_packet)
                                {
                                    const char *continue_cstr = (const char *)continue_packet->GetBytes ();
                                    const size_t continue_cstr_len = continue_packet->GetByteSize ();
                                    if (log)
                                        log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) got eBroadcastBitAsyncContinue: %s", __FUNCTION__, arg, process->GetID(), continue_cstr);

                                    if (::strstr (continue_cstr, "vAttach") == NULL)
                                        process->SetPrivateState(eStateRunning);
                                    StringExtractorGDBRemote response;
                                    StateType stop_state = process->GetGDBRemote().SendContinuePacketAndWaitForResponse (process, continue_cstr, continue_cstr_len, response);

                                    switch (stop_state)
                                    {
                                    case eStateStopped:
                                    case eStateCrashed:
                                    case eStateSuspended:
                                        process->m_last_stop_packet = response;
                                        process->m_last_stop_packet.SetFilePos (0);
                                        process->SetPrivateState (stop_state);
                                        break;

                                    case eStateExited:
                                        process->m_last_stop_packet = response;
                                        process->m_last_stop_packet.SetFilePos (0);
                                        response.SetFilePos(1);
                                        process->SetExitStatus(response.GetHexU8(), NULL);
                                        done = true;
                                        break;

                                    case eStateInvalid:
                                        process->SetExitStatus(-1, "lost connection");
                                        break;

                                    default:
                                        process->SetPrivateState (stop_state);
                                        break;
                                    }
                                }
                            }
                            break;

                        case eBroadcastBitAsyncThreadShouldExit:
                            if (log)
                                log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) got eBroadcastBitAsyncThreadShouldExit...", __FUNCTION__, arg, process->GetID());
                            done = true;
                            break;

                        default:
                            if (log)
                                log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) got unknown event 0x%8.8x", __FUNCTION__, arg, process->GetID(), event_type);
                            done = true;
                            break;
                    }
                }
                else if (event_sp->BroadcasterIs (&process->m_gdb_comm))
                {
                    if (event_type & Communication::eBroadcastBitReadThreadDidExit)
                    {
                        process->SetExitStatus (-1, "lost connection");
                        done = true;
                    }
                }
            }
            else
            {
                if (log)
                    log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) listener.WaitForEvent (NULL, event_sp) => false", __FUNCTION__, arg, process->GetID());
                done = true;
            }
        }
    }

    if (log)
        log->Printf ("ProcessGDBRemote::%s (arg = %p, pid = %i) thread exiting...", __FUNCTION__, arg, process->GetID());

    process->m_async_thread = LLDB_INVALID_HOST_THREAD;
    return NULL;
}

const char *
ProcessGDBRemote::GetDispatchQueueNameForThread
(
    addr_t thread_dispatch_qaddr,
    std::string &dispatch_queue_name
)
{
    dispatch_queue_name.clear();
    if (thread_dispatch_qaddr != 0 && thread_dispatch_qaddr != LLDB_INVALID_ADDRESS)
    {
        // Cache the dispatch_queue_offsets_addr value so we don't always have
        // to look it up
        if (m_dispatch_queue_offsets_addr == LLDB_INVALID_ADDRESS)
        {
            static ConstString g_dispatch_queue_offsets_symbol_name ("dispatch_queue_offsets");
            const Symbol *dispatch_queue_offsets_symbol = NULL;
            ModuleSP module_sp(GetTarget().GetImages().FindFirstModuleForFileSpec (FileSpec("libSystem.B.dylib", false)));
            if (module_sp)
                dispatch_queue_offsets_symbol = module_sp->FindFirstSymbolWithNameAndType (g_dispatch_queue_offsets_symbol_name, eSymbolTypeData);
            
            if (dispatch_queue_offsets_symbol == NULL)
            {
                module_sp = GetTarget().GetImages().FindFirstModuleForFileSpec (FileSpec("libdispatch.dylib", false));
                if (module_sp)
                    dispatch_queue_offsets_symbol = module_sp->FindFirstSymbolWithNameAndType (g_dispatch_queue_offsets_symbol_name, eSymbolTypeData);
            }
            if (dispatch_queue_offsets_symbol)
                m_dispatch_queue_offsets_addr = dispatch_queue_offsets_symbol->GetValue().GetLoadAddress(&m_target);

            if (m_dispatch_queue_offsets_addr == LLDB_INVALID_ADDRESS)
                return NULL;
        }

        uint8_t memory_buffer[8];
        DataExtractor data (memory_buffer, 
                            sizeof(memory_buffer), 
                            m_target.GetArchitecture().GetByteOrder(), 
                            m_target.GetArchitecture().GetAddressByteSize());

        // Excerpt from src/queue_private.h
        struct dispatch_queue_offsets_s
        {
            uint16_t dqo_version;
            uint16_t dqo_label;
            uint16_t dqo_label_size;
        } dispatch_queue_offsets;


        Error error;
        if (ReadMemory (m_dispatch_queue_offsets_addr, memory_buffer, sizeof(dispatch_queue_offsets), error) == sizeof(dispatch_queue_offsets))
        {
            uint32_t data_offset = 0;
            if (data.GetU16(&data_offset, &dispatch_queue_offsets.dqo_version, sizeof(dispatch_queue_offsets)/sizeof(uint16_t)))
            {
                if (ReadMemory (thread_dispatch_qaddr, &memory_buffer, data.GetAddressByteSize(), error) == data.GetAddressByteSize())
                {
                    data_offset = 0;
                    lldb::addr_t queue_addr = data.GetAddress(&data_offset);
                    lldb::addr_t label_addr = queue_addr + dispatch_queue_offsets.dqo_label;
                    dispatch_queue_name.resize(dispatch_queue_offsets.dqo_label_size, '\0');
                    size_t bytes_read = ReadMemory (label_addr, &dispatch_queue_name[0], dispatch_queue_offsets.dqo_label_size, error);
                    if (bytes_read < dispatch_queue_offsets.dqo_label_size)
                        dispatch_queue_name.erase (bytes_read);
                }
            }
        }
    }
    if (dispatch_queue_name.empty())
        return NULL;
    return dispatch_queue_name.c_str();
}

uint32_t
ProcessGDBRemote::ListProcessesMatchingName (const char *name, StringList &matches, std::vector<lldb::pid_t> &pids)
{
    // If we are planning to launch the debugserver remotely, then we need to fire up a debugserver
    // process and ask it for the list of processes. But if we are local, we can let the Host do it.
    if (m_local_debugserver)
    {
        return Host::ListProcessesMatchingName (name, matches, pids);
    }
    else 
    {
        // FIXME: Implement talking to the remote debugserver.
        return 0;
    }

}

bool
ProcessGDBRemote::NewThreadNotifyBreakpointHit (void *baton,
                             lldb_private::StoppointCallbackContext *context,
                             lldb::user_id_t break_id,
                             lldb::user_id_t break_loc_id)
{
    // I don't think I have to do anything here, just make sure I notice the new thread when it starts to 
    // run so I can stop it if that's what I want to do.
    LogSP log (lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
        log->Printf("Hit New Thread Notification breakpoint.");
    return false;
}


bool
ProcessGDBRemote::StartNoticingNewThreads()
{
    static const char *bp_names[] =
    {
        "start_wqthread",
        "_pthread_wqthread",
        "_pthread_start",
        NULL
    };
    
    LogSP log (lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    size_t num_bps = m_thread_observation_bps.size();
    if (num_bps != 0)
    {
        for (int i = 0; i < num_bps; i++)
        {
            lldb::BreakpointSP break_sp = m_target.GetBreakpointByID(m_thread_observation_bps[i]);
            if (break_sp)
            {
                if (log)
                    log->Printf("Enabled noticing new thread breakpoint.");
                break_sp->SetEnabled(true);
            }
        }
    }
    else 
    {
        for (int i = 0; bp_names[i] != NULL; i++)
        {
            Breakpoint *breakpoint = m_target.CreateBreakpoint (NULL, bp_names[i], eFunctionNameTypeFull, true).get();
            if (breakpoint)
            {
                if (log)
                     log->Printf("Successfully created new thread notification breakpoint at \"%s\".", bp_names[i]);
                m_thread_observation_bps.push_back(breakpoint->GetID());
                breakpoint->SetCallback (ProcessGDBRemote::NewThreadNotifyBreakpointHit, this, true);
            }
            else
            {
                if (log)
                    log->Printf("Failed to create new thread notification breakpoint.");
                return false;
            }
        }
    }

    return true;
}

bool
ProcessGDBRemote::StopNoticingNewThreads()
{   
    LogSP log (lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    if (log)
        log->Printf ("Disabling new thread notification breakpoint.");
    size_t num_bps = m_thread_observation_bps.size();
    if (num_bps != 0)
    {
        for (int i = 0; i < num_bps; i++)
        {
            
            lldb::BreakpointSP break_sp = m_target.GetBreakpointByID(m_thread_observation_bps[i]);
            if (break_sp)
            {
                break_sp->SetEnabled(false);
            }
        }
    }
    return true;
}
    

