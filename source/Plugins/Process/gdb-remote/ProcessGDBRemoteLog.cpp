//===-- ProcessGDBRemoteLog.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProcessGDBRemoteLog.h"

#include "lldb/Interpreter/Args.h"
#include "lldb/Core/StreamFile.h"

#include "ProcessGDBRemote.h"

using namespace lldb;
using namespace lldb_private;


// We want to avoid global constructors where code needs to be run so here we
// control access to our static g_log_sp by hiding it in a singleton function
// that will construct the static g_lob_sp the first time this function is 
// called.
static LogSP &
GetLog ()
{
    static LogSP g_log_sp;
    return g_log_sp;
}

LogSP
ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (uint32_t mask)
{
    LogSP log(GetLog ());
    if (log && mask)
    {
        uint32_t log_mask = log->GetMask().Get();
        if ((log_mask & mask) != mask)
            return LogSP();
    }
    return log;
}

void
ProcessGDBRemoteLog::DisableLog (Args &args, Stream *feedback_strm)
{
    LogSP log (GetLog ());
    if (log)
    {
        uint32_t flag_bits = 0;
        
        const size_t argc = args.GetArgumentCount ();
        if (argc > 0)
        {
            flag_bits = log->GetMask().Get();
            for (size_t i = 0; i < argc; ++i)
            {
                const char *arg = args.GetArgumentAtIndex (i);
                

                if      (::strcasecmp (arg, "all")        == 0 ) flag_bits &= ~GDBR_LOG_ALL;
                else if (::strcasecmp (arg, "async")      == 0 ) flag_bits &= ~GDBR_LOG_ASYNC;
                else if (::strncasecmp (arg, "break", 5)  == 0 ) flag_bits &= ~GDBR_LOG_BREAKPOINTS;
                else if (::strncasecmp (arg, "comm", 4)   == 0 ) flag_bits &= ~GDBR_LOG_COMM;
                else if (::strcasecmp (arg, "default")    == 0 ) flag_bits &= ~GDBR_LOG_DEFAULT;
                else if (::strcasecmp (arg, "packets")    == 0 ) flag_bits &= ~GDBR_LOG_PACKETS;
                else if (::strcasecmp (arg, "memory")     == 0 ) flag_bits &= ~GDBR_LOG_MEMORY;
                else if (::strcasecmp (arg, "data-short") == 0 ) flag_bits &= ~GDBR_LOG_MEMORY_DATA_SHORT;
                else if (::strcasecmp (arg, "data-long")  == 0 ) flag_bits &= ~GDBR_LOG_MEMORY_DATA_LONG;
                else if (::strcasecmp (arg, "process")    == 0 ) flag_bits &= ~GDBR_LOG_PROCESS;
                else if (::strcasecmp (arg, "step")       == 0 ) flag_bits &= ~GDBR_LOG_STEP;
                else if (::strcasecmp (arg, "thread")     == 0 ) flag_bits &= ~GDBR_LOG_THREAD;
                else if (::strcasecmp (arg, "verbose")    == 0 ) flag_bits &= ~GDBR_LOG_VERBOSE;
                else if (::strncasecmp (arg, "watch", 5)  == 0 ) flag_bits &= ~GDBR_LOG_WATCHPOINTS;
                else
                {
                    feedback_strm->Printf("error: unrecognized log category '%s'\n", arg);
                    ListLogCategories (feedback_strm);
                }
                
            }
        }
        
        if (flag_bits == 0)
            GetLog ().reset();
        else
            log->GetMask().Reset (flag_bits);
    }
    
    return;
}

LogSP
ProcessGDBRemoteLog::EnableLog (StreamSP &log_stream_sp, uint32_t log_options, Args &args, Stream *feedback_strm)
{
    // Try see if there already is a log - that way we can reuse its settings.
    // We could reuse the log in toto, but we don't know that the stream is the same.
    uint32_t flag_bits = 0;
    LogSP log(GetLog ());
    if (log)
        flag_bits = log->GetMask().Get();

    // Now make a new log with this stream if one was provided
    if (log_stream_sp)
    {
        log = make_shared<Log>(log_stream_sp);
        GetLog () = log;
    }

    if (log)
    {
        bool got_unknown_category = false;
        const size_t argc = args.GetArgumentCount();
        for (size_t i=0; i<argc; ++i)
        {
            const char *arg = args.GetArgumentAtIndex(i);

            if      (::strcasecmp (arg, "all")        == 0 ) flag_bits |= GDBR_LOG_ALL;
            else if (::strcasecmp (arg, "async")      == 0 ) flag_bits |= GDBR_LOG_ASYNC;
            else if (::strncasecmp (arg, "break", 5)  == 0 ) flag_bits |= GDBR_LOG_BREAKPOINTS;
            else if (::strncasecmp (arg, "comm", 4)   == 0 ) flag_bits |= GDBR_LOG_COMM;
            else if (::strcasecmp (arg, "default")    == 0 ) flag_bits |= GDBR_LOG_DEFAULT;
            else if (::strcasecmp (arg, "packets")    == 0 ) flag_bits |= GDBR_LOG_PACKETS;
            else if (::strcasecmp (arg, "memory")     == 0 ) flag_bits |= GDBR_LOG_MEMORY;
            else if (::strcasecmp (arg, "data-short") == 0 ) flag_bits |= GDBR_LOG_MEMORY_DATA_SHORT;
            else if (::strcasecmp (arg, "data-long")  == 0 ) flag_bits |= GDBR_LOG_MEMORY_DATA_LONG;
            else if (::strcasecmp (arg, "process")    == 0 ) flag_bits |= GDBR_LOG_PROCESS;
            else if (::strcasecmp (arg, "step")       == 0 ) flag_bits |= GDBR_LOG_STEP;
            else if (::strcasecmp (arg, "thread")     == 0 ) flag_bits |= GDBR_LOG_THREAD;
            else if (::strcasecmp (arg, "verbose")    == 0 ) flag_bits |= GDBR_LOG_VERBOSE;
            else if (::strncasecmp (arg, "watch", 5)  == 0 ) flag_bits |= GDBR_LOG_WATCHPOINTS;
            else
            {
                feedback_strm->Printf("error: unrecognized log category '%s'\n", arg);
                if (got_unknown_category == false)
                {
                    got_unknown_category = true;
                    ListLogCategories (feedback_strm);
                }
            }
        }
        if (flag_bits == 0)
            flag_bits = GDBR_LOG_DEFAULT;
        log->GetMask().Reset(flag_bits);
        log->GetOptions().Reset(log_options);
    }
    return log;
}

void
ProcessGDBRemoteLog::ListLogCategories (Stream *strm)
{
    strm->Printf("Logging categories for '%s':\n"
        "\tall - turn on all available logging categories\n"
        "\tasync - log asynchronous activity\n"
        "\tbreak - log breakpoints\n"
        "\tcommunication - log communication activity\n"
        "\tdefault - enable the default set of logging categories for liblldb\n"
        "\tpackets - log gdb remote packets\n"
        "\tmemory - log memory reads and writes\n"
        "\tdata-short - log memory bytes for memory reads and writes for short transactions only\n"
        "\tdata-long - log memory bytes for memory reads and writes for all transactions\n"
        "\tprocess - log process events and activities\n"
        "\tthread - log thread events and activities\n"
        "\tstep - log step related activities\n"
        "\tverbose - enable verbose logging\n"
        "\twatch - log watchpoint related activities\n", ProcessGDBRemote::GetPluginNameStatic());
}


void
ProcessGDBRemoteLog::LogIf (uint32_t mask, const char *format, ...)
{
    LogSP log (ProcessGDBRemoteLog::GetLogIfAllCategoriesSet (mask));
    if (log)
    {
        va_list args;
        va_start (args, format);
        log->VAPrintf (format, args);
        va_end (args);
    }
}
