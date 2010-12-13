//===-- Options.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Options_h_
#define liblldb_Options_h_

// C Includes
#include <getopt.h>

// C++ Includes
#include <set>
#include <vector>

// Other libraries and framework includes
// Project includes
#include "lldb/lldb-private.h"
#include "lldb/lldb-defines.h"
#include "lldb/Interpreter/Args.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Options Options.h "lldb/Interpreter/Options.h"
/// @brief A command line option parsing protocol class.
///
/// Options is designed to be subclassed to contain all needed
/// options for a given command. The options can be parsed by calling:
/// \code
///     Error Args::ParseOptions (Options &);
/// \endcode
///
/// The options are specified using the format defined for the libc
/// options parsing function getopt_long:
/// \code
///     #include <getopt.h>
///     int getopt_long(int argc, char * const *argv, const char *optstring, const struct option *longopts, int *longindex);
/// \endcode
///
/// Example code:
/// \code
///     #include <getopt.h>
///     #include <string>
///
///     class CommandOptions : public Options
///     {
///     public:
///         virtual struct option *
///         GetLongOptions() {
///             return g_options;
///         }
///
///         virtual Error
///         SetOptionValue (int option_idx, int option_val, const char *option_arg)
///         {
///             Error error;
///             switch (option_val)
///             {
///             case 'g': debug = true; break;
///             case 'v': verbose = true; break;
///             case 'l': log_file = option_arg; break;
///             case 'f': log_flags = strtoull(option_arg, NULL, 0); break;
///             default:
///                 error.SetErrorStringWithFormat("unrecognized short option %c", option_val);
///                 break;
///             }
///
///             return error;
///         }
///
///         CommandOptions () : debug (true), verbose (false), log_file (), log_flags (0)
///         {}
///
///         bool debug;
///         bool verbose;
///         std::string log_file;
///         uint32_t log_flags;
///
///         static struct option g_options[];
///
///     };
///
///     struct option CommandOptions::g_options[] =
///     {
///         { "debug",              no_argument,        NULL,   'g' },
///         { "log-file",           required_argument,  NULL,   'l' },
///         { "log-flags",          required_argument,  NULL,   'f' },
///         { "verbose",            no_argument,        NULL,   'v' },
///         { NULL,                 0,                  NULL,   0   }
///     };
///
///     int main (int argc, const char **argv, const char **envp)
///     {
///         CommandOptions options;
///         Args main_command;
///         main_command.SetArguments(argc, argv, false);
///         main_command.ParseOptions(options);
///
///         if (options.verbose)
///         {
///             std::cout << "verbose is on" << std::endl;
///         }
///     }
/// \endcode
//----------------------------------------------------------------------
class Options
{
public:

    Options ();

    virtual
    ~Options ();

    void
    BuildGetoptTable ();

    void
    BuildValidOptionSets ();

    uint32_t
    NumCommandOptions ();

    //------------------------------------------------------------------
    /// Get the option definitions to use when parsing Args options.
    ///
    /// @see Args::ParseOptions (Options&)
    /// @see man getopt_long
    //------------------------------------------------------------------
    struct option *
    GetLongOptions ();

    // This gets passed the short option as an integer...
    void
    OptionSeen (int short_option);

    bool
    VerifyOptions (CommandReturnObject &result);

    // Verify that the options given are in the options table and can be used together, but there may be
    // some required options that are missing (used to verify options that get folded into command aliases).

    bool
    VerifyPartialOptions (CommandReturnObject &result);

//    void
//    BuildAliasOptions (OptionArgVector *option_arg_vector, Args args);

    void
    OutputFormattedUsageText (Stream &strm,
                              const char *text,
                              uint32_t output_max_columns);

    void
    GenerateOptionUsage (CommandInterpreter &interpreter,
                         Stream &strm,
                         CommandObject *cmd);

    // The following two pure virtual functions must be defined by every class that inherits from
    // this class.

    virtual const lldb::OptionDefinition*
    GetDefinitions () { return NULL; }

    virtual void
    ResetOptionValues ();

    //------------------------------------------------------------------
    /// Set the value of an option.
    ///
    /// @param[in] option_idx
    ///     The index into the "struct option" array that was returned
    ///     by Options::GetLongOptions().
    ///
    /// @param[in] option_arg
    ///     The argument value for the option that the user entered, or
    ///     NULL if there is no argument for the current option.
    ///
    ///
    /// @see Args::ParseOptions (Options&)
    /// @see man getopt_long
    //------------------------------------------------------------------
    virtual Error
    SetOptionValue (int option_idx, const char *option_arg) = 0;

    //------------------------------------------------------------------
    ///  Handles the generic bits of figuring out whether we are in an option, and if so completing
    /// it.
    ///
    /// @param[in] input
    ///    The command line parsed into words
    ///
    /// @param[in] cursor_index
    ///     The index in \ainput of the word in which the cursor lies.
    ///
    /// @param[in] char_pos
    ///     The character position of the cursor in its argument word.
    ///
    /// @param[in] match_start_point
    /// @param[in] match_return_elements
    ///     See CommandObject::HandleCompletions for a description of how these work.
    ///
    /// @param[in] interpreter
    ///     The interpreter that's doing the completing.
    ///
    /// @param[out] word_complete
    ///     \btrue if this is a complete option value (a space will be inserted after the
    ///     completion.)  \bfalse otherwise.
    ///
    /// @param[out] matches
    ///     The array of matches returned.
    ///
    /// FIXME: This is the wrong return value, since we also need to make a distinction between
    /// total number of matches, and the window the user wants returned.
    ///
    /// @return
    ///     \btrue if we were in an option, \bfalse otherwise.
    //------------------------------------------------------------------
    bool
    HandleOptionCompletion (CommandInterpreter &interpreter,
                            Args &input,
                            OptionElementVector &option_map,
                            int cursor_index,
                            int char_pos,
                            int match_start_point,
                            int max_return_elements,
                            bool &word_complete,
                            lldb_private::StringList &matches);

    //------------------------------------------------------------------
    ///  Handles the generic bits of figuring out whether we are in an option, and if so completing
    /// it.
    ///
    /// @param[in] interpreter
    ///    The command interpreter doing the completion.
    ///
    /// @param[in] input
    ///    The command line parsed into words
    ///
    /// @param[in] cursor_index
    ///     The index in \ainput of the word in which the cursor lies.
    ///
    /// @param[in] char_pos
    ///     The character position of the cursor in its argument word.
    ///
    /// @param[in] opt_element_vector
    ///     The results of the options parse of \a input.
    ///
    /// @param[in] opt_element_index
    ///     The position in \a opt_element_vector of the word in \a input containing the cursor.
    ///
    /// @param[in] match_start_point
    /// @param[in] match_return_elements
    ///     See CommandObject::HandleCompletions for a description of how these work.
    ///
    /// @param[out] word_complete
    ///     \btrue if this is a complete option value (a space will be inserted after the
    ///     completion.)  \bfalse otherwise.
    ///
    /// @param[out] matches
    ///     The array of matches returned.
    ///
    /// FIXME: This is the wrong return value, since we also need to make a distinction between
    /// total number of matches, and the window the user wants returned.
    ///
    /// @return
    ///     \btrue if we were in an option, \bfalse otherwise.
    //------------------------------------------------------------------
    virtual bool
    HandleOptionArgumentCompletion (CommandInterpreter &interpreter,
                                    Args &input,
                                    int cursor_index,
                                    int char_pos,
                                    OptionElementVector &opt_element_vector,
                                    int opt_element_index,
                                    int match_start_point,
                                    int max_return_elements,
                                    bool &word_complete,
                                    StringList &matches);
    
protected:
    // This is a set of options expressed as indexes into the options table for this Option.
    typedef std::set<char> OptionSet;
    typedef std::vector<OptionSet> OptionSetVector;

    std::vector<struct option> m_getopt_table;
    OptionSet m_seen_options;
    OptionSetVector m_required_options;
    OptionSetVector m_optional_options;

    OptionSetVector &GetRequiredOptions ()
    {
        BuildValidOptionSets();
        return m_required_options;
    }
    
    OptionSetVector &GetOptionalOptions ()
    {
        BuildValidOptionSets();
        return m_optional_options;
    }

    bool
    IsASubset (const OptionSet& set_a, const OptionSet& set_b);

    size_t
    OptionsSetDiff (const OptionSet &set_a, const OptionSet &set_b, OptionSet &diffs);

    void
    OptionsSetUnion (const OptionSet &set_a, const OptionSet &set_b, OptionSet &union_set);
};

} // namespace lldb_private

#endif  // liblldb_Options_h_
