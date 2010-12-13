//===-- SBInstructionList.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBInstructionList_h_
#define LLDB_SBInstructionList_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>

namespace lldb {

class SBInstructionList
{
public:

    SBInstructionList ();

    SBInstructionList (const SBInstructionList &rhs);
    
#ifndef SWIG
    const SBInstructionList &
    operator = (const SBInstructionList &rhs);
#endif

    ~SBInstructionList ();

    size_t
    GetSize ();

    lldb::SBInstruction
    GetInstructionAtIndex (uint32_t idx);

    void
    Clear ();

    void
    AppendInstruction (lldb::SBInstruction inst);

    void
    Print (FILE *out);

    bool
    GetDescription (lldb::SBStream &description);

protected:
    friend class SBFunction;
    friend class SBSymbol;
    
    void
    SetDisassembler (const lldb::DisassemblerSP &opaque_sp);

private:    
    lldb::DisassemblerSP m_opaque_sp;
};


} // namespace lldb

#endif // LLDB_SBInstructionList_h_
