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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string>
#include "talk/base/basictypes.h"
#include "talk/base/common.h"
#include "talk/base/stream.h"
#include "talk/base/stringencode.h"

#ifdef WIN32
#include "talk/base/win32.h"
#define fileno _fileno
#endif

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////

StreamResult StreamInterface::WriteAll(const void* data, size_t data_len,
                                       size_t* written, int* error) {
  StreamResult result = SR_SUCCESS;
  size_t total_written = 0, current_written;
  while (total_written < data_len) {
    result = Write(static_cast<const char*>(data) + total_written,
                   data_len - total_written, &current_written, error);
    if (result != SR_SUCCESS)
      break;
    total_written += current_written;
  }
  if (written)
    *written = total_written;
  return result;
}

StreamResult StreamInterface::ReadAll(void* buffer, size_t buffer_len,
                                      size_t* read, int* error) {
  StreamResult result = SR_SUCCESS;
  size_t total_read = 0, current_read;
  while (total_read < buffer_len) {
    result = Read(static_cast<char*>(buffer) + total_read,
                  buffer_len - total_read, &current_read, error);
    if (result != SR_SUCCESS)
      break;
    total_read += current_read;
  }
  if (read)
    *read = total_read;
  return result;
}

StreamResult StreamInterface::ReadLine(std::string* line) {
  StreamResult result = SR_SUCCESS;
  while (true) {
    char ch;
    result = Read(&ch, sizeof(ch), NULL, NULL);
    if (result != SR_SUCCESS) {
      break;
    }
    if (ch == '\n') {
      break;
    }
    line->push_back(ch);
  }
  if (!line->empty()) {   // give back the line we've collected so far with
    result = SR_SUCCESS;  // a success code.  Otherwise return the last code
  }
  return result;
}

///////////////////////////////////////////////////////////////////////////////
// StreamTap
///////////////////////////////////////////////////////////////////////////////

StreamTap::StreamTap(StreamInterface* stream, StreamInterface* tap)
: StreamAdapterInterface(stream), tap_(NULL), tap_result_(SR_SUCCESS),
  tap_error_(0)
{
  AttachTap(tap);
}

void StreamTap::AttachTap(StreamInterface* tap) {
  tap_.reset(tap);
}

StreamInterface* StreamTap::DetachTap() { 
  return tap_.release();
}

StreamResult StreamTap::GetTapResult(int* error) {
  if (error) {
    *error = tap_error_;
  }
  return tap_result_;
}

StreamResult StreamTap::Read(void* buffer, size_t buffer_len,
                             size_t* read, int* error) {
  size_t backup_read;
  if (!read) {
    read = &backup_read;
  }
  StreamResult res = StreamAdapterInterface::Read(buffer, buffer_len,
                                                  read, error);
  if ((res == SR_SUCCESS) && (tap_result_ == SR_SUCCESS)) {
    tap_result_ = tap_->WriteAll(buffer, *read, NULL, &tap_error_);
  }
  return res;
}

StreamResult StreamTap::Write(const void* data, size_t data_len,
                              size_t* written, int* error) {
  size_t backup_written;
  if (!written) {
    written = &backup_written;
  }
  StreamResult res = StreamAdapterInterface::Write(data, data_len,
                                                   written, error);
  if ((res == SR_SUCCESS) && (tap_result_ == SR_SUCCESS)) {
    tap_result_ = tap_->WriteAll(data, *written, NULL, &tap_error_);
  }
  return res;
}

///////////////////////////////////////////////////////////////////////////////
// NullStream
///////////////////////////////////////////////////////////////////////////////

NullStream::NullStream() {
}

NullStream::~NullStream() {
}

StreamState NullStream::GetState() const {
  return SS_OPEN;
}

StreamResult NullStream::Read(void* buffer, size_t buffer_len,
                              size_t* read, int* error) {
  if (error) *error = -1;
  return SR_ERROR;
}


StreamResult NullStream::Write(const void* data, size_t data_len,
                               size_t* written, int* error) {
  if (written) *written = data_len;
  return SR_SUCCESS;
}

void NullStream::Close() {
}

bool NullStream::GetSize(size_t* size) const {
  if (size)
    *size = 0;
  return true;
}

bool NullStream::ReserveSize(size_t size) {
  return true;
}

bool NullStream::Rewind() {
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// FileStream
///////////////////////////////////////////////////////////////////////////////

FileStream::FileStream() : file_(NULL) {
}

FileStream::~FileStream() {
  FileStream::Close();
}

bool FileStream::Open(const std::string& filename, const char* mode) {
  Close();
#ifdef WIN32
  int filenamelen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(),
                                        filename.length() + 1, NULL, 0);
  int modelen     = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
  wchar_t *wfilename = new wchar_t[filenamelen+4];  // 4 for "\\?\"
  wchar_t *wfilename_dest = wfilename;
  wchar_t *wmode = new wchar_t[modelen];

  if (!filename.empty() && (filename[0] != '\\')) {
    wcscpy(wfilename, L"\\\\?\\");
    wfilename_dest = wfilename + 4;
  }

  if ((MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), filename.length() + 1,
                           wfilename_dest, filenamelen) > 0) &&
      (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, modelen) > 0)) {
    file_ = _wfopen(wfilename, wmode);
  } else {
    file_ = NULL;
  }

  delete[] wfilename;
  delete[] wmode;
#else
  file_ = fopen(filename.c_str(), mode);
#endif
  return (file_ != NULL);
}

bool FileStream::OpenShare(const std::string& filename, const char* mode,
                           int shflag) {
  Close();
#ifdef WIN32
  int filenamelen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(),
                                        filename.length() + 1, NULL, 0);
  int modelen     = MultiByteToWideChar(CP_UTF8, 0, mode, -1, NULL, 0);
  wchar_t *wfilename = new wchar_t[filenamelen+4];  // 4 for "\\?\"
  wchar_t *wfilename_dest = wfilename;
  wchar_t *wmode = new wchar_t[modelen];

  if (!filename.empty() && (filename[0] != '\\')) {
    wcscpy(wfilename, L"\\\\?\\");
    wfilename_dest = wfilename + 4;
  }

  if ((MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), filename.length() + 1,
                           wfilename_dest, filenamelen) > 0) &&
      (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, modelen) > 0)) {
    file_ = _wfsopen(wfilename, wmode, shflag);
  } else {
    file_ = NULL;
  }

  delete[] wfilename;
  delete[] wmode;
#else
  return Open(filename, mode);
#endif
  return (file_ != NULL);
}

bool FileStream::DisableBuffering() {
  if (!file_)
    return false;
  return (setvbuf(file_, NULL, _IONBF, 0) == 0);
}

StreamState FileStream::GetState() const {
  return (file_ == NULL) ? SS_CLOSED : SS_OPEN;
}

StreamResult FileStream::Read(void* buffer, size_t buffer_len,
                              size_t* read, int* error) {
  if (!file_)
    return SR_EOS;
  size_t result = fread(buffer, 1, buffer_len, file_);
  if ((result == 0) && (buffer_len > 0)) {
    if (feof(file_))
      return SR_EOS;
    if (error)
      *error = errno;
    return SR_ERROR;
  }
  if (read)
    *read = result;
  return SR_SUCCESS;
}

StreamResult FileStream::Write(const void* data, size_t data_len,
                               size_t* written, int* error) {
  if (!file_)
    return SR_EOS;
  size_t result = fwrite(data, 1, data_len, file_);
  if ((result == 0) && (data_len > 0)) {
    if (error)
      *error = errno;
    return SR_ERROR;
  }
  if (written)
    *written = result;
  return SR_SUCCESS;
}

void FileStream::Close() {
  if (file_) {
    fclose(file_);
    file_ = NULL;
  }
}

bool FileStream::SetPosition(size_t position) {
  if (!file_)
    return false;
  return (fseek(file_, position, SEEK_SET) == 0);
}

bool FileStream::GetPosition(size_t * position) const {
  ASSERT(position != NULL);
  if (!file_ || !position)
    return false;
  long result = ftell(file_);
  if (result < 0)
    return false;
  *position = result;
  return true;
}

bool FileStream::GetSize(size_t * size) const {
  ASSERT(size != NULL);
  if (!file_ || !size)
    return false;
  struct stat file_stats;
  if (fstat(fileno(file_), &file_stats) != 0)
    return false;
  *size = file_stats.st_size;
  return true;
}

bool FileStream::ReserveSize(size_t size) {
  // TODO: extend the file to the proper length
  return true;
}

bool FileStream::GetSize(const std::string& filename, size_t* size) {
  struct stat file_stats;
  if (stat(filename.c_str(), &file_stats) != 0)
    return false;
  *size = file_stats.st_size;
  return true;
}

int FileStream::Flush() {
  if (file_) {
    return fflush (file_);
  }
  // try to flush empty file?
  ASSERT(false);
  return 0;
}
///////////////////////////////////////////////////////////////////////////////


MemoryStream::MemoryStream()
  : allocated_length_(0), buffer_(NULL), data_length_(0), seek_position_(0) {
}

MemoryStream::MemoryStream(const char* data)
  : allocated_length_(0), buffer_(NULL), data_length_(0), seek_position_(0) {
  SetContents(data, strlen(data));
}

MemoryStream::MemoryStream(const char* data, size_t length)
  : allocated_length_(0), buffer_(NULL), data_length_(0), seek_position_(0) {
  SetContents(data, length);
}

MemoryStream::~MemoryStream() {
  delete [] buffer_;
}

void MemoryStream::SetContents(const char* data, size_t length) { 
  delete [] buffer_;
  data_length_ = allocated_length_ = length;
  buffer_ = new char[allocated_length_];
  memcpy(buffer_, data, data_length_);
}

StreamState MemoryStream::GetState() const {
  return SS_OPEN;
}

StreamResult MemoryStream::Read(void *buffer, size_t bytes,
    size_t *bytes_read, int *error) {
  if (seek_position_ >= data_length_) {
    // At end of stream
    if (error) {
      *error = EOF;
    }
    return SR_EOS;
  }

  size_t remaining_length = data_length_ - seek_position_;
  if (bytes > remaining_length) {
    // Read partial buffer
    bytes = remaining_length;
  }
  memcpy(buffer, &buffer_[seek_position_], bytes);
  seek_position_ += bytes;
  if (bytes_read) {
    *bytes_read = bytes;
  }
  return SR_SUCCESS;
}

StreamResult MemoryStream::Write(const void *buffer,
    size_t bytes, size_t *bytes_written, int *error) {
  StreamResult sr = SR_SUCCESS;
  int error_value = 0;
  size_t bytes_written_value = 0;

  size_t new_position = seek_position_ + bytes;
  if (new_position > allocated_length_) {
    // Increase buffer size to the larger of:
    // a) new position rounded up to next 256 bytes
    // b) double the previous length
    size_t new_allocated_length = _max((new_position | 0xFF) + 1,
                                       allocated_length_ * 2);
    if (char* new_buffer = new char[new_allocated_length]) {
      memcpy(new_buffer, buffer_, data_length_);
      delete [] buffer_;
      buffer_ = new_buffer;
      allocated_length_ = new_allocated_length;
    } else {
      error_value = ENOMEM;
      sr = SR_ERROR;
    }
  }

  if (sr == SR_SUCCESS) {
    bytes_written_value = bytes;
    memcpy(&buffer_[seek_position_], buffer, bytes);
    seek_position_ = new_position;
    if (data_length_ < seek_position_) {
      data_length_ = seek_position_;
    }
  }

  if (bytes_written) {
    *bytes_written = bytes_written_value;
  }
  if (error) {
    *error = error_value;
  }

  return sr;
}

void MemoryStream::Close() {
  // nothing to do
}

bool MemoryStream::SetPosition(size_t position) {
  if (position <= data_length_) {
    seek_position_ = position;
    return true;
  }
  return false;
}

bool MemoryStream::GetPosition(size_t *position) const {
  if (!position) {
    return false;
  }
  *position = seek_position_;
  return true;
}

bool MemoryStream::GetSize(size_t *size) const {
  if (!size) {
    return false;
  }
  *size = data_length_;
  return true;
}

bool MemoryStream::ReserveSize(size_t size) {
  if (allocated_length_ >= size)
    return true;

  if (char* new_buffer = new char[size]) {
    memcpy(new_buffer, buffer_, data_length_);
    delete [] buffer_;
    buffer_ = new_buffer;
    allocated_length_ = size;
    return true;
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////

StreamResult Flow(StreamInterface* source,
                  char* buffer, size_t buffer_len,
                  StreamInterface* sink) {
  ASSERT(buffer_len > 0);

  StreamResult result;
  size_t count, read_pos, write_pos;

  bool end_of_stream = false;
  do {
    // Read until buffer is full, end of stream, or error
    read_pos = 0;
    do {
      result = source->Read(buffer + read_pos, buffer_len - read_pos,
                            &count, NULL);
      if (result == SR_EOS) {
        end_of_stream = true;
      } else if (result != SR_SUCCESS) {
        return result;
      } else {
        read_pos += count;
      }
    } while (!end_of_stream && (read_pos < buffer_len));

    // Write until buffer is empty, or error (including end of stream)
    write_pos = 0;
    do {
      result = sink->Write(buffer + write_pos, read_pos - write_pos,
                           &count, NULL);
      if (result != SR_SUCCESS)
        return result;

      write_pos += count;
    } while (write_pos < read_pos);
  } while (!end_of_stream);

  return SR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////

LoggingAdapter::LoggingAdapter(StreamInterface* stream, LoggingSeverity level,
                               const std::string& label, bool hex_mode)
: StreamAdapterInterface(stream), level_(level), hex_mode_(hex_mode)
{
  label_.append("[");
  label_.append(label);
  label_.append("]");
}

StreamResult LoggingAdapter::Read(void* buffer, size_t buffer_len,
                                  size_t* read, int* error) {
  size_t local_read; if (!read) read = &local_read;
  StreamResult result = StreamAdapterInterface::Read(buffer, buffer_len, read, error);
  if (result == SR_SUCCESS) {
    LogMultiline(level_, label_.c_str(), true,
                 static_cast<const char *>(buffer), *read, hex_mode_, &lms_);
  }
  return result;
}

StreamResult LoggingAdapter::Write(const void* data, size_t data_len,
                                   size_t* written, int* error) {
  size_t local_written; if (!written) written = &local_written;
  StreamResult result = StreamAdapterInterface::Write(data, data_len, written, error);
  if (result == SR_SUCCESS) {
    LogMultiline(level_, label_.c_str(), false,
                 static_cast<const char *>(data), *written, hex_mode_, &lms_);
  }
  return result;
}

void LoggingAdapter::Close() {
  LOG_V(level_) << label_ << " Closed locally";
  StreamAdapterInterface::Close();
}

void LoggingAdapter::OnEvent(StreamInterface* stream, int events, int err) {
  if (events & SE_OPEN) {
    LOG_V(level_) << label_ << " Open";
  } else if (events & SE_CLOSE) {
    LOG_V(level_) << label_ << " Closed with error: " << err;
  }
  StreamAdapterInterface::OnEvent(stream, events, err);
}

///////////////////////////////////////////////////////////////////////////////
// StringStream - Reads/Writes to an external std::string
///////////////////////////////////////////////////////////////////////////////

StringStream::StringStream(std::string& str)
: str_(str), read_pos_(0), read_only_(false)
{
}

StringStream::StringStream(const std::string& str)
: str_(const_cast<std::string&>(str)), read_pos_(0), read_only_(true)
{
}

StreamState StringStream::GetState() const {
  return SS_OPEN;
}

StreamResult StringStream::Read(void* buffer, size_t buffer_len,
                                      size_t* read, int* error) {
  size_t available = talk_base::_min(buffer_len, str_.size() - read_pos_);
  if (!available)
    return SR_EOS;
  memcpy(buffer, str_.data() + read_pos_, available);
  read_pos_ += available;
  if (read)
    *read = available;
  return SR_SUCCESS;
}

StreamResult StringStream::Write(const void* data, size_t data_len,
                                      size_t* written, int* error) {
  if (read_only_) {
    if (error) {
      *error = -1;
    }
    return SR_ERROR;
  }
  str_.append(static_cast<const char*>(data),
              static_cast<const char*>(data) + data_len);
  if (written)
    *written = data_len;
  return SR_SUCCESS;
}

void StringStream::Close() {
}

bool StringStream::GetSize(size_t* size) const {
  ASSERT(size != NULL);
  *size = str_.size();
  return true;
}

bool StringStream::ReserveSize(size_t size) {
  if (read_only_)
    return false;
  str_.reserve(size);
  return true;
}

bool StringStream::Rewind() {
  read_pos_ = 0;
  return true;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace talk_base
