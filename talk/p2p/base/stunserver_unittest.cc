#include "talk/base/testclient.h"
#include "talk/base/thread.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/host.h"
#include "talk/p2p/base/stunserver.h"
#include <cstring>
#include <iostream>
#include <cassert>

using namespace cricket;

StunMessage* GetResponse(talk_base::TestClient* client) {
    talk_base::TestClient::Packet* packet = client->NextPacket();
  assert(packet);
  talk_base::ByteBuffer buf(packet->buf, packet->size);
  StunMessage* msg = new StunMessage();
  assert(msg->Read(&buf));
  delete packet;
  return msg;
}

int main(int argc, char* argv[]) {
  assert(talk_base::LocalHost().networks().size() >= 2);
  talk_base::SocketAddress server_addr(talk_base::LocalHost().networks()[1]->ip(), 7000);
  talk_base::SocketAddress client_addr(talk_base::LocalHost().networks()[1]->ip(), 6000);

  talk_base::Thread th;

  talk_base::AsyncUDPSocket* server_socket = 0;
  StunServer* server = 0;
  if (argc >= 2) {
    server_addr.SetIP(argv[1]);
    client_addr.SetIP(0);
    if (argc == 3)
      server_addr.SetPort(atoi(argv[2]));
    std::cout << "Using server at " << server_addr.ToString() << std::endl;
  } else {
    server_socket = talk_base::CreateAsyncUDPSocket(th.socketserver());
    assert(server_socket->Bind(server_addr) >= 0);
    server = new StunServer(server_socket);
  }

  talk_base::AsyncUDPSocket* client_socket = talk_base::CreateAsyncUDPSocket(th.socketserver());
  assert(client_socket->Bind(client_addr) >= 0);
  talk_base::TestClient* client = new talk_base::TestClient(client_socket, &th);

  th.Start();

  const char* bad = "this is a completely nonsensical message whose only "
                    "purpose is to make the parser go 'ack'.  it doesn't "
                    "look anything like a normal stun message";

  client->SendTo(bad, std::strlen(bad), server_addr);
  StunMessage* msg = GetResponse(client);
  assert(msg->type() == STUN_BINDING_ERROR_RESPONSE);

  const StunErrorCodeAttribute* err = msg->GetErrorCode();
  assert(err);
  assert(err->error_class() == 4);
  assert(err->number() == 0);
  assert(err->reason() == std::string("Bad Request"));

  delete msg;

  std::string transaction_id = "0123456789abcdef";

  StunMessage req;
  req.SetType(STUN_BINDING_REQUEST);
  req.SetTransactionID(transaction_id);

  talk_base::ByteBuffer buf;
  req.Write(&buf);

  client->SendTo(buf.Data(), buf.Length(), server_addr);
  StunMessage* msg2 = GetResponse(client);
  assert(msg2->type() == STUN_BINDING_RESPONSE);
  assert(msg2->transaction_id() == transaction_id);

  const StunAddressAttribute* mapped_addr =
      msg2->GetAddress(STUN_ATTR_MAPPED_ADDRESS);
  assert(mapped_addr);
  assert(mapped_addr->family() == 1);
  assert(mapped_addr->port() == client_addr.port());
  if (mapped_addr->ip() != client_addr.ip()) {
    printf("Warning: mapped IP (%s) != local IP (%s)\n",
        talk_base::SocketAddress::IPToString(mapped_addr->ip()).c_str(),
        client_addr.IPAsString().c_str());
  }

  const StunAddressAttribute* source_addr =
      msg2->GetAddress(STUN_ATTR_SOURCE_ADDRESS);
  assert(source_addr);
  assert(source_addr->family() == 1);
  assert(source_addr->port() == server_addr.port());
  assert(source_addr->ip() == server_addr.ip());

  delete msg2;

  th.Stop();

  delete server;
  delete server_socket;
  delete client;

  std::cout << "PASS" << std::endl;
  return 0;
}
