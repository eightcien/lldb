//===-- ObjectFile.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/RegularExpression.h"
#include "lldb/Core/Timer.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Symbol/SymbolFile.h"

using namespace lldb;
using namespace lldb_private;

ObjectFile*
ObjectFile::FindPlugin (Module* module, const FileSpec* file, lldb::addr_t file_offset, lldb::addr_t file_size)
{
    Timer scoped_timer (__PRETTY_FUNCTION__,
                        "ObjectFile::FindPlugin (module = %s/%s, file = %p, file_offset = 0x%z8.8x, file_size = 0x%z8.8x)",
                        module->GetFileSpec().GetDirectory().AsCString(),
                        module->GetFileSpec().GetFilename().AsCString(),
                        file, file_offset, file_size);
    std::auto_ptr<ObjectFile> object_file_ap;

    if (module != NULL)
    {
        if (file)
        {
            if (file_size == 0)
                file_size = file->GetByteSize();

            if (file_size == 0)
            {
                // Check for archive file with format "/path/to/archive.a(object.o)"
                char path_with_object[PATH_MAX*2];
                module->GetFileSpec().GetPath(path_with_object, sizeof(path_with_object));

                RegularExpression g_object_regex("(.*)\\(([^\\)]+)\\)$");
                if (g_object_regex.Execute (path_with_object, 2))
                {
                    FileSpec archive_file;
                    std::string path;
                    std::string object;
                    if (g_object_regex.GetMatchAtIndex (path_with_object, 1, path) &&
                        g_object_regex.GetMatchAtIndex (path_with_object, 2, object))
                    {
                        archive_file.SetFile (path.c_str(), false);
                        file_size = archive_file.GetByteSize();
                        if (file_size > 0)
                            module->SetFileSpecAndObjectName (archive_file, ConstString(object.c_str()));
                    }
                }
            }

            // No need to delegate further if (file_offset, file_size) exceeds the total file size.
            // This is the base case.
            if (file_offset + file_size > file->GetByteSize())
                return NULL;

            DataBufferSP file_header_data_sp(file->ReadFileContents(file_offset, 512));
            uint32_t idx;

            // Check if this is a normal object file by iterating through
            // all object file plugin instances.
            ObjectFileCreateInstance create_object_file_callback;
            for (idx = 0; (create_object_file_callback = PluginManager::GetObjectFileCreateCallbackAtIndex(idx)) != NULL; ++idx)
            {
                object_file_ap.reset (create_object_file_callback(module, file_header_data_sp, file, file_offset, file_size));
                if (object_file_ap.get())
                    return object_file_ap.release();
            }

            // Check if this is a object container by iterating through
            // all object container plugin instances and then trying to get
            // an object file from the container.
            ObjectContainerCreateInstance create_object_container_callback;
            for (idx = 0; (create_object_container_callback = PluginManager::GetObjectContainerCreateCallbackAtIndex(idx)) != NULL; ++idx)
            {
                std::auto_ptr<ObjectContainer> object_container_ap(create_object_container_callback(module, file_header_data_sp, file, file_offset, file_size));

                if (object_container_ap.get())
                    object_file_ap.reset (object_container_ap->GetObjectFile(file));

                if (object_file_ap.get())
                    return object_file_ap.release();
            }
        }
    }
    return NULL;
}

bool 
ObjectFile::SetModulesArchitecture (const ArchSpec &new_arch)
{
    return m_module->SetArchitecture (new_arch);
}

