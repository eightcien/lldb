"""
Test that variables with unsigned types display correctly.
"""

import os, time
import re
import unittest2
import lldb
from lldbtest import *

class UnsignedTypesTestCase(TestBase):

    mydir = "unsigned_types"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym(self):
        """Test that variables with unsigned types display correctly."""
        self.buildDsym()
        self.unsigned_types()

    def test_with_dwarf(self):
        """Test that variables with unsigned types display correctly."""
        self.buildDwarf()
        self.unsigned_types()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to break inside main().
        self.line = line_number('main.cpp', '// Set break point at this line.')

    def unsigned_types(self):
        """Test that variables with unsigned types display correctly."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break on line 19 in main() aftre the variables are assigned values.
        self.expect("breakpoint set -f main.cpp -l %d" % self.line,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: file ='main.cpp', line = %d, locations = 1" %
                        self.line)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped', 'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

        # Test that unsigned types display correctly.
        self.expect("frame variable -t -a", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(unsigned char) the_unsigned_char = 'c'",
            patterns = ["\((short unsigned int|unsigned short)\) the_unsigned_short = 99"],
            substrs = ["(unsigned int) the_unsigned_int = 99",
                       "(long unsigned int) the_unsigned_long = 99",
                       "(long long unsigned int) the_unsigned_long_long = 99",
                       "(uint32_t) the_uint32 = 99"])


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
