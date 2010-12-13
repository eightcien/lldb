//===-- ObjectContainerUniversalMachO.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectContainerUniversalMachO_h_
#define liblldb_ObjectContainerUniversalMachO_h_

#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Core/FileSpec.h"

#include "llvm/Support/MachO.h"

class ObjectContainerUniversalMachO :
    public lldb_private::ObjectContainer
{
public:
    //------------------------------------------------------------------
    // Static Functions
    //------------------------------------------------------------------
    static void
    Initialize();

    static void
    Terminate();

    static const char *
    GetPluginNameStatic();

    static const char *
    GetPluginDescriptionStatic();

    static lldb_private::ObjectContainer *
    CreateInstance (lldb_private::Module* module,
                    lldb::DataBufferSP& dataSP,
                    const lldb_private::FileSpec *file,
                    lldb::addr_t offset,
                    lldb::addr_t length);

    static bool
    MagicBytesMatch (lldb::DataBufferSP& dataSP);

    //------------------------------------------------------------------
    // Member Functions
    //------------------------------------------------------------------
    ObjectContainerUniversalMachO (lldb_private::Module* module,
                                   lldb::DataBufferSP& dataSP,
                                   const lldb_private::FileSpec *file,
                                   lldb::addr_t offset,
                                   lldb::addr_t length);

    virtual
    ~ObjectContainerUniversalMachO();

    virtual bool
    ParseHeader ();

    virtual void
    Dump (lldb_private::Stream *s) const;

    virtual size_t
    GetNumArchitectures () const;

    virtual bool
    GetArchitectureAtIndex (uint32_t cpu_idx, lldb_private::ArchSpec& arch) const;

    virtual lldb_private::ObjectFile *
    GetObjectFile (const lldb_private::FileSpec *file);

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
    GetPluginCommandHelp (const char *command, lldb_private::Stream *strm);

    virtual lldb_private::Error
    ExecutePluginCommand (lldb_private::Args &command, lldb_private::Stream *strm);

    virtual lldb_private::Log *
    EnablePluginLogging (lldb_private::Stream *strm, lldb_private::Args &command);


protected:
    llvm::MachO::fat_header m_header;
    std::vector<llvm::MachO::fat_arch> m_fat_archs;
};

#endif  // liblldb_ObjectContainerUniversalMachO_h_
