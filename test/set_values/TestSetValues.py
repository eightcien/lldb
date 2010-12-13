"""Test settings and readings of program variables."""

import os, time
import unittest2
import lldb
from lldbtest import *

class SetValuesTestCase(TestBase):

    mydir = "set_values"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym(self):
        """Test settings and readings of program variables."""
        self.buildDsym()
        self.set_values()

    def test_with_dwarf(self):
        """Test settings and readings of program variables."""
        self.buildDwarf()
        self.set_values()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line numbers to break inside main().
        self.line1 = line_number('main.c', '// Set break point #1.')
        self.line2 = line_number('main.c', '// Set break point #2.')
        self.line3 = line_number('main.c', '// Set break point #3.')
        self.line4 = line_number('main.c', '// Set break point #4.')
        self.line5 = line_number('main.c', '// Set break point #5.')

    def set_values(self):
        """Test settings and readings of program variables."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Set breakpoints on several places to set program variables.
        self.expect("breakpoint set -f main.c -l %d" % self.line1,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: file ='main.c', line = %d, locations = 1" %
                        self.line1)

        self.expect("breakpoint set -f main.c -l %d" % self.line2,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 2: file ='main.c', line = %d, locations = 1" %
                        self.line2)

        self.expect("breakpoint set -f main.c -l %d" % self.line3,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 3: file ='main.c', line = %d, locations = 1" %
                        self.line3)

        self.expect("breakpoint set -f main.c -l %d" % self.line4,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 4: file ='main.c', line = %d, locations = 1" %
                        self.line4)

        self.expect("breakpoint set -f main.c -l %d" % self.line5,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 5: file ='main.c', line = %d, locations = 1" %
                        self.line5)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

        # main.c:15
        # Check that 'frame variable -t' displays the correct data type and value.
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(char) i = 'a'")

        # Now set variable 'i' and check that it is correctly displayed.
        self.runCmd("expression i = 'b'")
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(char) i = 'b'")

        self.runCmd("continue")

        # main.c:36
        # Check that 'frame variable -t' displays the correct data type and value.
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            patterns = ["\((short unsigned int|unsigned short)\) i = 33"])

        # Now set variable 'i' and check that it is correctly displayed.
        self.runCmd("expression i = 333")
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            patterns = ["\((short unsigned int|unsigned short)\) i = 333"])

        self.runCmd("continue")

        # main.c:57
        # Check that 'frame variable -t' displays the correct data type and value.
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(long int) i = 33")

        # Now set variable 'i' and check that it is correctly displayed.
        self.runCmd("expression i = 33333")
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(long int) i = 33333")

        self.runCmd("continue")

        # main.c:78
        # Check that 'frame variable -t' displays the correct data type and value.
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(double) i = 3.14159")

        # Now set variable 'i' and check that it is correctly displayed.
        self.runCmd("expression i = 3.14")
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(double) i = 3.14")

        self.runCmd("continue")

        # main.c:85
        # Check that 'frame variable -t' displays the correct data type and value.
        # rdar://problem/8422727
        # set_values test directory: 'frame variable' shows only (long double) i =
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(long double) i = 3.14159")

        # Now set variable 'i' and check that it is correctly displayed.
        self.runCmd("expression i = 3.1")
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = "(long double) i = 3.1")


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
