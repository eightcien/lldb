"""
Test thread stepping features in combination with frame select.
"""

import os, time
import re
import unittest2
import lldb, lldbutil
from lldbtest import *

class ThreadSteppingTestCase(TestBase):

    mydir = "stepping"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_step_out_with_dsym_and_run_command(self):
        """Exercise thread step-out and frame select followed by thread step-out."""
        self.buildDwarf()
        self.thread_step_out()

    def test_step_out_with_dwarf_and_run_command(self):
        """Exercise thread step-out and frame select followed by thread step-out."""
        self.buildDwarf()
        self.thread_step_out()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to of function 'c'.
        self.line1 = line_number('main.c', '// Find the line number of function "c" here.')
        self.line2 = line_number('main.c', '// frame select 2, thread step-out while stopped at "c(1)"')
        self.line3 = line_number('main.c', '// thread step-out while stopped at "c(2)"')
        self.line4 = line_number('main.c', '// frame select 1, thread step-out while stopped at "c(3)"')

    def thread_step_out(self):
        """Exercise thread step-out and frame select followed by thread step-out."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Create a breakpoint inside function 'c'.
        self.expect("breakpoint set -f main.c -l %d" % self.line1, BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: file ='main.c', line = %d, locations = 1" %
                        self.line1)

        # Now run the program.
        self.runCmd("run", RUN_SUCCEEDED)

        # The process should be stopped at this point.
        self.expect("process status", PROCESS_STOPPED,
            patterns = ['Process .* stopped'])

        # The frame #0 should correspond to main.c:32, the executable statement
        # in function name 'c'.  And frame #3 should point to main.c:37.
        self.expect("thread backtrace", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ["stop reason = breakpoint"],
            patterns = ["frame #0.*main.c:%d" % self.line1,
                        "frame #3.*main.c:%d" % self.line2])

        # We want to move the pc to frame #3.  This can be accomplished by
        # 'frame select 2', followed by 'thread step-out'.
        self.runCmd("frame select 2")
        self.runCmd("thread step-out")
        self.expect("thread backtrace", STEP_OUT_SUCCEEDED,
            substrs = ["stop reason = step out"],
            patterns = ["frame #0.*main.c:%d" % self.line2])

        # Let's move on to a single step-out case.
        self.runCmd("process continue")

        # The process should be stopped at this point.
        self.expect("process status", PROCESS_STOPPED,
            patterns = ['Process .* stopped'])
        self.runCmd("thread step-out")
        self.expect("thread backtrace", STEP_OUT_SUCCEEDED,
            substrs = ["stop reason = step out"],
            patterns = ["frame #0.*main.c:%d" % self.line3])

        # Do another frame selct, followed by thread step-out.
        self.runCmd("process continue")

        # The process should be stopped at this point.
        self.expect("process status", PROCESS_STOPPED,
            patterns = ['Process .* stopped'])
        self.runCmd("frame select 1")
        self.runCmd("thread step-out")
        self.expect("thread backtrace", STEP_OUT_SUCCEEDED,
            substrs = ["stop reason = step out"],
            patterns = ["frame #0.*main.c:%d" % self.line4])
        
        
if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
