#ifndef TALK_APP_WIN32_TARSTREAM_H__
#define TALK_APP_WIN32_TARSTREAM_H__

#include <string>
#include <vector>
#include "talk/base/fileutils.h"
#include "talk/base/sigslot.h"
#include "talk/base/stream.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// TarStream - acts as a source or sink for a tar-encoded collection of files
// and directories.  Operates synchronously.
///////////////////////////////////////////////////////////////////////////////

class TarStream : public StreamInterface {
 public:
  TarStream();
  virtual ~TarStream();

  // AddFilter is used to limit the elements which will be read or written.
  // In general, all members of the parent folder are read, and all members
  // of a tarfile are written.  However, if any filters are added, only those
  // items (and their contents, in the case of folders) are processed.  Filters
  // must be added before opening the stream.
  bool AddFilter(const std::string& pathname);

  // 'folder' is parent of the tar contents.  All paths will be evaluated
  // relative to it.  When 'read' is true, the specified folder will be 
  // traversed, and a tar stream will be generated (via Read).  Otherwise, a
  // tar stream is consumed (via Write), and files and folders will be created.
  bool Open(const std::string& folder, bool read);

  virtual talk_base::StreamState GetState() const;
  virtual talk_base::StreamResult Read(void* buffer, size_t buffer_len,
                                       size_t* read, int* error);
  virtual talk_base::StreamResult Write(const void* data, size_t data_len,
                                        size_t* written, int* error);
  virtual void Close();

  virtual bool GetSize(size_t* size) const { return false; }
  virtual bool ReserveSize(size_t size) { return true; }
  virtual bool Rewind() { return false; }

  // Every time a new entry header is read/written, this signal is fired with
  // the entry's name and size.
  sigslot::signal2<const std::string&, size_t> SignalNextEntry;

 private:
  typedef std::list<DirectoryIterator*> DirectoryList;
  enum ModeType { M_NONE, M_READ, M_WRITE };
  enum NextBlockType { NB_NONE, NB_FILE_HEADER, NB_DATA, NB_TRAILER };
  enum { BLOCK_SIZE = 512 };

  talk_base::StreamResult ProcessBuffer(void* buffer, size_t buffer_len,
                                        size_t* consumed, int* error);
  talk_base::StreamResult ProcessNextBlock(int* error);
  talk_base::StreamResult ProcessEmptyBlock(size_t start, int* error);
  talk_base::StreamResult ReadNextFile(int* error);
  talk_base::StreamResult WriteNextFile(int* error);

  talk_base::StreamResult ProcessNextEntry(const DirectoryIterator *data, 
                                           int *error);

  // Determine whether the given entry is allowed by our filters
  bool CheckFilter(const std::string& pathname);

  void WriteFieldN(size_t& pos, size_t max_len, size_t numeric_field);
  void WriteFieldS(size_t& pos, size_t max_len, const char* string_field);
  void WriteFieldF(size_t& pos, size_t max_len, const char* format, ...);

  void ReadFieldN(size_t& pos, size_t max_len, size_t* numeric_field);
  void ReadFieldS(size_t& pos, size_t max_len, std::string* string_field);
  
  void WriteChecksum(void);

  // Files and/or folders that should be processed
  std::vector<std::string> filters_;
  // Folder passed to Open
  std::string root_folder_;
  // Open for read or write?
  ModeType mode_;
  // The expected type of the next block
  NextBlockType next_block_;
  // The partial contents of the current block
  char block_[BLOCK_SIZE];
  size_t block_pos_;
  // The file which is currently being read or written
  talk_base::FileStream* current_;
  // Bytes remaining to be processed for current_
  size_t current_bytes_;
  // Note: the following variables are used in M_READ mode only.
  // Stack of open directory handles, representing depth-first search
  DirectoryList find_;
  // Subfolder path corresponding to current position in the directory tree
  std::string subfolder_;
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif  // TALK_APP_WIN32_TARSTREAM_H__
