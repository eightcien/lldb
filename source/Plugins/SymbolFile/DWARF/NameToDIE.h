//===-- NameToDIE.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_NameToDIE_h_
#define SymbolFileDWARF_NameToDIE_h_

#include <map>
#include "lldb/Core/ConstString.h"
#include "lldb/Core/RegularExpression.h"

class NameToDIE
{
public:
    typedef struct Info 
    {
        uint32_t cu_idx;
        uint32_t die_idx;
    } Info;
    

    NameToDIE () :
        m_collection ()
    {
    }
    
    ~NameToDIE ()
    {
    }
    
    void
    Dump (lldb_private::Stream *s);

    void
    Insert (const lldb_private::ConstString& name, const Info &info)
    {
        m_collection.insert (std::make_pair(name.AsCString(), info));
    }
    
    size_t
    Find (const lldb_private::ConstString &name, 
          std::vector<Info> &info_array) const;
    
    size_t
    Find (const lldb_private::RegularExpression& regex, 
          std::vector<Info> &info_array) const;

    size_t
    FindAllEntriesForCompileUnitWithIndex (const uint32_t cu_idx, 
                                           std::vector<Info> &info_array) const;

protected:
    typedef std::multimap<const char *, Info> collection;

    collection m_collection;
};

#endif  // SymbolFileDWARF_NameToDIE_h_
