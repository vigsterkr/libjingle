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

#include "talk/base/asyncudpsocket.h"
#include "talk/base/logging.h"

namespace talk_base {

const int BUF_SIZE = 64 * 1024;

AsyncUDPSocket::AsyncUDPSocket(AsyncSocket* socket)
    : AsyncPacketSocket(socket) {
  ASSERT(socket_ != NULL);
  size_ = BUF_SIZE;
  buf_ = new char[size_];

  // The socket should start out readable but not writable.
  socket_->SignalReadEvent.connect(this, &AsyncUDPSocket::OnReadEvent);
}

AsyncUDPSocket::~AsyncUDPSocket() {
  delete [] buf_;
}

void AsyncUDPSocket::OnReadEvent(AsyncSocket* socket) {
  ASSERT(socket == socket_);

  SocketAddress remote_addr;
  int len = socket_->RecvFrom(buf_, size_, &remote_addr);
  if (len < 0) {
    // An error here typically means we got an ICMP error in response to our
    // send datagram, indicating the remote address was unreachable.
    // When doing ICE, this kind of thing will often happen.
    // TODO: Do something better like forwarding the error to the user.
    SocketAddress local_addr = socket_->GetLocalAddress();
    LOG(LS_INFO) << "AsyncUDPSocket[" << local_addr.ToString() << "] "
                 << "receive failed with error " << socket_->GetError();
    return;
  }

  // TODO: Make sure that we got all of the packet.
  // If we did not, then we should resize our buffer to be large enough.
  SignalReadPacket(buf_, (size_t)len, remote_addr, this);
}

}  // namespace talk_base
