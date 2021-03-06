//===-- SBEvent.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBEvent.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBStream.h"

#include "lldb/Core/Event.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Core/ConstString.h"
#include "lldb/Target/Process.h"
#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Interpreter/CommandInterpreter.h"

using namespace lldb;
using namespace lldb_private;


SBEvent::SBEvent () :
    m_event_sp (),
    m_opaque_ptr (NULL)
{
}

SBEvent::SBEvent (uint32_t event_type, const char *cstr, uint32_t cstr_len) :
    m_event_sp (new Event (event_type, new EventDataBytes (cstr, cstr_len))),
    m_opaque_ptr (m_event_sp.get())
{
}

SBEvent::SBEvent (EventSP &event_sp) :
    m_event_sp (event_sp),
    m_opaque_ptr (event_sp.get())
{
}

SBEvent::SBEvent (const SBEvent &rhs) :
    m_event_sp (rhs.m_event_sp),
    m_opaque_ptr (rhs.m_opaque_ptr)
{
    
}
    
const SBEvent &
SBEvent::operator = (const SBEvent &rhs)
{
    if (this != &rhs)
    {
        m_event_sp = rhs.m_event_sp;
        m_opaque_ptr = rhs.m_opaque_ptr;
    }
    return *this;
}

SBEvent::~SBEvent()
{
}

const char *
SBEvent::GetDataFlavor ()
{
    Event *lldb_event = get();
    if (lldb_event)
    {
        EventData *event_data = lldb_event->GetData();
        if (event_data)
            return lldb_event->GetData()->GetFlavor().AsCString();
    }
    return NULL;
}

uint32_t
SBEvent::GetType () const
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    const Event *lldb_event = get();
    uint32_t event_type = 0;
    if (lldb_event)
        event_type = lldb_event->GetType();

    if (log)
    {
        StreamString sstr;
        if (lldb_event && lldb_event->GetBroadcaster() && lldb_event->GetBroadcaster()->GetEventNames(sstr, event_type, true))
            log->Printf ("SBEvent(%p)::GetType () => 0x%8.8x (%s)", get(), event_type, sstr.GetData());
        else
            log->Printf ("SBEvent(%p)::GetType () => 0x%8.8x", get(), event_type);

    }

    return event_type;
}

SBBroadcaster
SBEvent::GetBroadcaster () const
{
    SBBroadcaster broadcaster;
    const Event *lldb_event = get();
    if (lldb_event)
        broadcaster.reset (lldb_event->GetBroadcaster(), false);
    return broadcaster;
}

bool
SBEvent::BroadcasterMatchesPtr (const SBBroadcaster *broadcaster)
{
    if (broadcaster)
        return BroadcasterMatchesRef (*broadcaster);
    return false;
}

bool
SBEvent::BroadcasterMatchesRef (const SBBroadcaster &broadcaster)
{

    Event *lldb_event = get();
    bool success = false;
    if (lldb_event)
        success = lldb_event->BroadcasterIs (broadcaster.get());

    // For logging, this gets a little chatty so only enable this when verbose logging is on
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API | LIBLLDB_LOG_VERBOSE));
    if (log)
        log->Printf ("SBEvent(%p)::BroadcasterMatchesRef (SBBroadcaster(%p): %s) => %i", 
                     get(),
                     broadcaster.get(),
                     broadcaster.GetName(),
                     success);

    return success;
}

void
SBEvent::Clear()
{
    Event *lldb_event = get();
    if (lldb_event)
        lldb_event->Clear();
}

EventSP &
SBEvent::GetSP () const
{
    return m_event_sp;
}

Event *
SBEvent::get() const
{
    // There is a dangerous accessor call GetSharedPtr which can be used, so if
    // we have anything valid in m_event_sp, we must use that since if it gets
    // used by a function that puts something in there, then it won't update
    // m_opaque_ptr...
    if (m_event_sp)
        m_opaque_ptr = m_event_sp.get();

    return m_opaque_ptr;
}

void
SBEvent::reset (EventSP &event_sp)
{
    m_event_sp = event_sp;
    m_opaque_ptr = m_event_sp.get();
}

void
SBEvent::reset (Event* event_ptr)
{
    m_opaque_ptr = event_ptr;
    m_event_sp.reset();
}

bool
SBEvent::IsValid() const
{
    // Do NOT use m_opaque_ptr directly!!! Must use the SBEvent::get()
    // accessor. See comments in SBEvent::get()....
    return SBEvent::get() != NULL;

}

const char *
SBEvent::GetCStringFromEvent (const SBEvent &event)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBEvent(%p)::GetCStringFromEvent () => \"%s\"", 
                     event.get(), 
                     reinterpret_cast<const char *>(EventDataBytes::GetBytesFromEvent (event.get())));

    return reinterpret_cast<const char *>(EventDataBytes::GetBytesFromEvent (event.get()));
}


bool
SBEvent::GetDescription (SBStream &description)
{
    if (get())
    {
        description.ref();
        m_opaque_ptr->Dump (description.get());
    }
    else
        description.Printf ("No value");

    return true;
}

bool
SBEvent::GetDescription (SBStream &description) const
{
    if (get())
    {
        description.ref();
        m_opaque_ptr->Dump (description.get());
    }
    else
        description.Printf ("No value");

    return true;
}
