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

#ifndef TALK_P2P_BASE_TCPPORT_H_
#define TALK_P2P_BASE_TCPPORT_H_

#include <string>
#include <list>
#include "talk/base/asynctcpsocket.h"
#include "talk/p2p/base/port.h"

namespace cricket {

class TCPConnection;

extern const std::string LOCAL_PORT_TYPE;  // type of TCP ports

// Communicates using a local TCP port.
//
// This class is designed to allow subclasses to take advantage of the
// connection management provided by this class.  A subclass should take of all
// packet sending and preparation, but when a packet is received, it should
// call this TCPPort::OnReadPacket (3 arg) to dispatch to a connection.
class TCPPort : public Port {
 public:
  static TCPPort* Create(talk_base::Thread* thread,
                         talk_base::SocketFactory* factory,
                         talk_base::Network* network,
                         const talk_base::SocketAddress& local_addr,
                         bool allow_listen) {
    TCPPort* port = new TCPPort(thread, factory, network, local_addr,
                                allow_listen);
    if (!port->Init()) {
      delete port;
      port = NULL;
    }
    return port;
  }
  bool Init();
  virtual ~TCPPort();

  virtual Connection* CreateConnection(const Candidate& address,
                                       CandidateOrigin origin);

  virtual void PrepareAddress();

  virtual int SetOption(talk_base::Socket::Option opt, int value);
  virtual int GetError();

 protected:
  TCPPort(talk_base::Thread* thread, talk_base::SocketFactory* factory,
          talk_base::Network* network, const talk_base::SocketAddress& address,
          bool allow_listen);

  // Handles sending using the local TCP socket.
  virtual int SendTo(const void* data, size_t size,
                     const talk_base::SocketAddress& addr, bool payload);

  // Creates TCPConnection for incoming sockets
  void OnAcceptEvent(talk_base::AsyncSocket* socket);

 private:
  struct Incoming {
    talk_base::SocketAddress addr;
    talk_base::AsyncTCPSocket * socket;
  };

  talk_base::AsyncTCPSocket* GetIncoming(const talk_base::SocketAddress& addr,
                                         bool remove = false);

  // Receives packet signal from the local TCP Socket.
  void OnReadPacket(const char* data, size_t size,
                    const talk_base::SocketAddress& remote_addr,
                    talk_base::AsyncPacketSocket* socket);

  // Note: use this until Network ips are stable, then use network->ip
  talk_base::SocketAddress address_;
  bool incoming_only_;
  bool allow_listen_;
  talk_base::AsyncSocket* socket_;
  int error_;
  std::list<Incoming> incoming_;

  friend class TCPConnection;
};

class TCPConnection : public Connection {
 public:
  // Connection is outgoing unless socket is specified
  TCPConnection(TCPPort* port, const Candidate& candidate,
    talk_base::AsyncTCPSocket* socket = 0);
  virtual ~TCPConnection();

  virtual int Send(const void* data, size_t size);
  virtual int GetError();

  talk_base::AsyncTCPSocket * socket() { return socket_; }

 private:
  void OnConnect(talk_base::AsyncTCPSocket* socket);
  void OnClose(talk_base::AsyncTCPSocket* socket, int error);
  void OnReadPacket(const char* data, size_t size,
                    const talk_base::SocketAddress& remote_addr,
                    talk_base::AsyncPacketSocket* socket);

  talk_base::AsyncTCPSocket* socket_;
  int error_;

  friend class TCPPort;
};

}  // namespace cricket

#endif  // TALK_P2P_BASE_TCPPORT_H_
