
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Log.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

#include "DYLDRendezvous.h"

using namespace lldb;
using namespace lldb_private;

namespace {

addr_t
ResolveRendezvousAddress(Process *process)
{
    addr_t info_location;
    addr_t info_addr;
    ArchSpec arch;
    Error error;
    size_t size;

    arch = process->GetTarget().GetArchitecture();
    info_location = process->GetImageInfoAddress();

    if (info_location == LLDB_INVALID_ADDRESS)
        return LLDB_INVALID_ADDRESS;

    info_addr = 0;
    size = process->DoReadMemory(info_location, &info_addr,
                                 arch.GetAddressByteSize(), error);
    if (size != arch.GetAddressByteSize() || error.Fail())
        return LLDB_INVALID_ADDRESS;

    if (info_addr == 0)
        return LLDB_INVALID_ADDRESS;

    return info_addr;
}

} // end anonymous namespace.

DYLDRendezvous::DYLDRendezvous(Process *process)
    : m_process(process),
      m_rendezvous_addr(LLDB_INVALID_ADDRESS),
      m_version(0),
      m_map_addr(LLDB_INVALID_ADDRESS),
      m_brk(LLDB_INVALID_ADDRESS),
      m_state(0),
      m_ldbase(LLDB_INVALID_ADDRESS),
      m_soentries()
{
}

bool
DYLDRendezvous::Resolve()
{
    const size_t word_size = 4;
    size_t address_size;
    size_t padding;
    addr_t info_addr;
    addr_t cursor;

    address_size = m_process->GetTarget().GetArchitecture().GetAddressByteSize();
    padding = address_size - word_size;
    cursor = info_addr = ResolveRendezvousAddress(m_process);

    if (cursor == LLDB_INVALID_ADDRESS)
        return false;

    if (!(cursor = ReadMemory(cursor, &m_version, word_size)))
        return false;

    if (!(cursor = ReadMemory(cursor + padding, &m_map_addr, address_size)))
        return false;

    if (!(cursor = ReadMemory(cursor, &m_brk, address_size)))
        return false;

    if (!(cursor = ReadMemory(cursor, &m_state, word_size)))
        return false;

    if (!(cursor = ReadMemory(cursor + padding, &m_ldbase, address_size)))
        return false;

    m_rendezvous_addr = info_addr;    

    return UpdateSOEntries();
}

bool
DYLDRendezvous::IsResolved()
{
    return m_rendezvous_addr != LLDB_INVALID_ADDRESS;
}

bool
DYLDRendezvous::UpdateSOEntries()
{
    SOEntry entry;
    addr_t address_size;

    if (m_map_addr == LLDB_INVALID_ADDRESS)
        return false;

    m_soentries.clear();
    address_size = m_process->GetTarget().GetArchitecture().GetAddressByteSize();

    for (addr_t cursor = m_map_addr; cursor != 0; cursor = entry.next)
    {
        entry.clear();

        if (!(cursor = ReadMemory(cursor, &entry.base_addr, address_size)))
            return false;

        if (!(cursor = ReadMemory(cursor, &entry.path_addr, address_size)))
            return false;

        if (!(cursor = ReadMemory(cursor, &entry.dyn_addr, address_size)))
            return false;

        if (!(cursor = ReadMemory(cursor, &entry.next, address_size)))
            return false;

        if (!(cursor = ReadMemory(cursor, &entry.prev, address_size)))
            return false;

        entry.path = ReadStringFromMemory(entry.path_addr);

        if (entry.path.empty())
            continue;

        m_soentries.push_back(entry);
    }

    return true;
}

addr_t
DYLDRendezvous::ReadMemory(addr_t addr, void *dst, size_t size)
{
    size_t bytes_read;
    Error error;

    bytes_read = m_process->DoReadMemory(addr, dst, size, error);
    if (bytes_read != size || error.Fail())
        return 0;

    return addr + bytes_read;
}

std::string
DYLDRendezvous::ReadStringFromMemory(addr_t addr)
{
    std::string str;
    Error error;
    size_t size;
    char c;

    if (addr == LLDB_INVALID_ADDRESS)
        return std::string();

    for (;;) {
        size = m_process->DoReadMemory(addr, &c, 1, error);
        if (size != 1 || error.Fail())
            return std::string();
        if (c == 0)
            break;
        else {
            str.push_back(c);
            addr++;
        }
    }

    return str;
}

void
DYLDRendezvous::DumpToLog(LogSP log) const
{
    int state = GetState();

    if (!log)
        return;

    log->PutCString("DYLDRendezvous:");
    log->Printf("   Address: %lx", GetRendezvousAddress());
    log->Printf("   Version: %d",  GetVersion());
    log->Printf("   Link   : %lx", GetLinkMapAddress());
    log->Printf("   Break  : %lx", GetBreakAddress());
    log->Printf("   LDBase : %lx", GetLDBase());
    log->Printf("   State  : %s", 
                (state == eConsistent) ? "consistent" :
                (state == eAdd)        ? "add"        :
                (state == eDelete)     ? "delete"     : "unknown");
    
    const_iterator I = begin();
    const_iterator E = end();

    if (I != E) 
        log->PutCString("DYLDRendezvous SOEntries:");
    
    for (int i = 1; I != E; ++I, ++i) 
    {
        log->Printf("\n   SOEntry [%d] %s", i, I->path.c_str());
        log->Printf("      Base : %lx", I->base_addr);
        log->Printf("      Path : %lx", I->path_addr);
        log->Printf("      Dyn  : %lx", I->dyn_addr);
        log->Printf("      Next : %lx", I->next);
        log->Printf("      Prev : %lx", I->prev);
    }
}
