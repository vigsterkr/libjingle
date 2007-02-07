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

#ifndef TALK_BASE_FILEUTILS_H__
#define TALK_BASE_FILEUTILS_H__

#include <string>

#ifdef _WINDOWS
#include <windows.h>
#else
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "talk/base/common.h"

namespace talk_base {

class FileStream;
class Pathname;

//////////////////////////
// Directory Iterator   //
//////////////////////////

// A DirectoryTraverser is created with a given directory. It originally points to
// the first file in the directory, and can be advanecd with Next(). This allows you
// to get information about each file.

class DirectoryIterator {

 public:
  // Constructor 
  DirectoryIterator();

  // Destructor
  ~DirectoryIterator();

  // Starts traversing a directory
  // dir is the directory to traverse
  // returns true if the directory exists and is valid
  // The iterator will point to the first entry in the directory
  bool Iterate(const Pathname &path);

  // Advances to the next file
  // returns true if there were more files in the directory.
  bool Next();

  // returns true if the file currently pointed to is a directory
  bool IsDirectory() const;

  // returns the name of the file currently pointed to
  std::string Name() const;

  // returns the size of the file currently pointed to
  size_t FileSize() const;

  // returns the last modified time of the file currently poitned to
  time_t FileModifyTime() const;

 private:
  std::string directory_;
#ifdef _WINDOWS
  WIN32_FIND_DATA data_;
  HANDLE handle_;
#else
  DIR *dir_;
  struct dirent *dirent_;
  struct stat stat_;
#endif
};

class Filesystem {
 public:
 
   virtual bool CreateFolderI(const Pathname &pathname) = 0;
	 
  // Opens a file. Returns an open StreamInterface if function succeeds. Otherwise,
  // returns NULL.
  virtual FileStream *OpenFileI(const Pathname &filename, 
			    const std::string &mode) = 0;

  // This will attempt to delete the path located at filename. If filename is a file,
  // it will be unlinked. If the path is a directory, it will recursively unlink and remove
  // all the files and directory within it
  virtual bool DeleteFileI(const Pathname &filename) = 0;

  // Creates a directory. This will call itself recursively to create /foo/bar even if
  // /foo does not exist.
  // Returns TRUE if function succeeds
  
  // This moves a file from old_path to new_path, where "file" can be a plain file
  // or directory, which will be moved recursively.
  // Returns true if function succeeds.
  virtual bool MoveFileI(const Pathname &old_path, const Pathname &new_path) = 0;

  // This copies a file from old_path to _new_path where "file" can be a plain file
  // or directory, which will be copied recursively.
  // Returns true if function succeeds
  virtual bool CopyFileI(const Pathname &old_path, const Pathname &new_path) = 0;

  // Returns true if a pathname is a directory
  virtual bool IsFolderI(const Pathname& pathname) = 0;

  // Returns true if a file exists at this path
  virtual bool FileExistsI(const Pathname& pathname) = 0;

  // Returns true if pathname represents a temporary location on the system.
  virtual bool IsTemporaryPathI(const Pathname& pathname) = 0;

  // A folder appropriate for storing temporary files (Contents are
  // automatically deleted when the program exists)
  virtual bool GetTemporaryFolderI(Pathname &path, bool create,
                                 const std::string *append) = 0;
  
  virtual std::string TempFilenameI(const Pathname &dir, const std::string &prefix) = 0;

  virtual bool GetFileSizeI(const Pathname &dir, size_t *size) = 0;
  
  static Filesystem *default_filesystem(void) { ASSERT(default_filesystem_!=NULL); return default_filesystem_; }
  static void set_default_filesystem(Filesystem *filesystem) {default_filesystem_ = filesystem; }
  
  
  static bool CreateFolder(const Pathname &pathname);
  
  static FileStream *OpenFile(const Pathname &filename, 
			    const std::string &mode);
  static bool DeleteFile(const Pathname &filename);
  static bool MoveFile(const Pathname &old_path, const Pathname &new_path);
  static bool CopyFile(const Pathname &old_path, const Pathname &new_path);
  static bool IsFolder(const Pathname& pathname);
  static bool FileExists(const Pathname &pathname);
  static bool IsTemporaryPath(const Pathname& pathname);
  static bool GetTemporaryFolder(Pathname &path, bool create,
                                 const std::string *append);
  static std::string TempFilename(const Pathname &dir, const std::string &prefix);
  static bool GetFileSize(const Pathname &dir, size_t *size);
  
 private:
  static Filesystem *default_filesystem_;
  static Filesystem *EnsureDefaultFilesystem();

};

// Generates a unique temporary filename in 'directory' with the given 'prefix'
 std::string TempFilename(const Pathname &dir, const std::string &prefix);

  // Generates a unique filename based on the input path.  If no path component
  // is specified, it uses the temporary directory.  If a filename is provided,
  // up to 100 variations of form basename-N.extension are tried.  When
  // create_empty is true, an empty file of this name is created (which
  // decreases the chance of a temporary filename collision with another
  // process).
 bool CreateUniqueFile(talk_base::Pathname& path, bool create_empty);

}

#endif   // TALK_BASE_FILEUTILS_H__
