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
#include <iostream>
#include <cassert>
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/helpers.h"
#include "talk/p2p/base/stunport.h"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std {
  using ::strerror;
}
#endif

#ifdef POSIX
#include <errno.h>
#endif // POSIX

namespace cricket {

const int KEEPALIVE_DELAY = 10 * 1000; // 10 seconds - sort timeouts
const int RETRY_DELAY = 50; // 50ms, from ICE spec
const uint32 RETRY_TIMEOUT = 50 * 1000; // ICE says 50 secs

// Handles a binding request sent to the STUN server.
class StunPortBindingRequest : public StunRequest {
public:
  StunPortBindingRequest(StunPort* port, bool keep_alive,
                         const talk_base::SocketAddress& addr)
    : port_(port), keep_alive_(keep_alive), server_addr_(addr) {
    start_time_ = talk_base::GetMillisecondCount();
  }

  virtual ~StunPortBindingRequest() {
  }

  const talk_base::SocketAddress& server_addr() const { return server_addr_; }

  virtual void Prepare(StunMessage* request) {
    request->SetType(STUN_BINDING_REQUEST);
  }

  virtual void OnResponse(StunMessage* response) {
    const StunAddressAttribute* addr_attr =
        response->GetAddress(STUN_ATTR_MAPPED_ADDRESS);
    if (!addr_attr) {
      LOG(LERROR) << "Binding response missing mapped address.";
    } else if (addr_attr->family() != 1) {
      LOG(LERROR) << "Binding address has bad family";
    } else {
      talk_base::SocketAddress addr(addr_attr->ip(), addr_attr->port());
      port_->AddAddress(addr, "udp", true);
    }

    // We will do a keep-alive regardless of whether this request suceeds.
    // This should have almost no impact on network usage.
    if (keep_alive_) {
      port_->requests_.SendDelayed(
          new StunPortBindingRequest(port_, true, server_addr_),
          KEEPALIVE_DELAY);
    }
  }

  virtual void OnErrorResponse(StunMessage* response) {
    const StunErrorCodeAttribute* attr = response->GetErrorCode();
    if (!attr) {
      LOG(LERROR) << "Bad allocate response error code";
    } else {
      LOG(LERROR) << "Binding error response:"
                 << " class=" << attr->error_class()
                 << " number=" << attr->number()
                 << " reason='" << attr->reason() << "'";
    }

    port_->SignalAddressError(port_);

    if (keep_alive_ 
        && (talk_base::GetMillisecondCount() - start_time_ <= RETRY_TIMEOUT)) {
      port_->requests_.SendDelayed(
          new StunPortBindingRequest(port_, true, server_addr_),
          KEEPALIVE_DELAY);
    }
  }

  virtual void OnTimeout() {
    LOG(LERROR) << "Binding request timed out from " 
      << port_->GetLocalAddress().ToString() 
      << " (" << port_->network()->name() << ")";

    port_->SignalAddressError(port_);

    if (keep_alive_ 
        && (talk_base::GetMillisecondCount() - start_time_ <= RETRY_TIMEOUT)) {
      port_->requests_.SendDelayed(
          new StunPortBindingRequest(port_, true, server_addr_),
          RETRY_DELAY);
    }
  }

private:
  StunPort* port_;
  bool keep_alive_;
  talk_base::SocketAddress server_addr_;
  uint32 start_time_;
};

const std::string STUN_PORT_TYPE("stun");

StunPort::StunPort(talk_base::Thread* thread, talk_base::SocketFactory* factory, 
                   talk_base::Network* network,
                   const talk_base::SocketAddress& local_addr,
                   const talk_base::SocketAddress& server_addr)
  : UDPPort(thread, STUN_PORT_TYPE, factory, network),
    server_addr_(server_addr), requests_(thread), error_(0) {

  socket_ = CreatePacketSocket(PROTO_UDP);
  socket_->SignalReadPacket.connect(this, &StunPort::OnReadPacket);
  if (socket_->Bind(local_addr) < 0)
    PLOG(LERROR, socket_->GetError()) << "bind";

  requests_.SignalSendPacket.connect(this, &StunPort::OnSendPacket);
}

StunPort::~StunPort() {
  delete socket_;
}

void StunPort::PrepareAddress() {
  // We will keep pinging the stun server to make sure our NAT pin-hole stays
  // open during the call.
  requests_.Send(new StunPortBindingRequest(this, true, server_addr_));
}

void StunPort::PrepareSecondaryAddress() {
  ASSERT(!server_addr2_.IsAny());
  requests_.Send(new StunPortBindingRequest(this, false, server_addr2_));
}

int StunPort::SendTo(
    const void* data, size_t size, const talk_base::SocketAddress& addr, 
    bool payload) {
  int sent = socket_->SendTo(data, size, addr);
  if (sent < 0)
    error_ = socket_->GetError();
  return sent;
}

int StunPort::SetOption(talk_base::Socket::Option opt, int value) {
  return socket_->SetOption(opt, value);
}

int StunPort::GetError() {
  return error_;
}

void StunPort::OnReadPacket(
    const char* data, size_t size, const talk_base::SocketAddress& remote_addr,
    talk_base::AsyncPacketSocket* socket) {
  assert(socket == socket_);

  // Look for a response to a binding request.
  if (requests_.CheckResponse(data, size))
    return;

  // Process this data packet in the normal manner.
  UDPPort::OnReadPacket(data, size, remote_addr);
}

void StunPort::OnSendPacket(const void* data, size_t size, StunRequest* req) {
  StunPortBindingRequest* sreq = static_cast<StunPortBindingRequest*>(req);
  if (socket_->SendTo(data, size, sreq->server_addr()) < 0)
    PLOG(LERROR, socket_->GetError()) << "sendto";
}

} // namespace cricket
