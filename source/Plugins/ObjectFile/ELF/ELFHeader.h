//===-- ELFHeader.h ------------------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// @brief Generic structures and typedefs for ELF files.
///
/// This file provides definitions for the various entities comprising an ELF
/// file.  The structures are generic in the sense that they do not correspond
/// to the exact binary layout of an ELF, but can be used to hold the
/// information present in both 32 and 64 bit variants of the format.  Each
/// entity provides a \c Parse method which is capable of transparently reading
/// both 32 and 64 bit instances of the object.
//===----------------------------------------------------------------------===//

#ifndef liblldb_ELFHeader_h_
#define liblldb_ELFHeader_h_

#include "llvm/Support/ELF.h"

#include "lldb/lldb-enumerations.h"

namespace lldb_private
{
class DataExtractor;
} // End namespace lldb_private.

namespace elf 
{

//------------------------------------------------------------------------------
/// @name ELF type definitions.
///
/// Types used to represent the various components of ELF structures.  All types
/// are signed or unsigned integral types wide enough to hold values from both
/// 32 and 64 bit ELF variants.
//@{
typedef uint64_t elf_addr;
typedef uint64_t elf_off;
typedef uint16_t elf_half;
typedef uint32_t elf_word;
typedef int32_t  elf_sword;
typedef uint64_t elf_size;
typedef uint64_t elf_xword;
typedef int64_t  elf_sxword;
//@}

//------------------------------------------------------------------------------
/// @class ELFHeader
/// @brief Generic representation of an ELF file header.
///
/// This object is used to identify the general attributes on an ELF file and to
/// locate additional sections within the file.
struct ELFHeader 
{
    unsigned char e_ident[llvm::ELF::EI_NIDENT]; ///< ELF file identification.
    elf_addr      e_entry;            ///< Virtual address program entry point.
    elf_off       e_phoff;            ///< File offset of program header table.
    elf_off       e_shoff;            ///< File offset of section header table.
    elf_word      e_flags;            ///< Processor specific flags.
    elf_word      e_version;          ///< Version of object file (always 1).
    elf_half      e_type;             ///< Object file type.
    elf_half      e_machine;          ///< Target architecture.
    elf_half      e_ehsize;           ///< Byte size of the ELF header.
    elf_half      e_phentsize;        ///< Size of a program header table entry.
    elf_half      e_phnum;            ///< Number of program header entrys.
    elf_half      e_shentsize;        ///< Size of a section header table entry.
    elf_half      e_shnum;            ///< Number of section header entrys.
    elf_half      e_shstrndx;         ///< String table section index.

    ELFHeader();

    //--------------------------------------------------------------------------
    /// Returns true if this is a 32 bit ELF file header.
    ///
    /// @return
    ///    True if this is a 32 bit ELF file header.
    bool Is32Bit() const { 
        return e_ident[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS32; 
    }

    //--------------------------------------------------------------------------
    /// Returns true if this is a 64 bit ELF file header.
    ///
    /// @return
    ///   True if this is a 64 bit ELF file header.
    bool Is64Bit() const { 
        return e_ident[llvm::ELF::EI_CLASS] == llvm::ELF::ELFCLASS64; 
    }

    //--------------------------------------------------------------------------
    /// The byte order of this ELF file header.
    ///
    /// @return
    ///    The byte order of this ELF file as described by the header.
    lldb::ByteOrder
    GetByteOrder() const;

    //--------------------------------------------------------------------------
    /// Parse an ELFSectionHeader entry starting at position \p offset and
    /// update the data extractor with the address size and byte order
    /// attributes as defined by the header.
    ///
    /// @param[in,out] data
    ///    The DataExtractor to read from.  Updated with the address size and
    ///    byte order attributes appropriate to this header.
    ///
    /// @param[in,out] offset
    ///    Pointer to an offset in the data.  On return the offset will be
    ///    advanced by the number of bytes read.
    ///
    /// @return
    ///    True if the ELFSectionHeader was successfully read and false
    ///    otherwise.
    bool
    Parse(lldb_private::DataExtractor &data, uint32_t *offset);

    //--------------------------------------------------------------------------
    /// Examines at most EI_NIDENT bytes starting from the given pointer and
    /// determines if the magic ELF identification exists.
    ///
    /// @return
    ///    True if the given sequence of bytes identifies an ELF file.
    static bool
    MagicBytesMatch(const uint8_t *magic);

    //--------------------------------------------------------------------------
    /// Examines at most EI_NIDENT bytes starting from the given address and
    /// determines the address size of the underlying ELF file.  This function
    /// should only be called on an pointer for which MagicBytesMatch returns
    /// true.
    ///
    /// @return
    ///    The number of bytes forming an address in the ELF file (either 4 or
    ///    8), else zero if the address size could not be determined.
    static unsigned
    AddressSizeInBytes(const uint8_t *magic);
};

//------------------------------------------------------------------------------
/// @class ELFSectionHeader
/// @brief Generic representation of an ELF section header.
struct ELFSectionHeader 
{
    elf_word  sh_name;          ///< Section name string index.
    elf_word  sh_type;          ///< Section type.
    elf_xword sh_flags;         ///< Section attributes. 
    elf_addr  sh_addr;          ///< Virtual address of the section in memory.
    elf_off   sh_offset;        ///< Start of section from beginning of file.
    elf_xword sh_size;          ///< Number of bytes occupied in the file.
    elf_word  sh_link;          ///< Index of associated section.
    elf_word  sh_info;          ///< Extra section info (overloaded).
    elf_xword sh_addralign;     ///< Power of two alignment constraint.
    elf_xword sh_entsize;       ///< Byte size of each section entry.

    ELFSectionHeader();

    //--------------------------------------------------------------------------
    /// Parse an ELFSectionHeader entry from the given DataExtracter starting at
    /// position \p offset.
    ///
    /// @param[in] data
    ///    The DataExtractor to read from.  The address size of the extractor
    ///    determines if a 32 or 64 bit object should be read.
    ///
    /// @param[in,out] offset
    ///    Pointer to an offset in the data.  On return the offset will be
    ///    advanced by the number of bytes read.
    ///
    /// @return
    ///    True if the ELFSectionHeader was successfully read and false
    ///    otherwise.
    bool
    Parse(const lldb_private::DataExtractor &data, uint32_t *offset);
};

//------------------------------------------------------------------------------
/// @class ELFProgramHeader
/// @brief Generic representation of an ELF program header.
struct ELFProgramHeader
{
    elf_word  p_type;           ///< Type of program segement.
    elf_word  p_flags;          ///< Segement attibutes.
    elf_off   p_offset;         ///< Start of segment from begining of file.
    elf_addr  p_vaddr;          ///< Virtual address of segment in memory.
    elf_addr  p_paddr;          ///< Physical address (for non-VM systems). 
    elf_xword p_filesz;         ///< Byte size of the segment in file.
    elf_xword p_memsz;          ///< Byte size of the segment in memory.
    elf_xword p_align;          ///< Segement alignement constraint.

    ELFProgramHeader();

    /// Parse an ELFProgramHeader entry from the given DataExtracter starting at
    /// position \p offset.  The address size of the DataExtracter determines if
    /// a 32 or 64 bit object is to be parsed.
    ///
    /// @param[in] data
    ///    The DataExtractor to read from.  The address size of the extractor
    ///    determines if a 32 or 64 bit object should be read.
    ///
    /// @param[in,out] offset
    ///    Pointer to an offset in the data.  On return the offset will be
    ///    advanced by the number of bytes read.
    ///
    /// @return
    ///    True if the ELFProgramHeader was successfully read and false
    ///    otherwise.
    bool
    Parse(const lldb_private::DataExtractor &data, uint32_t *offset);
};

//------------------------------------------------------------------------------
/// @class ELFSymbol
/// @brief Represents a symbol within an ELF symbol table.
struct ELFSymbol
{
    elf_addr      st_value;     ///< Absolute or relocatable address.
    elf_xword     st_size;      ///< Size of the symbol or zero.
    elf_word      st_name;      ///< Symbol name string index.
    unsigned char st_info;      ///< Symbol type and binding attributes.
    unsigned char st_other;     ///< Reserved for future use.
    elf_half      st_shndx;     ///< Section to which this symbol applies.

    ELFSymbol();

    /// Returns the binding attribute of the st_info member.
    unsigned char getBinding() const { return st_info >> 4; }

    /// Returns the type attribute of the st_info member.
    unsigned char getType() const { return st_info & 0x0F; }

    /// Sets the bining and type of the st_info member.
    void setBindingAndType(unsigned char binding, unsigned char type) {
        st_info = (binding << 4) + (type & 0x0F);
    }

    /// Parse an ELFSymbol entry from the given DataExtracter starting at
    /// position \p offset.  The address size of the DataExtracter determines if
    /// a 32 or 64 bit object is to be parsed.
    ///
    /// @param[in] data
    ///    The DataExtractor to read from.  The address size of the extractor
    ///    determines if a 32 or 64 bit object should be read.
    ///
    /// @param[in,out] offset
    ///    Pointer to an offset in the data.  On return the offset will be
    ///    advanced by the number of bytes read.
    ///
    /// @return
    ///    True if the ELFSymbol was successfully read and false otherwise.
    bool
    Parse(const lldb_private::DataExtractor &data, uint32_t *offset);
};

//------------------------------------------------------------------------------
/// @class ELFDynamic
/// @brief Represents an entry in an ELF dynamic table.
struct ELFDynamic
{
    elf_sxword d_tag;           ///< Type of dynamic table entry.
    union
    {
        elf_xword d_val;        ///< Integer value of the table entry.
        elf_addr  d_ptr;        ///< Pointer value of the table entry.
    };

    ELFDynamic();

    /// Parse an ELFDynamic entry from the given DataExtracter starting at
    /// position \p offset.  The address size of the DataExtracter determines if
    /// a 32 or 64 bit object is to be parsed.
    ///
    /// @param[in] data
    ///    The DataExtractor to read from.  The address size of the extractor
    ///    determines if a 32 or 64 bit object should be read.
    ///
    /// @param[in,out] offset
    ///    Pointer to an offset in the data.  On return the offset will be
    ///    advanced by the number of bytes read.
    ///
    /// @return
    ///    True if the ELFDynamic entry was successfully read and false
    ///    otherwise.
    bool
    Parse(const lldb_private::DataExtractor &data, uint32_t *offset);
};

} // End namespace elf.

#endif // #ifndef liblldb_ELFHeader_h_
