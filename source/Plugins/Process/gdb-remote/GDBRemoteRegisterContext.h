//===-- GDBRemoteRegisterContext.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_GDBRemoteRegisterContext_h_
#define lldb_GDBRemoteRegisterContext_h_

// C Includes
// C++ Includes
#include <vector>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/Core/ConstString.h"
#include "lldb/Core/DataExtractor.h"
#include "lldb/Target/RegisterContext.h"


class ThreadGDBRemote;
class ProcessGDBRemote;

class GDBRemoteDynamicRegisterInfo
{
public:
    GDBRemoteDynamicRegisterInfo () :
        m_regs (),
        m_sets (),
        m_set_reg_nums (),
        m_reg_names (),
        m_reg_alt_names (),
        m_set_names (),
        m_reg_data_byte_size (0)
    {
    }

    ~GDBRemoteDynamicRegisterInfo ()
    {
    }

    void
    AddRegister (lldb::RegisterInfo &reg_info, lldb_private::ConstString &reg_name, lldb_private::ConstString &reg_alt_name, lldb_private::ConstString &set_name)
    {
        const uint32_t reg_num = m_regs.size();
        m_reg_names.push_back (reg_name);
        m_reg_alt_names.push_back (reg_alt_name);
        reg_info.name = reg_name.AsCString();
        assert (reg_info.name);
        reg_info.alt_name = reg_alt_name.AsCString(NULL);
        m_regs.push_back (reg_info);
        uint32_t set = GetRegisterSetIndexByName (set_name, true);
        assert (set < m_sets.size());
        assert (set < m_set_reg_nums.size());
        assert (set < m_set_names.size());
        m_set_reg_nums[set].push_back(reg_num);
        size_t end_reg_offset = reg_info.byte_offset + reg_info.byte_size;
        if (m_reg_data_byte_size < end_reg_offset)
            m_reg_data_byte_size = end_reg_offset;
    }

    void
    Finalize ()
    {
        for (uint32_t set = 0; set < m_sets.size(); ++set)
        {
            assert (m_sets.size() == m_set_reg_nums.size());
            m_sets[set].num_registers = m_set_reg_nums[set].size();
            m_sets[set].registers = &m_set_reg_nums[set][0];
        }
    }

    size_t
    GetNumRegisters() const
    {
        return m_regs.size();
    }

    size_t
    GetNumRegisterSets() const
    {
        return m_sets.size();
    }

    size_t
    GetRegisterDataByteSize() const
    {
        return m_reg_data_byte_size;
    }

    const lldb::RegisterInfo *
    GetRegisterInfoAtIndex (uint32_t i) const
    {
        if (i < m_regs.size())
            return &m_regs[i];
        return NULL;
    }

    const lldb::RegisterSet *
    GetRegisterSet (uint32_t i) const
    {
        if (i < m_sets.size())
            return &m_sets[i];
        return NULL;
    }

    uint32_t
    GetRegisterSetIndexByName (lldb_private::ConstString &set_name, bool can_create)
    {
        name_collection::iterator pos, end = m_set_names.end();
        for (pos = m_set_names.begin(); pos != end; ++pos)
        {
            if (*pos == set_name)
                return std::distance (m_set_names.begin(), pos);
        }

        m_set_names.push_back(set_name);
        m_set_reg_nums.resize(m_set_reg_nums.size()+1);
        lldb::RegisterSet new_set = { set_name.AsCString(), NULL, 0, NULL };
        m_sets.push_back (new_set);
        return m_sets.size() - 1;
    }

    uint32_t
    ConvertRegisterKindToRegisterNumber (uint32_t kind, uint32_t num) const
    {
        reg_collection::const_iterator pos, end = m_regs.end();
        for (pos = m_regs.begin(); pos != end; ++pos)
        {
            if (pos->kinds[kind] == num)
                return std::distance (m_regs.begin(), pos);
        }

        return LLDB_INVALID_REGNUM;
    }
    void
    Clear()
    {
        m_regs.clear();
        m_sets.clear();
        m_set_reg_nums.clear();
        m_reg_names.clear();
        m_reg_alt_names.clear();
        m_set_names.clear();
    }

    void
    HardcodeARMRegisters();

protected:
    //------------------------------------------------------------------
    // Classes that inherit from GDBRemoteRegisterContext can see and modify these
    //------------------------------------------------------------------
    typedef std::vector <lldb::RegisterInfo> reg_collection;
    typedef std::vector <lldb::RegisterSet> set_collection;
    typedef std::vector <uint32_t> reg_num_collection;
    typedef std::vector <reg_num_collection> set_reg_num_collection;
    typedef std::vector <lldb_private::ConstString> name_collection;

    reg_collection m_regs;
    set_collection m_sets;
    set_reg_num_collection m_set_reg_nums;
    name_collection m_reg_names;
    name_collection m_reg_alt_names;
    name_collection m_set_names;
    size_t m_reg_data_byte_size;   // The number of bytes required to store all registers
};

class GDBRemoteRegisterContext : public lldb_private::RegisterContext
{
public:
    //------------------------------------------------------------------
    // Constructors and Destructors
    //------------------------------------------------------------------
    GDBRemoteRegisterContext (ThreadGDBRemote &thread,
                              lldb_private::StackFrame *frame,
                              GDBRemoteDynamicRegisterInfo &reg_info,
                              bool read_all_at_once);

    virtual
    ~GDBRemoteRegisterContext ();

    //------------------------------------------------------------------
    // Subclasses must override these functions
    //------------------------------------------------------------------
    virtual void
    Invalidate ();

    virtual size_t
    GetRegisterCount ();

    virtual const lldb::RegisterInfo *
    GetRegisterInfoAtIndex (uint32_t reg);

    virtual size_t
    GetRegisterSetCount ();

    virtual const lldb::RegisterSet *
    GetRegisterSet (uint32_t reg_set);

    virtual bool
    ReadRegisterValue (uint32_t reg, lldb_private::Scalar &value);

    virtual bool
    ReadRegisterBytes (uint32_t reg, lldb_private::DataExtractor &data);

    virtual bool
    ReadAllRegisterValues (lldb::DataBufferSP &data_sp);

    virtual bool
    WriteRegisterValue (uint32_t reg, const lldb_private::Scalar &value);

    virtual bool
    WriteRegisterBytes (uint32_t reg, lldb_private::DataExtractor &data, uint32_t data_offset);

    virtual bool
    WriteAllRegisterValues (const lldb::DataBufferSP &data_sp);

    virtual uint32_t
    ConvertRegisterKindToRegisterNumber (uint32_t kind, uint32_t num);

protected:

    void
    SetAllRegisterValid (bool b);

    ProcessGDBRemote &
    GetGDBProcess();

    ThreadGDBRemote &
    GetGDBThread();

    GDBRemoteDynamicRegisterInfo &m_reg_info;
    std::vector<bool> m_reg_valid;
    uint32_t m_reg_valid_stop_id;
    lldb_private::DataExtractor m_reg_data;
    bool m_read_all_at_once;

private:
    //------------------------------------------------------------------
    // For GDBRemoteRegisterContext only
    //------------------------------------------------------------------
    DISALLOW_COPY_AND_ASSIGN (GDBRemoteRegisterContext);
};

#endif  // lldb_GDBRemoteRegisterContext_h_
