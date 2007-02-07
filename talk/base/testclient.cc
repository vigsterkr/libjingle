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

#ifdef POSIX
extern "C" {
#include <errno.h>
}
#endif // POSIX

#include "talk/base/testclient.h"
#include "talk/base/time.h"


namespace talk_base {

// DESIGN: Each packet received is posted to ourselves as a message on the
//         thread given to the constructor.  When we receive the message, we
//         put it into a list of packets.  We take the latter step so that we
//         can wait for a new packet to arrive by calling Get/Dispatch on the
//         thread.

TestClient::TestClient(AsyncPacketSocket* socket, Thread* thread)
      : thread_(thread), socket_(socket) {
  if (!thread_)
    thread_ = Thread::Current();
  packets_ = new std::vector<Packet*>();
  socket_->SignalReadPacket.connect(this, &TestClient::OnPacket);
}

TestClient::~TestClient() {
  delete socket_;
  for (unsigned i = 0; i < packets_->size(); i++)
    delete (*packets_)[i];
}

void TestClient::Send(const char* buf, size_t size) {
  int result = socket_->Send(buf, size);
  if (result < 0) {
    std::cerr << "send: " << std::strerror(errno) << std::endl;
    exit(1);
  }
}

void TestClient::SendTo(
    const char* buf, size_t size, const SocketAddress& dest) {
  int result = socket_->SendTo(buf, size, dest);
  if (result < 0) {
    std::cerr << "sendto: " << std::strerror(errno) << std::endl;
    exit(1);
  }
}

void TestClient::OnPacket(
      const char* buf, size_t size, const SocketAddress& remote_addr,
      AsyncPacketSocket* socket) {
  thread_->Post(this, 0, new Packet(remote_addr, buf, size));
}

void TestClient::OnMessage(Message *pmsg) {
  assert(pmsg->pdata);
  packets_->push_back(new Packet(*static_cast<Packet*>(pmsg->pdata)));
}

TestClient::Packet* TestClient::NextPacket() {

  // If no packets are currently available, we go into a get/dispatch loop for
  // at most 1 second.  If, during the loop, a packet arrives, then we can stop
  // early and return it.
  //
  // Note that the case where no packet arrives is important.  We often want to
  // test that a packet does not arrive.

  if (packets_->size() == 0) {
    uint32 msNext = 1000;
    uint32 msEnd = GetMillisecondCount() + msNext;

    while (true) {
      Message msg;
      if (!thread_->Get(&msg, msNext))
        break;
      thread_->Dispatch(&msg);
    
      uint32 msCur = GetMillisecondCount();
      if (msCur >= msEnd)
        break;
      msNext = msEnd - msCur;

      if (packets_->size() > 0)
        break;
    }
  }

  if (packets_->size() == 0) {
    return 0;
  } else {
    // Return the first packet placed in the queue.
    Packet* packet = packets_->front();
    packets_->erase(packets_->begin());
    return packet;
  }
}

void TestClient::CheckNextPacket(
      const char* buf, size_t size, SocketAddress* addr) {
  Packet* packet = NextPacket();
  assert(packet);
  assert(packet->size == size);
  assert(std::memcmp(packet->buf, buf, size) == 0);
  if (addr)
    *addr = packet->addr;
}

void TestClient::CheckNoPacket() {
  Packet* packet = NextPacket();
  assert(!packet);
}

TestClient::Packet::Packet(const SocketAddress& a, const char* b, size_t s)
    : addr(a), buf(0), size(s) {
  buf = new char[size];
  memcpy(buf, b, size);
}

TestClient::Packet::Packet(const Packet& p)
    : addr(p.addr), buf(0), size(p.size) {
  buf = new char[size];
  memcpy(buf, p.buf, size);
}

TestClient::Packet::~Packet() {
  delete buf;
}

} // namespace talk_base
