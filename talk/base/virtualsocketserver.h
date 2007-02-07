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

#ifndef TALK_BASE_VIRTUALSOCKETSERVER_H__
#define TALK_BASE_VIRTUALSOCKETSERVER_H__

#include <cassert>
#include <deque>
#include <map>

#include "talk/base/messagequeue.h"
#include "talk/base/socketserver.h"

namespace talk_base {

class VirtualSocket;
	
// Simulates a network in the same manner as a loopback interface.  The
// interface can create as many addresses as you want.  All of the sockets
// created by this network will be able to communicate with one another.
class VirtualSocketServer : public SocketServer, public MessageHandler {
public:
  VirtualSocketServer();
  virtual ~VirtualSocketServer();

  // Returns a new IP not used before in this network.
  uint32 GetNextIP();

  // Limits the network bandwidth (maximum bytes per second).  Zero means that
  // all sends occur instantly.
  uint32 bandwidth() { return bandwidth_; }
  void set_bandwidth(uint32 bandwidth) { bandwidth_ = bandwidth; }

  // Limits the total size of packets that will be kept in the send queue,
  // waiting for their turn to be written to the network  Defaults to 64 KB.
  uint32 queue_capacity() { return queue_capacity_; }
  void set_queue_capacity(uint32 queue_capacity) {
    queue_capacity_ = queue_capacity;
  }

  // Controls the (transit) delay for packets sent in the network.  This does
  // not inclue the time required to sit in the send queue.  Both of these
  // values are measured in milliseconds.
  uint32 delay_mean() { return delay_mean_; }
  uint32 delay_stddev() { return delay_stddev_; }
  void set_delay_mean(uint32 delay_mean) { delay_mean_ = delay_mean; }
  void set_delay_stddev(uint32 delay_stddev) {
    delay_stddev_ = delay_stddev;
  }

  // If the (transit) delay parameters are modified, this method should be
  // called to recompute the new distribution.
  void UpdateDelayDistribution();

  // Controls the (uniform) probability that any sent packet is dropped.  This
  // is separate from calculations to drop based on queue size.
  double drop_probability() { return drop_prob_; }
  void set_drop_probability(double drop_prob) {
    assert((0 <= drop_prob) && (drop_prob <= 1));
    drop_prob_ = drop_prob;
  }

  // SocketFactory:
  virtual Socket* CreateSocket(int type);
  virtual AsyncSocket* CreateAsyncSocket(int type);

  // SocketServer:
  virtual bool Wait(int cms, bool process_io);
  virtual void WakeUp();

  // Used to send internal wake-up messages.
  virtual void OnMessage(Message* msg);

  typedef std::pair<double,double> Point;
  typedef std::vector<Point> Function;

private:
  friend class VirtualSocket;

  typedef std::map<SocketAddress, VirtualSocket*> AddressMap;

  MessageQueue* msg_queue_;
  bool fWait_;
  uint32 wait_version_;
  uint32 next_ip_;
  uint16 next_port_;
  AddressMap* bindings_;

  uint32 bandwidth_;
  uint32 queue_capacity_;
  uint32 delay_mean_;
  uint32 delay_stddev_;
  Function* delay_dist_;
  CriticalSection delay_crit_;

  double drop_prob_;

  VirtualSocket* CreateSocketInternal(int type);

  // Attempts to bind the given socket to the given (non-zero) address.
  int Bind(const SocketAddress& addr, VirtualSocket* socket);

  // Binds the given socket to the given (non-zero) IP on an unused port.
  int Bind(VirtualSocket* socket, SocketAddress* addr);

  // Removes the binding for the given socket.
  int Unbind(const SocketAddress& addr, VirtualSocket* socket);

  // Sends the given packet to the socket at the given address (if one exists).
  int Send(VirtualSocket* socket, const void *pv, size_t cb,
      const SocketAddress& local_addr, const SocketAddress& remote_addr);

  // Computes the number of milliseconds required to send a packet of this size.
  uint32 SendDelay(uint32 size);

  // Returns the probability density function for the transit delay.
  Function* GetDelayDistribution();

  // Returns a random transit delay chosen from the appropriate distribution.
  uint32 GetRandomTransitDelay();

  // Basic operations on functions.  Those that return a function also take
  // ownership of the function given (and hence, may modify or delete it).
  Function* Accumulate(Function* f);
  Function* Invert(Function* f);
  Function* Resample(Function* f, double x1, double x2);
  double Evaluate(Function* f, double x);
};

} // namespace talk_base

#endif // TALK_BASE_VIRTUALSOCKETSERVER_H__
