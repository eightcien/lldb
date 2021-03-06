//===-- main.c --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <stdio.h>

int g_file_global_int = 42;
const char *g_file_global_cstr = "g_file_global_cstr";
static const char *g_file_static_cstr = "g_file_static_cstr";

extern int g_a;
int main (int argc, char const *argv[])
{
	static const char *g_func_static_cstr = "g_func_static_cstr";
	printf ("%s %s\n", g_file_global_cstr, g_file_static_cstr);
    return g_file_global_int + g_a; // Set break point at this line.  //// break $source:$line; continue; var -global g_a -global g_global_int
}
