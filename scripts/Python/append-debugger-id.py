#
# append-debugger-id.py
#
# This script adds a global variable, 'debugger_unique_id' to the lldb
# module (which was automatically generated via running swig), and 
# initializes it to 0.
#
# It also calls SBDebugger.Initialize() to initialize the lldb debugger
# subsystem.
#

import sys

if len (sys.argv) != 2:
    output_name = "./lldb.py"
else:
    output_name = sys.argv[1] + "/lldb.py"

# print "output_name is '" + output_name + "'"

with open(output_name, 'a') as f_out:
    f_out.write("debugger_unique_id = 0\n")
    f_out.write("SBDebugger.Initialize()\n")
