#include "talk/base/basicdefs.h"
#include "talk/base/basictypes.h"
#include "talk/base/tarstream.h"
#include "talk/base/pathutils.h"
#include "talk/base/stringutils.h"
#include "talk/base/common.h"

using namespace talk_base;

///////////////////////////////////////////////////////////////////////////////
// TarStream
///////////////////////////////////////////////////////////////////////////////

TarStream::TarStream() : mode_(M_NONE), next_block_(NB_NONE), block_pos_(0),
                         current_(NULL), current_bytes_(0) {
}

TarStream::~TarStream() {
  Close();
}

bool TarStream::AddFilter(const std::string& pathname) {
  if (pathname.empty())
    return false;
  Pathname archive_path(pathname);
  archive_path.SetFolderDelimiter('/');
  archive_path.Normalize();
  filters_.push_back(archive_path.pathname());
  return true;
}

bool TarStream::Open(const std::string& folder, bool read) {
  Close();

  Pathname root_folder;
  root_folder.SetFolder(folder);
  root_folder.Normalize();
  root_folder_.assign(root_folder.folder());

  if (read) {
    std::string pattern(root_folder_);
    DirectoryIterator *iter = new DirectoryIterator();

    if (iter->Iterate(pattern) == false) {
      delete iter;
      return false;
    }
    mode_ = M_READ;
    find_.push_front(iter);
    next_block_ = NB_FILE_HEADER;
    block_pos_ = BLOCK_SIZE;
    int error;
    if (SR_SUCCESS != ProcessNextEntry(find_.front(), &error)) {
      return false;
    }
  } else {
    if (!Filesystem::CreateFolder(root_folder_)) {
      return false;
    }

    mode_ = M_WRITE;
    next_block_ = NB_FILE_HEADER;
    block_pos_ = 0;
  }
  return true;
}

StreamState TarStream::GetState() const {
  return (M_NONE == mode_) ? SS_CLOSED : SS_OPEN;
}

StreamResult TarStream::Read(void* buffer, size_t buffer_len,
                             size_t* read, int* error) {
  if (M_READ != mode_) {
    return SR_EOS;
  }
  return ProcessBuffer(buffer, buffer_len, read, error);
}

StreamResult TarStream::Write(const void* data, size_t data_len,
                              size_t* written, int* error) {
  if (M_WRITE != mode_) {
    return SR_EOS;
  }
  // Note: data is not modified unless M_READ == mode_
  return ProcessBuffer(const_cast<void*>(data), data_len, written, error);
}

void TarStream::Close() {
  root_folder_.clear();
  next_block_ = NB_NONE;
  block_pos_ = 0;
  delete current_;
  current_ = NULL;
  current_bytes_ = 0;
  for (DirectoryList::iterator it = find_.begin(); it != find_.end(); ++it) {
    delete(*it);
  }
  find_.clear();
  subfolder_.clear();
}

StreamResult TarStream::ProcessBuffer(void* buffer, size_t buffer_len,
                                      size_t* consumed, int* error) {
  size_t local_consumed;
  if (!consumed) consumed = &local_consumed;
  int local_error;
  if (!error) error = &local_error;

  StreamResult result = SR_SUCCESS;
  *consumed = 0;

  while (*consumed < buffer_len) {
    size_t available = BLOCK_SIZE - block_pos_;
    if (available == 0) {
      result = ProcessNextBlock(error);
      if (SR_SUCCESS != result) {
        break;
      }
    } else {
      size_t bytes_to_copy = talk_base::_min(available, buffer_len - *consumed);
      char* buffer_ptr = static_cast<char*>(buffer) + *consumed;
      char* block_ptr = block_ + block_pos_;
      if (M_READ == mode_) {
        memcpy(buffer_ptr, block_ptr, bytes_to_copy);
      } else {
        memcpy(block_ptr, buffer_ptr, bytes_to_copy);
      }
      *consumed += bytes_to_copy;
      block_pos_ += bytes_to_copy;
    }
  }

  // SR_EOS means no data was consumed on this operation.  So we may need to
  // return SR_SUCCESS instead, and then we will return SR_EOS next time.
  if ((SR_EOS == result) && (*consumed > 0)) {
    result = SR_SUCCESS;
  }

  return result;
}

StreamResult TarStream::ProcessNextBlock(int* error) {
  ASSERT(NULL != error);
  ASSERT(M_NONE != mode_);
  ASSERT(BLOCK_SIZE == block_pos_);

  StreamResult result;
  if (NB_NONE == next_block_) {

    return SR_EOS;

  } else if (NB_TRAILER == next_block_) {

    // Trailer block is zeroed
    result = ProcessEmptyBlock(0, error);
    if (SR_SUCCESS != result)
      return result;
    next_block_ = NB_NONE;

  } else if (NB_FILE_HEADER == next_block_) {

    if (M_READ == mode_) {
      result = ReadNextFile(error);
    } else {
      result = WriteNextFile(error);
    }

    // If there are no more files, we are at the first trailer block
    if (SR_EOS == result) {
      block_pos_ = 0;
      next_block_ = NB_TRAILER;
      result = ProcessEmptyBlock(0, error);
    }
    if (SR_SUCCESS != result)
      return result;

  } else if (NB_DATA == next_block_) {

    size_t block_consumed = 0;
    size_t block_available = talk_base::_min<size_t>(BLOCK_SIZE, current_bytes_);
    while (block_consumed < block_available) {
      void* block_ptr = static_cast<char*>(block_) + block_consumed;
      size_t available = block_available - block_consumed, consumed;
      if (M_READ == mode_) {
        ASSERT(NULL != current_);
        result = current_->Read(block_ptr, available, &consumed, error);
      } else if (current_) {
        result = current_->Write(block_ptr, available, &consumed, error);
      } else {
        consumed = available;
        result = SR_SUCCESS;
      }
      switch (result) {
      case SR_ERROR:
        return result;
      case SR_BLOCK:
      case SR_EOS:
        ASSERT(false);
        *error = 0; // TODO: make errors
        return SR_ERROR;
      case SR_SUCCESS:
        block_consumed += consumed;
        break;
      }
    }

    current_bytes_ -= block_consumed;
    if (current_bytes_ == 0) {
      // The remainder of the block is zeroed
      result = ProcessEmptyBlock(block_consumed, error);
      if (SR_SUCCESS != result)
        return result;
      delete current_;
      current_ = NULL;
      next_block_ = NB_FILE_HEADER;
    }

  } else {
    ASSERT(false);
  }

  block_pos_ = 0;
  return SR_SUCCESS;
}

StreamResult TarStream::ProcessEmptyBlock(size_t start, int* error) {
  ASSERT(NULL != error);
  ASSERT(M_NONE != mode_);
  if (M_READ == mode_) {
    memset(block_ + start, 0, BLOCK_SIZE - start);
  } else {
    if (!talk_base::memory_check(block_ + start, 0, BLOCK_SIZE - start)) {
      *error = 0; // TODO: make errors
      return SR_ERROR;
    }
  }
  return SR_SUCCESS;
}

StreamResult TarStream::ReadNextFile(int* error) {
  ASSERT(NULL != error);
  ASSERT(M_READ == mode_);
  ASSERT(NB_FILE_HEADER == next_block_);
  ASSERT(BLOCK_SIZE == block_pos_);
  ASSERT(NULL == current_);

  // ReadNextFile conducts a depth-first recursive search through the directory
  // tree.  find_ maintains a stack of open directory handles, which
  // corresponds to our current position in the tree.  At any point, the
  // directory at the top (front) of the stack is being enumerated.  If a
  // directory is found, it is opened and pushed onto the top of the stack.
  // When a directory enumeration completes, that directory is popped off the
  // top of the stack.

  // Note: Since ReadNextFile can only return one block of data at a time, we
  // cannot simultaneously return the entry for a directory, and the entry for
  // the first element in that directory at the same time.  In this case, we
  // push a NULL entry onto the find_ stack, which indicates that the next
  // iteration should begin enumeration of the "new" directory.
  StreamResult result = SR_SUCCESS;
  while (BLOCK_SIZE == block_pos_) {
    ASSERT(!find_.empty());

    if (NULL != find_.front()) {
      if (find_.front()->Next()) {
	result = ProcessNextEntry(find_.front(), error);
        if (SR_SUCCESS != result) {
          return result;
        }
        continue;
      }
      delete(find_.front());
    } else {
      Pathname pattern(root_folder_);
      pattern.AppendFolder(subfolder_);
      find_.front() = new DirectoryIterator();
      if (find_.front()->Iterate(pattern.pathname())) {
        result = ProcessNextEntry(find_.front(), error);
        if (SR_SUCCESS != result) {
          return result;
        }
        continue;
      }
      // TODO: should this be an error?
      LOG_F(LS_WARNING) << "Couldn't open folder: " << pattern.pathname();
    }

    find_.pop_front();
    subfolder_ = Pathname(subfolder_).parent_folder();

    if (find_.empty()) {
      return SR_EOS;
    }
  }

  ASSERT(0 == block_pos_);
  return SR_SUCCESS;
}

StreamResult TarStream::WriteNextFile(int* error) {
  ASSERT(NULL != error);
  ASSERT(M_WRITE == mode_);
  ASSERT(NB_FILE_HEADER == next_block_);
  ASSERT(BLOCK_SIZE == block_pos_);
  ASSERT(NULL == current_);
  ASSERT(0 == current_bytes_);

  std::string pathname, link, linked_name, magic, mversion;
  size_t file_size, modify_time, unused, checksum;

  size_t block_data = 0;
  ReadFieldS(block_data, 100, &pathname);
  ReadFieldN(block_data, 8,   &unused);  // mode
  ReadFieldN(block_data, 8,   &unused);  // owner uid
  ReadFieldN(block_data, 8,   &unused);  // owner gid
  ReadFieldN(block_data, 12,  &file_size);
  ReadFieldN(block_data, 12,  &modify_time);
  ReadFieldN(block_data, 8,   &checksum);
  if (checksum == 0) 
    block_data -= 8; // back-compatiblity 
  ReadFieldS(block_data, 1,   &link);
  ReadFieldS(block_data, 100, &linked_name);  // name of linked file
  ReadFieldS(block_data, 6,   &magic);
  ReadFieldS(block_data, 2,   &mversion);

  if (pathname.empty())
    return SR_EOS;

  std::string user, group, dev_major, dev_minor, prefix;
  if (magic == "ustar" || magic == "ustar ") {
    ReadFieldS(block_data, 32, &user);
    ReadFieldS(block_data, 32, &group);
    ReadFieldS(block_data, 8, &dev_major);
    ReadFieldS(block_data, 8, &dev_minor);
    ReadFieldS(block_data, 155, &prefix);

    pathname = prefix + pathname;
  }

  // Rest of the block must be empty
  StreamResult result = ProcessEmptyBlock(block_data, error);
  if (SR_SUCCESS != result) {
    return result;
  }

  Pathname archive_path(pathname);
  archive_path.SetFolderDelimiter('/');
  archive_path.Normalize();

  bool is_folder = archive_path.filename().empty();
  if (is_folder) {
    ASSERT(NB_FILE_HEADER == next_block_);
    ASSERT(0 == file_size);
  } else if (file_size > 0) {
    // We assign current_bytes_ because we must skip over the upcoming data
    // segments, regardless of whether we want to write them.
    next_block_ = NB_DATA;
    current_bytes_ = file_size;
  }

  if (!CheckFilter(archive_path.pathname())) {
    // If it's a directory, we will ignore it and all children by nature of
    // filter prefix matching.  If it is a file, we will ignore it because
    // current_ is NULL.
    return SR_SUCCESS;
  }

  // Sanity checks:
  // 1) No .. path segments
  if (archive_path.pathname().find("../") != std::string::npos) {
    LOG_F(LS_WARNING) << "Skipping path with .. entry: "
                      << archive_path.pathname();
    return SR_SUCCESS;
  }
  // 2) No drive letters
  if (archive_path.pathname().find(':') != std::string::npos) {
    LOG_F(LS_WARNING) << "Skipping path with drive letter: "
                      << archive_path.pathname();
    return SR_SUCCESS;
  }
  // 3) No absolute paths
  if (archive_path.pathname().find("//") != std::string::npos) {
    LOG_F(LS_WARNING) << "Skipping absolute path: "
                      << archive_path.pathname();
    return SR_SUCCESS;
  }

  Pathname local_path(root_folder_);
  local_path.AppendPathname(archive_path.pathname());
  local_path.Normalize();

  if (is_folder) {
    if (!Filesystem::CreateFolder(local_path)) {
      LOG_F(LS_WARNING) << "Couldn't create folder: " << local_path.pathname();
      *error = 0; // TODO
      return SR_ERROR;
    }
  } else {
    FileStream* stream = new FileStream;

    if (!stream->Open(local_path.pathname().c_str(), "wb")) {
      LOG_F(LS_WARNING) << "Couldn't create file: " << local_path.pathname();
      *error = 0; // TODO
      delete stream;
      return SR_ERROR;
    }
    if (file_size > 0) {
      current_ = stream;
    } else {
      stream->Close();
      delete stream;
    }
  }

  SignalNextEntry(archive_path.filename(), current_bytes_);

   
  return SR_SUCCESS;
}

StreamResult TarStream::ProcessNextEntry(const DirectoryIterator *data, int *error) {
  ASSERT(M_READ == mode_);
  ASSERT(NB_FILE_HEADER == next_block_);
  ASSERT(BLOCK_SIZE == block_pos_);
  ASSERT(NULL == current_);
  ASSERT(0 == current_bytes_);

  if (data->IsDirectory() &&
      (data->Name() == "." || data->Name() == ".."))
    return SR_SUCCESS;

  Pathname archive_path;
  archive_path.SetFolder(subfolder_);
  if (data->IsDirectory()) {
    archive_path.AppendFolder(data->Name());
  } else {
    archive_path.SetFilename(data->Name());
  }
  archive_path.SetFolderDelimiter('/');
  archive_path.Normalize();

  if (!CheckFilter(archive_path.pathname()))
    return SR_SUCCESS;

  if (archive_path.pathname().length() > 255) {
    // Cannot send a file name longer than 255 (yet)
    return SR_ERROR;
  }

  Pathname local_path(root_folder_);
  local_path.AppendPathname(archive_path.pathname());
  local_path.Normalize();

  if (data->IsDirectory()) {
    // Note: the NULL handle indicates that we need to open the folder next 
    // time.
    find_.push_front(NULL);
    subfolder_ = archive_path.pathname();
  } else {
    FileStream* stream = new FileStream;
    if (!stream->Open(local_path.pathname().c_str(), "rb")) {
      // TODO: Should this be an error?
      LOG_F(LS_WARNING) << "Couldn't open file: " << local_path.pathname();
      delete stream;
      return SR_SUCCESS;
    }
    current_ = stream;
    current_bytes_ = data->FileSize();
  }

  time_t modify_time = data->FileModifyTime();

  std::string pathname = archive_path.pathname();
  std::string magic, user, group, dev_major, dev_minor, prefix;  
  std::string name = pathname;
  bool ustar = false;
  if (name.length() > 100) {
    ustar = true;
    // Put last 100 characters into the name, and rest in prefix
    size_t path_length = pathname.length();
    prefix = pathname.substr(0, path_length - 100);
    name = pathname.substr(path_length - 100);
  }

  size_t block_data = 0;
  memset(block_, 0, BLOCK_SIZE);
  WriteFieldS(block_data, 100, name.c_str());
  WriteFieldS(block_data, 8,   data->IsDirectory() ? "777" : "666");   // mode
  WriteFieldS(block_data, 8,   "5");   // owner uid
  WriteFieldS(block_data, 8,   "5");   // owner gid
  WriteFieldN(block_data, 12,  current_bytes_);
  WriteFieldN(block_data, 12,  modify_time);
  WriteFieldS(block_data, 8, "        "); // Checksum. To be filled in later.
  WriteFieldS(block_data, 1,   data->IsDirectory() ? "5" : "0");  // link indicator (0 == normal file, 5 == directory)
  WriteFieldS(block_data, 100, "");   // name of linked file

  if (ustar) {
    WriteFieldS(block_data, 6,   "ustar");
    WriteFieldS(block_data, 2,   "");
    WriteFieldS(block_data, 32,  user.c_str());
    WriteFieldS(block_data, 32,  group.c_str());
    WriteFieldS(block_data, 8,   dev_major.c_str());
    WriteFieldS(block_data, 8,   dev_minor.c_str());
    WriteFieldS(block_data, 155, prefix.c_str());
  }

  // Rest of the block must be empty
  StreamResult result = ProcessEmptyBlock(block_data, error);
  WriteChecksum();

  block_pos_ = 0;
  if (current_bytes_ > 0) {
    next_block_ = data->IsDirectory() ? NB_FILE_HEADER : NB_DATA;
  }

  SignalNextEntry(archive_path.filename(), current_bytes_);

  return result;
}

void TarStream::WriteChecksum() {
  unsigned int sum = 0;

  for (int i = 0; i < BLOCK_SIZE; i++) 
    sum += static_cast<unsigned char>(block_[i]);

  sprintf(block_ + 148, "%06o", sum); 
}

bool TarStream::CheckFilter(const std::string& pathname) {
  if (filters_.empty())
    return true;

  // pathname is allowed when there is a filter which:
  // A) Equals name
  // B) Matches a folder prefix of name
  for (size_t i=0; i<filters_.size(); ++i) {
    const std::string& filter = filters_[i];
    // Make sure the filter is a prefix of name
    if (_strnicmp(pathname.c_str(), filter.data(), filter.length()) != 0)
      continue;

    // If the filter is not a directory, must match exactly
    if (!Pathname::IsFolderDelimiter(filter[filter.length()-1])
        && (filter.length() != pathname.length()))
      continue;

    return true;
  }

  return false;
}

void TarStream::WriteFieldN(size_t& pos, size_t max_len, size_t numeric_field) {
  WriteFieldF(pos, max_len, "%.*o", max_len - 1, numeric_field);
}

void TarStream::WriteFieldS(size_t& pos, size_t max_len,
                          const char* string_field) {
  ASSERT(pos + max_len <= BLOCK_SIZE);
  size_t len = strlen(string_field);
  size_t use_len = _min(len, max_len);
  memcpy(block_ + pos, string_field, use_len);
  pos += max_len;
}

void TarStream::WriteFieldF(size_t& pos, size_t max_len,
                          const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[BLOCK_SIZE];
  vsprintfn(buffer, ARRAY_SIZE(buffer), format, args);
  WriteFieldS(pos, max_len, buffer);
  va_end(args);
}

void TarStream::ReadFieldN(size_t& pos, size_t max_len, size_t* numeric_field) {
  ASSERT(NULL != numeric_field);
  std::string buffer;
  ReadFieldS(pos, max_len, &buffer);

  int value;
  if (!buffer.empty() && (1 == sscanf(buffer.c_str(), "%o", &value))) {
    *numeric_field = value;
  } else {
    *numeric_field = 0;
  }
}

void TarStream::ReadFieldS(size_t& pos, size_t max_len,
                           std::string* string_field) {
  ASSERT(NULL != string_field);
  ASSERT(pos + max_len <= BLOCK_SIZE);
  size_t value_len = talk_base::strlenn(block_ + pos, max_len);
  string_field->assign(block_ + pos, value_len);
  ASSERT(talk_base::memory_check(block_ + pos + value_len,
                                 0,
                                 max_len - value_len));
  pos += max_len;
}
