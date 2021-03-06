//===-- DWARFExpression.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DWARFExpression_h_
#define liblldb_DWARFExpression_h_

#include "lldb/lldb-private.h"
#include "lldb/Core/ClangForward.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Error.h"
#include "lldb/Core/Scalar.h"

class ClangExpressionVariable;
class ClangExpressionVariableList;

namespace lldb_private {

class ClangExpressionDeclMap;

//----------------------------------------------------------------------
/// @class DWARFExpression DWARFExpression.h "lldb/Expression/DWARFExpression.h"
/// @brief Encapsulates a DWARF location expression and interprets it.
///
/// DWARF location expressions are used in two ways by LLDB.  The first 
/// use is to find entities specified in the debug information, since
/// their locations are specified in precisely this language.  The second
/// is to interpret expressions without having to run the target in cases
/// where the overhead from copying JIT-compiled code into the target is
/// too high or where the target cannot be run.  This class encapsulates
/// a single DWARF location expression or a location list and interprets
/// it.
//----------------------------------------------------------------------
class DWARFExpression
{
public:
    //------------------------------------------------------------------
    /// Constructor
    //------------------------------------------------------------------
    DWARFExpression();

    //------------------------------------------------------------------
    /// Constructor
    ///
    /// @param[in] data
    ///     A data extractor configured to read the DWARF location expression's
    ///     bytecode.
    ///
    /// @param[in] data_offset
    ///     The offset of the location expression in the extractor.
    ///
    /// @param[in] data_length
    ///     The byte length of the location expression.
    //------------------------------------------------------------------
    DWARFExpression(const DataExtractor& data,
                    uint32_t data_offset,
                    uint32_t data_length);

    //------------------------------------------------------------------
    /// Copy constructor
    //------------------------------------------------------------------
    DWARFExpression(const DWARFExpression& rhs);

    //------------------------------------------------------------------
    /// Destructor
    //------------------------------------------------------------------
    virtual
    ~DWARFExpression();

    //------------------------------------------------------------------
    /// Print the description of the expression to a stream
    ///
    /// @param[in] s
    ///     The stream to print to.
    ///
    /// @param[in] level
    ///     The level of verbosity to use.
    ///
    /// @param[in] location_list_base_addr
    ///     If this is a location list based expression, this is the
    ///     address of the object that owns it. NOTE: this value is 
    ///     different from the DWARF version of the location list base
    ///     address which is compile unit relative. This base address
    ///     is the address of the object that owns the location list.
    //------------------------------------------------------------------
    void
    GetDescription (Stream *s, 
                    lldb::DescriptionLevel level, 
                    lldb::addr_t location_list_base_addr) const;

    //------------------------------------------------------------------
    /// Return true if the location expression contains data
    //------------------------------------------------------------------
    bool
    IsValid() const;

    //------------------------------------------------------------------
    /// Return true if a location list was provided
    //------------------------------------------------------------------
    bool
    IsLocationList() const;

    //------------------------------------------------------------------
    /// Search for a load address in the location list
    ///
    /// @param[in] process
    ///     The process to use when resolving the load address
    ///
    /// @param[in] addr
    ///     The address to resolve
    ///
    /// @return
    ///     True if IsLocationList() is true and the address was found;
    ///     false otherwise.
    //------------------------------------------------------------------
//    bool
//    LocationListContainsLoadAddress (Process* process, const Address &addr) const;
//
    bool
    LocationListContainsAddress (lldb::addr_t loclist_base_addr, lldb::addr_t addr) const;
    
    //------------------------------------------------------------------
    /// Make the expression parser read its location information from a
    /// given data source.  Does not change the offset and length
    ///
    /// @param[in] data
    ///     A data extractor configured to read the DWARF location expression's
    ///     bytecode.
    //------------------------------------------------------------------
    void
    SetOpcodeData(const DataExtractor& data);

    //------------------------------------------------------------------
    /// Make the expression parser read its location information from a
    /// given data source
    ///
    /// @param[in] data
    ///     A data extractor configured to read the DWARF location expression's
    ///     bytecode.
    ///
    /// @param[in] data_offset
    ///     The offset of the location expression in the extractor.
    ///
    /// @param[in] data_length
    ///     The byte length of the location expression.
    //------------------------------------------------------------------
    void
    SetOpcodeData(const DataExtractor& data, uint32_t data_offset, uint32_t data_length);

    //------------------------------------------------------------------
    /// Tells the expression that it refers to a location list.
    ///
    /// @param[in] slide
    ///     This value should be a slide that is applied to any values
    ///     in the location list data so the values become zero based
    ///     offsets into the object that owns the location list. We need
    ///     to make location lists relative to the objects that own them
    ///     so we can relink addresses on the fly.
    //------------------------------------------------------------------
    void
    SetLocationListSlide (lldb::addr_t slide);

    //------------------------------------------------------------------
    /// Return the call-frame-info style register kind
    //------------------------------------------------------------------
    int
    GetRegisterKind ();

    //------------------------------------------------------------------
    /// Set the call-frame-info style register kind
    ///
    /// @param[in] reg_kind
    ///     The register kind.
    //------------------------------------------------------------------
    void
    SetRegisterKind (int reg_kind);

    //------------------------------------------------------------------
    /// Wrapper for the static evaluate function that accepts an
    /// ExecutionContextScope instead of an ExecutionContext and uses
    /// member variables to populate many operands
    //------------------------------------------------------------------
    bool
    Evaluate (ExecutionContextScope *exe_scope,
              clang::ASTContext *ast_context,
              ClangExpressionVariableList *expr_locals,
              ClangExpressionDeclMap *decl_map,
              lldb::addr_t loclist_base_load_addr,
              const Value* initial_value_ptr,
              Value& result,
              Error *error_ptr) const;

    //------------------------------------------------------------------
    /// Wrapper for the static evaluate function that uses member 
    /// variables to populate many operands
    //------------------------------------------------------------------
    bool
    Evaluate (ExecutionContext *exe_ctx,
              clang::ASTContext *ast_context,
              ClangExpressionVariableList *expr_locals,
              ClangExpressionDeclMap *decl_map,
              RegisterContext *reg_ctx,
              lldb::addr_t loclist_base_load_addr,
              const Value* initial_value_ptr,
              Value& result,
              Error *error_ptr) const;

    //------------------------------------------------------------------
    /// Evaluate a DWARF location expression in a particular context
    ///
    /// @param[in] exe_ctx
    ///     The execution context in which to evaluate the location
    ///     expression.  The location expression may access the target's
    ///     memory, especially if it comes from the expression parser.
    ///
    /// @param[in] ast_context
    ///     The context in which to interpret types.
    ///
    /// @param[in] opcodes
    ///     This is a static method so the opcodes need to be provided
    ///     explicitly.
    ///
    /// @param[in] expr_locals
    ///     If the location expression was produced by the expression parser,
    ///     the list of local variables referenced by the DWARF expression.
    ///     This list should already have been populated during parsing;
    ///     the DWARF expression refers to variables by index.  Can be NULL if
    ///     the location expression uses no locals.
    ///
    /// @param[in] decl_map
    ///     If the location expression was produced by the expression parser,
    ///     the list of external variables referenced by the location 
    ///     expression.  Can be NULL if the location expression uses no
    ///     external variables.
    ///
    ///  @param[in] reg_ctx
    ///     An optional parameter which provides a RegisterContext for use
    ///     when evaluating the expression (i.e. for fetching register values).
    ///     Normally this will come from the ExecutionContext's StackFrame but
    ///     in the case where an expression needs to be evaluated while building
    ///     the stack frame list, this short-cut is available.
    ///
    /// @param[in] offset
    ///     The offset of the location expression in the data extractor.
    ///
    /// @param[in] length
    ///     The length in bytes of the location expression.
    ///
    /// @param[in] reg_set
    ///     The call-frame-info style register kind.
    ///
    /// @param[in] initial_value_ptr
    ///     A value to put on top of the interpreter stack before evaluating
    ///     the expression, if the expression is parametrized.  Can be NULL.
    ///
    /// @param[in] result
    ///     A value into which the result of evaluating the expression is
    ///     to be placed.
    ///
    /// @param[in] error_ptr
    ///     If non-NULL, used to report errors in expression evaluation.
    ///
    /// @return
    ///     True on success; false otherwise.  If error_ptr is non-NULL,
    ///     details of the failure are provided through it.
    //------------------------------------------------------------------
    static bool
    Evaluate (ExecutionContext *exe_ctx,
              clang::ASTContext *ast_context,
              ClangExpressionVariableList *expr_locals,
              ClangExpressionDeclMap *decl_map,
              RegisterContext *reg_ctx,
              const DataExtractor& opcodes,
              const uint32_t offset,
              const uint32_t length,
              const uint32_t reg_set,
              const Value* initial_value_ptr,
              Value& result,
              Error *error_ptr);

    //------------------------------------------------------------------
    /// Loads a ClangExpressionVariableList into the object
    ///
    /// @param[in] locals
    ///     If non-NULL, the list of locals used by this expression.
    ///     See Evaluate().
    //------------------------------------------------------------------
    void
    SetExpressionLocalVariableList (ClangExpressionVariableList *locals);
    
    //------------------------------------------------------------------
    /// Loads a ClangExpressionDeclMap into the object
    ///
    /// @param[in] locals
    ///     If non-NULL, the list of external variables used by this 
    ///     expression.  See Evaluate().
    //------------------------------------------------------------------
    void
    SetExpressionDeclMap (ClangExpressionDeclMap *decl_map);

protected:
    //------------------------------------------------------------------
    /// Pretty-prints the location expression to a stream
    ///
    /// @param[in] stream
    ///     The stream to use for pretty-printing.
    ///
    /// @param[in] offset
    ///     The offset into the data buffer of the opcodes to be printed.
    ///
    /// @param[in] length
    ///     The length in bytes of the opcodes to be printed.
    ///
    /// @param[in] level
    ///     The level of detail to use in pretty-printing.
    //------------------------------------------------------------------
    void
    DumpLocation(Stream *s, 
                 uint32_t offset, 
                 uint32_t length, 
                 lldb::DescriptionLevel level) const;
    
    //------------------------------------------------------------------
    /// Classes that inherit from DWARFExpression can see and modify these
    //------------------------------------------------------------------
    
    DataExtractor m_data;                       ///< A data extractor capable of reading opcode bytes
    int m_reg_kind;                             ///< One of the defines that starts with LLDB_REGKIND_
    lldb::addr_t m_loclist_slide;               ///< A value used to slide the location list offsets so that 
                                                ///< they are relative to the object that owns the location list
                                                ///< (the function for frame base and variable location lists)

};

} // namespace lldb_private

#endif  // liblldb_DWARFExpression_h_
