"""
Test lldb command aliases.
"""

import os, time
import unittest2
import lldb
from lldbtest import *

class AliasTestCase(TestBase):

    mydir = "alias_tests"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_with_dsym (self):
        self.buildDsym ()
        self.alias_tests ()

    def test_with_dwarf (self):
        self.buildDwarf ()
        self.alias_tests ()

    def alias_tests (self):
        exe = os.path.join (os.getcwd(), "a.out")
        self.expect("file " + exe,
                    patterns = [ "Current executable set to .*a.out" ])


        self.runCmd (r'''command alias hello expr (int) printf ("\n\nHello, anybody!\n\n")''')

        self.runCmd ("command alias python script")
        self.runCmd (r'''python print "\n\n\nWhoopee!\n\n\n"''')
#        self.expect (r'''python print "\n\n\nWhoopee!\n\n\n"''',
#                     substrs = [ "Whoopee!" ])

        self.runCmd (r'''python print "\n\t\x68\x65\x6c\x6c\x6f\n"''')
#        self.expect (r'''python print "\n\t\x68\x65\x6c\x6c\x6f\n"''',
#                     substrs = [ "hello" ])

        self.runCmd (r'''command alias pp python print "\n\t\x68\x65\x6c\x6c\x6f\n"''')
        self.runCmd ("pp")
#        self.expect ("pp",
#                     substrs = [ "hello" ])


        self.runCmd ("commands alias alias commands alias")
        self.runCmd ("commands alias unalias commands unalias")

        self.runCmd ("alias myrun process launch -t%1 --")
        self.runCmd ("alias bp breakpoint")

        self.expect ("alias bpa bp add",
                     COMMAND_FAILED_AS_EXPECTED, error = True,
                     substrs = [ "'add' is not a valid sub-command of 'bp'" ])

        self.runCmd ("alias bpa bp command add")
        self.runCmd ("alias bpi bp list")

        self.expect ("bp set -n foo",
                     startstr = "Breakpoint created: 1: name = 'foo', locations = 1")

        self.expect ("bp set -n sum",
                     startstr = "Breakpoint created: 2: name = 'sum', locations = 1")

        self.runCmd ("alias bfl bp set -f %1 -l %2")
        self.expect ("bfl main.cpp 32",
                     startstr = "Breakpoint created: 3: file ='main.cpp', line = 32, locations = 1")

        self.expect ("bpi",
                     startstr = "Current breakpoints:",
                     substrs = [ "1: name = 'foo', locations = 1",
                                 "2: name = 'sum', locations = 1",
                                 "3: file ='main.cpp', line = 32, locations = 1" ])

        self.runCmd ("bpa -p 1 -o 'print frame; print bp_loc'")
        self.runCmd ("bpa -c 2 -o 'frame variable b'")
        self.expect ("bpi",
                     substrs = [ "Current breakpoints:",
                                 "1: name = 'foo', locations = 1",
                                 "print frame; print bp_loc",
                                 "2: name = 'sum', locations = 1",
                                 "frame variable b" ])


        self.expect ("help run",
                     substrs = [ "'run' is an abbreviation for 'process launch --'" ])


        self.expect ("run",
                     patterns = [ "Process .* launched: .*a.out" ])

        self.expect (r'''expression printf("\x68\x65\x6c\x6c\x6f\n")''',
                     substrs = [ "(unsigned long) $",
                                 "= 6" ])

        self.expect ("hello",
                     substrs = [ "(int) $", 
                                 "= 19" ])

        self.expect ("expr -f x -- 68",
                     substrs = [ "(int) $",
                                 "= 0x00000044" ])

        self.runCmd ("alias exprf expr -f %1")
        self.runCmd ("alias exprf2 expr -f %1 --")
        self.expect ("exprf x -- 1234",
                     substrs = [ "(int) $",
                                 "= 0x000004d2" ])

        self.expect ('exprf2 s "Hi there!"',
                     substrs = [ "(const char) [0] = 'H'",
                                 "(const char) [1] = 'i'",
                                 "(const char) [2] = ' '",
                                 "(const char) [3] = 't'",
                                 "(const char) [4] = 'h'",
                                 "(const char) [5] = 'e'",
                                 "(const char) [6] = 'r'",
                                 "(const char) [7] = 'e'",
                                 "(const char) [8] = '!'",
                                 "(const char) [9] = '\\0'" ])
        

        self.expect ("exprf x 1234",
                     COMMAND_FAILED_AS_EXPECTED, error = True,
                     substrs = [ "use of undeclared identifier 'f'",
                                 "1 errors parsing expression" ])



if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()

