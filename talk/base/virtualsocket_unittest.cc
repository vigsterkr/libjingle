// Copyright 2006, Google Inc.

#include <complex>
#include <iostream>
#include <cassert>

#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/base/testclient.h"
#include "talk/base/time.h"

#ifdef POSIX
extern "C" {
#include <errno.h>
}
#endif // POSIX

using namespace talk_base;

void test_basic(Thread* thread, VirtualSocketServer* ss) {
  std::cout << "basic: ";
  std::cout.flush();

  SocketAddress addr1(ss->GetNextIP(), 5000);
  AsyncUDPSocket* socket = CreateAsyncUDPSocket(ss);
  socket->Bind(addr1);

  TestClient* client1 = new TestClient(socket);
  TestClient* client2 = new TestClient(CreateAsyncUDPSocket(ss));

  SocketAddress addr2;
  client2->SendTo("foo", 3, addr1);
  client1->CheckNextPacket("foo", 3, &addr2);

  SocketAddress addr3;
  client1->SendTo("bizbaz", 6, addr2);
  client2->CheckNextPacket("bizbaz", 6, &addr3);
  assert(addr3 == addr1);

  for (int i = 0; i < 10; i++) {
    client2 = new TestClient(CreateAsyncUDPSocket(ss));

    SocketAddress addr4;
    client2->SendTo("foo", 3, addr1);
    client1->CheckNextPacket("foo", 3, &addr4);
    assert((addr4.ip() == addr2.ip()) && (addr4.port() == addr2.port() + 1));

    SocketAddress addr5;
    client1->SendTo("bizbaz", 6, addr4);
    client2->CheckNextPacket("bizbaz", 6, &addr5);
    assert(addr5 == addr1);

    addr2 = addr4;
  }

  std::cout << "PASS" << std::endl;
}

// Sends at a constant rate but with random packet sizes.
struct Sender : public MessageHandler {
  Sender(Thread* th, AsyncUDPSocket* s, uint32 rt)
      : thread(th), socket(s), done(false), rate(rt), count(0) {
    last_send = GetMillisecondCount();
    thread->PostDelayed(NextDelay(), this, 1);
  }

  uint32 NextDelay() {
    uint32 size = (rand() % 4096) + 1;
    return 1000 * size / rate;
  }

  void OnMessage(Message* pmsg) {
    assert(pmsg->message_id == 1);

    if (done)
      return;

    uint32 cur_time = GetMillisecondCount();
    uint32 delay = cur_time - last_send;
    uint32 size = rate * delay / 1000;
    size = std::min(size, uint32(4096));
    size = std::max(size, uint32(4));

    count += size;
    *reinterpret_cast<uint32*>(dummy) = cur_time;
    socket->Send(dummy, size);

    last_send = cur_time;
    thread->PostDelayed(NextDelay(), this, 1);
  }

  Thread* thread;
  AsyncUDPSocket* socket;
  bool done;
  uint32 rate; // bytes per second
  uint32 count;
  uint32 last_send;
  char dummy[4096];
};

struct Receiver : public MessageHandler, public sigslot::has_slots<> {
  Receiver(Thread* th, AsyncUDPSocket* s, uint32 bw)
      : thread(th), socket(s), bandwidth(bw), done(false), count(0),
        sec_count(0), sum(0), sum_sq(0), samples(0) {
    socket->SignalReadPacket.connect(this, &Receiver::OnReadPacket);
    thread->PostDelayed(1000, this, 1);
  }

  ~Receiver() {
    thread->Clear(this);
  }

  void OnReadPacket(
      const char* data, size_t size, const SocketAddress& remote_addr, 
      AsyncPacketSocket* s) {
    assert(s == socket);
    assert(size >= 4);

    count += size;
    sec_count += size;

    uint32 send_time = *reinterpret_cast<const uint32*>(data);
    uint32 recv_time = GetMillisecondCount();
    uint32 delay = recv_time - send_time;
    sum += delay;
    sum_sq += delay * delay;
    samples += 1;
  }

  void OnMessage(Message* pmsg) {
    assert(pmsg->message_id == 1);
    // It is always possible for us to receive more than expected because
    // packets can be further delayed in delivery.
    if (bandwidth > 0)
      assert(sec_count <= 5 * bandwidth / 4);
    sec_count = 0;
    thread->PostDelayed(1000, this, 1);
  }  

  Thread* thread;
  AsyncUDPSocket* socket;
  uint32 bandwidth;
  bool done;
  uint32 count;
  uint32 sec_count;
  double sum;
  double sum_sq;
  uint32 samples;
};

void test_bandwidth(Thread* thread, VirtualSocketServer* ss) {
  std::cout << "bandwidth: ";
  std::cout.flush();

  AsyncUDPSocket* send_socket = CreateAsyncUDPSocket(ss);
  AsyncUDPSocket* recv_socket = CreateAsyncUDPSocket(ss);
  assert(send_socket->Bind(SocketAddress(ss->GetNextIP(), 1000)) >= 0);
  assert(recv_socket->Bind(SocketAddress(ss->GetNextIP(), 1000)) >= 0);
  assert(send_socket->Connect(recv_socket->GetLocalAddress()) >= 0);

  uint32 bandwidth = 64 * 1024;
  ss->set_bandwidth(bandwidth);

  Sender sender(thread, send_socket, 80 * 1024);
  Receiver receiver(thread, recv_socket, bandwidth);

  Thread* pthMain = Thread::Current();
  pthMain->ProcessMessages(5000);
  sender.done = true;
  pthMain->ProcessMessages(5000);

  assert(receiver.count >= 5 * 3 * bandwidth / 4);
  assert(receiver.count <= 6 * bandwidth); // queue could drain for 1 sec

  delete send_socket;
  delete recv_socket;

  ss->set_bandwidth(0);

  std::cout << "PASS" << std::endl;
}

void test_delay(Thread* thread, VirtualSocketServer* ss) {
  std::cout << "delay: ";
  std::cout.flush();

  uint32 mean = 2000;
  uint32 stddev = 500;

  ss->set_delay_mean(mean);
  ss->set_delay_stddev(stddev);
  ss->UpdateDelayDistribution();

  AsyncUDPSocket* send_socket = CreateAsyncUDPSocket(ss);
  AsyncUDPSocket* recv_socket = CreateAsyncUDPSocket(ss);
  assert(send_socket->Bind(SocketAddress(ss->GetNextIP(), 1000)) >= 0);
  assert(recv_socket->Bind(SocketAddress(ss->GetNextIP(), 1000)) >= 0);
  assert(send_socket->Connect(recv_socket->GetLocalAddress()) >= 0);

  Sender sender(thread, send_socket, 64 * 1024);
  Receiver receiver(thread, recv_socket, 0);

  Thread* pthMain = Thread::Current();
  pthMain->ProcessMessages(5000);
  sender.done = true;
  pthMain->ProcessMessages(5000);

  double sample_mean = receiver.sum / receiver.samples;
  double num = receiver.sum_sq - 2 * sample_mean * receiver.sum +
       receiver.samples * sample_mean * sample_mean;
  double sample_stddev = std::sqrt(num / (receiver.samples - 1));
std::cout << "mean=" << sample_mean << " dev=" << sample_stddev << std::endl;

  assert(0.9 * mean <= sample_mean);
  assert(sample_mean <= 1.1 * mean);
  assert(0.9 * stddev <= sample_stddev);
  assert(sample_stddev <= 1.1 * stddev);

  delete send_socket;
  delete recv_socket;

  ss->set_delay_mean(0);
  ss->set_delay_stddev(0);
  ss->UpdateDelayDistribution();

  std::cout << "PASS" << std::endl;
}

int main(int argc, char* argv) {
  Thread *pthMain = Thread::Current(); 
  VirtualSocketServer* ss = new VirtualSocketServer();
  pthMain->set_socketserver(ss);

  test_basic(pthMain, ss);
  test_bandwidth(pthMain, ss);
  test_delay(pthMain, ss);

  return 0;
}
