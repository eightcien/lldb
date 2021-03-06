//===-- ASTResultSynthesizer.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "stdlib.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/Parse/Parser.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "lldb/Core/Log.h"
#include "lldb/Expression/ASTResultSynthesizer.h"

using namespace llvm;
using namespace clang;
using namespace lldb_private;

ASTResultSynthesizer::ASTResultSynthesizer(ASTConsumer *passthrough,
                                           TypeFromUser desired_type) :
    m_ast_context (NULL),
    m_passthrough (passthrough),
    m_passthrough_sema (NULL),
    m_sema (NULL),
    m_desired_type (desired_type)
{
    if (!m_passthrough)
        return;
    
    m_passthrough_sema = dyn_cast<SemaConsumer>(passthrough);
}

ASTResultSynthesizer::~ASTResultSynthesizer()
{
}

void
ASTResultSynthesizer::Initialize(ASTContext &Context) 
{
    m_ast_context = &Context;
    
    if (m_passthrough)
        m_passthrough->Initialize(Context);
}

void
ASTResultSynthesizer::TransformTopLevelDecl(Decl* D)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    if (NamedDecl *named_decl = dyn_cast<NamedDecl>(D))
    {
        if (log)
        {
            if (named_decl->getIdentifier())
                log->Printf("TransformTopLevelDecl(%s)", named_decl->getIdentifier()->getNameStart());
            else if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D))
                log->Printf("TransformTopLevelDecl(%s)", method_decl->getSelector().getAsString().c_str());
            else
                log->Printf("TransformTopLevelDecl(<complex>)");
        }

    }
    
    if (LinkageSpecDecl *linkage_spec_decl = dyn_cast<LinkageSpecDecl>(D))
    {
        RecordDecl::decl_iterator decl_iterator;
        
        for (decl_iterator = linkage_spec_decl->decls_begin();
             decl_iterator != linkage_spec_decl->decls_end();
             ++decl_iterator)
        {
            TransformTopLevelDecl(*decl_iterator);
        }
    }
    else if (ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(D))
    {
        if (m_ast_context &&
            !method_decl->getSelector().getAsString().compare("$__lldb_expr:"))
        {
            SynthesizeObjCMethodResult(method_decl);
        }
    }
    else if (FunctionDecl *function_decl = dyn_cast<FunctionDecl>(D))
    {
        if (m_ast_context &&
            !function_decl->getNameInfo().getAsString().compare("$__lldb_expr"))
        {
            SynthesizeFunctionResult(function_decl);
        }
    }
}

void 
ASTResultSynthesizer::HandleTopLevelDecl(DeclGroupRef D)
{
    DeclGroupRef::iterator decl_iterator;
    
    for (decl_iterator = D.begin();
         decl_iterator != D.end();
         ++decl_iterator)
    {
        Decl *decl = *decl_iterator;
        
        TransformTopLevelDecl(decl);
    }
    
    if (m_passthrough)
        m_passthrough->HandleTopLevelDecl(D);
}

bool 
ASTResultSynthesizer::SynthesizeFunctionResult (FunctionDecl *FunDecl)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    ASTContext &Ctx(*m_ast_context);

    if (!m_sema)
        return false;
    
    FunctionDecl *function_decl = FunDecl;
    
    if (!function_decl)
        return false;
    
    if (log && log->GetVerbose())
    {
        std::string s;
        raw_string_ostream os(s);
        
        Ctx.getTranslationUnitDecl()->print(os);
        
        os.flush();
        
        log->Printf("AST context before transforming:\n%s", s.c_str());
    }
    
    Stmt *function_body = function_decl->getBody();
    CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(function_body);
    
    bool ret = SynthesizeBodyResult (compound_stmt,
                                     function_decl);

    if (log && log->GetVerbose())
    {
        std::string s;
        raw_string_ostream os(s);
        
        function_decl->print(os);
        
        os.flush();
        
        log->Printf ("Transformed function AST:\n%s", s.c_str());
    }
    
    return ret;
}

bool
ASTResultSynthesizer::SynthesizeObjCMethodResult (ObjCMethodDecl *MethodDecl)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    ASTContext &Ctx(*m_ast_context);
    
    if (!m_sema)
        return false;
        
    if (!MethodDecl)
        return false;
    
    if (log && log->GetVerbose())
    {
        std::string s;
        raw_string_ostream os(s);
        
        Ctx.getTranslationUnitDecl()->print(os);
        
        os.flush();
        
        log->Printf("AST context before transforming:\n%s", s.c_str());
    }
    
    Stmt *method_body = MethodDecl->getBody();
    CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(method_body);
    
    bool ret = SynthesizeBodyResult (compound_stmt,
                                     MethodDecl);
    
    if (log)
    {
        std::string s;
        raw_string_ostream os(s);
        
        MethodDecl->print(os);
        
        os.flush();
        
        log->Printf("Transformed function AST:\n%s", s.c_str());
    }
    
    return ret;
}

bool 
ASTResultSynthesizer::SynthesizeBodyResult (CompoundStmt *Body, 
                                            DeclContext *DC)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    ASTContext &Ctx(*m_ast_context);
    
    CompoundStmt *compound_stmt = dyn_cast<CompoundStmt>(Body);
    
    if (!compound_stmt)
        return false;
    
    if (compound_stmt->body_empty())
        return false;
    
    Stmt **last_stmt_ptr = compound_stmt->body_end() - 1;
    Stmt *last_stmt = *last_stmt_ptr;
    
    while (dyn_cast<NullStmt>(last_stmt))
    {
        if (last_stmt_ptr != compound_stmt->body_begin())
        {
            last_stmt_ptr--;
            last_stmt = *last_stmt_ptr;
        }
        else
        {
            return false;
        }
    }
    
    Expr *last_expr = dyn_cast<Expr>(last_stmt);
    
    if (!last_expr)
        // No auxiliary variable necessary; expression returns void
        return true;
    
    // is_lvalue is used to record whether the expression returns an assignable Lvalue or an
    // Rvalue.  This is relevant because they are handled differently.
    //
    // For Lvalues
    //
    //   - In AST result synthesis (here!) the expression E is transformed into an initialization
    //     T *$__lldb_expr_result_ptr = &E.
    //
    //   - In structure allocation, a pointer-sized slot is allocated in the struct that is to be
    //     passed into the expression.
    //
    //   - In IR transformations, reads and writes to $__lldb_expr_result_ptr are redirected at
    //     an entry in the struct ($__lldb_arg) passed into the expression.  (Other persistent
    //     variables are treated similarly, having been materialized as references, but in those
    //     cases the value of the reference itself is never modified.)
    //
    //   - During materialization, $0 (the result persistent variable) is ignored.
    //
    //   - During dematerialization, $0 is marked up as a load address with value equal to the
    //     contents of the structure entry.
    //
    // For Rvalues
    //
    //   - In AST result synthesis the expression E is transformed into an initialization
    //     static T $__lldb_expr_result = E.
    //
    //   - In structure allocation, a pointer-sized slot is allocated in the struct that is to be
    //     passed into the expression.
    //
    //   - In IR transformations, an instruction is inserted at the beginning of the function to
    //     dereference the pointer resident in the slot.  Reads and writes to $__lldb_expr_result
    //     are redirected at that dereferenced version.  Guard variables for the static variable 
    //     are excised.
    //
    //   - During materialization, $0 (the result persistent variable) is populated with the location
    //     of a newly-allocated area of memory.
    //
    //   - During dematerialization, $0 is ignored.

    bool is_lvalue = 
        (last_expr->getValueKind() == VK_LValue || last_expr->getValueKind() == VK_XValue) &&
        (last_expr->getObjectKind() == OK_Ordinary);
    
    QualType expr_qual_type = last_expr->getType();
    const clang::Type *expr_type = expr_qual_type.getTypePtr();
    
    if (!expr_type)
        return false;
    
    if (expr_type->isVoidType())
        return true;
    
    if (log)
    {
        std::string s = expr_qual_type.getAsString();
        
        log->Printf("Last statement is an %s with type: %s", (is_lvalue ? "lvalue" : "rvalue"), s.c_str());
    }
    
    clang::VarDecl *result_decl;
    
    if (is_lvalue)
    {
        IdentifierInfo &result_ptr_id = Ctx.Idents.get("$__lldb_expr_result_ptr");
        
        QualType ptr_qual_type = Ctx.getPointerType(expr_qual_type);
        
        result_decl = VarDecl::Create(Ctx,
                                      DC,
                                      SourceLocation(),
                                      &result_ptr_id,
                                      ptr_qual_type,
                                      NULL,
                                      SC_Static,
                                      SC_Static);
        
        if (!result_decl)
            return false;
                
        ExprResult address_of_expr = m_sema->CreateBuiltinUnaryOp(SourceLocation(), UO_AddrOf, last_expr);
        
        m_sema->AddInitializerToDecl(result_decl, address_of_expr.take());
    }
    else
    {
        IdentifierInfo &result_id = Ctx.Idents.get("$__lldb_expr_result");
        
        result_decl = VarDecl::Create(Ctx, 
                                      DC, 
                                      SourceLocation(), 
                                      &result_id, 
                                      expr_qual_type, 
                                      NULL, 
                                      SC_Static, 
                                      SC_Static);
        
        if (!result_decl)
            return false;
        
        m_sema->AddInitializerToDecl(result_decl, last_expr);
    }
    
    DC->addDecl(result_decl);
    
    ///////////////////////////////
    // call AddInitializerToDecl
    //
        
    //m_sema->AddInitializerToDecl(result_decl, last_expr);
    
    /////////////////////////////////
    // call ConvertDeclToDeclGroup
    //
    
    Sema::DeclGroupPtrTy result_decl_group_ptr;
    
    result_decl_group_ptr = m_sema->ConvertDeclToDeclGroup(result_decl);
    
    ////////////////////////
    // call ActOnDeclStmt
    //
    
    StmtResult result_initialization_stmt_result(m_sema->ActOnDeclStmt(result_decl_group_ptr,
                                                                       SourceLocation(),
                                                                       SourceLocation()));
    
    ////////////////////////////////////////////////
    // replace the old statement with the new one
    //
    
    *last_stmt_ptr = reinterpret_cast<Stmt*>(result_initialization_stmt_result.take());

    return true;
}

void
ASTResultSynthesizer::HandleTranslationUnit(ASTContext &Ctx)
{    
    if (m_passthrough)
        m_passthrough->HandleTranslationUnit(Ctx);
}

void 
ASTResultSynthesizer::HandleTagDeclDefinition(TagDecl *D)
{
    if (m_passthrough)
        m_passthrough->HandleTagDeclDefinition(D);
}

void
ASTResultSynthesizer::CompleteTentativeDefinition(VarDecl *D)
{
    if (m_passthrough)
        m_passthrough->CompleteTentativeDefinition(D);
}

void 
ASTResultSynthesizer::HandleVTable(CXXRecordDecl *RD, bool DefinitionRequired) 
{
    if (m_passthrough)
        m_passthrough->HandleVTable(RD, DefinitionRequired);
}

void
ASTResultSynthesizer::PrintStats() 
{
    if (m_passthrough)
        m_passthrough->PrintStats();
}

void
ASTResultSynthesizer::InitializeSema(Sema &S)
{
    m_sema = &S;
    
    if (m_passthrough_sema)
        m_passthrough_sema->InitializeSema(S);
}

void 
ASTResultSynthesizer::ForgetSema() 
{
    m_sema = NULL;
    
    if (m_passthrough_sema)
        m_passthrough_sema->ForgetSema();
}
