//===-- SBSymbol.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

SBSymbol::SBSymbol () :
    m_opaque_ptr (NULL)
{
}

SBSymbol::SBSymbol (lldb_private::Symbol *lldb_object_ptr) :
    m_opaque_ptr (lldb_object_ptr)
{
}

SBSymbol::SBSymbol (const lldb::SBSymbol &rhs) :
    m_opaque_ptr (rhs.m_opaque_ptr)
{
}

const SBSymbol &
SBSymbol::operator = (const SBSymbol &rhs)
{
    m_opaque_ptr = rhs.m_opaque_ptr;
    return *this;
}

SBSymbol::~SBSymbol ()
{
    m_opaque_ptr = NULL;
}

void
SBSymbol::SetSymbol (lldb_private::Symbol *lldb_object_ptr)
{
    m_opaque_ptr = lldb_object_ptr;
}

bool
SBSymbol::IsValid () const
{
    return m_opaque_ptr != NULL;
}

const char *
SBSymbol::GetName() const
{
    const char *name = NULL;
    if (m_opaque_ptr)
        name = m_opaque_ptr->GetMangled().GetName().AsCString();

    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBSymbol(%p)::GetName () => \"%s\"", m_opaque_ptr, name ? name : "");
    return name;
}

const char *
SBSymbol::GetMangledName () const
{
    const char *name = NULL;
    if (m_opaque_ptr)
        name = m_opaque_ptr->GetMangled().GetMangledName().AsCString();
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));
    if (log)
        log->Printf ("SBSymbol(%p)::GetMangledName () => \"%s\"", m_opaque_ptr, name ? name : "");

    return name;
}


bool
SBSymbol::operator == (const SBSymbol &rhs) const
{
    return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool
SBSymbol::operator != (const SBSymbol &rhs) const
{
    return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool
SBSymbol::GetDescription (SBStream &description)
{
    if (m_opaque_ptr)
    {
        description.ref();
        m_opaque_ptr->GetDescription (description.get(), 
                                      lldb::eDescriptionLevelFull, NULL);
    }
    else
        description.Printf ("No value");
    
    return true;
}



SBInstructionList
SBSymbol::GetInstructions (SBTarget target)
{
    SBInstructionList sb_instructions;
    if (m_opaque_ptr)
    {
        Mutex::Locker api_locker;
        ExecutionContext exe_ctx;
        if (target.IsValid())
        {
            api_locker.Reset (target->GetAPIMutex().GetMutex());
            target->CalculateExecutionContext (exe_ctx);
        }
        const AddressRange *symbol_range = m_opaque_ptr->GetAddressRangePtr();
        if (symbol_range)
        {
            Module *module = symbol_range->GetBaseAddress().GetModule();
            if (module)
            {
                sb_instructions.SetDisassembler (Disassembler::DisassembleRange (module->GetArchitecture (),
                                                                                 exe_ctx,
                                                                                 *symbol_range));
            }
        }
    }
    return sb_instructions;
}

lldb_private::Symbol *
SBSymbol::get ()
{
    return m_opaque_ptr;
}

void
SBSymbol::reset (lldb_private::Symbol *symbol)
{
    m_opaque_ptr = symbol;
}

SBAddress
SBSymbol::GetStartAddress ()
{
    SBAddress addr;
    if (m_opaque_ptr)
    {
        // Make sure the symbol is an address based symbol first:
        AddressRange *symbol_arange_ptr = m_opaque_ptr->GetAddressRangePtr();
        if (symbol_arange_ptr)
        {
            addr.SetAddress (&symbol_arange_ptr->GetBaseAddress());
        }
    }
    return addr;
}

SBAddress
SBSymbol::GetEndAddress ()
{
    SBAddress addr;
    if (m_opaque_ptr)
    {
        AddressRange *symbol_arange_ptr = m_opaque_ptr->GetAddressRangePtr();
        if (symbol_arange_ptr)
        {
            addr_t byte_size = symbol_arange_ptr->GetByteSize();
            if (byte_size > 0)
            {
                addr.SetAddress (&symbol_arange_ptr->GetBaseAddress());
                addr->Slide (byte_size);
            }
        }
    }
    return addr;
}

uint32_t
SBSymbol::GetPrologueByteSize ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->GetPrologueByteSize();
    return 0;
}
