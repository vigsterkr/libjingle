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

#include "talk/base/basicdefs.h"
#include "talk/base/pathutils.h"
#include "talk/base/fileutils.h"
#include "talk/base/stringutils.h"
#include "talk/base/stream.h"

#include "talk/base/unixfilesystem.h"

namespace talk_base {

bool UnixFilesystem::CreateFolderI(const Pathname &path) {
  LOG(LS_INFO) << "Creating folder: " << path.pathname();
  int len = path.pathname().length();
  const char *pathname = path.pathname().c_str();
  if ((len <= 0) || (pathname[len-1] != '/'))
    return false;
  struct stat st;
  int res = ::stat(pathname, &st);
  if (res == 0) {
    // Something exists at this location, check if it is a directory
    return S_ISDIR(st.st_mode) != 0;
  } else if (errno != ENOENT) {
    // Unexpected error
    return false;
  }
  // Directory doesn't exist, look up one directory level
  do {
    --len;
  } while ((len > 0) && (pathname[len-1] !='/'));

  char *newstring = new char[len+1];
  strncpy(newstring, pathname, len);
  newstring[len] = '\0';

  if (!CreateFolder(Pathname(newstring))) {
    delete[] newstring;
    return false;
  }
  delete[] newstring;
  std::string no_slash(path.pathname(), 0, path.pathname().length()-1);
 
  return (::mkdir(no_slash.c_str(), 0755) == 0);
  }

FileStream *UnixFilesystem::OpenFileI(const Pathname &filename, 
			    const std::string &mode) {
  talk_base::FileStream *fs = new talk_base::FileStream();
  if (fs)
    fs->Open(filename.pathname().c_str(), mode.c_str());
  return fs;
}

bool UnixFilesystem::DeleteFileI(const Pathname &filename) {
  LOG(LS_INFO) << "Deleting " << filename.pathname();

   if (IsFolder(filename)) {
     Pathname dir;
     dir.SetFolder(filename.pathname());
     DirectoryIterator di;
     di.Iterate(dir.pathname());
     while(di.Next()) {
       if (di.Name() == "." || di.Name() == "..")
	 continue;
       Pathname subdir;
       subdir.SetFolder(filename.pathname());
       subdir.SetFilename(di.Name());
      
       if (!DeleteFile(subdir.pathname()))
	 return false;
     }
     std::string no_slash(filename.pathname(), 0, filename.pathname().length()-1);
     return ::rmdir(no_slash.c_str()) == 0;
   } 
     return ::unlink(filename.pathname().c_str()) == 0;
}

bool UnixFilesystem::GetTemporaryFolderI(Pathname &pathname, bool create,
				    const std::string *append) {
  pathname.SetPathname("/tmp");
  if (append) {
    pathname.AppendFolder(*append);
    if (create)
      CreateFolder(pathname);
  }
}

std::string UnixFilesystem::TempFilenameI(const Pathname &dir, const std::string &prefix) {
  int len = dir.pathname().size() + prefix.size() + 2 + 6;
  char *tempname = new char[len];
  
  snprintf(tempname, len, "%s/%sXXXXXX", dir.pathname().c_str(), prefix.c_str());
  int fd = ::mkstemp(tempname);
  if (fd != -1)
    ::close(fd);
  std::string ret(tempname);
  delete[] tempname;
  
  return ret;
}

bool UnixFilesystem::MoveFileI(const Pathname &old_path, const Pathname &new_path) 
{
  LOG(LS_INFO) << "Moving " << old_path.pathname() << " to " << new_path.pathname();
  if (rename(old_path.pathname().c_str(), new_path.pathname().c_str()) != 0) {
    if (errno != EXDEV)
      return false;
    if (!CopyFile(old_path, new_path))
      return false;
    if (!DeleteFile(old_path))
      return false;
  }
  return true;
}

bool UnixFilesystem::IsFolderI(const Pathname &path)
{
  struct stat st;
  if (stat(path.pathname().c_str(), &st) < 0)
    return false;

  return S_ISDIR(st.st_mode);
}

bool UnixFilesystem::CopyFileI(const Pathname &old_path, const Pathname &new_path) 
{
  LOG(LS_INFO) << "Copying " << old_path.pathname() << " to " << new_path.pathname();
  char buf[256];
  size_t len;
  if (IsFolder(old_path)) {
    Pathname new_dir;
    new_dir.SetFolder(new_path.pathname());
    Pathname old_dir;
    old_dir.SetFolder(old_path.pathname());
  
    if (!CreateFolder(new_dir))
      return false;
    DirectoryIterator di;
    di.Iterate(old_dir.pathname());
    while(di.Next()) {
      if (di.Name() == "." || di.Name() == "..")
	continue;
      Pathname source;
      Pathname dest;
      source.SetFolder(old_dir.pathname());
      dest.SetFolder(new_path.pathname());
      source.SetFilename(di.Name());
      dest.SetFilename(di.Name());
      
      if (!CopyFile(source, dest))
	return false;
    }
    return true;
  }

  StreamInterface *source = OpenFile(old_path, "rb");
  if (!source)
    return false;

  StreamInterface *dest = OpenFile(new_path, "wb");
  if (!dest) {
    delete source;
    return false;
  }
    
  while (source->Read(buf, sizeof(buf), &len, NULL) == talk_base::SR_SUCCESS)
    dest->Write(buf, len, NULL, NULL);

  delete source;
  delete dest;
  return true;
}

bool UnixFilesystem::IsTemporaryPathI(const Pathname& pathname)
{
  return (!strncmp(pathname.pathname().c_str(), "/tmp/", strlen("/tmp/")));
}

bool UnixFilesystem::FileExistsI(const Pathname& pathname)
{
   struct stat st;
   int res = ::stat(pathname.pathname().c_str(), &st);
   return res == 0;
}

bool UnixFilesystem::GetFileSizeI(const Pathname& pathname, size_t *size)
{
  struct stat st;
  if (::stat(pathname.pathname().c_str(), &st) != 0)
    return false;
  *size = st.st_size;
  return true;
}

}
