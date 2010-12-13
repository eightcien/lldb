"""
Test that breakpoint works correctly in the presence of dead-code stripping.
"""

import os, time
import unittest2
import lldb
from lldbtest import *

class DeadStripTestCase(TestBase):

    mydir = "dead-strip"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym(self):
        """Test breakpoint works correctly with dead-code stripping."""
        self.buildDsym()
        self.dead_strip()

    def test_with_dwarf(self):
        """Test breakpoint works correctly with dead-code stripping."""
        self.buildDwarf()
        self.dead_strip()

    def dead_strip(self):
        """Test breakpoint works correctly with dead-code stripping."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break by function name f1 (live code).
        self.expect("breakpoint set -s a.out -n f1", BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: name = 'f1', module = a.out, locations = 1")

        # Break by function name f2 (dead code).
        self.expect("breakpoint set -s a.out -n f2", BREAKPOINT_PENDING_CREATED,
            startstr = "Breakpoint created: 2: name = 'f2', module = a.out, locations = 0 (pending)")

        # Break by function name f3 (live code).
        self.expect("breakpoint set -s a.out -n f3", BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 3: name = 'f3', module = a.out, locations = 1")

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint (breakpoint #1).
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped',
                       'a.out`f1',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list 1", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

        self.runCmd("continue")

        # The stop reason of the thread should be breakpoint (breakpoint #3).
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped',
                       'a.out`f3',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list 3", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
