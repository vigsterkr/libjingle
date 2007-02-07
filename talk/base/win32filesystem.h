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

#ifndef _TALK_BASE_WIN32FILESYSTEM_H__
#define _TALK_BASE_WIN32FILESYSTEM_H__

#include "fileutils.h"

namespace talk_base {

class Win32Filesystem : public Filesystem{
 public:

  virtual bool CreateFolderI(const Pathname &pathname);
	 
  // Opens a file. Returns an open StreamInterface if function succeeds. Otherwise,
  // returns NULL.
  virtual FileStream *OpenFileI(const Pathname &filename, 
			    const std::string &mode);

  // This will attempt to delete the path located at filename. If filename is a file,
  // it will be unlinked. If the path is a directory, it will recursively unlink and remove
  // all the files and directory within it
  virtual bool DeleteFileI(const Pathname &filename);

  // Creates a directory. This will call itself recursively to create /foo/bar even if
  // /foo does not exist.
  // Returns TRUE if function succeeds
  
  // This moves a file from old_path to new_path, where "file" can be a plain file
  // or directory, which will be moved recursively.
  // Returns true if function succeeds.
  virtual bool MoveFileI(const Pathname &old_path, const Pathname &new_path);
  
  // This copies a file from old_path to _new_path where "file" can be a plain file
  // or directory, which will be copied recursively.
  // Returns true if function succeeds
  virtual bool CopyFileI(const Pathname &old_path, const Pathname &new_path);

  // Returns true if a pathname is a directory
  virtual bool IsFolderI(const Pathname& pathname);
  
  // Returns true if a file exists at path
  virtual bool FileExistsI(const Pathname &path);

  // Returns true if pathname represents a temporary location on the system.
  virtual bool IsTemporaryPathI(const Pathname& pathname);


  // All of the following functions set pathname and return true if successful.
  // Returned paths always include a trailing backslash.
  // If create is true, the path will be recursively created.
  // If append is non-NULL, it will be appended (and possibly created).

  virtual std::string TempFilenameI(const Pathname &dir, const std::string &prefix);

  virtual bool GetFileSizeI(const Pathname &pathname, size_t *size);
  
  // A folder appropriate for storing temporary files (Contents are
  // automatically deleted when the program exists)
  virtual bool GetTemporaryFolderI(Pathname &path, bool create,
                                 const std::string *append);
  };

}

#endif  // _WIN32FILESYSTEM_H__
