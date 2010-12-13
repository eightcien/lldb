//===-- SBCompileUnit.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBStream.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Core/Log.h"

using namespace lldb;
using namespace lldb_private;


SBCompileUnit::SBCompileUnit () :
    m_opaque_ptr (NULL)
{
}

SBCompileUnit::SBCompileUnit (lldb_private::CompileUnit *lldb_object_ptr) :
    m_opaque_ptr (lldb_object_ptr)
{
}

SBCompileUnit::SBCompileUnit(const SBCompileUnit &rhs) :
    m_opaque_ptr (rhs.m_opaque_ptr)
{
}

const SBCompileUnit &
SBCompileUnit::operator = (const SBCompileUnit &rhs)
{
    m_opaque_ptr = rhs.m_opaque_ptr;
    return *this;
}


SBCompileUnit::~SBCompileUnit ()
{
    m_opaque_ptr = NULL;
}

SBFileSpec
SBCompileUnit::GetFileSpec () const
{
    SBFileSpec file_spec;
    if (m_opaque_ptr)
        file_spec.SetFileSpec(*m_opaque_ptr);
    return file_spec;
}

uint32_t
SBCompileUnit::GetNumLineEntries () const
{
    if (m_opaque_ptr)
    {
        LineTable *line_table = m_opaque_ptr->GetLineTable ();
        if (line_table)
            return line_table->GetSize();
    }
    return 0;
}

SBLineEntry
SBCompileUnit::GetLineEntryAtIndex (uint32_t idx) const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBLineEntry sb_line_entry;
    if (m_opaque_ptr)
    {
        LineTable *line_table = m_opaque_ptr->GetLineTable ();
        if (line_table)
        {
            LineEntry line_entry;
            if (line_table->GetLineEntryAtIndex(idx, line_entry))
                sb_line_entry.SetLineEntry(line_entry);
        }
    }
    
    if (log)
    {
        SBStream sstr;
        sb_line_entry.GetDescription (sstr);
        log->Printf ("SBCompileUnit(%p)::GetLineEntryAtIndex (idx=%u) => SBLineEntry(%p): '%s'", 
                     m_opaque_ptr, idx, sb_line_entry.get(), sstr.GetData());
    }

    return sb_line_entry;
}

uint32_t
SBCompileUnit::FindLineEntryIndex (uint32_t start_idx, uint32_t line, SBFileSpec *inline_file_spec) const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    uint32_t index = UINT32_MAX;
    if (m_opaque_ptr)
    {
        FileSpec file_spec;
        if (inline_file_spec && inline_file_spec->IsValid())
            file_spec = inline_file_spec->ref();
        else
            file_spec = *m_opaque_ptr;

        
        index = m_opaque_ptr->FindLineEntry (start_idx,
                                             line,
                                             inline_file_spec ? inline_file_spec->get() : NULL,
                                             NULL);
    }

    if (log)
    {
        SBStream sstr;
        if (index == UINT32_MAX)
        {
            log->Printf ("SBCompileUnit(%p)::FindLineEntryIndex (start_idx=%u, line=%u, SBFileSpec(%p)) => NOT FOUND", 
                         m_opaque_ptr, start_idx, line, inline_file_spec ? inline_file_spec->get() : NULL);
        }
        else
        {
            log->Printf ("SBCompileUnit(%p)::FindLineEntryIndex (start_idx=%u, line=%u, SBFileSpec(%p)) => %u", 
                         m_opaque_ptr, start_idx, line, inline_file_spec ? inline_file_spec->get() : NULL, index);
        }
    }

    return index;
}

bool
SBCompileUnit::IsValid () const
{
    return m_opaque_ptr != NULL;
}

bool
SBCompileUnit::operator == (const SBCompileUnit &rhs) const
{
    return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool
SBCompileUnit::operator != (const SBCompileUnit &rhs) const
{
    return m_opaque_ptr != rhs.m_opaque_ptr;
}

const lldb_private::CompileUnit *
SBCompileUnit::operator->() const
{
    return m_opaque_ptr;
}

const lldb_private::CompileUnit &
SBCompileUnit::operator*() const
{
    return *m_opaque_ptr;
}

const lldb_private::CompileUnit *
SBCompileUnit::get () const
{
    return m_opaque_ptr;
}
    
bool
SBCompileUnit::GetDescription (SBStream &description)
{
    if (m_opaque_ptr)
    {
        description.ref();
        m_opaque_ptr->Dump (description.get(), false);
    }
    else
        description.Printf ("No Value");
    
    return true;
}
