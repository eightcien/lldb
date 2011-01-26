//===-- AppleObjCTrampolineHandler.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AppleObjCTrampolineHandler.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "AppleThreadPlanStepThroughObjCTrampoline.h"

#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/ConstString.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/FileSpec.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"

using namespace lldb;
using namespace lldb_private;

const char *AppleObjCTrampolineHandler::g_lookup_implementation_function_name = "__lldb_objc_find_implementation_for_selector";
const char *AppleObjCTrampolineHandler::g_lookup_implementation_function_code = "                               \n\
extern \"C\"                                                                                                    \n\
{                                                                                                               \n\
    extern void *class_getMethodImplementation(void *objc_class, void *sel);                                    \n\
    extern void *class_getMethodImplementation_stret(void *objc_class, void *sel);                              \n\
    extern void * sel_getUid(char *name);                                                                       \n\
    extern int printf(const char *format, ...);                                                                 \n\
}                                                                                                               \n\
extern \"C\" void * __lldb_objc_find_implementation_for_selector (void *object,                                 \n\
                                                    void *sel,                                                  \n\
                                                    int is_stret,                                               \n\
                                                    int is_super,                                               \n\
                                                    int is_super2,                                              \n\
                                                    int is_fixup,                                               \n\
                                                    int is_fixed,                                               \n\
                                                    int debug)                                                  \n\
{                                                                                                               \n\
    struct __lldb_imp_return_struct                                                                             \n\
    {                                                                                                           \n\
        void *class_addr;                                                                                       \n\
        void *sel_addr;                                                                                         \n\
        void *impl_addr;                                                                                        \n\
    };                                                                                                          \n\
                                                                                                                \n\
    struct __lldb_objc_class {                                                                                  \n\
        void *isa;                                                                                              \n\
        void *super_ptr;                                                                                        \n\
    };                                                                                                          \n\
    struct __lldb_objc_super {                                                                                  \n\
        void *reciever;                                                                                         \n\
        struct __lldb_objc_class *class_ptr;                                                                    \n\
    };                                                                                                          \n\
    struct __lldb_msg_ref {                                                                                     \n\
        void *dont_know;                                                                                        \n\
        void *sel;                                                                                              \n\
    };                                                                                                          \n\
                                                                                                                \n\
    struct __lldb_imp_return_struct return_struct;                                                              \n\
                                                                                                                \n\
    if (debug)                                                                                                  \n\
        printf (\"\\n*** Called with obj: 0x%p sel: 0x%p is_stret: %d is_super: %d, \"                          \n\
                \"is_super2: %d, is_fixup: %d, is_fixed: %d\\n\",                                               \n\
                 object, sel, is_stret, is_super, is_super2, is_fixup, is_fixed);                               \n\
    if (is_super)                                                                                               \n\
    {                                                                                                           \n\
        if (is_super2)                                                                                          \n\
        {                                                                                                       \n\
            return_struct.class_addr = ((__lldb_objc_super *) object)->class_ptr->super_ptr;                    \n\
        }                                                                                                       \n\
        else                                                                                                    \n\
        {                                                                                                       \n\
            return_struct.class_addr = ((__lldb_objc_super *) object)->class_ptr;                               \n\
        }                                                                                                       \n\
    }                                                                                                           \n\
    else                                                                                                        \n\
    {                                                                                                           \n\
        void *class_ptr = (void *) [(id) object class];                                                         \n\
        if (class_ptr == object)                                                                                \n\
        {                                                                                                       \n\
            struct __lldb_objc_class *class_as_class_struct = (struct __lldb_objc_class *) class_ptr;           \n\
            if (debug)                                                                                          \n\
                printf (\"Found a class object, need to return the meta class 0x%p -> 0x%p\\n\",                \n\
                        class_ptr, class_as_class_struct->isa);                                                 \n\
            return_struct.class_addr = class_as_class_struct->isa;                                              \n\
        }                                                                                                       \n\
        else                                                                                                    \n\
        {                                                                                                       \n\
            if (debug)                                                                                          \n\
                printf (\"[object class] returned: 0x%p.\\n\", class_ptr);                                      \n\
            return_struct.class_addr = class_ptr;                                                               \n\
        }                                                                                                       \n\
    }                                                                                                           \n\
                                                                                                                \n\
    if (is_fixup)                                                                                               \n\
    {                                                                                                           \n\
        if (is_fixed)                                                                                           \n\
        {                                                                                                       \n\
            return_struct.sel_addr = ((__lldb_msg_ref *) sel)->sel;                                             \n\
        }                                                                                                       \n\
        else                                                                                                    \n\
        {                                                                                                       \n\
            char *sel_name = (char *) ((__lldb_msg_ref *) sel)->sel;                                            \n\
            return_struct.sel_addr = sel_getUid (sel_name);                                                     \n\
            if (debug)                                                                                          \n\
                printf (\"\\n*** Got fixed up selector: 0x%p for name %s.\\n\",                                 \n\
                        return_struct.sel_addr, sel_name);                                                      \n\
        }                                                                                                       \n\
    }                                                                                                           \n\
    else                                                                                                        \n\
    {                                                                                                           \n\
        return_struct.sel_addr = sel;                                                                           \n\
    }                                                                                                           \n\
                                                                                                                \n\
    if (is_stret)                                                                                               \n\
    {                                                                                                           \n\
        return_struct.impl_addr = class_getMethodImplementation_stret (return_struct.class_addr,                \n\
                                                                       return_struct.sel_addr);                 \n\
    }                                                                                                           \n\
    else                                                                                                        \n\
    {                                                                                                           \n\
        return_struct.impl_addr = class_getMethodImplementation (return_struct.class_addr,                      \n\
                                                                       return_struct.sel_addr);                 \n\
    }                                                                                                           \n\
    if (debug)                                                                                                  \n\
        printf (\"\\n*** Returning implementation: 0x%p.\\n\", return_struct.impl_addr);                        \n\
                                                                                                                \n\
    return return_struct.impl_addr;                                                                             \n\
}                                                                                                               \n\
";

AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::VTableRegion(AppleObjCVTables *owner, lldb::addr_t header_addr) :
    m_valid (true),
    m_owner(owner),
    m_header_addr (header_addr),
    m_code_start_addr(0),
    m_code_end_addr (0),
    m_next_region (0)
{
    SetUpRegion ();
}

void
AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::SetUpRegion()
{
    // The header looks like:
    //
    //   uint16_t headerSize
    //   uint16_t descSize
    //   uint32_t descCount
    //   void * next
    //
    // First read in the header:
    
    char memory_buffer[16];
    Process *process = m_owner->GetProcess();
    DataExtractor data(memory_buffer, sizeof(memory_buffer), 
                       process->GetByteOrder(), 
                       process->GetAddressByteSize());
    size_t actual_size = 8 + process->GetAddressByteSize();
    Error error;
    size_t bytes_read = process->ReadMemory (m_header_addr, memory_buffer, actual_size, error);
    if (bytes_read != actual_size)
    {
        m_valid = false;
        return;
    }
    
    uint32_t offset_ptr = 0;
    const uint16_t header_size = data.GetU16(&offset_ptr);
    const uint16_t descriptor_size = data.GetU16(&offset_ptr);
    const size_t num_descriptors = data.GetU32(&offset_ptr);
    
    m_next_region = data.GetPointer(&offset_ptr);
    
    // If the header size is 0, that means we've come in too early before this data is set up.
    // Set ourselves as not valid, and continue.
    if (header_size == 0 || num_descriptors == 0)
    {
        m_valid = false;
        return;
    }
    
    // Now read in all the descriptors:
    // The descriptor looks like:
    //
    // uint32_t offset
    // uint32_t flags
    //
    // Where offset is either 0 - in which case it is unused, or
    // it is the offset of the vtable code from the beginning of the descriptor record.
    // Below, we'll convert that into an absolute code address, since I don't want to have
    // to compute it over and over.
    
    // Ingest the whole descriptor array:
    const lldb::addr_t desc_ptr = m_header_addr + header_size;
    const size_t desc_array_size = num_descriptors * descriptor_size;
    DataBufferSP data_sp(new DataBufferHeap (desc_array_size, '\0'));
    uint8_t* dst = (uint8_t*)data_sp->GetBytes();

    DataExtractor desc_extractor (dst, desc_array_size,
                                  process->GetByteOrder(), 
                                  process->GetAddressByteSize());
    bytes_read = process->ReadMemory(desc_ptr, dst, desc_array_size, error);
    if (bytes_read != desc_array_size)
    {
        m_valid = false;
        return;
    }
    
    // The actual code for the vtables will be laid out consecutively, so I also
    // compute the start and end of the whole code block.

    offset_ptr = 0;
    m_code_start_addr = 0;
    m_code_end_addr = 0;

    for (int i = 0; i < num_descriptors; i++)
    {
        lldb::addr_t start_offset = offset_ptr;
        uint32_t offset = desc_extractor.GetU32 (&offset_ptr);
        uint32_t flags  = desc_extractor.GetU32 (&offset_ptr);
        lldb:addr_t code_addr = desc_ptr + start_offset + offset;
        m_descriptors.push_back (VTableDescriptor(flags, code_addr));
        
        if (m_code_start_addr == 0 || code_addr < m_code_start_addr)
            m_code_start_addr = code_addr;
        if (code_addr > m_code_end_addr)
            m_code_end_addr = code_addr;
            
        offset_ptr = start_offset + descriptor_size;
    }
    // Finally, a little bird told me that all the vtable code blocks are the same size.  
    // Let's compute the blocks and if they are all the same add the size to the code end address:
    lldb::addr_t code_size = 0;
    bool all_the_same = true;
    for (int i = 0; i < num_descriptors - 1; i++)
    {
        lldb::addr_t this_size = m_descriptors[i + 1].code_start - m_descriptors[i].code_start;
        if (code_size == 0)
            code_size = this_size;
        else
        {
            if (this_size != code_size)
                all_the_same = false;
            if (this_size > code_size)
                code_size = this_size;
        }
    }
    if (all_the_same)
        m_code_end_addr += code_size;
}

bool 
AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::AddressInRegion (lldb::addr_t addr, uint32_t &flags)
{
    if (!IsValid())
        return false;
        
    if (addr < m_code_start_addr || addr > m_code_end_addr)
        return false;
        
    std::vector<VTableDescriptor>::iterator pos, end = m_descriptors.end();
    for (pos = m_descriptors.begin(); pos != end; pos++)
    {
        if (addr <= (*pos).code_start)
        {
            flags = (*pos).flags;
            return true;
        }
    }
    return false;
}

void
AppleObjCTrampolineHandler::AppleObjCVTables::VTableRegion::Dump (Stream &s)
{
    s.Printf ("Header addr: 0x%llx Code start: 0x%llx Code End: 0x%llx Next: 0x%llx\n", 
              m_header_addr, m_code_start_addr, m_code_end_addr, m_next_region);
    size_t num_elements = m_descriptors.size();
    for (size_t i = 0; i < num_elements; i++)
    {
        s.Indent();
        s.Printf ("Code start: 0x%llx Flags: %d\n", m_descriptors[i].code_start, m_descriptors[i].flags);
    }
}
        
AppleObjCTrampolineHandler::AppleObjCVTables::AppleObjCVTables (ProcessSP &process_sp, ModuleSP &objc_module_sp) :
        m_process_sp(process_sp),
        m_trampoline_header(LLDB_INVALID_ADDRESS),
        m_trampolines_changed_bp_id(LLDB_INVALID_BREAK_ID),
        m_objc_module_sp(objc_module_sp)
{
    
}

AppleObjCTrampolineHandler::AppleObjCVTables::~AppleObjCVTables()
{
    if (m_trampolines_changed_bp_id != LLDB_INVALID_BREAK_ID)
        m_process_sp->GetTarget().RemoveBreakpointByID (m_trampolines_changed_bp_id);
}
    
bool
AppleObjCTrampolineHandler::AppleObjCVTables::InitializeVTableSymbols ()
{
    if (m_trampoline_header != LLDB_INVALID_ADDRESS)
        return true;
    Target &target = m_process_sp->GetTarget();
    
    ModuleList &modules = target.GetImages();
    size_t num_modules = modules.GetSize();
    if (!m_objc_module_sp)
    {
        for (size_t i = 0; i < num_modules; i++)
        {
            if (m_process_sp->GetObjCLanguageRuntime()->IsModuleObjCLibrary (modules.GetModuleAtIndex(i)))
            {
                m_objc_module_sp = modules.GetModuleAtIndex(i);
                break;
            }
        }
    }
    
    if (m_objc_module_sp)
    {
        ConstString trampoline_name ("gdb_objc_trampolines");
        const Symbol *trampoline_symbol = m_objc_module_sp->FindFirstSymbolWithNameAndType(trampoline_name, 
                                                                                   eSymbolTypeData);
        if (trampoline_symbol != NULL)
        {
            if (!trampoline_symbol->GetValue().IsValid())
                return false;
                
            m_trampoline_header = trampoline_symbol->GetValue().GetLoadAddress(&target);
            if (m_trampoline_header == LLDB_INVALID_ADDRESS)
                return false;
            
            // Next look up the "changed" symbol and set a breakpoint on that...
            ConstString changed_name ("gdb_objc_trampolines_changed");
            const Symbol *changed_symbol = m_objc_module_sp->FindFirstSymbolWithNameAndType(changed_name, 
                                                                                   eSymbolTypeCode);
            if (changed_symbol != NULL)
            {
                if (!changed_symbol->GetValue().IsValid())
                    return false;
                    
                lldb::addr_t changed_addr = changed_symbol->GetValue().GetLoadAddress(&target);
                if (changed_addr != LLDB_INVALID_ADDRESS)
                {
                    BreakpointSP trampolines_changed_bp_sp = target.CreateBreakpoint (changed_addr,
                                                                                          true);
                    if (trampolines_changed_bp_sp != NULL)
                    {
                        m_trampolines_changed_bp_id = trampolines_changed_bp_sp->GetID();
                        trampolines_changed_bp_sp->SetCallback (RefreshTrampolines, this, true);
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}
    
bool 
AppleObjCTrampolineHandler::AppleObjCVTables::RefreshTrampolines (void *baton, 
                                    StoppointCallbackContext *context, 
                                    lldb::user_id_t break_id, 
                                    lldb::user_id_t break_loc_id)
{
    AppleObjCVTables *vtable_handler = (AppleObjCVTables *) baton;
    if (vtable_handler->InitializeVTableSymbols())
    {
        // The Update function is called with the address of an added region.  So we grab that address, and
        // feed it into ReadRegions.  Of course, our friend the ABI will get the values for us.
        Process *process = context->exe_ctx.process;
        const ABI *abi = process->GetABI();
        
        ClangASTContext *clang_ast_context = process->GetTarget().GetScratchClangASTContext();
        ValueList argument_values;
        Value input_value;
        void *clang_void_ptr_type = clang_ast_context->GetVoidPtrType(false);
        input_value.SetValueType (Value::eValueTypeScalar);
        input_value.SetContext (Value::eContextTypeClangType, clang_void_ptr_type);
        argument_values.PushValue(input_value);
        
        bool success = abi->GetArgumentValues (*(context->exe_ctx.thread), argument_values);
        if (!success)
            return false;
            
        // Now get a pointer value from the zeroth argument.
        Error error;
        DataExtractor data;
        error = argument_values.GetValueAtIndex(0)->GetValueAsData(&(context->exe_ctx), clang_ast_context->getASTContext(), data, 0);
        uint32_t offset_ptr = 0;
        lldb::addr_t region_addr = data.GetPointer(&offset_ptr);
        
        if (region_addr != 0)
            vtable_handler->ReadRegions(region_addr);
    }
    return false;
}

bool
AppleObjCTrampolineHandler::AppleObjCVTables::ReadRegions ()
{
    // The no argument version reads the start region from the value of the gdb_regions_header, and 
    // gets started from there.
    
    m_regions.clear();
    if (!InitializeVTableSymbols())
        return false;
    char memory_buffer[8];
    DataExtractor data(memory_buffer, sizeof(memory_buffer), 
                       m_process_sp->GetByteOrder(), 
                       m_process_sp->GetAddressByteSize());
    Error error;
    size_t bytes_read = m_process_sp->ReadMemory (m_trampoline_header, memory_buffer, m_process_sp->GetAddressByteSize(), error);
    if (bytes_read != m_process_sp->GetAddressByteSize())
        return false;
        
    uint32_t offset_ptr = 0;
    lldb::addr_t region_addr = data.GetPointer(&offset_ptr);
    return ReadRegions (region_addr);
}

bool
AppleObjCTrampolineHandler::AppleObjCVTables::ReadRegions (lldb::addr_t region_addr)
{
    if (!m_process_sp)
        return false;
        
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));
    
    // We aren't starting at the trampoline symbol.
    InitializeVTableSymbols ();
    lldb::addr_t next_region = region_addr;
    
    // Read in the sizes of the headers.
    while (next_region != 0) 
    {
        m_regions.push_back (VTableRegion(this, next_region));
        if (!m_regions.back().IsValid())
        {
            m_regions.clear();
            return false;
        }
        if (log)
        {
            StreamString s;
            m_regions.back().Dump(s);
            log->Printf("Read vtable region: \n%s", s.GetData());
        }
        
        next_region = m_regions.back().GetNextRegionAddr();
    }
    
    return true;
}
    
bool
AppleObjCTrampolineHandler::AppleObjCVTables::IsAddressInVTables (lldb::addr_t addr, uint32_t &flags)
{
    region_collection::iterator pos, end = m_regions.end();
    for (pos = m_regions.begin(); pos != end; pos++)
    {
        if ((*pos).AddressInRegion (addr, flags))
            return true;
    }
    return false;
}

const AppleObjCTrampolineHandler::DispatchFunction
AppleObjCTrampolineHandler::g_dispatch_functions[] =
{
    // NAME                              STRET  SUPER  SUPER2  FIXUP TYPE
    {"objc_msgSend",                     false, false, false, DispatchFunction::eFixUpNone    },
    {"objc_msgSend_fixup",               false, false, false, DispatchFunction::eFixUpToFix   },
    {"objc_msgSend_fixedup",             false, false, false, DispatchFunction::eFixUpFixed   },
    {"objc_msgSend_stret",               true,  false, false, DispatchFunction::eFixUpNone    },
    {"objc_msgSend_stret_fixup",         true,  false, false, DispatchFunction::eFixUpToFix   },
    {"objc_msgSend_stret_fixedup",       true,  false, false, DispatchFunction::eFixUpFixed   },
    {"objc_msgSend_fpret",               false, false, false, DispatchFunction::eFixUpNone    },
    {"objc_msgSend_fpret_fixup",         false, false, false, DispatchFunction::eFixUpToFix   },
    {"objc_msgSend_fpret_fixedup",       false, false, false, DispatchFunction::eFixUpFixed   },
    {"objc_msgSend_fp2ret",              false, false,  true, DispatchFunction::eFixUpNone    },
    {"objc_msgSend_fp2ret_fixup",        false, false,  true, DispatchFunction::eFixUpToFix   },
    {"objc_msgSend_fp2ret_fixedup",      false, false,  true, DispatchFunction::eFixUpFixed   },
    {"objc_msgSendSuper",                false, true,  false, DispatchFunction::eFixUpNone    },
    {"objc_msgSendSuper_stret",          true,  true,  false, DispatchFunction::eFixUpNone    },
    {"objc_msgSendSuper2",               false, true,   true, DispatchFunction::eFixUpNone    },
    {"objc_msgSendSuper2_fixup",         false, true,   true, DispatchFunction::eFixUpToFix   },
    {"objc_msgSendSuper2_fixedup",       false, true,   true, DispatchFunction::eFixUpFixed   },
    {"objc_msgSendSuper2_stret",         true,  true,   true, DispatchFunction::eFixUpNone    },
    {"objc_msgSendSuper2_stret_fixup",   true,  true,   true, DispatchFunction::eFixUpToFix   },
    {"objc_msgSendSuper2_stret_fixedup", true,  true,   true, DispatchFunction::eFixUpFixed   },
    {NULL}
};

AppleObjCTrampolineHandler::AppleObjCTrampolineHandler (ProcessSP process_sp, ModuleSP objc_module_sp) :
    m_process_sp (process_sp),
    m_objc_module_sp (objc_module_sp),
    m_impl_fn_addr (LLDB_INVALID_ADDRESS),
    m_impl_stret_fn_addr (LLDB_INVALID_ADDRESS),
    m_msg_forward_addr (LLDB_INVALID_ADDRESS)
{
    // Look up the known resolution functions:
    
    ConstString get_impl_name("class_getMethodImplementation");
    ConstString get_impl_stret_name("class_getMethodImplementation_stret");
    ConstString msg_forward_name("_objc_msgForward");
    ConstString msg_forward_stret_name("_objc_msgForward_stret");
    
    Target *target = m_process_sp ? &m_process_sp->GetTarget() : NULL;
    const Symbol *class_getMethodImplementation = m_objc_module_sp->FindFirstSymbolWithNameAndType (get_impl_name, eSymbolTypeCode);
    const Symbol *class_getMethodImplementation_stret = m_objc_module_sp->FindFirstSymbolWithNameAndType (get_impl_stret_name, eSymbolTypeCode);
    const Symbol *msg_forward = m_objc_module_sp->FindFirstSymbolWithNameAndType (msg_forward_name, eSymbolTypeCode);
    const Symbol *msg_forward_stret = m_objc_module_sp->FindFirstSymbolWithNameAndType (msg_forward_stret_name, eSymbolTypeCode);
    
    if (class_getMethodImplementation)
        m_impl_fn_addr = class_getMethodImplementation->GetValue().GetLoadAddress(target);
    if  (class_getMethodImplementation_stret)
        m_impl_stret_fn_addr = class_getMethodImplementation_stret->GetValue().GetLoadAddress(target);
    if (msg_forward)
        m_msg_forward_addr = msg_forward->GetValue().GetLoadAddress(target);
    if  (msg_forward_stret)
        m_msg_forward_stret_addr = msg_forward_stret->GetValue().GetLoadAddress(target);
    
    // FIXME: Do some kind of logging here.
    if (m_impl_fn_addr == LLDB_INVALID_ADDRESS || m_impl_stret_fn_addr == LLDB_INVALID_ADDRESS)
        return;
        
    // Look up the addresses for the objc dispatch functions and cache them.  For now I'm inspecting the symbol
    // names dynamically to figure out how to dispatch to them.  If it becomes more complicated than this we can 
    // turn the g_dispatch_functions char * array into a template table, and populate the DispatchFunction map
    // from there.

    for (int i = 0; g_dispatch_functions[i].name != NULL; i++)
    {
        ConstString name_const_str(g_dispatch_functions[i].name);
        const Symbol *msgSend_symbol = m_objc_module_sp->FindFirstSymbolWithNameAndType (name_const_str, eSymbolTypeCode);
        if (msgSend_symbol)
        {
            // FixMe: Make g_dispatch_functions static table of DispatchFunctions, and have the map be address->index.
            // Problem is we also need to lookup the dispatch function.  For now we could have a side table of stret & non-stret
            // dispatch functions.  If that's as complex as it gets, we're fine.
            
            lldb::addr_t sym_addr = msgSend_symbol->GetValue().GetLoadAddress(target);
            
            m_msgSend_map.insert(std::pair<lldb::addr_t, int>(sym_addr, i));
        }
    }
    
    // Build our vtable dispatch handler here:
    m_vtables_ap.reset(new AppleObjCVTables(process_sp, m_objc_module_sp));
    if (m_vtables_ap.get())
        m_vtables_ap->ReadRegions();        
}

ThreadPlanSP
AppleObjCTrampolineHandler::GetStepThroughDispatchPlan (Thread &thread, bool stop_others)
{
    ThreadPlanSP ret_plan_sp;
    lldb::addr_t curr_pc = thread.GetRegisterContext()->GetPC();
    
    DispatchFunction this_dispatch;
    bool found_it = false;
    
    // First step is to look and see if we are in one of the known ObjC dispatch functions.  We've already compiled
    // a table of same, so consult it.
    
    MsgsendMap::iterator pos;
    pos = m_msgSend_map.find (curr_pc);
    if (pos != m_msgSend_map.end())
    {
        this_dispatch = g_dispatch_functions[(*pos).second];
        found_it = true;
    }
    
    // Next check to see if we are in a vtable region:
    
    if (!found_it)
    {
        uint32_t flags;
        if (m_vtables_ap.get())
        {
            found_it = m_vtables_ap->IsAddressInVTables (curr_pc, flags);
            if (found_it)
            {
                this_dispatch.name = "vtable";
                this_dispatch.stret_return 
                        = (flags & AppleObjCVTables::eOBJC_TRAMPOLINE_STRET) == AppleObjCVTables::eOBJC_TRAMPOLINE_STRET;
                this_dispatch.is_super = false;
                this_dispatch.is_super2 = false;
                this_dispatch.fixedup = DispatchFunction::eFixUpFixed;
            }
        }
    }
    
    if (found_it)
    {
        LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_STEP));

        // We are decoding a method dispatch.  
        // First job is to pull the arguments out:
        
        lldb::StackFrameSP thread_cur_frame = thread.GetStackFrameAtIndex(0);
        
        Process *process = thread.CalculateProcess();
        const ABI *abi = process->GetABI();
        if (abi == NULL)
            return ret_plan_sp;
            
        Target *target = thread.CalculateTarget();
        
        ClangASTContext *clang_ast_context = target->GetScratchClangASTContext();
        ValueList argument_values;
        Value void_ptr_value;
        lldb::clang_type_t clang_void_ptr_type = clang_ast_context->GetVoidPtrType(false);
        void_ptr_value.SetValueType (Value::eValueTypeScalar);
        void_ptr_value.SetContext (Value::eContextTypeClangType, clang_void_ptr_type);
        
        int obj_index;
        int sel_index;
        
        // If this is a struct return dispatch, then the first argument is the
        // return struct pointer, and the object is the second, and the selector is the third.
        // Otherwise the object is the first and the selector the second.
        if (this_dispatch.stret_return)
        {
            obj_index = 1;
            sel_index = 2;
            argument_values.PushValue(void_ptr_value);
            argument_values.PushValue(void_ptr_value);
            argument_values.PushValue(void_ptr_value);
        }
        else
        {
            obj_index = 0;
            sel_index = 1;
            argument_values.PushValue(void_ptr_value);
            argument_values.PushValue(void_ptr_value);
        }

        
        bool success = abi->GetArgumentValues (thread, argument_values);
        if (!success)
            return ret_plan_sp;
            
        // Pull the class out of the Object and see if we've already cached this method call,
        // If so we can push a run-to-address plan directly.  Otherwise we have to figure out where
        // the implementation lives.

        Value isa_value(*(argument_values.GetValueAtIndex(obj_index)));

        // This is a little cheesy, but since object->isa is the first field,
        // making the object value a load address value and resolving it will get
        // the pointer sized data pointed to by that value...
        ExecutionContext exe_ctx;
        thread.CalculateExecutionContext (exe_ctx);

        isa_value.SetValueType(Value::eValueTypeLoadAddress);
        isa_value.ResolveValue(&exe_ctx, clang_ast_context->getASTContext());
        lldb::addr_t isa_addr = isa_value.GetScalar().ULongLong();
        lldb::addr_t sel_addr = argument_values.GetValueAtIndex(sel_index)->GetScalar().ULongLong();

        if (log)
        {
            log->Printf("Resolving method call for class - 0x%llx and selector - 0x%llx",
                        isa_addr, sel_addr);
        }
        ObjCLanguageRuntime *objc_runtime = m_process_sp->GetObjCLanguageRuntime ();
        assert(objc_runtime != NULL);
        
        lldb::addr_t impl_addr = objc_runtime->LookupInMethodCache (isa_addr, sel_addr);
                                                
        if (impl_addr == LLDB_INVALID_ADDRESS)
        {            
            // We haven't seen this class/selector pair yet.  Look it up.
            StreamString errors;
            Address impl_code_address;
            
            ValueList dispatch_values;
            
            // We've will inject a little function in the target that takes the object, selector and some flags,
            // and figures out the implementation.  Looks like:
            //      void *__lldb_objc_find_implementation_for_selector (void *object, 
            //                                                          void *sel, 
            //                                                          int is_stret, 
            //                                                          int is_super, 
            //                                                          int is_super2, 
            //                                                          int is_fixup, 
            //                                                          int is_fixed,
            //                                                          int debug)
            // So set up the arguments for that call.
            
            dispatch_values.PushValue (*(argument_values.GetValueAtIndex(obj_index)));
            dispatch_values.PushValue (*(argument_values.GetValueAtIndex(sel_index)));
            
            Value flag_value;
            lldb::clang_type_t clang_int_type = clang_ast_context->GetBuiltinTypeForEncodingAndBitSize(lldb::eEncodingSint, 32);
            flag_value.SetValueType (Value::eValueTypeScalar);
            flag_value.SetContext (Value::eContextTypeClangType, clang_int_type);
            
            if (this_dispatch.stret_return)
                flag_value.GetScalar() = 1;
            else
                flag_value.GetScalar() = 0;
            dispatch_values.PushValue (flag_value);
                    
            if (this_dispatch.is_super)
                flag_value.GetScalar() = 1;
            else
                flag_value.GetScalar() = 0;
            dispatch_values.PushValue (flag_value);
                    
            if (this_dispatch.is_super2)
                flag_value.GetScalar() = 1;
            else
                flag_value.GetScalar() = 0;
            dispatch_values.PushValue (flag_value);
                    
            switch (this_dispatch.fixedup)
            {
              case DispatchFunction::eFixUpNone:
                 flag_value.GetScalar() = 0;
                 dispatch_values.PushValue (flag_value);
                 dispatch_values.PushValue (flag_value);
                 break;
              case DispatchFunction::eFixUpFixed:
                 flag_value.GetScalar() = 1;
                 dispatch_values.PushValue (flag_value);
                 flag_value.GetScalar() = 1;
                 dispatch_values.PushValue (flag_value);
                 break;
              case DispatchFunction::eFixUpToFix:
                 flag_value.GetScalar() = 1;
                 dispatch_values.PushValue (flag_value);
                 flag_value.GetScalar() = 0;
                 dispatch_values.PushValue (flag_value);
                 break;
            }
            if (log) 
                flag_value.GetScalar() = 1;
            else
                flag_value.GetScalar() = 0;  // FIXME - Set to 0 when debugging is done.
                 dispatch_values.PushValue (flag_value);

            // Now, if we haven't already, make and insert the function as a ClangUtilityFunction, and make and insert 
            // it's runner ClangFunction.
            { 
                // Scope for mutex locker:
                Mutex::Locker locker(m_impl_function_mutex);
                
                // First stage is to make the ClangUtility to hold our injected function:

#define USE_BUILTIN_FUNCTION 0  // Define this to 1 and we will use the get_implementation function found in the target.
                                // This is useful for debugging additions to the get_impl function 'cause you don't have
                                // to bother with string-ifying the code into g_lookup_implementation_function_code.
                
                if (USE_BUILTIN_FUNCTION)
                {
                    ConstString our_utility_function_name("__lldb_objc_find_implementation_for_selector");
                    SymbolContextList sc_list;
                    exe_ctx.target->GetImages().FindSymbolsWithNameAndType (our_utility_function_name, eSymbolTypeCode, sc_list);
                    if (sc_list.GetSize() == 1)
                    {
                        SymbolContext sc;
                        sc_list.GetContextAtIndex(0, sc);
                        if (sc.symbol != NULL)
                            impl_code_address = sc.symbol->GetValue();
                            
                        //lldb::addr_t addr = impl_code_address.GetLoadAddress (exe_ctx.target);
                        //printf ("Getting address for our_utility_function: 0x%llx.\n", addr);
                    }
                    else
                    {
                        //printf ("Could not find implementation function address.\n");
                        return ret_plan_sp;
                    }
                }
                else if (!m_impl_code.get())
                {
                    m_impl_code.reset (new ClangUtilityFunction (g_lookup_implementation_function_code,
                                                                 g_lookup_implementation_function_name));
                    if (!m_impl_code->Install(errors, exe_ctx))
                    {
                        if (log)
                            log->Printf ("Failed to install implementation lookup: %s.", errors.GetData());
                        m_impl_code.reset();
                        return ret_plan_sp;
                    }
                    impl_code_address.Clear();
                    impl_code_address.SetOffset(m_impl_code->StartAddress());
                }
                else
                {
                    impl_code_address.Clear();
                    impl_code_address.SetOffset(m_impl_code->StartAddress());
                }

                // Next make the runner function for our implementation utility function.
                if (!m_impl_function.get())
                {
                     m_impl_function.reset(new ClangFunction(process->GetTargetTriple().GetCString(), 
                                                             clang_ast_context, 
                                                             clang_void_ptr_type, 
                                                             impl_code_address, 
                                                             dispatch_values));
                    
                    errors.Clear();        
                    unsigned num_errors = m_impl_function->CompileFunction(errors);
                    if (num_errors)
                    {
                        if (log)
                            log->Printf ("Error compiling function: \"%s\".", errors.GetData());
                        return ret_plan_sp;
                    }
                    
                    errors.Clear();
                    if (!m_impl_function->WriteFunctionWrapper(exe_ctx, errors))
                    {
                        if (log)
                            log->Printf ("Error Inserting function: \"%s\".", errors.GetData());
                        return ret_plan_sp;
                    }
                }
                
            } 
            
            errors.Clear();
            
            // Now write down the argument values for this particular call.  This looks like it might be a race condition
            // if other threads were calling into here, but actually it isn't because we allocate a new args structure for
            // this call by passing args_addr = LLDB_INVALID_ADDRESS...

            lldb::addr_t args_addr = LLDB_INVALID_ADDRESS;
            if (!m_impl_function->WriteFunctionArguments (exe_ctx, args_addr, impl_code_address, dispatch_values, errors))
                return ret_plan_sp;
        
            ret_plan_sp.reset (new AppleThreadPlanStepThroughObjCTrampoline (thread, this, args_addr,
                                                                        dispatch_values.GetValueAtIndex(0)->GetScalar().ULongLong(),
                                                                        isa_addr, sel_addr,
                                                                        stop_others));
            if (log)
            {
                StreamString s;
                ret_plan_sp->GetDescription(&s, eDescriptionLevelFull);
                log->Printf("Using ObjC step plan: %s.\n", s.GetData());
            }
        }
        else
        {
            if (log)
                log->Printf ("Found implementation address in cache: 0x%llx", impl_addr);
                 
            ret_plan_sp.reset (new ThreadPlanRunToAddress (thread, impl_addr, stop_others));
        }
    }
    
    return ret_plan_sp;
}

ClangFunction *
AppleObjCTrampolineHandler::GetLookupImplementationWrapperFunction ()
{
    return m_impl_function.get();
}
