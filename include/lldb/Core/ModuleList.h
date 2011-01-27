//===-- ModuleList.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ModuleList_h_
#define liblldb_ModuleList_h_

#include <vector>

#include "lldb/lldb-private.h"
#include "lldb/Host/Mutex.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class ModuleList ModuleList.h "lldb/Core/ModuleList.h"
/// @brief A collection class for Module objects.
///
/// Modules in the module collection class are stored as reference
/// counted shared pointers to Module objects.
//----------------------------------------------------------------------
class ModuleList
{
public:
    //------------------------------------------------------------------
    /// Default constructor.
    ///
    /// Creates an empty list of Module objects.
    //------------------------------------------------------------------
    ModuleList ();

    //------------------------------------------------------------------
    /// Copy Constructor.
    ///
    /// Creates a new module list object with a copy of the modules from
    /// \a rhs.
    ///
    /// @param[in] rhs
    ///     Another module list object.
    //------------------------------------------------------------------
    ModuleList (const ModuleList& rhs);

    //------------------------------------------------------------------
    /// Destructor.
    //------------------------------------------------------------------
    ~ModuleList ();

    //------------------------------------------------------------------
    /// Assignment operator.
    ///
    /// Copies the module list from \a rhs into this list.
    ///
    /// @param[in] rhs
    ///     Another module list object.
    ///
    /// @return
    ///     A const reference to this object.
    //------------------------------------------------------------------
    const ModuleList&
    operator= (const ModuleList& rhs);

    //------------------------------------------------------------------
    /// Append a module to the module list.
    ///
    /// Appends the module to the collection.
    ///
    /// @param[in] module_sp
    ///     A shared pointer to a module to add to this collection.
    //------------------------------------------------------------------
    void
    Append (lldb::ModuleSP &module_sp);

    bool
    AppendIfNeeded (lldb::ModuleSP &module_sp);

    //------------------------------------------------------------------
    /// Clear the object's state.
    ///
    /// Clears the list of modules and releases a reference to each
    /// module object and if the reference count goes to zero, the
    /// module will be deleted.
    //------------------------------------------------------------------
    void
    Clear ();

    //------------------------------------------------------------------
    /// Dump the description of each module contained in this list.
    ///
    /// Dump the description of each module contained in this list to
    /// the supplied stream \a s.
    ///
    /// @param[in] s
    ///     The stream to which to dump the object descripton.
    ///
    /// @see Module::Dump(Stream *) const
    //------------------------------------------------------------------
    void
    Dump (Stream *s) const;

    void
    LogUUIDAndPaths (lldb::LogSP &log_sp, 
                     const char *prefix_cstr);

    uint32_t
    GetIndexForModule (const Module *module) const;

    //------------------------------------------------------------------
    /// Get the module shared pointer for the module at index \a idx.
    ///
    /// @param[in] idx
    ///     An index into this module collection.
    ///
    /// @return
    ///     A shared pointer to a Module which can contain NULL if
    ///     \a idx is out of range.
    ///
    /// @see ModuleList::GetSize()
    //------------------------------------------------------------------
    lldb::ModuleSP
    GetModuleAtIndex (uint32_t idx);

    //------------------------------------------------------------------
    /// Get the module pointer for the module at index \a idx.
    ///
    /// @param[in] idx
    ///     An index into this module collection.
    ///
    /// @return
    ///     A pointer to a Module which can by NULL if \a idx is out
    ///     of range.
    ///
    /// @see ModuleList::GetSize()
    //------------------------------------------------------------------
    Module*
    GetModulePointerAtIndex (uint32_t idx) const;

    //------------------------------------------------------------------
    /// Find functions by name.
    ///
    /// Finds all functions that match \a name in all of the modules and
    /// returns the results in \a sc_list.
    ///
    /// @param[in] name
    ///     The name of the function we are looking for.
    ///
    /// @param[in] name_type_mask
    ///     A bit mask of bits that indicate what kind of names should
    ///     be used when doing the lookup. Bits include fully qualified
    ///     names, base names, C++ methods, or ObjC selectors. 
    ///     See FunctionNameType for more details.
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     The number of matches added to \a sc_list.
    //------------------------------------------------------------------
    size_t
    FindFunctions (const ConstString &name,
                   uint32_t name_type_mask,
                   bool include_symbols,
                   bool append,
                   SymbolContextList &sc_list);

    //------------------------------------------------------------------
    /// Find global and static variables by name.
    ///
    /// @param[in] name
    ///     The name of the global or static variable we are looking
    ///     for.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] variable_list
    ///     A list of variables that gets the matches appended to (if
    ///     \a append it \b true), or replace (if \a append is \b false).
    ///
    /// @return
    ///     The number of matches added to \a variable_list.
    //------------------------------------------------------------------
    uint32_t
    FindGlobalVariables (const ConstString &name,
                         bool append,
                         uint32_t max_matches,
                         VariableList& variable_list);

    //------------------------------------------------------------------
    /// Find global and static variables by regular exression.
    ///
    /// @param[in] regex
    ///     A regular expression to use when matching the name.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] variable_list
    ///     A list of variables that gets the matches appended to (if
    ///     \a append it \b true), or replace (if \a append is \b false).
    ///
    /// @return
    ///     The number of matches added to \a variable_list.
    //------------------------------------------------------------------
    uint32_t
    FindGlobalVariables (const RegularExpression& regex,
                         bool append,
                         uint32_t max_matches,
                         VariableList& variable_list);

    //------------------------------------------------------------------
    /// Finds the first module whose file specification matches \a
    /// file_spec.
    ///
    /// @param[in] file_spec_ptr
    ///     A file specification object to match against the Module's
    ///     file specifications. If \a file_spec does not have
    ///     directory information, matches will occur by matching only
    ///     the basename of any modules in this list. If this value is
    ///     NULL, then file specifications won't be compared when
    ///     searching for matching modules.
    ///
    /// @param[in] arch_ptr
    ///     The architecture to search for if non-NULL. If this value
    ///     is NULL no architecture matching will be performed.
    ///
    /// @param[in] uuid_ptr
    ///     The uuid to search for if non-NULL. If this value is NULL
    ///     no uuid matching will be performed.
    ///
    /// @param[in] object_name
    ///     An optional object name that must match as well. This value
    ///     can be NULL.
    ///
    /// @param[out] matching_module_list
    ///     A module list that gets filled in with any modules that
    ///     match the search criteria.
    ///
    /// @return
    ///     The number of matching modules found by the search.
    //------------------------------------------------------------------
    size_t
    FindModules (const FileSpec *file_spec_ptr,
                 const ArchSpec *arch_ptr,
                 const UUID *uuid_ptr,
                 const ConstString *object_name,
                 ModuleList& matching_module_list) const;

    lldb::ModuleSP
    FindModule (const Module *module_ptr);

    lldb::ModuleSP
    FindFirstModuleForFileSpec (const FileSpec &file_spec,
                                const ConstString *object_name = NULL);

    size_t
    FindSymbolsWithNameAndType (const ConstString &name,
                                lldb::SymbolType symbol_type,
                                SymbolContextList &sc_list);

    //------------------------------------------------------------------
    /// Find types by name.
    ///
    /// @param[in] sc
    ///     A symbol context that scopes where to extract a type list
    ///     from.
    ///
    /// @param[in] name
    ///     The name of the type we are looking for.
    ///
    /// @param[in] append
    ///     If \b true, any matches will be appended to \a
    ///     variable_list, else matches replace the contents of
    ///     \a variable_list.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a
    ///     max_matches. Specify UINT32_MAX to get all possible matches.
    ///
    /// @param[in] encoding
    ///     Limit the search to specific types, or get all types if
    ///     set to Type::invalid.
    ///
    /// @param[in] udt_name
    ///     If the encoding is a user defined type, specify the name
    ///     of the user defined type ("struct", "union", "class", etc).
    ///
    /// @param[out] type_list
    ///     A type list gets populated with any matches.
    ///
    /// @return
    ///     The number of matches added to \a type_list.
    //------------------------------------------------------------------
    uint32_t
    FindTypes (const SymbolContext& sc, 
               const ConstString &name, 
               bool append, 
               uint32_t max_matches, 
               TypeList& types);
    
    bool
    Remove (lldb::ModuleSP &module_sp);

    size_t
    Remove (ModuleList &module_list);
    
    bool
    ResolveFileAddress (lldb::addr_t vm_addr,
                        Address& so_addr);

    //------------------------------------------------------------------
    /// @copydoc Module::ResolveSymbolContextForAddress (const Address &,uint32_t,SymbolContext&)
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextForAddress (const Address& so_addr,
                                    uint32_t resolve_scope,
                                    SymbolContext& sc);

    //------------------------------------------------------------------
    /// @copydoc Module::ResolveSymbolContextForFilePath (const char *,uint32_t,bool,uint32_t,SymbolContextList&)
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextForFilePath (const char *file_path,
                                     uint32_t line,
                                     bool check_inlines,
                                     uint32_t resolve_scope,
                                     SymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// @copydoc Module::ResolveSymbolContextsForFileSpec (const FileSpec &,uint32_t,bool,uint32_t,SymbolContextList&)
    //------------------------------------------------------------------
    uint32_t
    ResolveSymbolContextsForFileSpec (const FileSpec &file_spec,
                                     uint32_t line,
                                     bool check_inlines,
                                     uint32_t resolve_scope,
                                     SymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// Gets the size of the module list.
    ///
    /// @return
    ///     The number of modules in the module list.
    //------------------------------------------------------------------
    size_t
    GetSize () const;

    static const lldb::ModuleSP
    GetModuleSP (const Module *module_ptr);

    static Error
    GetSharedModule (const FileSpec& file_spec,
                     const ArchSpec& arch,
                     const UUID *uuid_ptr,
                     const ConstString *object_name,
                     off_t object_offset,
                     lldb::ModuleSP &module_sp,
                     lldb::ModuleSP *old_module_sp_ptr,
                     bool *did_create_ptr);

    static size_t
    FindSharedModules (const FileSpec& in_file_spec,
                       const ArchSpec& arch,
                       const UUID *uuid_ptr,
                       const ConstString *object_name_ptr,
                       ModuleList &matching_module_list);

protected:
    //------------------------------------------------------------------
    // Class typedefs.
    //------------------------------------------------------------------
    typedef std::vector<lldb::ModuleSP> collection; ///< The module collection type.

    //------------------------------------------------------------------
    // Member variables.
    //------------------------------------------------------------------
    collection m_modules; ///< The collection of modules.
    mutable Mutex m_modules_mutex;

};

} // namespace lldb_private

#endif  // liblldb_ModuleList_h_
