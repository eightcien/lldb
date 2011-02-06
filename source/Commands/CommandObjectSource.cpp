//===-- CommandObjectSource.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CommandObjectSource.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Interpreter/Args.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Core/FileSpec.h"
#include "lldb/Target/Process.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/Options.h"

using namespace lldb;
using namespace lldb_private;

//-------------------------------------------------------------------------
// CommandObjectSourceList
//-------------------------------------------------------------------------

class CommandObjectSourceInfo : public CommandObject
{

    class CommandOptions : public Options
    {
    public:
        CommandOptions () :
            Options()
        {
        }

        ~CommandOptions ()
        {
        }

        Error
        SetOptionValue (int option_idx, const char *option_arg)
        {
            Error error;
            const char short_option = g_option_table[option_idx].short_option;
            switch (short_option)
            {
            case 'l':
                start_line = Args::StringToUInt32 (option_arg, 0);
                if (start_line == 0)
                    error.SetErrorStringWithFormat("Invalid line number: '%s'.\n", option_arg);
                break;

             case 'f':
                file_name = option_arg;
                break;

           default:
                error.SetErrorStringWithFormat("Unrecognized short option '%c'.\n", short_option);
                break;
            }

            return error;
        }

        void
        ResetOptionValues ()
        {
            Options::ResetOptionValues();

            file_spec.Clear();
            file_name.clear();
            start_line = 0;
        }

        const lldb::OptionDefinition*
        GetDefinitions ()
        {
            return g_option_table;
        }
        static lldb::OptionDefinition g_option_table[];

        // Instance variables to hold the values for command options.
        FileSpec file_spec;
        std::string file_name;
        uint32_t start_line;
        
    };
 
public:   
    CommandObjectSourceInfo(CommandInterpreter &interpreter) :
        CommandObject (interpreter,
                       "source info",
                       "Display information about the source lines from the current executable's debug info.",
                       "source info [<cmd-options>]")
    {
    }

    ~CommandObjectSourceInfo ()
    {
    }


    Options *
    GetOptions ()
    {
        return &m_options;
    }


    bool
    Execute
    (
        Args& args,
        CommandReturnObject &result
    )
    {
        result.AppendError ("Not yet implemented");
        result.SetStatus (eReturnStatusFailed);
        return false;
    }
protected:
    CommandOptions m_options;
};

lldb::OptionDefinition
CommandObjectSourceInfo::CommandOptions::g_option_table[] =
{
{ LLDB_OPT_SET_1, false, "line",       'l', required_argument, NULL, 0, eArgTypeLineNum,    "The line number at which to start the display source."},
{ LLDB_OPT_SET_1, false, "file",       'f', required_argument, NULL, CommandCompletions::eSourceFileCompletion, eArgTypeFilename,    "The file from which to display source."},
{ 0, false, NULL, 0, 0, NULL, 0, eArgTypeNone, NULL }
};

#pragma mark CommandObjectSourceList
//-------------------------------------------------------------------------
// CommandObjectSourceList
//-------------------------------------------------------------------------

class CommandObjectSourceList : public CommandObject
{

    class CommandOptions : public Options
    {
    public:
        CommandOptions () :
            Options()
        {
        }

        ~CommandOptions ()
        {
        }

        Error
        SetOptionValue (int option_idx, const char *option_arg)
        {
            Error error;
            const char short_option = g_option_table[option_idx].short_option;
            switch (short_option)
            {
            case 'l':
                start_line = Args::StringToUInt32 (option_arg, 0);
                if (start_line == 0)
                    error.SetErrorStringWithFormat("Invalid line number: '%s'.\n", option_arg);
                break;

            case 'c':
                num_lines = Args::StringToUInt32 (option_arg, 0);
                if (num_lines == 0)
                    error.SetErrorStringWithFormat("Invalid line count: '%s'.\n", option_arg);
                break;

             case 'f':
                file_name = option_arg;
                break;
                
            case 'n':
                symbol_name = option_arg;
                break;

            case 's':
                m_modules.push_back (std::string (option_arg));
                break;
           default:
                error.SetErrorStringWithFormat("Unrecognized short option '%c'.\n", short_option);
                break;
            }

            return error;
        }

        void
        ResetOptionValues ()
        {
            Options::ResetOptionValues();

            file_spec.Clear();
            file_name.clear();
            symbol_name.clear();
            start_line = 0;
            num_lines = 10;
            m_modules.clear();
        }

        const lldb::OptionDefinition*
        GetDefinitions ()
        {
            return g_option_table;
        }
        static lldb::OptionDefinition g_option_table[];

        // Instance variables to hold the values for command options.
        FileSpec file_spec;
        std::string file_name;
        std::string symbol_name;
        uint32_t start_line;
        uint32_t num_lines;
        STLStringArray m_modules;        
    };
 
public:   
    CommandObjectSourceList(CommandInterpreter &interpreter) :
        CommandObject (interpreter,
                       "source list",
                       "Display source code (as specified) based on the current executable's debug info.",
                        NULL)
    {
        CommandArgumentEntry arg;
        CommandArgumentData file_arg;
        
        // Define the first (and only) variant of this arg.
        file_arg.arg_type = eArgTypeFilename;
        file_arg.arg_repetition = eArgRepeatOptional;
        
        // There is only one variant this argument could be; put it into the argument entry.
        arg.push_back (file_arg);
        
        // Push the data for the first argument into the m_arguments vector.
        m_arguments.push_back (arg);
    }

    ~CommandObjectSourceList ()
    {
    }


    Options *
    GetOptions ()
    {
        return &m_options;
    }


    bool
    Execute
    (
        Args& args,
        CommandReturnObject &result
    )
    {
        const int argc = args.GetArgumentCount();

        if (argc != 0)
        {
            result.AppendErrorWithFormat("'%s' takes no arguments, only flags.\n", GetCommandName());
            result.SetStatus (eReturnStatusFailed);
        }

        ExecutionContext exe_ctx(m_interpreter.GetDebugger().GetExecutionContext());
        
        if (!m_options.symbol_name.empty())
        {
            // Displaying the source for a symbol:
            Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
            if (target == NULL)
            {
                result.AppendError ("invalid target, set executable file using 'file' command");
                result.SetStatus (eReturnStatusFailed);
                return false;
            }
            
            SymbolContextList sc_list;
            ConstString name(m_options.symbol_name.c_str());
            bool include_symbols = false;
            bool append = true;
            size_t num_matches = 0;
            
            if (m_options.m_modules.size() > 0)
            {
                ModuleList matching_modules;
                for (unsigned i = 0, e = m_options.m_modules.size(); i != e; i++)
                {
                    FileSpec module_spec(m_options.m_modules[i].c_str(), false);
                    if (module_spec)
                    {
                        matching_modules.Clear();
                        target->GetImages().FindModules (&module_spec, NULL, NULL, NULL, matching_modules);
                        num_matches += matching_modules.FindFunctions (name, eFunctionNameTypeBase, include_symbols, append, sc_list);
                    }
                }
            }
            else
            {
                num_matches = target->GetImages().FindFunctions (name, eFunctionNameTypeBase, include_symbols, append, sc_list);
            }
            
            SymbolContext sc;

            if (num_matches == 0)
            {
                result.AppendErrorWithFormat("Could not find function named: \"%s\".\n", m_options.symbol_name.c_str());
                result.SetStatus (eReturnStatusFailed);
                return false;
            }
            
            sc_list.GetContextAtIndex (0, sc);
            FileSpec start_file;
            uint32_t start_line;
            uint32_t end_line;
            FileSpec end_file;
            if (sc.function != NULL)
            {
                sc.function->GetStartLineSourceInfo (start_file, start_line);
                if (start_line == 0)
                {
                    result.AppendErrorWithFormat("Could not find line information for start of function: \"%s\".\n", m_options.symbol_name.c_str());
                    result.SetStatus (eReturnStatusFailed);
                    return false;
                }
                sc.function->GetEndLineSourceInfo (end_file, end_line);
            }
            else
            {
                result.AppendErrorWithFormat("Could not find function info for: \"%s\".\n", m_options.symbol_name.c_str());
                result.SetStatus (eReturnStatusFailed);
                return false;
            }

            if (num_matches > 1)
            {
                // This could either be because there are multiple functions of this name, in which case
                // we'll have to specify this further...  Or it could be because there are multiple inlined instances
                // of one function.  So run through the matches and if they all have the same file & line then we can just
                // list one.
                
                bool found_multiple = false;
                
                for (size_t i = 1; i < num_matches; i++)
                {
                    SymbolContext scratch_sc;
                    sc_list.GetContextAtIndex (i, scratch_sc);
                    if (scratch_sc.function != NULL)
                    {
                        FileSpec scratch_file;
                        uint32_t scratch_line;
                        scratch_sc.function->GetStartLineSourceInfo (scratch_file, scratch_line);
                        if (scratch_file != start_file 
                            || scratch_line != start_line)
                        {
                            found_multiple = true;
                            break;
                        }
                    }
                }
                if (found_multiple)
                {
                    StreamString s;
                    for (size_t i = 0; i < num_matches; i++)
                    {
                        SymbolContext scratch_sc;
                        sc_list.GetContextAtIndex (i, scratch_sc);
                        if (scratch_sc.function != NULL)
                        {
                            s.Printf("\n%d: ", i); 
                            scratch_sc.function->Dump (&s, true);
                        }
                    }
                    result.AppendErrorWithFormat("Multiple functions found matching: %s: \n%s\n", 
                                                 m_options.symbol_name.c_str(),
                                                 s.GetData());
                    result.SetStatus (eReturnStatusFailed);
                    return false;
                }
            }
            
                
            // This is a little hacky, but the first line table entry for a function points to the "{" that 
            // starts the function block.  It would be nice to actually get the function
            // declaration in there too.  So back up a bit, but not further than what you're going to display.
            size_t lines_to_back_up = m_options.num_lines >= 10 ? 5 : m_options.num_lines/2;
            uint32_t line_no;
            if (start_line <= lines_to_back_up)
                line_no = 1;
            else
                line_no = start_line - lines_to_back_up;
                
            // For fun, if the function is shorter than the number of lines we're supposed to display, 
            // only display the function...
            if (end_line != 0)
            {
                if (m_options.num_lines > end_line - line_no)
                    m_options.num_lines = end_line - line_no;
            }
            
            char path_buf[PATH_MAX+1];
            start_file.GetPath(path_buf, PATH_MAX);
            result.AppendMessageWithFormat("File: %s.\n", path_buf);
            m_interpreter.GetDebugger().GetSourceManager().DisplaySourceLinesWithLineNumbers (start_file,
                                                                                              line_no,
                                                                                              0,
                                                                                              m_options.num_lines,
                                                                                              "",
                                                                                              &result.GetOutputStream());
            
            result.SetStatus (eReturnStatusSuccessFinishResult);
            return true;

        }
        else if (m_options.file_name.empty())
        {
            // Last valid source manager context, or the current frame if no
            // valid last context in source manager.
            // One little trick here, if you type the exact same list command twice in a row, it is
            // more likely because you typed it once, then typed it again
            if (m_options.start_line == 0)
            {
                if (m_interpreter.GetDebugger().GetSourceManager().DisplayMoreWithLineNumbers (&result.GetOutputStream()))
                {
                    result.SetStatus (eReturnStatusSuccessFinishResult);
                }
            }
            else
            {
                if (m_interpreter.GetDebugger().GetSourceManager().DisplaySourceLinesWithLineNumbersUsingLastFile(
                            m_options.start_line,   // Line to display
                            0,                      // Lines before line to display
                            m_options.num_lines,    // Lines after line to display
                            "",                     // Don't mark "line"
                            &result.GetOutputStream()))
                {
                    result.SetStatus (eReturnStatusSuccessFinishResult);
                }

            }
        }
        else
        {
            const char *filename = m_options.file_name.c_str();
            Target *target = m_interpreter.GetDebugger().GetSelectedTarget().get();
            if (target == NULL)
            {
                result.AppendError ("invalid target, set executable file using 'file' command");
                result.SetStatus (eReturnStatusFailed);
                return false;
            }


            bool check_inlines = false;
            SymbolContextList sc_list;
            size_t num_matches = 0;
            
            if (m_options.m_modules.size() > 0)
            {
                ModuleList matching_modules;
                for (unsigned i = 0, e = m_options.m_modules.size(); i != e; i++)
                {
                    FileSpec module_spec(m_options.m_modules[i].c_str(), false);
                    if (module_spec)
                    {
                        matching_modules.Clear();
                        target->GetImages().FindModules (&module_spec, NULL, NULL, NULL, matching_modules);
                        num_matches += matching_modules.ResolveSymbolContextForFilePath (filename,
                                                                                   0,
                                                                                   check_inlines,
                                                                                   eSymbolContextModule | eSymbolContextCompUnit,
                                                                                   sc_list);
                    }
                }
            }
            else
            {
                num_matches = target->GetImages().ResolveSymbolContextForFilePath (filename,
                                                                                   0,
                                                                                   check_inlines,
                                                                                   eSymbolContextModule | eSymbolContextCompUnit,
                                                                                   sc_list);
            }
            
            if (num_matches == 0)
            {
                result.AppendErrorWithFormat("Could not find source file \"%s\".\n", 
                                             m_options.file_name.c_str());
                result.SetStatus (eReturnStatusFailed);
                return false;
            }
            
            if (num_matches > 1)
            {
                SymbolContext sc;
                bool got_multiple = false;
                FileSpec *test_cu_spec = NULL;

                for (unsigned i = 0; i < num_matches; i++)
                {
                    sc_list.GetContextAtIndex(i, sc);
                    if (sc.comp_unit)
                    {
                        if (test_cu_spec)
                        {
                            if (test_cu_spec != static_cast<FileSpec *> (sc.comp_unit))
                                got_multiple = true;
                                break;
                        }
                        else
                            test_cu_spec = sc.comp_unit;
                    }
                }
                if (got_multiple)
                {
                    result.AppendErrorWithFormat("Multiple source files found matching: \"%s.\"\n", 
                                                 m_options.file_name.c_str());
                    result.SetStatus (eReturnStatusFailed);
                    return false;
                }
            }
            
            SymbolContext sc;
            if (sc_list.GetContextAtIndex(0, sc))
            {
                if (sc.comp_unit)
                {
                    m_interpreter.GetDebugger().GetSourceManager().DisplaySourceLinesWithLineNumbers (sc.comp_unit,
                                                                                                      m_options.start_line,
                                                                                                      0,
                                                                                                      m_options.num_lines,
                                                                                                      "",
                                                                                                      &result.GetOutputStream());
                    
                    result.SetStatus (eReturnStatusSuccessFinishResult);
                }
                else
                {
                    result.AppendErrorWithFormat("No comp unit found for: \"%s.\"\n", 
                                                 m_options.file_name.c_str());
                    result.SetStatus (eReturnStatusFailed);
                    return false;
                }
            }
        }
        return result.Succeeded();
    }
    
    virtual const char *GetRepeatCommand (Args &current_command_args, uint32_t index)
    {
        return m_cmd_name.c_str();
    }

protected:
    CommandOptions m_options;

};

lldb::OptionDefinition
CommandObjectSourceList::CommandOptions::g_option_table[] =
{
{ LLDB_OPT_SET_ALL, false, "count",    'c', required_argument, NULL, 0, eArgTypeCount,   "The number of source lines to display."},
{ LLDB_OPT_SET_ALL, false, "shlib",    's', required_argument, NULL, CommandCompletions::eModuleCompletion, eArgTypeShlibName, "Look up the source file in the given shared library."},
{ LLDB_OPT_SET_1, false, "file",       'f', required_argument, NULL, CommandCompletions::eSourceFileCompletion, eArgTypeFilename,    "The file from which to display source."},
{ LLDB_OPT_SET_1, false, "line",       'l', required_argument, NULL, 0, eArgTypeLineNum,    "The line number at which to start the display source."},
{ LLDB_OPT_SET_2, false, "name",       'n', required_argument, NULL, CommandCompletions::eSymbolCompletion, eArgTypeSymbol,    "The name of a function whose source to display."},
{ 0, false, NULL, 0, 0, NULL, 0, eArgTypeNone, NULL }
};

#pragma mark CommandObjectMultiwordSource

//-------------------------------------------------------------------------
// CommandObjectMultiwordSource
//-------------------------------------------------------------------------

CommandObjectMultiwordSource::CommandObjectMultiwordSource (CommandInterpreter &interpreter) :
    CommandObjectMultiword (interpreter,
                            "source",
                            "A set of commands for accessing source file information",
                            "source <subcommand> [<subcommand-options>]")
{
    LoadSubCommand ("info",   CommandObjectSP (new CommandObjectSourceInfo (interpreter)));
    LoadSubCommand ("list",   CommandObjectSP (new CommandObjectSourceList (interpreter)));
}

CommandObjectMultiwordSource::~CommandObjectMultiwordSource ()
{
}

