//===-- ArchSpec.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ArchSpec.h"

#include <stdio.h>

#include <string>

#include "llvm/Support/ELF.h"
#include "llvm/Support/MachO.h"
#include "lldb/Host/Endian.h"
#include "lldb/Host/Host.h"

using namespace lldb;
using namespace lldb_private;

#define ARCH_SPEC_SEPARATOR_CHAR '-'

namespace lldb_private {

    struct CoreDefinition
    {
        ByteOrder default_byte_order;
        uint32_t addr_byte_size;
        llvm::Triple::ArchType machine;
        ArchSpec::Core core;
        const char *name;
    };

}

// This core information can be looked using the ArchSpec::Core as the index
static const CoreDefinition g_core_definitions[ArchSpec::kNumCores] =
{
    { eByteOrderLittle, 4, llvm::Triple::alpha  , ArchSpec::eCore_alpha_generic   , "alpha"     },

    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_generic     , "arm"       },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv4       , "armv4"     },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv4t      , "armv4t"    },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv5       , "armv5"     },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv5t      , "armv5t"    },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv6       , "armv6"     },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_armv7       , "armv7"     },
    { eByteOrderLittle, 4, llvm::Triple::arm    , ArchSpec::eCore_arm_xscale      , "xscale"    },
    
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_generic     , "ppc"       },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc601      , "ppc601"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc602      , "ppc602"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc603      , "ppc603"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc603e     , "ppc603e"   },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc603ev    , "ppc603ev"  },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc604      , "ppc604"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc604e     , "ppc604e"   },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc620      , "ppc620"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc750      , "ppc750"    },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc7400     , "ppc7400"   },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc7450     , "ppc7450"   },
    { eByteOrderLittle, 4, llvm::Triple::ppc    , ArchSpec::eCore_ppc_ppc970      , "ppc970"    },
    
    { eByteOrderLittle, 8, llvm::Triple::ppc64  , ArchSpec::eCore_ppc64_generic   , "ppc64"     },
    { eByteOrderLittle, 8, llvm::Triple::ppc64  , ArchSpec::eCore_ppc64_ppc970_64 , "ppc970-64" },
    
    { eByteOrderLittle, 4, llvm::Triple::sparc  , ArchSpec::eCore_sparc_generic   , "sparc"     },
    { eByteOrderLittle, 8, llvm::Triple::sparcv9, ArchSpec::eCore_sparc9_generic  , "sparcv9"   },

    { eByteOrderLittle, 4, llvm::Triple::x86    , ArchSpec::eCore_x86_32_i386     , "i386"      },
    { eByteOrderLittle, 4, llvm::Triple::x86    , ArchSpec::eCore_x86_32_i486     , "i486"      },
    { eByteOrderLittle, 4, llvm::Triple::x86    , ArchSpec::eCore_x86_32_i486sx   , "i486sx"    },

    { eByteOrderLittle, 8, llvm::Triple::x86_64 , ArchSpec::eCore_x86_64_x86_64   , "x86_64"    }
};

struct ArchDefinitionEntry
{
    ArchSpec::Core core;
    uint32_t cpu;
    uint32_t sub;
};

struct ArchDefinition
{
    ArchitectureType type;
    size_t num_entries;
    const ArchDefinitionEntry *entries;
    uint32_t cpu_mask;
    uint32_t sub_mask;
    const char *name;
};


#define CPU_ANY (UINT32_MAX)

//===----------------------------------------------------------------------===//
// A table that gets searched linearly for matches. This table is used to
// convert cpu type and subtypes to architecture names, and to convert
// architecture names to cpu types and subtypes. The ordering is important and
// allows the precedence to be set when the table is built.
static const ArchDefinitionEntry g_macho_arch_entries[] =
{
    { ArchSpec::eCore_arm_generic     , llvm::MachO::CPUTypeARM       , CPU_ANY },
    { ArchSpec::eCore_arm_generic     , llvm::MachO::CPUTypeARM       , 0       },
    { ArchSpec::eCore_arm_armv4       , llvm::MachO::CPUTypeARM       , 5       },
    { ArchSpec::eCore_arm_armv6       , llvm::MachO::CPUTypeARM       , 6       },
    { ArchSpec::eCore_arm_armv5       , llvm::MachO::CPUTypeARM       , 7       },
    { ArchSpec::eCore_arm_xscale      , llvm::MachO::CPUTypeARM       , 8       },
    { ArchSpec::eCore_arm_armv7       , llvm::MachO::CPUTypeARM       , 9       },
    { ArchSpec::eCore_ppc_generic     , llvm::MachO::CPUTypePowerPC   , CPU_ANY },
    { ArchSpec::eCore_ppc_generic     , llvm::MachO::CPUTypePowerPC   , 0       },
    { ArchSpec::eCore_ppc_ppc601      , llvm::MachO::CPUTypePowerPC   , 1       },
    { ArchSpec::eCore_ppc_ppc602      , llvm::MachO::CPUTypePowerPC   , 2       },
    { ArchSpec::eCore_ppc_ppc603      , llvm::MachO::CPUTypePowerPC   , 3       },
    { ArchSpec::eCore_ppc_ppc603e     , llvm::MachO::CPUTypePowerPC   , 4       },
    { ArchSpec::eCore_ppc_ppc603ev    , llvm::MachO::CPUTypePowerPC   , 5       },
    { ArchSpec::eCore_ppc_ppc604      , llvm::MachO::CPUTypePowerPC   , 6       },
    { ArchSpec::eCore_ppc_ppc604e     , llvm::MachO::CPUTypePowerPC   , 7       },
    { ArchSpec::eCore_ppc_ppc620      , llvm::MachO::CPUTypePowerPC   , 8       },
    { ArchSpec::eCore_ppc_ppc750      , llvm::MachO::CPUTypePowerPC   , 9       },
    { ArchSpec::eCore_ppc_ppc7400     , llvm::MachO::CPUTypePowerPC   , 10      },
    { ArchSpec::eCore_ppc_ppc7450     , llvm::MachO::CPUTypePowerPC   , 11      },
    { ArchSpec::eCore_ppc_ppc970      , llvm::MachO::CPUTypePowerPC   , 100     },
    { ArchSpec::eCore_ppc64_generic   , llvm::MachO::CPUTypePowerPC64 , 0       },
    { ArchSpec::eCore_ppc64_ppc970_64 , llvm::MachO::CPUTypePowerPC64 , 100     },
    { ArchSpec::eCore_x86_32_i386     , llvm::MachO::CPUTypeI386      , 3       },
    { ArchSpec::eCore_x86_32_i486     , llvm::MachO::CPUTypeI386      , 4       },
    { ArchSpec::eCore_x86_32_i486sx   , llvm::MachO::CPUTypeI386      , 0x84    },
    { ArchSpec::eCore_x86_32_i386     , llvm::MachO::CPUTypeI386      , CPU_ANY },
    { ArchSpec::eCore_x86_64_x86_64   , llvm::MachO::CPUTypeX86_64    , 3       },
    { ArchSpec::eCore_x86_64_x86_64   , llvm::MachO::CPUTypeX86_64    , CPU_ANY }
};
static const ArchDefinition g_macho_arch_def = {
    eArchTypeMachO,
    sizeof(g_macho_arch_entries)/sizeof(g_macho_arch_entries[0]),
    g_macho_arch_entries,
    UINT32_MAX,     // CPU type mask
    0x00FFFFFFu,    // CPU subtype mask
    "mach-o"
};

//===----------------------------------------------------------------------===//
// A table that gets searched linearly for matches. This table is used to
// convert cpu type and subtypes to architecture names, and to convert
// architecture names to cpu types and subtypes. The ordering is important and
// allows the precedence to be set when the table is built.
static const ArchDefinitionEntry g_elf_arch_entries[] =
{
    { ArchSpec::eCore_sparc_generic   , llvm::ELF::EM_SPARC  , LLDB_INVALID_CPUTYPE }, // Sparc
    { ArchSpec::eCore_x86_32_i386     , llvm::ELF::EM_386    , LLDB_INVALID_CPUTYPE }, // Intel 80386
    { ArchSpec::eCore_x86_32_i486     , llvm::ELF::EM_486    , LLDB_INVALID_CPUTYPE }, // Intel 486 (deprecated)
    { ArchSpec::eCore_ppc_generic     , llvm::ELF::EM_PPC    , LLDB_INVALID_CPUTYPE }, // PowerPC
    { ArchSpec::eCore_ppc64_generic   , llvm::ELF::EM_PPC64  , LLDB_INVALID_CPUTYPE }, // PowerPC64
    { ArchSpec::eCore_arm_generic     , llvm::ELF::EM_ARM    , LLDB_INVALID_CPUTYPE }, // ARM
    { ArchSpec::eCore_alpha_generic   , llvm::ELF::EM_ALPHA  , LLDB_INVALID_CPUTYPE }, // DEC Alpha
    { ArchSpec::eCore_sparc9_generic  , llvm::ELF::EM_SPARCV9, LLDB_INVALID_CPUTYPE }, // SPARC V9
    { ArchSpec::eCore_x86_64_x86_64   , llvm::ELF::EM_X86_64 , LLDB_INVALID_CPUTYPE }, // AMD64
};

static const ArchDefinition g_elf_arch_def = {
    eArchTypeELF,
    sizeof(g_elf_arch_entries)/sizeof(g_elf_arch_entries[0]),
    g_elf_arch_entries,
    UINT32_MAX,     // CPU type mask
    UINT32_MAX,     // CPU subtype mask
    "elf",
};

//===----------------------------------------------------------------------===//
// Table of all ArchDefinitions
static const ArchDefinition *g_arch_definitions[] = {
    &g_macho_arch_def,
    &g_elf_arch_def,
};

static const size_t k_num_arch_definitions =
    sizeof(g_arch_definitions) / sizeof(g_arch_definitions[0]);

//===----------------------------------------------------------------------===//
// Static helper functions.


// Get the architecture definition for a given object type.
static const ArchDefinition *
FindArchDefinition (ArchitectureType arch_type)
{
    for (unsigned int i = 0; i < k_num_arch_definitions; ++i)
    {
        const ArchDefinition *def = g_arch_definitions[i];
        if (def->type == arch_type)
            return def;
    }
    return NULL;
}

// Get an architecture definition by name.
static const CoreDefinition *
FindCoreDefinition (llvm::StringRef name)
{
    for (unsigned int i = 0; i < ArchSpec::kNumCores; ++i)
    {
        if (name.equals_lower(g_core_definitions[i].name))
            return &g_core_definitions[i];
    }
    return NULL;
}

static inline const CoreDefinition *
FindCoreDefinition (ArchSpec::Core core)
{
    if (core >= 0 && core < ArchSpec::kNumCores)
        return &g_core_definitions[core];
    return NULL;
}

// Get a definition entry by cpu type and subtype.
static const ArchDefinitionEntry *
FindArchDefinitionEntry (const ArchDefinition *def, uint32_t cpu, uint32_t sub)
{
    if (def == NULL)
        return NULL;

    const uint32_t cpu_mask = def->cpu_mask;
    const uint32_t sub_mask = def->sub_mask;
    const ArchDefinitionEntry *entries = def->entries;
    for (size_t i = 0; i < def->num_entries; ++i)
    {
        if ((entries[i].cpu == (cpu_mask & cpu)) &&
            (entries[i].sub == (sub_mask & sub)))
            return &entries[i];
    }
    return NULL;
}

static const ArchDefinitionEntry *
FindArchDefinitionEntry (const ArchDefinition *def, ArchSpec::Core core)
{
    if (def == NULL)
        return NULL;
    
    const ArchDefinitionEntry *entries = def->entries;
    for (size_t i = 0; i < def->num_entries; ++i)
    {
        if (entries[i].core == core)
            return &entries[i];
    }
    return NULL;
}

//===----------------------------------------------------------------------===//
// Constructors and destructors.

ArchSpec::ArchSpec() :
    m_triple (),
    m_core (kCore_invalid),
    m_byte_order (eByteOrderInvalid)
{
}

ArchSpec::ArchSpec (const char *triple_cstr) :
    m_triple (),
    m_core (kCore_invalid),
    m_byte_order (eByteOrderInvalid)
{
    if (triple_cstr)
        SetTriple(triple_cstr);
}

ArchSpec::ArchSpec(const llvm::Triple &triple) :
    m_triple (),
    m_core (kCore_invalid),
    m_byte_order (eByteOrderInvalid)
{
    SetTriple(triple);
}

ArchSpec::ArchSpec (lldb::ArchitectureType arch_type, uint32_t cpu, uint32_t subtype) :
    m_triple (),
    m_core (kCore_invalid),
    m_byte_order (eByteOrderInvalid)
{
    SetArchitecture (arch_type, cpu, subtype);
}

ArchSpec::~ArchSpec()
{
}

//===----------------------------------------------------------------------===//
// Assignment and initialization.

const ArchSpec&
ArchSpec::operator= (const ArchSpec& rhs)
{
    if (this != &rhs)
    {
        m_triple = rhs.m_triple;
        m_core = rhs.m_core;
        m_byte_order = rhs.m_byte_order;
    }
    return *this;
}

void
ArchSpec::Clear()
{
    m_triple = llvm::Triple();
    m_core = kCore_invalid;
    m_byte_order = eByteOrderInvalid;
}

//===----------------------------------------------------------------------===//
// Predicates.


const char *
ArchSpec::GetArchitectureName () const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
        return core_def->name;
    return "unknown";
}

uint32_t
ArchSpec::GetMachOCPUType () const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
    {
        const ArchDefinitionEntry *arch_def = FindArchDefinitionEntry (&g_macho_arch_def, core_def->core);
        if (arch_def)
        {
            return arch_def->cpu;
        }
    }
    return LLDB_INVALID_CPUTYPE;
}

uint32_t
ArchSpec::GetMachOCPUSubType () const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
    {
        const ArchDefinitionEntry *arch_def = FindArchDefinitionEntry (&g_macho_arch_def, core_def->core);
        if (arch_def)
        {
            return arch_def->cpu;
        }
    }
    return LLDB_INVALID_CPUTYPE;
}

llvm::Triple::ArchType
ArchSpec::GetMachine () const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
        return core_def->machine;

    return llvm::Triple::UnknownArch;
}

uint32_t
ArchSpec::GetAddressByteSize() const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
        return core_def->addr_byte_size;
    return 0;
}

ByteOrder
ArchSpec::GetDefaultEndian () const
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
        return core_def->default_byte_order;
    return eByteOrderInvalid;
}

lldb::ByteOrder
ArchSpec::GetByteOrder () const
{
    if (m_byte_order == eByteOrderInvalid)
        return GetDefaultEndian();
    return m_byte_order;
}

//===----------------------------------------------------------------------===//
// Mutators.

bool
ArchSpec::SetTriple (const llvm::Triple &triple)
{
    m_triple = triple;
    
    llvm::StringRef arch_name (m_triple.getArchName());
    const CoreDefinition *core_def = FindCoreDefinition (arch_name);
    if (core_def)
    {
        m_core = core_def->core;
        m_byte_order = core_def->default_byte_order;

        // If the vendor, OS or environment aren't specified, default to the system?
        const ArchSpec &host_arch_ref = Host::GetArchitecture (Host::eSystemDefaultArchitecture);
        if (m_triple.getVendor() == llvm::Triple::UnknownVendor)
            m_triple.setVendor(host_arch_ref.GetTriple().getVendor());
        if (m_triple.getOS() == llvm::Triple::UnknownOS)
            m_triple.setOS(host_arch_ref.GetTriple().getOS());
        if (m_triple.getEnvironment() == llvm::Triple::UnknownEnvironment)
            m_triple.setEnvironment(host_arch_ref.GetTriple().getEnvironment());
    }
    else
    {
        Clear();
    }

    
    return IsValid();
}

bool
ArchSpec::SetTriple (const char *triple_cstr)
{
    if (triple_cstr || triple_cstr[0])
    {
        llvm::StringRef triple_stref (triple_cstr);
        if (triple_stref.startswith (LLDB_ARCH_DEFAULT))
        {
            // Special case for the current host default architectures...
            if (triple_stref.equals (LLDB_ARCH_DEFAULT_32BIT))
                *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture32);
            else if (triple_stref.equals (LLDB_ARCH_DEFAULT_64BIT))
                *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture64);
            else if (triple_stref.equals (LLDB_ARCH_DEFAULT))
                *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture);
        }
        else
        {
            std::string normalized_triple_sstr (llvm::Triple::normalize(triple_stref));
            triple_stref = normalized_triple_sstr;
            SetTriple (llvm::Triple (triple_stref));
        }
    }
    else
        Clear();
    return IsValid();
}

//bool
//ArchSpec::SetArchitecture (const char *arch_name)
//{
//    return SetArchitecture(llvm::StringRef (arch_name));
//}
//
//bool
//ArchSpec::SetArchitecture (const llvm::StringRef& arch_name)
//{
//    // All default architecture names start with LLDB_ARCH_DEFAULT.
//    if (arch_name.startswith (LLDB_ARCH_DEFAULT))
//    {
//        // Special case for the current host default architectures...
//        if (arch_name.equals (LLDB_ARCH_DEFAULT_32BIT))
//            *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture32);
//        else if (arch_name.equals (LLDB_ARCH_DEFAULT_64BIT))
//            *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture64);
//        else
//            *this = Host::GetArchitecture (Host::eSystemDefaultArchitecture);
//    }
//    else
//    {
//        const CoreDefinition *core_def = FindCoreDefinition (arch_name);
//        if (core_def)
//            m_core = core_def->core;
//        CoreUpdated(true);
//    }
//    return IsValid();
//}
//
bool
ArchSpec::SetArchitecture (lldb::ArchitectureType arch_type, uint32_t cpu, uint32_t sub)
{
    m_core = kCore_invalid;
    bool update_triple = true;
    const ArchDefinition *arch_def = FindArchDefinition(arch_type);
    if (arch_def)
    {
        const ArchDefinitionEntry *arch_def_entry = FindArchDefinitionEntry (arch_def, cpu, sub);
        if (arch_def_entry)
        {
            const CoreDefinition *core_def = FindCoreDefinition (arch_def_entry->core);
            if (core_def)
            {
                m_core = core_def->core;
                update_triple = false;
                m_triple.setArch (core_def->machine);
                if (arch_type == eArchTypeMachO)
                {
                    m_triple.setVendor (llvm::Triple::Apple);
                    m_triple.setOS (llvm::Triple::Darwin);
                }
                else
                {
                    m_triple.setVendor (llvm::Triple::UnknownVendor);
                    m_triple.setOS (llvm::Triple::UnknownOS);
                }
            }
        }
    }
    CoreUpdated(update_triple);
    return IsValid();
}

void
ArchSpec::SetByteOrder (lldb::ByteOrder byte_order)
{
    m_byte_order = byte_order;
}

//===----------------------------------------------------------------------===//
// Helper methods.

void
ArchSpec::CoreUpdated (bool update_triple)
{
    const CoreDefinition *core_def = FindCoreDefinition (m_core);
    if (core_def)
    {
        if (update_triple)
            m_triple = llvm::Triple(core_def->name, "unknown", "unknown");
        m_byte_order = core_def->default_byte_order;
    }
    else
    {
        if (update_triple)
            m_triple = llvm::Triple();
        m_byte_order = eByteOrderInvalid;
    }
}

//===----------------------------------------------------------------------===//
// Operators.

bool
lldb_private::operator== (const ArchSpec& lhs, const ArchSpec& rhs)
{
    const ArchSpec::Core lhs_core = lhs.GetCore ();
    const ArchSpec::Core rhs_core = rhs.GetCore ();

    if (lhs_core == rhs_core)
        return true;

    if (lhs_core == ArchSpec::kCore_any || rhs_core == ArchSpec::kCore_any)
        return true;

    if (lhs_core == ArchSpec::kCore_arm_any)
    {
        if ((rhs_core >= ArchSpec::kCore_arm_first && rhs_core <= ArchSpec::kCore_arm_last) || (rhs_core == ArchSpec::kCore_arm_any))
            return true;
    }
    else if (rhs_core == ArchSpec::kCore_arm_any)
    {
        if ((lhs_core >= ArchSpec::kCore_arm_first && lhs_core <= ArchSpec::kCore_arm_last) || (lhs_core == ArchSpec::kCore_arm_any))
            return true;
    }
    else if (lhs_core == ArchSpec::kCore_x86_32_any)
    {
        if ((rhs_core >= ArchSpec::kCore_x86_32_first && rhs_core <= ArchSpec::kCore_x86_32_last) || (rhs_core == ArchSpec::kCore_x86_32_any))
            return true;
    }
    else if (rhs_core == ArchSpec::kCore_x86_32_any)
    {
        if ((lhs_core >= ArchSpec::kCore_x86_32_first && lhs_core <= ArchSpec::kCore_x86_32_last) || (lhs_core == ArchSpec::kCore_x86_32_any))
            return true;
    }
    else if (lhs_core == ArchSpec::kCore_ppc_any)
    {
        if ((rhs_core >= ArchSpec::kCore_ppc_first && rhs_core <= ArchSpec::kCore_ppc_last) || (rhs_core == ArchSpec::kCore_ppc_any))
            return true;
    }
    else if (rhs_core == ArchSpec::kCore_ppc_any)
    {
        if ((lhs_core >= ArchSpec::kCore_ppc_first && lhs_core <= ArchSpec::kCore_ppc_last) || (lhs_core == ArchSpec::kCore_ppc_any))
            return true;
    }
    else if (lhs_core == ArchSpec::kCore_ppc64_any)
    {
        if ((rhs_core >= ArchSpec::kCore_ppc64_first && rhs_core <= ArchSpec::kCore_ppc64_last) || (rhs_core == ArchSpec::kCore_ppc64_any))
            return true;
    }
    else if (rhs_core == ArchSpec::kCore_ppc64_any)
    {
        if ((lhs_core >= ArchSpec::kCore_ppc64_first && lhs_core <= ArchSpec::kCore_ppc64_last) || (lhs_core == ArchSpec::kCore_ppc64_any))
            return true;
    }
    return false;
}

bool
lldb_private::operator!= (const ArchSpec& lhs, const ArchSpec& rhs)
{
    return !(lhs == rhs);
}

bool
lldb_private::operator<(const ArchSpec& lhs, const ArchSpec& rhs)
{
    const ArchSpec::Core lhs_core = lhs.GetCore ();
    const ArchSpec::Core rhs_core = rhs.GetCore ();
    return lhs_core < rhs_core;
}
