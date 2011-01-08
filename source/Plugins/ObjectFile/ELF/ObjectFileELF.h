//===-- ObjectFileELF.h --------------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjectFileELF_h_
#define liblldb_ObjectFileELF_h_

#include <stdint.h>
#include <vector>

#include "lldb/lldb-private.h"
#include "lldb/Core/FileSpec.h"
#include "lldb/Symbol/ObjectFile.h"

#include "ELFHeader.h"

//------------------------------------------------------------------------------
/// @class ObjectFileELF
/// @brief Generic ELF object file reader.
///
/// This class provides a generic ELF (32/64 bit) reader plugin implementing the
/// ObjectFile protocol.
class ObjectFileELF :
    public lldb_private::ObjectFile
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

    static lldb_private::ObjectFile *
    CreateInstance(lldb_private::Module* module,
                   lldb::DataBufferSP& dataSP,
                   const lldb_private::FileSpec* file,
                   lldb::addr_t offset,
                   lldb::addr_t length);

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
    ExecutePluginCommand(lldb_private::Args &command,
                         lldb_private::Stream *strm);

    virtual lldb_private::Log *
    EnablePluginLogging(lldb_private::Stream *strm, 
                        lldb_private::Args &command);

    //------------------------------------------------------------------
    // ObjectFile Protocol.
    //------------------------------------------------------------------
    virtual
    ~ObjectFileELF();

    virtual bool
    ParseHeader();

    virtual lldb::ByteOrder
    GetByteOrder() const;

    virtual bool
    IsExecutable () const;

    virtual size_t
    GetAddressByteSize() const;

    virtual lldb_private::Symtab *
    GetSymtab();

    virtual lldb_private::SectionList *
    GetSectionList();

    virtual void
    Dump(lldb_private::Stream *s);

    virtual bool
    GetTargetTriple(lldb_private::ConstString &target_triple);

    virtual bool
    GetUUID(lldb_private::UUID* uuid);

    virtual uint32_t
    GetDependentModules(lldb_private::FileSpecList& files);

private:
    ObjectFileELF(lldb_private::Module* module,
                  lldb::DataBufferSP& dataSP,
                  const lldb_private::FileSpec* file,
                  lldb::addr_t offset,
                  lldb::addr_t length);

    typedef std::vector<elf::ELFProgramHeader>  ProgramHeaderColl;
    typedef ProgramHeaderColl::iterator         ProgramHeaderCollIter;
    typedef ProgramHeaderColl::const_iterator   ProgramHeaderCollConstIter;

    typedef std::vector<elf::ELFSectionHeader>  SectionHeaderColl;
    typedef SectionHeaderColl::iterator         SectionHeaderCollIter;
    typedef SectionHeaderColl::const_iterator   SectionHeaderCollConstIter;

    /// Version of this reader common to all plugins based on this class.
    static const uint32_t m_plugin_version = 1;

    /// ELF file header.
    elf::ELFHeader m_header;

    /// Collection of program headers.
    ProgramHeaderColl m_program_headers;

    /// Collection of section headers.
    SectionHeaderColl m_section_headers;

    /// List of sections present in this ELF object file.
    mutable std::auto_ptr<lldb_private::SectionList> m_sections_ap;

    /// Table of all non-dynamic symbols present in this object file.
    mutable std::auto_ptr<lldb_private::Symtab> m_symtab_ap;

    /// List of file specifications corresponding to the modules (shared
    /// libraries) on which this object file depends.
    mutable std::auto_ptr<lldb_private::FileSpecList> m_filespec_ap;

    /// Data extractor holding the string table used to resolve section names.
    lldb_private::DataExtractor m_shstr_data;

    /// Returns a 1 based index of the given section header.
    unsigned
    SectionIndex(const SectionHeaderCollIter &I);

    /// Returns a 1 based index of the given section header.
    unsigned
    SectionIndex(const SectionHeaderCollConstIter &I) const;

    /// Parses all section headers present in this object file and populates
    /// m_program_headers.  This method will compute the header list only once.
    /// Returns the number of headers parsed.
    size_t
    ParseProgramHeaders();

    /// Parses all section headers present in this object file and populates
    /// m_section_headers.  This method will compute the header list only once.
    /// Returns the number of headers parsed.
    size_t
    ParseSectionHeaders();

    /// Scans the dynamic section and locates all dependent modules (shared
    /// libraries) populating m_filespec_ap.  This method will compute the
    /// dependent module list only once.  Returns the number of dependent
    /// modules parsed.
    size_t
    ParseDependentModules();

    /// Populates m_symtab_ap will all non-dynamic linker symbols.  This method
    /// will parse the symbols only once.  Returns the number of symbols parsed.
    void
    ParseSymbolTable(lldb_private::Symtab *symbol_table,
                     const elf::ELFSectionHeader &symtab_section,
                     lldb::user_id_t symtab_id);

    /// Loads the section name string table into m_shstr_data.  Returns the
    /// number of bytes constituting the table.
    size_t
    GetSectionHeaderStringTable();

    /// Utility method for looking up a section given its name.  Returns the
    /// index of the corresponding section or zero if no section with the given
    /// name can be found (note that section indices are always 1 based, and so
    /// section index 0 is never valid).
    lldb::user_id_t
    GetSectionIndexByName(const char *name);

    /// @name  ELF header dump routines
    //@{
    static void
    DumpELFHeader(lldb_private::Stream *s, const elf::ELFHeader& header);

    static void
    DumpELFHeader_e_ident_EI_DATA(lldb_private::Stream *s,
                                  unsigned char ei_data);

    static void
    DumpELFHeader_e_type(lldb_private::Stream *s, elf::elf_half e_type);
    //@}

    /// @name ELF program header dump routines
    //@{
    void
    DumpELFProgramHeaders(lldb_private::Stream *s);

    static void
    DumpELFProgramHeader(lldb_private::Stream *s, 
                         const elf::ELFProgramHeader &ph);

    static void
    DumpELFProgramHeader_p_type(lldb_private::Stream *s, elf::elf_word p_type);

    static void
    DumpELFProgramHeader_p_flags(lldb_private::Stream *s, 
                                 elf::elf_word p_flags);
    //@}

    /// @name ELF section header dump routines
    //@{
    void
    DumpELFSectionHeaders(lldb_private::Stream *s);

    static void
    DumpELFSectionHeader(lldb_private::Stream *s, 
                         const elf::ELFSectionHeader& sh);

    static void
    DumpELFSectionHeader_sh_type(lldb_private::Stream *s, 
                                 elf::elf_word sh_type);

    static void
    DumpELFSectionHeader_sh_flags(lldb_private::Stream *s, 
                                  elf::elf_word sh_flags);
    //@}

    /// ELF dependent module dump routine.
    void
    DumpDependentModules(lldb_private::Stream *s);

};

#endif // #ifndef liblldb_ObjectFileELF_h_
