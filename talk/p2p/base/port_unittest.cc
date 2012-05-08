/*
 * libjingle
 * Copyright 2004 Google Inc.
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

#include "talk/base/basicpacketsocketfactory.h"
#include "talk/base/gunit.h"
#include "talk/base/helpers.h"
#include "talk/base/host.h"
#include "talk/base/logging.h"
#include "talk/base/natserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/socketaddress.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/tcpport.h"
#include "talk/p2p/base/udpport.h"
#include "talk/p2p/base/teststunserver.h"
#include "talk/p2p/base/testrelayserver.h"

using talk_base::AsyncPacketSocket;
using talk_base::ByteBuffer;
using talk_base::NATType;
using talk_base::NAT_OPEN_CONE;
using talk_base::NAT_ADDR_RESTRICTED;
using talk_base::NAT_PORT_RESTRICTED;
using talk_base::NAT_SYMMETRIC;
using talk_base::PacketSocketFactory;
using talk_base::scoped_ptr;
using talk_base::Socket;
using talk_base::SocketAddress;
using namespace cricket;

static const int kTimeout = 1000;
static const SocketAddress kLocalAddr1 = SocketAddress("192.168.1.2", 0);
static const SocketAddress kLocalAddr2 = SocketAddress("192.168.1.3", 0);
static const SocketAddress kNatAddr1 = SocketAddress("77.77.77.77",
                                                    talk_base::NAT_SERVER_PORT);
static const SocketAddress kNatAddr2 = SocketAddress("88.88.88.88",
                                                    talk_base::NAT_SERVER_PORT);
static const SocketAddress kStunAddr = SocketAddress("99.99.99.1",
                                                     STUN_SERVER_PORT);
static const SocketAddress kRelayUdpIntAddr("99.99.99.2", 5000);
static const SocketAddress kRelayUdpExtAddr("99.99.99.3", 5001);
static const SocketAddress kRelayTcpIntAddr("99.99.99.2", 5002);
static const SocketAddress kRelayTcpExtAddr("99.99.99.3", 5003);
static const SocketAddress kRelaySslTcpIntAddr("99.99.99.2", 5004);
static const SocketAddress kRelaySslTcpExtAddr("99.99.99.3", 5005);
static const uint32 kDefaultHostPriority = ICE_TYPE_PREFERENCE_HOST << 24 |
    65535 << 8 | ICE_CANDIDATE_COMPONENT_DEFAULT;
static const uint32 kDefaultPrflxPriority = ICE_TYPE_PREFERENCE_PRFLX << 24 |
    65535 << 8 | ICE_CANDIDATE_COMPONENT_DEFAULT;
static const int kUnauthorizedCodeAsGice =
    STUN_ERROR_UNAUTHORIZED / 256 * 100 + STUN_ERROR_UNAUTHORIZED % 256;
static const char kUnauthorizedReason[] = "UNAUTHORIZED";

static Candidate GetCandidate(Port* port) {
  assert(port->candidates().size() == 1);
  return port->candidates()[0];
}

static SocketAddress GetAddress(Port* port) {
  return GetCandidate(port).address();
}

static IceMessage* CopyStunMessage(const IceMessage* src) {
  IceMessage* dst = new IceMessage();
  ByteBuffer buf;
  src->Write(&buf);
  dst->Read(&buf);
  return dst;
}

static bool WriteStunMessage(const StunMessage* msg, ByteBuffer* buf) {
  buf->Resize(0);  // clear out any existing buffer contents
  return msg->Write(buf);
}

// Stub port class for testing STUN generation and processing.
class TestPort : public Port {
 public:
  TestPort(talk_base::Thread* thread, const std::string& type,
           talk_base::PacketSocketFactory* factory, talk_base::Network* network,
           const talk_base::IPAddress& ip, int min_port, int max_port,
           const std::string& username_fragment, const std::string& password)
      : Port(thread, type, factory, network, ip, min_port, max_port,
             username_fragment, password)  {
    set_priority(kDefaultHostPriority);
  }
  ~TestPort() {}

  // Expose GetStunMessage so that we can test it.
  using cricket::Port::GetStunMessage;

  // The last StunMessage that was sent on this Port.
  // TODO: Make these const; requires changes to SendXXXXResponse.
  ByteBuffer* last_stun_buf() { return last_stun_buf_.get(); }
  IceMessage* last_stun_msg() { return last_stun_msg_.get(); }

  virtual void PrepareAddress() {
    AddAddress(talk_base::SocketAddress(ip(), min_port()), "udp", true);
  }
  virtual Connection* CreateConnection(const Candidate& remote_candidate,
                                       CandidateOrigin origin) {
    Connection* conn = new ProxyConnection(this, 0, remote_candidate);
    AddConnection(conn);
    return conn;
  }
  virtual int SendTo(
      const void* data, size_t size, const talk_base::SocketAddress& addr,
      bool payload) {
    if (!payload) {
      IceMessage* msg = new IceMessage;
      ByteBuffer* buf = new ByteBuffer(static_cast<const char*>(data), size);
      if (!msg->Read(buf)) {
        delete msg;
        delete buf;
        return -1;
      }
      buf->Reset();  // rewind it
      last_stun_buf_.reset(buf);
      last_stun_msg_.reset(msg);
    }
    return size;
  }
  virtual int SetOption(talk_base::Socket::Option opt, int value) {
    return 0;
  }
  virtual int GetError() {
    return 0;
  }

 private:
  talk_base::scoped_ptr<ByteBuffer> last_stun_buf_;
  talk_base::scoped_ptr<IceMessage> last_stun_msg_;
};

class TestChannel : public sigslot::has_slots<> {
 public:
  TestChannel(Port* p1, Port* p2)
      : src_(p1), dst_(p2), address_count_(0), conn_(NULL),
        remote_request_(NULL) {
    src_->SignalAddressReady.connect(this, &TestChannel::OnAddressReady);
    src_->SignalUnknownAddress.connect(this, &TestChannel::OnUnknownAddress);
  }

  int address_count() { return address_count_; }
  Connection* conn() { return conn_; }
  const SocketAddress& remote_address() { return remote_address_; }
  const std::string remote_fragment() { return remote_frag_; }

  void Start() {
    src_->PrepareAddress();
  }
  void CreateConnection() {
    conn_ = src_->CreateConnection(GetCandidate(dst_), Port::ORIGIN_MESSAGE);
  }
  void AcceptConnection() {
    ASSERT_TRUE(remote_request_.get() != NULL);
    Candidate c = GetCandidate(dst_);
    c.set_address(remote_address_);
    conn_ = src_->CreateConnection(c, Port::ORIGIN_MESSAGE);
    src_->SendBindingResponse(remote_request_.get(), remote_address_);
    remote_request_.reset();
  }
  void Ping() {
    conn_->Ping(0);
  }
  void Stop() {
    conn_->SignalDestroyed.connect(this, &TestChannel::OnDestroyed);
    conn_->Destroy();
  }

  void OnAddressReady(Port* port) {
    address_count_++;
  }

  void OnUnknownAddress(Port* port, const SocketAddress& addr,
                        IceMessage* msg, const std::string& rf,
                        bool /*port_muxed*/) {
    ASSERT_EQ(src_.get(), port);
    if (!remote_address_.IsNil()) {
      ASSERT_EQ(remote_address_, addr);
    }
    // MI and PRIORITY attribute should be present in ping requests when port
    // is in ICEPROTO_RFC5245 mode.
    const cricket::StunByteStringAttribute* mi_attr =
        msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
    const cricket::StunUInt32Attribute* priority_attr =
        msg->GetUInt32(STUN_ATTR_PRIORITY);
    if (src_->ice_protocol() == cricket::ICEPROTO_RFC5245) {
      ASSERT_TRUE(mi_attr != NULL);
      ASSERT_TRUE(priority_attr != NULL);
    } else {
      ASSERT_TRUE(mi_attr == NULL);
      ASSERT_TRUE(priority_attr == NULL);
    }
    remote_address_ = addr;
    remote_request_.reset(CopyStunMessage(msg));
    remote_frag_ = rf;
  }

  void OnDestroyed(Connection* conn) {
    ASSERT_EQ(conn_, conn);
    conn_ = NULL;
  }

 private:
  talk_base::Thread* thread_;
  talk_base::scoped_ptr<Port> src_;
  Port* dst_;

  int address_count_;
  Connection* conn_;
  SocketAddress remote_address_;
  talk_base::scoped_ptr<StunMessage> remote_request_;
  std::string remote_frag_;
};

class PortTest : public testing::Test {
 public:
  PortTest()
      : main_(talk_base::Thread::Current()),
        pss_(new talk_base::PhysicalSocketServer),
        ss_(new talk_base::VirtualSocketServer(pss_.get())),
        ss_scope_(ss_.get()),
        network_("unittest", "unittest", talk_base::IPAddress(INADDR_ANY), 32),
        socket_factory_(talk_base::Thread::Current()),
        nat_factory1_(ss_.get(), kNatAddr1),
        nat_factory2_(ss_.get(), kNatAddr2),
        nat_socket_factory1_(&nat_factory1_),
        nat_socket_factory2_(&nat_factory2_),
        stun_server_(main_, kStunAddr),
        relay_server_(main_, kRelayUdpIntAddr, kRelayUdpExtAddr,
                      kRelayTcpIntAddr, kRelayTcpExtAddr,
                      kRelaySslTcpIntAddr, kRelaySslTcpExtAddr),
        username_(talk_base::CreateRandomString(ICE_UFRAG_LENGTH)),
        password_(talk_base::CreateRandomString(ICE_PWD_LENGTH)),
        ice_protocol_(cricket::ICEPROTO_GOOGLE) {
    network_.AddIP(talk_base::IPAddress(INADDR_ANY));
  }

 protected:
  static void SetUpTestCase() {
    // Ensure the RNG is inited.
    talk_base::InitRandom(NULL, 0);
  }

  void TestLocalToLocal() {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    UDPPort* port2 = CreateUdpPort(kLocalAddr2);
    TestConnectivity("udp", port1, "udp", port2, true, true, true, true);
  }
  void TestLocalToStun(NATType type) {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    nat_server2_.reset(CreateNatServer(kNatAddr2, type));
    StunPort* port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    TestConnectivity("udp", port1, StunName(type), port2,
                     type == NAT_OPEN_CONE, true, type != NAT_SYMMETRIC, true);
  }
  void TestLocalToRelay(ProtocolType proto) {
    UDPPort* port1 = CreateUdpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    TestConnectivity("udp", port1, RelayName(proto), port2,
                     true, true, true, true);
  }
  void TestStunToLocal(NATType type) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    UDPPort* port2 = CreateUdpPort(kLocalAddr2);
    TestConnectivity(StunName(type), port1, "udp", port2,
                     true, type != NAT_SYMMETRIC, true, true);
  }
  void TestStunToStun(NATType type1, NATType type2) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type1));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    nat_server2_.reset(CreateNatServer(kNatAddr2, type2));
    StunPort* port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    TestConnectivity(StunName(type1), port1, StunName(type2), port2,
                     type2 == NAT_OPEN_CONE,
                     type1 != NAT_SYMMETRIC, type2 != NAT_SYMMETRIC,
                     type1 + type2 < (NAT_PORT_RESTRICTED + NAT_SYMMETRIC));
  }
  void TestStunToRelay(NATType type, ProtocolType proto) {
    nat_server1_.reset(CreateNatServer(kNatAddr1, type));
    StunPort* port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    TestConnectivity(StunName(type), port1, RelayName(proto), port2,
                     true, type != NAT_SYMMETRIC, true, true);
  }
  void TestTcpToTcp() {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    TCPPort* port2 = CreateTcpPort(kLocalAddr2);
    TestConnectivity("tcp", port1, "tcp", port2, true, false, true, true);
  }
  void TestTcpToRelay(ProtocolType proto) {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_TCP);
    TestConnectivity("tcp", port1, RelayName(proto), port2,
                     true, false, true, true);
  }
  void TestSslTcpToRelay(ProtocolType proto) {
    TCPPort* port1 = CreateTcpPort(kLocalAddr1);
    RelayPort* port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_SSLTCP);
    TestConnectivity("ssltcp", port1, RelayName(proto), port2,
                     true, false, true, true);
  }

  // helpers for above functions
  UDPPort* CreateUdpPort(const SocketAddress& addr) {
    return CreateUdpPort(addr, &socket_factory_);
  }
  UDPPort* CreateUdpPort(const SocketAddress& addr,
                         PacketSocketFactory* socket_factory) {
    UDPPort* port =  UDPPort::Create(main_, socket_factory, &network_,
                                     addr.ipaddr(), 0, 0, username_, password_);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  TCPPort* CreateTcpPort(const SocketAddress& addr) {
    TCPPort* port = CreateTcpPort(addr, &socket_factory_);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  TCPPort* CreateTcpPort(const SocketAddress& addr,
                         PacketSocketFactory* socket_factory) {
    TCPPort* port =  TCPPort::Create(main_, socket_factory, &network_,
                                     addr.ipaddr(), 0, 0, username_, password_,
                                     true);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  StunPort* CreateStunPort(const SocketAddress& addr,
                           talk_base::PacketSocketFactory* factory) {
    StunPort* port =  StunPort::Create(main_, factory, &network_,
                                       addr.ipaddr(), 0, 0,
                                       username_, password_, kStunAddr);
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  RelayPort* CreateRelayPort(const SocketAddress& addr,
                             ProtocolType int_proto, ProtocolType ext_proto) {
    RelayPort* port = RelayPort::Create(main_, &socket_factory_, &network_,
                                        addr.ipaddr(), 0, 0,
                                        username_, password_);
    SocketAddress addrs[] =
        { kRelayUdpIntAddr, kRelayTcpIntAddr, kRelaySslTcpIntAddr };
    port->AddServerAddress(ProtocolAddress(addrs[int_proto], int_proto));
    // TODO: Add an external address for ext_proto, so that the
    // other side can connect to this port using a non-UDP protocol.
    port->set_ice_protocol(ice_protocol_);
    return port;
  }
  talk_base::NATServer* CreateNatServer(const SocketAddress& addr,
                                        talk_base::NATType type) {
    return new talk_base::NATServer(type, ss_.get(), addr, ss_.get(), addr);
  }
  static const char* StunName(NATType type) {
    switch (type) {
      case NAT_OPEN_CONE:       return "stun(open cone)";
      case NAT_ADDR_RESTRICTED: return "stun(addr restricted)";
      case NAT_PORT_RESTRICTED: return "stun(port restricted)";
      case NAT_SYMMETRIC:       return "stun(symmetric)";
      default:                  return "stun(?)";
    }
  }
  static const char* RelayName(ProtocolType type) {
    switch (type) {
      case PROTO_UDP:           return "relay(udp)";
      case PROTO_TCP:           return "relay(tcp)";
      case PROTO_SSLTCP:        return "relay(ssltcp)";
      default:                  return "relay(?)";
    }
  }

  void TestCrossFamilyPorts(int type);

  // this does all the work
  void TestConnectivity(const char* name1, Port* port1,
                        const char* name2, Port* port2,
                        bool accept, bool same_addr1,
                        bool same_addr2, bool possible);

  void set_ice_protocol(cricket::IceProtocolType protocol) {
    ice_protocol_ = protocol;
  }

  IceMessage* CreateStunMessage(int type) {
    IceMessage* msg = new IceMessage();
    msg->SetType(type);
    msg->SetTransactionID("TESTTESTTEST");
    return msg;
  }
  IceMessage* CreateStunMessageWithUsername(int type,
                                            const std::string& username) {
    IceMessage* msg = CreateStunMessage(type);
    msg->AddAttribute(
        new StunByteStringAttribute(STUN_ATTR_USERNAME, username));
    return msg;
  }
  TestPort* CreateTestPort(const talk_base::SocketAddress& addr,
                           const std::string& username,
                           const std::string& password) {
    return new TestPort(main_, "test", &socket_factory_, &network_,
                        addr.ipaddr(), 0, 0, username, password);
  }



 private:
  talk_base::Thread* main_;
  talk_base::scoped_ptr<talk_base::PhysicalSocketServer> pss_;
  talk_base::scoped_ptr<talk_base::VirtualSocketServer> ss_;
  talk_base::SocketServerScope ss_scope_;
  talk_base::Network network_;
  talk_base::BasicPacketSocketFactory socket_factory_;
  talk_base::scoped_ptr<talk_base::NATServer> nat_server1_;
  talk_base::scoped_ptr<talk_base::NATServer> nat_server2_;
  talk_base::NATSocketFactory nat_factory1_;
  talk_base::NATSocketFactory nat_factory2_;
  talk_base::BasicPacketSocketFactory nat_socket_factory1_;
  talk_base::BasicPacketSocketFactory nat_socket_factory2_;
  TestStunServer stun_server_;
  TestRelayServer relay_server_;
  std::string username_;
  std::string password_;
  cricket::IceProtocolType ice_protocol_;
};

void PortTest::TestConnectivity(const char* name1, Port* port1,
                                const char* name2, Port* port2,
                                bool accept, bool same_addr1,
                                bool same_addr2, bool possible) {
  LOG(LS_INFO) << "Test: " << name1 << " to " << name2 << ": ";
  port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels.
  TestChannel ch1(port1, port2);
  TestChannel ch2(port2, port1);
  EXPECT_EQ(0, ch1.address_count());
  EXPECT_EQ(0, ch2.address_count());

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_WAIT(1, ch1.address_count(), kTimeout);
  ASSERT_EQ_WAIT(1, ch2.address_count(), kTimeout);

  // Send a ping from src to dst. This may or may not make it.
  ch1.CreateConnection();
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_TRUE_WAIT(ch1.conn()->connected(), kTimeout);  // for TCP connect
  ch1.Ping();
  WAIT(!ch2.remote_address().IsNil(), kTimeout);

  if (accept) {
    // We are able to send a ping from src to dst. This is the case when
    // sending to UDP ports and cone NATs.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_EQ(ch2.remote_fragment(), port1->username_fragment());

    // Ensure the ping came from the same address used for src.
    // This is the case unless the source NAT was symmetric.
    if (same_addr1) EXPECT_EQ(ch2.remote_address(), GetAddress(port1));
    EXPECT_TRUE(same_addr2);

    // Send a ping from dst to src.
    ch2.AcceptConnection();
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch2.conn()->write_state(),
                   kTimeout);
  } else {
    // We can't send a ping from src to dst, so flip it around. This will happen
    // when the destination NAT is addr/port restricted or symmetric.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_TRUE(ch2.remote_address().IsNil());

    // Send a ping from dst to src. Again, this may or may not make it.
    ch2.CreateConnection();
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    WAIT(ch2.conn()->write_state() == Connection::STATE_WRITABLE, kTimeout);

    if (same_addr1 && same_addr2) {
      // The new ping got back to the source.
      EXPECT_EQ(Connection::STATE_READABLE, ch1.conn()->read_state());
      EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());

      // First connection may not be writable if the first ping did not get
      // through.  So we will have to do another.
      if (ch1.conn()->write_state() == Connection::STATE_WRITE_CONNECT) {
        ch1.Ping();
        EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch1.conn()->write_state(),
                       kTimeout);
      }
    } else if (!same_addr1 && possible) {
      // The new ping went to the candidate address, but that address was bad.
      // This will happen when the source NAT is symmetric.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());

      // However, since we have now sent a ping to the source IP, we should be
      // able to get a ping from it. This gives us the real source address.
      ch1.Ping();
      EXPECT_TRUE_WAIT(!ch2.remote_address().IsNil(), kTimeout);
      EXPECT_EQ(Connection::STATE_READ_TIMEOUT, ch2.conn()->read_state());
      EXPECT_TRUE(ch1.remote_address().IsNil());

      // Pick up the actual address and establish the connection.
      ch2.AcceptConnection();
      ASSERT_TRUE(ch2.conn() != NULL);
      ch2.Ping();
      EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch2.conn()->write_state(),
                     kTimeout);
    } else if (!same_addr2 && possible) {
      // The new ping came in, but from an unexpected address. This will happen
      // when the destination NAT is symmetric.
      EXPECT_FALSE(ch1.remote_address().IsNil());
      EXPECT_EQ(Connection::STATE_READ_TIMEOUT, ch1.conn()->read_state());

      // Update our address and complete the connection.
      ch1.AcceptConnection();
      ch1.Ping();
      EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch1.conn()->write_state(),
                     kTimeout);
    } else {  // (!possible)
      // There should be s no way for the pings to reach each other. Check it.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
      ch1.Ping();
      WAIT(!ch2.remote_address().IsNil(), kTimeout);
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
    }
  }

  // Everything should be good, unless we know the situation is impossible.
  ASSERT_TRUE(ch1.conn() != NULL);
  ASSERT_TRUE(ch2.conn() != NULL);
  if (possible) {
    EXPECT_EQ(Connection::STATE_READABLE, ch1.conn()->read_state());
    EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_EQ(Connection::STATE_READABLE, ch2.conn()->read_state());
    EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  } else {
    EXPECT_NE(Connection::STATE_READABLE, ch1.conn()->read_state());
    EXPECT_NE(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_NE(Connection::STATE_READABLE, ch2.conn()->read_state());
    EXPECT_NE(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  }

  // Tear down and ensure that goes smoothly.
  ch1.Stop();
  ch2.Stop();
  EXPECT_TRUE_WAIT(ch1.conn() == NULL, kTimeout);
  EXPECT_TRUE_WAIT(ch2.conn() == NULL, kTimeout);
}

class FakePacketSocketFactory : public talk_base::PacketSocketFactory {
 public:
  FakePacketSocketFactory()
      : next_udp_socket_(NULL),
        next_server_tcp_socket_(NULL),
        next_client_tcp_socket_(NULL) {
  }
  virtual ~FakePacketSocketFactory() { }

  virtual AsyncPacketSocket* CreateUdpSocket(
      const SocketAddress& address, int min_port, int max_port) {
    EXPECT_TRUE(next_udp_socket_ != NULL);
    AsyncPacketSocket* result = next_udp_socket_;
    next_udp_socket_ = NULL;
    return result;
  }

  virtual AsyncPacketSocket* CreateServerTcpSocket(
      const SocketAddress& local_address, int min_port, int max_port,
      bool ssl) {
    EXPECT_TRUE(next_server_tcp_socket_ != NULL);
    AsyncPacketSocket* result = next_server_tcp_socket_;
    next_server_tcp_socket_ = NULL;
    return result;
  }

  // TODO: |proxy_info| and |user_agent| should be set
  // per-factory and not when socket is created.
  virtual AsyncPacketSocket* CreateClientTcpSocket(
      const SocketAddress& local_address, const SocketAddress& remote_address,
      const talk_base::ProxyInfo& proxy_info,
      const std::string& user_agent, bool ssl) {
    EXPECT_TRUE(next_client_tcp_socket_ != NULL);
    AsyncPacketSocket* result = next_client_tcp_socket_;
    next_client_tcp_socket_ = NULL;
    return result;
  }

  void set_next_udp_socket(AsyncPacketSocket* next_udp_socket) {
    next_udp_socket_ = next_udp_socket;
  }
  void set_next_server_tcp_socket(AsyncPacketSocket* next_server_tcp_socket) {
    next_server_tcp_socket_ = next_server_tcp_socket;
  }
  void set_next_client_tcp_socket(AsyncPacketSocket* next_client_tcp_socket) {
    next_client_tcp_socket_ = next_client_tcp_socket;
  }

 private:
  AsyncPacketSocket* next_udp_socket_;
  AsyncPacketSocket* next_server_tcp_socket_;
  AsyncPacketSocket* next_client_tcp_socket_;
};

class FakeAsyncPacketSocket : public AsyncPacketSocket {
 public:
  // Returns current local address. Address may be set to NULL if the
  // socket is not bound yet (GetState() returns STATE_BINDING).
  virtual SocketAddress GetLocalAddress() const {
    return SocketAddress();
  }

  // Returns remote address. Returns zeroes if this is not a client TCP socket.
  virtual SocketAddress GetRemoteAddress() const {
    return SocketAddress();
  }

  // Send a packet.
  virtual int Send(const void *pv, size_t cb) {
    return cb;
  }
  virtual int SendTo(const void *pv, size_t cb, const SocketAddress& addr) {
    return cb;
  }
  virtual int Close() {
    return 0;
  }

  virtual State GetState() const { return state_; }
  virtual int GetOption(Socket::Option opt, int* value) { return 0; }
  virtual int SetOption(Socket::Option opt, int value) { return 0; }
  virtual int GetError() const { return 0; }
  virtual void SetError(int error) { }

  void set_state(State state) { state_ = state; }

 private:
  State state_;
};

// Local -> XXXX
TEST_F(PortTest, TestLocalToLocal) {
  TestLocalToLocal();
}

TEST_F(PortTest, TestLocalToConeNat) {
  TestLocalToStun(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestLocalToARNat) {
  TestLocalToStun(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestLocalToPRNat) {
  TestLocalToStun(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestLocalToSymNat) {
  TestLocalToStun(NAT_SYMMETRIC);
}

TEST_F(PortTest, TestLocalToRelay) {
  TestLocalToRelay(PROTO_UDP);
}

TEST_F(PortTest, TestLocalToTcpRelay) {
  TestLocalToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestLocalToSslTcpRelay) {
  TestLocalToRelay(PROTO_SSLTCP);
}

// Cone NAT -> XXXX
TEST_F(PortTest, TestConeNatToLocal) {
  TestStunToLocal(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToConeNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToARNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToPRNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToSymNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestConeNatToRelay) {
  TestStunToRelay(NAT_OPEN_CONE, PROTO_UDP);
}

TEST_F(PortTest, TestConeNatToTcpRelay) {
  TestStunToRelay(NAT_OPEN_CONE, PROTO_TCP);
}

// Address-restricted NAT -> XXXX
TEST_F(PortTest, TestARNatToLocal) {
  TestStunToLocal(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToConeNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestARNatToARNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToPRNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestARNatToSymNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestARNatToRelay) {
  TestStunToRelay(NAT_ADDR_RESTRICTED, PROTO_UDP);
}

TEST_F(PortTest, TestARNATNatToTcpRelay) {
  TestStunToRelay(NAT_ADDR_RESTRICTED, PROTO_TCP);
}

// Port-restricted NAT -> XXXX
TEST_F(PortTest, TestPRNatToLocal) {
  TestStunToLocal(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToConeNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestPRNatToARNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToPRNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestPRNatToRelay) {
  TestStunToRelay(NAT_PORT_RESTRICTED, PROTO_UDP);
}

TEST_F(PortTest, TestPRNatToTcpRelay) {
  TestStunToRelay(NAT_PORT_RESTRICTED, PROTO_TCP);
}

// Symmetric NAT -> XXXX
TEST_F(PortTest, TestSymNatToLocal) {
  TestStunToLocal(NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToConeNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestSymNatToARNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToPRNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToRelay) {
  TestStunToRelay(NAT_SYMMETRIC, PROTO_UDP);
}

TEST_F(PortTest, TestSymNatToTcpRelay) {
  TestStunToRelay(NAT_SYMMETRIC, PROTO_TCP);
}

// Outbound TCP -> XXXX
TEST_F(PortTest, TestTcpToTcp) {
  TestTcpToTcp();
}

/* TODO: Enable these once testrelayserver can accept external TCP.
TEST_F(PortTest, TestTcpToTcpRelay) {
  TestTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestTcpToSslTcpRelay) {
  TestTcpToRelay(PROTO_SSLTCP);
}
*/

// Outbound SSLTCP -> XXXX
/* TODO: Enable these once testrelayserver can accept external SSL.
TEST_F(PortTest, TestSslTcpToTcpRelay) {
  TestSslTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestSslTcpToSslTcpRelay) {
  TestSslTcpToRelay(PROTO_SSLTCP);
}
*/

// This test case verifies standard ICE features in STUN messages. Currently it
// verifies Message Integrity attribute in STUN messages and username in STUN
// binding request will have colon (":") between remote and local username.
TEST_F(PortTest, TestLocalToLocalAsIce) {
  set_ice_protocol(cricket::ICEPROTO_RFC5245);
  UDPPort* port1 = CreateUdpPort(kLocalAddr1);
  ASSERT_EQ(cricket::ICEPROTO_RFC5245, port1->ice_protocol());
  UDPPort* port2 = CreateUdpPort(kLocalAddr2);
  ASSERT_EQ(cricket::ICEPROTO_RFC5245, port2->ice_protocol());
  // Same parameters as TestLocalToLocal above.
  TestConnectivity("udp", port1, "udp", port2, true, true, true, true);
}

TEST_F(PortTest, TestTcpNoDelay) {
  TCPPort* port1 = CreateTcpPort(kLocalAddr1);
  int option_value = -1;
  int success = port1->GetOption(talk_base::Socket::OPT_NODELAY,
                                 &option_value);
  ASSERT_EQ(0, success);  // GetOption() should complete successfully w/ 0
  ASSERT_EQ(1, option_value);
  delete port1;
}

TEST_F(PortTest, TestDelayedBindingUdp) {
  FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_udp_socket(socket);
  scoped_ptr<UDPPort> port(
      CreateUdpPort(kLocalAddr1, &socket_factory));

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->candidates().size());
  socket->SignalAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->candidates().size());
}

TEST_F(PortTest, TestDelayedBindingTcp) {
  FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_server_tcp_socket(socket);
  scoped_ptr<TCPPort> port(
      CreateTcpPort(kLocalAddr1, &socket_factory));

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->candidates().size());
  socket->SignalAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->candidates().size());
}

void PortTest::TestCrossFamilyPorts(int type) {
  FakePacketSocketFactory factory;
  scoped_ptr<Port> ports[4];
  SocketAddress addresses[4] = {SocketAddress("192.168.1.3", 0),
                                SocketAddress("192.168.1.4", 0),
                                SocketAddress("2001:db8::1", 0),
                                SocketAddress("2001:db8::2", 0)};
  for (int i = 0; i < 4; i++) {
    FakeAsyncPacketSocket *socket = new FakeAsyncPacketSocket();
    if (type == SOCK_DGRAM) {
      factory.set_next_udp_socket(socket);
      ports[i].reset(CreateUdpPort(addresses[i], &factory));
    } else if (type == SOCK_STREAM) {
      factory.set_next_server_tcp_socket(socket);
      ports[i].reset(CreateTcpPort(addresses[i], &factory));
    }
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->SignalAddressReady(socket, addresses[i]);
    ports[i]->PrepareAddress();
  }

  // IPv4 Port, connects to IPv6 candidate and then to IPv4 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  Connection* c = ports[0]->CreateConnection(GetCandidate(ports[2].get()),
                                             Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[0]->connections().size());
  c = ports[0]->CreateConnection(GetCandidate(ports[1].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
  EXPECT_EQ(1U, ports[0]->connections().size());

  // IPv6 Port, connects to IPv4 candidate and to IPv6 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  c = ports[2]->CreateConnection(GetCandidate(ports[0].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[2]->connections().size());
  c = ports[2]->CreateConnection(GetCandidate(ports[3].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
  EXPECT_EQ(1U, ports[2]->connections().size());
}

TEST_F(PortTest, TestSkipCrossFamilyTcp) {
  TestCrossFamilyPorts(SOCK_STREAM);
}

TEST_F(PortTest, TestSkipCrossFamilyUdp) {
  TestCrossFamilyPorts(SOCK_DGRAM);
}

// Test sending STUN messages in GICE format.
TEST_F(PortTest, TestSendStunMessageAsGice) {
  talk_base::scoped_ptr<TestPort> lport(
      CreateTestPort(kLocalAddr1, "lfrag", "lpass"));
  talk_base::scoped_ptr<TestPort> rport(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  lport->set_ice_protocol(ICEPROTO_GOOGLE);
  rport->set_ice_protocol(ICEPROTO_GOOGLE);

  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->candidates().empty());
  Connection* conn = lport->CreateConnection(rport->candidates()[0],
      Port::ORIGIN_MESSAGE);
  rport->CreateConnection(lport->candidates()[0], Port::ORIGIN_MESSAGE);
  conn->Ping(0);

  // Check that it's a proper BINDING-REQUEST.
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, 1000);
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunByteStringAttribute* username_attr = msg->GetByteString(
      STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != NULL);
  EXPECT_EQ("rfraglfrag", username_attr->GetString());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_FINGERPRINT) == NULL);

  // Save a copy of the BINDING-REQUEST for use below.
  talk_base::scoped_ptr<IceMessage> request(CopyStunMessage(msg));

  // Respond with a BINDING-RESPONSE.
  rport->SendBindingResponse(request.get(), lport->candidates()[0].address());
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  username_attr = msg->GetByteString(STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != NULL);  // GICE has a username in the response.
  EXPECT_EQ("rfraglfrag", username_attr->GetString());
  const StunAddressAttribute* addr_attr = msg->GetAddress(
      STUN_ATTR_MAPPED_ADDRESS);
  ASSERT_TRUE(addr_attr != NULL);
  EXPECT_EQ(lport->candidates()[0].address(), addr_attr->GetAddress());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_XOR_MAPPED_ADDRESS) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_FINGERPRINT) == NULL);

  // Respond with a BINDING-ERROR-RESPONSE. This wouldn't happen in real life,
  // but we can do it here.
  rport->SendBindingErrorResponse(request.get(),
                                  rport->candidates()[0].address(),
                                  STUN_ERROR_UNAUTHORIZED, kUnauthorizedReason);
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_ERROR_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  username_attr = msg->GetByteString(STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != NULL);  // GICE has a username in the response.
  EXPECT_EQ("rfraglfrag", username_attr->GetString());
  const StunErrorCodeAttribute* error_attr = msg->GetErrorCode();
  ASSERT_TRUE(error_attr != NULL);
  // The GICE wire format for error codes is incorrect.
  EXPECT_EQ(kUnauthorizedCodeAsGice, error_attr->code());
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED / 256, error_attr->eclass());
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED % 256, error_attr->number());
  EXPECT_EQ(kUnauthorizedReason, error_attr->reason());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_FINGERPRINT) == NULL);
}

// Test sending STUN messages in ICE format.
TEST_F(PortTest, TestSendStunMessageAsIce) {
  talk_base::scoped_ptr<TestPort> lport(
      CreateTestPort(kLocalAddr1, "lfrag", "lpass"));
  talk_base::scoped_ptr<TestPort> rport(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  lport->set_ice_protocol(ICEPROTO_RFC5245);
  rport->set_ice_protocol(ICEPROTO_RFC5245);

  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->candidates().empty());
  Connection* conn = lport->CreateConnection(rport->candidates()[0],
      Port::ORIGIN_MESSAGE);
  rport->CreateConnection(lport->candidates()[0], Port::ORIGIN_MESSAGE);
  conn->Ping(0);

  // Check that it's a proper BINDING-REQUEST.
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, 1000);
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != NULL);
  const StunUInt32Attribute* priority_attr = msg->GetUInt32(STUN_ATTR_PRIORITY);
  ASSERT_TRUE(priority_attr != NULL);
  EXPECT_EQ(kDefaultPrflxPriority, priority_attr->value());
  EXPECT_EQ("rfrag:lfrag", username_attr->GetString());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != NULL);
  EXPECT_TRUE(StunMessage::ValidateMessageIntegrity(
      lport->last_stun_buf()->Data(), lport->last_stun_buf()->Length(),
      "rpass"));
  // TODO: Check FINGERPRINT attribute

  // Save a copy of the BINDING-REQUEST for use below.
  talk_base::scoped_ptr<IceMessage> request(CopyStunMessage(msg));

  // Respond with a BINDING-RESPONSE.
  rport->SendBindingResponse(request.get(), lport->candidates()[0].address());
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != NULL);
  EXPECT_TRUE(StunMessage::ValidateMessageIntegrity(
      rport->last_stun_buf()->Data(), rport->last_stun_buf()->Length(),
      "rpass"));
  const StunAddressAttribute* addr_attr = msg->GetAddress(
      STUN_ATTR_XOR_MAPPED_ADDRESS);
  ASSERT_TRUE(addr_attr != NULL);
  EXPECT_EQ(lport->candidates()[0].address(), addr_attr->GetAddress());
  // No USERNAME or PRIORITY in ICE responses.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MAPPED_ADDRESS) == NULL);
  // TODO: Check FINGERPRINT attribute

  // Respond with a BINDING-ERROR-RESPONSE. This wouldn't happen in real life,
  // but we can do it here.
  rport->SendBindingErrorResponse(request.get(),
                                  lport->candidates()[0].address(),
                                  STUN_ERROR_UNAUTHORIZED, kUnauthorizedReason);
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_ERROR_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  // TODO: Should this include a MESSAGE-INTEGRITY?
  // TODO: Check FINGERPRINT attribute
  const StunErrorCodeAttribute* error_attr = msg->GetErrorCode();
  ASSERT_TRUE(error_attr != NULL);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, error_attr->code());
  EXPECT_EQ(kUnauthorizedReason, error_attr->reason());
  // No USERNAME with ICE.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
}

// Test handling STUN messages in GICE format.
TEST_F(PortTest, TestGetStunMessageAsGice) {
  // Our port will act as the "remote" port.
  talk_base::scoped_ptr<TestPort> port(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  port->set_ice_protocol(ICEPROTO_GOOGLE);

  talk_base::scoped_ptr<IceMessage> in_msg, out_msg;
  talk_base::scoped_ptr<ByteBuffer> buf(new ByteBuffer());
  talk_base::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid GICE username and no M-I.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                             "rfraglfrag"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);  // Succeeds, since this is GICE.
  ASSERT_EQ("lfrag", username);

  // Add M-I; should be ignored and rest of message parsed normally.
  in_msg->AddMessageIntegrity("password");
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("lfrag", username);

  // BINDING-RESPONSE with username, as done in GICE. Should succeed.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_RESPONSE,
                                             "rfraglfrag"));
  in_msg->AddAttribute(
      new StunAddressAttribute(STUN_ATTR_MAPPED_ADDRESS, kLocalAddr2));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("", username);

  // BINDING-RESPONSE without username. Should be tolerated as well.
  in_msg.reset(CreateStunMessage(STUN_BINDING_RESPONSE));
  in_msg->AddAttribute(
      new StunAddressAttribute(STUN_ATTR_MAPPED_ADDRESS, kLocalAddr2));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("", username);

  // BINDING-ERROR-RESPONSE with username and error code.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_ERROR_RESPONSE,
                                             "rfraglfrag"));
  in_msg->AddAttribute(new StunErrorCodeAttribute(STUN_ATTR_ERROR_CODE,
      kUnauthorizedCodeAsGice, kUnauthorizedReason));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("", username);
  ASSERT_TRUE(out_msg->GetErrorCode() != NULL);
  // GetStunMessage doesn't unmunge the GICE error code (happens downstream).
  EXPECT_EQ(kUnauthorizedCodeAsGice, out_msg->GetErrorCode()->code());
  EXPECT_EQ(kUnauthorizedReason, out_msg->GetErrorCode()->reason());
}

// Test handling STUN messages in ICE format.
TEST_F(PortTest, TestGetStunMessageAsIce) {
  // Our port will act as the "remote" port.
  talk_base::scoped_ptr<TestPort> port(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  port->set_ice_protocol(ICEPROTO_RFC5245);

  talk_base::scoped_ptr<IceMessage> in_msg, out_msg;
  talk_base::scoped_ptr<ByteBuffer> buf(new ByteBuffer());
  talk_base::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid GICE username and no M-I.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                             "rfrag:lfrag"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);  // Fails for ICE because no M-I.
  ASSERT_EQ("", username);

  // Add M-I; message should now parse properly.
  in_msg->AddMessageIntegrity("rpass");
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("lfrag", username);

  // BINDING-RESPONSE without username, as required by ICE.
  in_msg.reset(CreateStunMessage(STUN_BINDING_RESPONSE));
  // TODO: Add mapped/xor-mapped address
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("", username);

  // BINDING-ERROR-RESPONSE without username, with error code.
  in_msg.reset(CreateStunMessage(STUN_BINDING_ERROR_RESPONSE));
  in_msg->AddAttribute(new StunErrorCodeAttribute(STUN_ATTR_ERROR_CODE,
      STUN_ERROR_UNAUTHORIZED, kUnauthorizedReason));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() != NULL);
  ASSERT_EQ("", username);
  ASSERT_TRUE(out_msg->GetErrorCode() != NULL);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, out_msg->GetErrorCode()->code());
  EXPECT_EQ(kUnauthorizedReason, out_msg->GetErrorCode()->reason());
}

// Tests handling of GICE binding requests with missing or incorrect usernames.
TEST_F(PortTest, TestGetStunMessageAsGiceBadUsername) {
  talk_base::scoped_ptr<TestPort> port(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  port->set_ice_protocol(ICEPROTO_GOOGLE);

  talk_base::scoped_ptr<IceMessage> in_msg, out_msg;
  talk_base::scoped_ptr<ByteBuffer> buf(new ByteBuffer());
  talk_base::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST with no username.
  in_msg.reset(CreateStunMessage(STUN_BINDING_REQUEST));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with empty username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST, ""));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with too-short username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "lfra"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with reversed username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                             "lfragrfrag"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with garbage username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                             "abcdefgh"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);
}

// Tests handling of GICE binding requests with missing or incorrect usernames.
TEST_F(PortTest, TestGetStunMessageAsIceBadUsername) {
  talk_base::scoped_ptr<TestPort> port(
      CreateTestPort(kLocalAddr2, "rfrag", "rpass"));
  port->set_ice_protocol(ICEPROTO_RFC5245);

  talk_base::scoped_ptr<IceMessage> in_msg, out_msg;
  talk_base::scoped_ptr<ByteBuffer> buf(new ByteBuffer());
  talk_base::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST with no username.
  in_msg.reset(CreateStunMessage(STUN_BINDING_REQUEST));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with empty username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST, ""));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with too-short username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfra"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with reversed username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                            "lfrag:rfrag"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);

  // BINDING-REQUEST with garbage username.
  in_msg.reset(CreateStunMessageWithUsername(STUN_BINDING_REQUEST,
                                             "abcd:efgh"));
  WriteStunMessage(in_msg.get(), buf.get());
  EXPECT_TRUE(port->GetStunMessage(buf->Data(), buf->Length(), addr,
                                   out_msg.accept(), &username));
  ASSERT_TRUE(out_msg.get() == NULL);
  ASSERT_EQ("", username);
}
