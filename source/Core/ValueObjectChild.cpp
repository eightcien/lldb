//===-- ValueObjectChild.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectChild.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObjectList.h"

#include "lldb/Symbol/ClangASTType.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"

#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

ValueObjectChild::ValueObjectChild
(
    ValueObject* parent,
    clang::ASTContext *clang_ast,
    void *clang_type,
    const ConstString &name,
    uint32_t byte_size,
    int32_t byte_offset,
    uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset,
    bool is_base_class
) :
    ValueObject (parent),
    m_clang_ast (clang_ast),
    m_clang_type (clang_type),
    m_byte_size (byte_size),
    m_byte_offset (byte_offset),
    m_bitfield_bit_size (bitfield_bit_size),
    m_bitfield_bit_offset (bitfield_bit_offset),
    m_is_base_class (is_base_class)
{
    m_name = name;
}

ValueObjectChild::~ValueObjectChild()
{
}

lldb::ValueType
ValueObjectChild::GetValueType() const
{
    return m_parent->GetValueType();
}

uint32_t
ValueObjectChild::CalculateNumChildren()
{
    return ClangASTContext::GetNumChildren (m_clang_type, true);
}

ConstString
ValueObjectChild::GetTypeName()
{
    if (m_type_name.IsEmpty())
    {
        m_type_name = ClangASTType::GetClangTypeName (GetClangType());
        if (m_type_name)
        {
            if (m_bitfield_bit_size > 0)
            {
                const char *clang_type_name = m_type_name.AsCString();
                if (clang_type_name)
                {
                    std::vector<char> bitfield_type_name (strlen(clang_type_name) + 32, 0);
                    ::snprintf (&bitfield_type_name.front(), bitfield_type_name.size(), "%s:%u", clang_type_name, m_bitfield_bit_size);
                    m_type_name.SetCString(&bitfield_type_name.front());
                }
            }
        }
    }
    return m_type_name;
}

void
ValueObjectChild::UpdateValue (ExecutionContextScope *exe_scope)
{
    m_error.Clear();
    SetValueIsValid (false);
    ValueObject* parent = m_parent;
    if (parent)
    {
        if (parent->UpdateValueIfNeeded(exe_scope))
        {
            m_value.SetContext(Value::eContextTypeClangType, m_clang_type);

            // Copy the parent scalar value and the scalar value type
            m_value.GetScalar() = parent->GetValue().GetScalar();
            Value::ValueType value_type = parent->GetValue().GetValueType();
            m_value.SetValueType (value_type);

            if (ClangASTContext::IsPointerOrReferenceType (parent->GetClangType()))
            {
                uint32_t offset = 0;
                m_value.GetScalar() = parent->GetDataExtractor().GetPointer(&offset);

                lldb::addr_t addr = m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);

                if (addr == LLDB_INVALID_ADDRESS)
                {
                    m_error.SetErrorString ("parent address is invalid.");
                }
                else if (addr == 0)
                {
                    m_error.SetErrorString ("parent is NULL");
                }
                else
                {
                    m_value.GetScalar() += m_byte_offset;
                    if (m_pointers_point_to_load_addrs ||
                        value_type == Value::eValueTypeScalar ||
                        value_type == Value::eValueTypeFileAddress)
                        m_value.SetValueType (Value::eValueTypeLoadAddress);
                }
            }
            else
            {
                switch (value_type)
                {
                case Value::eValueTypeLoadAddress:
                case Value::eValueTypeFileAddress:
                case Value::eValueTypeHostAddress:
                    {
                        lldb::addr_t addr = m_value.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
                        if (addr == LLDB_INVALID_ADDRESS)
                        {
                            m_error.SetErrorString ("parent address is invalid.");
                        }
                        else if (addr == 0)
                        {
                            m_error.SetErrorString ("parent is NULL");
                        }
                        else
                        {
                            // Set this object's scalar value to the address of its
                            // value be adding its byte offset to the parent address
                            m_value.GetScalar() += GetByteOffset();
                        }
                    }
                    break;

                case Value::eValueTypeScalar:
                    // TODO: What if this is a register value? Do we try and
                    // extract the child value from within the parent data?
                    // Probably...
                default:
                    m_error.SetErrorString ("Parent has invalid value.");
                    break;
                }
            }

            if (m_error.Success())
            {
                ExecutionContext exe_ctx (exe_scope);
                m_error = m_value.GetValueAsData (&exe_ctx, GetClangAST (), m_data, 0);
            }
        }
        else
        {
            m_error.SetErrorStringWithFormat("Parent failed to evaluate: %s.\n", parent->GetError().AsCString());
        }
    }
    else
    {
        m_error.SetErrorString("ValueObjectChild has a NULL parent ValueObject.");
    }
}


bool
ValueObjectChild::IsInScope (StackFrame *frame)
{
    return m_parent->IsInScope (frame);
}
