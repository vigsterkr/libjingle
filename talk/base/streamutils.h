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

#ifndef TALK_APP_STREAMUTILS_H__
#define TALK_APP_STREAMUTILS_H__

#include "talk/base/sigslot.h"
#include "talk/base/stream.h"

///////////////////////////////////////////////////////////////////////////////
// StreamRelay - acts as an intermediary between two asynchronous streams,
//  reading from one stream and writing to the other, using a pre-specified
//  amount of buffering in both directions.
///////////////////////////////////////////////////////////////////////////////

class StreamRelay : public sigslot::has_slots<> {
public:
  StreamRelay(talk_base::StreamInterface* s1, 
              talk_base::StreamInterface* s2, size_t buffer_size);
  virtual ~StreamRelay();

  void Circulate(); // Simulate events to get things flowing
  void Close();

  sigslot::signal2<StreamRelay*, int> SignalClosed;

private:
  inline int Index(talk_base::StreamInterface* s) const 
    { return (s == dir_[1].stream); }
  inline int Complement(int index) const { return (1-index); }

  bool Flow(int read_index, int* error);
  void OnEvent(talk_base::StreamInterface* stream, int events, int error);

  struct Direction {
    talk_base::StreamInterface* stream;
    char* buffer;
    size_t data_len;
  };
  Direction dir_[2];
  size_t buffer_size_;
};

///////////////////////////////////////////////////////////////////////////////
// StreamCounter - counts the number of bytes which are transferred in either
//  direction.
///////////////////////////////////////////////////////////////////////////////

class StreamCounter : public talk_base::StreamAdapterInterface {
 public:
  explicit StreamCounter(talk_base::StreamInterface* stream);

  inline void ResetByteCount() { count_ = 0; }
  inline size_t GetByteCount() const { return count_; }

  sigslot::signal1<size_t> SignalUpdateByteCount;

  // StreamAdapterInterface
  virtual talk_base::StreamResult Read(void* buffer, size_t buffer_len,
                                       size_t* read, int* error);
  virtual talk_base::StreamResult Write(const void* data, size_t data_len,
                                        size_t* written, int* error);

 private:
  size_t count_;
};

///////////////////////////////////////////////////////////////////////////////

#endif  // TALK_APP_STREAMUTILS_H__
