#include <iostream>
#include <sstream>
#include <deque>
#include <map>

#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/host.h"
#include "talk/base/natserver.h"
#include "talk/base/natsocketfactory.h"
#include "talk/base/helpers.h"
#include "talk/xmpp/constants.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/base/sessionclient.h"
#include "talk/p2p/base/session.h"
#include "talk/p2p/base/portallocator.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/p2p/base/udpport.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/base/rawtransport.h"
#include "talk/p2p/base/stunserver.h"
#include "talk/p2p/base/relayserver.h"

using namespace cricket;
using namespace buzz;

const std::string kSessionType = "http://oink.splat/session";

const talk_base::SocketAddress kStunServerAddress("127.0.0.1", 7000);
const talk_base::SocketAddress kStunServerAddress2("127.0.0.1", 7001);

const talk_base::SocketAddress kRelayServerIntAddress("127.0.0.1", 7002);
const talk_base::SocketAddress kRelayServerExtAddress("127.0.0.1", 7003);

const int kNumPorts = 2;

int gPort = 28653;
int GetNextPort() {
  int p = gPort;
  gPort += 5;
  return p;
}

int gID = 0;
std::string GetNextID() {
  std::ostringstream ost;
  ost << gID++;
  return ost.str();
}

class TestPortAllocatorSession : public PortAllocatorSession {
public:
  TestPortAllocatorSession(talk_base::Thread* worker_thread, talk_base::SocketFactory* factory,
                           const std::string& name, const std::string& session_type)
      : PortAllocatorSession(0), worker_thread_(worker_thread),
        factory_(factory), name_(name), ports_(kNumPorts),
        address_("127.0.0.1", 0), network_("network", address_.ip()),
        running_(false) {
  }

  ~TestPortAllocatorSession() {
    for (int i = 0; i < ports_.size(); i++)
      delete ports_[i];
  }

  virtual void GetInitialPorts() {
    // These are the flags set by the raw transport.
    uint32 raw_flags = PORTALLOCATOR_DISABLE_UDP | PORTALLOCATOR_DISABLE_TCP;

    // If the client doesn't care, just give them two UDP ports.
    if (flags() == 0) {
      for (int i = 0; i < kNumPorts; i++) {
        ports_[i] = new UDPPort(worker_thread_, factory_, &network_,
                                GetAddress());
        AddPort(ports_[i]);
      }

    // If the client requested just stun and relay, we have to oblidge.
    } else if (flags() == raw_flags) {
      StunPort* sport = new StunPort(worker_thread_, factory_, &network_,
                                     GetAddress(), kStunServerAddress);
      sport->set_server_addr2(kStunServerAddress2);
      ports_[0] = sport;
      AddPort(sport);

      std::string username = CreateRandomString(16);
      std::string password = CreateRandomString(16);
      RelayPort* rport = new RelayPort(worker_thread_, factory_, &network_,
                                       GetAddress(), username, password, "");
      rport->AddServerAddress(
          ProtocolAddress(kRelayServerIntAddress, PROTO_UDP));
      ports_[1] = rport;
      AddPort(rport);
    } else {
      ASSERT(false);
    }
  }

  virtual void StartGetAllPorts() { running_ = true; }
  virtual void StopGetAllPorts() { running_ = false; }
  virtual bool IsGettingAllPorts() { return running_; }

  talk_base::SocketAddress GetAddress() const {
    talk_base::SocketAddress addr(address_);
    addr.SetPort(GetNextPort());
    return addr;
  }

  void AddPort(Port* port) {
    port->set_name(name_);
    port->set_preference(1.0);
    port->set_generation(0);
    port->SignalDestroyed.connect(
        this, &TestPortAllocatorSession::OnPortDestroyed);
    port->SignalAddressReady.connect(
        this, &TestPortAllocatorSession::OnAddressReady);
    port->PrepareAddress();
    SignalPortReady(this, port);
  }

  void OnPortDestroyed(Port* port) {
    for (int i = 0; i < ports_.size(); i++) {
      if (ports_[i] == port)
        ports_[i] = NULL;
    }
  }

  void OnAddressReady(Port* port) {
    SignalCandidatesReady(this, port->candidates());
  }

private:
  talk_base::Thread* worker_thread_;
  talk_base::SocketFactory* factory_;
  std::string name_;
  std::vector<Port*> ports_;
  talk_base::SocketAddress address_;
  talk_base::Network network_;
  bool running_;
};

class TestPortAllocator : public PortAllocator {
public:
  TestPortAllocator(talk_base::Thread* worker_thread, talk_base::SocketFactory* factory)
    : worker_thread_(worker_thread), factory_(factory) {
    if (factory_ == NULL)
      factory_ = worker_thread_->socketserver();
  }

  virtual PortAllocatorSession *CreateSession(const std::string &name, const std::string &session_type) {
    return new TestPortAllocatorSession(worker_thread_, factory_, name, session_type);
  }

private:
  talk_base::Thread* worker_thread_;
  talk_base::SocketFactory* factory_;
};

struct SessionManagerHandler : sigslot::has_slots<> {
  SessionManagerHandler(SessionManager* m, const std::string& u)
      : manager(m), username(u), create_count(0), destroy_count(0) {
    manager->SignalSessionCreate.connect(
        this, &SessionManagerHandler::OnSessionCreate);
    manager->SignalSessionDestroy.connect(
        this, &SessionManagerHandler::OnSessionDestroy);
    manager->SignalOutgoingMessage.connect(
        this, &SessionManagerHandler::OnOutgoingMessage);
    manager->SignalRequestSignaling.connect(
        this, &SessionManagerHandler::OnRequestSignaling);
  }

  void OnSessionCreate(Session *session, bool initiate) {
    create_count += 1;
    last_id = session->id();
  }

  void OnSessionDestroy(Session *session) {
    destroy_count += 1;
    last_id = session->id();
  }

  void OnOutgoingMessage(const XmlElement* stanza) {
    XmlElement* elem = new XmlElement(*stanza);
    ASSERT(elem->Name() == QN_IQ);
    ASSERT(elem->HasAttr(QN_TO));
    ASSERT(!elem->HasAttr(QN_FROM));
    ASSERT(elem->HasAttr(QN_TYPE));
    ASSERT((elem->Attr(QN_TYPE) == "set") ||
           (elem->Attr(QN_TYPE) == "result") ||
           (elem->Attr(QN_TYPE) == "error"));

    // Add in the appropriate "from".
    elem->SetAttr(QN_FROM, username);

    // Add in the appropriate IQ ID.
    if (elem->Attr(QN_TYPE) == "set") {
      ASSERT(!elem->HasAttr(QN_ID));
      elem->SetAttr(QN_ID, GetNextID());
    }

    stanzas_.push_back(elem);
  }

  void OnRequestSignaling() {
    manager->OnSignalingReady();
  }


  XmlElement* CheckNextStanza(const std::string& expected) {
    // Get the next stanza, which should exist.
    ASSERT(stanzas_.size() > 0);
    XmlElement* stanza = stanzas_.front();
    stanzas_.pop_front();

    // Make sure the stanza is correct.
    std::string actual = stanza->Str();
    if (actual != expected) {
      LOG(LERROR) << "Incorrect stanza: expected=\"" << expected
                  << "\" actual=\"" << actual << "\"";
      ASSERT(actual == expected);
    }

    return stanza;
  }

  void CheckNoStanza() {
    ASSERT(stanzas_.size() == 0);
  }

  void PrintNextStanza() {
    ASSERT(stanzas_.size() > 0);
    printf("Stanza: %s\n", stanzas_.front()->Str().c_str());
  }

  SessionManager* manager;
  std::string username;
  SessionID last_id;
  uint32 create_count;
  uint32 destroy_count;
  std::deque<XmlElement*> stanzas_;
};

struct SessionHandler : sigslot::has_slots<> {
  SessionHandler(Session* s) : session(s) {
    session->SignalState.connect(this, &SessionHandler::OnState);
    session->SignalError.connect(this, &SessionHandler::OnError);
  }

  void PrepareTransport() {
    Transport* transport = session->GetTransport(kNsP2pTransport);
    if (transport != NULL)
      transport->set_allow_local_ips(true);
  }

  void OnState(Session* session, Session::State state) {
    ASSERT(session == this->session);
    last_state = state;
  }

  void OnError(Session* session, Session::Error error) {
    ASSERT(session == this->session);
    ASSERT(false); // errors are bad!
  }

  Session* session;
  Session::State last_state;
};

struct MySessionClient: public SessionClient, public sigslot::has_slots<> {
  MySessionClient() : create_count(0), a(NULL), b(NULL) { }

  void AddManager(SessionManager* manager) {
    manager->AddClient(kSessionType, this);
    ASSERT(manager->GetClient(kSessionType) == this);
    manager->SignalSessionCreate.connect(
        this, &MySessionClient::OnSessionCreate);
  }

  const SessionDescription* CreateSessionDescription(
      const XmlElement* element) {
    return new SessionDescription();
  }

  XmlElement* TranslateSessionDescription(
      const SessionDescription* description) {
    return new XmlElement(QName(kSessionType, "description"));
  }

  void OnSessionCreate(Session *session, bool initiate) {
    create_count += 1;
    a = session->CreateChannel("a");
    b = session->CreateChannel("b");

    if (transport_name.size() > 0)
      session->SetPotentialTransports(&transport_name, 1);
  }

  void OnSessionDestroy(Session *session)
  {
  }
  
  void SetTransports(bool p2p, bool raw) {
    if (p2p && raw)
      return;  // this is the default

    if (p2p) {
      transport_name = kNsP2pTransport;
    }
  }

  int create_count;
  TransportChannel* a;
  TransportChannel* b;
  std::string transport_name;
};

struct ChannelHandler : sigslot::has_slots<> {
  ChannelHandler(TransportChannel* p)
    : channel(p), last_readable(false), last_writable(false), data_count(0),
      last_size(0) {
    p->SignalReadableState.connect(this, &ChannelHandler::OnReadableState);
    p->SignalWritableState.connect(this, &ChannelHandler::OnWritableState);
    p->SignalReadPacket.connect(this, &ChannelHandler::OnReadPacket);
  }

  void OnReadableState(TransportChannel* p) {
    ASSERT(p == channel);
    last_readable = channel->readable();
  }

  void OnWritableState(TransportChannel* p) {
    ASSERT(p == channel);
    last_writable = channel->writable();
  }

  void OnReadPacket(TransportChannel* p, const char* buf, size_t size) {
    ASSERT(p == channel);
    ASSERT(size <= sizeof(last_data));
    data_count += 1;
    last_size = size;
    std::memcpy(last_data, buf, size);
  }

  void Send(const char* data, size_t size) {
    int result = channel->SendPacket(data, size);
    ASSERT(result == static_cast<int>(size));
  }

  TransportChannel* channel;
  bool last_readable, last_writable;
  int data_count;
  char last_data[4096];
  size_t last_size;
};

char* Reverse(const char* str) {
  int len = strlen(str);
  char* rev = new char[len+1];
  for (int i = 0; i < len; i++)
    rev[i] = str[len-i-1];
  rev[len] = '\0';
  return rev;
}

// Sets up values that should be the same for every test.
void InitTest() {
  SetRandomSeed(7);
  gPort = 28653;
  gID = 0;
}

// Tests having client2 accept the session.
void TestAccept(talk_base::Thread* signaling_thread,
                Session* session1, Session* session2,
                SessionHandler* handler1, SessionHandler* handler2,
                SessionManager* manager1, SessionManager* manager2,
                SessionManagerHandler* manhandler1,
                SessionManagerHandler* manhandler2) {
  // Make sure the IQ ID is 5.
  ASSERT(gID <= 5);
  while (gID < 5) GetNextID();

  // Accept the session.
  SessionDescription* desc2 = new SessionDescription();
  bool valid = session2->Accept(desc2);
  ASSERT(valid);

  scoped_ptr<buzz::XmlElement> stanza;
  stanza.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"5\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"accept\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<ses:description xmlns:ses=\"http://oink.splat/session\"/>"
    "</session>"
    "</cli:iq>"));
  manhandler2->CheckNoStanza();

  // Simulate a tiny delay in sending.
  signaling_thread->ProcessMessages(10);

  // Delivery the accept.
  manager1->OnIncomingMessage(stanza.get());
  stanza.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" id=\"5\" type=\"result\" from=\"foo@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));
  manhandler1->CheckNoStanza();

  // Both sessions should be in progress after a short wait.
  signaling_thread->ProcessMessages(10);
  ASSERT(handler1->last_state == Session::STATE_INPROGRESS);
  ASSERT(handler2->last_state == Session::STATE_INPROGRESS);
}

// Tests sending data between two clients, over two channels.
void TestSendRecv(ChannelHandler* chanhandler1a, ChannelHandler* chanhandler1b,
                  ChannelHandler* chanhandler2a, ChannelHandler* chanhandler2b,
                  talk_base::Thread* signaling_thread, bool first_dropped) {
  const char* dat1a = "spamspamspamspamspamspamspambakedbeansspam";
  const char* dat1b = "Lobster Thermidor a Crevette with a mornay sauce...";
  const char* dat2a = Reverse(dat1a);
  const char* dat2b = Reverse(dat1b);

  // Sending from 2 -> 1 will enable 1 to send to 2 below.  That will then
  // enable 2 to send back to 1.  So the code below will just work.
  if (first_dropped) {
    chanhandler2a->Send(dat2a, strlen(dat2a));
    chanhandler2b->Send(dat2b, strlen(dat2b));
  }

  for (int i = 0; i < 20; i++) {
    chanhandler1a->Send(dat1a, strlen(dat1a));
    chanhandler1b->Send(dat1b, strlen(dat1b));
    chanhandler2a->Send(dat2a, strlen(dat2a));
    chanhandler2b->Send(dat2b, strlen(dat2b));

    signaling_thread->ProcessMessages(10);

    ASSERT(chanhandler1a->data_count == i + 1);
    ASSERT(chanhandler1b->data_count == i + 1);
    ASSERT(chanhandler2a->data_count == i + 1);
    ASSERT(chanhandler2b->data_count == i + 1);

    ASSERT(chanhandler1a->last_size == strlen(dat2a));
    ASSERT(chanhandler1b->last_size == strlen(dat2b));
    ASSERT(chanhandler2a->last_size == strlen(dat1a));
    ASSERT(chanhandler2b->last_size == strlen(dat1b));

    ASSERT(std::memcmp(chanhandler1a->last_data, dat2a, strlen(dat2a)) == 0);
    ASSERT(std::memcmp(chanhandler1b->last_data, dat2b, strlen(dat2b)) == 0);
    ASSERT(std::memcmp(chanhandler2a->last_data, dat1a, strlen(dat1a)) == 0);
    ASSERT(std::memcmp(chanhandler2b->last_data, dat1b, strlen(dat1b)) == 0);
  }
}

// Tests a session between two clients.  The inputs indicate whether we should
// replace each client's output with what we would see from an old client.
void TestP2PCompatibility(const std::string& test_name, bool old1, bool old2) {
  InitTest();

  talk_base::Thread* signaling_thread = talk_base::Thread::Current();
  scoped_ptr<talk_base::Thread> worker_thread(new talk_base::Thread());
  worker_thread->Start();

  scoped_ptr<PortAllocator> allocator(
      new TestPortAllocator(worker_thread.get(), NULL));
  scoped_ptr<MySessionClient> client(new MySessionClient());
  client->SetTransports(true, false);

  scoped_ptr<SessionManager> manager1(
      new SessionManager(allocator.get(), worker_thread.get()));
  scoped_ptr<SessionManagerHandler> manhandler1(
      new SessionManagerHandler(manager1.get(), "foo@baz.com"));
  client->AddManager(manager1.get());

  Session* session1 = manager1->CreateSession("foo@baz.com", kSessionType);
  ASSERT(manhandler1->create_count == 1);
  ASSERT(manhandler1->last_id == session1->id());
  scoped_ptr<SessionHandler> handler1(new SessionHandler(session1));

  ASSERT(client->create_count == 1);
  TransportChannel* chan1a = client->a;
  ASSERT(chan1a->name() == "a");
  ASSERT(session1->GetChannel("a") == chan1a);
  scoped_ptr<ChannelHandler> chanhandler1a(new ChannelHandler(chan1a));
  TransportChannel* chan1b = client->b;
  ASSERT(chan1b->name() == "b");
  ASSERT(session1->GetChannel("b") == chan1b);
  scoped_ptr<ChannelHandler> chanhandler1b(new ChannelHandler(chan1b));

  SessionDescription* desc1 = new SessionDescription();
  ASSERT(session1->state() == Session::STATE_INIT);
  bool valid = session1->Initiate("bar@baz.com", NULL, desc1);
  ASSERT(valid);
  handler1->PrepareTransport();

  signaling_thread->ProcessMessages(100);

  ASSERT(handler1->last_state == Session::STATE_SENTINITIATE);
  scoped_ptr<XmlElement> stanza1, stanza2;
  stanza1.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"0\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"initiate\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<ses:description xmlns:ses=\"http://oink.splat/session\"/>"
    "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\"/>"
    "</session>"
    "</cli:iq>"));
  stanza2.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"1\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"transport-info\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\">"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28653\""
    " preference=\"1\" username=\"h0ISP4S5SJKH/9EY\" protocol=\"udp\""
    " generation=\"0\" password=\"UhnAmO5C89dD2dZ+\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28658\""
    " preference=\"1\" username=\"yid4vfB3zXPvrRB9\" protocol=\"udp\""
    " generation=\"0\" password=\"SqLXTvcEyriIo+Mj\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28663\""
    " preference=\"1\" username=\"NvT78D7WxPWM1KL8\" protocol=\"udp\""
    " generation=\"0\" password=\"+mV/QhOapXu4caPX\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28668\""
    " preference=\"1\" username=\"8EzB7MH+TYpIlSp/\" protocol=\"udp\""
    " generation=\"0\" password=\"h+MelLXupoK5aYqC\" type=\"local\""
    " network=\"network\"/>"
    "</p:transport>"
    "</session>"
    "</cli:iq>"));
  manhandler1->CheckNoStanza();

  // If the first client were old, the initiate would have no transports and
  // the candidates would be sent in a candidates message.
  if (old1) {
    stanza1.reset(XmlElement::ForStr(
      "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"0\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"initiate\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<ses:description xmlns:ses=\"http://oink.splat/session\"/>"
      "</session>"
      "</cli:iq>"));
    stanza2.reset(XmlElement::ForStr(
      "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"1\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"candidates\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28653\""
      " preference=\"1\" username=\"h0ISP4S5SJKH/9EY\" protocol=\"udp\""
      " generation=\"0\" password=\"UhnAmO5C89dD2dZ+\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28658\""
      " preference=\"1\" username=\"yid4vfB3zXPvrRB9\" protocol=\"udp\""
      " generation=\"0\" password=\"SqLXTvcEyriIo+Mj\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28663\""
      " preference=\"1\" username=\"NvT78D7WxPWM1KL8\" protocol=\"udp\""
      " generation=\"0\" password=\"+mV/QhOapXu4caPX\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28668\""
      " preference=\"1\" username=\"8EzB7MH+TYpIlSp/\" protocol=\"udp\""
      " generation=\"0\" password=\"h+MelLXupoK5aYqC\" type=\"local\""
      " network=\"network\"/>"
      "</session>"
      "</cli:iq>"));
  }

  scoped_ptr<SessionManager> manager2(
      new SessionManager(allocator.get(), worker_thread.get()));
  scoped_ptr<SessionManagerHandler> manhandler2(
      new SessionManagerHandler(manager2.get(), "bar@baz.com"));
  client->AddManager(manager2.get());

  // Deliver the initiate.
  manager2->OnIncomingMessage(stanza1.get());
  stanza1.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" id=\"0\" type=\"result\" from=\"bar@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));

  // If client1 is old, we will not see a transport-accept.  If client2 is old,
  // then we should act as if it did not send one.
  if (!old1) {
    stanza1.reset(manhandler2->CheckNextStanza(
      "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"2\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\""
      " type=\"transport-accept\" id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\"/>"
      "</session>"
      "</cli:iq>"));
  } else {
    GetNextID();  // Advance the ID count to be the same in all cases.
    stanza1.reset(NULL);
  }
  if (old2) {
    stanza1.reset(NULL);
  }
  manhandler2->CheckNoStanza();
  ASSERT(manhandler2->create_count == 1);
  ASSERT(manhandler2->last_id == session1->id());

  Session* session2 = manager2->GetSession(session1->id());
  ASSERT(session2);
  ASSERT(session1->id() == session2->id());
  ASSERT(manhandler2->last_id == session2->id());
  ASSERT(session2->state() == Session::STATE_RECEIVEDINITIATE);
  scoped_ptr<SessionHandler> handler2(new SessionHandler(session2));
  handler2->PrepareTransport();

  ASSERT(session2->name() == session1->remote_name());
  ASSERT(session1->name() == session2->remote_name());

  ASSERT(session2->transport() != NULL);
  ASSERT(session2->transport()->name() == kNsP2pTransport);

  ASSERT(client->create_count == 2);
  TransportChannel* chan2a = client->a;
  scoped_ptr<ChannelHandler> chanhandler2a(new ChannelHandler(chan2a));
  TransportChannel* chan2b = client->b;
  scoped_ptr<ChannelHandler> chanhandler2b(new ChannelHandler(chan2b));

  // Deliver the candidates.
  manager2->OnIncomingMessage(stanza2.get());
  stanza2.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" id=\"1\" type=\"result\" from=\"bar@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));

  signaling_thread->ProcessMessages(10);

  // If client1 is old, we should see a candidates message instead of a
  // transport-info.  If client2 is old, we should act as if we did.
  const char* kCandidates2 =
    "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"3\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"candidates\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28673\""
    " preference=\"1\" username=\"FJDz3iuXjbQJDRjs\" protocol=\"udp\""
    " generation=\"0\" password=\"Ca5daV9m6G91qhlM\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28678\""
    " preference=\"1\" username=\"xlN53r3Jn/R5XuCt\" protocol=\"udp\""
    " generation=\"0\" password=\"rgik2pKsjaPSUdJd\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28683\""
    " preference=\"1\" username=\"IBZ8CSq8ot2+pSMp\" protocol=\"udp\""
    " generation=\"0\" password=\"i7RcDsGntMI6fzdd\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28688\""
    " preference=\"1\" username=\"SEtih9PYtMHCAlMI\" protocol=\"udp\""
    " generation=\"0\" password=\"wROrHJ3+gDxUUMp1\" type=\"local\""
    " network=\"network\"/>"
    "</session>"
    "</cli:iq>";
  if (old1) {
    stanza2.reset(manhandler2->CheckNextStanza(kCandidates2));
  } else {
    stanza2.reset(manhandler2->CheckNextStanza(
      "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"3\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"transport-info\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\">"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28673\""
      " preference=\"1\" username=\"FJDz3iuXjbQJDRjs\" protocol=\"udp\""
      " generation=\"0\" password=\"Ca5daV9m6G91qhlM\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28678\""
      " preference=\"1\" username=\"xlN53r3Jn/R5XuCt\" protocol=\"udp\""
      " generation=\"0\" password=\"rgik2pKsjaPSUdJd\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28683\""
      " preference=\"1\" username=\"IBZ8CSq8ot2+pSMp\" protocol=\"udp\""
      " generation=\"0\" password=\"i7RcDsGntMI6fzdd\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28688\""
      " preference=\"1\" username=\"SEtih9PYtMHCAlMI\" protocol=\"udp\""
      " generation=\"0\" password=\"wROrHJ3+gDxUUMp1\" type=\"local\""
      " network=\"network\"/>"
      "</p:transport>"
      "</session>"
      "</cli:iq>"));
  }
  if (old2) {
    stanza2.reset(XmlElement::ForStr(kCandidates2));
  }
  manhandler2->CheckNoStanza();

  // Deliver the transport-accept if one exists.
  if (stanza1.get() != NULL) {
    manager1->OnIncomingMessage(stanza1.get());
    stanza1.reset(manhandler1->CheckNextStanza(
      "<cli:iq to=\"bar@baz.com\" id=\"2\" type=\"result\" from=\"foo@baz.com\""
      " xmlns:cli=\"jabber:client\"/>"));
    manhandler1->CheckNoStanza();

    // The first session should now have a transport.
    ASSERT(session1->transport() != NULL);
    ASSERT(session1->transport()->name() == kNsP2pTransport);
  }

  // Deliver the candidates.  If client2 is old (or is acting old because
  // client1 is), then client1 will correct its earlier mistake of sending
  // transport-info by sending a candidates message.  If client1 is supposed to
  // be old, then it sent candidates earlier, so we drop this.
  manager1->OnIncomingMessage(stanza2.get());
  if (old1 || old2)  {
    stanza2.reset(manhandler1->CheckNextStanza(
      "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"4\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"candidates\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28653\""
      " preference=\"1\" username=\"h0ISP4S5SJKH/9EY\" protocol=\"udp\""
      " generation=\"0\" password=\"UhnAmO5C89dD2dZ+\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28658\""
      " preference=\"1\" username=\"yid4vfB3zXPvrRB9\" protocol=\"udp\""
      " generation=\"0\" password=\"SqLXTvcEyriIo+Mj\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28663\""
      " preference=\"1\" username=\"NvT78D7WxPWM1KL8\" protocol=\"udp\""
      " generation=\"0\" password=\"+mV/QhOapXu4caPX\" type=\"local\""
      " network=\"network\"/>"
      "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28668\""
      " preference=\"1\" username=\"8EzB7MH+TYpIlSp/\" protocol=\"udp\""
      " generation=\"0\" password=\"h+MelLXupoK5aYqC\" type=\"local\""
      " network=\"network\"/>"
      "</session>"
      "</cli:iq>"));
  } else {
    GetNextID();  // Advance the ID count to be the same in all cases.
    stanza2.reset(NULL);
  }
  if (old1) {
    stanza2.reset(NULL);
  }
  stanza1.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" id=\"3\" type=\"result\" from=\"foo@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));
  manhandler1->CheckNoStanza();

  // The first session must have a transport in either case now.
  ASSERT(session1->transport() != NULL);
  ASSERT(session1->transport()->name() == kNsP2pTransport);

  // If client1 just generated a candidates message, then we must deliver it.
  if (stanza2.get() != NULL) {
    manager2->OnIncomingMessage(stanza2.get());
    stanza2.reset(manhandler2->CheckNextStanza(
      "<cli:iq to=\"foo@baz.com\" id=\"4\" type=\"result\" from=\"bar@baz.com\""
      " xmlns:cli=\"jabber:client\"/>"));
    manhandler2->CheckNoStanza();
  }

  // The channels should be able to become writable at this point.  This
  // requires pinging, so it may take a little while.
  signaling_thread->ProcessMessages(500);
  ASSERT(chan1a->writable() && chan1a->readable());
  ASSERT(chan1b->writable() && chan1b->readable());
  ASSERT(chan2a->writable() && chan2a->readable());
  ASSERT(chan2b->writable() && chan2b->readable());
  ASSERT(chanhandler1a->last_writable);
  ASSERT(chanhandler1b->last_writable);
  ASSERT(chanhandler2a->last_writable);
  ASSERT(chanhandler2b->last_writable);

  // Accept the session.
  TestAccept(signaling_thread, session1, session2,
             handler1.get(), handler2.get(),
             manager1.get(), manager2.get(),
             manhandler1.get(), manhandler2.get());

  // Send a bunch of data between them.
  TestSendRecv(chanhandler1a.get(), chanhandler1b.get(), chanhandler2a.get(),
               chanhandler2b.get(), signaling_thread, false);

  manager1->DestroySession(session1);
  manager2->DestroySession(session2);

  ASSERT(manhandler1->create_count == 1);
  ASSERT(manhandler2->create_count == 1);
  ASSERT(manhandler1->destroy_count == 1);
  ASSERT(manhandler2->destroy_count == 1);

  worker_thread->Stop();

  std::cout << "P2P Compatibility: " << test_name << ": PASS" << std::endl;
}

// Tests the P2P transport.  The flags indicate whether they clients will
// advertise support for raw as well.
void TestP2P(const std::string& test_name, bool raw1, bool raw2) {
  InitTest();

  talk_base::Thread* signaling_thread = talk_base::Thread::Current();
  scoped_ptr<talk_base::Thread> worker_thread(new talk_base::Thread());
  worker_thread->Start();

  scoped_ptr<PortAllocator> allocator(
      new TestPortAllocator(worker_thread.get(), NULL));
  scoped_ptr<MySessionClient> client1(new MySessionClient());
  client1->SetTransports(true, raw1);
  scoped_ptr<MySessionClient> client2(new MySessionClient());
  client2->SetTransports(true, raw2);

  scoped_ptr<SessionManager> manager1(
      new SessionManager(allocator.get(), worker_thread.get()));
  scoped_ptr<SessionManagerHandler> manhandler1(
      new SessionManagerHandler(manager1.get(), "foo@baz.com"));
  client1->AddManager(manager1.get());

  Session* session1 = manager1->CreateSession("foo@baz.com", kSessionType);
  ASSERT(manhandler1->create_count == 1);
  ASSERT(manhandler1->last_id == session1->id());
  scoped_ptr<SessionHandler> handler1(new SessionHandler(session1));

  ASSERT(client1->create_count == 1);
  TransportChannel* chan1a = client1->a;
  ASSERT(chan1a->name() == "a");
  ASSERT(session1->GetChannel("a") == chan1a);
  scoped_ptr<ChannelHandler> chanhandler1a(new ChannelHandler(chan1a));
  TransportChannel* chan1b = client1->b;
  ASSERT(chan1b->name() == "b");
  ASSERT(session1->GetChannel("b") == chan1b);
  scoped_ptr<ChannelHandler> chanhandler1b(new ChannelHandler(chan1b));

  SessionDescription* desc1 = new SessionDescription();
  ASSERT(session1->state() == Session::STATE_INIT);
  bool valid = session1->Initiate("bar@baz.com", NULL, desc1);
  ASSERT(valid);
  handler1->PrepareTransport();

  signaling_thread->ProcessMessages(100);

  ASSERT(handler1->last_state == Session::STATE_SENTINITIATE);
  scoped_ptr<XmlElement> stanza1, stanza2;
  if (raw1) {
    stanza1.reset(manhandler1->CheckNextStanza(
      "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"0\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"initiate\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<ses:description xmlns:ses=\"http://oink.splat/session\"/>"
      "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\"/>"
      "<raw:transport xmlns:raw=\"http://www.google.com/transport/raw\"/>"
      "</session>"
      "</cli:iq>"));
  } else {
    stanza1.reset(manhandler1->CheckNextStanza(
      "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"0\""
      " xmlns:cli=\"jabber:client\">"
      "<session xmlns=\"http://www.google.com/session\" type=\"initiate\""
      " id=\"2154761789\" initiator=\"foo@baz.com\">"
      "<ses:description xmlns:ses=\"http://oink.splat/session\"/>"
      "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\"/>"
      "</session>"
      "</cli:iq>"));
  }
  stanza2.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" type=\"set\" from=\"foo@baz.com\" id=\"1\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"transport-info\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\">"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28653\""
    " preference=\"1\" username=\"h0ISP4S5SJKH/9EY\" protocol=\"udp\""
    " generation=\"0\" password=\"UhnAmO5C89dD2dZ+\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28658\""
    " preference=\"1\" username=\"yid4vfB3zXPvrRB9\" protocol=\"udp\""
    " generation=\"0\" password=\"SqLXTvcEyriIo+Mj\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28663\""
    " preference=\"1\" username=\"NvT78D7WxPWM1KL8\" protocol=\"udp\""
    " generation=\"0\" password=\"+mV/QhOapXu4caPX\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28668\""
    " preference=\"1\" username=\"8EzB7MH+TYpIlSp/\" protocol=\"udp\""
    " generation=\"0\" password=\"h+MelLXupoK5aYqC\" type=\"local\""
    " network=\"network\"/>"
    "</p:transport>"
    "</session>"
    "</cli:iq>"));
  manhandler1->CheckNoStanza();

  scoped_ptr<SessionManager> manager2(
      new SessionManager(allocator.get(), worker_thread.get()));
  scoped_ptr<SessionManagerHandler> manhandler2(
      new SessionManagerHandler(manager2.get(), "bar@baz.com"));
  client2->AddManager(manager2.get());

  // Deliver the initiate.
  manager2->OnIncomingMessage(stanza1.get());
  stanza1.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" id=\"0\" type=\"result\" from=\"bar@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));
  stanza1.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"2\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\""
    " type=\"transport-accept\" id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\"/>"
    "</session>"
    "</cli:iq>"));
  manhandler2->CheckNoStanza();
  ASSERT(manhandler2->create_count == 1);
  ASSERT(manhandler2->last_id == session1->id());

  Session* session2 = manager2->GetSession(session1->id());
  ASSERT(session2);
  ASSERT(session1->id() == session2->id());
  ASSERT(manhandler2->last_id == session2->id());
  ASSERT(session2->state() == Session::STATE_RECEIVEDINITIATE);
  scoped_ptr<SessionHandler> handler2(new SessionHandler(session2));
  handler2->PrepareTransport();

  ASSERT(session2->name() == session1->remote_name());
  ASSERT(session1->name() == session2->remote_name());

  ASSERT(session2->transport() != NULL);
  ASSERT(session2->transport()->name() == kNsP2pTransport);

  ASSERT(client2->create_count == 1);
  TransportChannel* chan2a = client2->a;
  scoped_ptr<ChannelHandler> chanhandler2a(new ChannelHandler(chan2a));
  TransportChannel* chan2b = client2->b;
  scoped_ptr<ChannelHandler> chanhandler2b(new ChannelHandler(chan2b));

  // Deliver the candidates.
  manager2->OnIncomingMessage(stanza2.get());
  stanza2.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" id=\"1\" type=\"result\" from=\"bar@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));

  signaling_thread->ProcessMessages(10);

  stanza2.reset(manhandler2->CheckNextStanza(
    "<cli:iq to=\"foo@baz.com\" type=\"set\" from=\"bar@baz.com\" id=\"3\""
    " xmlns:cli=\"jabber:client\">"
    "<session xmlns=\"http://www.google.com/session\" type=\"transport-info\""
    " id=\"2154761789\" initiator=\"foo@baz.com\">"
    "<p:transport xmlns:p=\"http://www.google.com/transport/p2p\">"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28673\""
    " preference=\"1\" username=\"FJDz3iuXjbQJDRjs\" protocol=\"udp\""
    " generation=\"0\" password=\"Ca5daV9m6G91qhlM\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"a\" address=\"127.0.0.1\" port=\"28678\""
    " preference=\"1\" username=\"xlN53r3Jn/R5XuCt\" protocol=\"udp\""
    " generation=\"0\" password=\"rgik2pKsjaPSUdJd\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28683\""
    " preference=\"1\" username=\"IBZ8CSq8ot2+pSMp\" protocol=\"udp\""
    " generation=\"0\" password=\"i7RcDsGntMI6fzdd\" type=\"local\""
    " network=\"network\"/>"
    "<candidate name=\"b\" address=\"127.0.0.1\" port=\"28688\""
    " preference=\"1\" username=\"SEtih9PYtMHCAlMI\" protocol=\"udp\""
    " generation=\"0\" password=\"wROrHJ3+gDxUUMp1\" type=\"local\""
    " network=\"network\"/>"
    "</p:transport>"
    "</session>"
    "</cli:iq>"));
  manhandler2->CheckNoStanza();

  // Deliver the transport-accept.
  manager1->OnIncomingMessage(stanza1.get());
  stanza1.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" id=\"2\" type=\"result\" from=\"foo@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));
  manhandler1->CheckNoStanza();

  // The first session should now have a transport.
  ASSERT(session1->transport() != NULL);
  ASSERT(session1->transport()->name() == kNsP2pTransport);

  // Deliver the candidates.
  manager1->OnIncomingMessage(stanza2.get());
  stanza1.reset(manhandler1->CheckNextStanza(
    "<cli:iq to=\"bar@baz.com\" id=\"3\" type=\"result\" from=\"foo@baz.com\""
    " xmlns:cli=\"jabber:client\"/>"));
  manhandler1->CheckNoStanza();

  // The channels should be able to become writable at this point.  This
  // requires pinging, so it may take a little while.
  signaling_thread->ProcessMessages(500);
  ASSERT(chan1a->writable() && chan1a->readable());
  ASSERT(chan1b->writable() && chan1b->readable());
  ASSERT(chan2a->writable() && chan2a->readable());
  ASSERT(chan2b->writable() && chan2b->readable());
  ASSERT(chanhandler1a->last_writable);
  ASSERT(chanhandler1b->last_writable);
  ASSERT(chanhandler2a->last_writable);
  ASSERT(chanhandler2b->last_writable);

  // Accept the session.
  TestAccept(signaling_thread, session1, session2,
             handler1.get(), handler2.get(),
             manager1.get(), manager2.get(),
             manhandler1.get(), manhandler2.get());

  // Send a bunch of data between them.
  TestSendRecv(chanhandler1a.get(), chanhandler1b.get(), chanhandler2a.get(),
               chanhandler2b.get(), signaling_thread, false);

  manager1->DestroySession(session1);
  manager2->DestroySession(session2);

  ASSERT(manhandler1->create_count == 1);
  ASSERT(manhandler2->create_count == 1);
  ASSERT(manhandler1->destroy_count == 1);
  ASSERT(manhandler2->destroy_count == 1);

  worker_thread->Stop();

  std::cout << "P2P: " << test_name << ": PASS" << std::endl;
}
//
int main(int argc, char* argv[]) {
  talk_base::LogMessage::LogToDebug(talk_base::LS_WARNING);

  TestP2P("{p2p} => {p2p}", false, false);
  TestP2P("{p2p} => {p2p,raw}", false, true);
  TestP2P("{p2p,raw} => {p2p}", true, false);
  TestP2P("{p2p,raw} => {p2p,raw}", true, true);
  TestP2PCompatibility("New => New", false, false);
  TestP2PCompatibility("Old => New", true, false);
  TestP2PCompatibility("New => Old", false, true);
  TestP2PCompatibility("Old => Old", true, true);

  return 0;
}
