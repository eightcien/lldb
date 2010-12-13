//===-- UnwindPlan.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Core/ConstString.h"

using namespace lldb;
using namespace lldb_private;

bool
UnwindPlan::Row::RegisterLocation::operator == (const UnwindPlan::Row::RegisterLocation& rhs) const
{
    if (m_type != rhs.m_type)
        return false;
    if (m_type == atCFAPlusOffset || m_type == isCFAPlusOffset)
        return m_location.offset == rhs.m_location.offset;
    if (m_type == inOtherRegister)
        return m_location.reg_num == rhs.m_location.reg_num;
    if (m_type == atDWARFExpression || m_type == isDWARFExpression)
        if (m_location.expr.length == rhs.m_location.expr.length)
            return !memcmp (m_location.expr.opcodes, rhs.m_location.expr.opcodes, m_location.expr.length);
    return false;
}

// This function doesn't copy the dwarf expression bytes; they must remain in allocated
// memory for the lifespan of this UnwindPlan object.
void
UnwindPlan::Row::RegisterLocation::SetAtDWARFExpression (const uint8_t *opcodes, uint32_t len)
{
    m_type = atDWARFExpression;
    m_location.expr.opcodes = opcodes;
    m_location.expr.length = len;
}

// This function doesn't copy the dwarf expression bytes; they must remain in allocated
// memory for the lifespan of this UnwindPlan object.
void
UnwindPlan::Row::RegisterLocation::SetIsDWARFExpression (const uint8_t *opcodes, uint32_t len)
{
    m_type = isDWARFExpression;
    m_location.expr.opcodes = opcodes;
    m_location.expr.length = len;
}

void
UnwindPlan::Row::RegisterLocation::SetUnspecified () 
{
    m_type = unspecified;
}

void
UnwindPlan::Row::RegisterLocation::SetUndefined () 
{
    m_type = isUndefined;
}

void
UnwindPlan::Row::RegisterLocation::SetSame () 
{
    m_type = isSame;
}


void
UnwindPlan::Row::RegisterLocation::SetAtCFAPlusOffset (int32_t offset)
{
    m_type = atCFAPlusOffset;
    m_location.offset = offset;
}

void
UnwindPlan::Row::RegisterLocation::SetIsCFAPlusOffset (int32_t offset)
{
    m_type = isCFAPlusOffset;
    m_location.offset = offset;
}

void
UnwindPlan::Row::RegisterLocation::SetInRegister (uint32_t reg_num)
{
    m_type = inOtherRegister;
    m_location.reg_num = reg_num;
}

void
UnwindPlan::Row::RegisterLocation::Dump (Stream &s) const
{
    switch (m_type)
    {
        case unspecified: 
            s.Printf ("unspecified"); 
            break;
        case isUndefined: 
            s.Printf ("isUndefined"); 
            break;
        case isSame: 
            s.Printf ("isSame"); 
            break;
        case atCFAPlusOffset: 
            s.Printf ("atCFAPlusOffset %d", m_location.offset); 
            break;
        case isCFAPlusOffset: 
            s.Printf ("isCFAPlusOffset %d", m_location.offset); 
            break;
        case inOtherRegister: 
            s.Printf ("inOtherRegister %d", m_location.reg_num); 
            break;
        case atDWARFExpression: 
            s.Printf ("atDWARFExpression");
            break;
        case isDWARFExpression: 
            s.Printf ("isDWARFExpression");
            break;
    }
}

void
UnwindPlan::Row::Clear ()
{
    m_offset = 0;
    m_cfa_reg_num = 0;
    m_cfa_offset = 0;
    m_register_locations.clear();
}

void
UnwindPlan::Row::Dump (Stream& s, int register_kind, Thread* thread) const
{
    RegisterContext *rctx = NULL;
    const RegisterInfo *rinfo = NULL;
    int translated_regnum;
    if (thread && thread->GetRegisterContext())
    {
        rctx = thread->GetRegisterContext();
    }
    s.Printf ("offset %ld, CFA reg ", (long) GetOffset());
    if (rctx
        && (translated_regnum = rctx->ConvertRegisterKindToRegisterNumber (register_kind, GetCFARegister())) != -1
        && (rinfo = rctx->GetRegisterInfoAtIndex (translated_regnum)) != NULL
        && rinfo->name != NULL
        && rinfo->name[0] != '\0')
    {
        s.Printf ("%s, ", rinfo->name);
    }
    else
    {
        s.Printf ("%d, ", (int)(int)  GetCFARegister());
    }
    s.Printf ("CFA offset %d", (int) GetCFAOffset ());
    for (collection::const_iterator idx = m_register_locations.begin (); idx != m_register_locations.end (); ++idx)
    {
        s.Printf (" [");
        bool printed_name = false;
        if (thread && thread->GetRegisterContext())
        {
            rctx = thread->GetRegisterContext();
            translated_regnum = rctx->ConvertRegisterKindToRegisterNumber (register_kind, idx->first);
            rinfo = rctx->GetRegisterInfoAtIndex (translated_regnum);
            if (rinfo && rinfo->name)
            {
                s.Printf ("%s ", rinfo->name);
                printed_name = true;
            }
        }
        if (!printed_name)
        {
            s.Printf ("reg %d ", idx->first);
        }
        idx->second.Dump(s);
        s.Printf ("]");
    }
    s.Printf ("\n");
}

UnwindPlan::Row::Row() :
    m_offset(0),
    m_cfa_reg_num(0),
    m_cfa_offset(0),
    m_register_locations()
{
}

bool
UnwindPlan::Row::GetRegisterInfo (uint32_t reg_num, UnwindPlan::Row::RegisterLocation& register_location) const
{
    collection::const_iterator pos = m_register_locations.find(reg_num);
    if (pos != m_register_locations.end())
    {
        register_location = pos->second;
        return true;
    }
    return false;
}

void
UnwindPlan::Row::SetRegisterInfo (uint32_t reg_num, const UnwindPlan::Row::RegisterLocation register_location)
{
    m_register_locations[reg_num] = register_location;
}


void
UnwindPlan::AppendRow (const UnwindPlan::Row &row)
{
    if (m_row_list.empty() || m_row_list.back().GetOffset() != row.GetOffset())
        m_row_list.push_back(row);
    else
        m_row_list.back() = row;
}

const UnwindPlan::Row *
UnwindPlan::GetRowForFunctionOffset (int offset) const
{
    const UnwindPlan::Row *rowp = NULL;
    if (offset == -1 && m_row_list.size() > 0)
    {
        return &m_row_list[m_row_list.size() - 1];
    }
    for (int i = 0; i < m_row_list.size(); ++i)
    {
        if (m_row_list[i].GetOffset() <= offset)
        {
            rowp = &m_row_list[i];
        }
        else
        {
            break;
        }
    }
    return rowp;
}

bool
UnwindPlan::IsValidRowIndex (uint32_t idx) const
{
    return idx < m_row_list.size();
}

const UnwindPlan::Row&
UnwindPlan::GetRowAtIndex (uint32_t idx) const
{
    // You must call IsValidRowIndex(idx) first before calling this!!!
    return m_row_list[idx];
}

int
UnwindPlan::GetRowCount () const
{
    return m_row_list.size ();
}

void
UnwindPlan::SetRegisterKind (uint32_t rk)
{
    m_register_kind = rk;
}

uint32_t
UnwindPlan::GetRegisterKind (void) const
{
    return m_register_kind;
}

void
UnwindPlan::SetPlanValidAddressRange (const AddressRange& range)
{
   if (range.GetBaseAddress().IsValid() && range.GetByteSize() != 0)
   {
       m_plan_valid_address_range = range;
   }
// .GetBaseAddress() = addr;
//    m_plan_valid_address_range.SetByteSize (range.GetByteSize());
}

bool
UnwindPlan::PlanValidAtAddress (Address addr)
{
    if (!m_plan_valid_address_range.GetBaseAddress().IsValid() || m_plan_valid_address_range.GetByteSize() == 0)
        return true;

    if (!addr.IsValid())
        return true;

    if (m_plan_valid_address_range.ContainsFileAddress (addr))
        return true;

    return false;
}

void
UnwindPlan::Dump (Stream& s, Thread *thread) const
{
    if (!m_source_name.IsEmpty())
    {
        s.Printf ("This UnwindPlan originally sourced from %s\n", m_source_name.GetCString());
    }
    if (m_plan_valid_address_range.GetBaseAddress().IsValid() && m_plan_valid_address_range.GetByteSize() > 0)
    {
        s.Printf ("Address range of this UnwindPlan: ");
        m_plan_valid_address_range.Dump (&s, &thread->GetProcess().GetTarget(), Address::DumpStyleSectionNameOffset);
        s.Printf ("\n");
    }
    else
    {
        s.Printf ("No valid address range recorded for this UnwindPlan.\n");
    }
    s.Printf ("UnwindPlan register kind %d", m_register_kind);
    switch (m_register_kind)
    {
        case eRegisterKindGCC: s.Printf (" [eRegisterKindGCC]"); break;
        case eRegisterKindDWARF: s.Printf (" [eRegisterKindDWARF]"); break;
        case eRegisterKindGeneric: s.Printf (" [eRegisterKindGeneric]"); break;
        case eRegisterKindGDB: s.Printf (" [eRegisterKindGDB]"); break;
        case eRegisterKindLLDB: s.Printf (" [eRegisterKindLLDB]"); break;
        default: break;
    }
    s.Printf ("\n");
    for (int i = 0; IsValidRowIndex (i); i++)
    {
        s.Printf ("UnwindPlan row at index %d: ", i);
        m_row_list[i].Dump(s, m_register_kind, thread);
    }
}

void
UnwindPlan::SetSourceName (const char *source)
{
    m_source_name = ConstString (source);
}

ConstString
UnwindPlan::GetSourceName () const
{
    return m_source_name;
}
