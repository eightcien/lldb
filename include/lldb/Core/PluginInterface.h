//===-- PluginInterface.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PluginInterface_h_
#define liblldb_PluginInterface_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"

namespace lldb_private {

class PluginInterface
{
public:
    virtual
    ~PluginInterface () {}

    virtual const char *
    GetPluginName() = 0;

    virtual const char *
    GetShortPluginName() = 0;

    virtual uint32_t
    GetPluginVersion() = 0;

    virtual void
    GetPluginCommandHelp (const char *command, Stream *strm) = 0;

    virtual Error
    ExecutePluginCommand (Args &command, Stream *strm) = 0;

    virtual Log *
    EnablePluginLogging (Stream *strm, Args &command) = 0;
};

} // namespace lldb_private

#endif  // liblldb_PluginInterface_h_
