//===-- DynamicLoaderLinux.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <link.h>

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Log.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "DynamicLoaderLinuxDYLD.h"

using namespace lldb;
using namespace lldb_private;

void
DynamicLoaderLinuxDYLD::Initialize()
{
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(),
                                  CreateInstance);
}

void
DynamicLoaderLinuxDYLD::Terminate()
{
}

const char *
DynamicLoaderLinuxDYLD::GetPluginName()
{
    return "DynamicLoaderLinuxDYLD";
}

const char *
DynamicLoaderLinuxDYLD::GetShortPluginName()
{
    return "linux-dyld";
}

const char *
DynamicLoaderLinuxDYLD::GetPluginNameStatic()
{
    return "dynamic-loader.linux-dyld";
}

const char *
DynamicLoaderLinuxDYLD::GetPluginDescriptionStatic()
{
    return "Dynamic loader plug-in that watches for shared library "
           "loads/unloads in Linux processes.";
}

void
DynamicLoaderLinuxDYLD::GetPluginCommandHelp(const char *command, Stream *strm)
{
}

uint32_t
DynamicLoaderLinuxDYLD::GetPluginVersion()
{
    return 1;
}

DynamicLoader *
DynamicLoaderLinuxDYLD::CreateInstance(Process *process)
{
    return new DynamicLoaderLinuxDYLD(process);
}

DynamicLoaderLinuxDYLD::DynamicLoaderLinuxDYLD(Process *process)
    : DynamicLoader(process),
      m_rendezvous(process)
{
}

DynamicLoaderLinuxDYLD::~DynamicLoaderLinuxDYLD()
{
}

void
DynamicLoaderLinuxDYLD::DidAttach()
{
    Module *executable = m_process->GetTarget().GetExecutableModule().get();
    if (executable != NULL)
        UpdateLoadedSections(executable);

    ResolveImageInfo();
}

void
DynamicLoaderLinuxDYLD::DidLaunch()
{
    Module *executable = m_process->GetTarget().GetExecutableModule().get();
    if (executable != NULL)
        UpdateLoadedSections(executable);

    ProbeEntry(executable);
    ResolveImageInfo();
}

Error
DynamicLoaderLinuxDYLD::ExecutePluginCommand(Args &command, Stream *strm)
{
    return Error();
}

Log *
DynamicLoaderLinuxDYLD::EnablePluginLogging(Stream *strm, Args &command)
{
    return NULL;
}

Error
DynamicLoaderLinuxDYLD::CanLoadImage()
{
    return Error();
}

void
DynamicLoaderLinuxDYLD::UpdateLoadedSections(Module *module, addr_t base_addr)
{
    ObjectFile *obj_file = module->GetObjectFile();
    SectionList *sections = obj_file->GetSectionList();
    SectionLoadList &load_list = m_process->GetTarget().GetSectionLoadList();

    // FIXME: SectionList provides iterator types, but no begin/end methods.
    size_t num_sections = sections->GetSize();
    for (unsigned i = 0; i < num_sections; ++i)
    {
        Section *section = sections->GetSectionAtIndex(i).get();

        lldb::addr_t new_load_addr = section->GetFileAddress() + base_addr;
        lldb::addr_t old_load_addr = load_list.GetSectionLoadAddress(section);

        if (old_load_addr == LLDB_INVALID_ADDRESS ||
            old_load_addr != new_load_addr)
            load_list.SetSectionLoadAddress(section, new_load_addr);
    }
}

bool
DynamicLoaderLinuxDYLD::ResolveImageInfo()
{
    LogSP log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));

    if (!m_rendezvous.Resolve())
        return false;

    UpdateLinkMap();

    if (log)
        m_rendezvous.DumpToLog(log);

    return true;
}

void
DynamicLoaderLinuxDYLD::ProbeEntry(Module *module)
{
    ObjectFile *obj_file = module->GetObjectFile();
    addr_t entry_addr = obj_file->GetEntryPoint();
    Breakpoint *entry_break;

    if (entry_addr == LLDB_INVALID_ADDRESS)
        return;
    
    entry_break = m_process->GetTarget().CreateBreakpoint(entry_addr, true).get();
    entry_break->SetCallback(EntryBreakpointHit, this, true);
}

bool
DynamicLoaderLinuxDYLD::EntryBreakpointHit(void *baton, 
                                           StoppointCallbackContext *context, 
                                           user_id_t break_id, 
                                           user_id_t break_loc_id)
{
    DynamicLoaderLinuxDYLD* dyld_instance;

    dyld_instance = static_cast<DynamicLoaderLinuxDYLD*>(baton);
    dyld_instance->ResolveImageInfo();
    return false; /* Continue running. */
}

user_id_t
DynamicLoaderLinuxDYLD::SetNotificationBreakpoint()
{
    Breakpoint *dyld_break;
    addr_t break_addr;

    if (!ResolveImageInfo())
        return false;

    break_addr = m_rendezvous.GetBreakAddress();
    dyld_break = m_process->GetTarget().CreateBreakpoint(break_addr, true).get();
    dyld_break->SetCallback(NotifyBreakpointHit, this, true);
    return dyld_break->GetID();
}

bool
DynamicLoaderLinuxDYLD::NotifyBreakpointHit(void *baton, 
                                            StoppointCallbackContext *context, 
                                            user_id_t break_id, 
                                            user_id_t break_loc_id)
{
    DynamicLoaderLinuxDYLD* dyld_instance;

    dyld_instance = static_cast<DynamicLoaderLinuxDYLD*>(baton);
    dyld_instance->UpdateImageInfo();

    // Return true to stop the target, false to just let the target run.
    return dyld_instance->GetStopWhenImagesChange();
}

void
DynamicLoaderLinuxDYLD::UpdateImageInfo()
{
}

ThreadPlanSP
DynamicLoaderLinuxDYLD::GetStepThroughTrampolinePlan(Thread &thread, bool stop_others)
{
    ThreadPlanSP thread_plan_sp;

    std::cerr << "DynamicLoaderLinux: GetStepThroughTrampolinePlan not implemented\n";
    return thread_plan_sp;
}

void
DynamicLoaderLinuxDYLD::UpdateLinkMap()
{
    DYLDRendezvous::iterator I;
    DYLDRendezvous::iterator E;
    ModuleList module_list;
    LogSP log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_DYNAMIC_LOADER));
    
    for (I = m_rendezvous.begin(), E = m_rendezvous.end(); I != E; ++I)
    {
        FileSpec file(I->path.c_str(), false);
        ModuleSP module_sp = LoadModuleAtAddress(file, I->base_addr);
        
        if (!module_sp.empty()) {
            module_list.Append(module_sp);
            if (log) 
                log->Printf("DYLD Loaded: \n", I->path.c_str());
        }
    }

    m_process->GetTarget().ModulesDidLoad(module_list);
}

ModuleSP
DynamicLoaderLinuxDYLD::LoadModuleAtAddress(const FileSpec &file, addr_t base_addr)
{
    Target &target = m_process->GetTarget();
    ModuleList &modules = target.GetImages();
    ModuleSP module_sp;

    if ((module_sp = modules.FindFirstModuleForFileSpec(file))) 
    {
        UpdateLoadedSections(module_sp.get(), base_addr);
    }
    else if ((module_sp = target.GetSharedModule(file, target.GetArchitecture()))) 
    {
        UpdateLoadedSections(module_sp.get(), base_addr);
        modules.Append(module_sp);
    }

    return module_sp;
}
