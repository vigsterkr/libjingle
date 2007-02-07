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
#include "talk/base/convert.h"
#include "talk/base/pathutils.h"
#include "talk/base/fileutils.h"
#include "talk/base/stringutils.h"
#include "talk/base/stream.h"

#include "talk/base/win32filesystem.h"

namespace talk_base {

bool Win32Filesystem::CreateFolderI(const Pathname &pathname) {
  int len = pathname.pathname().length();

  if ((len <= 0) || (pathname.pathname().c_str()[len-1] != '\\')) {
    return false;
  }

  DWORD res = ::GetFileAttributes(Utf16(pathname.pathname()).AsWz());
  if (res != INVALID_FILE_ATTRIBUTES) {
    // Something exists at this location, check if it is a directory
    return ((res & FILE_ATTRIBUTE_DIRECTORY) != 0);
  } else if ((GetLastError() != ERROR_FILE_NOT_FOUND)
              && (GetLastError() != ERROR_PATH_NOT_FOUND)) {
    // Unexpected error
    return false;
  }
  // Directory doesn't exist, look up one directory level
  do {
    --len;
  } while ((len > 0) && (pathname.pathname().c_str()[len-1] != '\\'));

  if (!CreateFolder(std::string(pathname.pathname().c_str(),len)))
    return false;

  if (pathname.pathname().c_str()[0] != '\\') {
	  std::string long_path = std::string("\\\\?\\") + pathname.pathname();
	  return (::CreateDirectory(Utf16(long_path).AsWz(), NULL) != 0);
  } else {
    return (::CreateDirectory(Utf16(pathname.pathname()).AsWz(), NULL) != 0);
  }
}

FileStream *Win32Filesystem::OpenFileI(const Pathname &filename, 
			    const std::string &mode) {
  talk_base::FileStream *fs = new talk_base::FileStream();
  if (fs)
    fs->Open(filename.pathname().c_str(), mode.c_str());
  return fs;
}

bool Win32Filesystem::DeleteFileI(const Pathname &filename) {
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
     return ::RemoveDirectory(Utf16(no_slash).AsWz()) == 0;
   } 
   return ::DeleteFile(Utf16(filename.pathname()).AsWz()) == 0;
}

bool Win32Filesystem::GetTemporaryFolderI(Pathname &pathname, bool create,
				    const std::string *append) {
 ASSERT(!g_application_name_.empty());
  wchar_t buffer[MAX_PATH + 1];
  if (!::GetTempPath(ARRAY_SIZE(buffer), buffer))
    return false;
  if (!::GetLongPathName(buffer, buffer, ARRAY_SIZE(buffer)))
    return false;
  size_t len = strlen(buffer);
  if ((len > 0) && (buffer[len-1] != '\\')) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              L"\\");
  }
  if ((len > 0) && (buffer[len-1] != '\\')) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              L"\\");
  }
  if (len >= ARRAY_SIZE(buffer) - 1)
    return false;
  pathname.clear();
  pathname.SetFolder(Utf8(buffer).AsSz());
  if (append != NULL)
    pathname.AppendFolder(*append);
  if (create)
    CreateFolderI(pathname);
  return true;
}

std::string Win32Filesystem::TempFilenameI(const Pathname &dir, const std::string &prefix) {
	wchar_t filename[MAX_PATH];
	if (::GetTempFileName(Utf16(dir.pathname()).AsWz(), Utf16(prefix).AsWz(), 0, filename) == 0)
		return Utf8(filename).AsString();
	return "";
}

bool Win32Filesystem::MoveFileI(const Pathname &old_path, const Pathname &new_path) 
{
  LOG(LS_INFO) << "Moving " << old_path.pathname() << " to " << new_path.pathname();
  if (_wrename(Utf16(old_path.pathname()).AsWz(), Utf16(new_path.pathname()).AsWz()) != 0) {
    if (errno != EXDEV) {
      printf("errno: %d\n", errno);
      return false;
    }
    if (!CopyFile(old_path, new_path))
      return false;
    if (!DeleteFile(old_path))
      return false;
  }
  return true;
}

bool Win32Filesystem::IsFolderI(const Pathname &path)
{
  WIN32_FILE_ATTRIBUTE_DATA data = {0};
  if (0 == ::GetFileAttributesEx(Utf16(path.pathname()).AsWz(), GetFileExInfoStandard, &data))
    return false;
  return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
}

bool Win32Filesystem::FileExistsI(const Pathname &path)
{
  DWORD res = ::GetFileAttributes(Utf16(path.pathname()).AsWz());
  return res != INVALID_FILE_ATTRIBUTES;
}

bool Win32Filesystem::CopyFileI(const Pathname &old_path, const Pathname &new_path) 
{
  return ::CopyFile(Utf16(old_path.pathname()).AsWz(), Utf16(new_path.pathname()).AsWz(), TRUE) == 0;
}

bool Win32Filesystem::IsTemporaryPathI(const Pathname& pathname)
{
  TCHAR buffer[MAX_PATH + 1];
  if (!::GetTempPath(ARRAY_SIZE(buffer), buffer))
    return false;
  if (!::GetLongPathName(buffer, buffer, ARRAY_SIZE(buffer)))
    return false;
  return (::strnicmp(Utf16(pathname.pathname()).AsWz(), buffer, strlen(buffer)) == 0);
}

bool Win32Filesystem::GetFileSizeI(const Pathname &pathname, size_t *size)
{
  WIN32_FILE_ATTRIBUTE_DATA data = {0};
  if (::GetFileAttributesEx(Utf16(pathname.pathname()).AsWz(), GetFileExInfoStandard, &data) == 0)
	  return false;
  *size = data.nFileSizeLow;
  return true;
}

}
