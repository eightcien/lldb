//===-- DYLDRendezvous.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Rendezvous_H_
#define liblldb_Rendezvous_H_

// C Includes
// C++ Includes
#include <list>
#include <string>

// Other libraries and framework includes
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"


namespace lldb_private {
class Process;
}

class DYLDRendezvous {

public:
    DYLDRendezvous(lldb_private::Process *process);
    
    bool 
    Resolve();

    bool 
    IsResolved();

    lldb::addr_t
    GetRendezvousAddress() const { return m_rendezvous_addr; }

    int
    GetVersion() const { return m_version; }

    lldb::addr_t 
    GetLinkMapAddress() const { return m_map_addr; }

    lldb::addr_t
    GetBreakAddress() const { return m_brk; }

    int
    GetState() const { return m_state; }

    lldb::addr_t
    GetLDBase() const { return m_ldbase; }

    void
    DumpToLog(lldb::LogSP log) const;

    /// @brief Constants describing the state of the rendezvous.
    ///
    /// @see GetState().
    enum RendezvousState {
        eConsistent,
        eAdd,
        eDelete
    };

    /// @brief Structure representing the shared objects currently loaded into
    /// the inferior process.
    ///
    /// This object is a rough analogue to the struct link_map object which
    /// actually lives in the inferiors memory.
    struct SOEntry {
        lldb::addr_t base_addr; ///< Base address of the loaded object.
        lldb::addr_t path_addr; ///< String naming the shared object.
        std::string  path;      ///< Absolute file name of shared object.
        lldb::addr_t dyn_addr;  ///< Dynamic section of shared object.
        lldb::addr_t next;      ///< Address of next so_entry.
        lldb::addr_t prev;      ///< Address of previous so_entry.

        SOEntry() { clear(); }

        void clear() {
            base_addr = 0;
            path_addr = 0;
            dyn_addr  = 0;
            next = 0;
            prev = 0;
            path.clear();
        }
    };

protected:
    typedef std::list<SOEntry> SOEntryList;

public:
    typedef SOEntryList::iterator iterator;
    typedef SOEntryList::const_iterator const_iterator;

    iterator begin() { return m_soentries.begin(); }
    iterator end() { return m_soentries.end(); }

    const_iterator begin() const { return m_soentries.begin(); }
    const_iterator end() const { return m_soentries.end(); }

protected:
    lldb_private::Process *m_process;

    /// Location of the r_debug structure in the inferiors address space.
    lldb::addr_t m_rendezvous_addr;

    /// Version of the r_debug protocol.
    int m_version;

    /// Pointer to the first entry in the link map.
    lldb::addr_t m_map_addr;

    /// Address of the run-time linker function called each time a library is
    /// loaded/unloaded.
    lldb::addr_t m_brk;

    /// Current state of the rendezvous.
    int m_state;

    /// Base address where the run-time linker is loaded.
    lldb::addr_t m_ldbase;

    /// List of SOEntry objects corresponding to the current link map state.
    SOEntryList m_soentries;

    /// Reads @p size bytes from the inferiors address space starting at @p
    /// addr.
    ///
    /// @returns addr + size if the read was successful and false otherwise.
    lldb::addr_t
    ReadMemory(lldb::addr_t addr, void *dst, size_t size);

    /// Reads a null-terminated C string from the memory location starting at @p
    /// addr.
    std::string
    ReadStringFromMemory(lldb::addr_t addr);

    bool
    UpdateSOEntries();
};

#endif
