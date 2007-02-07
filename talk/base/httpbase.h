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

#ifndef TALK_BASE_HTTPBASE_H__
#define TALK_BASE_HTTPBASE_H__

#include "talk/base/httpcommon.h"

namespace talk_base {

class StreamInterface;

//////////////////////////////////////////////////////////////////////
// HttpParser
//////////////////////////////////////////////////////////////////////

class HttpParser {
public:
  HttpParser();
  virtual ~HttpParser();
  
  void reset();
  bool process(const char* buffer, size_t len, size_t& read, HttpError& err);
  void end_of_input();
  void complete(HttpError err);
  
protected:
  bool process_line(const char* line, size_t len, HttpError& err);

  // HttpParser Interface
  virtual HttpError onHttpRecvLeader(const char* line, size_t len) = 0;
  virtual HttpError onHttpRecvHeader(const char* name, size_t nlen,
                                     const char* value, size_t vlen) = 0;
  virtual HttpError onHttpRecvHeaderComplete(bool chunked, size_t& data_size) = 0;
  virtual HttpError onHttpRecvData(const char* data, size_t len, size_t& read) = 0;
  virtual void onHttpRecvComplete(HttpError err) = 0;
  
private:
  enum State {
    ST_LEADER, ST_HEADERS,
    ST_CHUNKSIZE, ST_CHUNKTERM, ST_TRAILERS,
    ST_DATA, ST_COMPLETE
  } state_;
  bool chunked_;
  size_t data_size_;
};

//////////////////////////////////////////////////////////////////////
// IHttpNotify
//////////////////////////////////////////////////////////////////////

enum HttpMode { HM_NONE, HM_CONNECT, HM_RECV, HM_SEND };

class IHttpNotify {
public:
  virtual HttpError onHttpHeaderComplete(bool chunked, size_t& data_size) = 0;
  virtual void onHttpComplete(HttpMode mode, HttpError err) = 0;
  virtual void onHttpClosed(HttpError err) = 0;
};

//////////////////////////////////////////////////////////////////////
// HttpBase
//////////////////////////////////////////////////////////////////////

class HttpBase : private HttpParser, public sigslot::has_slots<> {
public:
  HttpBase();
  virtual ~HttpBase();

  void notify(IHttpNotify* notify) { notify_ = notify; }
  bool attach(StreamInterface* stream);
  StreamInterface* stream() { return stream_; }
  StreamInterface* detach();
  bool isConnected() const;

  void send(HttpData* data);
  void recv(HttpData* data);
  void abort(HttpError err);

  HttpMode mode() const { return mode_; }

  void set_ignore_data(bool ignore) { ignore_data_ = ignore; }
  bool ignore_data() const { return ignore_data_; }

protected:
  void flush_data();
  void queue_headers();
  void do_complete(HttpError err = HE_NONE);

  void OnEvent(StreamInterface* stream, int events, int error);
  
  // HttpParser Interface
  virtual HttpError onHttpRecvLeader(const char* line, size_t len);
  virtual HttpError onHttpRecvHeader(const char* name, size_t nlen,
                                     const char* value, size_t vlen);
  virtual HttpError onHttpRecvHeaderComplete(bool chunked, size_t& data_size);
  virtual HttpError onHttpRecvData(const char* data, size_t len, size_t& read);
  virtual void onHttpRecvComplete(HttpError err);

private:
  enum { kBufferSize = 32 * 1024 };

  HttpMode mode_;
  HttpData* data_;
  IHttpNotify* notify_;
  StreamInterface* stream_;
  char buffer_[kBufferSize];
  size_t len_;

  bool ignore_data_, chunk_data_;
  HttpData::const_iterator header_;
};

//////////////////////////////////////////////////////////////////////

} // namespace talk_base

#endif // TALK_BASE_HTTPBASE_H__
