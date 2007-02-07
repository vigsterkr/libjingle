#include "talk/base/helpers.h"
#include "talk/base/host.h"
#include "talk/base/natserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/socketaddress.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/p2p/base/udpport.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/relayserver.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/stunserver.h"
#include <iostream>

using namespace cricket;

const uint32 MSG_CONNECT = 1;
const uint32 MSG_PREP_ADDRESS = 2;
const uint32 MSG_CREATE_CONN = 3;
const uint32 MSG_ACCEPT_CONN = 4;
const uint32 MSG_PING = 5;

Candidate GetCandidate(Port* port) {
  assert(port->candidates().size() == 1);
  return port->candidates()[0];
}

talk_base::SocketAddress GetAddress(Port* port) {
  return GetCandidate(port).address();
}

struct Foo : public talk_base::MessageHandler, public sigslot::has_slots<> {
  int count;
  talk_base::SocketAddress address;
  StunMessage* request;
  std::string remote_frag;

  talk_base::Thread* thread;
  Port* port1;
  Port* port2;
  Connection* conn;

  Foo(talk_base::Thread* th, Port* p1, Port* p2)
      : count(0), thread(th), port1(p1), port2(p2), conn(0) {
  }

  void OnAddressReady(Port* port) {
    count += 1;
  }

  void OnUnknownAddress(
      Port* port, const talk_base::SocketAddress& addr, StunMessage* msg,
      const std::string& rf) {
    assert(port == port1);
    if (!address.IsAny()) {
      assert(addr == address);
      delete request;
    }
    address = addr;
    request = msg;
    remote_frag = rf;
  }

  void OnMessage(talk_base::Message* pmsg) {
    assert(talk_base::Thread::Current() == thread);

    switch (pmsg->message_id) {
    case MSG_CONNECT:
      port1->SignalAddressReady.connect(this, &Foo::OnAddressReady);
      port1->SignalUnknownAddress.connect(this, &Foo::OnUnknownAddress);
      break;

    case MSG_PREP_ADDRESS:
      port1->PrepareAddress();
      break;

    case MSG_CREATE_CONN:
      conn = port1->CreateConnection(GetCandidate(port2), Port::ORIGIN_MESSAGE);
      assert(conn);
      conn->Ping(0);
      break;

    case MSG_PING:
      assert(conn);
      conn->Ping(0);
      break;

    case MSG_ACCEPT_CONN: {
      Candidate c = GetCandidate(port2);
      c.set_address(address);
      conn = port1->CreateConnection(c, Port::ORIGIN_MESSAGE);
      assert(conn);
      port1->SendBindingResponse(request, address);
      conn->Ping(0);
      delete request;
      break;
    }

    default:
      assert(false);
      break;
    }
  }
};

void test(talk_base::Thread* pthMain, const char* name1, Port* port1,
          talk_base::Thread* pthBack, const char* name2, Port* port2,
          bool accept = true, bool same_addr = true) {
  Foo* foo1 = new Foo(pthMain, port1, port2);
  Foo* foo2 = new Foo(pthBack, port2, port1);

  std::cout << "Test: " << name1 << " to " << name2 << ": ";
  std::cout.flush();

  pthBack->Start();

  pthMain->Post(foo1, MSG_CONNECT);
  pthBack->Post(foo2, MSG_CONNECT);
  pthMain->ProcessMessages(10);
  assert(foo1->count == 0);
  assert(foo2->count == 0);

  pthMain->Post(foo1, MSG_PREP_ADDRESS);
  pthMain->ProcessMessages(200);
  assert(foo1->count == 1);

  pthBack->Post(foo2, MSG_PREP_ADDRESS);
  pthMain->ProcessMessages(200);
  assert(foo2->count == 1);

  pthMain->Post(foo1, MSG_CREATE_CONN);
  pthMain->ProcessMessages(200);

  if (accept) {

    assert(foo1->address.IsAny());
    assert(foo2->remote_frag == port1->username_fragment());
    assert(!same_addr || (foo2->address == GetAddress(port1)));

    pthBack->Post(foo2, MSG_ACCEPT_CONN);
    pthMain->ProcessMessages(200);

  } else {

    assert(foo1->address.IsAny());
    assert(foo2->address.IsAny());

    pthBack->Post(foo2, MSG_CREATE_CONN);
    pthMain->ProcessMessages(200);

    if (same_addr) {
      assert(foo1->conn->read_state() == Connection::STATE_READABLE);
      assert(foo2->conn->write_state() == Connection::STATE_WRITABLE);

      // First connection may not be writable if the first ping did not get
      // through.  So we will have to do another.
      if (foo1->conn->write_state() == Connection::STATE_WRITE_CONNECT) {
        pthMain->Post(foo1, MSG_PING);
        pthMain->ProcessMessages(200);
      }
    } else {
      assert(foo1->address.IsAny());
      assert(foo2->address.IsAny());

      pthMain->Post(foo1, MSG_PING);
      pthMain->ProcessMessages(200);

      assert(foo1->address.IsAny());
      assert(!foo2->address.IsAny());

      pthBack->Post(foo2, MSG_ACCEPT_CONN);
      pthMain->ProcessMessages(200);
    }
  }

  assert(foo1->conn->read_state() == Connection::STATE_READABLE);
  assert(foo1->conn->write_state() == Connection::STATE_WRITABLE);
  assert(foo2->conn->read_state() == Connection::STATE_READABLE);
  assert(foo2->conn->write_state() == Connection::STATE_WRITABLE);

  pthBack->Stop();

  delete port1;
  delete port2;
  delete foo1;
  delete foo2;

  std::cout << "PASS" << std::endl;
}

const talk_base::SocketAddress local_addr = talk_base::SocketAddress("127.0.0.1", 0);
const talk_base::SocketAddress relay_int_addr = talk_base::SocketAddress("127.0.0.1", 5000);
const talk_base::SocketAddress relay_ext_addr = talk_base::SocketAddress("127.0.0.1", 5001);
const talk_base::SocketAddress stun_addr = talk_base::SocketAddress("127.0.0.1", STUN_SERVER_PORT);
const talk_base::SocketAddress nat_addr = talk_base::SocketAddress("127.0.0.1", talk_base::NAT_SERVER_PORT);

void test_udp() {
  talk_base::Thread* pthMain = talk_base::Thread::Current();
  talk_base::Thread* pthBack = new talk_base::Thread();
  talk_base::Network* network = new talk_base::Network("network", local_addr.ip());

  test(pthMain, "udp", new UDPPort(pthMain, NULL, network, local_addr),
       pthBack, "udp", new UDPPort(pthBack, NULL, network, local_addr));

  delete pthBack;
}

void test_relay() {
  talk_base::Thread* pthMain = talk_base::Thread::Current();
  talk_base::Thread* pthBack = new talk_base::Thread();
  talk_base::Network* network = new talk_base::Network("network", local_addr.ip());

  RelayServer relay_server(pthBack);

  talk_base::AsyncUDPSocket* int_socket = talk_base::CreateAsyncUDPSocket(pthBack->socketserver());
  assert(int_socket->Bind(relay_int_addr) >= 0);
  relay_server.AddInternalSocket(int_socket);

  talk_base::AsyncUDPSocket* ext_socket = talk_base::CreateAsyncUDPSocket(pthBack->socketserver());
  assert(ext_socket->Bind(relay_ext_addr) >= 0);
  relay_server.AddExternalSocket(ext_socket);

  std::string username = CreateRandomString(16);
  std::string password = CreateRandomString(16);

  RelayPort* rport =
      new RelayPort(pthBack, NULL, network, local_addr, username, password, "");
  rport->AddServerAddress(ProtocolAddress(relay_int_addr, PROTO_UDP));

  test(pthMain, "udp", new UDPPort(pthMain, NULL, network, local_addr),
       pthBack, "relay", rport);

  delete pthBack;
}

const char* NATName(talk_base::NATType type) {
  switch (type) {
  case talk_base::NAT_OPEN_CONE:       return "open cone";
  case talk_base::NAT_ADDR_RESTRICTED: return "addr restricted";
  case talk_base::NAT_PORT_RESTRICTED: return "port restricted";
  case talk_base::NAT_SYMMETRIC:       return "symmetric";
  default:
    assert(false);
    return 0;
  }
}

void test_stun(talk_base::NATType nat_type) {
  talk_base::Thread* pthMain = talk_base::Thread::Current();
  talk_base::Thread* pthBack = new talk_base::Thread();
  talk_base::Network* network = new talk_base::Network("network", local_addr.ip());

  talk_base::NATServer* nat = new talk_base::NATServer(
      nat_type, pthMain->socketserver(), nat_addr,
      pthMain->socketserver(), nat_addr);
  talk_base::NATSocketFactory* nat_factory =
      new talk_base::NATSocketFactory(pthMain->socketserver(), nat_addr);

  StunPort* stun_port =
      new StunPort(pthMain, nat_factory, network, local_addr, stun_addr);

  talk_base::AsyncUDPSocket* stun_socket = talk_base::CreateAsyncUDPSocket(pthMain->socketserver());
  assert(stun_socket->Bind(stun_addr) >= 0);
  StunServer* stun_server = new StunServer(stun_socket);

  char name[256];
  sprintf(name, "stun (%s)", NATName(nat_type));

  test(pthMain, name, stun_port,
       pthBack, "udp", new UDPPort(pthBack, NULL, network, local_addr),
       true, nat_type != talk_base::NAT_SYMMETRIC);

  delete stun_server;
  delete stun_socket;
  delete nat;
  delete nat_factory;
  delete pthBack;
}

void test_stun(talk_base::NATType nat1_type, talk_base::NATType nat2_type) {

  talk_base::Thread* pthMain = talk_base::Thread::Current();
  talk_base::Thread* pthBack = new talk_base::Thread();
  talk_base::Network* network = new talk_base::Network("network", local_addr.ip());

  talk_base::SocketAddress local_addr1(talk_base::LocalHost().networks()[0]->ip(), 0);
  talk_base::SocketAddress local_addr2(talk_base::LocalHost().networks()[1]->ip(), 0);

  talk_base::SocketAddress nat1_addr(local_addr1.ip(), talk_base::NAT_SERVER_PORT);
  talk_base::SocketAddress nat2_addr(local_addr2.ip(), talk_base::NAT_SERVER_PORT);

  talk_base::SocketAddress stun1_addr(local_addr1.ip(), STUN_SERVER_PORT);
  talk_base::SocketAddress stun2_addr(local_addr2.ip(), STUN_SERVER_PORT);

  talk_base::NATServer* nat1 = new talk_base::NATServer(
      nat1_type, pthMain->socketserver(), nat1_addr,
      pthMain->socketserver(), nat1_addr);
  talk_base::NATSocketFactory* nat1_factory =
      new talk_base::NATSocketFactory(pthMain->socketserver(), nat1_addr);

  StunPort* stun1_port =
      new StunPort(pthMain, nat1_factory, network, local_addr1, stun1_addr);

  talk_base::NATServer* nat2 = new talk_base::NATServer(
      nat2_type, pthBack->socketserver(), nat2_addr,
      pthBack->socketserver(), nat2_addr);
  talk_base::NATSocketFactory* nat2_factory =
      new talk_base::NATSocketFactory(pthBack->socketserver(), nat2_addr);

  StunPort* stun2_port =
      new StunPort(pthMain, nat2_factory, network, local_addr2, stun2_addr);

  talk_base::AsyncUDPSocket* stun1_socket = talk_base::CreateAsyncUDPSocket(pthMain->socketserver());
  assert(stun1_socket->Bind(stun_addr) >= 0);
  StunServer* stun1_server = new StunServer(stun1_socket);

  talk_base::AsyncUDPSocket* stun2_socket = talk_base::CreateAsyncUDPSocket(pthBack->socketserver());
  assert(stun2_socket->Bind(stun2_addr) >= 0);
  StunServer* stun2_server = new StunServer(stun2_socket);

  char name1[256], name2[256];
  sprintf(name1, "stun (%s)", NATName(nat1_type));
  sprintf(name2, "stun (%s)", NATName(nat2_type));

  test(pthMain, name1, stun1_port,
       pthBack, name2, stun2_port,
       nat2_type == talk_base::NAT_OPEN_CONE, nat1_type != talk_base::NAT_SYMMETRIC);

  delete stun1_server;
  delete stun1_socket;
  delete stun2_server;
  delete stun2_socket;
  delete nat1;
  delete nat1_factory;
  delete nat2;
  delete nat2_factory;
  delete pthBack;
}

int main(int argc, char* argv[]) {
  InitRandom(NULL, 0);

  test_udp();

  test_relay();

  test_stun(talk_base::NAT_OPEN_CONE);
  test_stun(talk_base::NAT_ADDR_RESTRICTED);
  test_stun(talk_base::NAT_PORT_RESTRICTED);
  test_stun(talk_base::NAT_SYMMETRIC);

  test_stun(talk_base::NAT_OPEN_CONE, talk_base::NAT_OPEN_CONE);
  test_stun(talk_base::NAT_ADDR_RESTRICTED, talk_base::NAT_OPEN_CONE);
  test_stun(talk_base::NAT_PORT_RESTRICTED, talk_base::NAT_OPEN_CONE);
  test_stun(talk_base::NAT_SYMMETRIC, talk_base::NAT_OPEN_CONE);

  test_stun(talk_base::NAT_ADDR_RESTRICTED, talk_base::NAT_ADDR_RESTRICTED);
  test_stun(talk_base::NAT_PORT_RESTRICTED, talk_base::NAT_ADDR_RESTRICTED);
  test_stun(talk_base::NAT_PORT_RESTRICTED, talk_base::NAT_PORT_RESTRICTED);
  test_stun(talk_base::NAT_SYMMETRIC, talk_base::NAT_ADDR_RESTRICTED);

  return 0;
}
