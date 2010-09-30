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

#if defined(_MSC_VER) && _MSC_VER < 1300
#pragma warning(disable:4786)
#endif

#include "talk/p2p/base/tcpport.h"

#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/p2p/base/common.h"

namespace cricket {

TCPPort::TCPPort(talk_base::Thread* thread, talk_base::SocketFactory* factory,
                 talk_base::Network* network,
                 const talk_base::SocketAddress& address,
                 bool allow_listen)
    : Port(thread, LOCAL_PORT_TYPE, factory, network), address_(address),
      incoming_only_(address_.port() != 0), allow_listen_(allow_listen),
      socket_(NULL), error_(0) {
}

bool TCPPort::Init() {
  // We don't use CreatePacketSocket here since we're creating a listen socket.
  // However we will treat failure to create or bind a TCP socket as fatal.
  // This should never happen.
  socket_ = factory_->CreateAsyncSocket(SOCK_STREAM);
  if (!socket_) {
    LOG_J(LS_ERROR, this) << "TCP socket creation failed.";
    return false;
  }
  if (socket_->Bind(address_) < 0) {
    LOG_J(LS_ERROR, this) << "TCP bind failed with error "
                          << socket_->GetError();
    return false;
  }
  socket_->SignalReadEvent.connect(this, &TCPPort::OnAcceptEvent);
  return true;
}

TCPPort::~TCPPort() {
  delete socket_;
}

Connection* TCPPort::CreateConnection(const Candidate& address,
                                      CandidateOrigin origin) {
  // We only support TCP protocols
  if ((address.protocol() != "tcp") && (address.protocol() != "ssltcp"))
    return NULL;

  // We can't accept TCP connections incoming on other ports
  if (origin == ORIGIN_OTHER_PORT)
    return NULL;

  // Check if we are allowed to make outgoing TCP connections
  if (incoming_only_ && (origin == ORIGIN_MESSAGE))
    return NULL;

  // We don't know how to act as an ssl server yet
  if ((address.protocol() == "ssltcp") && (origin == ORIGIN_THIS_PORT))
    return NULL;

  TCPConnection* conn = NULL;
  if (talk_base::AsyncTCPSocket * socket
        = GetIncoming(address.address(), true)) {
    socket->SignalReadPacket.disconnect(this);
    conn = new TCPConnection(this, address, socket);
  } else {
    conn = new TCPConnection(this, address);
  }
  AddConnection(conn);
  return conn;
}

void TCPPort::PrepareAddress() {
  if (!allow_listen_) {
    LOG_J(LS_INFO, this) << "Not listening due to firewall restrictions.";
  } else if (socket_->Listen(5) < 0) {
    LOG_J(LS_WARNING, this) << "TCP listen failed with error "
                            << socket_->GetError();
  }
  // Note: We still add the address, since otherwise the remote side won't
  // recognize our incoming TCP connections.
  AddAddress(socket_->GetLocalAddress(), "tcp", true);
}

int TCPPort::SendTo(const void* data, size_t size,
                    const talk_base::SocketAddress& addr, bool payload) {
  talk_base::AsyncTCPSocket * socket = NULL;
  if (TCPConnection * conn = static_cast<TCPConnection*>(GetConnection(addr))) {
    socket = conn->socket();
  } else {
    socket = GetIncoming(addr);
  }
  if (!socket) {
    LOG_J(LS_ERROR, this) << "Attempted to send to an unknown destination, "
                          << addr.ToString();
    return -1;  // TODO: Set error_
  }

  int sent = socket->Send(data, size);
  if (sent < 0) {
    error_ = socket->GetError();
    LOG_J(LS_ERROR, this) << "TCP send of " << size
                          << " bytes failed with error " << error_;
  }
  return sent;
}

int TCPPort::SetOption(talk_base::Socket::Option opt, int value) {
  return socket_->SetOption(opt, value);
}

int TCPPort::GetError() {
  return error_;
}

void TCPPort::OnAcceptEvent(talk_base::AsyncSocket* socket) {
  ASSERT(socket == socket_);

  Incoming incoming;
  talk_base::AsyncSocket* newsocket = socket->Accept(&incoming.addr);
  if (!newsocket) {
    // TODO: Do something better like forwarding the error to the user.
    LOG_J(LS_ERROR, this) << "TCP accept failed with error "
                          << socket_->GetError();
    return;
  }
  incoming.socket = new talk_base::AsyncTCPSocket(newsocket);
  incoming.socket->SignalReadPacket.connect(this, &TCPPort::OnReadPacket);

  LOG_J(LS_VERBOSE, this) << "Accepted connection from "
                          << incoming.addr.ToString();
  incoming_.push_back(incoming);

  // Prime a read event in case data is waiting
  newsocket->SignalReadEvent(newsocket);
}

talk_base::AsyncTCPSocket* TCPPort::GetIncoming(
    const talk_base::SocketAddress& addr, bool remove) {
  talk_base::AsyncTCPSocket* socket = NULL;
  for (std::list<Incoming>::iterator it = incoming_.begin();
       it != incoming_.end(); ++it) {
    if (it->addr == addr) {
      socket = it->socket;
      if (remove)
        incoming_.erase(it);
      break;
    }
  }
  return socket;
}

void TCPPort::OnReadPacket(const char* data, size_t size,
                           const talk_base::SocketAddress& remote_addr,
                            talk_base::AsyncPacketSocket* socket) {
  Port::OnReadPacket(data, size, remote_addr);
}

TCPConnection::TCPConnection(TCPPort* port, const Candidate& candidate,
                             talk_base::AsyncTCPSocket* socket)
    : Connection(port, 0, candidate), socket_(socket), error_(0) {
  bool outgoing = (socket_ == NULL);
  if (outgoing) {
    // TODO: Handle failures here (unlikely since TCP)
    socket_ = static_cast<talk_base::AsyncTCPSocket*>(port->CreatePacketSocket(
        (candidate.protocol() == "ssltcp") ? PROTO_SSLTCP : PROTO_TCP));
  } else {
    // Incoming connections should match the network address
    ASSERT(socket_->GetLocalAddress().EqualIPs(port->address_));
  }
  socket_->SignalReadPacket.connect(this, &TCPConnection::OnReadPacket);
  socket_->SignalClose.connect(this, &TCPConnection::OnClose);
  if (outgoing) {
    set_connected(false);
    talk_base::SocketAddress local_address(port->address_.ip(), 0);
    socket_->SignalConnect.connect(this, &TCPConnection::OnConnect);
    socket_->Bind(local_address);
    socket_->Connect(candidate.address());
    LOG_J(LS_VERBOSE, this) << "Connecting from " << local_address.ToString()
                            << " to " << candidate.address().ToString();
  }
}

TCPConnection::~TCPConnection() {
  delete socket_;
}

int TCPConnection::Send(const void* data, size_t size) {
  if (write_state() != STATE_WRITABLE) {
    // TODO: Should STATE_WRITE_TIMEOUT return a non-blocking error?
    error_ = EWOULDBLOCK;
    return SOCKET_ERROR;
  }
  int sent = socket_->Send(data, size);
  if (sent < 0) {
    error_ = socket_->GetError();
  } else {
    send_rate_tracker_.Update(sent);
  }
  return sent;
}

int TCPConnection::GetError() {
  return error_;
}

void TCPConnection::OnConnect(talk_base::AsyncTCPSocket* socket) {
  ASSERT(socket == socket_);
  LOG_J(LS_VERBOSE, this) << "Connection established to "
                          << socket->GetRemoteAddress().ToString();
  set_connected(true);
}

void TCPConnection::OnClose(talk_base::AsyncTCPSocket* socket, int error) {
  ASSERT(socket == socket_);
  LOG_J(LS_VERBOSE, this) << "Connection closed with error " << error;
  set_connected(false);
  set_write_state(STATE_WRITE_TIMEOUT);
}

void TCPConnection::OnReadPacket(const char* data, size_t size,
                                 const talk_base::SocketAddress& remote_addr,
                                 talk_base::AsyncPacketSocket* socket) {
  ASSERT(socket == socket_);
  Connection::OnReadPacket(data, size);
}

}  // namespace cricket
