"""
Test lldb process launch flags..
"""

import os, time
import unittest2
import lldb
from lldbtest import *

class ProcessLaunchTestCase(TestBase):

    mydir = "process_launch"

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    def test_io_with_dsym (self):
        self.buildDsym ()
        self.process_io_test ()

    def test_io_with_dwarf (self):
        self.buildDwarf ()
        self.process_io_test ()

    def process_io_test (self):
        """Test that process launch I/O redirection flags work properly."""
        exe = os.path.join (os.getcwd(), "a.out")
        self.expect("file " + exe,
                    patterns = [ "Current executable set to .*a.out" ])


        in_file = os.path.join (os.getcwd(), "input-file.txt")
        out_file = os.path.join (os.getcwd(), "output-test.out")
        err_file = os.path.join (os.getcwd(), "output-test.err")


        # Make sure the output files do not exist before launching the process
        try:
            os.remove (out_file)
        except OSError:
            pass

        try:
            os.remove (err_file)
        except OSError:
            pass

        launch_command = "process launch -i " + in_file + " -o " + out_file + " -e " + err_file
        
        self.expect (launch_command,
                     patterns = [ "Process .* launched: .*a.out" ])


        success = True
        err_msg = ""

        # Check to see if the 'stdout' file was created
        try:
            out_f = open (out_file)
        except IOError:
            success = False
            err_msg = err_msg + "   ERROR: stdout file was not created.\n"
        else:
            # Check to see if the 'stdout' file contains the right output
            line = out_f.readline ();
            if line != "This should go to stdout.\n":
                success = False
                err_msg = err_msg + "    ERROR: stdout file does not contain correct output.\n"
                out_f.close();
            
        # Try to delete the 'stdout' file
        try:
            os.remove (out_file)
        except OSError:
            pass

        # Check to see if the 'stderr' file was created
        try:
            err_f = open (err_file)
        except IOError:
            success = False
            err_msg = err_msg + "     ERROR:  stderr file was not created.\n"
        else:
            # Check to see if the 'stderr' file contains the right output
            line = err_f.readline ()
            if line != "This should go to stderr.\n":
                success = False
                err_msg = err_msg + "    ERROR: stderr file does not contain correct output.\n\
"
                err_f.close()

        # Try to delete the 'stderr' file
        try:
            os.remove (err_file)
        except OSError:
            pass

        if not success:
            self.fail (err_msg)

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()

