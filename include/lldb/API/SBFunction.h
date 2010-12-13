//===-- SBFunction.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBFunction_h_
#define LLDB_SBFunction_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBInstructionList.h"

namespace lldb {

class SBFunction
{
public:

    SBFunction ();

    SBFunction (const lldb::SBFunction &rhs);

#ifndef SWIG
    const lldb::SBFunction &
    operator = (const lldb::SBFunction &rhs);
#endif


    ~SBFunction ();

    bool
    IsValid () const;

    const char *
    GetName() const;

    const char *
    GetMangledName () const;

    lldb::SBInstructionList
    GetInstructions (lldb::SBTarget target);

#ifndef SWIG
    bool
    operator == (const lldb::SBFunction &rhs) const;

    bool
    operator != (const lldb::SBFunction &rhs) const;
#endif

    bool
    GetDescription (lldb::SBStream &description);

protected:

    lldb_private::Function *
    get ();

private:
    friend class SBFrame;
    friend class SBSymbolContext;

    SBFunction (lldb_private::Function *lldb_object_ptr);


    lldb_private::Function *m_opaque_ptr;
};


} // namespace lldb

#endif // LLDB_SBFunction_h_
