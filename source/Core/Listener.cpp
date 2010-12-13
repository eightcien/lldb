//===-- Listener.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Listener.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Broadcaster.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Core/Event.h"
#include "lldb/Host/TimeValue.h"
#include "lldb/lldb-private-log.h"
#include <algorithm>

using namespace lldb;
using namespace lldb_private;

Listener::Listener(const char *name) :
    m_name (name),
    m_broadcasters(),
    m_broadcasters_mutex (Mutex::eMutexTypeRecursive),
    m_events (),
    m_events_mutex (Mutex::eMutexTypeRecursive),
    m_cond_wait()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_OBJECT));
    if (log)
        log->Printf ("%p Listener::Listener('%s')", this, m_name.c_str());
}

Listener::~Listener()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_OBJECT));
    if (log)
        log->Printf ("%p Listener::~Listener('%s')", this, m_name.c_str());
    Clear();
}

void
Listener::Clear()
{
    Mutex::Locker locker(m_broadcasters_mutex);
    broadcaster_collection::iterator pos, end = m_broadcasters.end();
    for (pos = m_broadcasters.begin(); pos != end; ++pos)
        pos->first->RemoveListener (this, pos->second.event_mask);
    m_broadcasters.clear();
    m_cond_wait.SetValue (false, eBroadcastNever);
    m_broadcasters.clear();
}

uint32_t
Listener::StartListeningForEvents (Broadcaster* broadcaster, uint32_t event_mask)
{
    if (broadcaster)
    {
        // Scope for "locker"
        // Tell the broadcaster to add this object as a listener
        {
            Mutex::Locker locker(m_broadcasters_mutex);
            m_broadcasters.insert(std::make_pair(broadcaster, BroadcasterInfo(event_mask)));
        }

        uint32_t acquired_mask = broadcaster->AddListener (this, event_mask);

        if (event_mask != acquired_mask)
        {

        }
        LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS));
        if (log)
            log->Printf ("%p Listener::StartListeningForEvents (broadcaster = %p, mask = 0x%8.8x) acquired_mask = 0x%8.8x for %s",
                         this,
                         broadcaster,
                         event_mask,
                         acquired_mask,
                         m_name.c_str());

        return acquired_mask;

    }
    return 0;
}

uint32_t
Listener::StartListeningForEvents (Broadcaster* broadcaster, uint32_t event_mask, HandleBroadcastCallback callback, void *callback_user_data)
{
    if (broadcaster)
    {
        // Scope for "locker"
        // Tell the broadcaster to add this object as a listener
        {
            Mutex::Locker locker(m_broadcasters_mutex);
            m_broadcasters.insert(std::make_pair(broadcaster, BroadcasterInfo(event_mask, callback, callback_user_data)));
        }

        uint32_t acquired_mask = broadcaster->AddListener (this, event_mask);

        LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS));
        if (log)
            log->Printf ("%p Listener::StartListeningForEvents (broadcaster = %p, mask = 0x%8.8x, callback = %p, user_data = %p) acquired_mask = 0x%8.8x for %s",
                        this, broadcaster, event_mask, callback, callback_user_data, acquired_mask, m_name.c_str());

        return acquired_mask;
    }
    return 0;
}

bool
Listener::StopListeningForEvents (Broadcaster* broadcaster, uint32_t event_mask)
{
    if (broadcaster)
    {
        // Scope for "locker"
        {
            Mutex::Locker locker(m_broadcasters_mutex);
            m_broadcasters.erase (broadcaster);
        }
        // Remove the broadcaster from our set of broadcasters
        return broadcaster->RemoveListener (this, event_mask);
    }

    return false;
}

// Called when a Broadcaster is in its destuctor. We need to remove all
// knowledge of this broadcaster and any events that it may have queued up
void
Listener::BroadcasterWillDestruct (Broadcaster *broadcaster)
{
    // Scope for "broadcasters_locker"
    {
        Mutex::Locker broadcasters_locker(m_broadcasters_mutex);
        m_broadcasters.erase (broadcaster);
    }

    // Scope for "event_locker"
    {
        Mutex::Locker event_locker(m_events_mutex);
        // Remove all events for this broadcaster object.
        event_collection::iterator pos = m_events.begin();
        while (pos != m_events.end())
        {
            if ((*pos)->GetBroadcaster() == broadcaster)
                pos = m_events.erase(pos);
            else
                ++pos;
        }

        if (m_events.empty())
            m_cond_wait.SetValue (false, eBroadcastNever);

    }
}

void
Listener::AddEvent (EventSP &event_sp)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS));
    if (log)
        log->Printf ("%p Listener('%s')::AddEvent (event_sp = {%p})", this, m_name.c_str(), event_sp.get());

    // Scope for "locker"
    {
        Mutex::Locker locker(m_events_mutex);
        m_events.push_back (event_sp);
    }
    m_cond_wait.SetValue (true, eBroadcastAlways);
}

class EventBroadcasterMatches
{
public:
    EventBroadcasterMatches (Broadcaster *broadcaster) :
        m_broadcaster (broadcaster)    {
    }

    bool operator() (const EventSP &event_sp) const
    {
        if (event_sp->BroadcasterIs(m_broadcaster))
            return true;
        else
            return false;
    }

private:
    Broadcaster *m_broadcaster;

};

class EventMatcher
{
public:
    EventMatcher (Broadcaster *broadcaster, const ConstString *broadcaster_names, uint32_t num_broadcaster_names, uint32_t event_type_mask) :
        m_broadcaster (broadcaster),
        m_broadcaster_names (broadcaster_names),
        m_num_broadcaster_names (num_broadcaster_names),
        m_event_type_mask (event_type_mask)
    {
    }

    bool operator() (const EventSP &event_sp) const
    {
        if (m_broadcaster && !event_sp->BroadcasterIs(m_broadcaster))
            return false;

        if (m_broadcaster_names)
        {
            bool found_source = false;
            const ConstString &event_broadcaster_name = event_sp->GetBroadcaster()->GetBroadcasterName();
            for (uint32_t i=0; i<m_num_broadcaster_names; ++i)
            {
                if (m_broadcaster_names[i] == event_broadcaster_name)
                {
                    found_source = true;
                    break;
                }
            }
            if (!found_source)
                return false;
        }

        if (m_event_type_mask == 0 || m_event_type_mask & event_sp->GetType())
            return true;
        return false;
    }

private:
    Broadcaster *m_broadcaster;
    const ConstString *m_broadcaster_names;
    const uint32_t m_num_broadcaster_names;
    const uint32_t m_event_type_mask;
};


bool
Listener::FindNextEventInternal
(
    Broadcaster *broadcaster,   // NULL for any broadcaster
    const ConstString *broadcaster_names, // NULL for any event
    uint32_t num_broadcaster_names,
    uint32_t event_type_mask,
    EventSP &event_sp,
    bool remove)
{
    //LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS));

    Mutex::Locker lock(m_events_mutex);

    if (m_events.empty())
        return false;


    Listener::event_collection::iterator pos = m_events.end();

    if (broadcaster == NULL && broadcaster_names == NULL && event_type_mask == 0)
    {
        pos = m_events.begin();
    }
    else
    {
        pos = std::find_if (m_events.begin(), m_events.end(), EventMatcher (broadcaster, broadcaster_names, num_broadcaster_names, event_type_mask));
    }

    if (pos != m_events.end())
    {
        event_sp = *pos;
        if (remove)
        {
            m_events.erase(pos);

            if (m_events.empty())
                m_cond_wait.SetValue (false, eBroadcastNever);
        }
        
        // Unlock the event queue here.  We've removed this event and are about to return
        // it so it should be okay to get the next event off the queue here - and it might
        // be useful to do that in the "DoOnRemoval".
        lock.Reset();
        event_sp->DoOnRemoval();
        return true;
    }

    event_sp.reset();
    return false;
}

Event *
Listener::PeekAtNextEvent ()
{
    EventSP event_sp;
    if (FindNextEventInternal (NULL, NULL, 0, 0, event_sp, false))
        return event_sp.get();
    return NULL;
}

Event *
Listener::PeekAtNextEventForBroadcaster (Broadcaster *broadcaster)
{
    EventSP event_sp;
    if (FindNextEventInternal (broadcaster, NULL, 0, 0, event_sp, false))
        return event_sp.get();
    return NULL;
}

Event *
Listener::PeekAtNextEventForBroadcasterWithType (Broadcaster *broadcaster, uint32_t event_type_mask)
{
    EventSP event_sp;
    if (FindNextEventInternal (broadcaster, NULL, 0, event_type_mask, event_sp, false))
        return event_sp.get();
    return NULL;
}


bool
Listener::GetNextEventInternal
(
    Broadcaster *broadcaster,   // NULL for any broadcaster
    const ConstString *broadcaster_names, // NULL for any event
    uint32_t num_broadcaster_names,
    uint32_t event_type_mask,
    EventSP &event_sp
)
{
    return FindNextEventInternal (broadcaster, broadcaster_names, num_broadcaster_names, event_type_mask, event_sp, true);
}

bool
Listener::GetNextEvent (EventSP &event_sp)
{
    return GetNextEventInternal (NULL, NULL, 0, 0, event_sp);
}


bool
Listener::GetNextEventForBroadcaster (Broadcaster *broadcaster, EventSP &event_sp)
{
    return GetNextEventInternal (broadcaster, NULL, 0, 0, event_sp);
}

bool
Listener::GetNextEventForBroadcasterWithType (Broadcaster *broadcaster, uint32_t event_type_mask, EventSP &event_sp)
{
    return GetNextEventInternal (broadcaster, NULL, 0, event_type_mask, event_sp);
}


bool
Listener::WaitForEventsInternal
(
    const TimeValue *timeout,
    Broadcaster *broadcaster,   // NULL for any broadcaster
    const ConstString *broadcaster_names, // NULL for any event
    uint32_t num_broadcaster_names,
    uint32_t event_type_mask,
    EventSP &event_sp
)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS));
    bool timed_out = false;

    if (log)
    {
        log->Printf ("%p Listener::WaitForEventsInternal (timeout = { %p }) for %s",
                    this, timeout, m_name.c_str());
    }

    while (1)
    {
        if (GetNextEventInternal (broadcaster, broadcaster_names, num_broadcaster_names, event_type_mask, event_sp))
            return true;

        // Reset condition value to false, so we can wait for new events to be
        // added that might meet our current filter
        m_cond_wait.SetValue (false, eBroadcastNever);

        if (m_cond_wait.WaitForValueEqualTo (true, timeout, &timed_out))
            continue;

        else if (timed_out)
        {
            log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS);
            if (log)
                log->Printf ("%p Listener::WaitForEvents() timed out for %s", this, m_name.c_str());
            break;
        }
        else
        {
            log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EVENTS);
            if (log)
                log->Printf ("%p Listener::WaitForEvents() unknown error for %s", this, m_name.c_str());
            break;
        }
    }

    return false;
}

bool
Listener::WaitForEventForBroadcasterWithType
(
    const TimeValue *timeout,
    Broadcaster *broadcaster,
    uint32_t event_type_mask,
    EventSP &event_sp
)
{
    return WaitForEventsInternal (timeout, broadcaster, NULL, 0, event_type_mask, event_sp);
}

bool
Listener::WaitForEventForBroadcaster
(
    const TimeValue *timeout,
    Broadcaster *broadcaster,
    EventSP &event_sp
)
{
    return WaitForEventsInternal (timeout, broadcaster, NULL, 0, 0, event_sp);
}

bool
Listener::WaitForEvent (const TimeValue *timeout, EventSP &event_sp)
{
    return WaitForEventsInternal (timeout, NULL, NULL, 0, 0, event_sp);
}

//Listener::broadcaster_collection::iterator
//Listener::FindBroadcasterWithMask (Broadcaster *broadcaster, uint32_t event_mask, bool exact)
//{
//    broadcaster_collection::iterator pos;
//    broadcaster_collection::iterator end = m_broadcasters.end();
//    for (pos = m_broadcasters.find (broadcaster);
//        pos != end && pos->first == broadcaster;
//        ++pos)
//    {
//        if (exact)
//        {
//            if ((event_mask & pos->second.event_mask) == event_mask)
//                return pos;
//        }
//        else
//        {
//            if (event_mask & pos->second.event_mask)
//                return pos;
//        }
//    }
//    return end;
//}

size_t
Listener::HandleBroadcastEvent (EventSP &event_sp)
{
    size_t num_handled = 0;
    Mutex::Locker locker(m_broadcasters_mutex);
    Broadcaster *broadcaster = event_sp->GetBroadcaster();
    broadcaster_collection::iterator pos;
    broadcaster_collection::iterator end = m_broadcasters.end();
    for (pos = m_broadcasters.find (broadcaster);
        pos != end && pos->first == broadcaster;
        ++pos)
    {
        BroadcasterInfo info = pos->second;
        if (event_sp->GetType () & info.event_mask)
        {
            if (info.callback != NULL)
            {
                info.callback (event_sp, info.callback_user_data);
                ++num_handled;
            }
        }
    }
    return num_handled;
}
