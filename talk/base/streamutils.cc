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


#include "talk/base/common.h"
#include "talk/base/streamutils.h"

///////////////////////////////////////////////////////////////////////////////
// TODO: Extend so that one side can close, and other side can send
// buffered data.

StreamRelay::StreamRelay(talk_base::StreamInterface* s1, 
                         talk_base::StreamInterface* s2,
                         size_t buffer_size) : buffer_size_(buffer_size) {
  dir_[0].stream = s1;
  dir_[1].stream = s2;

  ASSERT(s1->GetState() != talk_base::SS_CLOSED);
  ASSERT(s2->GetState() != talk_base::SS_CLOSED);

  for (size_t i=0; i<2; ++i) {
    dir_[i].stream->SignalEvent.connect(this, &StreamRelay::OnEvent);
    dir_[i].buffer = new char[buffer_size_];
    dir_[i].data_len = 0;
  }
}

StreamRelay::~StreamRelay() {
  for (size_t i=0; i<2; ++i) {
    delete dir_[i].stream;
    delete [] dir_[i].buffer;
  }
}

void
StreamRelay::Circulate() {
  int error = 0;
  if (!Flow(0, &error) || !Flow(1, &error)) {
    Close();
    SignalClosed(this, error);
  }
}

void
StreamRelay::Close() {
  for (size_t i=0; i<2; ++i) {
    dir_[i].stream->SignalEvent.disconnect(this);
    dir_[i].stream->Close();
  }
}

bool
StreamRelay::Flow(int read_index, int* error) {
  Direction& reader = dir_[read_index];
  Direction& writer = dir_[Complement(read_index)];

  bool progress;
  do {
    progress = false;

    while (reader.stream->GetState() == talk_base::SS_OPEN) {
      size_t available = buffer_size_ - reader.data_len;
      if (available == 0)
        break;

      *error = 0;
      size_t read = 0;
      talk_base::StreamResult result 
        = reader.stream->Read(reader.buffer + reader.data_len, available, 
                              &read, error);
      if ((result == talk_base::SR_BLOCK) || (result == talk_base::SR_EOS))
        break;

      if (result == talk_base::SR_ERROR)
        return false;

      progress = true;
      ASSERT((read > 0) && (read <= available));
      reader.data_len += read;
    }

    size_t total_written = 0;
    while (writer.stream->GetState() == talk_base::SS_OPEN) {
      size_t available = reader.data_len - total_written;
      if (available == 0)
        break;

      *error = 0;
      size_t written = 0;
      talk_base::StreamResult result 
          = writer.stream->Write(reader.buffer + total_written,
                                 available, &written, error);
      if ((result == talk_base::SR_BLOCK) || (result == talk_base::SR_EOS))
        break;

      if (result == talk_base::SR_ERROR)
        return false;

      progress = true;
      ASSERT((written > 0) && (written <= available));
      total_written += written;
    }

    reader.data_len -= total_written;
    if (reader.data_len > 0) {
      memmove(reader.buffer, reader.buffer + total_written, reader.data_len);
    }
  } while (progress);

  return true;
}

void StreamRelay::OnEvent(talk_base::StreamInterface* stream, int events, 
                          int error) {
  int index = Index(stream);

  // Note: In the following cases, we are treating the open event as both
  // readable and writeable, for robustness.  It won't hurt if we are wrong.

  if ((events & talk_base::SE_OPEN | talk_base::SE_READ) 
      && !Flow(index, &error)) {
    events = talk_base::SE_CLOSE;
  }

  if ((events & talk_base::SE_OPEN | talk_base::SE_WRITE) 
      && !Flow(Complement(index), &error)) {
    events = talk_base::SE_CLOSE;
  }

  if (events & talk_base::SE_CLOSE) {
    Close();
    SignalClosed(this, error);
  }
}

///////////////////////////////////////////////////////////////////////////////
// StreamCounter - counts the number of bytes which are transferred in either
//  direction.
///////////////////////////////////////////////////////////////////////////////

StreamCounter::StreamCounter(talk_base::StreamInterface* stream)
  : StreamAdapterInterface(stream), count_(0) {
}

talk_base::StreamResult StreamCounter::Read(void* buffer, size_t buffer_len,
                                            size_t* read, int* error) {
  size_t tmp;
  if (!read)
    read = &tmp;
  talk_base::StreamResult result 
    = StreamAdapterInterface::Read(buffer, buffer_len,
                                   read, error);
  if (result == talk_base::SR_SUCCESS)
    count_ += *read;
  SignalUpdateByteCount(count_);
  return result;
}

talk_base::StreamResult StreamCounter::Write(
    const void* data, size_t data_len, size_t* written, int* error) {
  size_t tmp;
  if (!written)
    written = &tmp;
  talk_base::StreamResult result 
    = StreamAdapterInterface::Write(data, data_len, written, error);
  if (result == talk_base::SR_SUCCESS)
    count_ += *written;
  SignalUpdateByteCount(count_);
  return result;
}
