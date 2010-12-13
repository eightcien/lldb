//===-- ObjCLanguageRuntime.h ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjCLanguageRuntime_h_
#define liblldb_ObjCLanguageRuntime_h_

// C Includes
// C++ Includes
#include <map>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Target/LanguageRuntime.h"

namespace lldb_private {
    
class ClangUtilityFunction;

class ObjCLanguageRuntime :
    public LanguageRuntime
{
public:
    virtual
    ~ObjCLanguageRuntime();
    
    virtual lldb::LanguageType
    GetLanguageType () const
    {
        return lldb::eLanguageTypeObjC;
    }
    
    virtual bool
    IsModuleObjCLibrary (const lldb::ModuleSP &module_sp) = 0;
    
    virtual bool
    ReadObjCLibrary (const lldb::ModuleSP &module_sp) = 0;
    
    virtual bool
    HasReadObjCLibrary () = 0;
    
    virtual lldb::ThreadPlanSP
    GetStepThroughTrampolinePlan (Thread &thread, bool stop_others) = 0;
    
    lldb::addr_t
    LookupInMethodCache (lldb::addr_t class_addr, lldb::addr_t sel);

    void
    AddToMethodCache (lldb::addr_t class_addr, lldb::addr_t sel, lldb::addr_t impl_addr);
    
    virtual ClangUtilityFunction *
    CreateObjectChecker (const char *) = 0;
    
protected:
    //------------------------------------------------------------------
    // Classes that inherit from ObjCLanguageRuntime can see and modify these
    //------------------------------------------------------------------
    ObjCLanguageRuntime(Process *process);
private:
    // We keep a map of <Class,Selector>->Implementation so we don't have to call the resolver
    // function over and over.
    
    // FIXME: We need to watch for the loading of Protocols, and flush the cache for any
    // class that we see so changed.
    
    struct ClassAndSel
    {
        ClassAndSel()
        {
            sel_addr = LLDB_INVALID_ADDRESS;
            class_addr = LLDB_INVALID_ADDRESS;
        }
        ClassAndSel (lldb::addr_t in_sel_addr, lldb::addr_t in_class_addr) :
            class_addr (in_class_addr),
            sel_addr(in_sel_addr)
        {
        }
        bool operator== (const ClassAndSel &rhs)
        {
            if (class_addr == rhs.class_addr
                && sel_addr == rhs.sel_addr)
                return true;
            else
                return false;
        }
        
        bool operator< (const ClassAndSel &rhs) const
        {
            if (class_addr < rhs.class_addr)
                return true;
            else if (class_addr > rhs.class_addr)
                return false;
            else
            {
                if (sel_addr < rhs.sel_addr)
                    return true;
                else
                    return false;
            }
        }
        
        lldb::addr_t class_addr;
        lldb::addr_t sel_addr;
    };

    typedef std::map<ClassAndSel,lldb::addr_t> MsgImplMap;
    MsgImplMap m_impl_cache;        

    DISALLOW_COPY_AND_ASSIGN (ObjCLanguageRuntime);
};

} // namespace lldb_private

#endif  // liblldb_ObjCLanguageRuntime_h_
