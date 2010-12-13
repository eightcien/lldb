"""
Test the printing of anonymous and named namespace variables.
"""

import os, time
import unittest2
import lldb
from lldbtest import *

class NamespaceTestCase(TestBase):

    mydir = "namespace"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym_and_run_command(self):
        """Test that anonymous and named namespace variables display correctly."""
        self.buildDsym()
        self.namespace_variable_commands()

    # rdar://problem/8659840
    # runCmd: frame variable -c -G i
    # runCmd failed!
    # error: can't find global variable 'i'
    def test_with_dwarf_and_run_command(self):
        """Test that anonymous and named namespace variables display correctly."""
        self.buildDwarf()
        self.namespace_variable_commands()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line numbers for declarations of namespace variables i and j.
        self.line_var_i = line_number('main.cpp',
                '// Find the line number for anonymous namespace variable i.')
        self.line_var_j = line_number('main.cpp',
                '// Find the line number for named namespace variable j.')
        # And the line number to break at.
        self.line_break = line_number('main.cpp',
                '// Set break point at this line.')

    def namespace_variable_commands(self):
        """Test that anonymous and named namespace variables display correctly."""
        self.runCmd("file a.out", CURRENT_EXECUTABLE_SET)

        self.expect("breakpoint set -f main.cpp -l %d" % self.line_break,
                    BREAKPOINT_CREATED,
            startstr = "Breakpoint created: 1: file ='main.cpp', line = %d, locations = 1" %
                        self.line_break)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['state is stopped',
                       'stop reason = breakpoint'])

        # On Mac OS X, gcc 4.2 emits the wrong debug info with respect to types.
        slist = ['(int) a = 12', 'anon_uint', 'a_uint', 'b_uint', 'y_uint']
        if sys.platform.startswith("darwin") and self.getCompiler() in ['clang', 'llvm-gcc']:
            slist = ['(int) a = 12',
                     '(my_uint_t) anon_uint = 0',
                     '(A::uint_t) a_uint = 1',
                     '(A::B::uint_t) b_uint = 2',
                     '(Y::uint_t) y_uint = 3']

        # 'frame variable' displays the local variables with type information.
        self.expect('frame variable', VARIABLES_DISPLAYED_CORRECTLY,
            substrs = slist)

        # 'frame variable' with basename 'i' should work.
        self.expect("frame variable -c -G i",
            startstr = "main.cpp:%d: (int) (anonymous namespace)::i = 3" % self.line_var_i)
        # main.cpp:12: (int) (anonymous namespace)::i = 3

        # 'frame variable' with basename 'j' should work, too.
        self.expect("frame variable -c -G j",
            startstr = "main.cpp:%d: (int) A::B::j = 4" % self.line_var_j)
        # main.cpp:19: (int) A::B::j = 4

        # 'frame variable' should support address-of operator.
        self.runCmd("frame variable &i")

        # 'frame variable' with fully qualified name 'A::B::j' should work.
        self.expect("frame variable 'A::B::j", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = '(int) A::B::j = 4')

        # So should the anonymous namespace case.
        self.expect("frame variable '(anonymous namespace)::i", VARIABLES_DISPLAYED_CORRECTLY,
            startstr = '(int) (anonymous namespace)::i = 3')

        # rdar://problem/8660275
        # test/namespace: 'expression -- i+j' not working
        self.expect("expression -- i + j",
            startstr = "(int) $0 = 7")
        # (int) $0 = 7

        # rdar://problem/8668674
        # expression command with fully qualified namespace for a variable does not work
        #self.runCmd("expression -- '(anonymous namespace)::i'")
        #
        #self.runCmd("expression -- 'A::B::j'")


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
