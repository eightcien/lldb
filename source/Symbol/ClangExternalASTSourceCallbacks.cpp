//===-- ClangExternalASTSourceCallbacks.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/ClangExternalASTSourceCallbacks.h"

// C Includes
// C++ Includes
// Other libraries and framework includes

// Clang headers like to use NDEBUG inside of them to enable/disable debug 
// releated features using "#ifndef NDEBUG" preprocessor blocks to do one thing
// or another. This is bad because it means that if clang was built in release
// mode, it assumes that you are building in release mode which is not always
// the case. You can end up with functions that are defined as empty in header
// files when NDEBUG is not defined, and this can cause link errors with the
// clang .a files that you have since you might be missing functions in the .a
// file. So we have to define NDEBUG when including clang headers to avoid any
// mismatches. This is covered by rdar://problem/8691220

#ifndef NDEBUG
#define LLDB_DEFINED_NDEBUG_FOR_CLANG
#define NDEBUG
// Need to include assert.h so it is as clang would expect it to be (disabled)
#include <assert.h>
#endif

#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"

#ifdef LLDB_DEFINED_NDEBUG_FOR_CLANG
#undef NDEBUG
#undef LLDB_DEFINED_NDEBUG_FOR_CLANG
// Need to re-include assert.h so it is as _we_ would expect it to be (enabled)
#include <assert.h>
#endif

#include "lldb/Core/Log.h"

using namespace clang;
using namespace lldb_private;

clang::DeclContextLookupResult 
ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName 
(
    const clang::DeclContext *decl_ctx,
    clang::DeclarationName clang_decl_name
)
{
    std::string decl_name (clang_decl_name.getAsString());

    switch (clang_decl_name.getNameKind()) {
    // Normal identifiers.
    case clang::DeclarationName::Identifier:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"Identifier\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        if (clang_decl_name.getAsIdentifierInfo()->getBuiltinID() != 0)
            return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
        break;

    case clang::DeclarationName::ObjCZeroArgSelector:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"ObjCZeroArgSelector\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::ObjCOneArgSelector:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"ObjCOneArgSelector\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::ObjCMultiArgSelector:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"ObjCMultiArgSelector\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXConstructorName:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXConstructorName\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXDestructorName:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXDestructorName\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXConversionFunctionName:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXConversionFunctionName\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXOperatorName:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXOperatorName\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXLiteralOperatorName:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXLiteralOperatorName\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return DeclContext::lookup_result();
        break;

    case clang::DeclarationName::CXXUsingDirective:
        //printf ("ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(decl_ctx = %p, decl_name = { kind = \"CXXUsingDirective\", name = \"%s\")\n", decl_ctx, decl_name.c_str());
        return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    }

    return DeclContext::lookup_result();
}

void
ClangExternalASTSourceCallbacks::CompleteType (TagDecl *tag_decl)
{
    if (m_callback_tag_decl)
        m_callback_tag_decl (m_callback_baton, tag_decl);
}

void
ClangExternalASTSourceCallbacks::CompleteType (ObjCInterfaceDecl *objc_decl)
{
    if (m_callback_objc_decl)
        m_callback_objc_decl (m_callback_baton, objc_decl);
}
