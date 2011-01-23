"""Show bitfields and check that they display correctly."""

import os, time
import unittest2
import lldb
from lldbtest import *

class BitfieldsTestCase(TestBase):

    mydir = "bitfields"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym_and_run_command(self):
        """Test 'frame variable ...' on a variable with bitfields."""
        self.buildDsym()
        self.bitfields_variable()

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    @python_api_test
    def test_with_dsym_and_python_api(self):
        """Use Python APIs to inspect a bitfields variable."""
        self.buildDsym()
        self.bitfields_variable_python()

    def test_with_dwarf_and_run_command(self):
        """Test 'frame variable ...' on a variable with bitfields."""
        self.buildDwarf()
        self.bitfields_variable()

    @python_api_test
    def test_with_dwarf_and_python_api(self):
        """Use Python APIs to inspect a bitfields variable."""
        self.buildDwarf()
        self.bitfields_variable_python()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to break inside main().
        self.line = line_number('main.c', '// Set break point at this line.')

    def bitfields_variable(self):
        """Test 'frame variable ...' on a variable with bitfields."""
        exe = os.path.join(os.getcwd(), "a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break inside the main.
        self.expect("breakpoint set -f main.c -l %d" % self.line,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: file ='main.c', line = %d, locations = 1" %
                        self.line)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped',
                       'stop reason = breakpoint'])

        # The breakpoint should have a hit count of 1.
        self.expect("breakpoint list", BREAKPOINT_HIT_ONCE,
            substrs = [' resolved, hit count = 1'])

        # This should display correctly.
        self.expect("frame variable -t bits", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ['(uint32_t:1) b1 = 1',
                       '(uint32_t:2) b2 = 3',
                       '(uint32_t:3) b3 = 7',
                       '(uint32_t:4) b4 = 15',
                       '(uint32_t:5) b5 = 31',
                       '(uint32_t:6) b6 = 63',
                       '(uint32_t:7) b7 = 127',
                       '(uint32_t:4) four = 15'])

        # And so should this.
        # rdar://problem/8348251
        self.expect("frame variable -t", VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ['(uint32_t:1) b1 = 1',
                       '(uint32_t:2) b2 = 3',
                       '(uint32_t:3) b3 = 7',
                       '(uint32_t:4) b4 = 15',
                       '(uint32_t:5) b5 = 31',
                       '(uint32_t:6) b6 = 63',
                       '(uint32_t:7) b7 = 127',
                       '(uint32_t:4) four = 15'])

    def bitfields_variable_python(self):
        """Use Python APIs to inspect a bitfields variable."""
        exe = os.path.join(os.getcwd(), "a.out")

        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target.IsValid(), VALID_TARGET)

        breakpoint = target.BreakpointCreateByLocation("main.c", self.line)
        self.assertTrue(breakpoint.IsValid(), VALID_BREAKPOINT)

        error = lldb.SBError()
        self.process = target.Launch (None, None, os.ctermid(), os.ctermid(), os.ctermid(), None, 0, False, error)
        self.assertTrue(self.process.IsValid(), PROCESS_IS_VALID)

        # The stop reason of the thread should be breakpoint.
        thread = target.GetProcess().GetThreadAtIndex(0)
        if thread.GetStopReason() != lldb.eStopReasonBreakpoint:
            from lldbutil import StopReasonString
            self.fail(STOPPED_DUE_TO_BREAKPOINT_WITH_STOP_REASON_AS %
                      StopReasonString(thread.GetStopReason()))

        # The breakpoint should have a hit count of 1.
        self.assertTrue(breakpoint.GetHitCount() == 1, BREAKPOINT_HIT_ONCE)

        # Lookup the "bits" variable which contains 8 bitfields.
        frame = thread.GetFrameAtIndex(0)
        bits = frame.FindVariable("bits")
        self.DebugSBValue(frame, bits)
        self.assertTrue(bits.GetTypeName() == "Bits" and
                        bits.GetNumChildren() == 8 and
                        bits.GetByteSize() == 4,
                        "(Bits)bits with byte size of 4 and 8 children")

        # Notice the pattern of int(b1.GetValue(frame), 0).  We pass a base of 0
        # so that the proper radix is determined based on the contents of the
        # string.
        b1 = bits.GetChildAtIndex(0)
        self.DebugSBValue(frame, b1)
        self.assertTrue(b1.GetName() == "b1" and
                        b1.GetTypeName() == "uint32_t:1" and
                        b1.IsInScope(frame) and
                        int(b1.GetValue(frame), 0) == 1,
                        'bits.b1 has type uint32_t:1, is in scope, and == 1')

        b7 = bits.GetChildAtIndex(6)
        self.DebugSBValue(frame, b7)
        self.assertTrue(b7.GetName() == "b7" and
                        b7.GetTypeName() == "uint32_t:7" and
                        b7.IsInScope(frame) and
                        int(b7.GetValue(frame), 0) == 127,
                        'bits.b7 has type uint32_t:7, is in scope, and == 127')

        four = bits.GetChildAtIndex(7)
        self.DebugSBValue(frame, four)
        self.assertTrue(four.GetName() == "four" and
                        four.GetTypeName() == "uint32_t:4" and
                        four.IsInScope(frame) and
                        int(four.GetValue(frame), 0) == 15,
                        'bits.four has type uint32_t:4, is in scope, and == 15')

        # Now kill the process, and we are done.
        rc = target.GetProcess().Kill()
        self.assertTrue(rc.Success())


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
