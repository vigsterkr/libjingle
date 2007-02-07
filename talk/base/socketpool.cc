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

#include <errno.h>

#include "talk/base/asyncsocket.h"
#include "talk/base/logging.h"
#include "talk/base/socketfactory.h"
#include "talk/base/socketpool.h"
#include "talk/base/socketstream.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// StreamCache - Caches a set of open streams, defers creation to a separate
//  StreamPool.
///////////////////////////////////////////////////////////////////////////////

StreamCache::StreamCache(StreamPool* pool) : pool_(pool) {
}

StreamCache::~StreamCache() {
  for (ConnectedList::iterator it = active_.begin(); it != active_.end();
       ++it) {
    delete it->second;
  }
  for (ConnectedList::iterator it = cached_.begin(); it != cached_.end();
       ++it) {
    delete it->second;
  }
}

StreamInterface* StreamCache::RequestConnectedStream(
    const SocketAddress& remote, int* err) {
  LOG_F(LS_VERBOSE) << "(" << remote.ToString() << ")";
  for (ConnectedList::iterator it = cached_.begin(); it != cached_.end();
       ++it) {
    if (remote == it->first) {
      it->second->SignalEvent.disconnect(this);
      // Move from cached_ to active_
      active_.push_front(*it);
      cached_.erase(it);
      if (err)
        *err = 0;
      LOG_F(LS_VERBOSE) << "Providing cached stream";
      return active_.front().second;
    }
  }
  if (StreamInterface* stream = pool_->RequestConnectedStream(remote, err)) {
    // We track active streams so that we can remember their address
    active_.push_front(ConnectedStream(remote, stream));
    LOG_F(LS_VERBOSE) << "Providing new stream";
    return active_.front().second;
  }
  return NULL;
}

void StreamCache::ReturnConnectedStream(StreamInterface* stream) {
  for (ConnectedList::iterator it = active_.begin(); it != active_.end();
       ++it) {
    if (stream == it->second) {
      LOG_F(LS_VERBOSE) << "(" << it->first.ToString() << ")";
      if (stream->GetState() == SS_CLOSED) {
        // Return closed streams
        LOG_F(LS_VERBOSE) << "Returning closed stream";
        pool_->ReturnConnectedStream(it->second);
      } else {
        // Monitor open streams
        stream->SignalEvent.connect(this, &StreamCache::OnStreamEvent);
        LOG_F(LS_VERBOSE) << "Caching stream";
        cached_.push_front(*it);
      }
      active_.erase(it);
      return;
    }
  }
  ASSERT(false);
}

void StreamCache::OnStreamEvent(StreamInterface* stream, int events, int err) {
  if ((events & SE_CLOSE) == 0) {
    LOG_F(LS_WARNING) << "(" << events << ", " << err
                      << ") received non-close event";
    return;
  }
  for (ConnectedList::iterator it = cached_.begin(); it != cached_.end();
       ++it) {
    if (stream == it->second) {
      LOG_F(LS_VERBOSE) << "(" << it->first.ToString() << ")";
      // We don't cache closed streams, so return it.
      it->second->SignalEvent.disconnect(this);
      LOG_F(LS_VERBOSE) << "Returning closed stream";
      pool_->ReturnConnectedStream(it->second);
      cached_.erase(it);
      return;
    }
  }
  ASSERT(false);
}

//////////////////////////////////////////////////////////////////////
// NewSocketPool
//////////////////////////////////////////////////////////////////////

NewSocketPool::NewSocketPool(SocketFactory* factory) : factory_(factory) {
}

NewSocketPool::~NewSocketPool() {
  for (size_t i = 0; i < used_.size(); ++i) {
    delete used_[i];
  }
}

StreamInterface* 
NewSocketPool::RequestConnectedStream(const SocketAddress& remote, int* err) {
  AsyncSocket* socket = factory_->CreateAsyncSocket(SOCK_STREAM);
  if (!socket) {
    ASSERT(false);
    if (err)
      *err = -1;
    return NULL;
  }
  if ((socket->Connect(remote) != 0) && !socket->IsBlocking()) {
    if (err)
      *err = socket->GetError();
    delete socket;
    return NULL;
  }
  if (err)
    *err = 0;
  return new SocketStream(socket);
}

void
NewSocketPool::ReturnConnectedStream(StreamInterface* stream) {
  used_.push_back(stream);
}

//////////////////////////////////////////////////////////////////////
// ReuseSocketPool
//////////////////////////////////////////////////////////////////////

ReuseSocketPool::ReuseSocketPool(SocketFactory* factory, AsyncSocket* socket)
  : factory_(factory), stream_(NULL) {
  stream_ = socket ? new SocketStream(socket) : NULL;
}

ReuseSocketPool::~ReuseSocketPool() {
  delete stream_;
}

void
ReuseSocketPool::setSocket(AsyncSocket* socket) {
  ASSERT(false); // TODO: need ref-counting to make this work
  delete stream_;
  stream_ = socket ? new SocketStream(socket) : NULL;
}

StreamInterface* 
ReuseSocketPool::RequestConnectedStream(const SocketAddress& remote, int* err) {
  if (!stream_) {
    LOG(LS_INFO) << "ReuseSocketPool - Creating new socket";
    AsyncSocket* socket = factory_->CreateAsyncSocket(SOCK_STREAM);
    if (!socket) {
      ASSERT(false);
      if (err)
        *err = -1;
      return NULL;
    }
    stream_ = new SocketStream(socket);
  }
  if ((stream_->GetState() == SS_OPEN) &&
      (stream_->GetSocket()->GetRemoteAddress() == remote)) {
    LOG(LS_INFO) << "ReuseSocketPool - Reusing connection to: "
                 << remote.ToString();
  } else {
    stream_->Close();
    if ((stream_->GetSocket()->Connect(remote) != 0)
        && !stream_->GetSocket()->IsBlocking()) {
      if (err)
        *err = stream_->GetSocket()->GetError();
      return NULL;
    } else {
      LOG(LS_INFO) << "ReuseSocketPool - Opening connection to: "
                   << remote.ToString();
    }
  }
  if (err)
    *err = 0;
  return stream_;
}

void
ReuseSocketPool::ReturnConnectedStream(StreamInterface* stream) {
  // Note: this might not be true with the advent of setSocket
  ASSERT(stream == stream_);
}

///////////////////////////////////////////////////////////////////////////////
// LoggingPoolAdapter - Adapts a StreamPool to supply streams with attached
// LoggingAdapters.
///////////////////////////////////////////////////////////////////////////////

LoggingPoolAdapter::LoggingPoolAdapter(
    StreamPool* pool, LoggingSeverity level, const std::string& label,
    bool binary_mode)
  : pool_(pool), level_(level), label_(label), binary_mode_(binary_mode) {
}

LoggingPoolAdapter::~LoggingPoolAdapter() {
  for (StreamList::iterator it = recycle_bin_.begin();
       it != recycle_bin_.end(); ++it) {
    delete *it;
  }
}

StreamInterface* LoggingPoolAdapter::RequestConnectedStream(
    const SocketAddress& remote, int* err) {
  if (StreamInterface* stream = pool_->RequestConnectedStream(remote, err)) {
    if (recycle_bin_.empty()) {
      return new LoggingAdapter(stream, level_, label_, binary_mode_);
    }
    LoggingAdapter* logging = recycle_bin_.front();
    recycle_bin_.pop_front();
    logging->Attach(stream);
    return logging;
  }
  return NULL;
}

void LoggingPoolAdapter::ReturnConnectedStream(StreamInterface* stream) {
  LoggingAdapter* logging = static_cast<LoggingAdapter*>(stream);
  pool_->ReturnConnectedStream(logging->Detach());
  recycle_bin_.push_back(logging);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace talk_base
