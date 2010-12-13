//===-- BreakpointSiteList.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_BreakpointSiteList_h_
#define liblldb_BreakpointSiteList_h_

// C Includes
// C++ Includes
#include <map>
// Other libraries and framework includes
// Project includes
#include "lldb/Breakpoint/BreakpointSite.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class BreakpointSiteList BreakpointSiteList.h "lldb/Breakpoint/BreakpointSiteList.h"
/// @brief Class that manages lists of BreakpointSite shared pointers.
//----------------------------------------------------------------------
class BreakpointSiteList
{
// At present Process directly accesses the map of BreakpointSites so it can
// do quick lookups into the map (using GetMap).
// FIXME: Find a better interface for this.
friend class Process;

public:
    //------------------------------------------------------------------
    /// Default constructor makes an empty list.
    //------------------------------------------------------------------
    BreakpointSiteList();

    //------------------------------------------------------------------
    /// Destructor, currently does nothing.
    //------------------------------------------------------------------
    ~BreakpointSiteList();

    //------------------------------------------------------------------
    /// Add a BreakpointSite to the list.
    ///
    /// @param[in] bp_site_sp
    ///    A shared pointer to a breakpoint site being added to the list.
    ///
    /// @return
    ///    The ID of the BreakpointSite in the list.
    //------------------------------------------------------------------
    lldb::break_id_t
    Add (const lldb::BreakpointSiteSP& bp_site_sp);

    //------------------------------------------------------------------
    /// Standard Dump routine, doesn't do anything at present.
    /// @param[in] s
    ///     Stream into which to dump the description.
    //------------------------------------------------------------------
    void
    Dump (Stream *s) const;

    //------------------------------------------------------------------
    /// Returns a shared pointer to the breakpoint site at address
    /// \a addr.
    ///
    /// @param[in] addr
    ///     The address to look for.
    ///
    /// @result
    ///     A shared pointer to the breakpoint site. May contain a NULL
    ///     pointer if no breakpoint site exists with a matching address.
    //------------------------------------------------------------------
    lldb::BreakpointSiteSP
    FindByAddress (lldb::addr_t addr);

    //------------------------------------------------------------------
    /// Returns a shared pointer to the breakpoint site with id \a breakID.
    ///
    /// @param[in] breakID
    ///   The breakpoint site ID to seek for.
    ///
    /// @result
    ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if the
    ///   breakpoint doesn't exist.
    //------------------------------------------------------------------
    lldb::BreakpointSiteSP
    FindByID (lldb::break_id_t breakID);

    //------------------------------------------------------------------
    /// Returns a shared pointer to the breakpoint site with id \a breakID - const version.
    ///
    /// @param[in] breakID
    ///   The breakpoint site ID to seek for.
    ///
    /// @result
    ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if the
    ///   breakpoint doesn't exist.
    //------------------------------------------------------------------
    const lldb::BreakpointSiteSP
    FindByID (lldb::break_id_t breakID) const;

    //------------------------------------------------------------------
    /// Returns the breakpoint site id to the breakpoint site at address \a addr.
    ///
    /// @param[in] addr
    ///   The address to match.
    ///
    /// @result
    ///   The ID of the breakpoint site, or LLDB_INVALID_BREAK_ID.
    //------------------------------------------------------------------
    lldb::break_id_t
    FindIDByAddress (lldb::addr_t addr);

    //------------------------------------------------------------------
    /// Returns a shared pointer to the breakpoint site with index \a i.
    ///
    /// @param[in] i
    ///   The breakpoint site index to seek for.
    ///
    /// @result
    ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if the
    ///   breakpoint doesn't exist.
    //------------------------------------------------------------------
    lldb::BreakpointSiteSP
    GetByIndex (uint32_t i);

    //------------------------------------------------------------------
    /// Returns a shared pointer to the breakpoint site with index \a i - const version.
    ///
    /// @param[in] i
    ///   The breakpoint site index to seek for.
    ///
    /// @result
    ///   A shared pointer to the breakpoint site.  May contain a NULL pointer if the
    ///   breakpoint doesn't exist.
    //------------------------------------------------------------------
    const lldb::BreakpointSiteSP
    GetByIndex (uint32_t i) const;

    //------------------------------------------------------------------
    /// Removes the breakpoint site given by \b breakID from this list.
    ///
    /// @param[in] breakID
    ///   The breakpoint site index to remove.
    ///
    /// @result
    ///   \b true if the breakpoint site \a breakID was in the list.
    //------------------------------------------------------------------
    bool
    Remove (lldb::break_id_t breakID);

    //------------------------------------------------------------------
    /// Removes the breakpoint site at address \a addr from this list.
    ///
    /// @param[in] addr
    ///   The address from which to remove a breakpoint site.
    ///
    /// @result
    ///   \b true if \a addr had a breakpoint site to remove from the list.
    //------------------------------------------------------------------
    bool
    RemoveByAddress (lldb::addr_t addr);

    void
    SetEnabledForAll(const bool enable, const lldb::break_id_t except_id = LLDB_INVALID_BREAK_ID);

    typedef void (*BreakpointSiteSPMapFunc) (lldb::BreakpointSiteSP &bp, void *baton);

    //------------------------------------------------------------------
    /// Enquires of the breakpoint site on in this list with ID \a breakID whether
    /// we should stop for the breakpoint or not.
    ///
    /// @param[in] context
    ///    This contains the information about this stop.
    ///
    /// @param[in] breakID
    ///    This break ID that we hit.
    ///
    /// @return
    ///    \b true if we should stop, \b false otherwise.
    //------------------------------------------------------------------
    bool
    ShouldStop (StoppointCallbackContext *context, lldb::break_id_t breakID);

    //------------------------------------------------------------------
    /// Returns the number of elements in the list.
    ///
    /// @result
    ///   The number of elements.
    //------------------------------------------------------------------
    size_t
    GetSize() const { return m_bp_site_list.size(); }

protected:
    typedef std::map<lldb::addr_t, lldb::BreakpointSiteSP> collection;

    collection::iterator
    GetIDIterator(lldb::break_id_t breakID);

    collection::const_iterator
    GetIDConstIterator(lldb::break_id_t breakID) const;

    // This function exposes the m_bp_site_list.  I use the in Process because there
    // are places there where you want to iterate over the list, and it is less efficient
    // to do it by index.  FIXME: Find a better way to do this.

    const collection *
    GetMap ();

    collection m_bp_site_list;  // The breakpoint site list.
};

} // namespace lldb_private

#endif  // liblldb_BreakpointSiteList_h_
