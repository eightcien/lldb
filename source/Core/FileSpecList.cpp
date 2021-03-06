//===-- FileSpecList.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/Stream.h"
#include <algorithm>

using namespace lldb_private;
using namespace std;

//------------------------------------------------------------------
// Default constructor
//------------------------------------------------------------------
FileSpecList::FileSpecList() :
    m_files()
{
}

//------------------------------------------------------------------
// Copy constructor
//------------------------------------------------------------------
FileSpecList::FileSpecList(const FileSpecList& rhs) :
    m_files(rhs.m_files)
{
}

//------------------------------------------------------------------
// Destructor
//------------------------------------------------------------------
FileSpecList::~FileSpecList()
{
}

//------------------------------------------------------------------
// Assignment operator
//------------------------------------------------------------------
const FileSpecList&
FileSpecList::operator= (const FileSpecList& rhs)
{
    if (this != &rhs)
        m_files = rhs.m_files;
    return *this;
}

//------------------------------------------------------------------
// Append the "file_spec" to the end of the file spec list.
//------------------------------------------------------------------
void
FileSpecList::Append(const FileSpec &file_spec)
{
    m_files.push_back(file_spec);
}

//------------------------------------------------------------------
// Only append the "file_spec" if this list doesn't already contain
// it.
//
// Returns true if "file_spec" was added, false if this list already
// contained a copy of "file_spec".
//------------------------------------------------------------------
bool
FileSpecList::AppendIfUnique(const FileSpec &file_spec)
{
    collection::iterator pos, end = m_files.end();
    if (find(m_files.begin(), end, file_spec) == end)
    {
        m_files.push_back(file_spec);
        return true;
    }
    return false;
}

//------------------------------------------------------------------
// Clears the file list.
//------------------------------------------------------------------
void
FileSpecList::Clear()
{
    m_files.clear();
}

//------------------------------------------------------------------
// Dumps the file list to the supplied stream pointer "s".
//------------------------------------------------------------------
void
FileSpecList::Dump(Stream *s) const
{
    for_each (m_files.begin(), m_files.end(), bind2nd(mem_fun_ref(&FileSpec::Dump),s));
}

//------------------------------------------------------------------
// Find the index of the file in the file spec list that matches
// "file_spec" starting "start_idx" entries into the file spec list.
//
// Returns the valid index of the file that matches "file_spec" if
// it is found, else UINT32_MAX is returned.
//------------------------------------------------------------------
uint32_t
FileSpecList::FindFileIndex (uint32_t start_idx, const FileSpec &file_spec) const
{
    const uint32_t num_files = m_files.size();
    uint32_t idx;

    // When looking for files, we will compare only the filename if the
    // FILE_SPEC argument is empty
    bool compare_filename_only = file_spec.GetDirectory().IsEmpty();

    for (idx = start_idx; idx < num_files; ++idx)
    {
        if (compare_filename_only)
        {
            if (m_files[idx].GetFilename() == file_spec.GetFilename())
                return idx;
        }
        else
        {
            if (m_files[idx] == file_spec)
                return idx;
        }
    }

    // We didn't find the file, return an invalid index
    return UINT32_MAX;
}

//------------------------------------------------------------------
// Returns the FileSpec object at index "idx". If "idx" is out of
// range, then an empty FileSpec object will be returned.
//------------------------------------------------------------------
const FileSpec &
FileSpecList::GetFileSpecAtIndex(uint32_t idx) const
{

    if (idx < m_files.size())
        return m_files[idx];
    static FileSpec g_empty_file_spec;
    return g_empty_file_spec;
}

const FileSpec *
FileSpecList::GetFileSpecPointerAtIndex(uint32_t idx) const
{
    if (idx < m_files.size())
        return &m_files[idx];
    return NULL;
}

//------------------------------------------------------------------
// Return the size in bytes that this object takes in memory. This
// returns the size in bytes of this object's member variables and
// any FileSpec objects its member variables contain, the result
// doesn't not include the string values for the directories any
// filenames as those are in shared string pools.
//------------------------------------------------------------------
size_t
FileSpecList::MemorySize () const
{
    size_t mem_size = sizeof(FileSpecList);
    collection::const_iterator pos, end = m_files.end();
    for (pos = m_files.begin(); pos != end; ++pos)
    {
        mem_size += pos->MemorySize();
    }

    return mem_size;
}

//------------------------------------------------------------------
// Return the number of files in the file spec list.
//------------------------------------------------------------------
uint32_t
FileSpecList::GetSize() const
{
    return m_files.size();
}

size_t
FileSpecList::GetFilesMatchingPartialPath (const char *path, bool dir_okay, FileSpecList &matches)
{
#if 0  // FIXME: Just sketching...
    matches.Clear();
    FileSpec path_spec = FileSpec (path);
    if (path_spec.Exists ())
    {
        FileSpec::FileType type = path_spec.GetFileType();
        if (type == FileSpec::eFileTypeSymbolicLink)
            // Shouldn't there be a Resolve on a file spec that real-path's it?
        {
        }

        if (type == FileSpec::eFileTypeRegular
            || (type == FileSpec::eFileTypeDirectory && dir_okay))
        {
            matches.Append (path_spec);
            return 1;
        }
        else if (type == FileSpec::eFileTypeDirectory)
        {
            // Fill the match list with all the files in the directory:

        }
        else
        {
            return 0;
        }

    }
    else
    {
        ConstString dir_name = path_spec.GetDirectory();
        Constring file_name = GetFilename();
        if (dir_name == NULL)
        {
            // Match files in the CWD.
        }
        else
        {
            // Match files in the given directory:

        }
    }
#endif
    return 0;
}
