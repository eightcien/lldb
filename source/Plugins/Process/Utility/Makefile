##===- source/Plugins/Utility/Makefile ---------------------*- Makefile -*-===##
# 
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
# 
##===----------------------------------------------------------------------===##

LLDB_LEVEL := ../../../..
LLVM_LEVEL = $(LLDB_LEVEL)/../..
LIBRARYNAME := lldbPluginUtility
BUILD_ARCHIVE = 1

# Bring in the configuration first as we need to compile according to platform.
include $(LLVM_LEVEL)/Makefile.config

# Pick out the sources on a per-platform basis.  The default is to build
# everything in this directory if SOURCES is not defined.
ifeq ($(HOST_OS),Linux)
SOURCES := ArchDefaultUnwindPlan-x86.cpp ArchVolatileRegs-x86.cpp \
           UnwindAssemblyProfiler-x86.cpp
endif

include $(LLDB_LEVEL)/Makefile
