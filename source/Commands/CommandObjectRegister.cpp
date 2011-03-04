//===-- CommandObjectRegister.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectRegister.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/DataExtractor.h"
#include "lldb/Core/Scalar.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/RegisterContext.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// "register read"
//----------------------------------------------------------------------
class CommandObjectRegisterRead : public CommandObject
{
public:
    CommandObjectRegisterRead (CommandInterpreter &interpreter) :
        CommandObject (interpreter, 
                       "register read",
                       "Dump the contents of one or more register values from the current frame.  If no register is specified, dumps them all.",
                       //"register read [<reg-name1> [<reg-name2> [...]]]",
                       NULL,
                       eFlagProcessMustBeLaunched | eFlagProcessMustBePaused)
    {
        CommandArgumentEntry arg;
        CommandArgumentData register_arg;
        
        // Define the first (and only) variant of this arg.
        register_arg.arg_type = eArgTypeRegisterName;
        register_arg.arg_repetition = eArgRepeatStar;
        
        // There is only one variant this argument could be; put it into the argument entry.
        arg.push_back (register_arg);
        
        // Push the data for the first argument into the m_arguments vector.
        m_arguments.push_back (arg);
    }

    virtual
    ~CommandObjectRegisterRead ()
    {
    }

    virtual bool
    Execute 
    (
        Args& command,
        CommandReturnObject &result
    )
    {
        Stream &output_stream = result.GetOutputStream();
        DataExtractor reg_data;
        ExecutionContext exe_ctx(m_interpreter.GetDebugger().GetExecutionContext());
        RegisterContext *reg_context = exe_ctx.GetRegisterContext ();

        if (reg_context)
        {
            const RegisterInfo *reg_info = NULL;
            if (command.GetArgumentCount() == 0)
            {
                uint32_t set_idx;
                const uint32_t num_register_sets = reg_context->GetRegisterSetCount();
                for (set_idx = 0; set_idx < num_register_sets; ++set_idx)
                {
                    uint32_t unavailable_count = 0;
                    const RegisterSet * const reg_set = reg_context->GetRegisterSet(set_idx);
                    output_stream.Printf ("%s:\n", reg_set->name);
                    output_stream.IndentMore ();
                    const uint32_t num_registers = reg_set->num_registers;
                    for (uint32_t reg_idx = 0; reg_idx < num_registers; ++reg_idx)
                    {
                        uint32_t reg = reg_set->registers[reg_idx];
                        reg_info = reg_context->GetRegisterInfoAtIndex(reg);
                        if (reg_context->ReadRegisterBytes(reg, reg_data))
                        {
                            output_stream.Indent ();
                            output_stream.Printf ("%-12s = ", reg_info ? reg_info->name : "<INVALID REGINFO>");
                            reg_data.Dump(&output_stream, 0, reg_info->format, reg_info->byte_size, 1, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0);
                            output_stream.EOL();
                        }
                        else
                        {
                            ++unavailable_count;
                        }
                    }
                    if (unavailable_count)
                    {
                        output_stream.Indent ();
                        output_stream.Printf("%u registers were unavailable.\n", unavailable_count);
                    }
                    output_stream.IndentLess ();
                    output_stream.EOL();
                }
            }
            else
            {
                const char *arg_cstr;
                for (int arg_idx = 0; (arg_cstr = command.GetArgumentAtIndex(arg_idx)) != NULL; ++arg_idx)
                {
                    reg_info = reg_context->GetRegisterInfoByName(arg_cstr);

                    if (reg_info)
                    {
                        output_stream.Printf("%-12s = ", reg_info->name);
                        if (reg_context->ReadRegisterBytes(reg_info->kinds[eRegisterKindLLDB], reg_data))
                        {
                            reg_data.Dump(&output_stream, 0, reg_info->format, reg_info->byte_size, 1, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0);
                        }
                        else
                        {
                            output_stream.PutCString ("error: unavailable");
                        }
                        output_stream.EOL();
                    }
                    else
                    {
                        result.AppendErrorWithFormat ("Invalid register name '%s'.\n", arg_cstr);
                    }
                }
            }
        }
        else
        {
            result.AppendError ("no current frame");
            result.SetStatus (eReturnStatusFailed);
        }
        return result.Succeeded();
    }
};


//----------------------------------------------------------------------
// "register write"
//----------------------------------------------------------------------
class CommandObjectRegisterWrite : public CommandObject
{
public:
    CommandObjectRegisterWrite (CommandInterpreter &interpreter) :
        CommandObject (interpreter,
                       "register write",
                       "Modify a single register value.",
                       //"register write <reg-name> <value>",
                       NULL,
                       eFlagProcessMustBeLaunched | eFlagProcessMustBePaused)
    {
        CommandArgumentEntry arg1;
        CommandArgumentEntry arg2;
        CommandArgumentData register_arg;
        CommandArgumentData value_arg;
        
        // Define the first (and only) variant of this arg.
        register_arg.arg_type = eArgTypeRegisterName;
        register_arg.arg_repetition = eArgRepeatPlain;
        
        // There is only one variant this argument could be; put it into the argument entry.
        arg1.push_back (register_arg);
        
        // Define the first (and only) variant of this arg.
        value_arg.arg_type = eArgTypeValue;
        value_arg.arg_repetition = eArgRepeatPlain;
        
        // There is only one variant this argument could be; put it into the argument entry.
        arg2.push_back (value_arg);
        
        // Push the data for the first argument into the m_arguments vector.
        m_arguments.push_back (arg1);
        m_arguments.push_back (arg2);
    }

    virtual
    ~CommandObjectRegisterWrite ()
    {
    }

    virtual bool
    Execute 
    (
        Args& command,
        CommandReturnObject &result
    )
    {
        DataExtractor reg_data;
        ExecutionContext exe_ctx(m_interpreter.GetDebugger().GetExecutionContext());
        RegisterContext *reg_context = exe_ctx.GetRegisterContext ();

        if (reg_context)
        {
            if (command.GetArgumentCount() != 2)
            {
                result.AppendError ("register write takes exactly 2 arguments: <reg-name> <value>");
                result.SetStatus (eReturnStatusFailed);
            }
            else
            {
                const char *reg_name = command.GetArgumentAtIndex(0);
                const char *value_str = command.GetArgumentAtIndex(1);
                const RegisterInfo *reg_info = reg_context->GetRegisterInfoByName(reg_name);

                if (reg_info)
                {
                    Scalar scalar;
                    Error error(scalar.SetValueFromCString (value_str, reg_info->encoding, reg_info->byte_size));
                    if (error.Success())
                    {
                        if (reg_context->WriteRegisterValue(reg_info->kinds[eRegisterKindLLDB], scalar))
                        {
                            result.SetStatus (eReturnStatusSuccessFinishNoResult);
                            return true;
                        }
                    }
                    else
                    {
                        result.AppendErrorWithFormat ("Failed to write register '%s' with value '%s': %s\n",
                                                     reg_name,
                                                     value_str,
                                                     error.AsCString());
                        result.SetStatus (eReturnStatusFailed);
                    }
                }
                else
                {
                    result.AppendErrorWithFormat ("Register not found for '%s'.\n", reg_name);
                    result.SetStatus (eReturnStatusFailed);
                }
            }
        }
        else
        {
            result.AppendError ("no current frame");
            result.SetStatus (eReturnStatusFailed);
        }
        return result.Succeeded();
    }
};


//----------------------------------------------------------------------
// CommandObjectRegister constructor
//----------------------------------------------------------------------
CommandObjectRegister::CommandObjectRegister(CommandInterpreter &interpreter) :
    CommandObjectMultiword (interpreter,
                            "register",
                            "A set of commands to access thread registers.",
                            "register [read|write] ...")
{
    LoadSubCommand ("read",  CommandObjectSP (new CommandObjectRegisterRead (interpreter)));
    LoadSubCommand ("write", CommandObjectSP (new CommandObjectRegisterWrite (interpreter)));
}


//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
CommandObjectRegister::~CommandObjectRegister()
{
}
