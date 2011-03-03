"""
Test retrieval of SBAddress from function/symbol, disassembly, and SBAddress APIs.
"""

import os, time
import re
import unittest2
import lldb, lldbutil
from lldbtest import *

class DisasmAPITestCase(TestBase):

    mydir = os.path.join("python_api", "function_symbol")

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    @python_api_test
    def test_with_dsym(self):
        """Exercise getting SBAddress objects, disassembly, and SBAddress APIs."""
        self.buildDsym()
        self.disasm_and_address_api()

    @python_api_test
    def test_with_dwarf(self):
        """Exercise getting SBAddress objects, disassembly, and SBAddress APIs."""
        self.buildDwarf()
        self.disasm_and_address_api()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to of function 'c'.
        self.line1 = line_number('main.c', '// Find the line number for breakpoint 1 here.')
        self.line2 = line_number('main.c', '// Find the line number for breakpoint 2 here.')

    def disasm_and_address_api(self):
        """Exercise getting SBAddress objects, disassembly, and SBAddress APIs."""
        exe = os.path.join(os.getcwd(), "a.out")

        # Create a target by the debugger.
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target.IsValid(), VALID_TARGET)

        # Now create the two breakpoints inside function 'a'.
        breakpoint1 = target.BreakpointCreateByLocation('main.c', self.line1)
        breakpoint2 = target.BreakpointCreateByLocation('main.c', self.line2)
        #print "breakpoint1:", breakpoint1
        #print "breakpoint2:", breakpoint2
        self.assertTrue(breakpoint1.IsValid() and
                        breakpoint1.GetNumLocations() == 1,
                        VALID_BREAKPOINT)
        self.assertTrue(breakpoint2.IsValid() and
                        breakpoint2.GetNumLocations() == 1,
                        VALID_BREAKPOINT)

        # Now launch the process, and do not stop at entry point.
        error = lldb.SBError()
        self.process = target.Launch (self.dbg.GetListener(), None, None, os.ctermid(), os.ctermid(), os.ctermid(), None, 0, False, error)

        self.process = target.GetProcess()
        self.assertTrue(self.process.IsValid(), PROCESS_IS_VALID)

        # Frame #0 should be on self.line1.
        self.assertTrue(self.process.GetState() == lldb.eStateStopped)
        thread = lldbutil.get_stopped_thread(self.process, lldb.eStopReasonBreakpoint)
        self.assertTrue(thread != None, "There should be a thread stopped due to breakpoint condition")
        frame0 = thread.GetFrameAtIndex(0)
        lineEntry = frame0.GetLineEntry()
        self.assertTrue(lineEntry.GetLine() == self.line1)

        address1 = lineEntry.GetStartAddress()
        #print "address1:", address1

        # Now call SBTarget.ResolveSymbolContextForAddress() with address1.
        context1 = target.ResolveSymbolContextForAddress(address1, lldb.eSymbolContextEverything)

        self.assertTrue(context1.IsValid())
        print "context1:", context1

        # Continue the inferior, the breakpoint 2 should be hit.
        self.process.Continue()
        self.assertTrue(self.process.GetState() == lldb.eStateStopped)
        thread = lldbutil.get_stopped_thread(self.process, lldb.eStopReasonBreakpoint)
        self.assertTrue(thread != None, "There should be a thread stopped due to breakpoint condition")
        frame0 = thread.GetFrameAtIndex(0)
        lineEntry = frame0.GetLineEntry()
        self.assertTrue(lineEntry.GetLine() == self.line2)

        # Verify that the symbol and the function has the same address range per function 'a'.
        symbol = context1.GetSymbol()
        function = frame0.GetFunction()
        self.assertTrue(symbol.IsValid() and function.IsValid())

        print "symbol:", symbol
        print "disassembly=>\n", lldbutil.disassemble(target, symbol)

        print "function:", function
        print "disassembly=>\n", lldbutil.disassemble(target, function)

        sa1 = symbol.GetStartAddress()
        #print "sa1:", sa1
        #ea1 = symbol.GetEndAddress()
        #print "ea1:", ea1
        sa2 = function.GetStartAddress()
        #print "sa2:", sa2
        #ea2 = function.GetEndAddress()
        #print "ea2:", ea2

        stream1 = lldb.SBStream()
        sa1.GetDescription(stream1)
        stream2 = lldb.SBStream()
        sa2.GetDescription(stream2)

        self.expect(stream1.GetData(), "The two starting addresses should be the same", exe=False,
            startstr = stream2.GetData())

        
if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
