/*
 * libjingle
 * Copyright 2004--2006, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <cassert>

#ifdef WIN32
#include "talk/base/convert.h"
#endif

#include "talk/base/pathutils.h"
#include "talk/base/fileutils.h"
#include "talk/base/stringutils.h"
#include "talk/base/stream.h"

#include "talk/base/unixfilesystem.h"
#include "talk/base/win32filesystem.h"

#ifndef WIN32
#define MAX_PATH 256
#endif

namespace talk_base {

//////////////////////////
// Directory Iterator   //
//////////////////////////

// A Directoryraverser is created with a given directory. It originally points to
// the first file in the directory, and can be advanecd with Next(). This allows you
// to get information about each file.

  // Constructor
DirectoryIterator::DirectoryIterator() : 
#ifdef _WIN32
  handle_(INVALID_HANDLE_VALUE)
#else
  dir_(NULL), dirent_(NULL)
#endif
{}

  // Destructor
DirectoryIterator::~DirectoryIterator() {
#ifdef WIN32
  if (handle_ != INVALID_HANDLE_VALUE)
    ::FindClose(handle_);
#else
  if (dir_)
    closedir(dir_);
#endif
}

  // Starts traversing a directory.
  // dir is the directory to traverse
  // returns true if the directory exists and is valid
bool DirectoryIterator::Iterate(const Pathname &dir) {
  directory_ = dir.pathname();
#ifdef WIN32
  if (handle_ != INVALID_HANDLE_VALUE)
    ::FindClose(handle_);
  std::string d = dir.pathname() + '*';
  handle_ = ::FindFirstFile(Utf16(d).AsWz(), &data_);
  if (handle_ == INVALID_HANDLE_VALUE)
    return false;
#else
  if (dir_ != NULL)
    closedir(dir_);
  dir_ = ::opendir(directory_.c_str());
  if (dir_ == NULL)
    return false;
  dirent_ = readdir(dir_);
  if (dirent_ == NULL)
    return false;
  
  if (::stat(std::string(directory_ + Name()).c_str(), &stat_) != 0)
    return false;
#endif
  return true;
}

  // Advances to the next file
  // returns true if there were more files in the directory.
bool DirectoryIterator::Next() {
#ifdef WIN32
  return ::FindNextFile(handle_, &data_) == TRUE;
#else
  dirent_ = ::readdir(dir_);
  if (dirent_ == NULL)
    return false;

  return ::stat(std::string(directory_ + Name()).c_str(), &stat_) == 0;
#endif
}

  // returns true if the file currently pointed to is a directory
bool DirectoryIterator::IsDirectory() const {
#ifdef WIN32
  return (data_.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != FALSE;
#else
  return S_ISDIR(stat_.st_mode);
#endif
}

  // returns the name of the file currently pointed to
std::string DirectoryIterator::Name() const {
#ifdef WIN32
  return Utf8(data_.cFileName).AsString();
#else
  assert(dirent_ != NULL);
  return dirent_->d_name;
#endif
}

  // returns the size of the file currently pointed to
size_t DirectoryIterator::FileSize() const {
#ifndef WIN32
      	return stat_.st_size;
#else
	return data_.nFileSizeLow;
#endif
}
 
  // returns the last modified time of this file
time_t DirectoryIterator::FileModifyTime() const {
#ifdef WIN32
return 0;
#else
  return stat_.st_mtime;
#endif
}

Filesystem *Filesystem::default_filesystem_ = 0;
  

bool Filesystem::CreateFolder(const Pathname &pathname)
{
  return EnsureDefaultFilesystem()->CreateFolderI(pathname);
}

FileStream *Filesystem::OpenFile(const Pathname &filename, 
				 const std::string &mode)
{
  return EnsureDefaultFilesystem()->OpenFileI(filename, mode);
}

bool Filesystem::DeleteFile(const Pathname &filename)
{
  return EnsureDefaultFilesystem()->DeleteFileI(filename);
}

bool Filesystem::MoveFile(const Pathname &old_path, const Pathname &new_path)
{
  return EnsureDefaultFilesystem()->MoveFileI(old_path, new_path);
}

bool Filesystem::CopyFile(const Pathname &old_path, const Pathname &new_path)
{
  return EnsureDefaultFilesystem()->CopyFileI(old_path, new_path);
}

bool Filesystem::IsFolder(const Pathname& pathname)
{
  return EnsureDefaultFilesystem()->IsFolderI(pathname);
}

bool Filesystem::FileExists(const Pathname& pathname)
{
  return EnsureDefaultFilesystem()->FileExistsI(pathname);
}

bool Filesystem::IsTemporaryPath(const Pathname& pathname)
{
  return EnsureDefaultFilesystem()->IsTemporaryPathI(pathname);
}

bool Filesystem::GetTemporaryFolder(Pathname &path, bool create,
			       const std::string *append)
{
  return EnsureDefaultFilesystem()->GetTemporaryFolderI(path,create, append);
}

std::string Filesystem::TempFilename(const Pathname &dir, const std::string &prefix)
{
  return EnsureDefaultFilesystem()->TempFilenameI(dir, prefix);
}

bool Filesystem::GetFileSize(const Pathname &dir, size_t *size)
{
  return EnsureDefaultFilesystem()->GetFileSizeI(dir, size);
}

Filesystem *Filesystem::EnsureDefaultFilesystem()
{
  if (!default_filesystem_)
#ifdef WIN32
    default_filesystem_ = new Win32Filesystem();
#else
    default_filesystem_ = new UnixFilesystem();
#endif
    return default_filesystem_;
}

bool CreateUniqueFile(Pathname& path, bool create_empty) {
  LOG(LS_INFO) << "Path " << path.pathname() << std::endl;
  // If not folder is supplied, use the temporary folder
  if (path.folder().empty()) {
    Pathname temporary_path;
    if (!Filesystem::GetTemporaryFolder(temporary_path, true, NULL)) {
      printf("Get temp failed\n");
      return false;
    }
    path.SetFolder(temporary_path.pathname());
  }

  // If not filename is supplied, use a temporary name
  if (path.filename().empty()) {
    std::string folder(path.folder());
    std::string filename = Filesystem::TempFilename(folder, "gt");
    path.SetFilename(filename);
    if (!create_empty) {
      Filesystem::DeleteFile(path.pathname());
    }
    return true;
  }
  
  // Otherwise, create a unique name based on the given filename
  // foo.txt -> foo-N.txt
  const std::string basename = path.basename();
  const size_t MAX_VERSION = 100;
  size_t version = 0;
  while (version < MAX_VERSION) {
    std::string pathname = path.pathname();
 
    if (!Filesystem::FileExists(pathname)) {
      if (create_empty) {
        FileStream* fs = Filesystem::OpenFile(pathname,"w");
	delete fs;
      }
      return true;
    }
    version += 1;
    char version_base[MAX_PATH];
    talk_base::sprintfn(version_base, ARRAY_SIZE(version_base), "%s-%u",
                        basename.c_str(), version);
    path.SetBasename(version_base);
  }
  return true;
}
}
