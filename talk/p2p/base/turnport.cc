/*
 * libjingle
 * Copyright 2012, Google Inc.
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

#include "talk/p2p/base/turnport.h"

#include <functional>

#include "talk/base/asyncpacketsocket.h"
#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/nethelpers.h"
#include "talk/base/socketaddress.h"
#include "talk/base/stringencode.h"
#include "talk/p2p/base/common.h"
#include "talk/p2p/base/stun.h"

namespace cricket {

// TODO(juberti): Move to stun.h when relay messages have been renamed.
static const int TURN_ALLOCATE_REQUEST = STUN_ALLOCATE_REQUEST;
static const int TURN_ALLOCATE_ERROR_RESPONSE = STUN_ALLOCATE_ERROR_RESPONSE;

// TODO(juberti): Extract to turnmessage.h
static const int TURN_DEFAULT_PORT = 3478;
static const int TURN_CHANNEL_NUMBER_START = 0x4000;
static const int TURN_PERMISSION_TIMEOUT = 5 * 60 * 1000;  // 5 minutes

static const size_t TURN_CHANNEL_HEADER_SIZE = 4U;

inline bool IsTurnChannelData(uint16 msg_type) {
  return ((msg_type & 0xC000) == 0x4000);  // MSB are 0b01
}

class TurnAllocateRequest : public StunRequest {
 public:
  explicit TurnAllocateRequest(TurnPort* port);
  virtual void Prepare(StunMessage* request);
  virtual void OnResponse(StunMessage* response);
  virtual void OnErrorResponse(StunMessage* response);
  virtual void OnTimeout();

 private:
  // Handles authentication challenge from the server.
  void OnAuthChallenge(StunMessage* response, int code);
  void OnUnknownAttribute(StunMessage* response);

  TurnPort* port_;
};

class TurnRefreshRequest : public StunRequest {
 public:
  explicit TurnRefreshRequest(TurnPort* port);
  virtual void Prepare(StunMessage* request);
  virtual void OnResponse(StunMessage* response);
  virtual void OnErrorResponse(StunMessage* response);
  virtual void OnTimeout();

 private:
  TurnPort* port_;
};

class TurnCreatePermissionRequest : public StunRequest {
 public:
  TurnCreatePermissionRequest(TurnPort* port, TurnEntry* entry,
                              const talk_base::SocketAddress& ext_addr);
  virtual void Prepare(StunMessage* request);
  virtual void OnResponse(StunMessage* response);
  virtual void OnErrorResponse(StunMessage* response);
  virtual void OnTimeout();

 private:
  TurnPort* port_;
  TurnEntry* entry_;
  talk_base::SocketAddress ext_addr_;
};

class TurnChannelBindRequest : public StunRequest {
 public:
  TurnChannelBindRequest(TurnPort* port, TurnEntry* entry, int channel_id,
                         const talk_base::SocketAddress& ext_addr);
  virtual void Prepare(StunMessage* request);
  virtual void OnResponse(StunMessage* response);
  virtual void OnErrorResponse(StunMessage* response);
  virtual void OnTimeout();

 private:
  TurnPort* port_;
  TurnEntry* entry_;
  int channel_id_;
  talk_base::SocketAddress ext_addr_;
};

// Manages a "connection" to a remote destination. We will attempt to bring up
// a channel for this remote destination to reduce the overhead of sending data.
class TurnEntry : public sigslot::has_slots<> {
 public:
  enum BindState { STATE_UNBOUND, STATE_BINDING, STATE_BOUND };
  TurnEntry(TurnPort* port, int channel_id,
            const talk_base::SocketAddress& ext_addr);

  TurnPort* port() { return port_; }

  int channel_id() const { return channel_id_; }
  const talk_base::SocketAddress& address() const { return ext_addr_; }
  BindState state() const { return state_; }

  // Sends a packet to the given destination address.
  // This will wrap the packet in STUN if necessary.
  int Send(const void* data, size_t size, bool payload);

  void OnCreatePermissionSuccess();
  void OnCreatePermissionError();
  void OnChannelBindSuccess();
  void OnChannelBindError();

 private:
  TurnPort* port_;
  int channel_id_;
  talk_base::SocketAddress ext_addr_;
  BindState state_;
};

TurnPort::TurnPort(talk_base::Thread* thread,
                   talk_base::PacketSocketFactory* factory,
                   talk_base::Network* network,
                   const talk_base::IPAddress& ip,
                   int min_port, int max_port,
                   const std::string& username,
                   const std::string& password,
                   const talk_base::SocketAddress& server_address,
                   const RelayCredentials& credentials)
    : Port(thread, RELAY_PORT_TYPE, ICE_TYPE_PREFERENCE_RELAY,
           factory, network, ip, min_port, max_port,
           username, password),
      server_address_(server_address),
      credentials_(credentials),
      resolver_(NULL),
      error_(0),
      request_manager_(thread),
      next_channel_number_(TURN_CHANNEL_NUMBER_START) {
  request_manager_.SignalSendPacket.connect(this, &TurnPort::OnSendStunPacket);
}

TurnPort::~TurnPort() {
  // TODO(juberti): Should this even be necessary?
  while (!entries_.empty()) {
    DestroyEntry(entries_.front()->address());
  }
}

bool TurnPort::Init() {
  socket_.reset(socket_factory()->CreateUdpSocket(
      talk_base::SocketAddress(ip(), 0), min_port(), max_port()));
  if (!socket_) {
    LOG_J(LS_WARNING, this) << "UDP socket creation failed";
    return false;
  }
  socket_->SignalReadPacket.connect(this, &TurnPort::OnReadPacket);
  return true;
}

void TurnPort::PrepareAddress() {
  if (credentials_.username.empty() ||
      credentials_.password.empty()) {
    LOG(LS_ERROR) << "Allocation can't be started without setting the"
                  << " TURN server credentials for the user.";
    SignalAddressError(this);
    return;
  }

  if (!server_address_.port()) {
    server_address_.SetPort(TURN_DEFAULT_PORT);
  }

  if (server_address_.IsUnresolved()) {
    ResolveTurnAddress();
  } else {
    SendRequest(new TurnAllocateRequest(this), 0);
  }
}

Connection* TurnPort::CreateConnection(const Candidate& address,
                                       CandidateOrigin origin) {
  // TURN-UDP can only connect to UDP candidates.
  if (address.protocol() != "udp") {
    return NULL;
  }

  if (!IsCompatibleAddress(address.address())) {
    return NULL;
  }

  // Create an entry, if needed, so we can get our permissions set up correctly.
  CreateEntry(address.address());

  // TODO(juberti): The '0' index will need to change if we start gathering STUN
  // candidates on this port.
  ProxyConnection* conn = new ProxyConnection(this, 0, address);
  AddConnection(conn);
  return conn;
}

int TurnPort::SetOption(talk_base::Socket::Option opt, int value) {
  return socket_->SetOption(opt, value);
}

int TurnPort::GetOption(talk_base::Socket::Option opt, int* value) {
  return socket_->GetOption(opt, value);
}

int TurnPort::GetError() {
  return error_;
}

int TurnPort::SendTo(const void* data, size_t size,
                     const talk_base::SocketAddress& addr,
                     bool payload) {
  // Try to find an entry for this specific address; we should have one.
  TurnEntry* entry = FindEntry(addr);
  ASSERT(entry != NULL);
  if (!entry) {
    return 0;
  }

  // TODO(juberti): Wire this up once we support sending over connected
  // transports (e.g. TCP or TLS)
  // If the entry is connected, then we can send on it (though wrapping may
  // still be necessary).  Otherwise, we can't yet use this connection, so we
  // default to the first one.
  /*
  if (!connected()) {
    error_ = EWOULDBLOCK;
    return SOCKET_ERROR;
  }
  */

  // Send the actual contents to the server using the usual mechanism.
  int sent = entry->Send(data, size, payload);
  if (sent <= 0) {
    return SOCKET_ERROR;
  }

  // The caller of the function is expecting the number of user data bytes,
  // rather than the size of the packet.
  return size;
}

void TurnPort::OnReadPacket(talk_base::AsyncPacketSocket* socket,
                           const char* data, size_t size,
                           const talk_base::SocketAddress& remote_addr) {
  ASSERT(socket == socket_.get());
  ASSERT(remote_addr == server_address_);

  // The message must be at least the size of a channel header.
  if (size < TURN_CHANNEL_HEADER_SIZE) {
    LOG_J(LS_WARNING, this) << "Received TURN message that was too short";
    return;
  }

  // Check the message type, to see if is a Channel Data message.
  // The message will either be channel data, a TURN data indication, or
  // a response to a previous request.
  uint16 msg_type = talk_base::GetBE16(data);
  if (IsTurnChannelData(msg_type)) {
    HandleChannelData(msg_type, data, size);
  } else if (msg_type == TURN_DATA_INDICATION) {
    HandleDataIndication(data, size);
  } else {
    // This must be a response for one of our requests.
    // Check success responses, but not errors, for MESSAGE-INTEGRITY.
    if (IsStunSuccessResponseType(msg_type) &&
        !StunMessage::ValidateMessageIntegrity(data, size, hash())) {
      LOG_J(LS_WARNING, this) << "Received TURN message with invalid "
                              << "message integrity, msg_type=" << msg_type;
      return;
    }
    request_manager_.CheckResponse(data, size);
  }
}

void TurnPort::ResolveTurnAddress() {
  if (resolver_)
    return;

  resolver_ = new talk_base::AsyncResolver();
  resolver_->SignalWorkDone.connect(this, &TurnPort::OnResolveResult);
  resolver_->set_address(server_address_);
  resolver_->Start();
}

void TurnPort::OnResolveResult(talk_base::SignalThread* signal_thread) {
  ASSERT(signal_thread == resolver_);
  if (resolver_->error() != 0) {
    LOG_J(LS_WARNING, this) << "TURN host lookup received error "
                            << resolver_->error();
    SignalAddressError(this);
    return;
  }

  server_address_ = resolver_->address();
  PrepareAddress();
}

void TurnPort::OnSendStunPacket(const void* data, size_t size,
                                StunRequest* request) {
  if (Send(data, size) < 0) {
    LOG_J(LS_ERROR, this) << "Failed to send TURN message, err="
                          << socket_->GetError();
  }
}

void TurnPort::OnStunAddress(const talk_base::SocketAddress& address) {
  // For relay, mapped address is rel-addr.
  set_related_address(address);
}

void TurnPort::OnAllocateSuccess(const talk_base::SocketAddress& address) {
  AddAddress(address, socket_->GetLocalAddress(), "udp",
             RELAY_PORT_TYPE, ICE_TYPE_PREFERENCE_RELAY, true);
}

void TurnPort::OnAllocateError() {
  SignalAddressError(this);
}

void TurnPort::HandleDataIndication(const char* data, size_t size) {
  // Read in the message, and process according to RFC5766, Section 10.4.
  talk_base::ByteBuffer buf(data, size);
  TurnMessage msg;
  if (!msg.Read(&buf)) {
    LOG_J(LS_WARNING, this) << "Received invalid TURN data indication";
    return;
  }

  // Check mandatory attributes.
  const StunAddressAttribute* addr_attr =
      msg.GetAddress(STUN_ATTR_XOR_PEER_ADDRESS);
  if (!addr_attr) {
    LOG_J(LS_WARNING, this) << "Missing STUN_ATTR_XOR_PEER_ADDRESS attribute "
                            << "in data indication.";
    return;
  }

  const StunByteStringAttribute* data_attr =
      msg.GetByteString(STUN_ATTR_DATA);
  if (!data_attr) {
    LOG_J(LS_WARNING, this) << "Missing STUN_ATTR_DATA attribute in "
                            << "data indication.";
    return;
  }

  // Verify that the data came from somewhere we think we have a permission for.
  talk_base::SocketAddress ext_addr(addr_attr->GetAddress());
  if (!HasPermission(ext_addr.ipaddr())) {
    LOG_J(LS_WARNING, this) << "Received TURN data indication with invalid "
                            << "peer address, addr=" << ext_addr.ToString();
    return;
  }

  DispatchPacket(data_attr->bytes(), data_attr->length(), ext_addr, PROTO_UDP);
}

void TurnPort::HandleChannelData(int channel_id, const char* data,
                                 size_t size) {
  // Read the message, and process according to RFC5766, Section 11.6.
  //    0                   1                   2                   3
  //    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |         Channel Number        |            Length             |
  //   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //   |                                                               |
  //   /                       Application Data                        /
  //   /                                                               /
  //   |                                                               |
  //   |                               +-------------------------------+
  //   |                               |
  //   +-------------------------------+

  // Extract header fields from the message.
  uint16 len = talk_base::GetBE16(data + 2);
  if (len != size - TURN_CHANNEL_HEADER_SIZE) {
    LOG_J(LS_WARNING, this) << "Received TURN channel data message with "
                            << "incorrect length, len=" << len;
    return;
  }

  // Ensure this is a channel we know about.
  TurnEntry* entry = FindEntry(channel_id);
  if (!entry) {
    LOG_J(LS_WARNING, this) << "Received TURN channel data message for invalid "
                            << "channel, channel_id=" << channel_id;
    return;
  }

  DispatchPacket(data + TURN_CHANNEL_HEADER_SIZE, len, entry->address(),
                 PROTO_UDP);
}

void TurnPort::DispatchPacket(const char* data, size_t size,
    const talk_base::SocketAddress& remote_addr, ProtocolType proto) {
  if (Connection* conn = GetConnection(remote_addr)) {
    conn->OnReadPacket(data, size);
  } else {
    Port::OnReadPacket(data, size, remote_addr, proto);
  }
}

bool TurnPort::ScheduleRefresh(int lifetime) {
  // Lifetime is in seconds; we schedule a refresh for one minute less.
  if (lifetime < 2 * 60) {
    LOG_J(LS_WARNING, this) << "Received response with lifetime that was "
                            << "too short, lifetime=" << lifetime;
    return false;
  }

  SendRequest(new TurnRefreshRequest(this), (lifetime - 60) * 1000);
  return true;
}

void TurnPort::SendRequest(StunRequest* req, int delay) {
  request_manager_.SendDelayed(req, delay);
}

void TurnPort::AddRequestAuthInfo(StunMessage* msg) {
  // If we've gotten the necessary data from the server, add it to our request.
  VERIFY(!hash_.empty());
  VERIFY(msg->AddAttribute(new StunByteStringAttribute(
      STUN_ATTR_USERNAME, credentials_.username)));
  VERIFY(msg->AddAttribute(new StunByteStringAttribute(
      STUN_ATTR_REALM, realm_)));
  VERIFY(msg->AddAttribute(new StunByteStringAttribute(
      STUN_ATTR_NONCE, nonce_)));
  VERIFY(msg->AddMessageIntegrity(hash()));
}

int TurnPort::Send(const void* data, size_t len) {
  return socket_->SendTo(data, len, server_address_);
}

void TurnPort::UpdateHash() {
  VERIFY(ComputeStunCredentialHash(credentials_.username, realm_,
                                   credentials_.password, &hash_));
}

static bool MatchesIP(TurnEntry* e, talk_base::IPAddress ipaddr) {
  return e->address().ipaddr() == ipaddr;
}
bool TurnPort::HasPermission(const talk_base::IPAddress& ipaddr) const {
  return (std::find_if(entries_.begin(), entries_.end(),
      std::bind2nd(std::ptr_fun(MatchesIP), ipaddr)) != entries_.end());
}

static bool MatchesAddress(TurnEntry* e, talk_base::SocketAddress addr) {
  return e->address() == addr;
}
TurnEntry* TurnPort::FindEntry(const talk_base::SocketAddress& addr) const {
  EntryList::const_iterator it = std::find_if(entries_.begin(), entries_.end(),
      std::bind2nd(std::ptr_fun(MatchesAddress), addr));
  return (it != entries_.end()) ? *it : NULL;
}

static bool MatchesChannelId(TurnEntry* e, int id) {
  return e->channel_id() == id;
}
TurnEntry* TurnPort::FindEntry(int channel_id) const {
  EntryList::const_iterator it = std::find_if(entries_.begin(), entries_.end(),
      std::bind2nd(std::ptr_fun(MatchesChannelId), channel_id));
  return (it != entries_.end()) ? *it : NULL;
}

TurnEntry* TurnPort::CreateEntry(const talk_base::SocketAddress& addr) {
  ASSERT(FindEntry(addr) == NULL);
  TurnEntry* entry = new TurnEntry(this, next_channel_number_++, addr);
  entries_.push_back(entry);
  return entry;
}

void TurnPort::DestroyEntry(const talk_base::SocketAddress& addr) {
  TurnEntry* entry = FindEntry(addr);
  ASSERT(entry != NULL);
  entries_.remove(entry);
  delete entry;
}

TurnAllocateRequest::TurnAllocateRequest(TurnPort* port)
    : StunRequest(new TurnMessage()),
      port_(port) {
}

void TurnAllocateRequest::Prepare(StunMessage* request) {
  // Create the request as indicated in RFC 5766, Section 6.1.
  request->SetType(TURN_ALLOCATE_REQUEST);
  StunUInt32Attribute* transport_attr = StunAttribute::CreateUInt32(
      STUN_ATTR_REQUESTED_TRANSPORT);
  transport_attr->SetValue(IPPROTO_UDP << 24);
  VERIFY(request->AddAttribute(transport_attr));
  if (!port_->hash().empty()) {
    port_->AddRequestAuthInfo(request);
  }
}

void TurnAllocateRequest::OnResponse(StunMessage* response) {
  // Check mandatory attributes as indicated in RFC5766, Section 6.3.
  const StunAddressAttribute* mapped_attr =
      response->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  if (!mapped_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_XOR_MAPPED_ADDRESS "
                             << "attribute in allocate success response";
    return;
  }

  // TODO(mallinath) - Use mapped address for STUN candidate.
  port_->OnStunAddress(mapped_attr->GetAddress());

  const StunAddressAttribute* relayed_attr =
      response->GetAddress(STUN_ATTR_XOR_RELAYED_ADDRESS);
  if (!relayed_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_XOR_RELAYED_ADDRESS "
                             << "attribute in allocate success response";
    return;
  }

  const StunUInt32Attribute* lifetime_attr =
      response->GetUInt32(STUN_ATTR_TURN_LIFETIME);
  if (!lifetime_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_TURN_LIFETIME attribute in "
                             << "allocate success response";
    return;
  }

  // Notify the port the allocate succeeded, and schedule a refresh request.
  port_->OnAllocateSuccess(relayed_attr->GetAddress());
  port_->ScheduleRefresh(lifetime_attr->value());
}

void TurnAllocateRequest::OnErrorResponse(StunMessage* response) {
  // Process error response according to RFC5766, Section 6.4.
  const StunErrorCodeAttribute* errorcode = response->GetErrorCode();
  switch (errorcode->code()) {
    case STUN_ERROR_UNAUTHORIZED:       // Unauthrorized.
    case STUN_ERROR_STALE_CREDENTIALS:  // Stale Nonce.
      OnAuthChallenge(response, errorcode->code());
      break;
    default:
      LOG_J(LS_WARNING, port_) << "Allocate response error, code="
                               << errorcode->code();
      port_->OnAllocateError();
  }
}

void TurnAllocateRequest::OnTimeout() {
  LOG_J(LS_WARNING, port_) << "Allocate response timeout";
}

void TurnAllocateRequest::OnAuthChallenge(StunMessage* response, int code) {
  // If we failed to authenticate even after we sent our credentials, fail hard.
  if (code == STUN_ERROR_UNAUTHORIZED && !port_->hash().empty()) {
    LOG_J(LS_WARNING, port_) << "Failed to authenticate with the server "
                             << "after challenge.";
    port_->OnAllocateError();
    return;
  }

  // Check the mandatory attributes.
  const StunByteStringAttribute* realm_attr =
      response->GetByteString(STUN_ATTR_REALM);
  if (!realm_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_REALM attribute in "
                             << "allocate unauthorized response.";
    return;
  }
  port_->set_realm(realm_attr->GetString());

  const StunByteStringAttribute* nonce_attr =
      response->GetByteString(STUN_ATTR_NONCE);
  if (!nonce_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_NONCE attribute in "
                             << "allocate unauthorized response.";
    return;
  }
  port_->set_nonce(nonce_attr->GetString());

  // Send another allocate request, with the received realm and nonce values.
  port_->SendRequest(new TurnAllocateRequest(port_), 0);
}

TurnRefreshRequest::TurnRefreshRequest(TurnPort* port)
    : StunRequest(new TurnMessage()),
      port_(port) {
}

void TurnRefreshRequest::Prepare(StunMessage* request) {
  // Create the request as indicated in RFC 5766, Section 7.1.
  // No attributes need to be included.
  request->SetType(TURN_REFRESH_REQUEST);
  port_->AddRequestAuthInfo(request);
}

void TurnRefreshRequest::OnResponse(StunMessage* response) {
  // Check mandatory attributes as indicated in RFC5766, Section 7.3.
  const StunUInt32Attribute* lifetime_attr =
      response->GetUInt32(STUN_ATTR_TURN_LIFETIME);
  if (!lifetime_attr) {
    LOG_J(LS_WARNING, port_) << "Missing STUN_ATTR_TURN_LIFETIME attribute in "
                             << "refresh success response.";
    return;
  }

  // Schedule a refresh based on the returned lifetime value.
  port_->ScheduleRefresh(lifetime_attr->value());
}

void TurnRefreshRequest::OnErrorResponse(StunMessage* response) {
  // TODO(juberti): Handle 437 error response as a success.
}

void TurnRefreshRequest::OnTimeout() {
}

TurnCreatePermissionRequest::TurnCreatePermissionRequest(
    TurnPort* port, TurnEntry* entry,
    const talk_base::SocketAddress& ext_addr)
    : StunRequest(new TurnMessage()),
      port_(port),
      entry_(entry),
      ext_addr_(ext_addr) {
}

void TurnCreatePermissionRequest::Prepare(StunMessage* request) {
  // Create the request as indicated in RFC5766, Section 9.1.
  request->SetType(TURN_CREATE_PERMISSION_REQUEST);
  VERIFY(request->AddAttribute(new StunXorAddressAttribute(
      STUN_ATTR_XOR_PEER_ADDRESS, ext_addr_)));
  port_->AddRequestAuthInfo(request);
}

void TurnCreatePermissionRequest::OnResponse(StunMessage* response) {
  entry_->OnCreatePermissionSuccess();
}

void TurnCreatePermissionRequest::OnErrorResponse(StunMessage* response) {
  entry_->OnCreatePermissionError();
}

void TurnCreatePermissionRequest::OnTimeout() {
  LOG_J(LS_WARNING, port_) << "Create permission timeout";
}

TurnChannelBindRequest::TurnChannelBindRequest(
    TurnPort* port, TurnEntry* entry,
    int channel_id, const talk_base::SocketAddress& ext_addr)
    : StunRequest(new TurnMessage()),
      port_(port),
      entry_(entry),
      channel_id_(channel_id),
      ext_addr_(ext_addr) {
}

void TurnChannelBindRequest::Prepare(StunMessage* request) {
  // Create the request as indicated in RFC5766, Section 11.1.
  request->SetType(TURN_CHANNEL_BIND_REQUEST);
  VERIFY(request->AddAttribute(new StunUInt32Attribute(
      STUN_ATTR_CHANNEL_NUMBER, channel_id_ << 16)));
  VERIFY(request->AddAttribute(new StunXorAddressAttribute(
      STUN_ATTR_XOR_PEER_ADDRESS, ext_addr_)));
  port_->AddRequestAuthInfo(request);
}

void TurnChannelBindRequest::OnResponse(StunMessage* response) {
  entry_->OnChannelBindSuccess();
  // Refresh the channel binding just under the permission timeout
  // threshold. The channel binding has a longer lifetime, but
  // this is the easiest way to keep both the channel and the
  // permission from expiring.
  port_->SendRequest(new TurnChannelBindRequest(
      port_, entry_, channel_id_, ext_addr_),
      TURN_PERMISSION_TIMEOUT - 60 * 1000);
}

void TurnChannelBindRequest::OnErrorResponse(StunMessage* response) {
  entry_->OnChannelBindError();
}

void TurnChannelBindRequest::OnTimeout() {
  LOG_J(LS_WARNING, port_) << "Channel bind timeout";
}

TurnEntry::TurnEntry(TurnPort* port, int channel_id,
                     const talk_base::SocketAddress& ext_addr)
    : port_(port),
      channel_id_(channel_id),
      ext_addr_(ext_addr),
      state_(STATE_UNBOUND) {
  port_->SendRequest(new TurnCreatePermissionRequest(
      port_, this, ext_addr_), 0);
}

int TurnEntry::Send(const void* data, size_t size, bool payload) {
  talk_base::ByteBuffer buf;
  if (state_ != STATE_BOUND) {
    // If we haven't bound the channel yet, we have to use a Send Indication.
    TurnMessage msg;
    msg.SetType(TURN_SEND_INDICATION);
    msg.SetTransactionID(
        talk_base::CreateRandomString(kStunTransactionIdLength));
    VERIFY(msg.AddAttribute(new StunXorAddressAttribute(
        STUN_ATTR_XOR_PEER_ADDRESS, ext_addr_)));
    VERIFY(msg.AddAttribute(new StunByteStringAttribute(
        STUN_ATTR_DATA, data, size)));
    VERIFY(msg.Write(&buf));

    // If we're sending real data, request a channel bind that we can use later.
    if (state_ == STATE_UNBOUND && payload) {
      port_->SendRequest(new TurnChannelBindRequest(
          port_, this, channel_id_, ext_addr_), 0);
      state_ = STATE_BINDING;
    }
  } else {
    // If the channel is bound, we can send the data as a Channel Message.
    buf.WriteUInt16(channel_id_);
    buf.WriteUInt16(size);
    buf.WriteBytes(reinterpret_cast<const char*>(data), size);
  }
  return port_->Send(buf.Data(), buf.Length());
}

void TurnEntry::OnCreatePermissionSuccess() {
  LOG_J(LS_INFO, port_) << "Create permission for " << ext_addr_.ToString()
                        << " succeeded";
}

void TurnEntry::OnCreatePermissionError() {
  LOG_J(LS_WARNING, port_) << "Create permission for " << ext_addr_.ToString()
                           << " failed";
}

void TurnEntry::OnChannelBindSuccess() {
  LOG_J(LS_INFO, port_) << "Channel bind for " << ext_addr_.ToString()
                        << " succeeded";
  ASSERT(state_ == STATE_BINDING);
  state_ = STATE_BOUND;
}

void TurnEntry::OnChannelBindError() {
  // TODO(mallinath) - Implement handling of error response for channel
  // bind request as per http://tools.ietf.org/html/rfc5766#section-11.3
  LOG_J(LS_WARNING, port_) << "Channel bind for " << ext_addr_.ToString()
                           << " failed";
}

}  // namespace cricket
