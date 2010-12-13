//===-- BreakpointSiteList.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointSiteList.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Stream.h"
#include <algorithm>

using namespace lldb;
using namespace lldb_private;

BreakpointSiteList::BreakpointSiteList() :
    m_bp_site_list()
{
}

BreakpointSiteList::~BreakpointSiteList()
{
}

// Add breakpoint site to the list.  However, if the element already exists in the
// list, then we don't add it, and return LLDB_INVALID_BREAK_ID.

lldb::break_id_t
BreakpointSiteList::Add(const BreakpointSiteSP &bp)
{
    lldb::addr_t bp_site_load_addr = bp->GetLoadAddress();
    collection::iterator iter = m_bp_site_list.find (bp_site_load_addr);

    if (iter == m_bp_site_list.end())
    {
        m_bp_site_list.insert (iter, collection::value_type (bp_site_load_addr, bp));
        return bp->GetID();
    }
    else
    {
        return LLDB_INVALID_BREAK_ID;
    }
}

bool
BreakpointSiteList::ShouldStop (StoppointCallbackContext *context, lldb::break_id_t site_id)
{
    BreakpointSiteSP site_sp (FindByID (site_id));
    if (site_sp)
    {
        // Let the BreakpointSite decide if it should stop here (could not have
        // reached it's target hit count yet, or it could have a callback
        // that decided it shouldn't stop (shared library loads/unloads).
        return site_sp->ShouldStop (context);
    }
    // We should stop here since this BreakpointSite isn't valid anymore or it
    // doesn't exist.
    return true;
}
lldb::break_id_t
BreakpointSiteList::FindIDByAddress (lldb::addr_t addr)
{
    BreakpointSiteSP bp = FindByAddress (addr);
    if (bp)
    {
        //DBLogIf(PD_LOG_BREAKPOINTS, "BreakpointSiteList::%s ( addr = 0x%8.8llx ) => %u", __FUNCTION__, (uint64_t)addr, bp->GetID());
        return bp.get()->GetID();
    }
    //DBLogIf(PD_LOG_BREAKPOINTS, "BreakpointSiteList::%s ( addr = 0x%8.8llx ) => NONE", __FUNCTION__, (uint64_t)addr);
    return LLDB_INVALID_BREAK_ID;
}

bool
BreakpointSiteList::Remove (lldb::break_id_t break_id)
{
    collection::iterator pos = GetIDIterator(break_id);    // Predicate
    if (pos != m_bp_site_list.end())
    {
        m_bp_site_list.erase(pos);
        return true;
    }
    return false;
}

bool
BreakpointSiteList::RemoveByAddress (lldb::addr_t address)
{
    collection::iterator pos =  m_bp_site_list.find(address);
    if (pos != m_bp_site_list.end())
    {
        m_bp_site_list.erase(pos);
        return true;
    }
    return false;
}

class BreakpointSiteIDMatches
{
public:
    BreakpointSiteIDMatches (lldb::break_id_t break_id) :
        m_break_id(break_id)
    {
    }

    bool operator() (std::pair <lldb::addr_t, BreakpointSiteSP> val_pair) const
    {
        return m_break_id == val_pair.second.get()->GetID();
    }

private:
   const lldb::break_id_t m_break_id;
};

BreakpointSiteList::collection::iterator
BreakpointSiteList::GetIDIterator (lldb::break_id_t break_id)
{
    return std::find_if(m_bp_site_list.begin(), m_bp_site_list.end(),   // Search full range
                        BreakpointSiteIDMatches(break_id));             // Predicate
}

BreakpointSiteList::collection::const_iterator
BreakpointSiteList::GetIDConstIterator (lldb::break_id_t break_id) const
{
    return std::find_if(m_bp_site_list.begin(), m_bp_site_list.end(),   // Search full range
                        BreakpointSiteIDMatches(break_id));             // Predicate
}

BreakpointSiteSP
BreakpointSiteList::FindByID (lldb::break_id_t break_id)
{
    BreakpointSiteSP stop_sp;
    collection::iterator pos = GetIDIterator(break_id);
    if (pos != m_bp_site_list.end())
        stop_sp = pos->second;

    return stop_sp;
}

const BreakpointSiteSP
BreakpointSiteList::FindByID (lldb::break_id_t break_id) const
{
    BreakpointSiteSP stop_sp;
    collection::const_iterator pos = GetIDConstIterator(break_id);
    if (pos != m_bp_site_list.end())
        stop_sp = pos->second;

    return stop_sp;
}

BreakpointSiteSP
BreakpointSiteList::FindByAddress (lldb::addr_t addr)
{
    BreakpointSiteSP found_sp;

    collection::iterator iter =  m_bp_site_list.find(addr);
    if (iter != m_bp_site_list.end())
        found_sp = iter->second;
    return found_sp;
}

void
BreakpointSiteList::Dump (Stream *s) const
{
    s->Printf("%.*p: ", (int)sizeof(void*) * 2, this);
    s->Indent();
    s->Printf("BreakpointSiteList with %u BreakpointSites:\n", (uint32_t)m_bp_site_list.size());
    s->IndentMore();
    collection::const_iterator pos;
    collection::const_iterator end = m_bp_site_list.end();
    for (pos = m_bp_site_list.begin(); pos != end; ++pos)
        pos->second.get()->Dump(s);
    s->IndentLess();
}


BreakpointSiteSP
BreakpointSiteList::GetByIndex (uint32_t i)
{
    BreakpointSiteSP stop_sp;
    collection::iterator end = m_bp_site_list.end();
    collection::iterator pos;
    uint32_t curr_i = 0;
    for (pos = m_bp_site_list.begin(), curr_i = 0; pos != end; ++pos, ++curr_i)
    {
        if (curr_i == i)
            stop_sp = pos->second;
    }
    return stop_sp;
}

const BreakpointSiteSP
BreakpointSiteList::GetByIndex (uint32_t i) const
{
    BreakpointSiteSP stop_sp;
    collection::const_iterator end = m_bp_site_list.end();
    collection::const_iterator pos;
    uint32_t curr_i = 0;
    for (pos = m_bp_site_list.begin(), curr_i = 0; pos != end; ++pos, ++curr_i)
    {
        if (curr_i == i)
            stop_sp = pos->second;
    }
    return stop_sp;
}

void
BreakpointSiteList::SetEnabledForAll (const bool enabled, const lldb::break_id_t except_id)
{
    collection::iterator end = m_bp_site_list.end();
    collection::iterator pos;
    for (pos = m_bp_site_list.begin(); pos != end; ++pos)
    {
        if (except_id != LLDB_INVALID_BREAK_ID && except_id != pos->second->GetID())
            pos->second->SetEnabled (enabled);
        else
            pos->second->SetEnabled (!enabled);
    }
}

const BreakpointSiteList::collection *
BreakpointSiteList::GetMap ()
{
    return &m_bp_site_list;
}
