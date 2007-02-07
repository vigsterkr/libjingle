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

#include <iostream>
#include <cassert>
#include "talk/base/natsocketfactory.h"

namespace talk_base {

class NATSocket : public AsyncSocket {
public:
  NATSocket(Socket* socket, const SocketAddress& server_addr)
      : async_(false), connected_(false), server_addr_(server_addr),
        socket_(socket), buf_(0), size_(0) {
  }

  NATSocket(AsyncSocket* socket, const SocketAddress& server_addr)
      : async_(true), connected_(false), server_addr_(server_addr),
        socket_(socket), buf_(0), size_(0) {
    socket->SignalReadEvent.connect(this, &NATSocket::OnReadEvent);
    socket->SignalWriteEvent.connect(this, &NATSocket::OnWriteEvent);
  }

  virtual ~NATSocket() {
    delete socket_;
    delete buf_;
  }

  SocketAddress GetLocalAddress() const {
    return socket_->GetLocalAddress();
  }

  SocketAddress GetRemoteAddress() const {
    return remote_addr_; // will be ANY if not connected
  }

  int Bind(const SocketAddress& addr) {
    return socket_->Bind(addr);
  }

  int Connect(const SocketAddress& addr) {
    connected_ = true;
    remote_addr_ = addr;
    return 0;
  }

  int Send(const void *pv, size_t cb) {
    assert(connected_);
    return SendInternal(pv, cb, remote_addr_);
  }

  int SendTo(const void *pv, size_t cb, const SocketAddress& addr) {
    assert(!connected_);
    return SendInternal(pv, cb, addr);
  }

  int SendInternal(const void *pv, size_t cb, const SocketAddress& addr) {
    size_t size = cb + addr.Size_();
    char* buf = new char[size];
    Encode(static_cast<const char*>(pv), cb, buf, size, addr);

    int result = socket_->SendTo(buf, size, server_addr_);
    delete buf;
    if (result < 0) {
      return result;
    } else {
      assert(result == static_cast<int>(size)); // TODO: This isn't fair.
      return (int)((size_t)result - addr.Size_());
    }
  }

  int Recv(void *pv, size_t cb) {
    SocketAddress addr;
    return RecvFrom(pv, cb, &addr);
  }

  int RecvFrom(void *pv, size_t cb, SocketAddress *paddr) {
    // Make sure we have enough room to read the requested amount plus the
    // header address.
    SocketAddress remote_addr;
    Grow(cb + remote_addr.Size_());

    // Read the packet from the socket.
    int result = socket_->RecvFrom(buf_, size_, &remote_addr);
    if (result < 0)
      return result;
    assert(remote_addr == server_addr_);

    // TODO: we need better framing so that we know how many bytes we can
    // return before we need to read the next address.  For UDP, this will be
    // fine as long as the reader always reads everything in the packet.
    assert((size_t)result < size_);

    // Decode the wire packet into the actual results.
    SocketAddress real_remote_addr;
    size_t real_size = cb;
    Decode(buf_, result, pv, &real_size, &real_remote_addr);

    // Make sure this packet should be delivered before returning it.
    if (!connected_ || (real_remote_addr == remote_addr_)) {
      if (paddr)
        *paddr = real_remote_addr;
      return (int)real_size;
    } else {
      std::cerr << "Dropping packet from unknown remote address: "
                << real_remote_addr.ToString() << std::endl;
      return 0; // Tell the caller we didn't read anything
    }
  }

  int Close() {
    connected_ = false;
    remote_addr_ = SocketAddress();
    return socket_->Close();
  }

  int Listen(int backlog) {
    assert(false); // not yet implemented
    return 0;
  }

  Socket* Accept(SocketAddress *paddr) {
    assert(false); // not yet implemented
    return 0;
  }

  AsyncSocket* asyncsocket() {
    assert(async_);
    return static_cast<AsyncSocket*>(socket_);
  }

  int GetError() const { return socket_->GetError(); }
  void SetError(int error) { socket_->SetError(error); }

  ConnState GetState() const { return connected_ ? CS_CONNECTED : CS_CLOSED; }

  virtual int EstimateMTU(uint16* mtu) { return socket_->EstimateMTU(mtu); }
  virtual int SetOption(Option opt, int value) { return socket_->SetOption(opt, value); }

  void OnReadEvent(AsyncSocket* socket) {
    assert(socket == socket_);
    SignalReadEvent(this);
  }

  void OnWriteEvent(AsyncSocket* socket) {
    assert(socket == socket_);
    SignalWriteEvent(this);
  }

private:
  // Makes sure the buffer is at least the given size.
  void Grow(size_t new_size) {
    if (size_ < new_size) {
      delete buf_;
      size_ = new_size;
      buf_ = new char[size_];
    }
  }

  // Encodes the given data and intended remote address into a packet to send
  // to the NAT server.
  void Encode(const char* data, size_t data_size, char* buf, size_t buf_size,
	      const SocketAddress& remote_addr) {
    assert(buf_size == data_size + remote_addr.Size_());
    remote_addr.Write_(buf, (int)buf_size);
    std::memcpy(buf + remote_addr.Size_(), data, data_size);
  }

  // Decodes the given packet from the NAT server into the actual remote
  // address and data.
  void Decode(const char* data, size_t data_size, void* buf, size_t* buf_size,
	      SocketAddress* remote_addr) {
    assert(data_size >= remote_addr->Size_());
    assert(data_size <= *buf_size + remote_addr->Size_());
    remote_addr->Read_(data, (int)data_size);
    *buf_size = data_size - remote_addr->Size_();
    std::memcpy(buf, data + remote_addr->Size_(), *buf_size);
  }

  bool async_;
  bool connected_;
  SocketAddress remote_addr_;
  SocketAddress server_addr_;   // address of the NAT server
  Socket* socket_;
  char* buf_;
  size_t size_;
};

NATSocketFactory::NATSocketFactory(
    SocketFactory* factory, const SocketAddress& nat_addr)
    : factory_(factory), nat_addr_(nat_addr) {
}

Socket* NATSocketFactory::CreateSocket(int type) {
  assert(type == SOCK_DGRAM); // TCP is not yet suported
  return new NATSocket(factory_->CreateSocket(type), nat_addr_);
}

AsyncSocket* NATSocketFactory::CreateAsyncSocket(int type) {
  assert(type == SOCK_DGRAM); // TCP is not yet suported
  return new NATSocket(factory_->CreateAsyncSocket(type), nat_addr_);
}

} // namespace talk_base
