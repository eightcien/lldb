//===-- SymbolFileDWARF.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARF.h"

// Other libraries and framework includes
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/DeclSpec.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/Scalar.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/Timer.h"
#include "lldb/Core/Value.h"

#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/ClangExternalASTSourceCallbacks.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/VariableList.h"

#include "DWARFCompileUnit.h"
#include "DWARFDebugAbbrev.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugInfoEntry.h"
#include "DWARFDebugLine.h"
#include "DWARFDebugPubnames.h"
#include "DWARFDebugRanges.h"
#include "DWARFDIECollection.h"
#include "DWARFFormValue.h"
#include "DWARFLocationList.h"
#include "LogChannelDWARF.h"
#include "SymbolFileDWARFDebugMap.h"

#include <map>

//#define ENABLE_DEBUG_PRINTF // COMMENT OUT THIS LINE PRIOR TO CHECKIN

#ifdef ENABLE_DEBUG_PRINTF
#include <stdio.h>
#define DEBUG_PRINTF(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...)
#endif

#define DIE_IS_BEING_PARSED ((lldb_private::Type*)1)

using namespace lldb;
using namespace lldb_private;


static AccessType
DW_ACCESS_to_AccessType (uint32_t dwarf_accessibility)
{
    switch (dwarf_accessibility)
    {
        case DW_ACCESS_public:      return eAccessPublic;
        case DW_ACCESS_private:     return eAccessPrivate;
        case DW_ACCESS_protected:   return eAccessProtected;
        default:                    break;
    }
    return eAccessNone;
}

void
SymbolFileDWARF::Initialize()
{
    LogChannelDWARF::Initialize();
    PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                   GetPluginDescriptionStatic(),
                                   CreateInstance);
}

void
SymbolFileDWARF::Terminate()
{
    PluginManager::UnregisterPlugin (CreateInstance);
    LogChannelDWARF::Initialize();
}


const char *
SymbolFileDWARF::GetPluginNameStatic()
{
    return "symbol-file.dwarf2";
}

const char *
SymbolFileDWARF::GetPluginDescriptionStatic()
{
    return "DWARF and DWARF3 debug symbol file reader.";
}


SymbolFile*
SymbolFileDWARF::CreateInstance (ObjectFile* obj_file)
{
    return new SymbolFileDWARF(obj_file);
}

TypeList *          
SymbolFileDWARF::GetTypeList ()
{
    if (m_debug_map_symfile)
        return m_debug_map_symfile->GetTypeList();
    return m_obj_file->GetModule()->GetTypeList();

}

//----------------------------------------------------------------------
// Gets the first parent that is a lexical block, function or inlined
// subroutine, or compile unit.
//----------------------------------------------------------------------
static const DWARFDebugInfoEntry *
GetParentSymbolContextDIE(const DWARFDebugInfoEntry *child_die)
{
    const DWARFDebugInfoEntry *die;
    for (die = child_die->GetParent(); die != NULL; die = die->GetParent())
    {
        dw_tag_t tag = die->Tag();

        switch (tag)
        {
        case DW_TAG_compile_unit:
        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
        case DW_TAG_lexical_block:
            return die;
        }
    }
    return NULL;
}


SymbolFileDWARF::SymbolFileDWARF(ObjectFile* objfile) :
    SymbolFile (objfile),
    m_debug_map_symfile (NULL),
    m_clang_tu_decl (NULL),
    m_flags(),
    m_data_debug_abbrev(),
    m_data_debug_frame(),
    m_data_debug_info(),
    m_data_debug_line(),
    m_data_debug_loc(),
    m_data_debug_ranges(),
    m_data_debug_str(),
    m_abbr(),
    m_aranges(),
    m_info(),
    m_line(),
    m_function_basename_index(),
    m_function_fullname_index(),
    m_function_method_index(),
    m_function_selector_index(),
    m_objc_class_selectors_index(),
    m_global_index(),
    m_type_index(),
    m_namespace_index(),
    m_indexed (false),
    m_is_external_ast_source (false),
    m_ranges(),
    m_unique_ast_type_map ()
{
}

SymbolFileDWARF::~SymbolFileDWARF()
{
    if (m_is_external_ast_source)
        m_obj_file->GetModule()->GetClangASTContext().RemoveExternalSource ();
}

static const ConstString &
GetDWARFMachOSegmentName ()
{
    static ConstString g_dwarf_section_name ("__DWARF");
    return g_dwarf_section_name;
}

UniqueDWARFASTTypeMap &
SymbolFileDWARF::GetUniqueDWARFASTTypeMap ()
{
    if (m_debug_map_symfile)
        return m_debug_map_symfile->GetUniqueDWARFASTTypeMap ();
    return m_unique_ast_type_map;
}

ClangASTContext &       
SymbolFileDWARF::GetClangASTContext ()
{
    if (m_debug_map_symfile)
        return m_debug_map_symfile->GetClangASTContext ();

    ClangASTContext &ast = m_obj_file->GetModule()->GetClangASTContext();
    if (!m_is_external_ast_source)
    {
        m_is_external_ast_source = true;
        llvm::OwningPtr<clang::ExternalASTSource> ast_source_ap (
            new ClangExternalASTSourceCallbacks (SymbolFileDWARF::CompleteTagDecl,
                                                 SymbolFileDWARF::CompleteObjCInterfaceDecl,
                                                 this));

        ast.SetExternalSource (ast_source_ap);
    }
    return ast;
}

void
SymbolFileDWARF::InitializeObject()
{
    // Install our external AST source callbacks so we can complete Clang types.
    Module *module = m_obj_file->GetModule();
    if (module)
    {
        const SectionList *section_list = m_obj_file->GetSectionList();

        const Section* section = section_list->FindSectionByName(GetDWARFMachOSegmentName ()).get();

        // Memory map the DWARF mach-o segment so we have everything mmap'ed
        // to keep our heap memory usage down.
        if (section)
            section->MemoryMapSectionDataFromObjectFile(m_obj_file, m_dwarf_data);
    }
}

bool
SymbolFileDWARF::SupportedVersion(uint16_t version)
{
    return version == 2 || version == 3;
}

uint32_t
SymbolFileDWARF::GetAbilities ()
{
    uint32_t abilities = 0;
    if (m_obj_file != NULL)
    {
        const Section* section = NULL;
        const SectionList *section_list = m_obj_file->GetSectionList();
        if (section_list == NULL)
            return 0;

        uint64_t debug_abbrev_file_size = 0;
        uint64_t debug_aranges_file_size = 0;
        uint64_t debug_frame_file_size = 0;
        uint64_t debug_info_file_size = 0;
        uint64_t debug_line_file_size = 0;
        uint64_t debug_loc_file_size = 0;
        uint64_t debug_macinfo_file_size = 0;
        uint64_t debug_pubnames_file_size = 0;
        uint64_t debug_pubtypes_file_size = 0;
        uint64_t debug_ranges_file_size = 0;
        uint64_t debug_str_file_size = 0;

        section = section_list->FindSectionByName(GetDWARFMachOSegmentName ()).get();
        
        if (section)
            section_list = &section->GetChildren ();
        
        section = section_list->FindSectionByType (eSectionTypeDWARFDebugInfo, true).get();
        if (section != NULL)
        {
            debug_info_file_size = section->GetByteSize();

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugAbbrev, true).get();
            if (section)
                debug_abbrev_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugAbbrevData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugAranges, true).get();
            if (section)
                debug_aranges_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugArangesData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugFrame, true).get();
            if (section)
                debug_frame_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugFrameData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugLine, true).get();
            if (section)
                debug_line_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugLineData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugLoc, true).get();
            if (section)
                debug_loc_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugLocData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugMacInfo, true).get();
            if (section)
                debug_macinfo_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugMacInfoData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugPubNames, true).get();
            if (section)
                debug_pubnames_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugPubNamesData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugPubTypes, true).get();
            if (section)
                debug_pubtypes_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugPubTypesData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugRanges, true).get();
            if (section)
                debug_ranges_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugRangesData);

            section = section_list->FindSectionByType (eSectionTypeDWARFDebugStr, true).get();
            if (section)
                debug_str_file_size = section->GetByteSize();
            else
                m_flags.Set (flagsGotDebugStrData);
        }

        if (debug_abbrev_file_size > 0 && debug_info_file_size > 0)
            abilities |= CompileUnits | Functions | Blocks | GlobalVariables | LocalVariables | VariableTypes;

        if (debug_line_file_size > 0)
            abilities |= LineTables;

        if (debug_aranges_file_size > 0)
            abilities |= AddressAcceleratorTable;

        if (debug_pubnames_file_size > 0)
            abilities |= FunctionAcceleratorTable;

        if (debug_pubtypes_file_size > 0)
            abilities |= TypeAcceleratorTable;

        if (debug_macinfo_file_size > 0)
            abilities |= MacroInformation;

        if (debug_frame_file_size > 0)
            abilities |= CallFrameInformation;
    }
    return abilities;
}

const DataExtractor&
SymbolFileDWARF::GetCachedSectionData (uint32_t got_flag, SectionType sect_type, DataExtractor &data)
{
    if (m_flags.IsClear (got_flag))
    {
        m_flags.Set (got_flag);
        const SectionList *section_list = m_obj_file->GetSectionList();
        if (section_list)
        {
            Section *section = section_list->FindSectionByType(sect_type, true).get();
            if (section)
            {
                // See if we memory mapped the DWARF segment?
                if (m_dwarf_data.GetByteSize())
                {
                    data.SetData(m_dwarf_data, section->GetOffset (), section->GetByteSize());
                }
                else
                {
                    if (section->ReadSectionDataFromObjectFile(m_obj_file, data) == 0)
                        data.Clear();
                }
            }
        }
    }
    return data;
}

const DataExtractor&
SymbolFileDWARF::get_debug_abbrev_data()
{
    return GetCachedSectionData (flagsGotDebugAbbrevData, eSectionTypeDWARFDebugAbbrev, m_data_debug_abbrev);
}

const DataExtractor&
SymbolFileDWARF::get_debug_frame_data()
{
    return GetCachedSectionData (flagsGotDebugFrameData, eSectionTypeDWARFDebugFrame, m_data_debug_frame);
}

const DataExtractor&
SymbolFileDWARF::get_debug_info_data()
{
    return GetCachedSectionData (flagsGotDebugInfoData, eSectionTypeDWARFDebugInfo, m_data_debug_info);
}

const DataExtractor&
SymbolFileDWARF::get_debug_line_data()
{
    return GetCachedSectionData (flagsGotDebugLineData, eSectionTypeDWARFDebugLine, m_data_debug_line);
}

const DataExtractor&
SymbolFileDWARF::get_debug_loc_data()
{
    return GetCachedSectionData (flagsGotDebugLocData, eSectionTypeDWARFDebugLoc, m_data_debug_loc);
}

const DataExtractor&
SymbolFileDWARF::get_debug_ranges_data()
{
    return GetCachedSectionData (flagsGotDebugRangesData, eSectionTypeDWARFDebugRanges, m_data_debug_ranges);
}

const DataExtractor&
SymbolFileDWARF::get_debug_str_data()
{
    return GetCachedSectionData (flagsGotDebugStrData, eSectionTypeDWARFDebugStr, m_data_debug_str);
}


DWARFDebugAbbrev*
SymbolFileDWARF::DebugAbbrev()
{
    if (m_abbr.get() == NULL)
    {
        const DataExtractor &debug_abbrev_data = get_debug_abbrev_data();
        if (debug_abbrev_data.GetByteSize() > 0)
        {
            m_abbr.reset(new DWARFDebugAbbrev());
            if (m_abbr.get())
                m_abbr->Parse(debug_abbrev_data);
        }
    }
    return m_abbr.get();
}

const DWARFDebugAbbrev*
SymbolFileDWARF::DebugAbbrev() const
{
    return m_abbr.get();
}

DWARFDebugAranges*
SymbolFileDWARF::DebugAranges()
{
    // It turns out that llvm-gcc doesn't generate .debug_aranges in .o files
    // and we are already parsing all of the DWARF because the .debug_pubnames
    // is useless (it only mentions symbols that are externally visible), so
    // don't use the .debug_aranges section, we should be using a debug aranges
    // we got from SymbolFileDWARF::Index().

    if (!m_indexed)
        Index();
    
    
//    if (m_aranges.get() == NULL)
//    {
//        Timer scoped_timer(__PRETTY_FUNCTION__, "%s this = %p", __PRETTY_FUNCTION__, this);
//        m_aranges.reset(new DWARFDebugAranges());
//        if (m_aranges.get())
//        {
//            const DataExtractor &debug_aranges_data = get_debug_aranges_data();
//            if (debug_aranges_data.GetByteSize() > 0)
//                m_aranges->Extract(debug_aranges_data);
//            else
//                m_aranges->Generate(this);
//        }
//    }
    return m_aranges.get();
}

const DWARFDebugAranges*
SymbolFileDWARF::DebugAranges() const
{
    return m_aranges.get();
}


DWARFDebugInfo*
SymbolFileDWARF::DebugInfo()
{
    if (m_info.get() == NULL)
    {
        Timer scoped_timer(__PRETTY_FUNCTION__, "%s this = %p", __PRETTY_FUNCTION__, this);
        if (get_debug_info_data().GetByteSize() > 0)
        {
            m_info.reset(new DWARFDebugInfo());
            if (m_info.get())
            {
                m_info->SetDwarfData(this);
            }
        }
    }
    return m_info.get();
}

const DWARFDebugInfo*
SymbolFileDWARF::DebugInfo() const
{
    return m_info.get();
}

DWARFCompileUnit*
SymbolFileDWARF::GetDWARFCompileUnitForUID(lldb::user_id_t cu_uid)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info)
        return info->GetCompileUnit(cu_uid).get();
    return NULL;
}


DWARFDebugRanges*
SymbolFileDWARF::DebugRanges()
{
    if (m_ranges.get() == NULL)
    {
        Timer scoped_timer(__PRETTY_FUNCTION__, "%s this = %p", __PRETTY_FUNCTION__, this);
        if (get_debug_ranges_data().GetByteSize() > 0)
        {
            m_ranges.reset(new DWARFDebugRanges());
            if (m_ranges.get())
                m_ranges->Extract(this);
        }
    }
    return m_ranges.get();
}

const DWARFDebugRanges*
SymbolFileDWARF::DebugRanges() const
{
    return m_ranges.get();
}

bool
SymbolFileDWARF::ParseCompileUnit (DWARFCompileUnit* curr_cu, CompUnitSP& compile_unit_sp)
{
    if (curr_cu != NULL)
    {
        const DWARFDebugInfoEntry * cu_die = curr_cu->GetCompileUnitDIEOnly ();
        if (cu_die)
        {
            const char * cu_die_name = cu_die->GetName(this, curr_cu);
            const char * cu_comp_dir = cu_die->GetAttributeValueAsString(this, curr_cu, DW_AT_comp_dir, NULL);
            LanguageType class_language = (LanguageType)cu_die->GetAttributeValueAsUnsigned(this, curr_cu, DW_AT_language, 0);
            if (cu_die_name)
            {
                FileSpec cu_file_spec;

                if (cu_die_name[0] == '/' || cu_comp_dir == NULL || cu_comp_dir[0] == '\0')
                {
                    // If we have a full path to the compile unit, we don't need to resolve
                    // the file.  This can be expensive e.g. when the source files are NFS mounted.
                    cu_file_spec.SetFile (cu_die_name, false);
                }
                else
                {
                    std::string fullpath(cu_comp_dir);
                    if (*fullpath.rbegin() != '/')
                        fullpath += '/';
                    fullpath += cu_die_name;
                    cu_file_spec.SetFile (fullpath.c_str(), false);
                }

                compile_unit_sp.reset(new CompileUnit(m_obj_file->GetModule(), curr_cu, cu_file_spec, curr_cu->GetOffset(), class_language));
                if (compile_unit_sp.get())
                {
                    curr_cu->SetUserData(compile_unit_sp.get());
                    return true;
                }
            }
        }
    }
    return false;
}

uint32_t
SymbolFileDWARF::GetNumCompileUnits()
{
    DWARFDebugInfo* info = DebugInfo();
    if (info)
        return info->GetNumCompileUnits();
    return 0;
}

CompUnitSP
SymbolFileDWARF::ParseCompileUnitAtIndex(uint32_t cu_idx)
{
    CompUnitSP comp_unit;
    DWARFDebugInfo* info = DebugInfo();
    if (info)
    {
        DWARFCompileUnit* curr_cu = info->GetCompileUnitAtIndex(cu_idx);
        if (curr_cu != NULL)
        {
            // Our symbol vendor shouldn't be asking us to add a compile unit that
            // has already been added to it, which this DWARF plug-in knows as it
            // stores the lldb compile unit (CompileUnit) pointer in each
            // DWARFCompileUnit object when it gets added.
            assert(curr_cu->GetUserData() == NULL);
            ParseCompileUnit(curr_cu, comp_unit);
        }
    }
    return comp_unit;
}

static void
AddRangesToBlock
(
    Block& block,
    DWARFDebugRanges::RangeList& ranges,
    addr_t block_base_addr
)
{
    ranges.SubtractOffset (block_base_addr);
    size_t range_idx = 0;
    const DWARFDebugRanges::Range *debug_range;
    for (range_idx = 0; (debug_range = ranges.RangeAtIndex(range_idx)) != NULL; range_idx++)
    {
        block.AddRange(debug_range->begin_offset, debug_range->end_offset);
    }
}


Function *
SymbolFileDWARF::ParseCompileUnitFunction (const SymbolContext& sc, DWARFCompileUnit* dwarf_cu, const DWARFDebugInfoEntry *die)
{
    DWARFDebugRanges::RangeList func_ranges;
    const char *name = NULL;
    const char *mangled = NULL;
    int decl_file = 0;
    int decl_line = 0;
    int decl_column = 0;
    int call_file = 0;
    int call_line = 0;
    int call_column = 0;
    DWARFExpression frame_base;

    assert (die->Tag() == DW_TAG_subprogram);
    
    if (die->Tag() != DW_TAG_subprogram)
        return NULL;

    const DWARFDebugInfoEntry *parent_die = die->GetParent();
    switch (parent_die->Tag())
    {
    case DW_TAG_structure_type:
    case DW_TAG_class_type:
        // We have methods of a class or struct
        {
            Type *class_type = ResolveType (dwarf_cu, parent_die);
            if (class_type)
                class_type->GetClangType();
        }
        break;

    default:
        // Parse the function prototype as a type that can then be added to concrete function instance
        ParseTypes (sc, dwarf_cu, die, false, false);
        break;
    }
    
    //FixupTypes();

    if (die->GetDIENamesAndRanges(this, dwarf_cu, name, mangled, func_ranges, decl_file, decl_line, decl_column, call_file, call_line, call_column, &frame_base))
    {
        // Union of all ranges in the function DIE (if the function is discontiguous)
        AddressRange func_range;
        lldb::addr_t lowest_func_addr = func_ranges.LowestAddress(0);
        lldb::addr_t highest_func_addr = func_ranges.HighestAddress(0);
        if (lowest_func_addr != LLDB_INVALID_ADDRESS && lowest_func_addr <= highest_func_addr)
        {
            func_range.GetBaseAddress().ResolveAddressUsingFileSections (lowest_func_addr, m_obj_file->GetSectionList());
            if (func_range.GetBaseAddress().IsValid())
                func_range.SetByteSize(highest_func_addr - lowest_func_addr);
        }

        if (func_range.GetBaseAddress().IsValid())
        {
            Mangled func_name;
            if (mangled)
                func_name.SetValue(mangled, true);
            else if (name)
                func_name.SetValue(name, false);

            FunctionSP func_sp;
            std::auto_ptr<Declaration> decl_ap;
            if (decl_file != 0 || decl_line != 0 || decl_column != 0)
                decl_ap.reset(new Declaration (sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(decl_file), 
                                               decl_line, 
                                               decl_column));

            Type *func_type = m_die_to_type.lookup (die);

            assert(func_type == NULL || func_type != DIE_IS_BEING_PARSED);

            func_range.GetBaseAddress().ResolveLinkedAddress();

            func_sp.reset(new Function (sc.comp_unit,
                                        die->GetOffset(),       // UserID is the DIE offset
                                        die->GetOffset(),
                                        func_name,
                                        func_type,
                                        func_range));           // first address range

            if (func_sp.get() != NULL)
            {
                func_sp->GetFrameBaseExpression() = frame_base;
                sc.comp_unit->AddFunction(func_sp);
                return func_sp.get();
            }
        }
    }
    return NULL;
}

size_t
SymbolFileDWARF::ParseCompileUnitFunctions(const SymbolContext &sc)
{
    assert (sc.comp_unit);
    size_t functions_added = 0;
    DWARFCompileUnit* dwarf_cu = GetDWARFCompileUnitForUID(sc.comp_unit->GetID());
    if (dwarf_cu)
    {
        DWARFDIECollection function_dies;
        const size_t num_funtions = dwarf_cu->AppendDIEsWithTag (DW_TAG_subprogram, function_dies);
        size_t func_idx;
        for (func_idx = 0; func_idx < num_funtions; ++func_idx)
        {
            const DWARFDebugInfoEntry *die = function_dies.GetDIEPtrAtIndex(func_idx);
            if (sc.comp_unit->FindFunctionByUID (die->GetOffset()).get() == NULL)
            {
                if (ParseCompileUnitFunction(sc, dwarf_cu, die))
                    ++functions_added;
            }
        }
        //FixupTypes();
    }
    return functions_added;
}

bool
SymbolFileDWARF::ParseCompileUnitSupportFiles (const SymbolContext& sc, FileSpecList& support_files)
{
    assert (sc.comp_unit);
    DWARFCompileUnit* curr_cu = GetDWARFCompileUnitForUID(sc.comp_unit->GetID());
    assert (curr_cu);
    const DWARFDebugInfoEntry * cu_die = curr_cu->GetCompileUnitDIEOnly();

    if (cu_die)
    {
        const char * cu_comp_dir = cu_die->GetAttributeValueAsString(this, curr_cu, DW_AT_comp_dir, NULL);
        dw_offset_t stmt_list = cu_die->GetAttributeValueAsUnsigned(this, curr_cu, DW_AT_stmt_list, DW_INVALID_OFFSET);

        // All file indexes in DWARF are one based and a file of index zero is
        // supposed to be the compile unit itself.
        support_files.Append (*sc.comp_unit);

        return DWARFDebugLine::ParseSupportFiles(get_debug_line_data(), cu_comp_dir, stmt_list, support_files);
    }
    return false;
}

struct ParseDWARFLineTableCallbackInfo
{
    LineTable* line_table;
    const SectionList *section_list;
    lldb::addr_t prev_sect_file_base_addr;
    lldb::addr_t curr_sect_file_base_addr;
    bool is_oso_for_debug_map;
    bool prev_in_final_executable;
    DWARFDebugLine::Row prev_row;
    SectionSP prev_section_sp;
    SectionSP curr_section_sp;
};

//----------------------------------------------------------------------
// ParseStatementTableCallback
//----------------------------------------------------------------------
static void
ParseDWARFLineTableCallback(dw_offset_t offset, const DWARFDebugLine::State& state, void* userData)
{
    LineTable* line_table = ((ParseDWARFLineTableCallbackInfo*)userData)->line_table;
    if (state.row == DWARFDebugLine::State::StartParsingLineTable)
    {
        // Just started parsing the line table
    }
    else if (state.row == DWARFDebugLine::State::DoneParsingLineTable)
    {
        // Done parsing line table, nothing to do for the cleanup
    }
    else
    {
        ParseDWARFLineTableCallbackInfo* info = (ParseDWARFLineTableCallbackInfo*)userData;
        // We have a new row, lets append it

        if (info->curr_section_sp.get() == NULL || info->curr_section_sp->ContainsFileAddress(state.address) == false)
        {
            info->prev_section_sp = info->curr_section_sp;
            info->prev_sect_file_base_addr = info->curr_sect_file_base_addr;
            // If this is an end sequence entry, then we subtract one from the
            // address to make sure we get an address that is not the end of
            // a section.
            if (state.end_sequence && state.address != 0)
                info->curr_section_sp = info->section_list->FindSectionContainingFileAddress (state.address - 1);
            else
                info->curr_section_sp = info->section_list->FindSectionContainingFileAddress (state.address);

            if (info->curr_section_sp.get())
                info->curr_sect_file_base_addr = info->curr_section_sp->GetFileAddress ();
            else
                info->curr_sect_file_base_addr = 0;
        }
        if (info->curr_section_sp.get())
        {
            lldb::addr_t curr_line_section_offset = state.address - info->curr_sect_file_base_addr;
            // Check for the fancy section magic to determine if we

            if (info->is_oso_for_debug_map)
            {
                // When this is a debug map object file that contains DWARF
                // (referenced from an N_OSO debug map nlist entry) we will have
                // a file address in the file range for our section from the
                // original .o file, and a load address in the executable that
                // contains the debug map.
                //
                // If the sections for the file range and load range are
                // different, we have a remapped section for the function and
                // this address is resolved. If they are the same, then the
                // function for this address didn't make it into the final
                // executable.
                bool curr_in_final_executable = info->curr_section_sp->GetLinkedSection () != NULL;

                // If we are doing DWARF with debug map, then we need to carefully
                // add each line table entry as there may be gaps as functions
                // get moved around or removed.
                if (!info->prev_row.end_sequence && info->prev_section_sp.get())
                {
                    if (info->prev_in_final_executable)
                    {
                        bool terminate_previous_entry = false;
                        if (!curr_in_final_executable)
                        {
                            // Check for the case where the previous line entry
                            // in a function made it into the final executable,
                            // yet the current line entry falls in a function
                            // that didn't. The line table used to be contiguous
                            // through this address range but now it isn't. We
                            // need to terminate the previous line entry so
                            // that we can reconstruct the line range correctly
                            // for it and to keep the line table correct.
                            terminate_previous_entry = true;
                        }
                        else if (info->curr_section_sp.get() != info->prev_section_sp.get())
                        {
                            // Check for cases where the line entries used to be
                            // contiguous address ranges, but now they aren't.
                            // This can happen when order files specify the
                            // ordering of the functions.
                            lldb::addr_t prev_line_section_offset = info->prev_row.address - info->prev_sect_file_base_addr;
                            Section *curr_sect = info->curr_section_sp.get();
                            Section *prev_sect = info->prev_section_sp.get();
                            assert (curr_sect->GetLinkedSection());
                            assert (prev_sect->GetLinkedSection());
                            lldb::addr_t object_file_addr_delta = state.address - info->prev_row.address;
                            lldb::addr_t curr_linked_file_addr = curr_sect->GetLinkedFileAddress() + curr_line_section_offset;
                            lldb::addr_t prev_linked_file_addr = prev_sect->GetLinkedFileAddress() + prev_line_section_offset;
                            lldb::addr_t linked_file_addr_delta = curr_linked_file_addr - prev_linked_file_addr;
                            if (object_file_addr_delta != linked_file_addr_delta)
                                terminate_previous_entry = true;
                        }

                        if (terminate_previous_entry)
                        {
                            line_table->InsertLineEntry (info->prev_section_sp,
                                                         state.address - info->prev_sect_file_base_addr,
                                                         info->prev_row.line,
                                                         info->prev_row.column,
                                                         info->prev_row.file,
                                                         false,                 // is_stmt
                                                         false,                 // basic_block
                                                         false,                 // state.prologue_end
                                                         false,                 // state.epilogue_begin
                                                         true);                 // end_sequence);
                        }
                    }
                }

                if (curr_in_final_executable)
                {
                    line_table->InsertLineEntry (info->curr_section_sp,
                                                 curr_line_section_offset,
                                                 state.line,
                                                 state.column,
                                                 state.file,
                                                 state.is_stmt,
                                                 state.basic_block,
                                                 state.prologue_end,
                                                 state.epilogue_begin,
                                                 state.end_sequence);
                    info->prev_section_sp = info->curr_section_sp;
                }
                else
                {
                    // If the current address didn't make it into the final
                    // executable, the current section will be the __text
                    // segment in the .o file, so we need to clear this so
                    // we can catch the next function that did make it into
                    // the final executable.
                    info->prev_section_sp.reset();
                    info->curr_section_sp.reset();
                }

                info->prev_in_final_executable = curr_in_final_executable;
            }
            else
            {
                // We are not in an object file that contains DWARF for an
                // N_OSO, this is just a normal DWARF file. The DWARF spec
                // guarantees that the addresses will be in increasing order
                // so, since we store line tables in file address order, we
                // can always just append the line entry without needing to
                // search for the correct insertion point (we don't need to
                // use LineEntry::InsertLineEntry()).
                line_table->AppendLineEntry (info->curr_section_sp,
                                             curr_line_section_offset,
                                             state.line,
                                             state.column,
                                             state.file,
                                             state.is_stmt,
                                             state.basic_block,
                                             state.prologue_end,
                                             state.epilogue_begin,
                                             state.end_sequence);
            }
        }

        info->prev_row = state;
    }
}

bool
SymbolFileDWARF::ParseCompileUnitLineTable (const SymbolContext &sc)
{
    assert (sc.comp_unit);
    if (sc.comp_unit->GetLineTable() != NULL)
        return true;

    DWARFCompileUnit* dwarf_cu = GetDWARFCompileUnitForUID(sc.comp_unit->GetID());
    if (dwarf_cu)
    {
        const DWARFDebugInfoEntry *dwarf_cu_die = dwarf_cu->GetCompileUnitDIEOnly();
        const dw_offset_t cu_line_offset = dwarf_cu_die->GetAttributeValueAsUnsigned(this, dwarf_cu, DW_AT_stmt_list, DW_INVALID_OFFSET);
        if (cu_line_offset != DW_INVALID_OFFSET)
        {
            std::auto_ptr<LineTable> line_table_ap(new LineTable(sc.comp_unit));
            if (line_table_ap.get())
            {
                ParseDWARFLineTableCallbackInfo info = { line_table_ap.get(), m_obj_file->GetSectionList(), 0, 0, m_debug_map_symfile != NULL, false};
                uint32_t offset = cu_line_offset;
                DWARFDebugLine::ParseStatementTable(get_debug_line_data(), &offset, ParseDWARFLineTableCallback, &info);
                sc.comp_unit->SetLineTable(line_table_ap.release());
                return true;
            }
        }
    }
    return false;
}

size_t
SymbolFileDWARF::ParseFunctionBlocks
(
    const SymbolContext& sc,
    Block *parent_block,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *die,
    addr_t subprogram_low_pc,
    bool parse_siblings,
    bool parse_children
)
{
    size_t blocks_added = 0;
    while (die != NULL)
    {
        dw_tag_t tag = die->Tag();

        switch (tag)
        {
        case DW_TAG_inlined_subroutine:
        case DW_TAG_subprogram:
        case DW_TAG_lexical_block:
            {
                DWARFDebugRanges::RangeList ranges;
                const char *name = NULL;
                const char *mangled_name = NULL;
                Block *block = NULL;
                if (tag != DW_TAG_subprogram)
                {
                    BlockSP block_sp(new Block (die->GetOffset()));
                    parent_block->AddChild(block_sp);
                    block = block_sp.get();
                }
                else
                {
                    block = parent_block;
                }

                int decl_file = 0;
                int decl_line = 0;
                int decl_column = 0;
                int call_file = 0;
                int call_line = 0;
                int call_column = 0;
                if (die->GetDIENamesAndRanges (this, 
                                               dwarf_cu, 
                                               name, 
                                               mangled_name, 
                                               ranges, 
                                               decl_file, decl_line, decl_column,
                                               call_file, call_line, call_column))
                {
                    if (tag == DW_TAG_subprogram)
                    {
                        assert (subprogram_low_pc == LLDB_INVALID_ADDRESS);
                        subprogram_low_pc = ranges.LowestAddress(0);
                    }
                    else if (tag == DW_TAG_inlined_subroutine)
                    {
                        // We get called here for inlined subroutines in two ways.  
                        // The first time is when we are making the Function object 
                        // for this inlined concrete instance.  Since we're creating a top level block at
                        // here, the subprogram_low_pc will be LLDB_INVALID_ADDRESS.  So we need to 
                        // adjust the containing address.
                        // The second time is when we are parsing the blocks inside the function that contains
                        // the inlined concrete instance.  Since these will be blocks inside the containing "real"
                        // function the offset will be for that function.  
                        if (subprogram_low_pc == LLDB_INVALID_ADDRESS)
                        {
                            subprogram_low_pc = ranges.LowestAddress(0);
                        }
                    }
                    
                    AddRangesToBlock (*block, ranges, subprogram_low_pc);

                    if (tag != DW_TAG_subprogram && (name != NULL || mangled_name != NULL))
                    {
                        std::auto_ptr<Declaration> decl_ap;
                        if (decl_file != 0 || decl_line != 0 || decl_column != 0)
                            decl_ap.reset(new Declaration(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(decl_file), 
                                                          decl_line, decl_column));

                        std::auto_ptr<Declaration> call_ap;
                        if (call_file != 0 || call_line != 0 || call_column != 0)
                            call_ap.reset(new Declaration(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(call_file), 
                                                          call_line, call_column));

                        block->SetInlinedFunctionInfo (name, mangled_name, decl_ap.get(), call_ap.get());
                    }

                    ++blocks_added;

                    if (parse_children && die->HasChildren())
                    {
                        blocks_added += ParseFunctionBlocks (sc, 
                                                             block, 
                                                             dwarf_cu, 
                                                             die->GetFirstChild(), 
                                                             subprogram_low_pc, 
                                                             true, 
                                                             true);
                    }
                }
            }
            break;
        default:
            break;
        }

        if (parse_siblings)
            die = die->GetSibling();
        else
            die = NULL;
    }
    return blocks_added;
}

size_t
SymbolFileDWARF::ParseChildMembers
(
    const SymbolContext& sc,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *parent_die,
    clang_type_t class_clang_type,
    const LanguageType class_language,
    std::vector<clang::CXXBaseSpecifier *>& base_classes,
    std::vector<int>& member_accessibilities,
    DWARFDIECollection& member_function_dies,
    AccessType& default_accessibility,
    bool &is_a_class
)
{
    if (parent_die == NULL)
        return 0;

    size_t count = 0;
    const DWARFDebugInfoEntry *die;
    const uint8_t *fixed_form_sizes = DWARFFormValue::GetFixedFormSizesForAddressSize (dwarf_cu->GetAddressByteSize());
    uint32_t member_idx = 0;

    for (die = parent_die->GetFirstChild(); die != NULL; die = die->GetSibling())
    {
        dw_tag_t tag = die->Tag();

        switch (tag)
        {
        case DW_TAG_member:
            {
                DWARFDebugInfoEntry::Attributes attributes;
                const size_t num_attributes = die->GetAttributes (this, 
                                                                  dwarf_cu, 
                                                                  fixed_form_sizes, 
                                                                  attributes);
                if (num_attributes > 0)
                {
                    Declaration decl;
                    //DWARFExpression location;
                    const char *name = NULL;
                    bool is_artificial = false;
                    lldb::user_id_t encoding_uid = LLDB_INVALID_UID;
                    AccessType accessibility = eAccessNone;
                    //off_t member_offset = 0;
                    size_t byte_size = 0;
                    size_t bit_offset = 0;
                    size_t bit_size = 0;
                    uint32_t i;
                    for (i=0; i<num_attributes && !is_artificial; ++i)
                    {
                        const dw_attr_t attr = attributes.AttributeAtIndex(i);
                        DWARFFormValue form_value;
                        if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                        {
                            switch (attr)
                            {
                            case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                            case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                            case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                            case DW_AT_name:        name = form_value.AsCString(&get_debug_str_data()); break;
                            case DW_AT_type:        encoding_uid = form_value.Reference(dwarf_cu); break;
                            case DW_AT_bit_offset:  bit_offset = form_value.Unsigned(); break;
                            case DW_AT_bit_size:    bit_size = form_value.Unsigned(); break;
                            case DW_AT_byte_size:   byte_size = form_value.Unsigned(); break;
                            case DW_AT_data_member_location:
//                                if (form_value.BlockData())
//                                {
//                                    Value initialValue(0);
//                                    Value memberOffset(0);
//                                    const DataExtractor& debug_info_data = get_debug_info_data();
//                                    uint32_t block_length = form_value.Unsigned();
//                                    uint32_t block_offset = form_value.BlockData() - debug_info_data.GetDataStart();
//                                    if (DWARFExpression::Evaluate(NULL, NULL, debug_info_data, NULL, NULL, block_offset, block_length, eRegisterKindDWARF, &initialValue, memberOffset, NULL))
//                                    {
//                                        member_offset = memberOffset.ResolveValue(NULL, NULL).UInt();
//                                    }
//                                }
                                break;

                            case DW_AT_accessibility: accessibility = DW_ACCESS_to_AccessType (form_value.Unsigned()); break;
                            case DW_AT_artificial: is_artificial = form_value.Unsigned() != 0; break;
                            case DW_AT_declaration:
                            case DW_AT_description:
                            case DW_AT_mutable:
                            case DW_AT_visibility:
                            default:
                            case DW_AT_sibling:
                                break;
                            }
                        }
                    }
                    
                    // FIXME: Make Clang ignore Objective-C accessibility for expressions
                    
                    if (class_language == eLanguageTypeObjC ||
                        class_language == eLanguageTypeObjC_plus_plus)
                        accessibility = eAccessNone; 
                    
                    if (member_idx == 0 && !is_artificial && name && (strstr (name, "_vptr$") == name))
                    {
                        // Not all compilers will mark the vtable pointer
                        // member as artificial (llvm-gcc). We can't have
                        // the virtual members in our classes otherwise it
                        // throws off all child offsets since we end up
                        // having and extra pointer sized member in our 
                        // class layouts.
                        is_artificial = true;
                    }

                    if (is_artificial == false)
                    {
                        Type *member_type = ResolveTypeUID(encoding_uid);
                        assert(member_type);
                        if (accessibility == eAccessNone)
                            accessibility = default_accessibility;
                        member_accessibilities.push_back(accessibility);

                        GetClangASTContext().AddFieldToRecordType (class_clang_type, 
                                                                   name, 
                                                                   member_type->GetClangLayoutType(), 
                                                                   accessibility, 
                                                                   bit_size);
                    }
                }
                ++member_idx;
            }
            break;

        case DW_TAG_subprogram:
            // Let the type parsing code handle this one for us. 
            member_function_dies.Append (die);
            break;

        case DW_TAG_inheritance:
            {
                is_a_class = true;
                if (default_accessibility == eAccessNone)
                    default_accessibility = eAccessPrivate;
                // TODO: implement DW_TAG_inheritance type parsing
                DWARFDebugInfoEntry::Attributes attributes;
                const size_t num_attributes = die->GetAttributes (this, 
                                                                  dwarf_cu, 
                                                                  fixed_form_sizes, 
                                                                  attributes);
                if (num_attributes > 0)
                {
                    Declaration decl;
                    DWARFExpression location;
                    lldb::user_id_t encoding_uid = LLDB_INVALID_UID;
                    AccessType accessibility = default_accessibility;
                    bool is_virtual = false;
                    bool is_base_of_class = true;
                    off_t member_offset = 0;
                    uint32_t i;
                    for (i=0; i<num_attributes; ++i)
                    {
                        const dw_attr_t attr = attributes.AttributeAtIndex(i);
                        DWARFFormValue form_value;
                        if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                        {
                            switch (attr)
                            {
                            case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                            case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                            case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                            case DW_AT_type:        encoding_uid = form_value.Reference(dwarf_cu); break;
                            case DW_AT_data_member_location:
                                if (form_value.BlockData())
                                {
                                    Value initialValue(0);
                                    Value memberOffset(0);
                                    const DataExtractor& debug_info_data = get_debug_info_data();
                                    uint32_t block_length = form_value.Unsigned();
                                    uint32_t block_offset = form_value.BlockData() - debug_info_data.GetDataStart();
                                    if (DWARFExpression::Evaluate (NULL, 
                                                                   NULL, 
                                                                   NULL, 
                                                                   NULL, 
                                                                   NULL,
                                                                   debug_info_data, 
                                                                   block_offset, 
                                                                   block_length, 
                                                                   eRegisterKindDWARF, 
                                                                   &initialValue, 
                                                                   memberOffset, 
                                                                   NULL))
                                    {
                                        member_offset = memberOffset.ResolveValue(NULL, NULL).UInt();
                                    }
                                }
                                break;

                            case DW_AT_accessibility:
                                accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned());
                                break;

                            case DW_AT_virtuality: is_virtual = form_value.Unsigned() != 0; break;
                            default:
                            case DW_AT_sibling:
                                break;
                            }
                        }
                    }

                    Type *base_class_type = ResolveTypeUID(encoding_uid);
                    assert(base_class_type);
                    
                    if (class_language == eLanguageTypeObjC)
                    {
                        GetClangASTContext().SetObjCSuperClass(class_clang_type, base_class_type->GetClangType());
                    }
                    else
                    {
                        base_classes.push_back (GetClangASTContext().CreateBaseClassSpecifier (base_class_type->GetClangType(), 
                                                                                               accessibility, 
                                                                                               is_virtual, 
                                                                                               is_base_of_class));
                        assert(base_classes.back());
                    }
                }
            }
            break;

        default:
            break;
        }
    }
    return count;
}


clang::DeclContext*
SymbolFileDWARF::GetClangDeclContextForTypeUID (lldb::user_id_t type_uid)
{
    DWARFDebugInfo* debug_info = DebugInfo();
    if (debug_info)
    {
        DWARFCompileUnitSP cu_sp;
        const DWARFDebugInfoEntry* die = debug_info->GetDIEPtr(type_uid, &cu_sp);
        if (die)
            return GetClangDeclContextForDIE (cu_sp.get(), die);
    }
    return NULL;
}

Type*
SymbolFileDWARF::ResolveTypeUID (lldb::user_id_t type_uid)
{
    DWARFDebugInfo* debug_info = DebugInfo();
    if (debug_info)
    {
        DWARFCompileUnitSP cu_sp;
        const DWARFDebugInfoEntry* type_die = debug_info->GetDIEPtr(type_uid, &cu_sp);
        if (type_die != NULL)
        {
            // We might be coming in in the middle of a type tree (a class
            // withing a class, an enum within a class), so parse any needed
            // parent DIEs before we get to this one...
            const DWARFDebugInfoEntry* parent_die = type_die->GetParent();
            switch (parent_die->Tag())
            {
            case DW_TAG_structure_type:
            case DW_TAG_union_type:
            case DW_TAG_class_type:
                ResolveType(cu_sp.get(), parent_die);
                break;
            }
            return ResolveType (cu_sp.get(), type_die);
        }
    }
    return NULL;
}

// This function is used when SymbolFileDWARFDebugMap owns a bunch of
// SymbolFileDWARF objects to detect if this DWARF file is the one that
// can resolve a clang_type.
bool
SymbolFileDWARF::HasForwardDeclForClangType (lldb::clang_type_t clang_type)
{
    clang_type_t clang_type_no_qualifiers = ClangASTType::RemoveFastQualifiers(clang_type);
    const DWARFDebugInfoEntry* die = m_forward_decl_clang_type_to_die.lookup (clang_type_no_qualifiers);
    return die != NULL;
}


lldb::clang_type_t
SymbolFileDWARF::ResolveClangOpaqueTypeDefinition (lldb::clang_type_t clang_type)
{
    // We have a struct/union/class/enum that needs to be fully resolved.
    clang_type_t clang_type_no_qualifiers = ClangASTType::RemoveFastQualifiers(clang_type);
    const DWARFDebugInfoEntry* die = m_forward_decl_clang_type_to_die.lookup (clang_type_no_qualifiers);
    if (die == NULL)
    {
//        if (m_debug_map_symfile)
//        {
//            Type *type = m_die_to_type[die];
//            if (type && type->GetSymbolFile() != this)
//                return type->GetClangType();
//        }
        // We have already resolved this type...
        return clang_type;
    }
    // Once we start resolving this type, remove it from the forward declaration
    // map in case anyone child members or other types require this type to get resolved.
    // The type will get resolved when all of the calls to SymbolFileDWARF::ResolveClangOpaqueTypeDefinition
    // are done.
    m_forward_decl_clang_type_to_die.erase (clang_type_no_qualifiers);
    

    DWARFDebugInfo* debug_info = DebugInfo();

    DWARFCompileUnit *curr_cu = debug_info->GetCompileUnitContainingDIE (die->GetOffset()).get();
    Type *type = m_die_to_type.lookup (die);

    const dw_tag_t tag = die->Tag();

    DEBUG_PRINTF ("0x%8.8x: %s (\"%s\") - resolve forward declaration...\n", 
                  die->GetOffset(), 
                  DW_TAG_value_to_name(tag), 
                  type->GetName().AsCString());
    assert (clang_type);
    DWARFDebugInfoEntry::Attributes attributes;

    ClangASTContext &ast = GetClangASTContext();

    switch (tag)
    {
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_class_type:
        ast.StartTagDeclarationDefinition (clang_type);
        if (die->HasChildren())
        {
            LanguageType class_language = eLanguageTypeUnknown;
            bool is_objc_class = ClangASTContext::IsObjCClassType (clang_type);
            if (is_objc_class)
                class_language = eLanguageTypeObjC;

            int tag_decl_kind = -1;
            AccessType default_accessibility = eAccessNone;
            if (tag == DW_TAG_structure_type)
            {
                tag_decl_kind = clang::TTK_Struct;
                default_accessibility = eAccessPublic;
            }
            else if (tag == DW_TAG_union_type)
            {
                tag_decl_kind = clang::TTK_Union;
                default_accessibility = eAccessPublic;
            }
            else if (tag == DW_TAG_class_type)
            {
                tag_decl_kind = clang::TTK_Class;
                default_accessibility = eAccessPrivate;
            }

            SymbolContext sc(GetCompUnitForDWARFCompUnit(curr_cu));
            std::vector<clang::CXXBaseSpecifier *> base_classes;
            std::vector<int> member_accessibilities;
            bool is_a_class = false;
            // Parse members and base classes first
            DWARFDIECollection member_function_dies;

            ParseChildMembers (sc, 
                               curr_cu, 
                               die, 
                               clang_type,
                               class_language,
                               base_classes, 
                               member_accessibilities,
                               member_function_dies,
                               default_accessibility, 
                               is_a_class);

            // Now parse any methods if there were any...
            size_t num_functions = member_function_dies.Size();                
            if (num_functions > 0)
            {
                for (size_t i=0; i<num_functions; ++i)
                {
                    ResolveType(curr_cu, member_function_dies.GetDIEPtrAtIndex(i));
                }
            }
            
            if (class_language == eLanguageTypeObjC)
            {
                std::string class_str (ClangASTContext::GetTypeName (clang_type));
                if (!class_str.empty())
                {
                
                    ConstString class_name (class_str.c_str());
                    std::vector<NameToDIE::Info> method_die_infos;
                    if (m_objc_class_selectors_index.Find (class_name, method_die_infos))
                    {
                        DWARFCompileUnit* method_cu = NULL;
                        DWARFCompileUnit* prev_method_cu = NULL;
                        const size_t num_objc_methods = method_die_infos.size();
                        for (size_t i=0;i<num_objc_methods; ++i, prev_method_cu = method_cu)
                        {
                            method_cu = debug_info->GetCompileUnitAtIndex(method_die_infos[i].cu_idx);
                            
                            if (method_cu != prev_method_cu)
                                method_cu->ExtractDIEsIfNeeded (false);

                            DWARFDebugInfoEntry *method_die = method_cu->GetDIEAtIndexUnchecked(method_die_infos[i].die_idx);
                            
                            ResolveType (method_cu, method_die);
                        }
                    }
                }
            }
            
            // If we have a DW_TAG_structure_type instead of a DW_TAG_class_type we
            // need to tell the clang type it is actually a class.
            if (class_language != eLanguageTypeObjC)
            {
                if (is_a_class && tag_decl_kind != clang::TTK_Class)
                    ast.SetTagTypeKind (clang_type, clang::TTK_Class);
            }

            // Since DW_TAG_structure_type gets used for both classes
            // and structures, we may need to set any DW_TAG_member
            // fields to have a "private" access if none was specified.
            // When we parsed the child members we tracked that actual
            // accessibility value for each DW_TAG_member in the
            // "member_accessibilities" array. If the value for the
            // member is zero, then it was set to the "default_accessibility"
            // which for structs was "public". Below we correct this
            // by setting any fields to "private" that weren't correctly
            // set.
            if (is_a_class && !member_accessibilities.empty())
            {
                // This is a class and all members that didn't have
                // their access specified are private.
                ast.SetDefaultAccessForRecordFields (clang_type, 
                                                     eAccessPrivate, 
                                                     &member_accessibilities.front(), 
                                                     member_accessibilities.size());
            }

            if (!base_classes.empty())
            {
                ast.SetBaseClassesForClassType (clang_type, 
                                                &base_classes.front(), 
                                                base_classes.size());

                // Clang will copy each CXXBaseSpecifier in "base_classes"
                // so we have to free them all.
                ClangASTContext::DeleteBaseClassSpecifiers (&base_classes.front(), 
                                                            base_classes.size());
            }
            
        }
        ast.CompleteTagDeclarationDefinition (clang_type);
        return clang_type;

    case DW_TAG_enumeration_type:
        ast.StartTagDeclarationDefinition (clang_type);
        if (die->HasChildren())
        {
            SymbolContext sc(GetCompUnitForDWARFCompUnit(curr_cu));
            ParseChildEnumerators(sc, clang_type, type->GetByteSize(), curr_cu, die);
        }
        ast.CompleteTagDeclarationDefinition (clang_type);
        return clang_type;

    default:
        assert(false && "not a forward clang type decl!");
        break;
    }
    return NULL;
}

Type*
SymbolFileDWARF::ResolveType (DWARFCompileUnit* curr_cu, const DWARFDebugInfoEntry* type_die, bool assert_not_being_parsed)
{
    if (type_die != NULL)
    {
        Type *type = m_die_to_type.lookup (type_die);
        if (type == NULL)
            type = GetTypeForDIE (curr_cu, type_die).get();
        if (assert_not_being_parsed)
            assert (type != DIE_IS_BEING_PARSED);
        return type;
    }
    return NULL;
}

CompileUnit*
SymbolFileDWARF::GetCompUnitForDWARFCompUnit (DWARFCompileUnit* curr_cu, uint32_t cu_idx)
{
    // Check if the symbol vendor already knows about this compile unit?
    if (curr_cu->GetUserData() == NULL)
    {
        // The symbol vendor doesn't know about this compile unit, we
        // need to parse and add it to the symbol vendor object.
        CompUnitSP dc_cu;
        ParseCompileUnit(curr_cu, dc_cu);
        if (dc_cu.get())
        {
            // Figure out the compile unit index if we weren't given one
            if (cu_idx == UINT32_MAX)
                DebugInfo()->GetCompileUnit(curr_cu->GetOffset(), &cu_idx);

            m_obj_file->GetModule()->GetSymbolVendor()->SetCompileUnitAtIndex(dc_cu, cu_idx);
            
            if (m_debug_map_symfile)
                m_debug_map_symfile->SetCompileUnit(this, dc_cu);
        }
    }
    return (CompileUnit*)curr_cu->GetUserData();
}

bool
SymbolFileDWARF::GetFunction (DWARFCompileUnit* curr_cu, const DWARFDebugInfoEntry* func_die, SymbolContext& sc)
{
    sc.Clear();
    // Check if the symbol vendor already knows about this compile unit?
    sc.module_sp = m_obj_file->GetModule()->GetSP();
    sc.comp_unit = GetCompUnitForDWARFCompUnit(curr_cu, UINT32_MAX);

    sc.function = sc.comp_unit->FindFunctionByUID (func_die->GetOffset()).get();
    if (sc.function == NULL)
        sc.function = ParseCompileUnitFunction(sc, curr_cu, func_die);

    return sc.function != NULL;
}

uint32_t
SymbolFileDWARF::ResolveSymbolContext (const Address& so_addr, uint32_t resolve_scope, SymbolContext& sc)
{
    Timer scoped_timer(__PRETTY_FUNCTION__,
                       "SymbolFileDWARF::ResolveSymbolContext (so_addr = { section = %p, offset = 0x%llx }, resolve_scope = 0x%8.8x)",
                       so_addr.GetSection(),
                       so_addr.GetOffset(),
                       resolve_scope);
    uint32_t resolved = 0;
    if (resolve_scope & (   eSymbolContextCompUnit |
                            eSymbolContextFunction |
                            eSymbolContextBlock |
                            eSymbolContextLineEntry))
    {
        lldb::addr_t file_vm_addr = so_addr.GetFileAddress();

        DWARFDebugAranges* debug_aranges = DebugAranges();
        DWARFDebugInfo* debug_info = DebugInfo();
        if (debug_aranges)
        {
            dw_offset_t cu_offset = debug_aranges->FindAddress(file_vm_addr);
            if (cu_offset != DW_INVALID_OFFSET)
            {
                uint32_t cu_idx;
                DWARFCompileUnit* curr_cu = debug_info->GetCompileUnit(cu_offset, &cu_idx).get();
                if (curr_cu)
                {
                    sc.comp_unit = GetCompUnitForDWARFCompUnit(curr_cu, cu_idx);
                    assert(sc.comp_unit != NULL);
                    resolved |= eSymbolContextCompUnit;

                    if (resolve_scope & eSymbolContextLineEntry)
                    {
                        LineTable *line_table = sc.comp_unit->GetLineTable();
                        if (line_table == NULL)
                        {
                            if (ParseCompileUnitLineTable(sc))
                                line_table = sc.comp_unit->GetLineTable();
                        }
                        if (line_table != NULL)
                        {
                            if (so_addr.IsLinkedAddress())
                            {
                                Address linked_addr (so_addr);
                                linked_addr.ResolveLinkedAddress();
                                if (line_table->FindLineEntryByAddress (linked_addr, sc.line_entry))
                                {
                                    resolved |= eSymbolContextLineEntry;
                                }
                            }
                            else if (line_table->FindLineEntryByAddress (so_addr, sc.line_entry))
                            {
                                resolved |= eSymbolContextLineEntry;
                            }
                        }
                    }

                    if (resolve_scope & (eSymbolContextFunction | eSymbolContextBlock))
                    {
                        DWARFDebugInfoEntry *function_die = NULL;
                        DWARFDebugInfoEntry *block_die = NULL;
                        if (resolve_scope & eSymbolContextBlock)
                        {
                            curr_cu->LookupAddress(file_vm_addr, &function_die, &block_die);
                        }
                        else
                        {
                            curr_cu->LookupAddress(file_vm_addr, &function_die, NULL);
                        }

                        if (function_die != NULL)
                        {
                            sc.function = sc.comp_unit->FindFunctionByUID (function_die->GetOffset()).get();
                            if (sc.function == NULL)
                                sc.function = ParseCompileUnitFunction(sc, curr_cu, function_die);
                        }

                        if (sc.function != NULL)
                        {
                            resolved |= eSymbolContextFunction;

                            if (resolve_scope & eSymbolContextBlock)
                            {
                                Block& block = sc.function->GetBlock (true);

                                if (block_die != NULL)
                                    sc.block = block.FindBlockByID (block_die->GetOffset());
                                else
                                    sc.block = block.FindBlockByID (function_die->GetOffset());
                                if (sc.block)
                                    resolved |= eSymbolContextBlock;
                            }
                        }
                    }
                }
            }
        }
    }
    return resolved;
}



uint32_t
SymbolFileDWARF::ResolveSymbolContext(const FileSpec& file_spec, uint32_t line, bool check_inlines, uint32_t resolve_scope, SymbolContextList& sc_list)
{
    const uint32_t prev_size = sc_list.GetSize();
    if (resolve_scope & eSymbolContextCompUnit)
    {
        DWARFDebugInfo* debug_info = DebugInfo();
        if (debug_info)
        {
            uint32_t cu_idx;
            DWARFCompileUnit* curr_cu = NULL;

            for (cu_idx = 0; (curr_cu = debug_info->GetCompileUnitAtIndex(cu_idx)) != NULL; ++cu_idx)
            {
                CompileUnit *dc_cu = GetCompUnitForDWARFCompUnit(curr_cu, cu_idx);
                bool file_spec_matches_cu_file_spec = dc_cu != NULL && FileSpec::Compare(file_spec, *dc_cu, false) == 0;
                if (check_inlines || file_spec_matches_cu_file_spec)
                {
                    SymbolContext sc (m_obj_file->GetModule());
                    sc.comp_unit = GetCompUnitForDWARFCompUnit(curr_cu, cu_idx);
                    assert(sc.comp_unit != NULL);

                    uint32_t file_idx = UINT32_MAX;

                    // If we are looking for inline functions only and we don't
                    // find it in the support files, we are done.
                    if (check_inlines)
                    {
                        file_idx = sc.comp_unit->GetSupportFiles().FindFileIndex (1, file_spec);
                        if (file_idx == UINT32_MAX)
                            continue;
                    }

                    if (line != 0)
                    {
                        LineTable *line_table = sc.comp_unit->GetLineTable();

                        if (line_table != NULL && line != 0)
                        {
                            // We will have already looked up the file index if
                            // we are searching for inline entries.
                            if (!check_inlines)
                                file_idx = sc.comp_unit->GetSupportFiles().FindFileIndex (1, file_spec);

                            if (file_idx != UINT32_MAX)
                            {
                                uint32_t found_line;
                                uint32_t line_idx = line_table->FindLineEntryIndexByFileIndex (0, file_idx, line, false, &sc.line_entry);
                                found_line = sc.line_entry.line;

                                while (line_idx != UINT32_MAX)
                                {
                                    sc.function = NULL;
                                    sc.block = NULL;
                                    if (resolve_scope & (eSymbolContextFunction | eSymbolContextBlock))
                                    {
                                        const lldb::addr_t file_vm_addr = sc.line_entry.range.GetBaseAddress().GetFileAddress();
                                        if (file_vm_addr != LLDB_INVALID_ADDRESS)
                                        {
                                            DWARFDebugInfoEntry *function_die = NULL;
                                            DWARFDebugInfoEntry *block_die = NULL;
                                            curr_cu->LookupAddress(file_vm_addr, &function_die, resolve_scope & eSymbolContextBlock ? &block_die : NULL);

                                            if (function_die != NULL)
                                            {
                                                sc.function = sc.comp_unit->FindFunctionByUID (function_die->GetOffset()).get();
                                                if (sc.function == NULL)
                                                    sc.function = ParseCompileUnitFunction(sc, curr_cu, function_die);
                                            }

                                            if (sc.function != NULL)
                                            {
                                                Block& block = sc.function->GetBlock (true);

                                                if (block_die != NULL)
                                                    sc.block = block.FindBlockByID (block_die->GetOffset());
                                                else
                                                    sc.block = block.FindBlockByID (function_die->GetOffset());
                                            }
                                        }
                                    }

                                    sc_list.Append(sc);
                                    line_idx = line_table->FindLineEntryIndexByFileIndex (line_idx + 1, file_idx, found_line, true, &sc.line_entry);
                                }
                            }
                        }
                        else if (file_spec_matches_cu_file_spec && !check_inlines)
                        {
                            // only append the context if we aren't looking for inline call sites
                            // by file and line and if the file spec matches that of the compile unit
                            sc_list.Append(sc);
                        }
                    }
                    else if (file_spec_matches_cu_file_spec && !check_inlines)
                    {
                        // only append the context if we aren't looking for inline call sites
                        // by file and line and if the file spec matches that of the compile unit
                        sc_list.Append(sc);
                    }

                    if (!check_inlines)
                        break;
                }
            }
        }
    }
    return sc_list.GetSize() - prev_size;
}

void
SymbolFileDWARF::Index ()
{
    if (m_indexed)
        return;
    m_indexed = true;
    Timer scoped_timer (__PRETTY_FUNCTION__,
                        "SymbolFileDWARF::Index (%s)",
                        GetObjectFile()->GetFileSpec().GetFilename().AsCString());

    DWARFDebugInfo* debug_info = DebugInfo();
    if (debug_info)
    {
        m_aranges.reset(new DWARFDebugAranges());
    
        uint32_t cu_idx = 0;
        const uint32_t num_compile_units = GetNumCompileUnits();
        for (cu_idx = 0; cu_idx < num_compile_units; ++cu_idx)
        {
            DWARFCompileUnit* curr_cu = debug_info->GetCompileUnitAtIndex(cu_idx);

            bool clear_dies = curr_cu->ExtractDIEsIfNeeded (false) > 1;

            curr_cu->Index (cu_idx,
                            m_function_basename_index,
                            m_function_fullname_index,
                            m_function_method_index,
                            m_function_selector_index,
                            m_objc_class_selectors_index,
                            m_global_index, 
                            m_type_index,
                            m_namespace_index,
                            DebugRanges(),
                            m_aranges.get());  
            
            // Keep memory down by clearing DIEs if this generate function
            // caused them to be parsed
            if (clear_dies)
                curr_cu->ClearDIEs (true);
        }
        
        m_aranges->Sort();

#if defined (ENABLE_DEBUG_PRINTF)
        StreamFile s(stdout, false);
        s.Printf ("DWARF index for (%s) '%s/%s':", 
                  GetObjectFile()->GetModule()->GetArchitecture().AsCString(),
                  GetObjectFile()->GetFileSpec().GetDirectory().AsCString(), 
                  GetObjectFile()->GetFileSpec().GetFilename().AsCString());
        s.Printf("\nFunction basenames:\n");    m_function_basename_index.Dump (&s);
        s.Printf("\nFunction fullnames:\n");    m_function_fullname_index.Dump (&s);
        s.Printf("\nFunction methods:\n");      m_function_method_index.Dump (&s);
        s.Printf("\nFunction selectors:\n");    m_function_selector_index.Dump (&s);
        s.Printf("\nObjective C class selectors:\n");    m_objc_class_selectors_index.Dump (&s);
        s.Printf("\nGlobals and statics:\n");   m_global_index.Dump (&s); 
        s.Printf("\nTypes:\n");                 m_type_index.Dump (&s);
        s.Printf("\nNamepaces:\n");             m_namespace_index.Dump (&s);
#endif
    }
}

uint32_t
SymbolFileDWARF::FindGlobalVariables (const ConstString &name, bool append, uint32_t max_matches, VariableList& variables)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info == NULL)
        return 0;

    // If we aren't appending the results to this list, then clear the list
    if (!append)
        variables.Clear();

    // Remember how many variables are in the list before we search in case
    // we are appending the results to a variable list.
    const uint32_t original_size = variables.GetSize();

    // Index the DWARF if we haven't already
    if (!m_indexed)
        Index ();

    SymbolContext sc;
    sc.module_sp = m_obj_file->GetModule()->GetSP();
    assert (sc.module_sp);
    
    DWARFCompileUnit* curr_cu = NULL;
    DWARFCompileUnit* prev_cu = NULL;
    const DWARFDebugInfoEntry* die = NULL;
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = m_global_index.Find(name, die_info_array);
    for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
    {
        curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
        
        if (curr_cu != prev_cu)
            curr_cu->ExtractDIEsIfNeeded (false);

        die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);

        sc.comp_unit = GetCompUnitForDWARFCompUnit(curr_cu, UINT32_MAX);
        assert(sc.comp_unit != NULL);

        ParseVariables(sc, curr_cu, LLDB_INVALID_ADDRESS, die, false, false, &variables);

        if (variables.GetSize() - original_size >= max_matches)
            break;
    }

    // Return the number of variable that were appended to the list
    return variables.GetSize() - original_size;
}

uint32_t
SymbolFileDWARF::FindGlobalVariables(const RegularExpression& regex, bool append, uint32_t max_matches, VariableList& variables)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info == NULL)
        return 0;

    // If we aren't appending the results to this list, then clear the list
    if (!append)
        variables.Clear();

    // Remember how many variables are in the list before we search in case
    // we are appending the results to a variable list.
    const uint32_t original_size = variables.GetSize();

    // Index the DWARF if we haven't already
    if (!m_indexed)
        Index ();

    SymbolContext sc;
    sc.module_sp = m_obj_file->GetModule()->GetSP();
    assert (sc.module_sp);
    
    DWARFCompileUnit* curr_cu = NULL;
    DWARFCompileUnit* prev_cu = NULL;
    const DWARFDebugInfoEntry* die = NULL;
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = m_global_index.Find(regex, die_info_array);
    for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
    {
        curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
        
        if (curr_cu != prev_cu)
            curr_cu->ExtractDIEsIfNeeded (false);

        die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);

        sc.comp_unit = GetCompUnitForDWARFCompUnit(curr_cu, UINT32_MAX);
        assert(sc.comp_unit != NULL);

        ParseVariables(sc, curr_cu, LLDB_INVALID_ADDRESS, die, false, false, &variables);

        if (variables.GetSize() - original_size >= max_matches)
            break;
    }

    // Return the number of variable that were appended to the list
    return variables.GetSize() - original_size;
}


void
SymbolFileDWARF::FindFunctions
(
    const ConstString &name, 
    const NameToDIE &name_to_die,
    SymbolContextList& sc_list
)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info == NULL)
        return;

    SymbolContext sc;
    sc.module_sp = m_obj_file->GetModule()->GetSP();
    assert (sc.module_sp);
    
    DWARFCompileUnit* curr_cu = NULL;
    DWARFCompileUnit* prev_cu = NULL;
    const DWARFDebugInfoEntry* die = NULL;
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = name_to_die.Find (name, die_info_array);
    for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
    {
        curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
        
        if (curr_cu != prev_cu)
            curr_cu->ExtractDIEsIfNeeded (false);

        die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);
        
        const DWARFDebugInfoEntry* inlined_die = NULL;
        if (die->Tag() == DW_TAG_inlined_subroutine)
        {
            inlined_die = die;
            
            while ((die = die->GetParent()) != NULL)
            {
                if (die->Tag() == DW_TAG_subprogram)
                    break;
            }
        }
        assert (die->Tag() == DW_TAG_subprogram);
        if (GetFunction (curr_cu, die, sc))
        {
            Address addr;
            // Parse all blocks if needed
            if (inlined_die)
            {
                sc.block = sc.function->GetBlock (true).FindBlockByID (inlined_die->GetOffset());
                assert (sc.block != NULL);
                if (sc.block->GetStartAddress (addr) == false)
                    addr.Clear();
            }
            else 
            {
                sc.block = NULL;
                addr = sc.function->GetAddressRange().GetBaseAddress();
            }

            if (addr.IsValid())
            {
            
                // We found the function, so we should find the line table
                // and line table entry as well
                LineTable *line_table = sc.comp_unit->GetLineTable();
                if (line_table == NULL)
                {
                    if (ParseCompileUnitLineTable(sc))
                        line_table = sc.comp_unit->GetLineTable();
                }
                if (line_table != NULL)
                    line_table->FindLineEntryByAddress (addr, sc.line_entry);

                sc_list.Append(sc);
            }
        }
    }
}


void
SymbolFileDWARF::FindFunctions
(
    const RegularExpression &regex, 
    const NameToDIE &name_to_die,
    SymbolContextList& sc_list
)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info == NULL)
        return;

    SymbolContext sc;
    sc.module_sp = m_obj_file->GetModule()->GetSP();
    assert (sc.module_sp);
    
    DWARFCompileUnit* curr_cu = NULL;
    DWARFCompileUnit* prev_cu = NULL;
    const DWARFDebugInfoEntry* die = NULL;
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = name_to_die.Find(regex, die_info_array);
    for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
    {
        curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
        
        if (curr_cu != prev_cu)
            curr_cu->ExtractDIEsIfNeeded (false);

        die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);
        
        const DWARFDebugInfoEntry* inlined_die = NULL;
        if (die->Tag() == DW_TAG_inlined_subroutine)
        {
            inlined_die = die;
            
            while ((die = die->GetParent()) != NULL)
            {
                if (die->Tag() == DW_TAG_subprogram)
                    break;
            }
        }
        assert (die->Tag() == DW_TAG_subprogram);
        if (GetFunction (curr_cu, die, sc))
        {
            Address addr;
            // Parse all blocks if needed
            if (inlined_die)
            {
                sc.block = sc.function->GetBlock (true).FindBlockByID (inlined_die->GetOffset());
                assert (sc.block != NULL);
                if (sc.block->GetStartAddress (addr) == false)
                    addr.Clear();
            }
            else 
            {
                sc.block = NULL;
                addr = sc.function->GetAddressRange().GetBaseAddress();
            }

            if (addr.IsValid())
            {
            
                // We found the function, so we should find the line table
                // and line table entry as well
                LineTable *line_table = sc.comp_unit->GetLineTable();
                if (line_table == NULL)
                {
                    if (ParseCompileUnitLineTable(sc))
                        line_table = sc.comp_unit->GetLineTable();
                }
                if (line_table != NULL)
                    line_table->FindLineEntryByAddress (addr, sc.line_entry);

                sc_list.Append(sc);
            }
        }
    }
}

uint32_t
SymbolFileDWARF::FindFunctions
(
    const ConstString &name, 
    uint32_t name_type_mask, 
    bool append, 
    SymbolContextList& sc_list
)
{
    Timer scoped_timer (__PRETTY_FUNCTION__,
                        "SymbolFileDWARF::FindFunctions (name = '%s')",
                        name.AsCString());

    // If we aren't appending the results to this list, then clear the list
    if (!append)
        sc_list.Clear();

    // Remember how many sc_list are in the list before we search in case
    // we are appending the results to a variable list.
    uint32_t original_size = sc_list.GetSize();

    // Index the DWARF if we haven't already
    if (!m_indexed)
        Index ();

    if (name_type_mask & eFunctionNameTypeBase)
        FindFunctions (name, m_function_basename_index, sc_list);

    if (name_type_mask & eFunctionNameTypeFull)
        FindFunctions (name, m_function_fullname_index, sc_list);

    if (name_type_mask & eFunctionNameTypeMethod)
        FindFunctions (name, m_function_method_index, sc_list);

    if (name_type_mask & eFunctionNameTypeSelector)
        FindFunctions (name, m_function_selector_index, sc_list);

    // Return the number of variable that were appended to the list
    return sc_list.GetSize() - original_size;
}


uint32_t
SymbolFileDWARF::FindFunctions(const RegularExpression& regex, bool append, SymbolContextList& sc_list)
{
    Timer scoped_timer (__PRETTY_FUNCTION__,
                        "SymbolFileDWARF::FindFunctions (regex = '%s')",
                        regex.GetText());

    // If we aren't appending the results to this list, then clear the list
    if (!append)
        sc_list.Clear();

    // Remember how many sc_list are in the list before we search in case
    // we are appending the results to a variable list.
    uint32_t original_size = sc_list.GetSize();

    // Index the DWARF if we haven't already
    if (!m_indexed)
        Index ();

    FindFunctions (regex, m_function_basename_index, sc_list);

    FindFunctions (regex, m_function_fullname_index, sc_list);

    // Return the number of variable that were appended to the list
    return sc_list.GetSize() - original_size;
}

uint32_t
SymbolFileDWARF::FindTypes(const SymbolContext& sc, const ConstString &name, bool append, uint32_t max_matches, TypeList& types)
{
    DWARFDebugInfo* info = DebugInfo();
    if (info == NULL)
        return 0;

    // If we aren't appending the results to this list, then clear the list
    if (!append)
        types.Clear();

    // Index if we already haven't to make sure the compile units
    // get indexed and make their global DIE index list
    if (!m_indexed)
        Index ();

    const uint32_t initial_types_size = types.GetSize();
    DWARFCompileUnit* curr_cu = NULL;
    DWARFCompileUnit* prev_cu = NULL;
    const DWARFDebugInfoEntry* die = NULL;
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = m_type_index.Find (name, die_info_array);
    for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
    {
        curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
        
        if (curr_cu != prev_cu)
            curr_cu->ExtractDIEsIfNeeded (false);

        die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);

        Type *matching_type = ResolveType (curr_cu, die);
        if (matching_type)
        {
            // We found a type pointer, now find the shared pointer form our type list
            TypeSP type_sp (GetTypeList()->FindType(matching_type->GetID()));
            assert (type_sp.get() != NULL);
            types.InsertUnique (type_sp);
            if (types.GetSize() >= max_matches)
                break;
        }
    }
    return types.GetSize() - initial_types_size;
}


ClangNamespaceDecl
SymbolFileDWARF::FindNamespace (const SymbolContext& sc, 
                                const ConstString &name)
{
    ClangNamespaceDecl namespace_decl;
    DWARFDebugInfo* info = DebugInfo();
    if (info)
    {
        // Index if we already haven't to make sure the compile units
        // get indexed and make their global DIE index list
        if (!m_indexed)
            Index ();

        DWARFCompileUnit* curr_cu = NULL;
        DWARFCompileUnit* prev_cu = NULL;
        const DWARFDebugInfoEntry* die = NULL;
        std::vector<NameToDIE::Info> die_info_array;
        const size_t num_matches = m_namespace_index.Find (name, die_info_array);
        for (size_t i=0; i<num_matches; ++i, prev_cu = curr_cu)
        {
            curr_cu = info->GetCompileUnitAtIndex(die_info_array[i].cu_idx);
            
            if (curr_cu != prev_cu)
                curr_cu->ExtractDIEsIfNeeded (false);

            die = curr_cu->GetDIEAtIndexUnchecked(die_info_array[i].die_idx);

            clang::NamespaceDecl *clang_namespace_decl = ResolveNamespaceDIE (curr_cu, die);
            if (clang_namespace_decl)
            {
                namespace_decl.SetASTContext (GetClangASTContext().getASTContext());
                namespace_decl.SetNamespaceDecl (clang_namespace_decl);
            }
        }
    }
    return namespace_decl;
}

uint32_t
SymbolFileDWARF::FindTypes(std::vector<dw_offset_t> die_offsets, uint32_t max_matches, TypeList& types)
{
    // Remember how many sc_list are in the list before we search in case
    // we are appending the results to a variable list.
    uint32_t original_size = types.GetSize();

    const uint32_t num_die_offsets = die_offsets.size();
    // Parse all of the types we found from the pubtypes matches
    uint32_t i;
    uint32_t num_matches = 0;
    for (i = 0; i < num_die_offsets; ++i)
    {
        Type *matching_type = ResolveTypeUID (die_offsets[i]);
        if (matching_type)
        {
            // We found a type pointer, now find the shared pointer form our type list
            TypeSP type_sp (GetTypeList()->FindType(matching_type->GetID()));
            assert (type_sp.get() != NULL);
            types.InsertUnique (type_sp);
            ++num_matches;
            if (num_matches >= max_matches)
                break;
        }
    }

    // Return the number of variable that were appended to the list
    return types.GetSize() - original_size;
}


size_t
SymbolFileDWARF::ParseChildParameters
(
    const SymbolContext& sc,
    TypeSP& type_sp,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *parent_die,
    bool skip_artificial,
    TypeList* type_list,
    std::vector<clang_type_t>& function_param_types,
    std::vector<clang::ParmVarDecl*>& function_param_decls,
    unsigned &type_quals
)
{
    if (parent_die == NULL)
        return 0;

    const uint8_t *fixed_form_sizes = DWARFFormValue::GetFixedFormSizesForAddressSize (dwarf_cu->GetAddressByteSize());

    size_t arg_idx = 0;
    const DWARFDebugInfoEntry *die;
    for (die = parent_die->GetFirstChild(); die != NULL; die = die->GetSibling())
    {
        dw_tag_t tag = die->Tag();
        switch (tag)
        {
        case DW_TAG_formal_parameter:
            {
                DWARFDebugInfoEntry::Attributes attributes;
                const size_t num_attributes = die->GetAttributes(this, dwarf_cu, fixed_form_sizes, attributes);
                if (num_attributes > 0)
                {
                    const char *name = NULL;
                    Declaration decl;
                    dw_offset_t param_type_die_offset = DW_INVALID_OFFSET;
                    bool is_artificial = false;
                    // one of None, Auto, Register, Extern, Static, PrivateExtern

                    clang::StorageClass storage = clang::SC_None;
                    uint32_t i;
                    for (i=0; i<num_attributes; ++i)
                    {
                        const dw_attr_t attr = attributes.AttributeAtIndex(i);
                        DWARFFormValue form_value;
                        if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                        {
                            switch (attr)
                            {
                            case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                            case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                            case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                            case DW_AT_name:        name = form_value.AsCString(&get_debug_str_data()); break;
                            case DW_AT_type:        param_type_die_offset = form_value.Reference(dwarf_cu); break;
                            case DW_AT_artificial:  is_artificial = form_value.Unsigned() != 0; break;
                            case DW_AT_location:
    //                          if (form_value.BlockData())
    //                          {
    //                              const DataExtractor& debug_info_data = debug_info();
    //                              uint32_t block_length = form_value.Unsigned();
    //                              DataExtractor location(debug_info_data, form_value.BlockData() - debug_info_data.GetDataStart(), block_length);
    //                          }
    //                          else
    //                          {
    //                          }
    //                          break;
                            case DW_AT_const_value:
                            case DW_AT_default_value:
                            case DW_AT_description:
                            case DW_AT_endianity:
                            case DW_AT_is_optional:
                            case DW_AT_segment:
                            case DW_AT_variable_parameter:
                            default:
                            case DW_AT_abstract_origin:
                            case DW_AT_sibling:
                                break;
                            }
                        }
                    }

                    bool skip = false;
                    if (skip_artificial)
                    {
                        if (is_artificial)
                        {
                            // In order to determine if a C++ member function is
                            // "const" we have to look at the const-ness of "this"...
                            // Ugly, but that
                            if (arg_idx == 0)
                            {
                                const DWARFDebugInfoEntry *grandparent_die = parent_die->GetParent();
                                if (grandparent_die && (grandparent_die->Tag() == DW_TAG_structure_type || 
                                                        grandparent_die->Tag() == DW_TAG_class_type))
                                {
                                    LanguageType language = sc.comp_unit->GetLanguage();
                                    if (language == eLanguageTypeObjC_plus_plus || language == eLanguageTypeC_plus_plus)
                                    {
                                        // Often times compilers omit the "this" name for the
                                        // specification DIEs, so we can't rely upon the name
                                        // being in the formal parameter DIE...
                                        if (name == NULL || ::strcmp(name, "this")==0)
                                        {
                                            Type *this_type = ResolveTypeUID (param_type_die_offset);
                                            if (this_type)
                                            {
                                                uint32_t encoding_mask = this_type->GetEncodingMask();
                                                if (encoding_mask & Type::eEncodingIsPointerUID)
                                                {
                                                    if (encoding_mask & (1u << Type::eEncodingIsConstUID))
                                                        type_quals |= clang::Qualifiers::Const;
                                                    if (encoding_mask & (1u << Type::eEncodingIsVolatileUID))
                                                        type_quals |= clang::Qualifiers::Volatile;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            skip = true;
                        }
                        else
                        {

                            // HACK: Objective C formal parameters "self" and "_cmd" 
                            // are not marked as artificial in the DWARF...
                            CompileUnit *curr_cu = GetCompUnitForDWARFCompUnit(dwarf_cu, UINT32_MAX);
                            if (curr_cu && (curr_cu->GetLanguage() == eLanguageTypeObjC || curr_cu->GetLanguage() == eLanguageTypeObjC_plus_plus))
                            {
                                if (name && name[0] && (strcmp (name, "self") == 0 || strcmp (name, "_cmd") == 0))
                                    skip = true;
                            }
                        }
                    }

                    if (!skip)
                    {
                        Type *type = ResolveTypeUID(param_type_die_offset);
                        if (type)
                        {
                            function_param_types.push_back (type->GetClangForwardType());

                            clang::ParmVarDecl *param_var_decl = GetClangASTContext().CreateParameterDeclaration (name, type->GetClangForwardType(), storage);
                            assert(param_var_decl);
                            function_param_decls.push_back(param_var_decl);
                        }
                    }
                }
                arg_idx++;
            }
            break;

        default:
            break;
        }
    }
    return arg_idx;
}

size_t
SymbolFileDWARF::ParseChildEnumerators
(
    const SymbolContext& sc,
    clang_type_t  enumerator_clang_type,
    uint32_t enumerator_byte_size,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *parent_die
)
{
    if (parent_die == NULL)
        return 0;

    size_t enumerators_added = 0;
    const DWARFDebugInfoEntry *die;
    const uint8_t *fixed_form_sizes = DWARFFormValue::GetFixedFormSizesForAddressSize (dwarf_cu->GetAddressByteSize());

    for (die = parent_die->GetFirstChild(); die != NULL; die = die->GetSibling())
    {
        const dw_tag_t tag = die->Tag();
        if (tag == DW_TAG_enumerator)
        {
            DWARFDebugInfoEntry::Attributes attributes;
            const size_t num_child_attributes = die->GetAttributes(this, dwarf_cu, fixed_form_sizes, attributes);
            if (num_child_attributes > 0)
            {
                const char *name = NULL;
                bool got_value = false;
                int64_t enum_value = 0;
                Declaration decl;

                uint32_t i;
                for (i=0; i<num_child_attributes; ++i)
                {
                    const dw_attr_t attr = attributes.AttributeAtIndex(i);
                    DWARFFormValue form_value;
                    if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                    {
                        switch (attr)
                        {
                        case DW_AT_const_value:
                            got_value = true;
                            enum_value = form_value.Unsigned();
                            break;

                        case DW_AT_name:
                            name = form_value.AsCString(&get_debug_str_data());
                            break;

                        case DW_AT_description:
                        default:
                        case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                        case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                        case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                        case DW_AT_sibling:
                            break;
                        }
                    }
                }

                if (name && name[0] && got_value)
                {
                    GetClangASTContext().AddEnumerationValueToEnumerationType (enumerator_clang_type, 
                                                                               enumerator_clang_type, 
                                                                               decl, 
                                                                               name, 
                                                                               enum_value, 
                                                                               enumerator_byte_size * 8);
                    ++enumerators_added;
                }
            }
        }
    }
    return enumerators_added;
}

void
SymbolFileDWARF::ParseChildArrayInfo
(
    const SymbolContext& sc,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *parent_die,
    int64_t& first_index,
    std::vector<uint64_t>& element_orders,
    uint32_t& byte_stride,
    uint32_t& bit_stride
)
{
    if (parent_die == NULL)
        return;

    const DWARFDebugInfoEntry *die;
    const uint8_t *fixed_form_sizes = DWARFFormValue::GetFixedFormSizesForAddressSize (dwarf_cu->GetAddressByteSize());
    for (die = parent_die->GetFirstChild(); die != NULL; die = die->GetSibling())
    {
        const dw_tag_t tag = die->Tag();
        switch (tag)
        {
        case DW_TAG_enumerator:
            {
                DWARFDebugInfoEntry::Attributes attributes;
                const size_t num_child_attributes = die->GetAttributes(this, dwarf_cu, fixed_form_sizes, attributes);
                if (num_child_attributes > 0)
                {
                    const char *name = NULL;
                    bool got_value = false;
                    int64_t enum_value = 0;

                    uint32_t i;
                    for (i=0; i<num_child_attributes; ++i)
                    {
                        const dw_attr_t attr = attributes.AttributeAtIndex(i);
                        DWARFFormValue form_value;
                        if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                        {
                            switch (attr)
                            {
                            case DW_AT_const_value:
                                got_value = true;
                                enum_value = form_value.Unsigned();
                                break;

                            case DW_AT_name:
                                name = form_value.AsCString(&get_debug_str_data());
                                break;

                            case DW_AT_description:
                            default:
                            case DW_AT_decl_file:
                            case DW_AT_decl_line:
                            case DW_AT_decl_column:
                            case DW_AT_sibling:
                                break;
                            }
                        }
                    }
                }
            }
            break;

        case DW_TAG_subrange_type:
            {
                DWARFDebugInfoEntry::Attributes attributes;
                const size_t num_child_attributes = die->GetAttributes(this, dwarf_cu, fixed_form_sizes, attributes);
                if (num_child_attributes > 0)
                {
                    const char *name = NULL;
                    bool got_value = false;
                    uint64_t byte_size = 0;
                    int64_t enum_value = 0;
                    uint64_t num_elements = 0;
                    uint64_t lower_bound = 0;
                    uint64_t upper_bound = 0;
                    uint32_t i;
                    for (i=0; i<num_child_attributes; ++i)
                    {
                        const dw_attr_t attr = attributes.AttributeAtIndex(i);
                        DWARFFormValue form_value;
                        if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                        {
                            switch (attr)
                            {
                            case DW_AT_const_value:
                                got_value = true;
                                enum_value = form_value.Unsigned();
                                break;

                            case DW_AT_name:
                                name = form_value.AsCString(&get_debug_str_data());
                                break;

                            case DW_AT_count:
                                num_elements = form_value.Unsigned();
                                break;

                            case DW_AT_bit_stride:
                                bit_stride = form_value.Unsigned();
                                break;

                            case DW_AT_byte_stride:
                                byte_stride = form_value.Unsigned();
                                break;

                            case DW_AT_byte_size:
                                byte_size = form_value.Unsigned();
                                break;

                            case DW_AT_lower_bound:
                                lower_bound = form_value.Unsigned();
                                break;

                            case DW_AT_upper_bound:
                                upper_bound = form_value.Unsigned();
                                break;

                            default:
                            case DW_AT_abstract_origin:
                            case DW_AT_accessibility:
                            case DW_AT_allocated:
                            case DW_AT_associated:
                            case DW_AT_data_location:
                            case DW_AT_declaration:
                            case DW_AT_description:
                            case DW_AT_sibling:
                            case DW_AT_threads_scaled:
                            case DW_AT_type:
                            case DW_AT_visibility:
                                break;
                            }
                        }
                    }

                    if (upper_bound > lower_bound)
                        num_elements = upper_bound - lower_bound + 1;

                    if (num_elements > 0)
                        element_orders.push_back (num_elements);
                }
            }
            break;
        }
    }
}

TypeSP
SymbolFileDWARF::GetTypeForDIE (DWARFCompileUnit *curr_cu, const DWARFDebugInfoEntry* die)
{
    TypeSP type_sp;
    if (die != NULL)
    {
        assert(curr_cu != NULL);
        Type *type_ptr = m_die_to_type.lookup (die);
        if (type_ptr == NULL)
        {
            CompileUnit* lldb_cu = GetCompUnitForDWARFCompUnit(curr_cu);
            assert (lldb_cu);
            SymbolContext sc(lldb_cu);
            type_sp = ParseType(sc, curr_cu, die, NULL);
        }
        else if (type_ptr != DIE_IS_BEING_PARSED)
        {
            // Grab the existing type from the master types lists
            type_sp = GetTypeList()->FindType(type_ptr->GetID());
        }

    }
    return type_sp;
}

clang::DeclContext *
SymbolFileDWARF::GetClangDeclContextForDIEOffset (dw_offset_t die_offset)
{
    if (die_offset != DW_INVALID_OFFSET)
    {
        DWARFCompileUnitSP cu_sp;
        const DWARFDebugInfoEntry* die = DebugInfo()->GetDIEPtr(die_offset, &cu_sp);
        return GetClangDeclContextForDIE (cu_sp.get(), die);
    }
    return NULL;
}


clang::NamespaceDecl *
SymbolFileDWARF::ResolveNamespaceDIE (DWARFCompileUnit *curr_cu, const DWARFDebugInfoEntry *die)
{
    if (die->Tag() == DW_TAG_namespace)
    {
        const char *namespace_name = die->GetAttributeValueAsString(this, curr_cu, DW_AT_name, NULL);
        if (namespace_name)
        {
            Declaration decl;   // TODO: fill in the decl object
            clang::NamespaceDecl *namespace_decl = GetClangASTContext().GetUniqueNamespaceDeclaration (namespace_name, decl, GetClangDeclContextForDIE (curr_cu, die->GetParent()));
            if (namespace_decl)
                m_die_to_decl_ctx[die] = (clang::DeclContext*)namespace_decl;
            return namespace_decl;
        }
    }
    return NULL;
}

clang::DeclContext *
SymbolFileDWARF::GetClangDeclContextForDIE (DWARFCompileUnit *curr_cu, const DWARFDebugInfoEntry *die)
{
    if (m_clang_tu_decl == NULL)
        m_clang_tu_decl = GetClangASTContext().getASTContext()->getTranslationUnitDecl();

    //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x )\n", die->GetOffset());
    const DWARFDebugInfoEntry * const decl_die = die;
    clang::DeclContext *decl_ctx = NULL;

    while (die != NULL)
    {
        // If this is the original DIE that we are searching for a declaration 
        // for, then don't look in the cache as we don't want our own decl 
        // context to be our decl context...
        if (decl_die != die)
        {
            DIEToDeclContextMap::iterator pos = m_die_to_decl_ctx.find(die);
            if (pos != m_die_to_decl_ctx.end())
            {
                //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) => 0x%8.8x\n", decl_die->GetOffset(), die->GetOffset());
                return pos->second;
            }

            //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) checking parent 0x%8.8x\n", decl_die->GetOffset(), die->GetOffset());

            switch (die->Tag())
            {
            case DW_TAG_namespace:
                {
                    const char *namespace_name = die->GetAttributeValueAsString(this, curr_cu, DW_AT_name, NULL);
                    if (namespace_name)
                    {
                        Declaration decl;   // TODO: fill in the decl object
                        clang::NamespaceDecl *namespace_decl = GetClangASTContext().GetUniqueNamespaceDeclaration (namespace_name, decl, GetClangDeclContextForDIE (curr_cu, die));
                        if (namespace_decl)
                        {
                            //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) => 0x%8.8x\n", decl_die->GetOffset(), die->GetOffset());
                            m_die_to_decl_ctx[die] = (clang::DeclContext*)namespace_decl;
                        }
                        return namespace_decl;
                    }
                }
                break;

            case DW_TAG_structure_type:
            case DW_TAG_union_type:
            case DW_TAG_class_type:
                {
                    Type* type = ResolveType (curr_cu, die);
                    pos = m_die_to_decl_ctx.find(die);
                    if (pos != m_die_to_decl_ctx.end())
                    {
                        //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) => 0x%8.8x\n", decl_die->GetOffset(), die->GetOffset());
                        return pos->second;
                    }
                    else
                    {
                        if (type)
                        {
                            decl_ctx = ClangASTContext::GetDeclContextForType (type->GetClangForwardType ());
                            if (decl_ctx)
                                return decl_ctx;
                        }
                    }
                }
                break;

            default:
                break;
            }
        }

        dw_offset_t die_offset = die->GetAttributeValueAsReference(this, curr_cu, DW_AT_specification, DW_INVALID_OFFSET);
        if (die_offset != DW_INVALID_OFFSET)
        {
            //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) check DW_AT_specification 0x%8.8x\n", decl_die->GetOffset(), die_offset);
            decl_ctx = GetClangDeclContextForDIEOffset (die_offset);
            if (decl_ctx != m_clang_tu_decl)
                return decl_ctx;
        }

        die_offset = die->GetAttributeValueAsReference(this, curr_cu, DW_AT_abstract_origin, DW_INVALID_OFFSET);
        if (die_offset != DW_INVALID_OFFSET)
        {
            //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) check DW_AT_abstract_origin 0x%8.8x\n", decl_die->GetOffset(), die_offset);
            decl_ctx = GetClangDeclContextForDIEOffset (die_offset);
            if (decl_ctx != m_clang_tu_decl)
                return decl_ctx;
        }

        die = die->GetParent();
    }
    // Right now we have only one translation unit per module...
    //printf ("SymbolFileDWARF::GetClangDeclContextForDIE ( die = 0x%8.8x ) => 0x%8.8x\n", decl_die->GetOffset(), curr_cu->GetFirstDIEOffset());
    return m_clang_tu_decl;
}

// This function can be used when a DIE is found that is a forward declaration
// DIE and we want to try and find a type that has the complete definition.
TypeSP
SymbolFileDWARF::FindDefinitionTypeForDIE (
    DWARFCompileUnit* cu, 
    const DWARFDebugInfoEntry *die, 
    const ConstString &type_name
)
{
    TypeSP type_sp;

    if (cu == NULL || die == NULL || !type_name)
        return type_sp;

    if (!m_indexed)
        Index ();

    const dw_tag_t type_tag = die->Tag();
    std::vector<NameToDIE::Info> die_info_array;
    const size_t num_matches = m_type_index.Find (type_name, die_info_array);
    if (num_matches > 0)
    {
        DWARFCompileUnit* type_cu = NULL;
        DWARFCompileUnit* curr_cu = cu;
        DWARFDebugInfo *info = DebugInfo();
        for (size_t i=0; i<num_matches; ++i)
        {
            type_cu = info->GetCompileUnitAtIndex (die_info_array[i].cu_idx);
            
            if (type_cu != curr_cu)
            {
                type_cu->ExtractDIEsIfNeeded (false);
                curr_cu = type_cu;
            }

            DWARFDebugInfoEntry *type_die = type_cu->GetDIEAtIndexUnchecked (die_info_array[i].die_idx);
            
            if (type_die != die && type_die->Tag() == type_tag)
            {
                // Hold off on comparing parent DIE tags until
                // we know what happens with stuff in namespaces
                // for gcc and clang...
                //DWARFDebugInfoEntry *parent_die = die->GetParent();
                //DWARFDebugInfoEntry *parent_type_die = type_die->GetParent();
                //if (parent_die->Tag() == parent_type_die->Tag())
                {
                    Type *resolved_type = ResolveType (type_cu, type_die, false);
                    if (resolved_type && resolved_type != DIE_IS_BEING_PARSED)
                    {
                        DEBUG_PRINTF ("resolved 0x%8.8x (cu 0x%8.8x) from %s to 0x%8.8x (cu 0x%8.8x)\n",
                                      die->GetOffset(), 
                                      curr_cu->GetOffset(), 
                                      m_obj_file->GetFileSpec().GetFilename().AsCString(),
                                      type_die->GetOffset(), 
                                      type_cu->GetOffset());
                        
                        m_die_to_type[die] = resolved_type;
                        type_sp = GetTypeList()->FindType(resolved_type->GetID());
                        if (!type_sp)
                        {
                            DEBUG_PRINTF("unable to resolve type '%s' from DIE 0x%8.8x\n", type_name.GetCString(), die->GetOffset());
                        }
                        break;
                    }
                }
            }
        }
    }
    return type_sp;
}

TypeSP
SymbolFileDWARF::ParseType (const SymbolContext& sc, DWARFCompileUnit* dwarf_cu, const DWARFDebugInfoEntry *die, bool *type_is_new_ptr)
{
    TypeSP type_sp;

    if (type_is_new_ptr)
        *type_is_new_ptr = false;

    AccessType accessibility = eAccessNone;
    if (die != NULL)
    {
        Type *type_ptr = m_die_to_type.lookup (die);
        TypeList* type_list = GetTypeList();
        if (type_ptr == NULL)
        {
            ClangASTContext &ast = GetClangASTContext();
            if (type_is_new_ptr)
                *type_is_new_ptr = true;

            const dw_tag_t tag = die->Tag();

            bool is_forward_declaration = false;
            DWARFDebugInfoEntry::Attributes attributes;
            const char *type_name_cstr = NULL;
            ConstString type_name_const_str;
            Type::ResolveState resolve_state = Type::eResolveStateUnresolved;
            size_t byte_size = 0;
            Declaration decl;

            Type::EncodingDataType encoding_data_type = Type::eEncodingIsUID;
            clang_type_t clang_type = NULL;

            dw_attr_t attr;

            switch (tag)
            {
            case DW_TAG_base_type:
            case DW_TAG_pointer_type:
            case DW_TAG_reference_type:
            case DW_TAG_typedef:
            case DW_TAG_const_type:
            case DW_TAG_restrict_type:
            case DW_TAG_volatile_type:
                {
                    // Set a bit that lets us know that we are currently parsing this
                    m_die_to_type[die] = DIE_IS_BEING_PARSED;

                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
                    uint32_t encoding = 0;
                    lldb::user_id_t encoding_uid = LLDB_INVALID_UID;

                    if (num_attributes > 0)
                    {
                        uint32_t i;
                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                                case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                                case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                                case DW_AT_name:
                                    type_name_cstr = form_value.AsCString(&get_debug_str_data());
                                    type_name_const_str.SetCString(type_name_cstr);
                                    break;
                                case DW_AT_byte_size:   byte_size = form_value.Unsigned();  break;
                                case DW_AT_encoding:    encoding = form_value.Unsigned(); break;
                                case DW_AT_type:        encoding_uid = form_value.Reference(dwarf_cu); break;
                                default:
                                case DW_AT_sibling:
                                    break;
                                }
                            }
                        }
                    }

                    DEBUG_PRINTF ("0x%8.8x: %s (\"%s\") type => 0x%8.8x\n", die->GetOffset(), DW_TAG_value_to_name(tag), type_name_cstr, encoding_uid);

                    switch (tag)
                    {
                    default:
                        break;

                    case DW_TAG_base_type:
                        resolve_state = Type::eResolveStateFull;
                        clang_type = ast.GetBuiltinTypeForDWARFEncodingAndBitSize (type_name_cstr, 
                                                                                   encoding, 
                                                                                   byte_size * 8);
                        break;

                    case DW_TAG_pointer_type:   encoding_data_type = Type::eEncodingIsPointerUID;           break;
                    case DW_TAG_reference_type: encoding_data_type = Type::eEncodingIsLValueReferenceUID;   break;
                    case DW_TAG_typedef:        encoding_data_type = Type::eEncodingIsTypedefUID;           break;
                    case DW_TAG_const_type:     encoding_data_type = Type::eEncodingIsConstUID;             break;
                    case DW_TAG_restrict_type:  encoding_data_type = Type::eEncodingIsRestrictUID;          break;
                    case DW_TAG_volatile_type:  encoding_data_type = Type::eEncodingIsVolatileUID;          break;
                    }

                    if (type_name_cstr != NULL && sc.comp_unit != NULL && 
                        (sc.comp_unit->GetLanguage() == eLanguageTypeObjC || sc.comp_unit->GetLanguage() == eLanguageTypeObjC_plus_plus))
                    {
                        static ConstString g_objc_type_name_id("id");
                        static ConstString g_objc_type_name_Class("Class");
                        static ConstString g_objc_type_name_selector("SEL");
                        
                        if (type_name_const_str == g_objc_type_name_id)
                        {
                            clang_type = ast.GetBuiltInType_objc_id();
                            resolve_state = Type::eResolveStateFull;

                        }
                        else if (type_name_const_str == g_objc_type_name_Class)
                        {
                            clang_type = ast.GetBuiltInType_objc_Class();
                            resolve_state = Type::eResolveStateFull;
                        }
                        else if (type_name_const_str == g_objc_type_name_selector)
                        {
                            clang_type = ast.GetBuiltInType_objc_selector();
                            resolve_state = Type::eResolveStateFull;
                        }
                    }
                        
                    type_sp.reset( new Type (die->GetOffset(), 
                                             this, 
                                             type_name_const_str, 
                                             byte_size, 
                                             NULL, 
                                             encoding_uid, 
                                             encoding_data_type, 
                                             &decl, 
                                             clang_type, 
                                             resolve_state));
                    
                    m_die_to_type[die] = type_sp.get();

//                  Type* encoding_type = GetUniquedTypeForDIEOffset(encoding_uid, type_sp, NULL, 0, 0, false);
//                  if (encoding_type != NULL)
//                  {
//                      if (encoding_type != DIE_IS_BEING_PARSED)
//                          type_sp->SetEncodingType(encoding_type);
//                      else
//                          m_indirect_fixups.push_back(type_sp.get());
//                  }
                }
                break;

            case DW_TAG_structure_type:
            case DW_TAG_union_type:
            case DW_TAG_class_type:
                {
                    // Set a bit that lets us know that we are currently parsing this
                    m_die_to_type[die] = DIE_IS_BEING_PARSED;

                    LanguageType class_language = eLanguageTypeUnknown;
                    //bool struct_is_class = false;
                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
                    if (num_attributes > 0)
                    {
                        uint32_t i;
                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                case DW_AT_decl_file:
                                    decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); 
                                    break;

                                case DW_AT_decl_line:
                                    decl.SetLine(form_value.Unsigned()); 
                                    break;

                                case DW_AT_decl_column: 
                                    decl.SetColumn(form_value.Unsigned()); 
                                    break;

                                case DW_AT_name:
                                    type_name_cstr = form_value.AsCString(&get_debug_str_data());
                                    type_name_const_str.SetCString(type_name_cstr);
                                    break;

                                case DW_AT_byte_size:   
                                    byte_size = form_value.Unsigned(); 
                                    break;

                                case DW_AT_accessibility: 
                                    accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned()); 
                                    break;

                                case DW_AT_declaration: 
                                    is_forward_declaration = form_value.Unsigned() != 0; 
                                    break;

                                case DW_AT_APPLE_runtime_class: 
                                    class_language = (LanguageType)form_value.Signed(); 
                                    break;

                                case DW_AT_allocated:
                                case DW_AT_associated:
                                case DW_AT_data_location:
                                case DW_AT_description:
                                case DW_AT_start_scope:
                                case DW_AT_visibility:
                                default:
                                case DW_AT_sibling:
                                    break;
                                }
                            }
                        }
                    }

                    UniqueDWARFASTType unique_ast_entry;
                    if (decl.IsValid())
                    {
                        if (GetUniqueDWARFASTTypeMap().Find (type_name_const_str,
                                                             die,
                                                             decl,
                                                             unique_ast_entry))
                        {
                            // We have already parsed this type or from another 
                            // compile unit. GCC loves to use the "one definition
                            // rule" which can result in multiple definitions
                            // of the same class over and over in each compile
                            // unit.
                            type_sp = unique_ast_entry.m_type_sp;
                            if (type_sp)
                            {
                                m_die_to_type[die] = type_sp.get();
                                return type_sp;
                            }
                        }
                    }
                    
                    DEBUG_PRINTF ("0x%8.8x: %s (\"%s\")\n", die->GetOffset(), DW_TAG_value_to_name(tag), type_name_cstr);

                    int tag_decl_kind = -1;
                    AccessType default_accessibility = eAccessNone;
                    if (tag == DW_TAG_structure_type)
                    {
                        tag_decl_kind = clang::TTK_Struct;
                        default_accessibility = eAccessPublic;
                    }
                    else if (tag == DW_TAG_union_type)
                    {
                        tag_decl_kind = clang::TTK_Union;
                        default_accessibility = eAccessPublic;
                    }
                    else if (tag == DW_TAG_class_type)
                    {
                        tag_decl_kind = clang::TTK_Class;
                        default_accessibility = eAccessPrivate;
                    }


                    if (is_forward_declaration)
                    {
                        // We have a forward declaration to a type and we need
                        // to try and find a full declaration. We look in the
                        // current type index just in case we have a forward
                        // declaration followed by an actual declarations in the
                        // DWARF. If this fails, we need to look elsewhere...
                    
                        type_sp = FindDefinitionTypeForDIE (dwarf_cu, die, type_name_const_str);

                        if (!type_sp && m_debug_map_symfile)
                        {
                            // We weren't able to find a full declaration in
                            // this DWARF, see if we have a declaration anywhere    
                            // else...
                            type_sp = m_debug_map_symfile->FindDefinitionTypeForDIE (dwarf_cu, die, type_name_const_str);
                        }

                        if (type_sp)
                        {
                            // We found a real definition for this type elsewhere
                            // so lets use it and cache the fact that we found
                            // a complete type for this die
                            m_die_to_type[die] = type_sp.get();
                            return type_sp;
                        }
                    }
                    assert (tag_decl_kind != -1);
                    bool clang_type_was_created = false;
                    clang_type = m_forward_decl_die_to_clang_type.lookup (die);
                    if (clang_type == NULL)
                    {
                        clang_type_was_created = true;
                        clang_type = ast.CreateRecordType (type_name_cstr, 
                                                           tag_decl_kind, 
                                                           GetClangDeclContextForDIE (dwarf_cu, die), 
                                                           class_language);
                    }

                    // Store a forward declaration to this class type in case any 
                    // parameters in any class methods need it for the clang 
                    // types for function prototypes. 
                    m_die_to_decl_ctx[die] = ClangASTContext::GetDeclContextForType (clang_type);
                    type_sp.reset (new Type (die->GetOffset(), 
                                             this, 
                                             type_name_const_str, 
                                             byte_size, 
                                             NULL, 
                                             LLDB_INVALID_UID, 
                                             Type::eEncodingIsUID, 
                                             &decl, 
                                             clang_type, 
                                             Type::eResolveStateForward));


                    // Add our type to the unique type map so we don't
                    // end up creating many copies of the same type over
                    // and over in the ASTContext for our module
                    unique_ast_entry.m_type_sp = type_sp;
                    unique_ast_entry.m_die = die;
                    unique_ast_entry.m_declaration = decl;
                    GetUniqueDWARFASTTypeMap().Insert (type_name_const_str, 
                                                       unique_ast_entry);
                    
                    if (die->HasChildren() == false && is_forward_declaration == false)
                    {
                        // No children for this struct/union/class, lets finish it
                        ast.StartTagDeclarationDefinition (clang_type);
                        ast.CompleteTagDeclarationDefinition (clang_type);
                    }
                    else if (clang_type_was_created)
                    {
                        // Leave this as a forward declaration until we need
                        // to know the details of the type. lldb_private::Type
                        // will automatically call the SymbolFile virtual function
                        // "SymbolFileDWARF::ResolveClangOpaqueTypeDefinition(Type *)"
                        // When the definition needs to be defined.
                        m_forward_decl_die_to_clang_type[die] = clang_type;
                        m_forward_decl_clang_type_to_die[ClangASTType::RemoveFastQualifiers (clang_type)] = die;
                        ClangASTContext::SetHasExternalStorage (clang_type, true);
                    }
                }
                break;

            case DW_TAG_enumeration_type:
                {
                    // Set a bit that lets us know that we are currently parsing this
                    m_die_to_type[die] = DIE_IS_BEING_PARSED;

                    lldb::user_id_t encoding_uid = DW_INVALID_OFFSET;

                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
                    if (num_attributes > 0)
                    {
                        uint32_t i;

                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                case DW_AT_decl_file:       decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                                case DW_AT_decl_line:       decl.SetLine(form_value.Unsigned()); break;
                                case DW_AT_decl_column:     decl.SetColumn(form_value.Unsigned()); break;
                                case DW_AT_name:
                                    type_name_cstr = form_value.AsCString(&get_debug_str_data());
                                    type_name_const_str.SetCString(type_name_cstr);
                                    break;
                                case DW_AT_type:            encoding_uid = form_value.Reference(dwarf_cu); break;
                                case DW_AT_byte_size:       byte_size = form_value.Unsigned(); break;
                                case DW_AT_accessibility:   accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
                                case DW_AT_declaration:     is_forward_declaration = form_value.Unsigned() != 0; break;
                                case DW_AT_allocated:
                                case DW_AT_associated:
                                case DW_AT_bit_stride:
                                case DW_AT_byte_stride:
                                case DW_AT_data_location:
                                case DW_AT_description:
                                case DW_AT_start_scope:
                                case DW_AT_visibility:
                                case DW_AT_specification:
                                case DW_AT_abstract_origin:
                                case DW_AT_sibling:
                                    break;
                                }
                            }
                        }

                        DEBUG_PRINTF ("0x%8.8x: %s (\"%s\")\n", die->GetOffset(), DW_TAG_value_to_name(tag), type_name_cstr);

                        clang_type_t enumerator_clang_type = NULL;
                        clang_type = m_forward_decl_die_to_clang_type.lookup (die);
                        if (clang_type == NULL)
                        {
                            if (die->GetOffset() == 0x1c436)
                                printf("REMOVE THIS!!!\n");
                            enumerator_clang_type = ast.GetBuiltinTypeForDWARFEncodingAndBitSize (NULL, 
                                                                                                  DW_ATE_signed, 
                                                                                                  byte_size * 8);
                            clang_type = ast.CreateEnumerationType (type_name_cstr, 
                                                                    GetClangDeclContextForDIE (dwarf_cu, die), 
                                                                    decl,
                                                                    enumerator_clang_type);
                        }
                        else
                        {
                            enumerator_clang_type = ClangASTContext::GetEnumerationIntegerType (clang_type);
                            assert (enumerator_clang_type != NULL);
                        }

                        m_die_to_decl_ctx[die] = ClangASTContext::GetDeclContextForType (clang_type);
                        type_sp.reset( new Type (die->GetOffset(), 
                                                 this, 
                                                 type_name_const_str, 
                                                 byte_size, 
                                                 NULL, 
                                                 encoding_uid, 
                                                 Type::eEncodingIsUID,
                                                 &decl, 
                                                 clang_type, 
                                                 Type::eResolveStateForward));

#if LEAVE_ENUMS_FORWARD_DECLARED
                        // Leave this as a forward declaration until we need
                        // to know the details of the type. lldb_private::Type
                        // will automatically call the SymbolFile virtual function
                        // "SymbolFileDWARF::ResolveClangOpaqueTypeDefinition(Type *)"
                        // When the definition needs to be defined.
                        m_forward_decl_die_to_clang_type[die] = clang_type;
                        m_forward_decl_clang_type_to_die[ClangASTType::RemoveFastQualifiers (clang_type)] = die;
                        ClangASTContext::SetHasExternalStorage (clang_type, true);
#else
                        ast.StartTagDeclarationDefinition (clang_type);
                        if (die->HasChildren())
                        {
                            SymbolContext cu_sc(GetCompUnitForDWARFCompUnit(dwarf_cu));
                            ParseChildEnumerators(cu_sc, clang_type, type_sp->GetByteSize(), dwarf_cu, die);
                        }
                        ast.CompleteTagDeclarationDefinition (clang_type);
#endif
                    }
                }
                break;

            case DW_TAG_inlined_subroutine:
            case DW_TAG_subprogram:
            case DW_TAG_subroutine_type:
                {
                    // Set a bit that lets us know that we are currently parsing this
                    m_die_to_type[die] = DIE_IS_BEING_PARSED;

                    const char *mangled = NULL;
                    dw_offset_t type_die_offset = DW_INVALID_OFFSET;
                    bool is_variadic = false;
                    bool is_inline = false;
                    bool is_static = false;
                    bool is_virtual = false;
                    bool is_explicit = false;

                    unsigned type_quals = 0;
                    clang::StorageClass storage = clang::SC_None;//, Extern, Static, PrivateExtern


                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
                    if (num_attributes > 0)
                    {
                        uint32_t i;
                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                                case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                                case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                                case DW_AT_name:
                                    type_name_cstr = form_value.AsCString(&get_debug_str_data());
                                    type_name_const_str.SetCString(type_name_cstr);
                                    break;

                                case DW_AT_MIPS_linkage_name:   mangled = form_value.AsCString(&get_debug_str_data()); break;
                                case DW_AT_type:                type_die_offset = form_value.Reference(dwarf_cu); break;
                                case DW_AT_accessibility:       accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
                                case DW_AT_declaration:         is_forward_declaration = form_value.Unsigned() != 0; break;
                                case DW_AT_inline:              is_inline = form_value.Unsigned() != 0; break;
                                case DW_AT_virtuality:          is_virtual = form_value.Unsigned() != 0;  break;
                                case DW_AT_explicit:            is_explicit = form_value.Unsigned() != 0;  break; 

                                case DW_AT_external:
                                    if (form_value.Unsigned())
                                    {
                                        if (storage == clang::SC_None)
                                            storage = clang::SC_Extern;
                                        else
                                            storage = clang::SC_PrivateExtern;
                                    }
                                    break;

                                case DW_AT_allocated:
                                case DW_AT_associated:
                                case DW_AT_address_class:
                                case DW_AT_artificial:
                                case DW_AT_calling_convention:
                                case DW_AT_data_location:
                                case DW_AT_elemental:
                                case DW_AT_entry_pc:
                                case DW_AT_frame_base:
                                case DW_AT_high_pc:
                                case DW_AT_low_pc:
                                case DW_AT_object_pointer:
                                case DW_AT_prototyped:
                                case DW_AT_pure:
                                case DW_AT_ranges:
                                case DW_AT_recursive:
                                case DW_AT_return_addr:
                                case DW_AT_segment:
                                case DW_AT_specification:
                                case DW_AT_start_scope:
                                case DW_AT_static_link:
                                case DW_AT_trampoline:
                                case DW_AT_visibility:
                                case DW_AT_vtable_elem_location:
                                case DW_AT_abstract_origin:
                                case DW_AT_description:
                                case DW_AT_sibling:
                                    break;
                                }
                            }
                        }
                    }

                    DEBUG_PRINTF ("0x%8.8x: %s (\"%s\")\n", die->GetOffset(), DW_TAG_value_to_name(tag), type_name_cstr);

                    clang_type_t return_clang_type = NULL;
                    Type *func_type = NULL;
                    
                    if (type_die_offset != DW_INVALID_OFFSET)
                        func_type = ResolveTypeUID(type_die_offset);

                    if (func_type)
                        return_clang_type = func_type->GetClangLayoutType();
                    else
                        return_clang_type = ast.GetBuiltInType_void();


                    std::vector<clang_type_t> function_param_types;
                    std::vector<clang::ParmVarDecl*> function_param_decls;

                    // Parse the function children for the parameters
                    if (die->HasChildren())
                    {
                        bool skip_artificial = true;
                        ParseChildParameters (sc, 
                                              type_sp, 
                                              dwarf_cu, 
                                              die, 
                                              skip_artificial, 
                                              type_list, 
                                              function_param_types, 
                                              function_param_decls,
                                              type_quals);
                    }

                    // clang_type will get the function prototype clang type after this call
                    clang_type = ast.CreateFunctionType (return_clang_type, 
                                                         &function_param_types[0], 
                                                         function_param_types.size(), 
                                                         is_variadic, 
                                                         type_quals);
                    
                    if (type_name_cstr)
                    {
                        bool type_handled = false;
                        const DWARFDebugInfoEntry *parent_die = die->GetParent();
                        if (tag == DW_TAG_subprogram)
                        {
                            if (type_name_cstr[1] == '[' && (type_name_cstr[0] == '-' || type_name_cstr[0] == '+'))
                            {
                                // We need to find the DW_TAG_class_type or 
                                // DW_TAG_struct_type by name so we can add this
                                // as a member function of the class.
                                const char *class_name_start = type_name_cstr + 2;
                                const char *class_name_end = ::strchr (class_name_start, ' ');
                                SymbolContext empty_sc;
                                clang_type_t class_opaque_type = NULL;
                                if (class_name_start < class_name_end)
                                {
                                    ConstString class_name (class_name_start, class_name_end - class_name_start);
                                    TypeList types;
                                    const uint32_t match_count = FindTypes (empty_sc, class_name, true, UINT32_MAX, types);
                                    if (match_count > 0)
                                    {
                                        for (uint32_t i=0; i<match_count; ++i)
                                        {
                                            Type *type = types.GetTypeAtIndex (i).get();
                                            clang_type_t type_clang_forward_type = type->GetClangForwardType();
                                            if (ClangASTContext::IsObjCClassType (type_clang_forward_type))
                                            {
                                                class_opaque_type = type_clang_forward_type;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if (class_opaque_type)
                                {
                                    // If accessibility isn't set to anything valid, assume public for 
                                    // now...
                                    if (accessibility == eAccessNone)
                                        accessibility = eAccessPublic;

                                    clang::ObjCMethodDecl *objc_method_decl;
                                    objc_method_decl = ast.AddMethodToObjCObjectType (class_opaque_type, 
                                                                                      type_name_cstr,
                                                                                      clang_type,
                                                                                      accessibility);
                                    type_handled = objc_method_decl != NULL;
                                }
                            }
                            else if (parent_die->Tag() == DW_TAG_class_type ||
                                     parent_die->Tag() == DW_TAG_structure_type)
                            {
                                // Look at the parent of this DIE and see if is is
                                // a class or struct and see if this is actually a
                                // C++ method
                                Type *class_type = ResolveType (dwarf_cu, parent_die);
                                if (class_type)
                                {
                                    clang_type_t class_opaque_type = class_type->GetClangForwardType();
                                    if (ClangASTContext::IsCXXClassType (class_opaque_type))
                                    {
                                        // Neither GCC 4.2 nor clang++ currently set a valid accessibility
                                        // in the DWARF for C++ methods... Default to public for now...
                                        if (accessibility == eAccessNone)
                                            accessibility = eAccessPublic;
                                        
                                        if (!is_static && !die->HasChildren())
                                        {
                                            // We have a C++ member function with no children (this pointer!)
                                            // and clang will get mad if we try and make a function that isn't
                                            // well formed in the DWARF, so we will just skip it...
                                            type_handled = true;
                                        }
                                        else
                                        {
                                            clang::CXXMethodDecl *cxx_method_decl;
                                            cxx_method_decl = ast.AddMethodToCXXRecordType (class_opaque_type, 
                                                                                            type_name_cstr,
                                                                                            clang_type,
                                                                                            accessibility,
                                                                                            is_virtual,
                                                                                            is_static,
                                                                                            is_inline,
                                                                                            is_explicit);
                                            type_handled = cxx_method_decl != NULL;
                                        }
                                    }
                                }
                            }
                        }
                            
                        if (!type_handled)
                        {
                            // We just have a function that isn't part of a class
                            clang::FunctionDecl *function_decl = ast.CreateFunctionDeclaration (type_name_cstr, 
                                                                                                clang_type, 
                                                                                                storage, 
                                                                                                is_inline);
                            
                            // Add the decl to our DIE to decl context map
                            assert (function_decl);
                            m_die_to_decl_ctx[die] = function_decl;
                            if (!function_param_decls.empty())
                                ast.SetFunctionParameters (function_decl, 
                                                           &function_param_decls.front(), 
                                                           function_param_decls.size());
                        }
                    }
                    type_sp.reset( new Type (die->GetOffset(), 
                                             this, 
                                             type_name_const_str, 
                                             0, 
                                             NULL, 
                                             LLDB_INVALID_UID, 
                                             Type::eEncodingIsUID, 
                                             &decl, 
                                             clang_type, 
                                             Type::eResolveStateFull));                    
                    assert(type_sp.get());
                }
                break;

            case DW_TAG_array_type:
                {
                    // Set a bit that lets us know that we are currently parsing this
                    m_die_to_type[die] = DIE_IS_BEING_PARSED;

                    lldb::user_id_t type_die_offset = DW_INVALID_OFFSET;
                    int64_t first_index = 0;
                    uint32_t byte_stride = 0;
                    uint32_t bit_stride = 0;
                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);

                    if (num_attributes > 0)
                    {
                        uint32_t i;
                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                                case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                                case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                                case DW_AT_name:
                                    type_name_cstr = form_value.AsCString(&get_debug_str_data());
                                    type_name_const_str.SetCString(type_name_cstr);
                                    break;

                                case DW_AT_type:            type_die_offset = form_value.Reference(dwarf_cu); break;
                                case DW_AT_byte_size:       byte_size = form_value.Unsigned(); break;
                                case DW_AT_byte_stride:     byte_stride = form_value.Unsigned(); break;
                                case DW_AT_bit_stride:      bit_stride = form_value.Unsigned(); break;
                                case DW_AT_accessibility:   accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
                                case DW_AT_declaration:     is_forward_declaration = form_value.Unsigned() != 0; break;
                                case DW_AT_allocated:
                                case DW_AT_associated:
                                case DW_AT_data_location:
                                case DW_AT_description:
                                case DW_AT_ordering:
                                case DW_AT_start_scope:
                                case DW_AT_visibility:
                                case DW_AT_specification:
                                case DW_AT_abstract_origin:
                                case DW_AT_sibling:
                                    break;
                                }
                            }
                        }

                        DEBUG_PRINTF ("0x%8.8x: %s (\"%s\")\n", die->GetOffset(), DW_TAG_value_to_name(tag), type_name_cstr);

                        Type *element_type = ResolveTypeUID(type_die_offset);

                        if (element_type)
                        {
                            std::vector<uint64_t> element_orders;
                            ParseChildArrayInfo(sc, dwarf_cu, die, first_index, element_orders, byte_stride, bit_stride);
                            // We have an array that claims to have no members, lets give it at least one member...
                            if (element_orders.empty())
                                element_orders.push_back (1);
                            if (byte_stride == 0 && bit_stride == 0)
                                byte_stride = element_type->GetByteSize();
                            clang_type_t array_element_type = element_type->GetClangType();
                            uint64_t array_element_bit_stride = byte_stride * 8 + bit_stride;
                            uint64_t num_elements = 0;
                            std::vector<uint64_t>::const_reverse_iterator pos;
                            std::vector<uint64_t>::const_reverse_iterator end = element_orders.rend();
                            for (pos = element_orders.rbegin(); pos != end; ++pos)
                            {
                                num_elements = *pos;
                                clang_type = ast.CreateArrayType (array_element_type, 
                                                                  num_elements, 
                                                                  num_elements * array_element_bit_stride);
                                array_element_type = clang_type;
                                array_element_bit_stride = array_element_bit_stride * num_elements;
                            }
                            ConstString empty_name;
                            type_sp.reset( new Type (die->GetOffset(), 
                                                     this, 
                                                     empty_name, 
                                                     array_element_bit_stride / 8, 
                                                     NULL, 
                                                     type_die_offset, 
                                                     Type::eEncodingIsUID, 
                                                     &decl, 
                                                     clang_type, 
                                                     Type::eResolveStateFull));
                            type_sp->SetEncodingType (element_type);
                        }
                    }
                }
                break;

            case DW_TAG_ptr_to_member_type:
                {
                    dw_offset_t type_die_offset = DW_INVALID_OFFSET;
                    dw_offset_t containing_type_die_offset = DW_INVALID_OFFSET;

                    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
                    
                    if (num_attributes > 0) {
                        uint32_t i;
                        for (i=0; i<num_attributes; ++i)
                        {
                            attr = attributes.AttributeAtIndex(i);
                            DWARFFormValue form_value;
                            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
                            {
                                switch (attr)
                                {
                                    case DW_AT_type:
                                        type_die_offset = form_value.Reference(dwarf_cu); break;
                                    case DW_AT_containing_type:
                                        containing_type_die_offset = form_value.Reference(dwarf_cu); break;
                                }
                            }
                        }
                        
                        Type *pointee_type = ResolveTypeUID(type_die_offset);
                        Type *class_type = ResolveTypeUID(containing_type_die_offset);
                        
                        clang_type_t pointee_clang_type = pointee_type->GetClangForwardType();
                        clang_type_t class_clang_type = class_type->GetClangLayoutType();

                        clang_type = ast.CreateMemberPointerType(pointee_clang_type, 
                                                                 class_clang_type);

                        byte_size = ClangASTType::GetClangTypeBitWidth (ast.getASTContext(), 
                                                                       clang_type) / 8;

                        type_sp.reset( new Type (die->GetOffset(), 
                                                 this, 
                                                 type_name_const_str, 
                                                 byte_size, 
                                                 NULL, 
                                                 LLDB_INVALID_UID, 
                                                 Type::eEncodingIsUID, 
                                                 NULL, 
                                                 clang_type, 
                                                 Type::eResolveStateForward));
                    }
                                            
                    break;
                }
            default:
                assert(false && "Unhandled type tag!");
                break;
            }

            if (type_sp.get())
            {
                const DWARFDebugInfoEntry *sc_parent_die = GetParentSymbolContextDIE(die);
                dw_tag_t sc_parent_tag = sc_parent_die ? sc_parent_die->Tag() : 0;

                SymbolContextScope * symbol_context_scope = NULL;
                if (sc_parent_tag == DW_TAG_compile_unit)
                {
                    symbol_context_scope = sc.comp_unit;
                }
                else if (sc.function != NULL)
                {
                    symbol_context_scope = sc.function->GetBlock(true).FindBlockByID(sc_parent_die->GetOffset());
                    if (symbol_context_scope == NULL)
                        symbol_context_scope = sc.function;
                }

                if (symbol_context_scope != NULL)
                {
                    type_sp->SetSymbolContextScope(symbol_context_scope);
                }

                // We are ready to put this type into the uniqued list up at the module level
                type_list->Insert (type_sp);

                m_die_to_type[die] = type_sp.get();
            }
        }
        else if (type_ptr != DIE_IS_BEING_PARSED)
        {
            type_sp = type_list->FindType(type_ptr->GetID());
        }
    }
    return type_sp;
}

size_t
SymbolFileDWARF::ParseTypes
(
    const SymbolContext& sc, 
    DWARFCompileUnit* dwarf_cu, 
    const DWARFDebugInfoEntry *die, 
    bool parse_siblings, 
    bool parse_children
)
{
    size_t types_added = 0;
    while (die != NULL)
    {
        bool type_is_new = false;
        if (ParseType(sc, dwarf_cu, die, &type_is_new).get())
        {
            if (type_is_new)
                ++types_added;
        }

        if (parse_children && die->HasChildren())
        {
            if (die->Tag() == DW_TAG_subprogram)
            {
                SymbolContext child_sc(sc);
                child_sc.function = sc.comp_unit->FindFunctionByUID(die->GetOffset()).get();
                types_added += ParseTypes(child_sc, dwarf_cu, die->GetFirstChild(), true, true);
            }
            else
                types_added += ParseTypes(sc, dwarf_cu, die->GetFirstChild(), true, true);
        }

        if (parse_siblings)
            die = die->GetSibling();
        else
            die = NULL;
    }
    return types_added;
}


size_t
SymbolFileDWARF::ParseFunctionBlocks (const SymbolContext &sc)
{
    assert(sc.comp_unit && sc.function);
    size_t functions_added = 0;
    DWARFCompileUnit* dwarf_cu = GetDWARFCompileUnitForUID(sc.comp_unit->GetID());
    if (dwarf_cu)
    {
        dw_offset_t function_die_offset = sc.function->GetID();
        const DWARFDebugInfoEntry *function_die = dwarf_cu->GetDIEPtr(function_die_offset);
        if (function_die)
        {
            ParseFunctionBlocks(sc, &sc.function->GetBlock (false), dwarf_cu, function_die, LLDB_INVALID_ADDRESS, false, true);
        }
    }

    return functions_added;
}


size_t
SymbolFileDWARF::ParseTypes (const SymbolContext &sc)
{
    // At least a compile unit must be valid
    assert(sc.comp_unit);
    size_t types_added = 0;
    DWARFCompileUnit* dwarf_cu = GetDWARFCompileUnitForUID(sc.comp_unit->GetID());
    if (dwarf_cu)
    {
        if (sc.function)
        {
            dw_offset_t function_die_offset = sc.function->GetID();
            const DWARFDebugInfoEntry *func_die = dwarf_cu->GetDIEPtr(function_die_offset);
            if (func_die && func_die->HasChildren())
            {
                types_added = ParseTypes(sc, dwarf_cu, func_die->GetFirstChild(), true, true);
            }
        }
        else
        {
            const DWARFDebugInfoEntry *dwarf_cu_die = dwarf_cu->DIE();
            if (dwarf_cu_die && dwarf_cu_die->HasChildren())
            {
                types_added = ParseTypes(sc, dwarf_cu, dwarf_cu_die->GetFirstChild(), true, true);
            }
        }
    }

    return types_added;
}

size_t
SymbolFileDWARF::ParseVariablesForContext (const SymbolContext& sc)
{
    if (sc.comp_unit != NULL)
    {
        DWARFDebugInfo* info = DebugInfo();
        if (info == NULL)
            return 0;
        
        uint32_t cu_idx = UINT32_MAX;
        DWARFCompileUnit* dwarf_cu = info->GetCompileUnit(sc.comp_unit->GetID(), &cu_idx).get();

        if (dwarf_cu == NULL)
            return 0;

        if (sc.function)
        {
            const DWARFDebugInfoEntry *function_die = dwarf_cu->GetDIEPtr(sc.function->GetID());
            
            dw_addr_t func_lo_pc = function_die->GetAttributeValueAsUnsigned (this, dwarf_cu, DW_AT_low_pc, DW_INVALID_ADDRESS);
            assert (func_lo_pc != DW_INVALID_ADDRESS);

            return ParseVariables(sc, dwarf_cu, func_lo_pc, function_die->GetFirstChild(), true, true);
        }
        else if (sc.comp_unit)
        {
            uint32_t vars_added = 0;
            VariableListSP variables (sc.comp_unit->GetVariableList(false));
            
            if (variables.get() == NULL)
            {
                variables.reset(new VariableList());
                sc.comp_unit->SetVariableList(variables);

                // Index if we already haven't to make sure the compile units
                // get indexed and make their global DIE index list
                if (!m_indexed)
                    Index ();

                std::vector<NameToDIE::Info> global_die_info_array;
                const size_t num_globals = m_global_index.FindAllEntriesForCompileUnitWithIndex (cu_idx, global_die_info_array);
                for (size_t idx=0; idx<num_globals; ++idx)
                {
                    VariableSP var_sp (ParseVariableDIE(sc, dwarf_cu, dwarf_cu->GetDIEAtIndexUnchecked(global_die_info_array[idx].die_idx), LLDB_INVALID_ADDRESS));
                    if (var_sp)
                    {
                        variables->AddVariableIfUnique (var_sp);
                        ++vars_added;
                    }
                }
            }
            return vars_added;
        }
    }
    return 0;
}


VariableSP
SymbolFileDWARF::ParseVariableDIE
(
    const SymbolContext& sc,
    DWARFCompileUnit* dwarf_cu,
    const DWARFDebugInfoEntry *die,
    const lldb::addr_t func_low_pc
)
{

    VariableSP var_sp (m_die_to_variable_sp[die]);
    if (var_sp)
        return var_sp;  // Already been parsed!
    
    const dw_tag_t tag = die->Tag();
    DWARFDebugInfoEntry::Attributes attributes;
    const size_t num_attributes = die->GetAttributes(this, dwarf_cu, NULL, attributes);
    if (num_attributes > 0)
    {
        const char *name = NULL;
        const char *mangled = NULL;
        Declaration decl;
        uint32_t i;
        Type *var_type = NULL;
        DWARFExpression location;
        bool is_external = false;
        bool is_artificial = false;
        AccessType accessibility = eAccessNone;

        for (i=0; i<num_attributes; ++i)
        {
            dw_attr_t attr = attributes.AttributeAtIndex(i);
            DWARFFormValue form_value;
            if (attributes.ExtractFormValueAtIndex(this, i, form_value))
            {
                switch (attr)
                {
                case DW_AT_decl_file:   decl.SetFile(sc.comp_unit->GetSupportFiles().GetFileSpecAtIndex(form_value.Unsigned())); break;
                case DW_AT_decl_line:   decl.SetLine(form_value.Unsigned()); break;
                case DW_AT_decl_column: decl.SetColumn(form_value.Unsigned()); break;
                case DW_AT_name:        name = form_value.AsCString(&get_debug_str_data()); break;
                case DW_AT_MIPS_linkage_name: mangled = form_value.AsCString(&get_debug_str_data()); break;
                case DW_AT_type:        var_type = ResolveTypeUID(form_value.Reference(dwarf_cu)); break;
                case DW_AT_external:    is_external = form_value.Unsigned() != 0; break;
                case DW_AT_location:
                    {
                        if (form_value.BlockData())
                        {
                            const DataExtractor& debug_info_data = get_debug_info_data();

                            uint32_t block_offset = form_value.BlockData() - debug_info_data.GetDataStart();
                            uint32_t block_length = form_value.Unsigned();
                            location.SetOpcodeData(get_debug_info_data(), block_offset, block_length);
                        }
                        else
                        {
                            const DataExtractor&    debug_loc_data = get_debug_loc_data();
                            const dw_offset_t debug_loc_offset = form_value.Unsigned();

                            size_t loc_list_length = DWARFLocationList::Size(debug_loc_data, debug_loc_offset);
                            if (loc_list_length > 0)
                            {
                                location.SetOpcodeData(debug_loc_data, debug_loc_offset, loc_list_length);
                                assert (func_low_pc != LLDB_INVALID_ADDRESS);
                                location.SetLocationListSlide (func_low_pc - dwarf_cu->GetBaseAddress());
                            }
                        }
                    }
                    break;

                case DW_AT_artificial:      is_artificial = form_value.Unsigned() != 0; break;
                case DW_AT_accessibility:   accessibility = DW_ACCESS_to_AccessType(form_value.Unsigned()); break;
                case DW_AT_const_value:
                case DW_AT_declaration:
                case DW_AT_description:
                case DW_AT_endianity:
                case DW_AT_segment:
                case DW_AT_start_scope:
                case DW_AT_visibility:
                default:
                case DW_AT_abstract_origin:
                case DW_AT_sibling:
                case DW_AT_specification:
                    break;
                }
            }
        }

        if (location.IsValid())
        {
            assert(var_type != DIE_IS_BEING_PARSED);

            ValueType scope = eValueTypeInvalid;

            const DWARFDebugInfoEntry *sc_parent_die = GetParentSymbolContextDIE(die);
            dw_tag_t parent_tag = sc_parent_die ? sc_parent_die->Tag() : 0;

            if (tag == DW_TAG_formal_parameter)
                scope = eValueTypeVariableArgument;
            else if (is_external || parent_tag == DW_TAG_compile_unit)
                scope = eValueTypeVariableGlobal;
            else
                scope = eValueTypeVariableLocal;

            SymbolContextScope * symbol_context_scope = NULL;
            if (parent_tag == DW_TAG_compile_unit)
            {
                symbol_context_scope = sc.comp_unit;
            }
            else if (sc.function != NULL)
            {
                symbol_context_scope = sc.function->GetBlock(true).FindBlockByID(sc_parent_die->GetOffset());
                if (symbol_context_scope == NULL)
                    symbol_context_scope = sc.function;
            }

            assert(symbol_context_scope != NULL);
            var_sp.reset (new Variable(die->GetOffset(), 
                                       name, 
                                       mangled,
                                       var_type, 
                                       scope, 
                                       symbol_context_scope, 
                                       &decl, 
                                       location, 
                                       is_external, 
                                       is_artificial));
            
        }
    }
    // Cache var_sp even if NULL (the variable was just a specification or
    // was missing vital information to be able to be displayed in the debugger
    // (missing location due to optimization, etc)) so we don't re-parse
    // this DIE over and over later...
    m_die_to_variable_sp[die] = var_sp;
    return var_sp;
}

size_t
SymbolFileDWARF::ParseVariables
(
    const SymbolContext& sc,
    DWARFCompileUnit* dwarf_cu,
    const lldb::addr_t func_low_pc,
    const DWARFDebugInfoEntry *orig_die,
    bool parse_siblings,
    bool parse_children,
    VariableList* cc_variable_list
)
{
    if (orig_die == NULL)
        return 0;

    size_t vars_added = 0;
    const DWARFDebugInfoEntry *die = orig_die;
    const DWARFDebugInfoEntry *sc_parent_die = GetParentSymbolContextDIE(orig_die);
    dw_tag_t parent_tag = sc_parent_die ? sc_parent_die->Tag() : 0;
    VariableListSP variables;
    switch (parent_tag)
    {
    case DW_TAG_compile_unit:
        if (sc.comp_unit != NULL)
        {
            variables = sc.comp_unit->GetVariableList(false);
            if (variables.get() == NULL)
            {
                variables.reset(new VariableList());
                sc.comp_unit->SetVariableList(variables);
            }
        }
        else
        {
            assert(!"Parent DIE was a compile unit, yet we don't have a valid compile unit in the symbol context...");
            vars_added = 0;
        }
        break;

    case DW_TAG_subprogram:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_lexical_block:
        if (sc.function != NULL)
        {
            // Check to see if we already have parsed the variables for the given scope
            
            Block *block = sc.function->GetBlock(true).FindBlockByID(sc_parent_die->GetOffset());
            assert (block != NULL);
            variables = block->GetVariableList(false, false);
            if (variables.get() == NULL)
            {
                variables.reset(new VariableList());
                block->SetVariableList(variables);
            }
        }
        else
        {
            assert(!"Parent DIE was a function or block, yet we don't have a function in the symbol context...");
            vars_added = 0;
        }
        break;

    default:
        assert(!"Didn't find appropriate parent DIE for variable list...");
        break;
    }

    // We need to have a variable list at this point that we can add variables to
    assert(variables.get());

    while (die != NULL)
    {
        dw_tag_t tag = die->Tag();

        // Check to see if we have already parsed this variable or constant?
        if (m_die_to_variable_sp[die])
        {
            if (cc_variable_list)
                cc_variable_list->AddVariableIfUnique (m_die_to_variable_sp[die]);
        }
        else
        {
            // We haven't already parsed it, lets do that now.
            if ((tag == DW_TAG_variable) ||
                (tag == DW_TAG_constant) ||
                (tag == DW_TAG_formal_parameter && sc.function))
            {
                VariableSP var_sp (ParseVariableDIE(sc, dwarf_cu, die, func_low_pc));
                if (var_sp)
                {
                    variables->AddVariableIfUnique (var_sp);
                    if (cc_variable_list)
                        cc_variable_list->AddVariableIfUnique (var_sp);
                    ++vars_added;
                }
            }
        }

        bool skip_children = (sc.function == NULL && tag == DW_TAG_subprogram);

        if (!skip_children && parse_children && die->HasChildren())
        {
            vars_added += ParseVariables(sc, dwarf_cu, func_low_pc, die->GetFirstChild(), true, true, cc_variable_list);
        }

        if (parse_siblings)
            die = die->GetSibling();
        else
            die = NULL;
    }

    return vars_added;
}

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
const char *
SymbolFileDWARF::GetPluginName()
{
    return "SymbolFileDWARF";
}

const char *
SymbolFileDWARF::GetShortPluginName()
{
    return GetPluginNameStatic();
}

uint32_t
SymbolFileDWARF::GetPluginVersion()
{
    return 1;
}

void
SymbolFileDWARF::GetPluginCommandHelp (const char *command, Stream *strm)
{
}

Error
SymbolFileDWARF::ExecutePluginCommand (Args &command, Stream *strm)
{
    Error error;
    error.SetErrorString("No plug-in command are currently supported.");
    return error;
}

Log *
SymbolFileDWARF::EnablePluginLogging (Stream *strm, Args &command)
{
    return NULL;
}

void
SymbolFileDWARF::CompleteTagDecl (void *baton, clang::TagDecl *decl)
{
    SymbolFileDWARF *symbol_file_dwarf = (SymbolFileDWARF *)baton;
    clang_type_t clang_type = symbol_file_dwarf->GetClangASTContext().GetTypeForDecl (decl);
    if (clang_type)
        symbol_file_dwarf->ResolveClangOpaqueTypeDefinition (clang_type);
}

void
SymbolFileDWARF::CompleteObjCInterfaceDecl (void *baton, clang::ObjCInterfaceDecl *decl)
{
    SymbolFileDWARF *symbol_file_dwarf = (SymbolFileDWARF *)baton;
    clang_type_t clang_type = symbol_file_dwarf->GetClangASTContext().GetTypeForDecl (decl);
    if (clang_type)
        symbol_file_dwarf->ResolveClangOpaqueTypeDefinition (clang_type);
}

