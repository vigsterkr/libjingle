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

#ifdef OSX
#include <errno.h>
#endif

#ifdef WIN32
#include "talk/base/win32.h"
#else  // !WIN32
#define SEC_E_CERT_EXPIRED (-2146893016)
#endif  // !WIN32

#include "talk/base/common.h"
#include "talk/base/httpbase.h"
#include "talk/base/logging.h"
#include "talk/base/socket.h"
#include "talk/base/stringutils.h"

namespace talk_base {

//////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////

bool MatchHeader(const char* str, size_t len, HttpHeader header) {
  const char* const header_str = ToString(header);
  const size_t header_len = strlen(header_str);
  return (len == header_len) && (_strnicmp(str, header_str, header_len) == 0);
}

//////////////////////////////////////////////////////////////////////
// HttpParser
//////////////////////////////////////////////////////////////////////

HttpParser::HttpParser() {
  reset();
}

HttpParser::~HttpParser() {
}

void 
HttpParser::reset() {
  state_ = ST_LEADER;
  chunked_ = false;
  data_size_ = SIZE_UNKNOWN;
}

bool
HttpParser::process(const char* buffer, size_t len, size_t& processed,
                    HttpError& err) {
  processed = 0;
  err = HE_NONE;

  if (state_ >= ST_COMPLETE) {
    ASSERT(false);
    return false;
  }

  while (true) {
    if (state_ < ST_DATA) {
      size_t pos = processed;
      while ((pos < len) && (buffer[pos] != '\n')) {
        pos += 1;
      }
      if (pos >= len) {
        break; // don't have a full header
      }
      const char* line = buffer + processed;
      size_t len = (pos - processed);
      processed = pos + 1;
      while ((len > 0) && isspace(static_cast<unsigned char>(line[len-1]))) {
        len -= 1;
      }
      if (!process_line(line, len, err)) {
        return false; // no more processing
      }
    } else if (data_size_ == 0) {
      if (chunked_) {
        state_ = ST_CHUNKTERM;
      } else {
        return false;
      }
    } else {
      size_t available = len - processed;
      if (available <= 0) {
        break; // no more data
      }
      if ((data_size_ != SIZE_UNKNOWN) && (available > data_size_)) {
        available = data_size_;
      }
      size_t read = 0;
      err = onHttpRecvData(buffer + processed, available, read);
      if (err != HE_NONE) {
        return false; // error occurred
      }
      processed += read;
      if (data_size_ != SIZE_UNKNOWN) {
        data_size_ -= read;
      }
    }
  }

  return true;
}

bool
HttpParser::process_line(const char* line, size_t len, HttpError& err) {
  switch (state_) {
  case ST_LEADER:
    state_ = ST_HEADERS;
    err = onHttpRecvLeader(line, len);
    break;

  case ST_HEADERS:
    if (len > 0) {
      const char* value = strchrn(line, len, ':');
      if (!value) {
        err = HE_PROTOCOL;
        break;
      }
      size_t nlen = (value - line);
      const char* eol = line + len;
      do {
        value += 1;
      } while ((value < eol) && isspace(static_cast<unsigned char>(*value)));
      size_t vlen = eol - value;
      if (MatchHeader(line, nlen, HH_CONTENT_LENGTH)) {
        if (sscanf(value, "%d", &data_size_) != 1) {
          err = HE_PROTOCOL;
          break;
        }
      } else if (MatchHeader(line, nlen, HH_TRANSFER_ENCODING)) {
        if ((vlen == 7) && (_strnicmp(value, "chunked", 7) == 0)) {
          chunked_ = true;
        } else if ((vlen == 8) && (_strnicmp(value, "identity", 8) == 0)) {
          chunked_ = false;
        } else {
          err = HE_PROTOCOL;
          break;
        }
      }
      err = onHttpRecvHeader(line, nlen, value, vlen);
    } else {
      state_ = chunked_ ? ST_CHUNKSIZE : ST_DATA;
      err = onHttpRecvHeaderComplete(chunked_, data_size_);
    }
    break;

  case ST_CHUNKSIZE:
    if (len > 0) {
      char* ptr = NULL;
      data_size_ = strtoul(line, &ptr, 16);
      if (ptr != line + len) {
        err = HE_PROTOCOL;
        break;
      }
      state_ = (data_size_ == 0) ? ST_TRAILERS : ST_DATA;
    } else {
      err = HE_PROTOCOL;
    }
    break;

  case ST_CHUNKTERM:
    if (len > 0) {
      err = HE_PROTOCOL;
    } else {
      state_ = chunked_ ? ST_CHUNKSIZE : ST_DATA;
    }
    break;

  case ST_TRAILERS:
    if (len == 0) {
      return false;
    }
    // err = onHttpRecvTrailer();
    break;

  default:
    break;
  }

  return (err == HE_NONE);
}
 
void 
HttpParser::end_of_input() {
  if ((state_ == ST_DATA) && (data_size_ == SIZE_UNKNOWN)) {
    complete(HE_NONE);
  } else {
    complete(HE_DISCONNECTED);
  }
}

void 
HttpParser::complete(HttpError err) {
  if (state_ < ST_COMPLETE) {
    state_ = ST_COMPLETE;
    onHttpRecvComplete(err);
  }
}

//////////////////////////////////////////////////////////////////////
// HttpBase
//////////////////////////////////////////////////////////////////////

HttpBase::HttpBase() : mode_(HM_NONE), data_(NULL), notify_(NULL),
                       stream_(NULL) {
}

HttpBase::~HttpBase() {
}

bool
HttpBase::isConnected() const {
  return (stream_ != NULL) && (stream_->GetState() == SS_OPEN);
}

bool
HttpBase::attach(StreamInterface* stream) {
  if ((mode_ != HM_NONE) || (stream_ != NULL) || (stream == NULL)) {
    ASSERT(false);
    return false;
  }
  stream_ = stream;
  stream_->SignalEvent.connect(this, &HttpBase::OnEvent);
  mode_ = (stream_->GetState() == SS_OPENING) ? HM_CONNECT : HM_NONE;
  return true;
}

StreamInterface*
HttpBase::detach() {
  if (mode_ != HM_NONE) {
    ASSERT(false);
    return NULL;
  }
  StreamInterface* stream = stream_;
  stream_ = NULL;
  if (stream) {
    stream->SignalEvent.disconnect(this);
  }
  return stream;
}

/*
bool
HttpBase::accept(PNSocket& socket) {
  if (mode_ != HM_NONE) {
    ASSERT(false);
    return false;
  }
  
  return socket.accept(stream_);
}

void
HttpBase::connect(const SocketAddress& addr) {
  if (mode_ != HM_NONE) {
    ASSERT(false);
    return;
  }

  mode_ = HM_CONNECT;

  SocketAddress local;
  if (!stream_.connect(local, addr)  && !stream_.isBlocking()) {
    onSocketConnect(&stream_, stream_.getError());
  }
}
*/
void
HttpBase::send(HttpData* data) {
  if (mode_ != HM_NONE) {
    ASSERT(false);
    return;
  } else if (!isConnected()) {
    OnEvent(stream_, SE_CLOSE, HE_DISCONNECTED);
    return;
  }
  
  mode_ = HM_SEND;
  data_ = data;
  len_ = 0;
  ignore_data_ = chunk_data_ = false;

  std::string encoding;
  if (data_->hasHeader(HH_TRANSFER_ENCODING, &encoding)
      && (encoding == "chunked")) {
    chunk_data_ = true;
  }
  
  len_ = data_->formatLeader(buffer_, sizeof(buffer_));
  len_ += strcpyn(buffer_ + len_, sizeof(buffer_) - len_, "\r\n");
  header_ = data_->begin();
  queue_headers();

  OnEvent(stream_, SE_WRITE, 0);
}

void
HttpBase::recv(HttpData* data) {
  if (mode_ != HM_NONE) {
    ASSERT(false);
    return;
  } else if (!isConnected()) {
    OnEvent(stream_, SE_CLOSE, HE_DISCONNECTED);
    return;
  }
  
  mode_ = HM_RECV;
  data_ = data;
  len_ = 0;
  ignore_data_ = chunk_data_ = false;
  
  reset();
  OnEvent(stream_, SE_READ, 0);
}

void
HttpBase::abort(HttpError err) {
  if (mode_ != HM_NONE) {
    if (stream_ != NULL) {
      stream_->Close();
    }
    do_complete(err);
  }
}

void
HttpBase::flush_data() {
  while (true) {
    for (size_t start = 0; start < len_; ) {
      size_t written;
      int error;
      StreamResult result = stream_->Write(buffer_ + start, len_ - start,
                                           &written, &error);
      if (result == SR_SUCCESS) {
        //LOG_F(LS_INFO) << "wrote " << res << " bytes";
        start += written;
        continue;
      } else if (result == SR_BLOCK) {
        //LOG_F(LS_INFO) << "blocking";
        len_ -= start;
        memmove(buffer_, buffer_ + start, len_);
        return;
      } else {
        ASSERT(result == SR_ERROR);
        LOG_F(LS_ERROR) << "error";
        OnEvent(stream_, SE_CLOSE, error);
        return;
      }
    }
    len_ = 0;

    // Check for more headers
    if (header_ != data_->end()) {
      queue_headers();
      continue;
    }

    // Check for document data
    if (!data_->document.get())
      break;

    size_t offset = 0, reserve = 0;
    if (chunk_data_) {
      // Reserve 10 characters at the start for 8-byte hex value and \r\n
      offset = 10;
      // ... and 2 characters at the end for \r\n
      reserve = offset + 2;
      ASSERT(reserve < sizeof(buffer_));
    }

    int error = 0;
    StreamResult result = data_->document->Read(buffer_ + offset,
                                                sizeof(buffer_) - reserve,
                                                &len_, &error);
    if (result == SR_SUCCESS) {
      if (!chunk_data_)
        continue;

      // Prepend the length and append \r\n
      sprintfn(buffer_, offset, "%.*x", (offset - 2), len_);
      memcpy(buffer_ + offset - 2, "\r\n", 2);
      memcpy(buffer_ + offset + len_, "\r\n", 2);
      ASSERT(len_ + reserve <= sizeof(buffer_));
      len_ += reserve;
    } else if (result == SR_EOS) {
      if (!chunk_data_)
        break;

      // Append the empty chunk and empty trailers, then turn off chunking.
      len_ = sprintfn(buffer_, sizeof(buffer_), "0\r\n\r\n");
      chunk_data_ = false;
    } else {
      LOG_F(LS_ERROR) << "Read error: " << error;
      do_complete(HE_STREAM);
      return;
    }
  }

  do_complete();
}

void
HttpBase::queue_headers() {
  while (header_ != data_->end()) {
    size_t len = sprintfn(buffer_ + len_, sizeof(buffer_) - len_,
                          "%.*s: %.*s\r\n",
                          header_->first.size(), header_->first.data(),
                          header_->second.size(), header_->second.data());
    if (len_ + len < sizeof(buffer_) - 3) {
      len_ += len;
      ++header_;
    } else if (len_ == 0) {
      LOG(WARNING) << "discarding header that is too long: " << header_->first;
      ++header_;
    } else {
      break;
    }
  }
  if (header_ == data_->end()) {
    len_ += strcpyn(buffer_ + len_, sizeof(buffer_) - len_, "\r\n");
  }
}

void
HttpBase::do_complete(HttpError err) {
  ASSERT(mode_ != HM_NONE);
  HttpMode mode = mode_;
  mode_ = HM_NONE;
  data_ = NULL;
  if (notify_) {
	  notify_->onHttpComplete(mode, err);
  }
}

void
HttpBase::OnEvent(StreamInterface* stream, int events, int error) {
  if ((events & SE_OPEN) && (mode_ == HM_CONNECT)) {
    do_complete();
    return;
  }

  if ((events & SE_WRITE) && (mode_ == HM_SEND)) {
    flush_data();
    return;
  }

  if ((events & SE_READ) && (mode_ == HM_RECV)) {
    // Do to the latency between receiving read notifications from
    // pseudotcpchannel, we rely on repeated calls to read in order to acheive
    // ideal throughput.  The number of reads is limited to prevent starving
    // the caller.
    size_t loop_count = 0;
    const size_t kMaxReadCount = 20;
    while (true) {
      if (len_ >= sizeof(buffer_)) {
        do_complete(HE_OVERFLOW);
        return;
      }
      size_t read;
      int error;
      StreamResult result = stream_->Read(buffer_ + len_,
                                          sizeof(buffer_) - len_,
                                          &read, &error);
      if ((result == SR_BLOCK) || (result == SR_EOS))
        return;
      if (result == SR_ERROR) {
        OnEvent(stream_, SE_CLOSE, error);
        return;
      }
      ASSERT(result == SR_SUCCESS);
      //LOG(INFO) << "HttpBase << " << std::string(buffer_ + len_, res);
      len_ += read;
      HttpError herr;
      bool more = process(buffer_, len_, read, herr);
      len_ -= read;
      memcpy(buffer_, buffer_ + read, len_);
      if (!more) {
        complete(herr);
        return;
      }
      if (++loop_count > kMaxReadCount) {
        LOG_F(LS_WARNING) << "danger of starvation";
        break;
      }
    }
    return;
  }

  if ((events & SE_CLOSE) == 0)
    return;

  if (stream_ != NULL) {
    stream_->Close();
  }
  HttpError herr;
  // TODO: Pass through errors instead of translating them?
  if (error == 0) {
    herr = HE_DISCONNECTED;
  } else if (error == SOCKET_EACCES) {
    herr = HE_AUTH;
  } else if (error == SEC_E_CERT_EXPIRED) {
    herr = HE_CERTIFICATE_EXPIRED;
  } else {
    LOG_F(LS_ERROR) << "SE_CLOSE error: " << error;
    herr = HE_SOCKET;
  }
  if ((mode_ == HM_RECV) && (error == HE_NONE)) {
    end_of_input();
  } else if (mode_ != HM_NONE) {
    do_complete(mkerr(herr, HE_DISCONNECTED));
  } else if (notify_) {
    notify_->onHttpClosed(mkerr(herr, HE_DISCONNECTED));
  }
}

//
// HttpParser Implementation
//

HttpError
HttpBase::onHttpRecvLeader(const char* line, size_t len) {
  return data_->parseLeader(line, len);
}

HttpError
HttpBase::onHttpRecvHeader(const char* name, size_t nlen, const char* value,
                           size_t vlen) {
  std::string sname(name, nlen), svalue(value, vlen);
  data_->addHeader(sname, svalue);
  //LOG(INFO) << sname << ": " << svalue;
  return HE_NONE;
}
 
HttpError
HttpBase::onHttpRecvHeaderComplete(bool chunked, size_t& data_size) {
  return notify_ ? notify_->onHttpHeaderComplete(chunked, data_size) : HE_NONE;
}

HttpError
HttpBase::onHttpRecvData(const char* data, size_t len, size_t& read) {
  if (ignore_data_ || !data_->document.get()) {
    read = len;
    return HE_NONE;  
  }
  int error = 0;
  switch (data_->document->Write(data, len, &read, &error)) {
    case SR_SUCCESS:
      return HE_NONE;
    case SR_EOS:
    case SR_BLOCK:
      LOG_F(LS_ERROR) << "Write EOS or block";
      return HE_STREAM;
  }
  LOG_F(LS_ERROR) << "Write error: " << error;
  return HE_STREAM;
}

void
HttpBase::onHttpRecvComplete(HttpError err) {
  do_complete(err);
}

} // namespace talk_base
