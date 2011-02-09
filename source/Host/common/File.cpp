//===-- FileSpec.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "lldb/Host/File.h"

#include <fcntl.h>

#include "lldb/Core/Error.h"
#include "lldb/Host/FileSpec.h"

using namespace lldb;
using namespace lldb_private;

static const char *
GetStreamOpenModeFromOptions (uint32_t options)
{
    if (options & File::eOpenOptionAppend)
    {
        if (options & File::eOpenOptionRead)
        {
            if (options & File::eOpenOptionCanCreateNewOnly)
                return "a+x";
            else
                return "a+";
        }
        else if (options & File::eOpenOptionWrite)
        {
            if (options & File::eOpenOptionCanCreateNewOnly)
                return "ax";
            else
                return "a";
        }
    }
    else if (options & File::eOpenOptionRead && options & File::eOpenOptionWrite)
    {
        if (options & File::eOpenOptionCanCreate)
        {
            if (options & File::eOpenOptionCanCreateNewOnly)
                return "w+x";
            else
                return "w+";
        }
        else
            return "r+";
    }
    else if (options & File::eOpenOptionRead)
    {
        return "r";
    }
    else if (options & File::eOpenOptionWrite)
    {
        return "w";
    }
    return NULL;
}

int File::kInvalidDescriptor = -1;
FILE * File::kInvalidStream = NULL;

File::File(const char *path, uint32_t options, uint32_t permissions) :
    m_descriptor (kInvalidDescriptor),
    m_stream (kInvalidStream),
    m_options (0),
    m_owned (false)
{
    Open (path, options, permissions);
}

File::File (const File &rhs) :
    m_descriptor (kInvalidDescriptor),
    m_stream (kInvalidStream),
    m_options (0),
    m_owned (false)
{
    Duplicate (rhs);
}
    

File &
File::operator = (const File &rhs)
{
    if (this != &rhs)
        Duplicate (rhs);        
    return *this;
}

File::~File()
{
    Close ();
}


int
File::GetDescriptor() const
{
    if (DescriptorIsValid())
        return m_descriptor;

    // Don't open the file descriptor if we don't need to, just get it from the
    // stream if we have one.
    if (StreamIsValid())
        return fileno (m_stream);

    // Invalid descriptor and invalid stream, return invalid descriptor.
    return kInvalidDescriptor;
}

void
File::SetDescriptor (int fd, bool transfer_ownership)
{
    if (IsValid())
        Close();
    m_descriptor = fd;
    m_owned = transfer_ownership;
}


FILE *
File::GetStream ()
{
    if (!StreamIsValid())
    {
        if (DescriptorIsValid())
        {
            const char *mode = GetStreamOpenModeFromOptions (m_options);
            if (mode)
                m_stream = ::fdopen (m_descriptor, mode);
        }
    }
    return m_stream;
}


void
File::SetStream (FILE *fh, bool transfer_ownership)
{
    if (IsValid())
        Close();
    m_stream = fh;
    m_owned = transfer_ownership;
}

Error
File::Duplicate (const File &rhs)
{
    Error error;
    if (IsValid ())
        Close();

    if (rhs.DescriptorIsValid())
    {
        m_descriptor = ::fcntl(rhs.GetDescriptor(), F_DUPFD);
        if (!DescriptorIsValid())
            error.SetErrorToErrno();
        else
        {
            m_options = rhs.m_options;
            m_owned = true;
        }
    }
    else
    {
        error.SetErrorString ("invalid file to duplicate");
    }
    return error;
}

Error
File::Open (const char *path, uint32_t options, uint32_t permissions)
{
    Error error;
    if (IsValid())
        Close ();

    int oflag = 0;
    if (options & eOpenOptionRead && 
        options & eOpenOptionWrite )
        oflag |= O_RDWR;
    else if (options & eOpenOptionRead)
        oflag |= O_RDONLY;
    else if (options & eOpenOptionWrite)
        oflag |= O_WRONLY;
    
    if (options & eOpenOptionNonBlocking)
        oflag |= O_NONBLOCK;

    if (options & eOpenOptionAppend)
        oflag |= O_APPEND;
    else
        oflag |= O_TRUNC;

    if (options & eOpenOptionCanCreate)
        oflag |= O_CREAT;
    
    if (options & eOpenOptionCanCreateNewOnly)
        oflag |= O_CREAT | O_EXCL;
    
    
    if (options & eOpenOptionSharedLock)
        oflag |= O_SHLOCK;

    if (options & eOpenOptionExclusiveLock)
        oflag |= O_EXLOCK;

    mode_t mode = 0;
    if (permissions & ePermissionsUserRead)     mode |= S_IRUSR;
    if (permissions & ePermissionsUserWrite)    mode |= S_IWUSR;
    if (permissions & ePermissionsUserExecute)  mode |= S_IXUSR;
    if (permissions & ePermissionsGroupRead)    mode |= S_IRGRP;
    if (permissions & ePermissionsGroupWrite)   mode |= S_IWGRP;
    if (permissions & ePermissionsGroupExecute) mode |= S_IXGRP;
    if (permissions & ePermissionsWorldRead)    mode |= S_IROTH;
    if (permissions & ePermissionsWorldWrite)   mode |= S_IWOTH;
    if (permissions & ePermissionsWorldExecute) mode |= S_IXOTH;

    m_descriptor = ::open(path, oflag, mode);
    if (!DescriptorIsValid())
        error.SetErrorToErrno();
    else
        m_owned = true;
    
    return error;
}

Error
File::Close ()
{
    Error error;
    if (IsValid ())
    {
        if (m_owned)
        {
            if (StreamIsValid())
            {
                if (::fclose (m_stream) == EOF)
                    error.SetErrorToErrno();
            }
            
            if (DescriptorIsValid())
            {
                if (::close (m_descriptor) != 0)
                    error.SetErrorToErrno();
            }
        }
        m_descriptor = kInvalidDescriptor;
        m_stream = kInvalidStream;
        m_options = 0;
        m_owned = false;
    }
    return error;
}


Error
File::GetFileSpec (FileSpec &file_spec) const
{
    Error error;
    if (IsValid ())
    {
        char path[PATH_MAX];
        if (::fcntl(GetDescriptor(), F_GETPATH, path) == -1)
            error.SetErrorToErrno();
        else
            file_spec.SetFile (path, false);
    }
    else 
        error.SetErrorString("invalid file handle");

    if (error.Fail())
        file_spec.Clear();
    return error;
}

Error
File::SeekFromStart (off_t& offset)
{
    Error error;
    if (DescriptorIsValid())
    {
        offset = ::lseek (m_descriptor, offset, SEEK_SET);

        if (offset == -1)
            error.SetErrorToErrno();
    }
    else 
    {
        error.SetErrorString("invalid file handle");
    }
    return error;
}

Error
File::SeekFromCurrent (off_t& offset)
{
    Error error;
    if (DescriptorIsValid())
    {
        offset = ::lseek (m_descriptor, offset, SEEK_CUR);
        
        if (offset == -1)
            error.SetErrorToErrno();
    }
    else 
    {
        error.SetErrorString("invalid file handle");
    }
    return error;
}

Error
File::SeekFromEnd (off_t& offset)
{
    Error error;
    if (DescriptorIsValid())
    {
        offset = ::lseek (m_descriptor, offset, SEEK_CUR);
        
        if (offset == -1)
            error.SetErrorToErrno();
    }
    else 
    {
        error.SetErrorString("invalid file handle");
    }
    return error;
}

Error
File::Flush ()
{
    Error error;
    if (StreamIsValid())
    {
        if (::fflush (m_stream) == EOF)
            error.SetErrorToErrno();
    }
    else if (!DescriptorIsValid())
    {
        error.SetErrorString("invalid file handle");
    }
    return error;
}


Error
File::Sync ()
{
    Error error;
    if (DescriptorIsValid())
    {
        if (::fsync (m_descriptor) == -1)
            error.SetErrorToErrno();
    }
    else 
    {
        error.SetErrorString("invalid file handle");
    }
    return error;
}

Error
File::Read (void *buf, size_t &num_bytes)
{
    Error error;
    if (DescriptorIsValid())
    {
        ssize_t bytes_read = ::read (m_descriptor, buf, num_bytes);
        if (bytes_read == -1)
        {
            error.SetErrorToErrno();
            num_bytes = 0;
        }
        else
            num_bytes = bytes_read;
    }
    else if (StreamIsValid())
    {
        size_t bytes_read = ::fread (buf, 1, num_bytes, m_stream);
        if (bytes_read == 0)
        {
            if (::feof(m_stream))
                error.SetErrorString ("feof");
            else if (::ferror (m_stream))
                error.SetErrorString ("ferror");
            num_bytes = 0;
        }
        else
            num_bytes = bytes_read;
    }
    else 
    {
        num_bytes = 0;
        error.SetErrorString("invalid file handle");
    }
    return error;
}
          
Error
File::Write (const void *buf, size_t &num_bytes)
{
    Error error;
    if (DescriptorIsValid())
    {
        ssize_t bytes_written = ::write (m_descriptor, buf, num_bytes);
        if (bytes_written == -1)
        {
            error.SetErrorToErrno();
            num_bytes = 0;
        }
        else
            num_bytes = bytes_written;
    }
    else if (StreamIsValid())
    {
        size_t bytes_written = ::fwrite (buf, 1, num_bytes, m_stream);
        if (bytes_written == 0)
        {
            if (::feof(m_stream))
                error.SetErrorString ("feof");
            else if (::ferror (m_stream))
                error.SetErrorString ("ferror");
            num_bytes = 0;
        }
        else
            num_bytes = bytes_written;
        
    }
    else 
    {
        num_bytes = 0;
        error.SetErrorString("invalid file handle");
    }
    return error;
}


Error
File::Read (void *buf, size_t &num_bytes, off_t &offset)
{
    Error error;
    int fd = GetDescriptor();
    if (fd != kInvalidDescriptor)
    {
        ssize_t bytes_read = ::pread (fd, buf, num_bytes, offset);
        if (bytes_read < 0)
        {
            num_bytes = 0;
            error.SetErrorToErrno();
        }
        else
        {
            offset += bytes_read;
            num_bytes = bytes_read;
        }
    }
    else 
    {
        num_bytes = 0;
        error.SetErrorString("invalid file handle");
    }
    return error;
}

Error
File::Write (const void *buf, size_t &num_bytes, off_t &offset)
{
    Error error;
    int fd = GetDescriptor();
    if (fd != kInvalidDescriptor)
    {
        ssize_t bytes_written = ::pwrite (m_descriptor, buf, num_bytes, offset);
        if (bytes_written < 0)
        {
            num_bytes = 0;
            error.SetErrorToErrno();
        }
        else
        {
            offset += bytes_written;
            num_bytes = bytes_written;
        }
    }
    else 
    {
        num_bytes = 0;
        error.SetErrorString("invalid file handle");
    }
    return error;
}

//------------------------------------------------------------------
// Print some formatted output to the stream.
//------------------------------------------------------------------
int
File::Printf (const char *format, ...)
{
    va_list args;
    va_start (args, format);
    int result = PrintfVarArg (format, args);
    va_end (args);
    return result;
}

//------------------------------------------------------------------
// Print some formatted output to the stream.
//------------------------------------------------------------------
int
File::PrintfVarArg (const char *format, va_list args)
{
    int result = 0;
    if (DescriptorIsValid())
    {
        char *s = NULL;
        result = vasprintf(&s, format, args);
        if (s != NULL)
        {
            if (result > 0)
            {
                size_t s_len = result;
                Write (s, s_len);
                result = s_len;
            }
            free (s);
        }
    }
    else if (StreamIsValid())
    {
        result = ::vfprintf (m_stream, format, args);
    }
    return result;
}
