"""
If the build* function is passed the compiler argument, for example, 'llvm-gcc',
it is passed as a make variable to the make command.  Otherwise, we check the
LLDB_CC environment variable; if it is defined, it is passed as a make variable
to the make command.

If neither the compiler keyword argument nor the LLDB_CC environment variable is
specified, no CC make variable is passed to the make command.  The Makefile gets
to define the default CC being used.

Same idea holds for LLDB_ARCH environment variable, which maps to the ARCH make
variable.
"""

import os
import lldbtest

#print "Hello, darwin plugin!"

def getCompiler():
    """Returns the compiler in effect the test suite is now running with."""
    return os.environ["CC"] if "CC" in os.environ else ""

def getRunSpec():
    """Environment variable spec to run this test again, invoked from within
    dumpSessionInfo()."""
    return "%s%s" % (getArchSpec(None), getCCSpec(None))

def getArchSpec(architecture):
    """
    Helper function to return the key-value string to specify the architecture
    used for the make system.
    """
    arch = architecture if architecture else None
    if not arch and "ARCH" in os.environ:
        arch = os.environ["ARCH"]

    # Note the leading space character.
    return (" ARCH=" + arch) if arch else ""

def getCCSpec(compiler):
    """
    Helper function to return the key-value string to specify the compiler
    used for the make system.
    """
    cc = compiler if compiler else None
    if not cc and "CC" in os.environ:
        cc = os.environ["CC"]

    # Note the leading space character.
    return (" CC=" + cc) if cc else ""

def getCmdLine(d):
    """
    Helper function to return a properly formatted command line argument(s)
    string used for the make system.
    """

    # If d is None or an empty mapping, just return an empty string.
    if not d:
        return ""

    cmdline = " ".join(["%s='%s'" % (k, v) for k, v in d.items()])

    # Note the leading space character.
    return " " + cmdline


def buildDefault(sender=None, architecture=None, compiler=None, dictionary=None):
    """Build the binaries the default way."""
    lldbtest.system(["/bin/sh", "-c",
                     "make clean" + getCmdLine(dictionary) + "; make"
                     + getArchSpec(architecture) + getCCSpec(compiler)
                     + getCmdLine(dictionary)],
                    sender=sender)

    # True signifies that we can handle building default.
    return True

def buildDsym(sender=None, architecture=None, compiler=None, dictionary=None):
    """Build the binaries with dsym debug info."""
    lldbtest.system(["/bin/sh", "-c",
                     "make clean" + getCmdLine(dictionary)
                     + "; make MAKE_DSYM=YES"
                     + getArchSpec(architecture) + getCCSpec(compiler)
                     + getCmdLine(dictionary)],
                    sender=sender)

    # True signifies that we can handle building dsym.
    return True

def buildDwarf(sender=None, architecture=None, compiler=None, dictionary=None):
    """Build the binaries with dwarf debug info."""
    lldbtest.system(["/bin/sh", "-c",
                     "make clean" + getCmdLine(dictionary)
                     + "; make MAKE_DSYM=NO"
                     + getArchSpec(architecture) + getCCSpec(compiler)
                     + getCmdLine(dictionary)],
                    sender=sender)

    # True signifies that we can handle building dsym.
    return True

def cleanup(sender=None, dictionary=None):
    """Perform a platform-specific cleanup after the test."""
    if os.path.isfile("Makefile"):
        lldbtest.system(["/bin/sh", "-c", "make clean"+getCmdLine(dictionary)],
                        sender=sender)

    # True signifies that we can handle building dsym.
    return True
