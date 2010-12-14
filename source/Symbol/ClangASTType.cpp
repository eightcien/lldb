//===-- ClangASTType.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ClangASTType.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/RecordLayout.h"

#include "clang/Basic/Builtins.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"

#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

#include "lldb/Core/ConstString.h"
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Scalar.h"
#include "lldb/Core/Stream.h"
#include "lldb/Core/StreamString.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"

using namespace lldb;
using namespace lldb_private;

ClangASTType::~ClangASTType()
{
}

ConstString
ClangASTType::GetClangTypeName ()
{
    return GetClangTypeName (m_type);
}

ConstString
ClangASTType::GetClangTypeName (clang_type_t clang_type)
{
    ConstString clang_type_name;
    if (clang_type)
    {
        clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

        const clang::TypedefType *typedef_type = qual_type->getAs<clang::TypedefType>();
        if (typedef_type)
        {
            const clang::TypedefDecl *typedef_decl = typedef_type->getDecl();
            std::string clang_typedef_name (typedef_decl->getQualifiedNameAsString());
            if (!clang_typedef_name.empty())
                clang_type_name.SetCString (clang_typedef_name.c_str());
        }
        else
        {
            std::string type_name(qual_type.getAsString());
            if (!type_name.empty())
                clang_type_name.SetCString (type_name.c_str());
        }
    }
    else
    {
        clang_type_name.SetCString ("<invalid>");
    }

    return clang_type_name;
}


clang_type_t
ClangASTType::GetPointeeType ()
{
    return GetPointeeType (m_type);
}

clang_type_t
ClangASTType::GetPointeeType (clang_type_t clang_type)
{
    if (clang_type)
    {
        clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
        
        return qual_type.getTypePtr()->getPointeeType().getAsOpaquePtr();
    }
    return NULL;
}

lldb::Encoding
ClangASTType::GetEncoding (uint32_t &count)
{
    return GetEncoding(m_type, count);
}


lldb::Encoding
ClangASTType::GetEncoding (clang_type_t clang_type, uint32_t &count)
{
    count = 1;
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

    switch (qual_type->getTypeClass())
    {
    case clang::Type::FunctionNoProto:
    case clang::Type::FunctionProto:
        break;

    case clang::Type::IncompleteArray:
    case clang::Type::VariableArray:
        break;

    case clang::Type::ConstantArray:
        break;

    case clang::Type::ExtVector:
    case clang::Type::Vector:
        // TODO: Set this to more than one???
        break;

    case clang::Type::Builtin:
        switch (cast<clang::BuiltinType>(qual_type)->getKind())
        {
        default: assert(0 && "Unknown builtin type!");
        case clang::BuiltinType::Void:
            break;

        case clang::BuiltinType::Bool:
        case clang::BuiltinType::Char_S:
        case clang::BuiltinType::SChar:
        case clang::BuiltinType::WChar:
        case clang::BuiltinType::Char16:
        case clang::BuiltinType::Char32:
        case clang::BuiltinType::Short:
        case clang::BuiltinType::Int:
        case clang::BuiltinType::Long:
        case clang::BuiltinType::LongLong:
        case clang::BuiltinType::Int128:        return lldb::eEncodingSint;

        case clang::BuiltinType::Char_U:
        case clang::BuiltinType::UChar:
        case clang::BuiltinType::UShort:
        case clang::BuiltinType::UInt:
        case clang::BuiltinType::ULong:
        case clang::BuiltinType::ULongLong:
        case clang::BuiltinType::UInt128:       return lldb::eEncodingUint;

        case clang::BuiltinType::Float:
        case clang::BuiltinType::Double:
        case clang::BuiltinType::LongDouble:    return lldb::eEncodingIEEE754;
        
        case clang::BuiltinType::ObjCClass:
        case clang::BuiltinType::ObjCId:
        case clang::BuiltinType::ObjCSel:       return lldb::eEncodingUint;

        case clang::BuiltinType::NullPtr:       return lldb::eEncodingUint;
        }
        break;
    // All pointer types are represented as unsigned integer encodings.
    // We may nee to add a eEncodingPointer if we ever need to know the
    // difference
    case clang::Type::ObjCObjectPointer:
    case clang::Type::BlockPointer:
    case clang::Type::Pointer:
    case clang::Type::LValueReference:
    case clang::Type::RValueReference:
    case clang::Type::MemberPointer:            return lldb::eEncodingUint;
    // Complex numbers are made up of floats
    case clang::Type::Complex:
        count = 2;
        return lldb::eEncodingIEEE754;

    case clang::Type::ObjCInterface:            break;
    case clang::Type::Record:                   break;
    case clang::Type::Enum:                     return lldb::eEncodingSint;
    case clang::Type::Typedef:
            return GetEncoding(cast<clang::TypedefType>(qual_type)->getDecl()->getUnderlyingType().getAsOpaquePtr(), count);
        break;

    case clang::Type::TypeOfExpr:
    case clang::Type::TypeOf:
    case clang::Type::Decltype:
//    case clang::Type::QualifiedName:
    case clang::Type::TemplateSpecialization:   break;
    }
    count = 0;
    return lldb::eEncodingInvalid;
}

lldb::Format
ClangASTType::GetFormat ()
{
    return GetFormat (m_type);
}

lldb::Format
ClangASTType::GetFormat (clang_type_t clang_type)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

    switch (qual_type->getTypeClass())
    {
    case clang::Type::FunctionNoProto:
    case clang::Type::FunctionProto:
        break;

    case clang::Type::IncompleteArray:
    case clang::Type::VariableArray:
        break;

    case clang::Type::ConstantArray:
        break;

    case clang::Type::ExtVector:
    case clang::Type::Vector:
        break;

    case clang::Type::Builtin:
        switch (cast<clang::BuiltinType>(qual_type)->getKind())
        {
        default: assert(0 && "Unknown builtin type!");
        case clang::BuiltinType::Void:
            break;

        case clang::BuiltinType::Bool:          return lldb::eFormatBoolean;
        case clang::BuiltinType::Char_S:
        case clang::BuiltinType::SChar:
        case clang::BuiltinType::Char_U:
        case clang::BuiltinType::UChar:
        case clang::BuiltinType::WChar:         return lldb::eFormatChar;
        case clang::BuiltinType::Char16:        return lldb::eFormatUnicode16;
        case clang::BuiltinType::Char32:        return lldb::eFormatUnicode32;
        case clang::BuiltinType::UShort:        return lldb::eFormatUnsigned;
        case clang::BuiltinType::Short:         return lldb::eFormatDecimal;
        case clang::BuiltinType::UInt:          return lldb::eFormatUnsigned;
        case clang::BuiltinType::Int:           return lldb::eFormatDecimal;
        case clang::BuiltinType::ULong:         return lldb::eFormatUnsigned;
        case clang::BuiltinType::Long:          return lldb::eFormatDecimal;
        case clang::BuiltinType::ULongLong:     return lldb::eFormatUnsigned;
        case clang::BuiltinType::LongLong:      return lldb::eFormatDecimal;
        case clang::BuiltinType::UInt128:       return lldb::eFormatUnsigned;
        case clang::BuiltinType::Int128:        return lldb::eFormatDecimal;
        case clang::BuiltinType::Float:         return lldb::eFormatFloat;
        case clang::BuiltinType::Double:        return lldb::eFormatFloat;
        case clang::BuiltinType::LongDouble:    return lldb::eFormatFloat;
        case clang::BuiltinType::NullPtr:       
        case clang::BuiltinType::Overload:
        case clang::BuiltinType::Dependent:
        case clang::BuiltinType::UndeducedAuto:
        case clang::BuiltinType::ObjCId:
        case clang::BuiltinType::ObjCClass:
        case clang::BuiltinType::ObjCSel:       return lldb::eFormatHex;
        }
        break;
    case clang::Type::ObjCObjectPointer:        return lldb::eFormatHex;
    case clang::Type::BlockPointer:             return lldb::eFormatHex;
    case clang::Type::Pointer:                  return lldb::eFormatHex;
    case clang::Type::LValueReference:
    case clang::Type::RValueReference:          return lldb::eFormatHex;
    case clang::Type::MemberPointer:            break;
    case clang::Type::Complex:                  return lldb::eFormatComplex;
    case clang::Type::ObjCInterface:            break;
    case clang::Type::Record:                   break;
    case clang::Type::Enum:                     return lldb::eFormatEnum;
    case clang::Type::Typedef:
            return ClangASTType::GetFormat(cast<clang::TypedefType>(qual_type)->getDecl()->getUnderlyingType().getAsOpaquePtr());

    case clang::Type::TypeOfExpr:
    case clang::Type::TypeOf:
    case clang::Type::Decltype:
//    case clang::Type::QualifiedName:
    case clang::Type::TemplateSpecialization:   break;
    }
    // We don't know hot to display this type...
    return lldb::eFormatBytes;
}


void
ClangASTType::DumpValue
(
    ExecutionContext *exe_ctx,
    Stream *s,
    lldb::Format format,
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size,
    uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset,
    bool show_types,
    bool show_summary,
    bool verbose,
    uint32_t depth
)
{
    return DumpValue (m_ast, 
                      m_type,
                      exe_ctx,
                      s,
                      format,
                      data,
                      data_byte_offset,
                      data_byte_size,
                      bitfield_bit_size,
                      bitfield_bit_offset,
                      show_types,
                      show_summary,
                      verbose,
                      depth);
}
                      
#define DEPTH_INCREMENT 2
void
ClangASTType::DumpValue
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    ExecutionContext *exe_ctx,
    Stream *s,
    lldb::Format format,
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size,
    uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset,
    bool show_types,
    bool show_summary,
    bool verbose,
    uint32_t depth
)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    switch (qual_type->getTypeClass())
    {
    case clang::Type::Record:
        {
            const clang::RecordType *record_type = cast<clang::RecordType>(qual_type.getTypePtr());
            const clang::RecordDecl *record_decl = record_type->getDecl();
            assert(record_decl);
            uint32_t field_bit_offset = 0;
            uint32_t field_byte_offset = 0;
            const clang::ASTRecordLayout &record_layout = ast_context->getASTRecordLayout(record_decl);
            uint32_t child_idx = 0;


            const clang::CXXRecordDecl *cxx_record_decl = dyn_cast<clang::CXXRecordDecl>(record_decl);
            if (cxx_record_decl)
            {
                // We might have base classes to print out first
                clang::CXXRecordDecl::base_class_const_iterator base_class, base_class_end;
                for (base_class = cxx_record_decl->bases_begin(), base_class_end = cxx_record_decl->bases_end();
                     base_class != base_class_end;
                     ++base_class)
                {
                    const clang::CXXRecordDecl *base_class_decl = cast<clang::CXXRecordDecl>(base_class->getType()->getAs<clang::RecordType>()->getDecl());

                    // Skip empty base classes
                    if (verbose == false && ClangASTContext::RecordHasFields(base_class_decl) == false)
                        continue;

                    if (base_class->isVirtual())
                        field_bit_offset = record_layout.getVBaseClassOffset(base_class_decl).getQuantity();
                    else
                        field_bit_offset = record_layout.getBaseClassOffset(base_class_decl).getQuantity();
                    field_byte_offset = field_bit_offset / 8;
                    assert (field_bit_offset % 8 == 0);
                    if (child_idx == 0)
                        s->PutChar('{');
                    else
                        s->PutChar(',');

                    clang::QualType base_class_qual_type = base_class->getType();
                    std::string base_class_type_name(base_class_qual_type.getAsString());

                    // Indent and print the base class type name
                    s->Printf("\n%*s%s ", depth + DEPTH_INCREMENT, "", base_class_type_name.c_str());

                    std::pair<uint64_t, unsigned> base_class_type_info = ast_context->getTypeInfo(base_class_qual_type);

                    // Dump the value of the member
                    DumpValue (ast_context,                        // The clang AST context for this type
                               base_class_qual_type.getAsOpaquePtr(),// The clang type we want to dump
                               exe_ctx,
                               s,                                  // Stream to dump to
                               ClangASTType::GetFormat(base_class_qual_type.getAsOpaquePtr()), // The format with which to display the member
                               data,                               // Data buffer containing all bytes for this type
                               data_byte_offset + field_byte_offset,// Offset into "data" where to grab value from
                               base_class_type_info.first / 8,     // Size of this type in bytes
                               0,                                  // Bitfield bit size
                               0,                                  // Bitfield bit offset
                               show_types,                         // Boolean indicating if we should show the variable types
                               show_summary,                       // Boolean indicating if we should show a summary for the current type
                               verbose,                            // Verbose output?
                               depth + DEPTH_INCREMENT);           // Scope depth for any types that have children
                    
                    ++child_idx;
                }
            }
            uint32_t field_idx = 0;
            clang::RecordDecl::field_iterator field, field_end;
            for (field = record_decl->field_begin(), field_end = record_decl->field_end(); field != field_end; ++field, ++field_idx, ++child_idx)
            {
                // Print the starting squiggly bracket (if this is the
                // first member) or comman (for member 2 and beyong) for
                // the struct/union/class member.
                if (child_idx == 0)
                    s->PutChar('{');
                else
                    s->PutChar(',');

                // Indent
                s->Printf("\n%*s", depth + DEPTH_INCREMENT, "");

                clang::QualType field_type = field->getType();
                // Print the member type if requested
                // Figure out the type byte size (field_type_info.first) and
                // alignment (field_type_info.second) from the AST context.
                std::pair<uint64_t, unsigned> field_type_info = ast_context->getTypeInfo(field_type);
                assert(field_idx < record_layout.getFieldCount());
                // Figure out the field offset within the current struct/union/class type
                field_bit_offset = record_layout.getFieldOffset (field_idx);
                field_byte_offset = field_bit_offset / 8;
                uint32_t field_bitfield_bit_size = 0;
                uint32_t field_bitfield_bit_offset = 0;
                if (ClangASTContext::FieldIsBitfield (ast_context, *field, field_bitfield_bit_size))
                    field_bitfield_bit_offset = field_bit_offset % 8;

                if (show_types)
                {
                    std::string field_type_name(field_type.getAsString());
                    if (field_bitfield_bit_size > 0)
                        s->Printf("(%s:%u) ", field_type_name.c_str(), field_bitfield_bit_size);
                    else
                        s->Printf("(%s) ", field_type_name.c_str());
                }
                // Print the member name and equal sign
                s->Printf("%s = ", field->getNameAsString().c_str());


                // Dump the value of the member
                DumpValue (ast_context,                    // The clang AST context for this type
                           field_type.getAsOpaquePtr(),    // The clang type we want to dump
                           exe_ctx,
                           s,                              // Stream to dump to
                           ClangASTType::GetFormat(field_type.getAsOpaquePtr()),   // The format with which to display the member
                           data,                           // Data buffer containing all bytes for this type
                           data_byte_offset + field_byte_offset,// Offset into "data" where to grab value from
                           field_type_info.first / 8,      // Size of this type in bytes
                           field_bitfield_bit_size,        // Bitfield bit size
                           field_bitfield_bit_offset,      // Bitfield bit offset
                           show_types,                     // Boolean indicating if we should show the variable types
                           show_summary,                   // Boolean indicating if we should show a summary for the current type
                           verbose,                        // Verbose output?
                           depth + DEPTH_INCREMENT);       // Scope depth for any types that have children
            }

            // Indent the trailing squiggly bracket
            if (child_idx > 0)
                s->Printf("\n%*s}", depth, "");
        }
        return;

    case clang::Type::Enum:
        {
            const clang::EnumType *enum_type = cast<clang::EnumType>(qual_type.getTypePtr());
            const clang::EnumDecl *enum_decl = enum_type->getDecl();
            assert(enum_decl);
            clang::EnumDecl::enumerator_iterator enum_pos, enum_end_pos;
            uint32_t offset = data_byte_offset;
            const int64_t enum_value = data.GetMaxU64Bitfield(&offset, data_byte_size, bitfield_bit_size, bitfield_bit_offset);
            for (enum_pos = enum_decl->enumerator_begin(), enum_end_pos = enum_decl->enumerator_end(); enum_pos != enum_end_pos; ++enum_pos)
            {
                if (enum_pos->getInitVal() == enum_value)
                {
                    s->Printf("%s", enum_pos->getNameAsString().c_str());
                    return;
                }
            }
            // If we have gotten here we didn't get find the enumerator in the
            // enum decl, so just print the integer.
            s->Printf("%lli", enum_value);
        }
        return;

    case clang::Type::ConstantArray:
        {
            const clang::ConstantArrayType *array = cast<clang::ConstantArrayType>(qual_type.getTypePtr());
            bool is_array_of_characters = false;
            clang::QualType element_qual_type = array->getElementType();

            clang::Type *canonical_type = element_qual_type->getCanonicalTypeInternal().getTypePtr();
            if (canonical_type)
                is_array_of_characters = canonical_type->isCharType();

            const uint64_t element_count = array->getSize().getLimitedValue();

            std::pair<uint64_t, unsigned> field_type_info = ast_context->getTypeInfo(element_qual_type);

            uint32_t element_idx = 0;
            uint32_t element_offset = 0;
            uint64_t element_byte_size = field_type_info.first / 8;
            uint32_t element_stride = element_byte_size;

            if (is_array_of_characters)
            {
                s->PutChar('"');
                data.Dump(s, data_byte_offset, lldb::eFormatChar, element_byte_size, element_count, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0);
                s->PutChar('"');
                return;
            }
            else
            {
                lldb::Format element_format = ClangASTType::GetFormat(element_qual_type.getAsOpaquePtr());

                for (element_idx = 0; element_idx < element_count; ++element_idx)
                {
                    // Print the starting squiggly bracket (if this is the
                    // first member) or comman (for member 2 and beyong) for
                    // the struct/union/class member.
                    if (element_idx == 0)
                        s->PutChar('{');
                    else
                        s->PutChar(',');

                    // Indent and print the index
                    s->Printf("\n%*s[%u] ", depth + DEPTH_INCREMENT, "", element_idx);

                    // Figure out the field offset within the current struct/union/class type
                    element_offset = element_idx * element_stride;

                    // Dump the value of the member
                    DumpValue (ast_context,                    // The clang AST context for this type
                               element_qual_type.getAsOpaquePtr(), // The clang type we want to dump
                               exe_ctx,
                               s,                              // Stream to dump to
                               element_format,                 // The format with which to display the element
                               data,                           // Data buffer containing all bytes for this type
                               data_byte_offset + element_offset,// Offset into "data" where to grab value from
                               element_byte_size,              // Size of this type in bytes
                               0,                              // Bitfield bit size
                               0,                              // Bitfield bit offset
                               show_types,                     // Boolean indicating if we should show the variable types
                               show_summary,                   // Boolean indicating if we should show a summary for the current type
                               verbose,                        // Verbose output?
                               depth + DEPTH_INCREMENT);       // Scope depth for any types that have children
                }

                // Indent the trailing squiggly bracket
                if (element_idx > 0)
                    s->Printf("\n%*s}", depth, "");
            }
        }
        return;

    case clang::Type::Typedef:
        {
            clang::QualType typedef_qual_type = cast<clang::TypedefType>(qual_type)->getDecl()->getUnderlyingType();
            lldb::Format typedef_format = ClangASTType::GetFormat(typedef_qual_type.getAsOpaquePtr());
            std::pair<uint64_t, unsigned> typedef_type_info = ast_context->getTypeInfo(typedef_qual_type);
            uint64_t typedef_byte_size = typedef_type_info.first / 8;

            return DumpValue (ast_context,        // The clang AST context for this type
                              typedef_qual_type.getAsOpaquePtr(), // The clang type we want to dump
                              exe_ctx,
                              s,                  // Stream to dump to
                              typedef_format,     // The format with which to display the element
                              data,               // Data buffer containing all bytes for this type
                              data_byte_offset,   // Offset into "data" where to grab value from
                              typedef_byte_size,  // Size of this type in bytes
                              bitfield_bit_size,  // Bitfield bit size
                              bitfield_bit_offset,// Bitfield bit offset
                              show_types,         // Boolean indicating if we should show the variable types
                              show_summary,       // Boolean indicating if we should show a summary for the current type
                              verbose,            // Verbose output?
                              depth);             // Scope depth for any types that have children
        }
        break;

    default:
        // We are down the a scalar type that we just need to display.
        data.Dump(s, data_byte_offset, format, data_byte_size, 1, UINT32_MAX, LLDB_INVALID_ADDRESS, bitfield_bit_size, bitfield_bit_offset);

        if (show_summary)
            DumpSummary (ast_context, clang_type, exe_ctx, s, data, data_byte_offset, data_byte_size);
        break;
    }
}



bool
ClangASTType::DumpTypeValue
(
    Stream *s,
    lldb::Format format,
    const lldb_private::DataExtractor &data,
    uint32_t byte_offset,
    size_t byte_size,
    uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset
)
{
    return DumpTypeValue (m_ast,
                          m_type,
                          s,
                          format,
                          data,
                          byte_offset,
                          byte_size,
                          bitfield_bit_size,
                          bitfield_bit_offset);
}


bool
ClangASTType::DumpTypeValue
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    Stream *s,
    lldb::Format format,
    const lldb_private::DataExtractor &data,
    uint32_t byte_offset,
    size_t byte_size,
    uint32_t bitfield_bit_size,
    uint32_t bitfield_bit_offset
)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    if (ClangASTContext::IsAggregateType (clang_type))
    {
        return 0;
    }
    else
    {
        const clang::Type::TypeClass type_class = qual_type->getTypeClass();
        switch (type_class)
        {
        case clang::Type::Enum:
            {
                const clang::EnumType *enum_type = cast<clang::EnumType>(qual_type.getTypePtr());
                const clang::EnumDecl *enum_decl = enum_type->getDecl();
                assert(enum_decl);
                clang::EnumDecl::enumerator_iterator enum_pos, enum_end_pos;
                uint32_t offset = byte_offset;
                const int64_t enum_value = data.GetMaxU64Bitfield (&offset, byte_size, bitfield_bit_size, bitfield_bit_offset);
                for (enum_pos = enum_decl->enumerator_begin(), enum_end_pos = enum_decl->enumerator_end(); enum_pos != enum_end_pos; ++enum_pos)
                {
                    if (enum_pos->getInitVal() == enum_value)
                    {
                        s->PutCString (enum_pos->getNameAsString().c_str());
                        return true;
                    }
                }
                // If we have gotten here we didn't get find the enumerator in the
                // enum decl, so just print the integer.

                s->Printf("%lli", enum_value);
                return true;
            }
            break;

        case clang::Type::Typedef:
            {
                clang::QualType typedef_qual_type = cast<clang::TypedefType>(qual_type)->getDecl()->getUnderlyingType();
                lldb::Format typedef_format = ClangASTType::GetFormat(typedef_qual_type.getAsOpaquePtr());
                std::pair<uint64_t, unsigned> typedef_type_info = ast_context->getTypeInfo(typedef_qual_type);
                uint64_t typedef_byte_size = typedef_type_info.first / 8;

                return ClangASTType::DumpTypeValue(
                            ast_context,            // The clang AST context for this type
                            typedef_qual_type.getAsOpaquePtr(),     // The clang type we want to dump
                            s,
                            typedef_format,         // The format with which to display the element
                            data,                   // Data buffer containing all bytes for this type
                            byte_offset,            // Offset into "data" where to grab value from
                            typedef_byte_size,      // Size of this type in bytes
                            bitfield_bit_size,      // Size in bits of a bitfield value, if zero don't treat as a bitfield
                            bitfield_bit_offset);   // Offset in bits of a bitfield value if bitfield_bit_size != 0
            }
            break;

        default:
            // We are down the a scalar type that we just need to display.
            return data.Dump(s,
                             byte_offset,
                             format,
                             byte_size,
                             1,
                             UINT32_MAX,
                             LLDB_INVALID_ADDRESS,
                             bitfield_bit_size,
                             bitfield_bit_offset);
            break;
        }
    }
    return 0;
}



void
ClangASTType::DumpSummary
(
    ExecutionContext *exe_ctx,
    Stream *s,
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size
)
{
    return DumpSummary (m_ast,
                        m_type,
                        exe_ctx, 
                        s, 
                        data, 
                        data_byte_offset, 
                        data_byte_size);
}

void
ClangASTType::DumpSummary
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    ExecutionContext *exe_ctx,
    Stream *s,
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size
)
{
    uint32_t length = 0;
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    if (ClangASTContext::IsCStringType (clang_type, length))
    {

        if (exe_ctx && exe_ctx->process)
        {
            uint32_t offset = data_byte_offset;
            lldb::addr_t pointer_addresss = data.GetMaxU64(&offset, data_byte_size);
            std::vector<uint8_t> buf;
            if (length > 0)
                buf.resize (length);
            else
                buf.resize (256);

            lldb_private::DataExtractor cstr_data(&buf.front(), buf.size(), exe_ctx->process->GetByteOrder(), 4);
            buf.back() = '\0';
            size_t bytes_read;
            size_t total_cstr_len = 0;
            Error error;
            while ((bytes_read = exe_ctx->process->ReadMemory (pointer_addresss, &buf.front(), buf.size(), error)) > 0)
            {
                const size_t len = strlen((const char *)&buf.front());
                if (len == 0)
                    break;
                if (total_cstr_len == 0)
                    s->PutCString (" \"");
                cstr_data.Dump(s, 0, lldb::eFormatChar, 1, len, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0);
                total_cstr_len += len;
                if (len < buf.size())
                    break;
                pointer_addresss += total_cstr_len;
            }
            if (total_cstr_len > 0)
                s->PutChar ('"');
        }
    }
}

uint64_t
ClangASTType::GetClangTypeBitWidth ()
{
    return GetClangTypeBitWidth (m_ast, m_type);
}

uint64_t
ClangASTType::GetClangTypeBitWidth (clang::ASTContext *ast_context, clang_type_t clang_type)
{
    if (ast_context && clang_type)
        return ast_context->getTypeSize(clang::QualType::getFromOpaquePtr(clang_type));
    return 0;
}

size_t
ClangASTType::GetTypeBitAlign ()
{
    return GetTypeBitAlign (m_ast, m_type);
}

size_t
ClangASTType::GetTypeBitAlign (clang::ASTContext *ast_context, clang_type_t clang_type)
{
    if (ast_context && clang_type)
        return ast_context->getTypeAlign(clang::QualType::getFromOpaquePtr(clang_type));
    return 0;
}


bool
ClangASTType::IsDefined()
{
    return ClangASTType::IsDefined (m_type);
}

bool
ClangASTType::IsDefined (clang_type_t clang_type)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    clang::TagType *tag_type = dyn_cast<clang::TagType>(qual_type.getTypePtr());
    if (tag_type)
    {
        clang::TagDecl *tag_decl = tag_type->getDecl();
        if (tag_decl)
            return tag_decl->getDefinition() != NULL;
        return false;
    }
    else
    {
        clang::ObjCObjectType *objc_class_type = dyn_cast<clang::ObjCObjectType>(qual_type);
        if (objc_class_type)
        {
            clang::ObjCInterfaceDecl *class_interface_decl = objc_class_type->getInterface();
            if (class_interface_decl->isForwardDecl())
                return false;
        }
    }
    return true;
}

bool
ClangASTType::IsConst()
{
    return ClangASTType::IsConst (m_type);
}

bool
ClangASTType::IsConst (lldb::clang_type_t clang_type)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    
    return qual_type.isConstQualified();
}

void
ClangASTType::DumpTypeDescription (Stream *s)
{
    return DumpTypeDescription (m_ast, m_type, s);
}

// Dump the full description of a type. For classes this means all of the
// ivars and member functions, for structs/unions all of the members. 
void
ClangASTType::DumpTypeDescription (clang::ASTContext *ast_context, clang_type_t clang_type, Stream *s)
{
    if (clang_type)
    {
        clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

        llvm::SmallVector<char, 1024> buf;
        llvm::raw_svector_ostream llvm_ostrm (buf);

        clang::TagType *tag_type = dyn_cast<clang::TagType>(qual_type.getTypePtr());
        if (tag_type)
        {
            clang::TagDecl *tag_decl = tag_type->getDecl();
            if (tag_decl)
                tag_decl->print(llvm_ostrm, 0);
        }
        else
        {
            const clang::Type::TypeClass type_class = qual_type->getTypeClass();
            switch (type_class)
            {
            case clang::Type::ObjCObject:
            case clang::Type::ObjCInterface:
                {
                    clang::ObjCObjectType *objc_class_type = dyn_cast<clang::ObjCObjectType>(qual_type.getTypePtr());
                    assert (objc_class_type);
                    if (objc_class_type)
                    {
                        clang::ObjCInterfaceDecl *class_interface_decl = objc_class_type->getInterface();
                        if (class_interface_decl)
                            class_interface_decl->print(llvm_ostrm, ast_context->PrintingPolicy, s->GetIndentLevel());
                    }
                }
                break;
            
            case clang::Type::Typedef:
                {
                    const clang::TypedefType *typedef_type = qual_type->getAs<clang::TypedefType>();
                    if (typedef_type)
                    {
                        const clang::TypedefDecl *typedef_decl = typedef_type->getDecl();
                        std::string clang_typedef_name (typedef_decl->getQualifiedNameAsString());
                        if (!clang_typedef_name.empty())
                            s->PutCString (clang_typedef_name.c_str());
                    }
                }
                break;

            default:
                {
                    std::string clang_type_name(qual_type.getAsString());
                    if (!clang_type_name.empty())
                        s->PutCString (clang_type_name.c_str());
                }
            }
        }
        
        llvm_ostrm.flush();
        if (buf.size() > 0)
        {
            s->Write (buf.data(), buf.size());
        }
    }
}

void
ClangASTType::DumpTypeCode (Stream *s)
{
    DumpTypeCode(m_type, s);
}

void
ClangASTType::DumpTypeCode (void *type, 
                            Stream *s)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(type));
    s->PutCString(qual_type.getAsString().c_str());
}

bool
ClangASTType::GetValueAsScalar
(
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size,
    Scalar &value
)
{
    return GetValueAsScalar (m_ast, 
                             m_type, 
                             data, 
                             data_byte_offset, 
                             data_byte_size, 
                             value);
}

bool
ClangASTType::GetValueAsScalar
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    const lldb_private::DataExtractor &data,
    uint32_t data_byte_offset,
    size_t data_byte_size,
    Scalar &value
)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

    if (ClangASTContext::IsAggregateType (clang_type))
    {
        return false;   // Aggregate types don't have scalar values
    }
    else
    {
        uint32_t count = 0;
        lldb::Encoding encoding = GetEncoding (clang_type, count);

        if (encoding == lldb::eEncodingInvalid || count != 1)
            return false;

        uint64_t bit_width = ast_context->getTypeSize(qual_type);
        uint32_t byte_size = (bit_width + 7 ) / 8;
        uint32_t offset = data_byte_offset;
        switch (encoding)
        {
        case lldb::eEncodingUint:
            if (byte_size <= sizeof(unsigned long long))
            {
                uint64_t uval64 = data.GetMaxU64 (&offset, byte_size);
                if (byte_size <= sizeof(unsigned int))
                {
                    value = (unsigned int)uval64;
                    return true;
                }
                else if (byte_size <= sizeof(unsigned long))
                {
                    value = (unsigned long)uval64;
                    return true;
                }
                else if (byte_size <= sizeof(unsigned long long))
                {
                    value = (unsigned long long )uval64;
                    return true;
                }
                else
                    value.Clear();
            }
            break;

        case lldb::eEncodingSint:
            if (byte_size <= sizeof(long long))
            {
                int64_t sval64 = data.GetMaxS64 (&offset, byte_size);
                if (byte_size <= sizeof(int))
                {
                    value = (int)sval64;
                    return true;
                }
                else if (byte_size <= sizeof(long))
                {
                    value = (long)sval64;
                    return true;
                }
                else if (byte_size <= sizeof(long long))
                {
                    value = (long long )sval64;
                    return true;
                }
                else
                    value.Clear();
            }
            break;

        case lldb::eEncodingIEEE754:
            if (byte_size <= sizeof(long double))
            {
                uint32_t u32;
                uint64_t u64;
                if (byte_size == sizeof(float))
                {
                    if (sizeof(float) == sizeof(uint32_t))
                    {
                        u32 = data.GetU32(&offset);
                        value = *((float *)&u32);
                        return true;
                    }
                    else if (sizeof(float) == sizeof(uint64_t))
                    {
                        u64 = data.GetU64(&offset);
                        value = *((float *)&u64);
                        return true;
                    }
                }
                else
                if (byte_size == sizeof(double))
                {
                    if (sizeof(double) == sizeof(uint32_t))
                    {
                        u32 = data.GetU32(&offset);
                        value = *((double *)&u32);
                        return true;
                    }
                    else if (sizeof(double) == sizeof(uint64_t))
                    {
                        u64 = data.GetU64(&offset);
                        value = *((double *)&u64);
                        return true;
                    }
                }
                else
                if (byte_size == sizeof(long double))
                {
                    if (sizeof(long double) == sizeof(uint32_t))
                    {
                        u32 = data.GetU32(&offset);
                        value = *((long double *)&u32);
                        return true;
                    }
                    else if (sizeof(long double) == sizeof(uint64_t))
                    {
                        u64 = data.GetU64(&offset);
                        value = *((long double *)&u64);
                        return true;
                    }
                }
            }
            break;
        }
    }
    return false;
}

bool
ClangASTType::SetValueFromScalar (const Scalar &value, Stream &strm)
{
    return SetValueFromScalar (m_ast, m_type, value, strm);
}

bool
ClangASTType::SetValueFromScalar
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    const Scalar &value,
    Stream &strm
)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

    // Aggregate types don't have scalar values
    if (!ClangASTContext::IsAggregateType (clang_type))
    {
        strm.GetFlags().Set(Stream::eBinary);
        uint32_t count = 0;
        lldb::Encoding encoding = GetEncoding (clang_type, count);

        if (encoding == lldb::eEncodingInvalid || count != 1)
            return false;

        uint64_t bit_width = ast_context->getTypeSize(qual_type);
        // This function doesn't currently handle non-byte aligned assignments
        if ((bit_width % 8) != 0)
            return false;

        uint32_t byte_size = (bit_width + 7 ) / 8;
        switch (encoding)
        {
        case lldb::eEncodingUint:
            switch (byte_size)
            {
            case 1: strm.PutHex8(value.UInt()); return true;
            case 2: strm.PutHex16(value.UInt()); return true;
            case 4: strm.PutHex32(value.UInt()); return true;
            case 8: strm.PutHex64(value.ULongLong()); return true;
            default:
                break;
            }
            break;

        case lldb::eEncodingSint:
            switch (byte_size)
            {
            case 1: strm.PutHex8(value.SInt()); return true;
            case 2: strm.PutHex16(value.SInt()); return true;
            case 4: strm.PutHex32(value.SInt()); return true;
            case 8: strm.PutHex64(value.SLongLong()); return true;
            default:
                break;
            }
            break;

        case lldb::eEncodingIEEE754:
            if (byte_size <= sizeof(long double))
            {
                if (byte_size == sizeof(float))
                {
                    strm.PutFloat(value.Float());
                    return true;
                }
                else
                if (byte_size == sizeof(double))
                {
                    strm.PutDouble(value.Double());
                    return true;
                }
                else
                if (byte_size == sizeof(long double))
                {
                    strm.PutDouble(value.LongDouble());
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

bool
ClangASTType::ReadFromMemory
(
    lldb_private::ExecutionContext *exe_ctx,
    lldb::addr_t addr,
    lldb::AddressType address_type,
    lldb_private::DataExtractor &data
)
{
    return ReadFromMemory (m_ast,
                           m_type,
                           exe_ctx, 
                           addr,
                           address_type,
                           data);
}


bool
ClangASTType::ReadFromMemory
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    lldb_private::ExecutionContext *exe_ctx,
    lldb::addr_t addr,
    lldb::AddressType address_type,
    lldb_private::DataExtractor &data
)
{
    if (address_type == lldb::eAddressTypeFile)
    {
        // Can't convert a file address to anything valid without more
        // context (which Module it came from)
        return false;
    }
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));

    const uint32_t byte_size = (ast_context->getTypeSize (qual_type) + 7) / 8;
    if (data.GetByteSize() < byte_size)
    {
        lldb::DataBufferSP data_sp(new DataBufferHeap (byte_size, '\0'));
        data.SetData(data_sp);
    }

    uint8_t* dst = (uint8_t*)data.PeekData(0, byte_size);
    if (dst != NULL)
    {
        if (address_type == lldb::eAddressTypeHost)
        {
            // The address is an address in this process, so just copy it
            memcpy (dst, (uint8_t*)NULL + addr, byte_size);
            return true;
        }
        else
        {
            if (exe_ctx && exe_ctx->process)
            {
                Error error;
                return exe_ctx->process->ReadMemory(addr, dst, byte_size, error) == byte_size;
            }
        }
    }
    return false;
}

bool
ClangASTType::WriteToMemory
(
    lldb_private::ExecutionContext *exe_ctx,
    lldb::addr_t addr,
    lldb::AddressType address_type,
    StreamString &new_value
)
{
    return WriteToMemory (m_ast,
                          m_type,
                          exe_ctx,
                          addr,
                          address_type,
                          new_value);
}

bool
ClangASTType::WriteToMemory
(
    clang::ASTContext *ast_context,
    clang_type_t clang_type,
    lldb_private::ExecutionContext *exe_ctx,
    lldb::addr_t addr,
    lldb::AddressType address_type,
    StreamString &new_value
)
{
    if (address_type == lldb::eAddressTypeFile)
    {
        // Can't convert a file address to anything valid without more
        // context (which Module it came from)
        return false;
    }
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    const uint32_t byte_size = (ast_context->getTypeSize (qual_type) + 7) / 8;

    if (byte_size > 0)
    {
        if (address_type == lldb::eAddressTypeHost)
        {
            // The address is an address in this process, so just copy it
            memcpy ((void *)addr, new_value.GetData(), byte_size);
            return true;
        }
        else
        {
            if (exe_ctx && exe_ctx->process)
            {
                Error error;
                return exe_ctx->process->WriteMemory(addr, new_value.GetData(), byte_size, error) == byte_size;
            }
        }
    }
    return false;
}


lldb::clang_type_t
ClangASTType::RemoveFastQualifiers (lldb::clang_type_t clang_type)
{
    clang::QualType qual_type(clang::QualType::getFromOpaquePtr(clang_type));
    qual_type.getQualifiers().removeFastQualifiers();
    return qual_type.getAsOpaquePtr();
}
