//===---------------------UnwindAssemblyProfiler.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_UnwindAssemblyProfiler_h_
#define utility_UnwindAssemblyProfiler_h_

#include "lldb-private.h"
#include "lldb/Core/PluginInterface.h"

namespace lldb_private {

class UnwindAssemblyProfiler :
   public PluginInterface
{
public:

    virtual
    ~UnwindAssemblyProfiler();

    virtual bool
    GetNonCallSiteUnwindPlanFromAssembly (lldb_private::AddressRange& func, Thread& thread, lldb_private::UnwindPlan& unwind_plan) = 0;

    virtual bool
    GetFastUnwindPlan (lldb_private::AddressRange& func, Thread& thread, lldb_private::UnwindPlan &unwind_plan) = 0;

    // thread may be NULL in which case we only use the Target (e.g. if this is called pre-process-launch).
    virtual bool
    FirstNonPrologueInsn (lldb_private::AddressRange& func, Target& target, Thread* thread, lldb_private::Address& first_non_prologue_insn) = 0;

    static UnwindAssemblyProfiler*
    FindPlugin (const ArchSpec &arch);

protected:
    UnwindAssemblyProfiler();
private:
    DISALLOW_COPY_AND_ASSIGN (UnwindAssemblyProfiler);
};

} // namespace lldb_private

#endif //utility_UnwindAssemblyProfiler_h_


