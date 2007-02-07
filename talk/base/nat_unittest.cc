#include <string>
#include <cstring>
#include <iostream>
#include <cassert>

#include "talk/base/natserver.h"
#include "talk/base/testclient.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/host.h"

#ifdef POSIX
extern "C" {
#include <errno.h>
}
#endif // POSIX

using namespace talk_base;

#define CHECK(arg) Check(arg, #arg)

void Check(int result, const char* desc) {
  if (result < 0) {
    std::cerr << desc << ": " << std::strerror(errno) << std::endl;
    exit(1);
  }
}

void CheckTest(bool act_val, bool exp_val, std::string desc) {
  if (act_val && !exp_val) {
    std::cerr << "error: " << desc << " was true, expected false" << std::endl;
    exit(1);
  } else if (!act_val && exp_val) {
    std::cerr << "error: " << desc << " was false, expected true" << std::endl;
    exit(1);
  }
}

void CheckReceive(
    TestClient* client, bool should_receive, const char* buf, size_t size) {
  if (should_receive)
    client->CheckNextPacket(buf, size, 0);
  else
    client->CheckNoPacket();
}

TestClient* CreateTestClient(
      SocketFactory* factory, const SocketAddress& local_addr) {
  AsyncUDPSocket* socket = CreateAsyncUDPSocket(factory);
  CHECK(socket->Bind(local_addr));
  return new TestClient(socket);
}

void TestNATPorts(
      SocketServer* internal, const SocketAddress& internal_addr,
      SocketServer* external, const SocketAddress external_addrs[4],
      NATType nat_type, bool exp_same) {

  Thread th_int(internal);
  Thread th_ext(external);

  SocketAddress server_addr = internal_addr;
  server_addr.SetPort(NAT_SERVER_PORT);
  NATServer* nat = new NATServer(
      nat_type, internal, server_addr, external, external_addrs[0]);
  NATSocketFactory* natsf = new NATSocketFactory(internal, server_addr);

  TestClient* in = CreateTestClient(natsf, internal_addr);
  TestClient* out[4];
  for (int i = 0; i < 4; i++)
    out[i] = CreateTestClient(external, external_addrs[i]);

  th_int.Start();
  th_ext.Start();

  const char* buf = "filter_test";
  size_t len = strlen(buf);

  in->SendTo(buf, len, external_addrs[0]);
  SocketAddress trans_addr;
  out[0]->CheckNextPacket(buf, len, &trans_addr);

  for (int i = 1; i < 4; i++) {
    in->SendTo(buf, len, external_addrs[i]);
    SocketAddress trans_addr2;
    out[i]->CheckNextPacket(buf, len, &trans_addr2);
    bool are_same = (trans_addr == trans_addr2);
    CheckTest(are_same, exp_same, "same translated address");
  }

  th_int.Stop();
  th_ext.Stop();

  delete nat;
  delete natsf;
  delete in;
  for (int i = 0; i < 4; i++)
    delete out[i];
}

void TestPorts(
      SocketServer* internal, const SocketAddress& internal_addr,
      SocketServer* external, const SocketAddress external_addrs[4]) {
  TestNATPorts(internal, internal_addr, external, external_addrs,
               NAT_OPEN_CONE, true);
  TestNATPorts(internal, internal_addr, external, external_addrs,
               NAT_ADDR_RESTRICTED, true);
  TestNATPorts(internal, internal_addr, external, external_addrs,
               NAT_PORT_RESTRICTED, true);
  TestNATPorts(internal, internal_addr, external, external_addrs,
               NAT_SYMMETRIC, false);
}

void TestNATFilters(
      SocketServer* internal, const SocketAddress& internal_addr,
      SocketServer* external, const SocketAddress external_addrs[4],
      NATType nat_type, bool filter_ip, bool filter_port) {

  Thread th_int(internal);
  Thread th_ext(external);

  SocketAddress server_addr = internal_addr;
  server_addr.SetPort(NAT_SERVER_PORT);
  NATServer* nat = new NATServer(
      nat_type, internal, server_addr, external, external_addrs[0]);
  NATSocketFactory* natsf = new NATSocketFactory(internal, server_addr);

  TestClient* in = CreateTestClient(natsf, internal_addr);
  TestClient* out[4];
  for (int i = 0; i < 4; i++)
    out[i] = CreateTestClient(external, external_addrs[i]);

  th_int.Start();
  th_ext.Start();

  const char* buf = "filter_test";
  size_t len = strlen(buf);

  in->SendTo(buf, len, external_addrs[0]);
  SocketAddress trans_addr;
  out[0]->CheckNextPacket(buf, len, &trans_addr);

  out[1]->SendTo(buf, len, trans_addr);
  CheckReceive(in, !filter_ip, buf, len);

  out[2]->SendTo(buf, len, trans_addr);
  CheckReceive(in, !filter_port, buf, len);

  out[3]->SendTo(buf, len, trans_addr);
  CheckReceive(in, !filter_ip && !filter_port, buf, len);

  th_int.Stop();
  th_ext.Stop();

  delete nat;
  delete natsf;
  delete in;
  for (int i = 0; i < 4; i++)
    delete out[i];
}

void TestFilters(
      SocketServer* internal, const SocketAddress& internal_addr,
      SocketServer* external, const SocketAddress external_addrs[4]) {
  TestNATFilters(internal, internal_addr, external, external_addrs,
                 NAT_OPEN_CONE, false, false);
  TestNATFilters(internal, internal_addr, external, external_addrs,
                 NAT_ADDR_RESTRICTED, true, false);
  TestNATFilters(internal, internal_addr, external, external_addrs,
                 NAT_PORT_RESTRICTED, true, true);
  TestNATFilters(internal, internal_addr, external, external_addrs,
                 NAT_SYMMETRIC, true, true);
}

const int PORT0 = 7405;
const int PORT1 = 7450;
const int PORT2 = 7505;

int main(int argc, char* argv[]) {
  assert(LocalHost().networks().size() >= 2);
  SocketAddress int_addr(LocalHost().networks()[1]->ip(), PORT0);

  std::string ext_ip1 =
      SocketAddress::IPToString(LocalHost().networks()[0]->ip()); // 127.0.0.1
  std::string ext_ip2 =
      SocketAddress::IPToString(LocalHost().networks()[1]->ip()); // 127.0.0.2
  assert(int_addr.IPAsString() != ext_ip1);
  //assert(int_addr.IPAsString() != ext_ip2); // uncomment

  SocketAddress ext_addrs[4] = {
      SocketAddress(ext_ip1, PORT1),
      SocketAddress(ext_ip2, PORT1),
      SocketAddress(ext_ip1, PORT2),
      SocketAddress(ext_ip2, PORT2)
  };

  PhysicalSocketServer* int_pss = new PhysicalSocketServer();
  PhysicalSocketServer* ext_pss = new PhysicalSocketServer();

  std::cout << "Testing on physical network:" << std::endl;
  TestPorts(int_pss, int_addr, ext_pss, ext_addrs);
  std::cout << "ports: PASS" << std::endl;
  TestFilters(int_pss, int_addr, ext_pss, ext_addrs);
  std::cout << "filters: PASS" << std::endl;

  VirtualSocketServer* int_vss = new VirtualSocketServer();
  VirtualSocketServer* ext_vss = new VirtualSocketServer();

  int_addr.SetIP(int_vss->GetNextIP());
  ext_addrs[0].SetIP(ext_vss->GetNextIP());
  ext_addrs[1].SetIP(ext_vss->GetNextIP());
  ext_addrs[2].SetIP(ext_addrs[0].ip());
  ext_addrs[3].SetIP(ext_addrs[1].ip());

  std::cout << "Testing on virtual network:" << std::endl;
  TestPorts(int_vss, int_addr, ext_vss, ext_addrs);
  std::cout << "ports: PASS" << std::endl;
  TestFilters(int_vss, int_addr, ext_vss, ext_addrs);
  std::cout << "filters: PASS" << std::endl;

  return 0;
}
