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

#include <algorithm>

#include "talk/base/httpcommon-inl.h"

#include "talk/base/asyncsocket.h"
#include "talk/base/common.h"
#include "talk/base/httpserver.h"
#include "talk/base/logging.h"
#include "talk/base/socketstream.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// HttpServer
///////////////////////////////////////////////////////////////////////////////

HttpServer::HttpServer() : next_connection_id_(1) {
}

HttpServer::~HttpServer() {
  for (ConnectionMap::iterator it = connections_.begin();
       it != connections_.end();
       ++it) {
    StreamInterface* stream = it->second->EndProcess();
    delete stream;
    delete it->second;
  }
}

int
HttpServer::HandleConnection(StreamInterface* stream) {
  int connection_id = next_connection_id_++;
  ASSERT(connection_id != HTTP_INVALID_CONNECTION_ID);
  Connection* connection = new Connection(connection_id, this);
  connections_.insert(ConnectionMap::value_type(connection_id, connection));
  connection->BeginProcess(stream);
  return connection_id;
}

void
HttpServer::Respond(HttpTransaction* transaction) {
  int connection_id = transaction->connection_id();
     if (Connection* connection = Find(connection_id)) {
          connection->Respond(transaction);
  } else {
    delete transaction;
    // We may be tempted to SignalHttpComplete, but that implies that a
    // connection still exists.
  }
}

void
HttpServer::Close(int connection_id, bool force) {
     if (Connection* connection = Find(connection_id)) {
          connection->InitiateClose(force);
     }
}

void
HttpServer::CloseAll(bool force) {
  std::list<Connection*> connections;
  for (ConnectionMap::const_iterator it = connections_.begin();
       it != connections_.end(); ++it) {
     connections.push_back(it->second);
  }
  for (std::list<Connection*>::const_iterator it = connections.begin();
      it != connections.end(); ++it) {
    (*it)->InitiateClose(force);
  }
}

HttpServer::Connection*
HttpServer::Find(int connection_id) {
  ConnectionMap::iterator it = connections_.find(connection_id);
  if (it == connections_.end())
    return NULL;
  return it->second;
}

void
HttpServer::Remove(int connection_id) {
  ConnectionMap::iterator it = connections_.find(connection_id);
  if (it == connections_.end()) {
    ASSERT(false);
    return;
  }
  Connection* connection = it->second;
  connections_.erase(it);
  SignalConnectionClosed(this, connection_id, connection->EndProcess());
  delete connection;
}

///////////////////////////////////////////////////////////////////////////////
// HttpServer::Connection
///////////////////////////////////////////////////////////////////////////////

HttpServer::Connection::Connection(int connection_id, HttpServer* server) 
  : connection_id_(connection_id), server_(server),
    current_(NULL), signalling_(false), close_(false) { 
}

HttpServer::Connection::~Connection() {
  delete current_;
}

void
HttpServer::Connection::BeginProcess(StreamInterface* stream) {
  base_.notify(this); 
  base_.attach(stream);
  current_ = new HttpTransaction(connection_id_);
  current_->request()->document.reset(new MemoryStream);
  if (base_.mode() != HM_CONNECT)
    base_.recv(current_->request());
}

StreamInterface*
HttpServer::Connection::EndProcess() {
  base_.notify(NULL);
  base_.abort(HE_DISCONNECTED);
  return base_.detach();
}

void
HttpServer::Connection::Respond(HttpTransaction* transaction) {
  ASSERT(current_ == NULL);
  current_ = transaction;
  if (current_->response()->begin() == current_->response()->end()) {
    current_->response()->set_error(HC_INTERNAL_SERVER_ERROR);
  }
  bool keep_alive = HttpShouldKeepAlive(*transaction->request());
  current_->response()->setHeader(HH_CONNECTION,
                                  keep_alive ? "Keep-Alive" : "Close",
                                  false);
  close_ = !HttpShouldKeepAlive(*transaction->response());
  base_.send(current_->response());
}

void
HttpServer::Connection::InitiateClose(bool force) {
  if (!signalling_ && (force || (base_.mode() != HM_SEND))) {
    server_->Remove(connection_id_);
  } else {
    close_ = true;
  }
}

//
// IHttpNotify Implementation
//
  
HttpError
HttpServer::Connection::onHttpHeaderComplete(bool chunked, size_t& data_size) {
  if (data_size == SIZE_UNKNOWN) {
    data_size = 0;
  }
  return HE_NONE;
}

void
HttpServer::Connection::onHttpComplete(HttpMode mode, HttpError err) {
  if (mode == HM_SEND) {
    ASSERT(current_ != NULL);
    signalling_ = true;
    server_->SignalHttpRequestComplete(server_, current_, err);
    signalling_ = false;
    if (close_) {
      // Force a close
      err = HE_DISCONNECTED;
    }
  }
  if (err != HE_NONE) {
    server_->Remove(connection_id_);
  } else if (mode == HM_CONNECT) {
    base_.recv(current_->request());
  } else if (mode == HM_RECV) {
    ASSERT(current_ != NULL);
    // TODO: do we need this?
    //request_.document_->rewind();
    HttpTransaction* transaction = current_;
    current_ = NULL;
    server_->SignalHttpRequest(server_, transaction);
  } else if (mode == HM_SEND) {
    current_->request()->clear(true);
    current_->request()->document.reset(new MemoryStream);
    current_->response()->clear(true);
    base_.recv(current_->request());
  } else {
    ASSERT(false);
  }
}

void
HttpServer::Connection::onHttpClosed(HttpError err) {
  UNUSED(err);
  server_->Remove(connection_id_);
}

///////////////////////////////////////////////////////////////////////////////
// HttpListenServer
///////////////////////////////////////////////////////////////////////////////

HttpListenServer::HttpListenServer(AsyncSocket* listener)
  : listener_(listener) {
  listener_->SignalReadEvent.connect(this, &HttpListenServer::OnReadEvent);
}

HttpListenServer::~HttpListenServer() {
}

int
HttpListenServer::Listen(const SocketAddress& address) {
  if ((listener_->Bind(address) != SOCKET_ERROR) &&
      (listener_->Listen(5) != SOCKET_ERROR))
    return 0;
  return listener_->GetError();
}

bool
HttpListenServer::GetAddress(SocketAddress& address) {
  address = listener_->GetLocalAddress();
  return !address.IsNil();
}

void
HttpListenServer::OnReadEvent(AsyncSocket* socket) {
  ASSERT(socket == listener_);
  AsyncSocket* incoming = static_cast<AsyncSocket*>(listener_->Accept(NULL));
  if (incoming)
    HandleConnection(new SocketStream(incoming));
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base
