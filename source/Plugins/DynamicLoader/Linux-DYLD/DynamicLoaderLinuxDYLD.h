//===-- DynamicLoaderLinux.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DynamicLoaderLinux_H_
#define liblldb_DynamicLoaderLinux_H_

// C Includes
// C++ Includes
#include <map>
#include <vector>
#include <string>

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Target/DynamicLoader.h"
#include "DYLDRendezvous.h"

class DynamicLoaderLinuxDYLD : public lldb_private::DynamicLoader
{
public:

    static void
    Initialize();

    static void
    Terminate();

    static const char *
    GetPluginNameStatic();

    static const char *
    GetPluginDescriptionStatic();

    static lldb_private::DynamicLoader *
    CreateInstance(lldb_private::Process *process);

    DynamicLoaderLinuxDYLD(lldb_private::Process *process);

    virtual
    ~DynamicLoaderLinuxDYLD();

    //------------------------------------------------------------------
    // DynamicLoader protocol
    //------------------------------------------------------------------

    virtual void
    DidAttach();

    virtual void
    DidLaunch();

    virtual lldb::ThreadPlanSP
    GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                 bool stop_others);

    virtual lldb_private::Error
    CanLoadImage();

    //------------------------------------------------------------------
    // PluginInterface protocol
    //------------------------------------------------------------------
    virtual const char *
    GetPluginName();

    virtual const char *
    GetShortPluginName();

    virtual uint32_t
    GetPluginVersion();

    virtual void
    GetPluginCommandHelp(const char *command, lldb_private::Stream *strm);

    virtual lldb_private::Error
    ExecutePluginCommand(lldb_private::Args &command, lldb_private::Stream *strm);

    virtual lldb_private::Log *
    EnablePluginLogging(lldb_private::Stream *strm, lldb_private::Args &command);

protected:

    DYLDRendezvous m_rendezvous;

    bool
    ResolveImageInfo();

    /// @returns ID of the dynamic loader rendezvous breakpoint on success else
    /// LLDB_INVALID_BREAK_ID on error.
    lldb::user_id_t
    SetNotificationBreakpoint();

    static bool
    NotifyBreakpointHit(void *baton, 
                        lldb_private::StoppointCallbackContext *context, 
                        lldb::user_id_t break_id, 
                        lldb::user_id_t break_loc_id);

    void
    UpdateImageInfo();

    void
    UpdateLoadedSections(lldb_private::Module *module, 
                         lldb::addr_t base_addr = 0);

    void
    ProbeEntry(lldb_private::Module *module);

    static bool
    EntryBreakpointHit(void *baton, 
                       lldb_private::StoppointCallbackContext *context, 
                       lldb::user_id_t break_id, 
                       lldb::user_id_t break_loc_id);

    void
    UpdateLinkMap();

    lldb::ModuleSP
    LoadModuleAtAddress(const lldb_private::FileSpec &file, lldb::addr_t base_addr);

private:
    DISALLOW_COPY_AND_ASSIGN(DynamicLoaderLinuxDYLD);
};

#endif  // liblldb_DynamicLoaderLinuxDYLD_H_
