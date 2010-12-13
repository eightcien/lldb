//===-- UserID.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/UserID.h"
#include "lldb/Core/Stream.h"

using namespace lldb;
using namespace lldb_private;

UserID::~UserID ()
{
}

Stream&
lldb_private::operator << (Stream& strm, const UserID& uid)
{
    strm.Printf("{0x%8.8x}", uid.GetID());
    return strm;
}
