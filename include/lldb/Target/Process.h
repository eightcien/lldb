//===-- Process.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Process_h_
#define liblldb_Process_h_

// C Includes
// C++ Includes
#include <list>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Core/Broadcaster.h"
#include "lldb/Core/Communication.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Event.h"
#include "lldb/Core/StringList.h"
#include "lldb/Core/ThreadSafeValue.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Breakpoint/BreakpointSiteList.h"
#include "lldb/Expression/ClangPersistentVariables.h"
#include "lldb/Expression/IRDynamicChecks.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Target/UnixSignals.h"

namespace lldb_private {


typedef enum ProcessPlugins
{
    eMacosx,
    eRemoteDebugger
} ProcessPlugins;


class ProcessInstanceSettings : public InstanceSettings
{
public:

    ProcessInstanceSettings (UserSettingsController &owner, bool live_instance = true, const char *name = NULL);
  
    ProcessInstanceSettings (const ProcessInstanceSettings &rhs);

    virtual
    ~ProcessInstanceSettings ();
  
    ProcessInstanceSettings&
    operator= (const ProcessInstanceSettings &rhs);
  

    void
    UpdateInstanceSettingsVariable (const ConstString &var_name,
                                    const char *index_value,
                                    const char *value,
                                    const ConstString &instance_name,
                                    const SettingEntry &entry,
                                    lldb::VarSetOperationType op,
                                    Error &err,
                                    bool pending);

    bool
    GetInstanceSettingsValue (const SettingEntry &entry,
                              const ConstString &var_name,
                              StringList &value,
                              Error *err);


    const Args &
    GetRunArguments () const
    {
        return m_run_args;
    }

    void
    SetRunArguments (const Args &args)
    {
        m_run_args = args;
    }

    void
    GetHostEnvironmentIfNeeded ();

    size_t
    GetEnvironmentAsArgs (Args &env);

    const char *
    GetStandardInputPath () const
    {
        if (m_input_path.empty())
            return NULL;
        return m_input_path.c_str();
    }

    void
    SetStandardInputPath (const char *path)
    {
        if (path && path[0])
            m_input_path.assign (path);
        else
        {
            // Make sure we deallocate memory in string...
            std::string tmp;
            tmp.swap (m_input_path);
        }
    }

    const char *
    GetStandardOutputPath () const
    {
        if (m_output_path.empty())
            return NULL;
        return m_output_path.c_str();
    }

    void
    SetStandardOutputPath (const char *path)
    {
        if (path && path[0])
            m_output_path.assign (path);
        else
        {
            // Make sure we deallocate memory in string...
            std::string tmp;
            tmp.swap (m_output_path);
        }
    }

    const char *
    GetStandardErrorPath () const
    {
        if (m_error_path.empty())
            return NULL;
        return m_error_path.c_str();
    }

    void
    SetStandardErrorPath (const char *path)
    {
        if (path && path[0])
            m_error_path.assign (path);
        else
        {
            // Make sure we deallocate memory in string...
            std::string tmp;
            tmp.swap (m_error_path);
        }
    }
    
    bool
    GetDisableASLR () const
    {
        return m_disable_aslr;
    }
    
    void
    SetDisableASLR (bool b)
    {
        m_disable_aslr = b;
    }
    
    bool
    GetDisableSTDIO () const
    {
        return m_disable_stdio;
    }
    
    void
    SetDisableSTDIO (bool b)
    {
        m_disable_stdio = b;
    }

protected:

    void
    CopyInstanceSettings (const lldb::InstanceSettingsSP &new_settings,
                          bool pending);

    const ConstString
    CreateInstanceName ();

    static const ConstString &
    RunArgsVarName ();

    static const ConstString &
    EnvVarsVarName ();

    static const ConstString &
    InheritHostEnvVarName ();

    static const ConstString &
    InputPathVarName ();

    static const ConstString &
    OutputPathVarName ();

    static const ConstString &
    ErrorPathVarName ();

    static const ConstString &
    PluginVarName ();

    static const ConstString &
    DisableASLRVarName();

    static const ConstString &
    DisableSTDIOVarName ();
    
private:

    typedef std::map<std::string, std::string> dictionary;
    Args m_run_args;
    dictionary m_env_vars;
    std::string m_input_path;
    std::string m_output_path;
    std::string m_error_path;
    ProcessPlugins m_plugin;
    bool m_disable_aslr;
    bool m_disable_stdio;
    bool m_inherit_host_env;
    bool m_got_host_env;
};


//----------------------------------------------------------------------
/// @class Process Process.h "lldb/Target/Process.h"
/// @brief A plug-in interface definition class for debugging a process.
//----------------------------------------------------------------------
class Process :
    public UserID,
    public Broadcaster,
    public ExecutionContextScope,
    public PluginInterface,
    public ProcessInstanceSettings
{
friend class ThreadList;
friend class ClangFunction; // For WaitForStateChangeEventsPrivate

public:

    //------------------------------------------------------------------
    /// Broadcaster event bits definitions.
    //------------------------------------------------------------------
    enum
    {
        eBroadcastBitStateChanged   = (1 << 0),
        eBroadcastBitInterrupt      = (1 << 1),
        eBroadcastBitSTDOUT         = (1 << 2),
        eBroadcastBitSTDERR         = (1 << 3)
    };

    enum
    {
        eBroadcastInternalStateControlStop = (1<<0),
        eBroadcastInternalStateControlPause = (1<<1),
        eBroadcastInternalStateControlResume = (1<<2)
    };
        
    //------------------------------------------------------------------
    /// A notification structure that can be used by clients to listen
    /// for changes in a process's lifetime.
    ///
    /// @see RegisterNotificationCallbacks (const Notifications&)
    /// @see UnregisterNotificationCallbacks (const Notifications&)
    //------------------------------------------------------------------
#ifndef SWIG
    typedef struct
    {
        void *baton;
        void (*initialize)(void *baton, Process *process);
        void (*process_state_changed) (void *baton, Process *process, lldb::StateType state);
    } Notifications;

    class ProcessEventData :
        public EventData
    {
        friend class Process;
        
        public:
            ProcessEventData ();
            ProcessEventData (const lldb::ProcessSP &process, lldb::StateType state);

            virtual ~ProcessEventData();

            static const ConstString &
            GetFlavorString ();

            virtual const ConstString &
            GetFlavor () const;

            const lldb::ProcessSP &
            GetProcessSP() const
            {
                return m_process_sp;
            }
            lldb::StateType
            GetState() const
            {
                return m_state;
            }
            bool
            GetRestarted () const
            {
                return m_restarted;
            }
            bool
            GetInterrupted () const
            {
                return m_interrupted;
            }

            virtual void
            Dump (Stream *s) const;

            virtual void
            DoOnRemoval (Event *event_ptr);

            static const Process::ProcessEventData *
            GetEventDataFromEvent (const Event *event_ptr);

            static lldb::ProcessSP
            GetProcessFromEvent (const Event *event_ptr);

            static lldb::StateType
            GetStateFromEvent (const Event *event_ptr);

            static bool
            GetRestartedFromEvent (const Event *event_ptr);

            static void
            SetRestartedInEvent (Event *event_ptr, bool new_value);

            static bool
            GetInterruptedFromEvent (const Event *event_ptr);

            static void
            SetInterruptedInEvent (Event *event_ptr, bool new_value);

            static bool
            SetUpdateStateOnRemoval (Event *event_ptr);

       private:

            void
            SetUpdateStateOnRemoval()
            {
                m_update_state = true;
            }
            void
            SetRestarted (bool new_value)
            {
                m_restarted = new_value;
            }
            void
            SetInterrupted (bool new_value)
            {
                m_interrupted = new_value;
            }

            lldb::ProcessSP m_process_sp;
            lldb::StateType m_state;
            bool m_restarted;  // For "eStateStopped" events, this is true if the target was automatically restarted.
            bool m_update_state;
            bool m_interrupted;
            DISALLOW_COPY_AND_ASSIGN (ProcessEventData);

    };

    class SettingsController : public UserSettingsController
    {
    public:
        
        SettingsController ();

        virtual
        ~SettingsController ();

        static SettingEntry global_settings_table[];
        static SettingEntry instance_settings_table[];

    protected:

        lldb::InstanceSettingsSP
        CreateInstanceSettings (const char *instance_name);

        static lldb::OptionEnumValueElement g_plugins[];

    private:

        // Class-wide settings.

        DISALLOW_COPY_AND_ASSIGN (SettingsController);
    };

#endif

    static void
    Initialize ();

    static void
    Terminate ();

    static lldb::UserSettingsControllerSP &
    GetSettingsController ();

    void
    UpdateInstanceName ();

    //------------------------------------------------------------------
    /// Construct with a shared pointer to a target, and the Process listener.
    //------------------------------------------------------------------
    Process(Target &target, Listener &listener);

    //------------------------------------------------------------------
    /// Destructor.
    ///
    /// The destructor is virtual since this class is designed to be
    /// inherited from by the plug-in instance.
    //------------------------------------------------------------------
    virtual
    ~Process();

    //------------------------------------------------------------------
    /// Find a Process plug-in that can debug \a module using the
    /// currently selected architecture.
    ///
    /// Scans all loaded plug-in interfaces that implement versions of
    /// the Process plug-in interface and returns the first instance
    /// that can debug the file.
    ///
    /// @param[in] module_sp
    ///     The module shared pointer that this process will debug.
    ///
    /// @param[in] plugin_name
    ///     If NULL, select the best plug-in for the binary. If non-NULL
    ///     then look for a plugin whose PluginInfo's name matches
    ///     this string.
    ///
    /// @see Process::CanDebug ()
    //------------------------------------------------------------------
    static Process*
    FindPlugin (Target &target, const char *plugin_name, Listener &listener);



    //------------------------------------------------------------------
    /// Static function that can be used with the \b host function
    /// Host::StartMonitoringChildProcess ().
    ///
    /// This function can be used by lldb_private::Process subclasses
    /// when they want to watch for a local process and have its exit
    /// status automatically set when the host child process exits.
    /// Subclasses should call Host::StartMonitoringChildProcess ()
    /// with:
    ///     callback = Process::SetHostProcessExitStatus
    ///     callback_baton = NULL
    ///     pid = Process::GetID()
    ///     monitor_signals = false
    //------------------------------------------------------------------
    static bool
    SetProcessExitStatus (void *callback_baton,   // The callback baton which should be set to NULL
                          lldb::pid_t pid,        // The process ID we want to monitor
                          int signo,              // Zero for no signal
                          int status);            // Exit value of process if signal is zero

    //------------------------------------------------------------------
    /// Check if a plug-in instance can debug the file in \a module.
    ///
    /// Each plug-in is given a chance to say whether it can debug
    /// the file in \a module. If the Process plug-in instance can
    /// debug a file on the current system, it should return \b true.
    ///
    /// @return
    ///     Returns \b true if this Process plug-in instance can
    ///     debug the executable, \b false otherwise.
    //------------------------------------------------------------------
    virtual bool
    CanDebug (Target &target) = 0;


    //------------------------------------------------------------------
    /// This object is about to be destroyed, do any necessary cleanup.
    ///
    /// Subclasses that override this method should always call this
    /// superclass method.
    //------------------------------------------------------------------
    virtual void
    Finalize();

    //------------------------------------------------------------------
    /// Launch a new process.
    ///
    /// Launch a new process by spawning a new process using the
    /// target object's executable module's file as the file to launch.
    /// Arguments are given in \a argv, and the environment variables
    /// are in \a envp. Standard input and output files can be
    /// optionally re-directed to \a stdin_path, \a stdout_path, and
    /// \a stderr_path.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses. It will first call Process::WillLaunch (Module *)
    /// and if that returns \b true, Process::DoLaunch (Module*,
    /// char const *[],char const *[],const char *,const char *,
    /// const char *) will be called to actually do the launching. If
    /// DoLaunch returns \b true, then Process::DidLaunch() will be
    /// called.
    ///
    /// @param[in] argv
    ///     The argument array.
    ///
    /// @param[in] envp
    ///     The environment array.
    ///
    /// @param[in] launch_flags
    ///     Flags to modify the launch (@see lldb::LaunchFlags)
    ///
    /// @param[in] stdin_path
    ///     The path to use when re-directing the STDIN of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] stdout_path
    ///     The path to use when re-directing the STDOUT of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] stderr_path
    ///     The path to use when re-directing the STDERR of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] working_directory
    ///     The working directory to have the child process run in
    ///
    /// @return
    ///     An error object. Call GetID() to get the process ID if
    ///     the error object is success.
    //------------------------------------------------------------------
    virtual Error
    Launch (char const *argv[],
            char const *envp[],
            uint32_t launch_flags,
            const char *stdin_path,
            const char *stdout_path,
            const char *stderr_path,
            const char *working_directory);

    //------------------------------------------------------------------
    /// Attach to an existing process using a process ID.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses. It will first call Process::WillAttach (lldb::pid_t)
    /// and if that returns \b true, Process::DoAttach (lldb::pid_t) will
    /// be called to actually do the attach. If DoAttach returns \b
    /// true, then Process::DidAttach() will be called.
    ///
    /// @param[in] pid
    ///     The process ID that we should attempt to attach to.
    ///
    /// @return
    ///     Returns \a pid if attaching was successful, or
    ///     LLDB_INVALID_PROCESS_ID if attaching fails.
    //------------------------------------------------------------------
    virtual Error
    Attach (lldb::pid_t pid);

    //------------------------------------------------------------------
    /// Attach to an existing process by process name.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses. It will first call
    /// Process::WillAttach (const char *) and if that returns \b
    /// true, Process::DoAttach (const char *) will be called to
    /// actually do the attach. If DoAttach returns \b true, then
    /// Process::DidAttach() will be called.
    ///
    /// @param[in] process_name
    ///     A process name to match against the current process list.
    ///
    /// @return
    ///     Returns \a pid if attaching was successful, or
    ///     LLDB_INVALID_PROCESS_ID if attaching fails.
    //------------------------------------------------------------------
    virtual Error
    Attach (const char *process_name, bool wait_for_launch);
    
    //------------------------------------------------------------------
    /// List the processes matching the given partial name.
    ///
    /// FIXME: Is it too heavyweight to create an entire process object to do this?
    /// The problem is for remote processes we're going to have to set up the same transport
    /// to get this data as to actually attach.  So we need to factor out transport
    /// and process before we can do this separately from the process.
    ///
    /// @param[in] name
    ///     A partial name to match against the current process list.
    ///
    /// @param[out] matches
    ///     The list of process names matching \a name.
    ///
    /// @param[in] pids
    ///     A vector filled with the pids that correspond to the names in \a matches.
    ///
    /// @return
    ///     Returns the number of matching processes.
    //------------------------------------------------------------------

    virtual uint32_t
    ListProcessesMatchingName (const char *name, StringList &matches, std::vector<lldb::pid_t> &pids);
    
    //------------------------------------------------------------------
    /// Find the architecture of a process by pid.
    ///
    /// FIXME: See comment for ListProcessesMatchingName.
    ///
    /// @param[in] pid
    ///     A pid to inspect.
    ///
    /// @return
    ///     Returns the architecture of the process or an invalid architecture if the process can't be found.
    //------------------------------------------------------------------
    virtual ArchSpec
    GetArchSpecForExistingProcess (lldb::pid_t pid);
    
    //------------------------------------------------------------------
    /// Find the architecture of a process by name.
    ///
    /// FIXME: See comment for ListProcessesMatchingName.
    ///
    /// @param[in] process_name
    ///     The process name to inspect.
    ///
    /// @return
    ///     Returns the architecture of the process or an invalid architecture if the process can't be found.
    //------------------------------------------------------------------
    virtual ArchSpec
    GetArchSpecForExistingProcess (const char *process_name);

    uint32_t
    GetAddressByteSize();

    void
    SetAddressByteSize (uint32_t addr_byte_size)
    {
        m_addr_byte_size = addr_byte_size;
    }

    //------------------------------------------------------------------
    /// Get the image information address for the current process.
    ///
    /// Some runtimes have system functions that can help dynamic
    /// loaders locate the dynamic loader information needed to observe
    /// shared libraries being loaded or unloaded. This function is
    /// in the Process interface (as opposed to the DynamicLoader
    /// interface) to ensure that remote debugging can take advantage of
    /// this functionality.
    ///
    /// @return
    ///     The address of the dynamic loader information, or
    ///     LLDB_INVALID_ADDRESS if this is not supported by this
    ///     interface.
    //------------------------------------------------------------------
    virtual lldb::addr_t
    GetImageInfoAddress ();

    //------------------------------------------------------------------
    /// Load a shared library into this process.
    ///
    /// Try and load a shared library into the current process. This
    /// call might fail in the dynamic loader plug-in says it isn't safe
    /// to try and load shared libraries at the moment.
    ///
    /// @param[in] image_spec
    ///     The image file spec that points to the shared library that
    ///     you want to load.
    ///
    /// @param[out] error
    ///     An error object that gets filled in with any errors that
    ///     might occur when trying to load the shared library.
    ///
    /// @return
    ///     A token that represents the shared library that can be
    ///     later used to unload the shared library. A value of
    ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
    ///     library can't be opened.
    //------------------------------------------------------------------
    virtual uint32_t
    LoadImage (const FileSpec &image_spec, Error &error);

    virtual Error
    UnloadImage (uint32_t image_token);

    //------------------------------------------------------------------
    /// Register for process and thread notifications.
    ///
    /// Clients can register nofication callbacks by filling out a
    /// Process::Notifications structure and calling this function.
    ///
    /// @param[in] callbacks
    ///     A structure that contains the notification baton and
    ///     callback functions.
    ///
    /// @see Process::Notifications
    //------------------------------------------------------------------
#ifndef SWIG
    void
    RegisterNotificationCallbacks (const Process::Notifications& callbacks);
#endif
    //------------------------------------------------------------------
    /// Unregister for process and thread notifications.
    ///
    /// Clients can unregister nofication callbacks by passing a copy of
    /// the original baton and callbacks in \a callbacks.
    ///
    /// @param[in] callbacks
    ///     A structure that contains the notification baton and
    ///     callback functions.
    ///
    /// @return
    ///     Returns \b true if the notification callbacks were
    ///     successfully removed from the process, \b false otherwise.
    ///
    /// @see Process::Notifications
    //------------------------------------------------------------------
#ifndef SWIG
    bool
    UnregisterNotificationCallbacks (const Process::Notifications& callbacks);
#endif
    //==================================================================
    // Built in Process Control functions
    //==================================================================
    //------------------------------------------------------------------
    /// Resumes all of a process's threads as configured using the
    /// Thread run control functions.
    ///
    /// Threads for a process should be updated with one of the run
    /// control actions (resume, step, or suspend) that they should take
    /// when the process is resumed. If no run control action is given
    /// to a thread it will be resumed by default.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses. This function will take care of disabling any
    /// breakpoints that threads may be stopped at, single stepping, and
    /// re-enabling breakpoints, and enabling the basic flow control
    /// that the plug-in instances need not worry about.
    ///
    /// @return
    ///     Returns an error object.
    ///
    /// @see Thread:Resume()
    /// @see Thread:Step()
    /// @see Thread:Suspend()
    //------------------------------------------------------------------
    Error
    Resume ();

    //------------------------------------------------------------------
    /// Halts a running process.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses.
    /// If the process is successfully halted, a eStateStopped
    /// process event with GetInterrupted will be broadcast.  If false, we will
    /// halt the process with no events generated by the halt.
    ///
    /// @return
    ///     Returns an error object.  If the error is empty, the process is halted.
    ///     otherwise the halt has failed.
    //------------------------------------------------------------------
    Error
    Halt ();

    //------------------------------------------------------------------
    /// Detaches from a running or stopped process.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    Error
    Detach ();

    //------------------------------------------------------------------
    /// Kills the process and shuts down all threads that were spawned
    /// to track and monitor the process.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    Error
    Destroy();

    //------------------------------------------------------------------
    /// Sends a process a UNIX signal \a signal.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    Error
    Signal (int signal);

    virtual UnixSignals &
    GetUnixSignals ();


    //==================================================================
    // Plug-in Process Control Overrides
    //==================================================================

    //------------------------------------------------------------------
    /// Called before attaching to a process.
    ///
    /// Allow Process plug-ins to execute some code before attaching a
    /// process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillAttachToProcessWithID (lldb::pid_t pid) 
    {
        return Error(); 
    }

    //------------------------------------------------------------------
    /// Called before attaching to a process.
    ///
    /// Allow Process plug-ins to execute some code before attaching a
    /// process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillAttachToProcessWithName (const char *process_name, bool wait_for_launch) 
    { 
        return Error(); 
    }

    //------------------------------------------------------------------
    /// Attach to an existing process using a process ID.
    ///
    /// @param[in] pid
    ///     The process ID that we should attempt to attach to.
    ///
    /// @return
    ///     Returns \a pid if attaching was successful, or
    ///     LLDB_INVALID_PROCESS_ID if attaching fails.
    //------------------------------------------------------------------
    virtual Error
    DoAttachToProcessWithID (lldb::pid_t pid) = 0;

    //------------------------------------------------------------------
    /// Attach to an existing process using a partial process name.
    ///
    /// @param[in] process_name
    ///     The name of the process to attach to.
    ///
    /// @param[in] wait_for_launch
    ///     If \b true, wait for the process to be launched and attach
    ///     as soon as possible after it does launch. If \b false, then
    ///     search for a matching process the currently exists.
    ///
    /// @return
    ///     Returns \a pid if attaching was successful, or
    ///     LLDB_INVALID_PROCESS_ID if attaching fails.
    //------------------------------------------------------------------
    virtual Error
    DoAttachToProcessWithName (const char *process_name, bool wait_for_launch) 
    {
        Error error;
        error.SetErrorString("attach by name is not supported");
        return error;
    }

    //------------------------------------------------------------------
    /// Called after attaching a process.
    ///
    /// Allow Process plug-ins to execute some code after attaching to
    /// a process.
    //------------------------------------------------------------------
    virtual void
    DidAttach () {}


    //------------------------------------------------------------------
    /// Called before launching to a process.
    ///
    /// Allow Process plug-ins to execute some code before launching a
    /// process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillLaunch (Module* module)
    {
        return Error();
    }

    //------------------------------------------------------------------
    /// Launch a new process.
    ///
    /// Launch a new process by spawning a new process using \a module's
    /// file as the file to launch. Arguments are given in \a argv,
    /// and the environment variables are in \a envp. Standard input
    /// and output files can be optionally re-directed to \a stdin_path,
    /// \a stdout_path, and \a stderr_path.
    ///
    /// @param[in] module
    ///     The module from which to extract the file specification and
    ///     launch.
    ///
    /// @param[in] argv
    ///     The argument array.
    ///
    /// @param[in] envp
    ///     The environment array.
    ///
    /// @param[in] launch_flags
    ///     Flags to modify the launch (@see lldb::LaunchFlags)
    ///
    /// @param[in] stdin_path
    ///     The path to use when re-directing the STDIN of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] stdout_path
    ///     The path to use when re-directing the STDOUT of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] stderr_path
    ///     The path to use when re-directing the STDERR of the new
    ///     process. If all stdXX_path arguments are NULL, a pseudo
    ///     terminal will be used.
    ///
    /// @param[in] working_directory
    ///     The working directory to have the child process run in
    ///
    /// @return
    ///     A new valid process ID, or LLDB_INVALID_PROCESS_ID if
    ///     launching fails.
    //------------------------------------------------------------------
    virtual Error
    DoLaunch (Module* module,
              char const *argv[],
              char const *envp[],
              uint32_t launch_flags,
              const char *stdin_path,
              const char *stdout_path,
              const char *stderr_path,
              const char *working_directory) = 0;
    
    //------------------------------------------------------------------
    /// Called after launching a process.
    ///
    /// Allow Process plug-ins to execute some code after launching
    /// a process.
    //------------------------------------------------------------------
    virtual void
    DidLaunch () {}



    //------------------------------------------------------------------
    /// Called before resuming to a process.
    ///
    /// Allow Process plug-ins to execute some code before resuming a
    /// process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillResume () { return Error(); }

    //------------------------------------------------------------------
    /// Resumes all of a process's threads as configured using the
    /// Thread run control functions.
    ///
    /// Threads for a process should be updated with one of the run
    /// control actions (resume, step, or suspend) that they should take
    /// when the process is resumed. If no run control action is given
    /// to a thread it will be resumed by default.
    ///
    /// @return
    ///     Returns \b true if the process successfully resumes using
    ///     the thread run control actions, \b false otherwise.
    ///
    /// @see Thread:Resume()
    /// @see Thread:Step()
    /// @see Thread:Suspend()
    //------------------------------------------------------------------
    virtual Error
    DoResume () = 0;

    //------------------------------------------------------------------
    /// Called after resuming a process.
    ///
    /// Allow Process plug-ins to execute some code after resuming
    /// a process.
    //------------------------------------------------------------------
    virtual void
    DidResume () {}


    //------------------------------------------------------------------
    /// Called before halting to a process.
    ///
    /// Allow Process plug-ins to execute some code before halting a
    /// process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillHalt () { return Error(); }

    //------------------------------------------------------------------
    /// Halts a running process.
    ///
    /// DoHalt must produce one and only one stop StateChanged event if it actually
    /// stops the process.  If the stop happens through some natural event (for
    /// instance a SIGSTOP), then forwarding that event will do.  Otherwise, you must 
    /// generate the event manually.  Note also, the private event thread is stopped when 
    /// DoHalt is run to prevent the events generated while halting to trigger
    /// other state changes before the halt is complete.
    ///
    /// @param[out] caused_stop
    ///     If true, then this Halt caused the stop, otherwise, the 
    ///     process was already stopped.
    ///
    /// @return
    ///     Returns \b true if the process successfully halts, \b false
    ///     otherwise.
    //------------------------------------------------------------------
    virtual Error
    DoHalt (bool &caused_stop) = 0;

    //------------------------------------------------------------------
    /// Called after halting a process.
    ///
    /// Allow Process plug-ins to execute some code after halting
    /// a process.
    //------------------------------------------------------------------
    virtual void
    DidHalt () {}

    //------------------------------------------------------------------
    /// Called before detaching from a process.
    ///
    /// Allow Process plug-ins to execute some code before detaching
    /// from a process.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    WillDetach () 
    {
        return Error(); 
    }

    //------------------------------------------------------------------
    /// Detaches from a running or stopped process.
    ///
    /// @return
    ///     Returns \b true if the process successfully detaches, \b
    ///     false otherwise.
    //------------------------------------------------------------------
    virtual Error
    DoDetach () = 0;

    //------------------------------------------------------------------
    /// Called after detaching from a process.
    ///
    /// Allow Process plug-ins to execute some code after detaching
    /// from a process.
    //------------------------------------------------------------------
    virtual void
    DidDetach () {}

    //------------------------------------------------------------------
    /// Called before sending a signal to a process.
    ///
    /// Allow Process plug-ins to execute some code before sending a
    /// signal to a process.
    ///
    /// @return
    ///     Returns no error if it is safe to proceed with a call to
    ///     Process::DoSignal(int), otherwise an error describing what
    ///     prevents the signal from being sent.
    //------------------------------------------------------------------
    virtual Error
    WillSignal () { return Error(); }

    //------------------------------------------------------------------
    /// Sends a process a UNIX signal \a signal.
    ///
    /// @return
    ///     Returns an error object.
    //------------------------------------------------------------------
    virtual Error
    DoSignal (int signal) = 0;



    virtual Error
    WillDestroy () { return Error(); }

    virtual Error
    DoDestroy () = 0;

    virtual void
    DidDestroy () { }


    //------------------------------------------------------------------
    /// Called after sending a signal to a process.
    ///
    /// Allow Process plug-ins to execute some code after sending a
    /// signal to a process.
    //------------------------------------------------------------------
    virtual void
    DidSignal () {}


    //------------------------------------------------------------------
    /// Currently called as part of ShouldStop.
    /// FIXME: Should really happen when the target stops before the
    /// event is taken from the queue...
    ///
    /// This callback is called as the event
    /// is about to be queued up to allow Process plug-ins to execute
    /// some code prior to clients being notified that a process was
    /// stopped. Common operations include updating the thread list,
    /// invalidating any thread state (registers, stack, etc) prior to
    /// letting the notification go out.
    ///
    //------------------------------------------------------------------
    virtual void
    RefreshStateAfterStop () = 0;

    //------------------------------------------------------------------
    /// Get the target object pointer for this module.
    ///
    /// @return
    ///     A Target object pointer to the target that owns this
    ///     module.
    //------------------------------------------------------------------
    Target &
    GetTarget ();

    //------------------------------------------------------------------
    /// Get the const target object pointer for this module.
    ///
    /// @return
    ///     A const Target object pointer to the target that owns this
    ///     module.
    //------------------------------------------------------------------
    const Target &
    GetTarget () const;

    //------------------------------------------------------------------
    /// Get accessor for the current process state.
    ///
    /// @return
    ///     The current state of the process.
    ///
    /// @see lldb::StateType
    //------------------------------------------------------------------
    lldb::StateType
    GetState ();
    
    lldb::ExecutionResults
    RunThreadPlan (ExecutionContext &exe_ctx,    
                    lldb::ThreadPlanSP &thread_plan_sp,
                    bool stop_others,
                    bool try_all_threads,
                    bool discard_on_error,
                    uint32_t single_thread_timeout_usec,
                    Stream &errors);

    static const char *
    ExecutionResultAsCString (lldb::ExecutionResults result);

protected:
    friend class CommandObjectProcessLaunch;
    friend class ProcessEventData;
    friend class CommandObjectBreakpointCommand;
    
    void
    SetState (lldb::EventSP &event_sp);

    lldb::StateType
    GetPrivateState ();

public:
    //------------------------------------------------------------------
    /// Get the exit status for a process.
    ///
    /// @return
    ///     The process's return code, or -1 if the current process
    ///     state is not eStateExited.
    //------------------------------------------------------------------
    int
    GetExitStatus ();

    //------------------------------------------------------------------
    /// Get a textual description of what the process exited.
    ///
    /// @return
    ///     The textual description of why the process exited, or NULL
    ///     if there is no description available.
    //------------------------------------------------------------------
    const char *
    GetExitDescription ();


    virtual void
    DidExit ()
    {
    }

    //------------------------------------------------------------------
    /// Get the number of times this process has posted a stop event.
    ///
    /// @return
    ///     The number of times this process has stopped while being
    ///     debugged.
    //------------------------------------------------------------------
    uint32_t
    GetStopID () const;

    //------------------------------------------------------------------
    /// Set accessor for the process exit status (return code).
    ///
    /// Sometimes a child exits and the exit can be detected by global
    /// functions (signal handler for SIGCHLD for example). This
    /// accessor allows the exit status to be set from an external
    /// source.
    ///
    /// Setting this will cause a eStateExited event to be posted to
    /// the process event queue.
    ///
    /// @param[in] exit_status
    ///     The value for the process's return code.
    ///
    /// @see lldb::StateType
    //------------------------------------------------------------------
    virtual bool
    SetExitStatus (int exit_status, const char *cstr);

    //------------------------------------------------------------------
    /// Check if a process is still alive.
    ///
    /// @return
    ///     Returns \b true if the process is still valid, \b false
    ///     otherwise.
    //------------------------------------------------------------------
    virtual bool
    IsAlive () = 0;

    //------------------------------------------------------------------
    /// Actually do the reading of memory from a process.
    ///
    /// Subclasses must override this function and can return fewer 
    /// bytes than requested when memory requests are too large. This
    /// class will break up the memory requests and keep advancing the
    /// arguments along as needed. 
    ///
    /// @param[in] vm_addr
    ///     A virtual load address that indicates where to start reading
    ///     memory from.
    ///
    /// @param[in] size
    ///     The number of bytes to read.
    ///
    /// @param[out] buf
    ///     A byte buffer that is at least \a size bytes long that
    ///     will receive the memory bytes.
    ///
    /// @return
    ///     The number of bytes that were actually read into \a buf.
    //------------------------------------------------------------------
    virtual size_t
    DoReadMemory (lldb::addr_t vm_addr, 
                  void *buf, 
                  size_t size,
                  Error &error) = 0;

    //------------------------------------------------------------------
    /// Read of memory from a process.
    ///
    /// This function will read memory from the current process's
    /// address space and remove any traps that may have been inserted
    /// into the memory.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses, the subclasses should implement
    /// Process::DoReadMemory (lldb::addr_t, size_t, void *).
    ///
    /// @param[in] vm_addr
    ///     A virtual load address that indicates where to start reading
    ///     memory from.
    ///
    /// @param[out] buf
    ///     A byte buffer that is at least \a size bytes long that
    ///     will receive the memory bytes.
    ///
    /// @param[in] size
    ///     The number of bytes to read.
    ///
    /// @return
    ///     The number of bytes that were actually read into \a buf. If
    ///     the returned number is greater than zero, yet less than \a
    ///     size, then this function will get called again with \a 
    ///     vm_addr, \a buf, and \a size updated appropriately. Zero is
    ///     returned to indicate an error.
    //------------------------------------------------------------------
    size_t
    ReadMemory (lldb::addr_t vm_addr, 
                void *buf, 
                size_t size,
                Error &error);

    size_t
    ReadMemoryFromInferior (lldb::addr_t vm_addr, 
                            void *buf, 
                            size_t size,
                            Error &error);
    
    //------------------------------------------------------------------
    /// Reads an unsigned integer of the specified byte size from 
    /// process memory.
    ///
    /// @param[in] load_addr
    ///     A load address of the integer to read.
    ///
    /// @param[in] byte_size
    ///     The size in byte of the integer to read.
    ///
    /// @param[out] error
    ///     An error that indicates the success or failure of this
    ///     operation. If error indicates success (error.Success()), 
    ///     then the value returned can be trusted, otherwise zero
    ///     will be returned.
    ///
    /// @return
    ///     The unsigned integer that was read from the process memory
    ///     space. If the integer was smaller than a uint64_t, any
    ///     unused upper bytes will be zero filled. If the process
    ///     byte order differs from the host byte order, the integer
    ///     value will be appropriately byte swapped into host byte
    ///     order.
    //------------------------------------------------------------------
    uint64_t
    ReadUnsignedInteger (lldb::addr_t load_addr, 
                         size_t byte_size,
                         Error &error);
    //------------------------------------------------------------------
    /// Actually do the writing of memory to a process.
    ///
    /// @param[in] vm_addr
    ///     A virtual load address that indicates where to start writing
    ///     memory to.
    ///
    /// @param[in] buf
    ///     A byte buffer that is at least \a size bytes long that
    ///     contains the data to write.
    ///
    /// @param[in] size
    ///     The number of bytes to write.
    ///
    /// @return
    ///     The number of bytes that were actually written.
    //------------------------------------------------------------------
    virtual size_t
    DoWriteMemory (lldb::addr_t vm_addr, const void *buf, size_t size, Error &error) = 0;

    //------------------------------------------------------------------
    /// Write memory to a process.
    ///
    /// This function will write memory to the current process's
    /// address space and maintain any traps that might be present due
    /// to software breakpoints.
    ///
    /// This function is not meant to be overridden by Process
    /// subclasses, the subclasses should implement
    /// Process::DoWriteMemory (lldb::addr_t, size_t, void *).
    ///
    /// @param[in] vm_addr
    ///     A virtual load address that indicates where to start writing
    ///     memory to.
    ///
    /// @param[in] buf
    ///     A byte buffer that is at least \a size bytes long that
    ///     contains the data to write.
    ///
    /// @param[in] size
    ///     The number of bytes to write.
    ///
    /// @return
    ///     The number of bytes that were actually written.
    //------------------------------------------------------------------
    size_t
    WriteMemory (lldb::addr_t vm_addr, const void *buf, size_t size, Error &error);


    //------------------------------------------------------------------
    /// Actually allocate memory in the process.
    ///
    /// This function will allocate memory in the process's address
    /// space.  This can't rely on the generic function calling mechanism,
    /// since that requires this function.
    ///
    /// @param[in] size
    ///     The size of the allocation requested.
    ///
    /// @return
    ///     The address of the allocated buffer in the process, or
    ///     LLDB_INVALID_ADDRESS if the allocation failed.
    //------------------------------------------------------------------

    virtual lldb::addr_t
    DoAllocateMemory (size_t size, uint32_t permissions, Error &error) = 0;

    //------------------------------------------------------------------
    /// The public interface to allocating memory in the process.
    ///
    /// This function will allocate memory in the process's address
    /// space.  This can't rely on the generic function calling mechanism,
    /// since that requires this function.
    ///
    /// @param[in] size
    ///     The size of the allocation requested.
    ///
    /// @param[in] permissions
    ///     Or together any of the lldb::Permissions bits.  The permissions on
    ///     a given memory allocation can't be changed after allocation.  Note
    ///     that a block that isn't set writable can still be written on from lldb,
    ///     just not by the process itself.
    ///
    /// @return
    ///     The address of the allocated buffer in the process, or
    ///     LLDB_INVALID_ADDRESS if the allocation failed.
    //------------------------------------------------------------------

    lldb::addr_t
    AllocateMemory (size_t size, uint32_t permissions, Error &error);

    //------------------------------------------------------------------
    /// Actually deallocate memory in the process.
    ///
    /// This function will deallocate memory in the process's address
    /// space that was allocated with AllocateMemory.
    ///
    /// @param[in] ptr
    ///     A return value from AllocateMemory, pointing to the memory you
    ///     want to deallocate.
    ///
    /// @return
    ///     \btrue if the memory was deallocated, \bfalse otherwise.
    //------------------------------------------------------------------

    virtual Error
    DoDeallocateMemory (lldb::addr_t ptr) = 0;

    //------------------------------------------------------------------
    /// The public interface to deallocating memory in the process.
    ///
    /// This function will deallocate memory in the process's address
    /// space that was allocated with AllocateMemory.
    ///
    /// @param[in] ptr
    ///     A return value from AllocateMemory, pointing to the memory you
    ///     want to deallocate.
    ///
    /// @return
    ///     \btrue if the memory was deallocated, \bfalse otherwise.
    //------------------------------------------------------------------

    Error
    DeallocateMemory (lldb::addr_t ptr);

    //------------------------------------------------------------------
    /// Get any available STDOUT.
    ///
    /// If the process was launched without supplying valid file paths
    /// for stdin, stdout, and stderr, then the Process class might
    /// try to cache the STDOUT for the process if it is able. Events
    /// will be queued indicating that there is STDOUT available that
    /// can be retrieved using this function.
    ///
    /// @param[out] buf
    ///     A buffer that will receive any STDOUT bytes that are
    ///     currently available.
    ///
    /// @param[out] buf_size
    ///     The size in bytes for the buffer \a buf.
    ///
    /// @return
    ///     The number of bytes written into \a buf. If this value is
    ///     equal to \a buf_size, another call to this function should
    ///     be made to retrieve more STDOUT data.
    //------------------------------------------------------------------
    virtual size_t
    GetSTDOUT (char *buf, size_t buf_size, Error &error)
    {
        error.SetErrorString("stdout unsupported");
        return 0;
    }


    //------------------------------------------------------------------
    /// Get any available STDERR.
    ///
    /// If the process was launched without supplying valid file paths
    /// for stdin, stdout, and stderr, then the Process class might
    /// try to cache the STDERR for the process if it is able. Events
    /// will be queued indicating that there is STDERR available that
    /// can be retrieved using this function.
    ///
    /// @param[out] buf
    ///     A buffer that will receive any STDERR bytes that are
    ///     currently available.
    ///
    /// @param[out] buf_size
    ///     The size in bytes for the buffer \a buf.
    ///
    /// @return
    ///     The number of bytes written into \a buf. If this value is
    ///     equal to \a buf_size, another call to this function should
    ///     be made to retrieve more STDERR data.
    //------------------------------------------------------------------
    virtual size_t
    GetSTDERR (char *buf, size_t buf_size, Error &error)
    {
        error.SetErrorString("stderr unsupported");
        return 0;
    }

    virtual size_t
    PutSTDIN (const char *buf, size_t buf_size, Error &error) 
    {
        error.SetErrorString("stdin unsupported");
        return 0;
    }

    //----------------------------------------------------------------------
    // Process Breakpoints
    //----------------------------------------------------------------------
    virtual size_t
    GetSoftwareBreakpointTrapOpcode (BreakpointSite* bp_site) = 0;

    virtual Error
    EnableBreakpoint (BreakpointSite *bp_site) = 0;

    virtual Error
    DisableBreakpoint (BreakpointSite *bp_site) = 0;

    // This is implemented completely using the lldb::Process API. Subclasses
    // don't need to implement this function unless the standard flow of
    // read existing opcode, write breakpoint opcode, verify breakpoint opcode
    // doesn't work for a specific process plug-in.
    virtual Error
    EnableSoftwareBreakpoint (BreakpointSite *bp_site);

    // This is implemented completely using the lldb::Process API. Subclasses
    // don't need to implement this function unless the standard flow of
    // restoring original opcode in memory and verifying the restored opcode
    // doesn't work for a specific process plug-in.
    virtual Error
    DisableSoftwareBreakpoint (BreakpointSite *bp_site);

    BreakpointSiteList &
    GetBreakpointSiteList();

    const BreakpointSiteList &
    GetBreakpointSiteList() const;

    void
    DisableAllBreakpointSites ();

    Error
    ClearBreakpointSiteByID (lldb::user_id_t break_id);

    lldb::break_id_t
    CreateBreakpointSite (lldb::BreakpointLocationSP &owner,
                          bool use_hardware);

    Error
    DisableBreakpointSiteByID (lldb::user_id_t break_id);

    Error
    EnableBreakpointSiteByID (lldb::user_id_t break_id);


    // BreakpointLocations use RemoveOwnerFromBreakpointSite to remove
    // themselves from the owner's list of this breakpoint sites.  This has to
    // be a static function because you can't be sure that removing the
    // breakpoint from it's containing map won't delete the breakpoint site,
    // and doing that in an instance method isn't copasetic.
    void
    RemoveOwnerFromBreakpointSite (lldb::user_id_t owner_id,
                                   lldb::user_id_t owner_loc_id,
                                   lldb::BreakpointSiteSP &bp_site_sp);

    //----------------------------------------------------------------------
    // Process Watchpoints (optional)
    //----------------------------------------------------------------------
    virtual Error
    EnableWatchpoint (WatchpointLocation *bp_loc);

    virtual Error
    DisableWatchpoint (WatchpointLocation *bp_loc);

    //------------------------------------------------------------------
    // Thread Queries
    //------------------------------------------------------------------
    virtual uint32_t
    UpdateThreadListIfNeeded () = 0;

    ThreadList &
    GetThreadList ();

    const ThreadList &
    GetThreadList () const;

    uint32_t
    GetNextThreadIndexID ();

    //------------------------------------------------------------------
    // Event Handling
    //------------------------------------------------------------------
    lldb::StateType
    GetNextEvent (lldb::EventSP &event_sp);

    lldb::StateType
    WaitForProcessToStop (const TimeValue *timeout);

    lldb::StateType
    WaitForStateChangedEvents (const TimeValue *timeout, lldb::EventSP &event_sp);
    
    Event *
    PeekAtStateChangedEvents ();
    

    //------------------------------------------------------------------
    /// If you need to ensure that you and only you will hear about some public
    /// event, then make a new listener, set to listen to process events, and
    /// then call this with that listener.  Then you will have to wait on that
    /// listener explicitly for events (rather than using the GetNextEvent & WaitFor*
    /// calls above.  Be sure to call RestoreProcessEvents when you are done.
    ///
    /// @param[in] listener
    ///     This is the new listener to whom all process events will be delivered.
    ///
    /// @return
    ///     Returns \b true if the new listener could be installed,
    ///     \b false otherwise.
    //------------------------------------------------------------------
    bool
    HijackProcessEvents (Listener *listener);
    
    //------------------------------------------------------------------
    /// Restores the process event broadcasting to its normal state.
    ///
    //------------------------------------------------------------------
    void
    RestoreProcessEvents ();

protected:
    //------------------------------------------------------------------
    /// This is the part of the event handling that for a process event.
    /// It decides what to do with the event and returns true if the
    /// event needs to be propagated to the user, and false otherwise.
    /// If the event is not propagated, this call will most likely set
    /// the target to executing again.
    ///
    /// @param[in] event_ptr
    ///     This is the event we are handling.
    ///
    /// @return
    ///     Returns \b true if the event should be reported to the
    ///     user, \b false otherwise.
    //------------------------------------------------------------------
    bool
    ShouldBroadcastEvent (Event *event_ptr);

public:
    //------------------------------------------------------------------
    /// Gets the byte order for this process.
    ///
    /// @return
    ///     A valid ByteOrder enumeration, or eByteOrderInvalid.
    //------------------------------------------------------------------
    lldb::ByteOrder
    GetByteOrder () const
    {
        return m_byte_order;
    }
    
    void
    SetByteOrder (lldb::ByteOrder byte_order)
    {
        m_byte_order = byte_order;
    }

    const ConstString &
    GetTargetTriple ()
    {
        return m_target_triple;
    }

    const ABI *
    GetABI ();

    virtual DynamicLoader *
    GetDynamicLoader ();
    
    virtual LanguageRuntime *
    GetLanguageRuntime (lldb::LanguageType language);

    virtual CPPLanguageRuntime *
    GetCPPLanguageRuntime ();

    virtual ObjCLanguageRuntime *
    GetObjCLanguageRuntime ();

    bool
    IsRunning () const;
    
    DynamicCheckerFunctions *GetDynamicCheckers()
    {
        return m_dynamic_checkers_ap.get();
    }
    
    void SetDynamicCheckers(DynamicCheckerFunctions *dynamic_checkers)
    {
        m_dynamic_checkers_ap.reset(dynamic_checkers);
    }

    virtual bool
    StartNoticingNewThreads()
    {   
        return true;
    }
    
    virtual bool
    StopNoticingNewThreads()
    {   
        return true;
    }
    
    //------------------------------------------------------------------
    // lldb::ExecutionContextScope pure virtual functions
    //------------------------------------------------------------------
    virtual Target *
    CalculateTarget ();

    virtual Process *
    CalculateProcess ();

    virtual Thread *
    CalculateThread ();

    virtual StackFrame *
    CalculateStackFrame ();

    virtual void
    CalculateExecutionContext (ExecutionContext &exe_ctx);
    
    lldb::ProcessSP
    GetSP ();
    
protected:
    class MemoryCache
    {
    public:
        //------------------------------------------------------------------
        // Constructors and Destructors
        //------------------------------------------------------------------
        MemoryCache ();
        
        ~MemoryCache ();
        
        void
        Clear();
        
        void
        Flush (lldb::addr_t addr, size_t size);

        size_t
        Read (Process *process,
              lldb::addr_t addr, 
              void *dst, 
              size_t dst_len,
              Error &error);
        
    protected:
        typedef std::map<lldb::addr_t, lldb::DataBufferSP> collection;
        //------------------------------------------------------------------
        // Classes that inherit from MemoryCache can see and modify these
        //------------------------------------------------------------------
        uint32_t m_cache_line_byte_size;
        Mutex m_cache_mutex;
        collection m_cache;

    private:
        DISALLOW_COPY_AND_ASSIGN (MemoryCache);
    };


    //------------------------------------------------------------------
    // Member variables
    //------------------------------------------------------------------
    Target &                    m_target;               ///< The target that owns this process.
    ThreadSafeValue<lldb::StateType>  m_public_state;
    ThreadSafeValue<lldb::StateType>  m_private_state; // The actual state of our process
    Broadcaster                 m_private_state_broadcaster;  // This broadcaster feeds state changed events into the private state thread's listener.
    Broadcaster                 m_private_state_control_broadcaster; // This is the control broadcaster, used to pause, resume & stop the private state thread.
    Listener                    m_private_state_listener;     // This is the listener for the private state thread.
    Predicate<bool>             m_private_state_control_wait; /// This Predicate is used to signal that a control operation is complete.
    lldb::thread_t              m_private_state_thread;  // Thread ID for the thread that watches interal state events
    uint32_t                    m_stop_id;              ///< A count of many times the process has stopped.
    uint32_t                    m_thread_index_id;      ///< Each thread is created with a 1 based index that won't get re-used.
    int                         m_exit_status;          ///< The exit status of the process, or -1 if not set.
    std::string                 m_exit_string;          ///< A textual description of why a process exited.
    ThreadList                  m_thread_list;          ///< The threads for this process.
    std::vector<Notifications>  m_notifications;        ///< The list of notifications that this process can deliver.
    std::vector<lldb::addr_t>   m_image_tokens;
    Listener                    &m_listener;
    BreakpointSiteList          m_breakpoint_site_list; ///< This is the list of breakpoint locations we intend
                                                        ///< to insert in the target.
    std::auto_ptr<DynamicCheckerFunctions>  m_dynamic_checkers_ap; ///< The functions used by the expression parser to validate data that expressions use.
    UnixSignals                 m_unix_signals;         /// This is the current signal set for this process.
    ConstString                 m_target_triple;
    lldb::ByteOrder             m_byte_order;           /// The byte order of the process. Should be set in DidLaunch/DidAttach.
    uint32_t                    m_addr_byte_size;       /// The size in bytes of an address/pointer for the inferior process. Should be set in DidLaunch/DidAttach.
    lldb::ABISP                 m_abi_sp;
    lldb::InputReaderSP         m_process_input_reader;
    lldb_private::Communication m_stdio_communication;
    lldb_private::Mutex         m_stdio_communication_mutex;
    std::string                 m_stdout_data;
    MemoryCache                 m_memory_cache;

    typedef std::map<lldb::LanguageType, lldb::LanguageRuntimeSP> LanguageRuntimeCollection; 
    LanguageRuntimeCollection m_language_runtimes;

    size_t
    RemoveBreakpointOpcodesFromBuffer (lldb::addr_t addr, size_t size, uint8_t *buf) const;

    void
    SynchronouslyNotifyStateChanged (lldb::StateType state);

    void
    SetPublicState (lldb::StateType new_state);

    void
    SetPrivateState (lldb::StateType state);

    bool
    StartPrivateStateThread ();

    void
    StopPrivateStateThread ();

    void
    PausePrivateStateThread ();

    void
    ResumePrivateStateThread ();

    static void *
    PrivateStateThread (void *arg);

    void *
    RunPrivateStateThread ();

    void
    HandlePrivateEvent (lldb::EventSP &event_sp);

    lldb::StateType
    WaitForProcessStopPrivate (const TimeValue *timeout, lldb::EventSP &event_sp);

    Error
    CompleteAttach ();


    // This waits for both the state change broadcaster, and the control broadcaster.
    // If control_only, it only waits for the control broadcaster.

    bool
    WaitForEventsPrivate (const TimeValue *timeout, lldb::EventSP &event_sp, bool control_only);

    lldb::StateType
    WaitForStateChangedEventsPrivate (const TimeValue *timeout, lldb::EventSP &event_sp);

    lldb::StateType
    WaitForState (const TimeValue *timeout,
                  const lldb::StateType *match_states,
                  const uint32_t num_match_states);

    size_t
    WriteMemoryPrivate (lldb::addr_t addr, const void *buf, size_t size, Error &error);
    
    void
    AppendSTDOUT (const char *s, size_t len);
    
    static void
    STDIOReadThreadBytesReceived (void *baton, const void *src, size_t src_len);
    
    void
    PushProcessInputReader ();
    
    void 
    PopProcessInputReader ();
    
    void
    ResetProcessInputReader ();
    
    void
    SetUpProcessInputReader (int file_descriptor);
    
    static size_t
    ProcessInputReaderCallback (void *baton,
                                InputReader &reader,
                                lldb::InputReaderAction notification,
                                const char *bytes,
                                size_t bytes_len);
    
    
private:
    //------------------------------------------------------------------
    // For Process only
    //------------------------------------------------------------------
    void ControlPrivateStateThread (uint32_t signal);

    DISALLOW_COPY_AND_ASSIGN (Process);

};

} // namespace lldb_private

#endif  // liblldb_Process_h_
