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

#ifndef TALK_BASE_WIN32SOCKETSERVER_H__
#define TALK_BASE_WIN32SOCKETSERVER_H__

#ifdef WIN32

#include "talk/base/messagequeue.h"
#include "talk/base/socketserver.h"
#include "talk/base/socketfactory.h"
#include "talk/base/socket.h"
#include "talk/base/asyncsocket.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// Win32Socket
///////////////////////////////////////////////////////////////////////////////

class Win32Socket : public talk_base::AsyncSocket {
public:
  Win32Socket();
  virtual ~Win32Socket();

  int Attach(SOCKET s);
  void SetTimeout(int ms);

  // AsyncSocket Interface
  virtual SocketAddress GetLocalAddress() const;
  virtual SocketAddress GetRemoteAddress() const;
  virtual int Bind(const SocketAddress& addr);
  virtual int Connect(const SocketAddress& addr);
  virtual int Send(const void *pv, size_t cb);
  virtual int SendTo(const void *pv, size_t cb, const SocketAddress& addr);
  virtual int Recv(void *pv, size_t cb);
  virtual int RecvFrom(void *pv, size_t cb, SocketAddress *paddr);
  virtual int Listen(int backlog);
  virtual Socket *Accept(SocketAddress *paddr);
  virtual int Close();
  virtual int GetError() const;
  virtual void SetError(int error);
  virtual ConnState GetState() const;
  virtual int EstimateMTU(uint16* mtu);
  virtual int SetOption(Option opt, int value);

private:
  bool Create(long events);
  void UpdateLastError();
  
  int DoConnect(const sockaddr_in& addr);
  void OnSocketNotify(int event, int error);
  void OnDnsNotify(int ip, int error);

  sockaddr_in addr_;         // address that we connected to (see DoConnect)
  SOCKET socket_;
  int error_;
  uint32 connect_time_;
  ConnState state_;
  bool signal_close_;

  class EventSink;
  friend class EventSink;
  EventSink * sink_;

  struct DnsLookup;
  DnsLookup * dns_;
};

///////////////////////////////////////////////////////////////////////////////
// Win32SocketServer
///////////////////////////////////////////////////////////////////////////////

class Win32SocketServer : public SocketServer {
public:
  Win32SocketServer(MessageQueue *message_queue);
  virtual ~Win32SocketServer();

  // SocketServer Interface
  virtual Socket* CreateSocket(int type);
  virtual AsyncSocket* CreateAsyncSocket(int type);
  virtual bool Wait(int cms, bool process_io);
  virtual void WakeUp();

  void Pump();

private:
  void CreateDummyWindow();

  MessageQueue *message_queue_;
  HWND hwnd_;
};

///////////////////////////////////////////////////////////////////////////////

} // namespace talk_base

#endif  // WIN32

#endif  // TALK_BASE_WIN32SOCKETSERVER_H__
