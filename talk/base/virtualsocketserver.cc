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
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include <errno.h>

#include "talk/base/virtualsocketserver.h"
#include "talk/base/common.h"
#include "talk/base/time.h"

namespace talk_base {

const uint32 HEADER_SIZE = 28; // IP + UDP headers

const uint32 MSG_ID_PACKET = 1;
// TODO: Add a message type for new connections.

// Packets are passed between sockets as messages.  We copy the data just like
// the kernel does.
class Packet : public MessageData {
public:
  Packet(const char* data, size_t size, const SocketAddress& from)
        : size_(size), from_(from) {
    assert(data);
    assert(size_ >= 0);
    data_ = new char[size_];
    std::memcpy(data_, data, size_);
  }

  virtual ~Packet() {
    delete data_;
  }

  const char* data() const { return data_; }
  size_t size() const { return size_; }
  const SocketAddress& from() const { return from_; }

  // Remove the first size bytes from the data.
  void Consume(size_t size) {
    assert(size < size_);
    size_ -= size;
    char* new_data = new char[size_];
    std::memcpy(new_data, data_, size);
    delete[] data_;
    data_ = new_data;
  }

private:
  char* data_;
  size_t size_;
  SocketAddress from_;
};

// Implements the socket interface using the virtual network.  Packets are
// passed as messages using the message queue of the socket server.
class VirtualSocket : public AsyncSocket, public MessageHandler {
public:
  VirtualSocket(
      VirtualSocketServer* server, int type, bool async, uint32 ip)
      : server_(server), type_(type), async_(async), connected_(false),
        local_ip_(ip), readable_(true), queue_size_(0) {
    assert((type_ == SOCK_DGRAM) || (type_ == SOCK_STREAM));
    packets_ = new std::vector<Packet*>();
  }

  ~VirtualSocket() {
    Close();

    for (unsigned i = 0; i < packets_->size(); i++)
      delete (*packets_)[i];
    delete packets_;
  }

  SocketAddress GetLocalAddress() const {
    return local_addr_;
  }

  SocketAddress GetRemoteAddress() const {
    return remote_addr_;
  }

  int Bind(const SocketAddress& addr) {
    assert(addr.port() != 0);
    int result = server_->Bind(addr, this);
    if (result >= 0)
      local_addr_ = addr;
    else
      error_ = EADDRINUSE;
    return result;
  }

  int Connect(const SocketAddress& addr) {
    assert(!connected_);
    connected_ = true;
    remote_addr_ = addr;
    assert(type_ == SOCK_DGRAM); // stream not yet implemented
    return 0;
  }

  int Close() {
    if (!local_addr_.IsAny())
      server_->Unbind(local_addr_, this);

    connected_ = false;
    local_addr_ = SocketAddress();
    remote_addr_ = SocketAddress();
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
    // If we have not been assigned a local port, then get one.
    if (local_addr_.IsAny()) {
      local_addr_.SetIP(local_ip_);
      int result = server_->Bind(this, &local_addr_);
      if (result < 0) {
        local_addr_.SetIP(0);
        error_ = EADDRINUSE;
        return result;
      }
    }

    // Send the data in a message to the appropriate socket.
    return server_->Send(this, pv, cb, local_addr_, addr);
  }

  int Recv(void *pv, size_t cb) {
    SocketAddress addr;
    return RecvFrom(pv, cb, &addr);
  }

  int RecvFrom(void *pv, size_t cb, SocketAddress *paddr) {
    // If we don't have a packet, then either error or wait for one to arrive.
    if (packets_->size() == 0) {
      if (async_) {
        error_ = EAGAIN;
        return -1;
      }
      while (packets_->size() == 0) {
        Message msg;
        server_->msg_queue_->Get(&msg);
        server_->msg_queue_->Dispatch(&msg);
      }
    }

    // Return the packet at the front of the queue.
    Packet* packet = packets_->front();
    *paddr = packet->from();
    int size = (int)packet->size();
    if (size <= (int)cb) {
      std::memcpy(pv, packet->data(), size);
      packets_->erase(packets_->begin());
      delete packet;
      return size;
    } else {
      std::memcpy(pv, packet->data(), cb);
      packet->Consume(cb);
      return (int)cb;
    }
  }

  int Listen(int backlog) {
    assert(false); // not yet implemented
    return 0;
  }

  Socket* Accept(SocketAddress *paddr) {
    assert(false); // not yet implemented
    return 0;
  }

  bool readable() { return readable_; }
  void set_readable(bool value) { readable_ = value; }

  bool writable() { return false; }
  void set_writable(bool value) {
    // TODO: Send ourselves messages (delayed after the first) to give them a
    // chance to write.
    assert(false);
  }

  int GetError() const {
    return error_;
  }

  void SetError(int error) {
    error_ = error;
  }

  ConnState GetState() const {
    return connected_ ? CS_CONNECTED : CS_CLOSED;
  }

  int SetOption(Option opt, int value) {
    return 0;
  }

  int EstimateMTU(uint16* mtu) {
    if (!connected_)
      return ENOTCONN;
    else
      return 65536;
  }

  void OnMessage(Message *pmsg) {
    if (pmsg->message_id == MSG_ID_PACKET) {
      assert(pmsg->pdata);
      Packet* packet = static_cast<Packet*>(pmsg->pdata);

      if (!readable_)
        return;

      packets_->push_back(packet);

      if (async_) {
        SignalReadEvent(this);

        // TODO: If the listeners don't want to read this packet now, we will
        // need to send ourselves delayed messages to try again.
        assert(packets_->size() == 0);
      }
    } else {
      assert(false);
    }
  }

private:
  struct QueueEntry {
    uint32 size;
    uint32 done_time;
  };

  typedef std::deque<QueueEntry> SendQueue;

  VirtualSocketServer* server_;
  int type_;
  bool async_;
  bool connected_;
  uint32 local_ip_;
  bool readable_;
  SocketAddress local_addr_;
  SocketAddress remote_addr_;
  std::vector<Packet*>* packets_;
  int error_;
  SendQueue queue_;
  uint32 queue_size_;
  CriticalSection queue_crit_;

  friend class VirtualSocketServer;
};

VirtualSocketServer::VirtualSocketServer()
      : fWait_(false), wait_version_(0), next_ip_(1), next_port_(45000),
        bandwidth_(0), queue_capacity_(64 * 1024), delay_mean_(0),
        delay_stddev_(0), delay_dist_(0), drop_prob_(0.0) {
  msg_queue_ = new MessageQueue(); // uses physical socket server for Wait
  bindings_ = new AddressMap();

  UpdateDelayDistribution();
}

VirtualSocketServer::~VirtualSocketServer() {
  delete bindings_;
  delete msg_queue_;
  delete delay_dist_;
}

uint32 VirtualSocketServer::GetNextIP() {
  return next_ip_++;
}

Socket* VirtualSocketServer::CreateSocket(int type) {
  return CreateSocketInternal(type);
}

AsyncSocket* VirtualSocketServer::CreateAsyncSocket(int type) {
  return CreateSocketInternal(type);
}

VirtualSocket* VirtualSocketServer::CreateSocketInternal(int type) {
  uint32 ip = (next_ip_ > 1) ? next_ip_ - 1 : 1;
  return new VirtualSocket(this, type, true, ip);
}

bool VirtualSocketServer::Wait(int cmsWait, bool process_io) {
  ASSERT(process_io);  // This can't be easily supported.

  uint32 msEnd;
  if (cmsWait != kForever)
    msEnd = GetMillisecondCount() + cmsWait;
  uint32 cmsNext = cmsWait;

  fWait_ = true;
  wait_version_ += 1;

  while (fWait_) {
    Message msg;
    if (!msg_queue_->Get(&msg, cmsNext))
      return true;
    msg_queue_->Dispatch(&msg);

    if (cmsWait != kForever) {
      uint32 msCur = GetMillisecondCount();
      if (msCur >= msEnd)
        return true;
      cmsNext = msEnd - msCur;
    }
  }
  return true;
}

const uint32 MSG_WAKE_UP = 1;

struct WakeUpMessage : public MessageData {
  WakeUpMessage(uint32 ver) : wait_version(ver) {}
  virtual ~WakeUpMessage() {}

  uint32 wait_version;
};

void VirtualSocketServer::WakeUp() {
  msg_queue_->Post(this, MSG_WAKE_UP, new WakeUpMessage(wait_version_));
}

void VirtualSocketServer::OnMessage(Message* pmsg) {
  assert(pmsg->message_id == MSG_WAKE_UP);
  assert(pmsg->pdata);
  WakeUpMessage* wmsg = static_cast<WakeUpMessage*>(pmsg->pdata);
  if (wmsg->wait_version == wait_version_)
    fWait_ = false;
  delete pmsg->pdata;
}

int VirtualSocketServer::Bind(
      const SocketAddress& addr, VirtualSocket* socket) {
  assert(addr.ip() > 0); // don't support any-address right now
  assert(addr.port() > 0);
  assert(socket);

  if (bindings_->find(addr) == bindings_->end()) {
    (*bindings_)[addr] = socket;
    return 0;
  } else {
    return -1;
  }
}

int VirtualSocketServer::Bind(VirtualSocket* socket, SocketAddress* addr) {
  assert(addr->ip() > 0); // don't support any-address right now
  assert(socket);

  for (int i = 0; i < 65536; i++) {
    addr->SetPort(next_port_++);
    if (addr->port() > 0) {
      AddressMap::iterator iter = bindings_->find(*addr);
      if (iter == bindings_->end()) {
        (*bindings_)[*addr] = socket;
        return 0;
      }
    }
  }

  errno = EADDRINUSE; // TODO: is there a better error number?
  return -1;
}

int VirtualSocketServer::Unbind(
      const SocketAddress& addr, VirtualSocket* socket) {
  assert((*bindings_)[addr] == socket);
  bindings_->erase(bindings_->find(addr));
  return 0;
}

static double Random() {
  return double(rand()) / RAND_MAX;
}

int VirtualSocketServer::Send(
    VirtualSocket* socket, const void *pv, size_t cb,
    const SocketAddress& local_addr, const SocketAddress& remote_addr) {

  // See if we want to drop this packet.
  if (Random() < drop_prob_) {
    std::cerr << "Dropping packet: bad luck" << std::endl;
    return 0;
  }

  uint32 cur_time = GetMillisecondCount();
  uint32 send_delay;

  // Determine whether we have enough bandwidth to accept this packet.  To do
  // this, we need to update the send queue.  Once we know it's current size,
  // we know whether we can fit this packet.
  //
  // NOTE: There are better algorithms for maintaining such a queue (such as
  // "Derivative Random Drop"); however, this algorithm is a more accurate
  // simulation of what a normal network would do.
  { 
    CritScope cs(&socket->queue_crit_);

    while ((socket->queue_.size() > 0) &&
           (socket->queue_.front().done_time <= cur_time)) {
      assert(socket->queue_size_ >= socket->queue_.front().size);
      socket->queue_size_ -= socket->queue_.front().size;
      socket->queue_.pop_front();
    }

    VirtualSocket::QueueEntry entry;
    entry.size = uint32(cb) + HEADER_SIZE;

    if (socket->queue_size_ + entry.size > queue_capacity_) {
      std::cerr << "Dropping packet: queue capacity exceeded" << std::endl;
      return 0; // not an error
    }

    socket->queue_size_ += entry.size;
    send_delay = SendDelay(socket->queue_size_);
    entry.done_time = cur_time + send_delay;
    socket->queue_.push_back(entry);
  }

  // Find the delay for crossing the many virtual hops of the network.
  uint32 transit_delay = GetRandomTransitDelay();

  // Post the packet as a message to be delivered (on our own thread)

  AddressMap::iterator iter = bindings_->find(remote_addr);
  if (iter != bindings_->end()) {
    Packet* p = new Packet(static_cast<const char*>(pv), cb, local_addr);
    uint32 delay = send_delay + transit_delay;
    msg_queue_->PostDelayed(delay, iter->second, MSG_ID_PACKET, p);
  } else {
    std::cerr << "No one listening at " << remote_addr.ToString() << std::endl;
  }
  return (int)cb;
}

uint32 VirtualSocketServer::SendDelay(uint32 size) {
  if (bandwidth_ == 0)
    return 0;
  else
    return 1000 * size / bandwidth_;
}

void PrintFunction(std::vector<std::pair<double,double> >* f) {
  for (uint32 i = 0; i < f->size(); i++)
    std::cout << (*f)[i].first << '\t' << (*f)[i].second << std::endl;
}

void VirtualSocketServer::UpdateDelayDistribution() {
  Function* dist = GetDelayDistribution();
  dist = Resample(Invert(Accumulate(dist)), 0, 1);

  // We take a lock just to make sure we don't leak memory.
  {
    CritScope cs(&delay_crit_);
    delete delay_dist_;
    delay_dist_ = dist;
  }
}

const int NUM_SAMPLES = 100; // 1000;

static double PI = 4 * std::atan(1.0);

static double Normal(double x, double mean, double stddev) {
  double a = (x - mean) * (x - mean) / (2 * stddev * stddev);
  return std::exp(-a) / (stddev * sqrt(2 * PI));
}

#if 0 // static unused gives a warning
static double Pareto(double x, double min, double k) {
  if (x < min)
    return 0;
  else
    return k * std::pow(min, k) / std::pow(x, k+1);
}
#endif

VirtualSocketServer::Function* VirtualSocketServer::GetDelayDistribution() {
  Function* f = new Function();

  if (delay_stddev_ == 0) {

    f->push_back(Point(delay_mean_, 1.0));

  } else {

    double start = 0;
    if (delay_mean_ >= 4 * double(delay_stddev_))
      start = delay_mean_ - 4 * double(delay_stddev_);
    double end = delay_mean_ + 4 * double(delay_stddev_);

    double delay_min = 0;
    if (delay_mean_ >= 1.0 * delay_stddev_)
      delay_min = delay_mean_ - 1.0 * delay_stddev_;

    for (int i = 0; i < NUM_SAMPLES; i++) {
      double x = start + (end - start) * i / (NUM_SAMPLES - 1);
      double y = Normal(x, delay_mean_, delay_stddev_);
      f->push_back(Point(x, y));
    }

  }

  return f;
}

uint32 VirtualSocketServer::GetRandomTransitDelay() {
  double delay = (*delay_dist_)[rand() % delay_dist_->size()].second;
  return uint32(delay);
}

struct FunctionDomainCmp {
  bool operator ()(const VirtualSocketServer::Point& p1, const VirtualSocketServer::Point& p2) {
    return p1.first < p2.first;
  }
  bool operator ()(double v1, const VirtualSocketServer::Point& p2) {
    return v1 < p2.first;
  }
  bool operator ()(const VirtualSocketServer::Point& p1, double v2) {
    return p1.first < v2;
  }
};

VirtualSocketServer::Function* VirtualSocketServer::Accumulate(Function* f) {
  assert(f->size() >= 1);
  double v = 0;
  for (Function::size_type i = 0; i < f->size() - 1; ++i) {
    double dx = (*f)[i].second * ((*f)[i+1].first - (*f)[i].first);
    v = (*f)[i].second = v + dx;
  }
  (*f)[f->size()-1].second = v;
  return f;
}

VirtualSocketServer::Function* VirtualSocketServer::Invert(Function* f) {
  for (Function::size_type i = 0; i < f->size(); ++i)
    std::swap((*f)[i].first, (*f)[i].second);

  std::sort(f->begin(), f->end(), FunctionDomainCmp());
  return f;
}

VirtualSocketServer::Function* VirtualSocketServer::Resample(
    Function* f, double x1, double x2) {
  Function* g = new Function();

  for (int i = 0; i < NUM_SAMPLES; i++) {
    double x = x1 + (x2 - x1) * i / (NUM_SAMPLES - 1);
    double y = Evaluate(f, x);
    g->push_back(Point(x, y));
  }

  delete f;
  return g;
}

double VirtualSocketServer::Evaluate(Function* f, double x) {
  Function::iterator iter =
      std::lower_bound(f->begin(), f->end(), x, FunctionDomainCmp());
  if (iter == f->begin()) {
    return (*f)[0].second;
  } else if (iter == f->end()) {
    assert(f->size() >= 1);
    return (*f)[f->size() - 1].second;
  } else if (iter->first == x) {
    return iter->second;
  } else {
    double x1 = (iter - 1)->first;
    double y1 = (iter - 1)->second;
    double x2 = iter->first;
    double y2 = iter->second;
    return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
  }
}

} // namespace talk_base
