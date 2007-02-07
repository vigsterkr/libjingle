/*
 * libjingle
 * Copyright 2004--2005, Google Inc.
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

#ifdef WIN32
#include "talk/base/win32.h"
#include <shellapi.h>
#include <shlobj.h>
#include <tchar.h>
#endif  // WIN32

#include "talk/base/common.h"
#include "talk/base/pathutils.h"
#include "talk/base/stringutils.h"
#include "talk/base/urlencode.h"

namespace talk_base {

std::string const EMPTY_STR = "";

// EXT_DELIM separates a file basename from extension
const char EXT_DELIM = '.';

// FOLDER_DELIMS separate folder segments and the filename
const char* const FOLDER_DELIMS = "/\\";

// DEFAULT_FOLDER_DELIM is the preferred delimiter for this platform
#if WIN32
const char DEFAULT_FOLDER_DELIM = '\\';
#else  // !WIN32
const char DEFAULT_FOLDER_DELIM = '/';
#endif  // !WIN32

///////////////////////////////////////////////////////////////////////////////
// Pathname - parsing of pathnames into components, and vice versa
///////////////////////////////////////////////////////////////////////////////

bool Pathname::IsFolderDelimiter(char ch) {
  return (NULL != ::strchr(FOLDER_DELIMS, ch));
}

Pathname::Pathname()
  : folder_delimiter_(DEFAULT_FOLDER_DELIM) {
}

Pathname::Pathname(const std::string& pathname)
  : folder_delimiter_(DEFAULT_FOLDER_DELIM) {
  SetPathname(pathname);
}

void Pathname::SetFolderDelimiter(char delimiter) {
  ASSERT(IsFolderDelimiter(delimiter));
  folder_delimiter_ = delimiter;
}

void Pathname::Normalize() {
  for (size_t i=0; i<folder_.length(); ++i) {
    if (IsFolderDelimiter(folder_[i])) {
      folder_[i] = folder_delimiter_;
    }
  }
}

void Pathname::clear() {
  folder_.clear();
  basename_.clear();
  extension_.clear();
}

std::string Pathname::pathname() const {
  std::string pathname(folder_);
  pathname.append(basename_);
  pathname.append(extension_);
  return pathname;
}

std::string Pathname::url() const {
  std::string s = "file://";
  for (size_t i=0; i<folder_.length(); ++i) {
    if (i == 1 && folder_[i] == ':') // drive letter
      s += '|';
    else if (IsFolderDelimiter(folder_[i]))
      s += '/';
    else
      s += folder_[i];
  }
  s += basename_;
  s += extension_;
  return UrlEncodeString(s);
}

void Pathname::SetPathname(const std::string &pathname) {
  std::string::size_type pos = pathname.find_last_of(FOLDER_DELIMS);
  if (pos != std::string::npos) {
    SetFolder(pathname.substr(0, pos + 1));
    SetFilename(pathname.substr(pos + 1));
  } else {
    SetFolder(EMPTY_STR);
    SetFilename(pathname);
  }
}

void Pathname::AppendPathname(const Pathname& pathname) {
  std::string full_pathname(folder_);
  full_pathname.append(pathname.pathname());
  SetPathname(full_pathname);
}

std::string Pathname::folder() const {
  return folder_;
}

std::string Pathname::folder_name() const {
  std::string::size_type pos = std::string::npos;
  if (folder_.size() >= 2) {
    pos = folder_.find_last_of(FOLDER_DELIMS, folder_.length() - 2);
  }
  if (pos != std::string::npos) {
    return folder_.substr(pos + 1);
  } else {
    return folder_;
  }
}

std::string Pathname::parent_folder() const {
  std::string::size_type pos = std::string::npos;
  if (folder_.size() >= 2) {
    pos = folder_.find_last_of(FOLDER_DELIMS, folder_.length() - 2);
  }
  if (pos != std::string::npos) {
    return folder_.substr(0, pos + 1);
  } else {
    return EMPTY_STR;
  }
}

void Pathname::SetFolder(const std::string& folder) {
  folder_.assign(folder);
  // Ensure folder ends in a path delimiter
  if (!folder_.empty() && !IsFolderDelimiter(folder_[folder_.length()-1])) {
    folder_.push_back(folder_delimiter_);
  }
}

void Pathname::AppendFolder(const std::string& folder) {
  folder_.append(folder);
  // Ensure folder ends in a path delimiter
  if (!folder_.empty() && !IsFolderDelimiter(folder_[folder_.length()-1])) {
    folder_.push_back(folder_delimiter_);
  }
}

std::string Pathname::basename() const {
  return basename_;
}

void Pathname::SetBasename(const std::string& basename) {
  ASSERT(basename.find_first_of(FOLDER_DELIMS) == std::string::npos);
  basename_.assign(basename);
}

std::string Pathname::extension() const {
  return extension_;
}

void Pathname::SetExtension(const std::string& extension) {
  ASSERT(extension.find_first_of(FOLDER_DELIMS) == std::string::npos);
  ASSERT(extension.find_first_of(EXT_DELIM, 1) == std::string::npos);
  extension_.assign(extension);
  // Ensure extension begins with the extension delimiter
  if (!extension_.empty() && (extension_[0] != EXT_DELIM)) {
    extension_.insert(extension_.begin(), EXT_DELIM);
  }
} 

std::string Pathname::filename() const {
  std::string filename(basename_);
  filename.append(extension_);
  return filename;
}

void Pathname::SetFilename(const std::string& filename) {
  std::string::size_type pos = filename.rfind(EXT_DELIM);
  if ((pos == std::string::npos) || (pos == 0)) {
    SetBasename(filename);
    SetExtension(EMPTY_STR);
  } else {
    SetBasename(filename.substr(0, pos));
    SetExtension(filename.substr(pos));
  }
}

///////////////////////////////////////////////////////////////////////////////
// CreateUniqueFile
///////////////////////////////////////////////////////////////////////////////

std::string g_organization_name;
std::string g_application_name;

void SetOrganizationName(const std::string& organization) {
  g_organization_name = organization;
}

void SetApplicationName(const std::string& application) {
  g_application_name = application;
}

bool CreateFolder(const talk_base::Pathname& path) {
#ifdef WIN32
  if (!path.filename().empty())
    return false;

  std::wstring pathname16 = ToUtf16(path.pathname());
  if (!pathname16.empty() && (pathname16[0] != '\\')) {
    pathname16 = L"\\\\?\\" + pathname16;
  }

  DWORD res = ::GetFileAttributes(pathname16.c_str());
  if (res != INVALID_FILE_ATTRIBUTES) {
    // Something exists at this location, check if it is a directory
    return ((res & FILE_ATTRIBUTE_DIRECTORY) != 0);
  } else if ((GetLastError() != ERROR_FILE_NOT_FOUND)
              && (GetLastError() != ERROR_PATH_NOT_FOUND)) {
    // Unexpected error
    return false;
  }

  // Directory doesn't exist, look up one directory level
  if (!path.parent_folder().empty()) {
    talk_base::Pathname parent(path);
    parent.SetFolder(path.parent_folder());
    if (!CreateFolder(parent)) {
      return false;
    }
  }

  return (::CreateDirectory(pathname16.c_str(), NULL) != 0);
#else  // !WIN32
  return false;
#endif  // !WIN32
}

bool FinishPath(talk_base::Pathname& path, bool create,
                const std::string& append) {
  if (!append.empty()) {
    path.AppendFolder(append);
  }
  if (create && !CreateFolder(path))
    return false;
  return true;
}

bool GetTemporaryFolder(talk_base::Pathname& path, bool create,
                        const std::string& append) {
  ASSERT(!g_application_name.empty());
#ifdef WIN32
  TCHAR buffer[MAX_PATH + 1];
  if (!::GetTempPath(ARRAY_SIZE(buffer), buffer))
    return false;
  if (!::GetLongPathName(buffer, buffer, ARRAY_SIZE(buffer)))
    return false;
  size_t len = strlen(buffer);
  if ((len > 0) && (buffer[len-1] != __T('\\'))) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              __T("\\"));
  }
  len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                            ToUtf16(g_application_name).c_str());
  if ((len > 0) && (buffer[len-1] != __T('\\'))) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              __T("\\"));
  }
  if (len >= ARRAY_SIZE(buffer) - 1)
    return false;
  path.clear();
  path.SetFolder(ToUtf8(buffer));
  return FinishPath(path, create, append);
#else  // !WIN32
  return false;
#endif  // !WIN32
}

bool GetAppDataFolder(talk_base::Pathname& path, bool create,
                      const std::string& append) {
  ASSERT(!g_organization_name.empty());
  ASSERT(!g_application_name.empty());
#ifdef WIN32
  TCHAR buffer[MAX_PATH + 1];
  if (!::SHGetSpecialFolderPath(NULL, buffer, CSIDL_LOCAL_APPDATA, TRUE))
    return false;
  if (!::GetLongPathName(buffer, buffer, ARRAY_SIZE(buffer)))
    return false;
  size_t len = talk_base::strcatn(buffer, ARRAY_SIZE(buffer), _T("\\"));
  len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                            ToUtf16(g_organization_name).c_str());
  if ((len > 0) && (buffer[len-1] != __T('\\'))) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              __T("\\"));
  }
  len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                            ToUtf16(g_application_name).c_str());
  if ((len > 0) && (buffer[len-1] != __T('\\'))) {
    len += talk_base::strcpyn(buffer + len, ARRAY_SIZE(buffer) - len,
                              __T("\\"));
  }
  if (len >= ARRAY_SIZE(buffer) - 1)
    return false;
  path.clear();
  path.SetFolder(ToUtf8(buffer));
  return FinishPath(path, create, append);
#else  // !WIN32
  return false;
#endif  // !WIN32
}

bool CleanupTemporaryFolder() {
#ifdef WIN32
  talk_base::Pathname temp_path;
  if (!GetTemporaryFolder(temp_path, false, ""))
    return false;

  std::wstring temp_path16 = ToUtf16(temp_path.pathname());
  temp_path16.append(1, '*');
  temp_path16.append(1, '\0');

  SHFILEOPSTRUCT file_op = { 0 };
  file_op.wFunc = FO_DELETE;
  file_op.pFrom = temp_path16.c_str();
  file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
  return (0 == SHFileOperation(&file_op));
#else  // !WIN32
  return false;
#endif  // !WIN32
}
#if 0
bool CreateUnijqueFile(talk_base::Pathname& path, bool create_empty) {
#ifdef WIN32
  // If not folder is supplied, use the temporary folder
  if (path.folder().empty()) {
    talk_base::Pathname temp_path;
    if (!GetTemporaryFolder(temp_path, true, "")) {
    
      return false;
    }
    path.SetFolder(temp_path.folder());
  }
  printf("path: %s\n", path.pathname());
  // If not filename is supplied, use a temporary name
  if (path.filename().empty()) {
    TCHAR filename[MAX_PATH];
    std::wstring folder((ToUtf16)(path.folder()));
    if (!::GetTempFileName(folder.c_str(), __T("gt"), 0, filename))
      return false;
    ASSERT(wcsncmp(folder.c_str(), filename, folder.length()) == 0);
    path.SetFilename(ToUtf8(filename + folder.length()));
    if (!create_empty) {
      VERIFY(::DeleteFile(ToUtf16(path.pathname()).c_str()) != FALSE);
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
    std::wstring pathname16 = ToUtf16(pathname).c_str();

    if (pathname16[0] != __T('\\'))
      pathname16 = __T("\\\\?\\") + pathname16;

    HANDLE hfile = CreateFile(pathname16.c_str(), GENERIC_WRITE, 0,
                              NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile != INVALID_HANDLE_VALUE) {
      CloseHandle(hfile);
      if (!create_empty) {
        VERIFY(::DeleteFile(pathname16.c_str()) != FALSE);
      }
      return true;
    } else {
      int err = GetLastError();
      if (err != ERROR_FILE_EXISTS && err != ERROR_ACCESS_DENIED) {
        return false;
      }
    }

    version += 1;
    char version_base[MAX_PATH];
    talk_base::sprintfn(version_base, ARRAY_SIZE(version_base), "%s-%u",
                        basename.c_str(), version);
    path.SetBasename(version_base);
  }
  return false;
#else  // !WIN32
  // TODO: Make this better.
  path.SetBasename("/tmp/temp-1");
#endif  // !WIN32
}
#endif
///////////////////////////////////////////////////////////////////////////////

} // namespace talk_base
