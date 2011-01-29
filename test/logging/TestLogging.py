"""
Test lldb logging.
"""

import os, time
import unittest2
import lldb
from lldbtest import *

class LogTestCase(TestBase):

    mydir = "logging"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym (self):
        self.buildDsym ()
        self.command_log_tests ("dsym")

    def test_with_dwarf (self):
        self.buildDwarf ()
        self.command_log_tests ("dwarf")

    def command_log_tests (self, type):
        exe = os.path.join (os.getcwd(), "a.out")
        self.expect("file " + exe,
                    patterns = [ "Current executable set to .*a.out" ])

        log_file = os.path.join (os.getcwd(), "lldb-commands-log-%s.txt" % type)

        if (os.path.exists (log_file)):
            os.remove (log_file)

        self.runCmd ("log enable lldb commands -f " + log_file)
        
        self.runCmd ("commands alias bp breakpoint")
                     
        self.runCmd ("bp set -n main")

        self.runCmd ("bp l")

        expected_log_lines = [
            "com.apple.main-thread Processing command: commands alias bp breakpoint\n",
            "com.apple.main-thread HandleCommand, cmd_obj : 'commands alias'\n",
            "com.apple.main-thread HandleCommand, revised_command_line: 'commands alias bp breakpoint'\n",
            "com.apple.main-thread HandleCommand, wants_raw_input:'True'\n",
            "com.apple.main-thread HandleCommand, command line after removing command name(s): 'bp breakpoint'\n",
            "\n",
            "com.apple.main-thread Processing command: bp set -n main\n",
            "com.apple.main-thread HandleCommand, cmd_obj : 'breakpoint set'\n",
            "com.apple.main-thread HandleCommand, revised_command_line: 'breakpoint set -n main'\n",
            "com.apple.main-thread HandleCommand, wants_raw_input:'False'\n",
            "com.apple.main-thread HandleCommand, command line after removing command name(s): '-n main'\n",
            "\n",
            "com.apple.main-thread Processing command: bp l\n",
            "com.apple.main-thread HandleCommand, cmd_obj : 'breakpoint list'\n",
            "com.apple.main-thread HandleCommand, revised_command_line: 'breakpoint l'\n",
            "com.apple.main-thread HandleCommand, wants_raw_input:'False'\n",
            "com.apple.main-thread HandleCommand, command line after removing command name(s): ''\n",
            "\n"
            ]

        self.assertTrue (os.path.isfile (log_file))

        idx = 0
        end = len (expected_log_lines)
        f = open (log_file)
        log_lines = f.readlines()
        f.close ()
        self.runCmd("log disable lldb")
        os.remove (log_file)

        err_msg = ""
        success = True

        if len (log_lines) != len (expected_log_lines):
            success = False
            err_msg = "Wrong number of lines in log file; expected: " + repr (len (expected_log_lines)) + " found: " + repr(len (log_lines))
        else:
            for line1, line2 in zip (log_lines, expected_log_lines):
                if line1 != line2:
                    success = False
                    err_msg = "Expected '" + line2 + "'; Found '" + line1 + "'"
                    break

        if not success:
            self.fail (err_msg)


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()

