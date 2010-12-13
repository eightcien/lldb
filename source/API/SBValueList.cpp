//===-- SBValueList.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "lldb/API/SBValueList.h"
#include "lldb/API/SBValue.h"
#include "lldb/API/SBStream.h"


#include "lldb/Core/Log.h"
#include "lldb/Core/ValueObjectList.h"

using namespace lldb;
using namespace lldb_private;

SBValueList::SBValueList () :
    m_opaque_ap ()
{
}

SBValueList::SBValueList (const SBValueList &rhs) :
    m_opaque_ap ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (rhs.IsValid())
        m_opaque_ap.reset (new lldb_private::ValueObjectList (*rhs));

    if (log)
    {
        log->Printf ("SBValueList::SBValueList (rhs.ap=%p) => this.ap = %p",
                     (rhs.IsValid() ? rhs.m_opaque_ap.get() : NULL), m_opaque_ap.get());

        uint32_t num_vars = GetSize();
        for (uint32_t i = 0; i < num_vars; ++i)
        {
            SBValue value = GetValueAtIndex (i);
            SBStream sstr;
            value.GetDescription (sstr);
            log->Printf ("  %s", sstr.GetData());
        }
    }
}

SBValueList::SBValueList (const lldb_private::ValueObjectList *lldb_object_ptr) :
    m_opaque_ap ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (lldb_object_ptr)
        m_opaque_ap.reset (new lldb_private::ValueObjectList (*lldb_object_ptr));

    if (log)
    {
        log->Printf ("SBValueList::SBValueList (lldb_object_ptr=%p) => this.ap = %p", lldb_object_ptr, 
                     m_opaque_ap.get());
                     

        uint32_t num_vars = GetSize();
        for (uint32_t i = 0; i < num_vars; ++i)
        {
            SBValue value = GetValueAtIndex (i);
            SBStream sstr;
            value.GetDescription (sstr);
            log->Printf ("  %s", sstr.GetData());
        }
    }
}

SBValueList::~SBValueList ()
{
}

bool
SBValueList::IsValid () const
{
    return (m_opaque_ap.get() != NULL);
}

const SBValueList &
SBValueList::operator = (const SBValueList &rhs)
{
    if (this != &rhs)
    {
        if (rhs.IsValid())
            m_opaque_ap.reset (new lldb_private::ValueObjectList (*rhs));
        else
            m_opaque_ap.reset ();
    }
    return *this;
}

lldb_private::ValueObjectList *
SBValueList::operator->()
{
    return m_opaque_ap.get();
}

lldb_private::ValueObjectList &
SBValueList::operator*()
{
    return *m_opaque_ap;
}

const lldb_private::ValueObjectList *
SBValueList::operator->() const
{
    return m_opaque_ap.get();
}

const lldb_private::ValueObjectList &
SBValueList::operator*() const
{
    return *m_opaque_ap;
}

void
SBValueList::Append (const SBValue &val_obj)
{
    if (val_obj.get())
    {
        CreateIfNeeded ();
        m_opaque_ap->Append (*val_obj);
    }
}

void
SBValueList::Append (lldb::ValueObjectSP& val_obj_sp)
{
    if (val_obj_sp)
    {
        CreateIfNeeded ();
        m_opaque_ap->Append (val_obj_sp);
    }
}


SBValue
SBValueList::GetValueAtIndex (uint32_t idx) const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    //if (log)
    //    log->Printf ("SBValueList::GetValueAtIndex (uint32_t idx) idx = %d", idx);

    SBValue sb_value;
    if (m_opaque_ap.get())
        *sb_value = m_opaque_ap->GetValueObjectAtIndex (idx);

    if (log)
    {
        SBStream sstr;
        sb_value.GetDescription (sstr);
        log->Printf ("SBValueList::GetValueAtIndex (this.ap=%p, idx=%d) => SBValue (this.sp = %p, '%s')", 
                     m_opaque_ap.get(), idx, sb_value.get(), sstr.GetData());
    }

    return sb_value;
}

uint32_t
SBValueList::GetSize () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    //if (log)
    //    log->Printf ("SBValueList::GetSize ()");

    uint32_t size = 0;
    if (m_opaque_ap.get())
        size = m_opaque_ap->GetSize();

    if (log)
        log->Printf ("SBValueList::GetSize (this.ap=%p) => %d", m_opaque_ap.get(), size);

    return size;
}

void
SBValueList::CreateIfNeeded ()
{
    if (m_opaque_ap.get() == NULL)
        m_opaque_ap.reset (new ValueObjectList());
}


SBValue
SBValueList::FindValueObjectByUID (lldb::user_id_t uid)
{
    SBValue sb_value;
    if ( m_opaque_ap.get())
        *sb_value = m_opaque_ap->FindValueObjectByUID (uid);
    return sb_value;
}

lldb_private::ValueObjectList *
SBValueList::get ()
{
    return m_opaque_ap.get();
}

