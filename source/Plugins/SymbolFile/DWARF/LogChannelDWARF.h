//===-- LogChannelDWARF.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LogChannelDWARF_h_
#define liblldb_LogChannelDWARF_h_

// C Includes
// C++ Includes
// Other libraries and framework includes

// Project includes
#include "lldb/Core/Log.h"

#define DWARF_LOG_VERBOSE           (1u << 0)
#define DWARF_LOG_DEBUG_INFO        (1u << 1)
#define DWARF_LOG_DEBUG_LINE        (1u << 2)
#define DWARF_LOG_DEBUG_PUBNAMES    (1u << 3)
#define DWARF_LOG_DEBUG_PUBTYPES    (1u << 4)
#define DWARF_LOG_ALL               (UINT32_MAX)
#define DWARF_LOG_DEFAULT           (DWARF_LOG_DEBUG_INFO)

class LogChannelDWARF : public lldb_private::LogChannel
{
public:
    LogChannelDWARF ();

    virtual
    ~LogChannelDWARF ();

    static void
    Initialize();

    static void
    Terminate();

    static const char *
    GetPluginNameStatic();

    static const char *
    GetPluginDescriptionStatic();

    static lldb_private::LogChannel *
    CreateInstance ();

    virtual const char *
    GetPluginName();

    virtual const char *
    GetShortPluginName();

    virtual uint32_t
    GetPluginVersion();

    virtual void
    GetPluginCommandHelp (const char *command, lldb_private::Stream *strm);

    virtual lldb_private::Error
    ExecutePluginCommand (lldb_private::Args &command, lldb_private::Stream *strm);

    virtual lldb_private::Log *
    EnablePluginLogging (lldb_private::Stream *strm, lldb_private::Args &command);

    virtual void
    Disable (lldb_private::Args &args, lldb_private::Stream *feedback_strm);

    void
    Delete ();

    virtual bool
    Enable (lldb::StreamSP &log_stream_sp,
            uint32_t log_options,
            lldb_private::Stream *feedback_strm,      // Feedback stream for argument errors etc
            const lldb_private::Args &categories);    // The categories to enable within this logging stream, if empty, enable default set

    virtual void
    ListCategories (lldb_private::Stream *strm);

    static lldb_private::Log *
    GetLog ();

    static lldb_private::Log *
    GetLogIfAll (uint32_t mask);

    static void
    LogIf (uint32_t mask, const char *format, ...);
};

#endif  // liblldb_LogChannelDWARF_h_
