//===-- RegularExpression.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DBRegex_h_
#define liblldb_DBRegex_h_
#if defined(__cplusplus)

#include <regex.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class RegularExpression RegularExpression.h "lldb/Core/RegularExpression.h"
/// @brief A C++ wrapper class for regex.
///
/// This regular expression class wraps the posix regex functions
/// \c regcomp(), \c regerror(), \c regexec(), and \c regfree() from
/// the header file in \c /usr/include/regex\.h.
//----------------------------------------------------------------------
class RegularExpression
{
public:
    //------------------------------------------------------------------
    /// Default constructor.
    ///
    /// The default constructor that initializes the object state such
    /// that it contains no compiled regular expression.
    //------------------------------------------------------------------
    RegularExpression ();

    //------------------------------------------------------------------
    /// Constructor that takes a regulare expression with flags.
    ///
    /// Constructor that compiles \a re using \a flags and stores the
    /// resulting compiled regular expression into this object.
    ///
    /// @param[in] re
    ///     A c string that represents the regular expression to
    ///     compile.
    ///
    /// @param[in] flags
    ///     Flags that are passed the the \c regcomp() function.
    //------------------------------------------------------------------
    RegularExpression (const char* re, int flags = REG_EXTENDED);

    //------------------------------------------------------------------
    /// Destructor.
    ///
    /// Any previosuly compiled regular expression contained in this
    /// object will be freed.
    //------------------------------------------------------------------
    ~RegularExpression ();

    //------------------------------------------------------------------
    /// Compile a regular expression.
    ///
    /// Compile a regular expression using the supplied regular
    /// expression text and flags. The compied regular expression lives
    /// in this object so that it can be readily used for regular
    /// expression matches. Execute() can be called after the regular
    /// expression is compiled. Any previosuly compiled regular
    /// expression contained in this object will be freed.
    ///
    /// @param[in] re
    ///     A NULL terminated C string that represents the regular
    ///     expression to compile.
    ///
    /// @param[in] flags
    ///     Flags that are passed the the \c regcomp() function.
    ///
    /// @return
    ///     \b true if the regular expression compiles successfully,
    ///     \b false otherwise.
    //------------------------------------------------------------------
    bool
    Compile (const char* re, int flags = REG_EXTENDED);

    //------------------------------------------------------------------
    /// Executes a regular expression.
    ///
    /// Execute a regular expression match using the compiled regular
    /// expression that is already in this object against the match
    /// string \a s. If any parens are used for regular expression
    /// matches \a match_count should indicate the number of regmatch_t
    /// values that are present in \a match_ptr. The regular expression
    /// will be executed using the \a execute_flags
    ///
    /// @param[in] string
    ///     The string to match against the compile regular expression.
    ///
    /// @param[in] match_count
    ///     The number of regmatch_t objects in \a match_ptr
    ///
    /// @param[in] execute_flags
    ///     Flags to pass to the \c regexec() function.
    ///
    /// @return
    ///     \b true if \a string matches the compiled regular
    ///     expression, \b false otherwise.
    //------------------------------------------------------------------
    bool
    Execute (const char* string, size_t match_count = 0, int execute_flags = 0) const;

    size_t
    GetErrorAsCString (char *err_str, size_t err_str_max_len) const;

    bool
    GetMatchAtIndex (const char* s, uint32_t idx, std::string& match_str) const;
    //------------------------------------------------------------------
    /// Free the compiled regular expression.
    ///
    /// If this object contains a valid compiled regular expression,
    /// this function will free any resources it was consuming.
    //------------------------------------------------------------------
    void
    Free ();

    //------------------------------------------------------------------
    /// Access the regular expression text.
    ///
    /// Returns the text that was used to compile the current regular
    /// expression.
    ///
    /// @return
    ///     The NULL terminated C string that was used to compile the
    ///     current regular expression
    //------------------------------------------------------------------
    const char*
    GetText () const;

    //------------------------------------------------------------------
    /// Test if valid.
    ///
    /// Test if this object contains a valid regular expression.
    ///
    /// @return
    ///     \b true if the regular expression compiled and is ready
    ///     for execution, \b false otherwise.
    //------------------------------------------------------------------
    bool
    IsValid () const;

private:
    //------------------------------------------------------------------
    // Member variables
    //------------------------------------------------------------------
    std::string m_re;   ///< A copy of the original regular expression text
    int m_comp_err;     ///< Error code for the regular expression compilation
    regex_t m_preg;     ///< The compiled regular expression
    mutable std::vector<regmatch_t> m_matches; ///< Where parenthesized subexpressions results are stored
};

} // namespace lldb_private

#endif  // #if defined(__cplusplus)
#endif  // liblldb_DBRegex_h_
