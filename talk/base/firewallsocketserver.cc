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

#include <cassert>
#include <algorithm>
#ifdef OSX
#include <errno.h>
#endif

#include "talk/base/firewallsocketserver.h"
#include "talk/base/asyncsocket.h"
#include "talk/base/logging.h"

namespace talk_base {

class FirewallSocket : public AsyncSocketAdapter {
public:
  FirewallSocket(FirewallSocketServer * server, AsyncSocket * socket, int type)
    : AsyncSocketAdapter(socket), server_(server), type_(type) {
  }
  FirewallSocket(FirewallSocketServer * server, Socket * socket, int type)
    : AsyncSocketAdapter(socket), server_(server), type_(type) {
  }

  virtual int Connect(const SocketAddress& addr) {
    if (type_ == SOCK_STREAM) {
      if (!server_->Check(FP_TCP, FD_OUT, addr)) {
        //LOG(INFO) << "FirewallSocket::Connect - Outbound TCP connection denied";
        // Note: handle this asynchronously?
        SetError(EHOSTUNREACH);
        return SOCKET_ERROR;
      }
    }
    return AsyncSocketAdapter::Connect(addr);
  }
  virtual int Send(const void * pv, size_t cb) {
    if (type_ == SOCK_DGRAM) {
      if (!server_->Check(FP_UDP, FD_OUT, GetRemoteAddress())) {
        //LOG(INFO) << "FirewallSocket::Send - Outbound UDP packet dropped";
        return static_cast<int>(cb);
      }
    }
    return AsyncSocketAdapter::Send(pv, cb);
  }
  virtual int SendTo(const void * pv, size_t cb, const SocketAddress& addr) {
    if (type_ == SOCK_DGRAM) {
      if (!server_->Check(FP_UDP, FD_OUT, addr)) {
        //LOG(INFO) << "FirewallSocket::SendTo - Outbound UDP packet dropped";
        return static_cast<int>(cb);
      }
    }
    return AsyncSocketAdapter::SendTo(pv, cb, addr);
  }
  virtual int Recv(void * pv, size_t cb) {
    if (type_ == SOCK_DGRAM) {
      if (!server_->Check(FP_UDP, FD_IN, GetRemoteAddress())) {
        while (true) {
          int res = AsyncSocketAdapter::Recv(pv, cb);
          if (res <= 0)
            return res;
          //LOG(INFO) << "FirewallSocket::Recv - Inbound UDP packet dropped";
        }
      }
    }
    return AsyncSocketAdapter::Recv(pv, cb);
  }
  virtual int RecvFrom(void * pv, size_t cb, SocketAddress * paddr) {
    if (type_ == SOCK_DGRAM) {
      while (true) {
        int res = AsyncSocketAdapter::RecvFrom(pv, cb, paddr);
        if (res <= 0)
          return res;
        if (server_->Check(FP_UDP, FD_IN, *paddr))
          return res;
        //LOG(INFO) << "FirewallSocket::RecvFrom - Inbound UDP packet dropped";
      }
    }
    return AsyncSocketAdapter::RecvFrom(pv, cb, paddr);
  }
  virtual Socket * Accept(SocketAddress *paddr) {
    while (Socket * sock = AsyncSocketAdapter::Accept(paddr)) {
      if (server_->Check(FP_TCP, FD_IN, *paddr))
        return sock;
      sock->Close();
      delete sock;
      //LOG(INFO) << "FirewallSocket::Accept - Inbound TCP connection denied";
    }
    return 0;
  }

private:
  FirewallSocketServer * server_;
  int type_;
};

FirewallSocketServer::FirewallSocketServer(SocketServer * server, FirewallManager * manager) : server_(server), manager_(manager) {
  if (manager_)
    manager_->AddServer(this);
}

FirewallSocketServer::~FirewallSocketServer() {
  if (manager_)
    manager_->RemoveServer(this);
}

void FirewallSocketServer::AddRule(bool allow, FirewallProtocol p, FirewallDirection d, const SocketAddress& addr) {
  Rule r;
  r.allow = allow;
  r.p = p;
  r.d = d;
  r.addr = addr;
  CritScope scope(&crit_);
  rules_.push_back(r);
}

void FirewallSocketServer::ClearRules() {
  CritScope scope(&crit_);
  rules_.clear();
}

bool FirewallSocketServer::Check(FirewallProtocol p, FirewallDirection d, const SocketAddress& addr) {
  CritScope scope(&crit_);
  for (size_t i=0; i<rules_.size(); ++i) {
    const Rule& r = rules_[i];
    if ((r.p != p) && (r.p != FP_ANY))
      continue;
    if ((r.d != d) && (r.d != FD_ANY))
      continue;
    if ((r.addr.ip() != addr.ip()) && !r.addr.IsAny())
      continue;
    if ((r.addr.port() != addr.port()) && (r.addr.port() != 0))
      continue;
    return r.allow;
  }
  return true;
}

Socket* FirewallSocketServer::CreateSocket(int type) {
  return WrapSocket(server_->CreateSocket(type), type);
}

AsyncSocket* FirewallSocketServer::CreateAsyncSocket(int type) {
  return WrapSocket(server_->CreateAsyncSocket(type), type);
}

Socket * FirewallSocketServer::WrapSocket(Socket * sock, int type) {
  if (!sock)
    return NULL;
  return new FirewallSocket(this, sock, type);
}

AsyncSocket * FirewallSocketServer::WrapSocket(AsyncSocket * sock, int type) {
  if (!sock)
    return NULL;
  return new FirewallSocket(this, sock, type);
}

FirewallManager::FirewallManager() {
}

FirewallManager::~FirewallManager() {
  assert(servers_.empty());
}

void FirewallManager::AddServer(FirewallSocketServer * server) {
  CritScope scope(&crit_);
  servers_.push_back(server);
}

void FirewallManager::RemoveServer(FirewallSocketServer * server) {
  CritScope scope(&crit_);
  servers_.erase(std::remove(servers_.begin(), servers_.end(), server), servers_.end());
}

void FirewallManager::AddRule(bool allow, FirewallProtocol p, FirewallDirection d, const SocketAddress& addr) {
  CritScope scope(&crit_);
  for (std::vector<FirewallSocketServer *>::const_iterator it = servers_.begin(); it != servers_.end(); ++it) {
    (*it)->AddRule(allow, p, d, addr);
  }
}

void FirewallManager::ClearRules() {
  CritScope scope(&crit_);
  for (std::vector<FirewallSocketServer *>::const_iterator it = servers_.begin(); it != servers_.end(); ++it) {
    (*it)->ClearRules();
  }
}

} // namespace talk_base
