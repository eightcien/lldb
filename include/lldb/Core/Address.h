//===-- Address.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Address_h_
#define liblldb_Address_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/Symbol/SymbolContextScope.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Address Address.h "lldb/Core/Address.h"
/// @brief A section + offset based address class.
///
/// The Address class allows addresses to be relative to a section
/// that can move during runtime due to images (executables, shared
/// libraries, bundles, frameworks) being loaded at different
/// addresses than the addresses found in the object file that
/// represents them on disk. There are currently two types of addresses
/// for a section:
///     @li file addresses
///     @li load addresses
///
/// File addresses represent the virtual addresses that are in the "on
/// disk" object files. These virtual addresses are converted to be
/// relative to unique sections scoped to the object file so that
/// when/if the addresses slide when the images are loaded/unloaded
/// in memory, we can easily track these changes without having to
/// update every object (compile unit ranges, line tables, function
/// address ranges, lexical block and inlined subroutine address
/// ranges, global and static variables) each time an image is loaded or
/// unloaded.
///
/// Load addresses represent the virtual addresses where each section
/// ends up getting loaded at runtime. Before executing a program, it
/// is common for all of the load addresses to be unresolved. When a
/// DynamicLoader plug-in receives notification that shared libraries
/// have been loaded/unloaded, the load addresses of the main executable
/// and any images (shared libraries) will be  resolved/unresolved. When
/// this happens, breakpoints that are in one of these sections can be
/// set/cleared.
//----------------------------------------------------------------------
class Address
{
public:
    //------------------------------------------------------------------
    /// Dump styles allow the Address::Dump(Stream *,DumpStyle) const
    /// function to display Address contents in a variety of ways.
    //------------------------------------------------------------------
    typedef enum {
        DumpStyleInvalid,               ///< Invalid dump style
        DumpStyleSectionNameOffset,     ///< Display as the section name + offset.
                                        ///< \code
                                        /// // address for printf in libSystem.B.dylib as a section name + offset
                                        /// libSystem.B.dylib.__TEXT.__text + 0x0005cfdf
                                        /// \endcode
        DumpStyleSectionPointerOffset,  ///< Display as the section pointer + offset (debug output).
                                        ///< \code
                                        /// // address for printf in libSystem.B.dylib as a section pointer + offset
                                        /// (lldb::Section *)0x35cc50 + 0x000000000005cfdf \endcode
        DumpStyleFileAddress,           ///< Display as the file address (if any).
                                        ///< \code
                                        /// // address for printf in libSystem.B.dylib as a file address
                                        /// 0x000000000005dcff \endcode
        DumpStyleModuleWithFileAddress, ///< Display as the file address with the module name prepended (if any).
                                        ///< \code
                                        /// // address for printf in libSystem.B.dylib as a file address
                                        /// libSystem.B.dylib[0x000000000005dcff] \endcode
        DumpStyleLoadAddress,           ///< Display as the load address (if resolved).
                                        ///< \code
                                        /// // address for printf in libSystem.B.dylib as a load address
                                        /// 0x00007fff8306bcff \endcode
        DumpStyleResolvedDescription,   ///< Display the details about what an address resolves to. This can
                                        ///< be anything from a symbol context summary (module, function/symbol, 
                                        ///< and file and line), to information about what the pointer points to
                                        ///< if the address is in a section (section of pointers, c strings, etc).
        DumpStyleResolvedDescriptionNoModule,
        DumpStyleDetailedSymbolContext  ///< Detailed symbol context information for an address for all symbol
                                        ///< context members.
    } DumpStyle;

    //------------------------------------------------------------------
    /// Default constructor.
    ///
    /// Initialize with a invalid section (NULL) and an invalid
    /// offset (LLDB_INVALID_ADDRESS).
    //------------------------------------------------------------------
    Address () :
        m_section (NULL),
        m_offset (LLDB_INVALID_ADDRESS)
    {
    }


    //------------------------------------------------------------------
    /// Copy constructor
    ///
    /// Makes a copy of the another Address object \a rhs.
    ///
    /// @param[in] rhs
    ///     A const Address object reference to copy.
    //------------------------------------------------------------------
    Address (const Address& rhs) :
        m_section (rhs.m_section),
        m_offset (rhs.m_offset)
    {
    }

    //------------------------------------------------------------------
    /// Construct with a section pointer and offset.
    ///
    /// Initialize the address with the supplied \a section and \a
    /// offset.
    ///
    /// @param[in] section
    ///     A section pointer to a valid lldb::Section, or NULL if the
    ///     address doesn't have a section or will get resolved later.
    ///
    /// @param[in] offset
    ///     The offset in bytes into \a section.
    //------------------------------------------------------------------
    Address (const Section* section, lldb::addr_t offset) :
        m_section (section),
        m_offset (offset)
    {
    }

    //------------------------------------------------------------------
    /// Construct with a virtual address and section list.
    ///
    /// Initialize and resolve the address with the supplied virtual
    /// address \a file_addr.
    ///
    /// @param[in] file_addr
    ///     A virtual file address.
    ///
    /// @param[in] section_list
    ///     A list of sections, one of which may contain the \a file_addr.
    //------------------------------------------------------------------
    Address (lldb::addr_t file_addr, const SectionList * section_list);

    //------------------------------------------------------------------
    /// Assignment operator.
    ///
    /// Copies the address value from another Address object \a rhs
    /// into \a this object.
    ///
    /// @param[in] rhs
    ///     A const Address object reference to copy.
    ///
    /// @return
    ///     A const Address object reference to \a this.
    //------------------------------------------------------------------
#ifndef SWIG
    const Address&
    operator= (const Address& rhs);
#endif
    //------------------------------------------------------------------
    /// Clear the object's state.
    ///
    /// Sets the section to an invalid value (NULL) and an invalid
    /// offset (LLDB_INVALID_ADDRESS).
    //------------------------------------------------------------------
    void
    Clear ()
    {
        m_section = NULL;
        m_offset = LLDB_INVALID_ADDRESS;
    }

    //------------------------------------------------------------------
    /// Compare two Address objects.
    ///
    /// @param[in] lhs
    ///     The Left Hand Side const Address object reference.
    ///
    /// @param[in] rhs
    ///     The Right Hand Side const Address object reference.
    ///
    /// @return
    ///     @li -1 if lhs < rhs
    ///     @li 0 if lhs == rhs
    ///     @li 1 if lhs > rhs
    //------------------------------------------------------------------
    static int
    CompareFileAddress (const Address& lhs, const Address& rhs);

    static int
    CompareLoadAddress (const Address& lhs, const Address& rhs, Target *target);

    static int
    CompareModulePointerAndOffset (const Address& lhs, const Address& rhs);

    // For use with std::map, std::multi_map
    class ModulePointerAndOffsetLessThanFunctionObject
    {
    public:
        ModulePointerAndOffsetLessThanFunctionObject () {}

        bool
        operator() (const Address& a, const Address& b) const
        {
            return Address::CompareModulePointerAndOffset(a, b) < 0;
        }
    };

    //------------------------------------------------------------------
    /// Dump a description of this object to a Stream.
    ///
    /// Dump a description of the contents of this object to the
    /// supplied stream \a s. There are many ways to display a section
    /// offset based address, and \a style lets the user choose.
    ///
    /// @param[in] s
    ///     The stream to which to dump the object descripton.
    ///
    /// @param[in] style
    ///     The display style for the address.
    ///
    /// @param[in] fallback_style
    ///     The display style for the address.
    ///
    /// @return
    ///     Returns \b true if the address was able to be displayed.
    ///     File and load addresses may be unresolved and it may not be
    ///     possible to display a valid value, \b false will be returned
    ///     in such cases.
    ///
    /// @see Address::DumpStyle
    //------------------------------------------------------------------
    bool
    Dump (Stream *s,
          ExecutionContextScope *exe_scope,
          DumpStyle style,
          DumpStyle fallback_style = DumpStyleInvalid,
          uint32_t addr_byte_size = UINT32_MAX) const;

    //------------------------------------------------------------------
    /// Get the file address.
    ///
    /// If an address comes from a file on disk that has section
    /// relative addresses, then it has a virtual address that is
    /// relative to unique section in the object file.
    ///
    /// @return
    ///     The valid file virtual address, or LLDB_INVALID_ADDRESS if
    ///     the address doesn't have a file virtual address (image is
    ///     from memory only with no representation on disk).
    //------------------------------------------------------------------
    lldb::addr_t
    GetFileAddress () const;

    //------------------------------------------------------------------
    /// Get the load address.
    ///
    /// If an address comes from a file on disk that has section
    /// relative addresses, then it has a virtual address that is
    /// relative to unique section in the object file. Sections get
    /// resolved at runtime by DynamicLoader plug-ins as images
    /// (executables and shared libraries) get loaded/unloaded. If a
    /// section is loaded, then the load address can be resolved.
    ///
    /// @return
    ///     The valid load virtual address, or LLDB_INVALID_ADDRESS if
    ///     the address is currently not loaded.
    //------------------------------------------------------------------
    lldb::addr_t
    GetLoadAddress (Target *target) const;

    //------------------------------------------------------------------
    /// Get the section relative offset value.
    ///
    /// @return
    ///     The current offset, or LLDB_INVALID_ADDRESS if this address
    ///     doesn't contain a valid offset.
    //------------------------------------------------------------------
    lldb::addr_t
    GetOffset () const { return m_offset; }

    //------------------------------------------------------------------
    /// Check if an address is section offset.
    ///
    /// When converting a virtual file or load address into a section
    /// offset based address, we often need to know if, given a section
    /// list, if the address was able to be converted to section offset.
    /// This function returns true if the current value contained in
    /// this object is section offset based.
    ///
    /// @return
    ///     Returns \b true if the address has a valid section and
    ///     offset, \b false otherwise.
    //------------------------------------------------------------------
    bool
    IsSectionOffset() const
    {
        return m_section != NULL && IsValid();
    }

    //------------------------------------------------------------------
    /// Check if the object state is valid.
    ///
    /// A valid Address object contains either a section pointer and
    /// and offset (for section offset based addresses), or just a valid
    /// offset (for absolute addresses that have no section).
    ///
    /// @return
    ///     Returns \b true if the the offset is valid, \b false
    ///     otherwise.
    //------------------------------------------------------------------
    bool
    IsValid() const
    {
        return m_offset != LLDB_INVALID_ADDRESS;
    }


    //------------------------------------------------------------------
    /// Get the memory cost of this object.
    ///
    /// @return
    ///     The number of bytes that this object occupies in memory.
    //------------------------------------------------------------------
    size_t
    MemorySize () const;

    //------------------------------------------------------------------
    /// Resolve a file virtual address using a section list.
    ///
    /// Given a list of sections, attempt to resolve \a addr as a
    /// an offset into one of the file sections.
    ///
    /// @return
    ///     Returns \b true if \a addr was able to be resolved, \b false
    ///     otherwise.
    //------------------------------------------------------------------
    bool
    ResolveAddressUsingFileSections (lldb::addr_t addr, const SectionList *sections);

    bool
    IsLinkedAddress () const;

    void
    ResolveLinkedAddress ();

    //------------------------------------------------------------------
    /// Get accessor for the module for this address.
    ///
    /// @return
    ///     Returns the Module pointer that this address is an offset
    ///     in, or NULL if this address doesn't belong in a module, or
    ///     isn't resolved yet.
    //------------------------------------------------------------------
    Module *
    GetModule () const;

    //------------------------------------------------------------------
    /// Get const accessor for the section.
    ///
    /// @return
    ///     Returns the const lldb::Section pointer that this address is an
    ///     offset in, or NULL if this address is absolute.
    //------------------------------------------------------------------
    const Section*
    GetSection() const { return m_section; }

    //------------------------------------------------------------------
    /// Set accessor for the offset.
    ///
    /// @param[in] offset
    ///     A new offset value for this object.
    ///
    /// @return
    ///     Returns \b true if the offset changed, \b false otherwise.
    //------------------------------------------------------------------
    bool
    SetOffset (lldb::addr_t offset)
    {
        bool changed = m_offset != offset;
        m_offset = offset;
        return changed;
    }

    bool
    Slide (int64_t offset)
    {
        if (m_offset != LLDB_INVALID_ADDRESS)
        {
            m_offset += offset;
            return true;
        }
        return false;
    }

    //------------------------------------------------------------------
    /// Set accessor for the section.
    ///
    /// @param[in] section
    ///     A new lldb::Section pointer to use as the section base. Can
    ///     be NULL for absolute addresses that are not relative to
    ///     any section.
    //------------------------------------------------------------------
    void
    SetSection (const Section* section) { m_section = section; }

    //------------------------------------------------------------------
    /// Reconstruct a symbol context from an address.
    ///
    /// This class doesn't inherit from SymbolContextScope because many
    /// address objects have short lifespans. Address objects that are
    /// section offset can reconstruct their symbol context by looking
    /// up the address in the module found in the section.
    ///
    /// @see SymbolContextScope::CalculateSymbolContext(SymbolContext*)
    //------------------------------------------------------------------
    void
    CalculateSymbolContext (SymbolContext *sc);

protected:
    //------------------------------------------------------------------
    // Member variables.
    //------------------------------------------------------------------
    const Section* m_section;   ///< The section for the address, can be NULL.
    lldb::addr_t m_offset;      ///< Offset into section if \a m_section != NULL, else the absolute address value.
};


//----------------------------------------------------------------------
// NOTE: Be careful using this operator. It can correctly compare two 
// addresses from the same Module correctly. It can't compare two 
// addresses from different modules in any meaningful way, but it will
// compare the module pointers.
// 
// To sum things up:
// - works great for addresses within the same module
// - it works for addresses across multiple modules, but don't expect the
//   address results to make much sense
//
// This basically lets Address objects be used in ordered collection 
// classes.
//----------------------------------------------------------------------
bool operator<  (const Address& lhs, const Address& rhs);
bool operator>  (const Address& lhs, const Address& rhs);



bool operator== (const Address& lhs, const Address& rhs);
bool operator!= (const Address& lhs, const Address& rhs);

} // namespace lldb_private

#endif  // liblldb_Address_h_
