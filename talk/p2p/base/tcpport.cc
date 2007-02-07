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

#ifdef POSIX
extern "C" {
#include <errno.h>
}
#endif // POSIX

#include <cassert>
#include <iostream>

#include "talk/base/common.h"
#include "talk/base/logging.h"
#ifdef WIN32
#include "talk/base/winfirewall.h"
#endif // WIN32
#include "talk/p2p/base/tcpport.h"

namespace cricket {

#ifdef WIN32
static talk_base::WinFirewall win_firewall;
#endif  // WIN32

TCPPort::TCPPort(talk_base::Thread* thread, talk_base::SocketFactory* factory, 
                 talk_base::Network* network, 
                 const talk_base::SocketAddress& address)
    : Port(thread, LOCAL_PORT_TYPE, factory, network), address_(address),
      incoming_only_(address_.port() != 0), error_(0) {
  socket_ = thread->socketserver()->CreateAsyncSocket(SOCK_STREAM);
  socket_->SignalReadEvent.connect(this, &TCPPort::OnAcceptEvent);
  if (socket_->Bind(address_) < 0) {
    LOG_F(LS_ERROR) << "Bind error: " << socket_->GetError();
  }
}

TCPPort::~TCPPort() {
  delete socket_;
}

Connection* TCPPort::CreateConnection(const Candidate& address,
                                      CandidateOrigin origin) {
  // We only support TCP protocols
  if ((address.protocol() != "tcp") && (address.protocol() != "ssltcp"))
    return 0;

  // We can't accept TCP connections incoming on other ports
  if (origin == ORIGIN_OTHER_PORT)
    return 0;

  // Check if we are allowed to make outgoing TCP connections
  if (incoming_only_ && (origin == ORIGIN_MESSAGE))
    return 0;

  // We don't know how to act as an ssl server yet
  if ((address.protocol() == "ssltcp") && (origin == ORIGIN_THIS_PORT))
    return 0;

  TCPConnection* conn = 0;
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
  assert(socket_);

  bool allow_listen = true;
#ifdef WIN32
  if (win_firewall.Initialize()) {
    char module_path[MAX_PATH + 1] = { 0 };
    ::GetModuleFileNameA(NULL, module_path, MAX_PATH);
    if (win_firewall.Enabled() && !win_firewall.Authorized(module_path)) {
      allow_listen = false;
    }
  }
#endif // WIN32
  if (!allow_listen) {
    LOG_F(LS_VERBOSE) << "Not listening due to firewall restrictions";
  } else if (socket_->Listen(5) < 0) {
    LOG_F(LS_ERROR) << "Listen error: " << socket_->GetError();
  }
  // Note: We still add the address, since otherwise the remote side won't
  // recognize our incoming TCP connections.
  AddAddress(socket_->GetLocalAddress(), "tcp", true);
}

int TCPPort::SendTo(const void* data, size_t size, 
                    const talk_base::SocketAddress& addr, bool payload) {
  talk_base::AsyncTCPSocket * socket = 0;

  if (TCPConnection * conn = static_cast<TCPConnection*>(GetConnection(addr))) {
    socket = conn->socket();
  } else {
    socket = GetIncoming(addr);
  }
  if (!socket) {
    LOG_F(LS_ERROR) << "Unknown destination: " << addr.ToString();
    return -1; // TODO: Set error_
  }

  //LOG_F(INFO) << "(" << size << ", " << addr.ToString() << ")";

  int sent = socket->Send(data, size);
  if (sent < 0) {
    error_ = socket->GetError();
    LOG_F(LS_ERROR) << "(" << size << ", " << addr.ToString()
                    << ") Send error: " << error_;
  }
  return sent;
}

int TCPPort::SetOption(talk_base::Socket::Option opt, int value) {
  return socket_->SetOption(opt, value);
}

int TCPPort::GetError() {
  assert(socket_);
  return error_;
}

void TCPPort::OnAcceptEvent(talk_base::AsyncSocket* socket) {
  assert(socket == socket_);

  Incoming incoming;
  talk_base::AsyncSocket * newsocket 
    = static_cast<talk_base::AsyncSocket *>(socket->Accept(&incoming.addr));
  if (!newsocket) {
    // TODO: Do something better like forwarding the error to the user.
    LOG_F(LS_ERROR) << "Accept error: " << socket_->GetError();
    return;
  }
  incoming.socket = new talk_base::AsyncTCPSocket(newsocket);
  incoming.socket->SignalReadPacket.connect(this, &TCPPort::OnReadPacket);

  LOG_F(LS_VERBOSE) << "(" << incoming.addr.ToString() << ")";
  incoming_.push_back(incoming);

  // Prime a read event in case data is waiting
  newsocket->SignalReadEvent(newsocket);
}

talk_base::AsyncTCPSocket * TCPPort::GetIncoming(
    const talk_base::SocketAddress& addr, bool remove) {
  talk_base::AsyncTCPSocket * socket = 0;
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
  bool outgoing = (socket_ == 0);
  if (outgoing) {
    socket_ = static_cast<talk_base::AsyncTCPSocket *>(port->CreatePacketSocket(
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
    LOG_F(LS_VERBOSE) << "Connecting from " << local_address.ToString()
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
    sent_total_bytes_ += sent;
  }
  return sent;
}

int TCPConnection::GetError() {
  return error_;
}

TCPPort* TCPConnection::tcpport() {
  return static_cast<TCPPort*>(port_);
}

void TCPConnection::OnConnect(talk_base::AsyncTCPSocket* socket) {
  assert(socket == socket_);
  LOG_F(LS_VERBOSE) << "(" << socket->GetRemoteAddress().ToString() << ")";
  set_connected(true);
}

void TCPConnection::OnClose(talk_base::AsyncTCPSocket* socket, int error) {
  assert(socket == socket_);
  LOG_F(LS_VERBOSE) << "(" << error << ")";
  set_connected(false);
  set_write_state(STATE_WRITE_TIMEOUT);
}

void TCPConnection::OnReadPacket(const char* data, size_t size, 
                                 const talk_base::SocketAddress& remote_addr,
                                 talk_base::AsyncPacketSocket* socket) {
  assert(socket == socket_);
  //LOG_F(LS_INFO) << "(" << size << ", " << remote_addr.ToString() << ")";
  Connection::OnReadPacket(data, size);
}

} // namespace cricket
