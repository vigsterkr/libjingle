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

#include "talk/p2p/base/port.h"

#include <algorithm>
#include <vector>

#include "talk/base/base64.h"
#include "talk/base/crc32.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/messagedigest.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/stringutils.h"
#include "talk/p2p/base/common.h"

namespace {

// The length of time we wait before timing out readability on a connection.
const uint32 CONNECTION_READ_TIMEOUT = 30 * 1000;   // 30 seconds

// The length of time we wait before timing out writability on a connection.
const uint32 CONNECTION_WRITE_TIMEOUT = 15 * 1000;  // 15 seconds

// The length of time we wait before we become unwritable.
const uint32 CONNECTION_WRITE_CONNECT_TIMEOUT = 5 * 1000;  // 5 seconds

// The number of pings that must fail to respond before we become unwritable.
const uint32 CONNECTION_WRITE_CONNECT_FAILURES = 5;

// This is the length of time that we wait for a ping response to come back.
const int CONNECTION_RESPONSE_TIMEOUT = 5 * 1000;   // 5 seconds

// Determines whether we have seen at least the given maximum number of
// pings fail to have a response.
inline bool TooManyFailures(
    const std::vector<uint32>& pings_since_last_response,
    uint32 maximum_failures,
    uint32 rtt_estimate,
    uint32 now) {

  // If we haven't sent that many pings, then we can't have failed that many.
  if (pings_since_last_response.size() < maximum_failures)
    return false;

  // Check if the window in which we would expect a response to the ping has
  // already elapsed.
  return pings_since_last_response[maximum_failures - 1] + rtt_estimate < now;
}

// Determines whether we have gone too long without seeing any response.
inline bool TooLongWithoutResponse(
    const std::vector<uint32>& pings_since_last_response,
    uint32 maximum_time,
    uint32 now) {

  if (pings_since_last_response.size() == 0)
    return false;

  return pings_since_last_response[0] + maximum_time < now;
}

// GICE(ICEPROTO_GOOGLE) requires different username for RTP and RTCP.
// This function generates a different username by +1 on the last character of
// the given username (|rtp_ufrag|).
std::string GetRtcpUfragFromRtpUfrag(const std::string& rtp_ufrag) {
  ASSERT(!rtp_ufrag.empty());
  if (rtp_ufrag.empty()) {
    return rtp_ufrag;
  }
  // Change the last character to the one next to it in the base64 table.
  char new_last_char;
  if (!talk_base::Base64::GetNextBase64Char(rtp_ufrag[rtp_ufrag.size() - 1],
                                            &new_last_char)) {
    // Should not be here.
    ASSERT(false);
  }
  std::string rtcp_ufrag = rtp_ufrag;
  rtcp_ufrag[rtcp_ufrag.size() - 1] = new_last_char;
  ASSERT(rtcp_ufrag != rtp_ufrag);
  return rtcp_ufrag;
}

// We will restrict RTT estimates (when used for determining state) to be
// within a reasonable range.
const uint32 MINIMUM_RTT = 100;   // 0.1 seconds
const uint32 MAXIMUM_RTT = 3000;  // 3 seconds

// When we don't have any RTT data, we have to pick something reasonable.  We
// use a large value just in case the connection is really slow.
const uint32 DEFAULT_RTT = MAXIMUM_RTT;

// Computes our estimate of the RTT given the current estimate.
inline uint32 ConservativeRTTEstimate(uint32 rtt) {
  return talk_base::_max(MINIMUM_RTT, talk_base::_min(MAXIMUM_RTT, 2 * rtt));
}

// Weighting of the old rtt value to new data.
const int RTT_RATIO = 3;  // 3 : 1

// The delay before we begin checking if this port is useless.
const int kPortTimeoutDelay = 30 * 1000;  // 30 seconds

const uint32 MSG_CHECKTIMEOUT = 1;
const uint32 MSG_DELETE = 1;
}

namespace cricket {

static const char* const PROTO_NAMES[] = { "udp", "tcp", "ssltcp" };

// TODO: Use the priority values from RFC 5245.
const uint32 PRIORITY_LOCAL_UDP = 2130706432U;  // pref = 1.0
const uint32 PRIORITY_LOCAL_STUN = 1912602624U;  // pref = 0.9
const uint32 PRIORITY_LOCAL_TCP = 1694498816U;  // pref = 0.8
const uint32 PRIORITY_RELAY = 1056964608U;  // pref = 0.5

const char* ProtoToString(ProtocolType proto) {
  return PROTO_NAMES[proto];
}

bool StringToProto(const char* value, ProtocolType* proto) {
  for (size_t i = 0; i <= PROTO_LAST; ++i) {
    if (strcmp(PROTO_NAMES[i], value) == 0) {
      *proto = static_cast<ProtocolType>(i);
      return true;
    }
  }
  return false;
}

Port::Port(talk_base::Thread* thread, const std::string& type,
           talk_base::PacketSocketFactory* factory, talk_base::Network* network,
           const talk_base::IPAddress& ip, int min_port, int max_port,
           const std::string& username_fragment, const std::string& password)
    : thread_(thread),
      factory_(factory),
      type_(type),
      network_(network),
      ip_(ip),
      min_port_(min_port),
      max_port_(max_port),
      component_(ICE_CANDIDATE_COMPONENT_DEFAULT),
      priority_(0),
      generation_(0),
      ice_username_fragment_(username_fragment),
      password_(password),
      lifetime_(LT_PRESTART),
      enable_port_packets_(false),
      ice_protocol_(ICEPROTO_GOOGLE),
      role_(ROLE_UNKNOWN),
      tiebreaker_(0) {
  ASSERT(factory_ != NULL);
  // If the username_fragment and password are empty, we should just create one.
  if (ice_username_fragment_.empty()) {
    ASSERT(password_.empty());
    ice_username_fragment_ = talk_base::CreateRandomString(ICE_UFRAG_LENGTH);
    password_ = talk_base::CreateRandomString(ICE_PWD_LENGTH);
  }
  LOG_J(LS_INFO, this) << "Port created";
}

Port::~Port() {
  // Delete all of the remaining connections.  We copy the list up front
  // because each deletion will cause it to be modified.

  std::vector<Connection*> list;

  AddressMap::iterator iter = connections_.begin();
  while (iter != connections_.end()) {
    list.push_back(iter->second);
    ++iter;
  }

  for (uint32 i = 0; i < list.size(); i++)
    delete list[i];
}

Connection* Port::GetConnection(const talk_base::SocketAddress& remote_addr) {
  AddressMap::const_iterator iter = connections_.find(remote_addr);
  if (iter != connections_.end())
    return iter->second;
  else
    return NULL;
}

int Port::ComputeCandidatePriority(const talk_base::SocketAddress& address,
                                   int type_preference) const {
  int addr_preference = IPAddressPrecedence(address.ipaddr());
  int p = (type_preference << 24) | (addr_preference << 8) | (256 - component_);
  return p;
}

// Foundation:  An arbitrary string that is the same for two candidates
//   that have the same type, base IP address, protocol (UDP, TCP,
//   etc.), and STUN or TURN server.  If any of these are different,
//   then the foundation will be different.  Two candidate pairs with
//   the same foundation pairs are likely to have similar network
//   characteristics.  Foundations are used in the frozen algorithm.
uint32 Port::ComputeFoundation(
    const std::string& protocol,
    const talk_base::SocketAddress& base_address) const {
  std::ostringstream ost;
  ost << type_ << base_address.ipaddr().ToString() << protocol;
  return talk_base::ComputeCrc32(ost.str());
}

void Port::AddAddress(const talk_base::SocketAddress& address,
                      const talk_base::SocketAddress& base_address,
                      const std::string& protocol,
                      bool final) {
  Candidate c;
  c.set_id(talk_base::CreateRandomString(8));
  c.set_component(component_);
  c.set_type(type_);
  c.set_protocol(protocol);
  c.set_address(address);
  c.set_priority(ComputeCandidatePriority(address, priority_ >> 24));
  c.set_username(username_fragment());
  c.set_password(password_);
  c.set_network_name(network_->name());
  c.set_generation(generation_);
  c.set_related_address(related_address_);
  c.set_foundation(ComputeFoundation(protocol, base_address));
  candidates_.push_back(c);

  if (final) {
    // Set related address if it's already not set. This can happen in relay
    // scenario where related address will be set later.
    for (size_t i = 0; i < candidates_.size(); ++i) {
      candidates_[i].set_related_address(related_address_);
    }
    SignalAddressReady(this);
  }
}

void Port::AddConnection(Connection* conn) {
  connections_[conn->remote_candidate().address()] = conn;
  conn->SignalDestroyed.connect(this, &Port::OnConnectionDestroyed);
  SignalConnectionCreated(this, conn);
}

void Port::OnReadPacket(
    const char* data, size_t size, const talk_base::SocketAddress& addr) {
  // If the user has enabled port packets, just hand this over.
  if (enable_port_packets_) {
    SignalReadPacket(this, data, size, addr);
    return;
  }

  // If this is an authenticated STUN request, then signal unknown address and
  // send back a proper binding response.
  talk_base::scoped_ptr<IceMessage> msg;
  std::string remote_username;
  if (!GetStunMessage(data, size, addr, msg.accept(), &remote_username)) {
    LOG_J(LS_ERROR, this) << "Received non-STUN packet from unknown address ("
                          << addr.ToString() << ")";
  } else if (!msg.get()) {
    // STUN message handled already
  } else if (msg->type() == STUN_BINDING_REQUEST) {
    // Check for role conflicts.
    if (ice_protocol() == ICEPROTO_RFC5245 &&
        !MaybeIceRoleConflict(addr, msg.get())) {
      LOG(LS_INFO) << "Received conflicting role from the peer.";
      return;
    }

    SignalUnknownAddress(this, addr, msg.get(), remote_username, false);
  } else {
    // NOTE(tschmelcher): STUN_BINDING_RESPONSE is benign. It occurs if we
    // pruned a connection for this port while it had STUN requests in flight,
    // because we then get back responses for them, which this code correctly
    // does not handle.
    if (msg->type() != STUN_BINDING_RESPONSE) {
      LOG_J(LS_ERROR, this) << "Received unexpected STUN message type ("
                            << msg->type() << ") from unknown address ("
                            << addr.ToString() << ")";
    }
  }
}

bool Port::GetStunMessage(const char* data, size_t size,
                          const talk_base::SocketAddress& addr,
                          IceMessage** out_msg, std::string* out_username) {
  // NOTE: This could clearly be optimized to avoid allocating any memory.
  //       However, at the data rates we'll be looking at on the client side,
  //       this probably isn't worth worrying about.
  ASSERT(out_msg != NULL);
  ASSERT(out_username != NULL);
  *out_msg = NULL;
  out_username->clear();

  // Don't bother parsing the packet if we can tell it's not STUN.
  // In ICE mode, all STUN packets will have a valid fingerprint.
  if (ice_protocol_ == ICEPROTO_RFC5245 &&
    !StunMessage::ValidateFingerprint(data, size)) {
    return false;
  }

  // Parse the request message.  If the packet is not a complete and correct
  // STUN message, then ignore it.
  talk_base::scoped_ptr<IceMessage> stun_msg(new IceMessage());
  talk_base::ByteBuffer buf(data, size);
  if (!stun_msg->Read(&buf) || (buf.Length() > 0)) {
    return false;
  }

  if (stun_msg->type() == STUN_BINDING_REQUEST) {
    // Check for the presence of USERNAME and MESSAGE-INTEGRITY (if ICE) first.
    // If not present, fail with a 400 Bad Request.
    if (!stun_msg->GetByteString(STUN_ATTR_USERNAME) ||
        (ice_protocol_ == ICEPROTO_RFC5245 &&
            !stun_msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY))) {
      LOG_J(LS_ERROR, this) << "Received STUN request without username/M-I "
                            << "from " << addr.ToString();
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_BAD_REQUEST,
                               STUN_ERROR_REASON_BAD_REQUEST);
      return true;
    }

    // If the username is bad or unknown, fail with a 401 Unauthorized.
    std::string local_ufrag;
    std::string remote_ufrag;
    if (!ParseStunUsername(stun_msg.get(), &local_ufrag, &remote_ufrag) ||
        local_ufrag != username_fragment()) {
      LOG_J(LS_ERROR, this) << "Received STUN request with bad local username "
                            << local_ufrag << " from " << addr.ToString();
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                               STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }

    // If ICE, and the MESSAGE-INTEGRITY is bad, fail with a 401 Unauthorized
    if (ice_protocol_ == ICEPROTO_RFC5245 &&
        !stun_msg->ValidateMessageIntegrity(data, size, password_)) {
      LOG_J(LS_ERROR, this) << "Received STUN request with bad M-I "
                            << "from " << addr.ToString();
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                               STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }
    out_username->assign(remote_ufrag);
  } else if ((stun_msg->type() == STUN_BINDING_RESPONSE) ||
             (stun_msg->type() == STUN_BINDING_ERROR_RESPONSE)) {
    if (stun_msg->type() == STUN_BINDING_ERROR_RESPONSE) {
      if (const StunErrorCodeAttribute* error_code = stun_msg->GetErrorCode()) {
        LOG_J(LS_ERROR, this) << "Received STUN binding error:"
                              << " class=" << error_code->eclass()
                              << " number=" << error_code->number()
                              << " reason='" << error_code->reason() << "'"
                              << " from " << addr.ToString();
        // Return message to allow error-specific processing
      } else {
        LOG_J(LS_ERROR, this) << "Received STUN binding error without a error "
                              << "code from " << addr.ToString();
        return true;
      }
    }
    // NOTE: Username should not be used in verifying response messages.
    out_username->clear();
  } else {
    LOG_J(LS_ERROR, this) << "Received STUN packet with invalid type ("
                          << stun_msg->type() << ") from " << addr.ToString();
    return true;
  }

  // Return the STUN message found.
  *out_msg = stun_msg.release();
  return true;
}

bool Port::IsCompatibleAddress(const talk_base::SocketAddress& addr) {
  int family = ip().family();
  // We use single-stack sockets, so families must match.
  if (addr.family() != family) {
    return false;
  }
  // Link-local IPv6 ports can only connect to other link-local IPv6 ports.
  if (family == AF_INET6 && (IPIsPrivate(ip()) != IPIsPrivate(addr.ipaddr()))) {
    return false;
  }
  return true;
}

bool Port::ParseStunUsername(const StunMessage* stun_msg,
                             std::string* local_ufrag,
                             std::string* remote_ufrag) const {
  // The packet must include a username that either begins or ends with our
  // fragment.  It should begin with our fragment if it is a request and it
  // should end with our fragment if it is a response.
  local_ufrag->clear();
  remote_ufrag->clear();
  const StunByteStringAttribute* username_attr =
        stun_msg->GetByteString(STUN_ATTR_USERNAME);
  if (username_attr == NULL)
    return false;

  const std::string username_attr_str = username_attr->GetString();
  if (ice_protocol_ == ICEPROTO_RFC5245) {
    size_t colon_pos = username_attr_str.find(":");
    if (colon_pos != std::string::npos) {  // RFRAG:LFRAG
      *local_ufrag = username_attr_str.substr(0, colon_pos);
      *remote_ufrag = username_attr_str.substr(
          colon_pos + 1, username_attr_str.size());
    } else {
      return false;
    }
  } else if (ice_protocol_ == ICEPROTO_GOOGLE) {
    int remote_frag_len = username_attr_str.size();
    remote_frag_len -= static_cast<int>(username_fragment().size());
    if (remote_frag_len < 0)
      return false;

    *local_ufrag = username_attr_str.substr(0, username_fragment().size());
    *remote_ufrag = username_attr_str.substr(
        username_fragment().size(), username_attr_str.size());
  }
  return true;
}

bool Port::MaybeIceRoleConflict(
    const talk_base::SocketAddress& addr, IceMessage* stun_msg) {
  // Validate ICE_CONTROLLING or ICE_CONTROLLED attributes.
  bool ret = true;
  TransportRole remote_ice_role = ROLE_UNKNOWN;
  uint64 remote_tiebreaker = 0;
  const StunUInt64Attribute* stun_attr =
      stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  if (stun_attr) {
    remote_ice_role = ROLE_CONTROLLING;
    remote_tiebreaker = stun_attr->value();
  }
  stun_attr = stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED);
  if (stun_attr) {
    remote_ice_role = ROLE_CONTROLLED;
    remote_tiebreaker = stun_attr->value();
  }

  switch (role_) {
    case ROLE_CONTROLLING:
      if (ROLE_CONTROLLING == remote_ice_role) {
        if (remote_tiebreaker >= tiebreaker_) {
          SignalRoleConflict();
        } else {
          // Send Role Conflict (487) error response.
          SendBindingErrorResponse(stun_msg, addr,
              STUN_ERROR_ROLE_CONFLICT, STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    case ROLE_CONTROLLED:
      if (ROLE_CONTROLLED == remote_ice_role) {
        if (remote_tiebreaker < tiebreaker_) {
          SignalRoleConflict();
        } else {
          // Send Role Conflict (487) error response.
          SendBindingErrorResponse(stun_msg, addr,
              STUN_ERROR_ROLE_CONFLICT, STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    default:
      ASSERT(false);
  }
  return ret;
}

void Port::CreateStunUsername(const std::string& remote_username,
                              std::string* stun_username_attr_str) const {
  stun_username_attr_str->clear();
  *stun_username_attr_str = remote_username;
  if (ice_protocol_ == ICEPROTO_RFC5245) {
    // Connectivity checks from L->R will have username RFRAG:LFRAG.
    stun_username_attr_str->append(":");
  }
  stun_username_attr_str->append(username_fragment());
}

void Port::SendBindingResponse(StunMessage* request,
                               const talk_base::SocketAddress& addr) {
  ASSERT(request->type() == STUN_BINDING_REQUEST);

  // Retrieve the username from the request.
  const StunByteStringAttribute* username_attr =
      request->GetByteString(STUN_ATTR_USERNAME);
  ASSERT(username_attr != NULL);
  if (username_attr == NULL) {
    // No valid username, skip the response.
    return;
  }

  // Fill in the response message.
  StunMessage response;
  response.SetType(STUN_BINDING_RESPONSE);
  response.SetTransactionID(request->transaction_id());

  // Only GICE messages have USERNAME and MAPPED-ADDRESS in the response.
  // ICE messages use XOR-MAPPED-ADDRESS, and add MESSAGE-INTEGRITY.
  if (ice_protocol_ == ICEPROTO_RFC5245) {
    response.AddAttribute(
        new StunXorAddressAttribute(STUN_ATTR_XOR_MAPPED_ADDRESS, addr));
    response.AddMessageIntegrity(password_);
    response.AddFingerprint();
  } else if (ice_protocol_ == ICEPROTO_GOOGLE) {
    response.AddAttribute(
        new StunAddressAttribute(STUN_ATTR_MAPPED_ADDRESS, addr));
    response.AddAttribute(new StunByteStringAttribute(
        STUN_ATTR_USERNAME, username_attr->GetString()));
  }

  // Send the response message.
  talk_base::ByteBuffer buf;
  response.Write(&buf);
  if (SendTo(buf.Data(), buf.Length(), addr, false) < 0) {
    LOG_J(LS_ERROR, this) << "Failed to send STUN ping response to "
                          << addr.ToString();
  }

  // The fact that we received a successful request means that this connection
  // (if one exists) should now be readable.
  Connection* conn = GetConnection(addr);
  ASSERT(conn != NULL);
  if (conn)
    conn->ReceivedPing();
}

void Port::SendBindingErrorResponse(StunMessage* request,
                                    const talk_base::SocketAddress& addr,
                                    int error_code, const std::string& reason) {
  ASSERT(request->type() == STUN_BINDING_REQUEST);

  // Fill in the response message.
  StunMessage response;
  response.SetType(STUN_BINDING_ERROR_RESPONSE);
  response.SetTransactionID(request->transaction_id());

  // When doing GICE, we need to write out the error code incorrectly to
  // maintain backwards compatiblility.
  StunErrorCodeAttribute* error_attr = StunAttribute::CreateErrorCode();
  if (ice_protocol_ == ICEPROTO_RFC5245) {
    error_attr->SetCode(error_code);
  } else if (ice_protocol_ == ICEPROTO_GOOGLE) {
    error_attr->SetClass(error_code / 256);
    error_attr->SetNumber(error_code % 256);
  }
  error_attr->SetReason(reason);
  response.AddAttribute(error_attr);

  if (ice_protocol_ == ICEPROTO_RFC5245) {
    // Per Section 10.1.2, certain error cases don't get a MESSAGE-INTEGRITY,
    // because we don't have enough information to determine the shared secret.
    if (error_code != STUN_ERROR_BAD_REQUEST &&
        error_code != STUN_ERROR_UNAUTHORIZED)
      response.AddMessageIntegrity(password_);
    response.AddFingerprint();
  } else if (ice_protocol_ == ICEPROTO_GOOGLE) {
    // GICE responses include a username, if one exists.
    const StunByteStringAttribute* username_attr =
        request->GetByteString(STUN_ATTR_USERNAME);
    if (username_attr)
      response.AddAttribute(new StunByteStringAttribute(
          STUN_ATTR_USERNAME, username_attr->GetString()));
  }

  // Send the response message.
  talk_base::ByteBuffer buf;
  response.Write(&buf);
  SendTo(buf.Data(), buf.Length(), addr, false);
  LOG_J(LS_INFO, this) << "Sending STUN binding error: reason=" << reason
                       << " to " << addr.ToString();
}

void Port::OnMessage(talk_base::Message *pmsg) {
  ASSERT(pmsg->message_id == MSG_CHECKTIMEOUT);
  ASSERT(lifetime_ == LT_PRETIMEOUT);
  lifetime_ = LT_POSTTIMEOUT;
  CheckTimeout();
}

std::string Port::ToString() const {
  std::stringstream ss;
  ss << "Port[" << content_name_ << ":" << component_
     << ":" << generation_ << ":" << type_
     << ":" << network_->ToString() << "]";
  return ss.str();
}

void Port::EnablePortPackets() {
  enable_port_packets_ = true;
}

void Port::Start() {
  // The port sticks around for a minimum lifetime, after which
  // we destroy it when it drops to zero connections.
  if (lifetime_ == LT_PRESTART) {
    lifetime_ = LT_PRETIMEOUT;
    thread_->PostDelayed(kPortTimeoutDelay, this, MSG_CHECKTIMEOUT);
  } else {
    LOG_J(LS_WARNING, this) << "Port restart attempted";
  }
}

void Port::OnConnectionDestroyed(Connection* conn) {
  AddressMap::iterator iter =
      connections_.find(conn->remote_candidate().address());
  ASSERT(iter != connections_.end());
  connections_.erase(iter);

  CheckTimeout();
}

void Port::Destroy() {
  ASSERT(connections_.empty());
  LOG_J(LS_INFO, this) << "Port deleted";
  SignalDestroyed(this);
  delete this;
}

void Port::CheckTimeout() {
  // If this port has no connections, then there's no reason to keep it around.
  // When the connections time out (both read and write), they will delete
  // themselves, so if we have any connections, they are either readable or
  // writable (or still connecting).
  if ((lifetime_ == LT_POSTTIMEOUT) && connections_.empty()) {
    Destroy();
  }
}

const std::string Port::username_fragment() const {
  if (ice_protocol_ == ICEPROTO_GOOGLE &&
      component_ == ICE_CANDIDATE_COMPONENT_RTCP) {
    // In GICE mode, we should adjust username fragment for rtcp component.
    return GetRtcpUfragFromRtpUfrag(ice_username_fragment_);
  } else {
    return ice_username_fragment_;
  }
}

// A ConnectionRequest is a simple STUN ping used to determine writability.
class ConnectionRequest : public StunRequest {
 public:
  explicit ConnectionRequest(Connection* connection)
      : StunRequest(new IceMessage()),
        connection_(connection),
        use_candidate_(false) {
  }

  virtual ~ConnectionRequest() {
  }

  void set_use_candidate(bool use_candidate) { use_candidate_ = use_candidate; }

  virtual void Prepare(StunMessage* request) {
    request->SetType(STUN_BINDING_REQUEST);
    std::string username;
    connection_->port()->CreateStunUsername(
        connection_->remote_candidate().username(), &username);
    request->AddAttribute(
        new StunByteStringAttribute(STUN_ATTR_USERNAME, username));

    // Adding ICE-specific attributes to the STUN request message.
    if (connection_->port()->ice_protocol() == ICEPROTO_RFC5245) {
      // Adding ICE_CONTROLLED or ICE_CONTROLLING attribute based on the role.
      if (connection_->port()->role() == ROLE_CONTROLLING) {
        request->AddAttribute(new StunUInt64Attribute(
            STUN_ATTR_ICE_CONTROLLING, connection_->port()->tiebreaker()));
      } else if (connection_->port()->role() == ROLE_CONTROLLED) {
        request->AddAttribute(new StunUInt64Attribute(
            STUN_ATTR_ICE_CONTROLLED, connection_->port()->tiebreaker()));
      } else {
        ASSERT(false);
      }

      // Adding USE-CANDIDATE attribute, if the flag is set to true.
      if (use_candidate_) {
        request->AddAttribute(new StunByteStringAttribute(
            STUN_ATTR_USE_CANDIDATE));
      }

      // Adding PRIORITY Attribute.
      // Changing the type preference to Peer Reflexive and local preference
      // and component id information is unchanged from the original priority.
      // priority = (2^24)*(type preference) +
      //           (2^8)*(local preference) +
      //           (2^0)*(256 - component ID)
      uint32 prflx_priority = ICE_TYPE_PREFERENCE_PRFLX << 24 |
          (connection_->local_candidate().priority() & 0x00FFFFFF);
      request->AddAttribute(
          new StunUInt32Attribute(STUN_ATTR_PRIORITY, prflx_priority));

      // Adding Message Integrity attribute.
      request->AddMessageIntegrity(connection_->remote_candidate().password());
      // Adding Fingerprint.
      request->AddFingerprint();
    }
  }

  virtual void OnResponse(StunMessage* response) {
    connection_->OnConnectionRequestResponse(this, response);
  }

  virtual void OnErrorResponse(StunMessage* response) {
    connection_->OnConnectionRequestErrorResponse(this, response);
  }

  virtual void OnTimeout() {
    connection_->OnConnectionRequestTimeout(this);
  }

  virtual int GetNextDelay() {
    // Each request is sent only once.  After a single delay , the request will
    // time out.
    timeout_ = true;
    return CONNECTION_RESPONSE_TIMEOUT;
  }

 private:
  Connection* connection_;
  bool use_candidate_;
};

//
// Connection
//

Connection::Connection(Port* port, size_t index,
                       const Candidate& remote_candidate)
  : port_(port), local_candidate_index_(index),
    remote_candidate_(remote_candidate), read_state_(STATE_READ_TIMEOUT),
    write_state_(STATE_WRITE_CONNECT), connected_(true), pruned_(false),
    requests_(port->thread()), rtt_(DEFAULT_RTT),
    last_ping_sent_(0), last_ping_received_(0), last_data_received_(0),
    last_ping_response_received_(0), reported_(false), nominated_(false) {
  // Wire up to send stun packets
  requests_.SignalSendPacket.connect(this, &Connection::OnSendStunPacket);
  LOG_J(LS_INFO, this) << "Connection created";
}

Connection::~Connection() {
}

const Candidate& Connection::local_candidate() const {
  ASSERT(local_candidate_index_ < port_->candidates().size());
  return port_->candidates()[local_candidate_index_];
}

void Connection::set_read_state(ReadState value) {
  ReadState old_value = read_state_;
  read_state_ = value;
  if (value != old_value) {
    LOG_J(LS_VERBOSE, this) << "set_read_state";
    SignalStateChange(this);
    CheckTimeout();
  }
}

void Connection::set_write_state(WriteState value) {
  WriteState old_value = write_state_;
  write_state_ = value;
  if (value != old_value) {
    LOG_J(LS_VERBOSE, this) << "set_write_state";
    SignalStateChange(this);
    CheckTimeout();
  }
}

void Connection::set_connected(bool value) {
  bool old_value = connected_;
  connected_ = value;
  if (value != old_value) {
    LOG_J(LS_VERBOSE, this) << "set_connected";
  }
}

void Connection::OnSendStunPacket(const void* data, size_t size,
                                  StunRequest* req) {
  if (port_->SendTo(data, size, remote_candidate_.address(), false) < 0) {
    LOG_J(LS_WARNING, this) << "Failed to send STUN ping " << req->id();
  }
}

void Connection::OnReadPacket(const char* data, size_t size) {
  talk_base::scoped_ptr<IceMessage> msg;
  std::string remote_ufrag;
  const talk_base::SocketAddress& addr(remote_candidate_.address());
  if (!port_->GetStunMessage(data, size, addr, msg.accept(), &remote_ufrag)) {
    // The packet did not parse as a valid STUN message

    // If this connection is readable, then pass along the packet.
    if (read_state_ == STATE_READABLE) {
      // readable means data from this address is acceptable
      // Send it on!

      last_data_received_ = talk_base::Time();
      recv_rate_tracker_.Update(size);
      SignalReadPacket(this, data, size);

      // If timed out sending writability checks, start up again
      if (!pruned_ && (write_state_ == STATE_WRITE_TIMEOUT))
        set_write_state(STATE_WRITE_CONNECT);
    } else {
      // Not readable means the remote address hasn't sent a valid
      // binding request yet.

      LOG_J(LS_WARNING, this)
        << "Received non-STUN packet from an unreadable connection.";
    }
  } else if (!msg.get()) {
    // The packet was STUN, but failed a check and was handled internally.
  } else {
    // The packet is STUN and passed the Port checks.
    // Perform our own checks to ensure this packet is valid.
    // If this is a STUN request, then update the readable bit and respond.
    // If this is a STUN response, then update the writable bit.
    switch (msg->type()) {
      case STUN_BINDING_REQUEST:
        if (remote_ufrag == remote_candidate_.username()) {
          // Check for role conflicts.
          if (port_->ice_protocol() == ICEPROTO_RFC5245 &&
              !port_->MaybeIceRoleConflict(addr, msg.get())) {
            // Received conflicting role from the peer.
            LOG(LS_INFO) << "Received conflicting role from the peer.";
            return;
          }

          // Incoming, validated stun request from remote peer.
          // This call will also set the connection readable.
          port_->SendBindingResponse(msg.get(), addr);

          // If timed out sending writability checks, start up again
          if (!pruned_ && (write_state_ == STATE_WRITE_TIMEOUT))
            set_write_state(STATE_WRITE_CONNECT);

          if ((port_->ice_protocol() == ICEPROTO_RFC5245) &&
              (port_->role() == ROLE_CONTROLLED)) {
            const StunByteStringAttribute* use_candidate_attr =
                msg->GetByteString(STUN_ATTR_USE_CANDIDATE);
            if (use_candidate_attr)
              SignalUseCandidate(this);
          }
        } else {
          // The packet had the right local username, but the remote username
          // was not the right one for the remote address.
          LOG_J(LS_ERROR, this)
            << "Received STUN request with bad remote username "
            << remote_ufrag;
          port_->SendBindingErrorResponse(msg.get(), addr,
                                          STUN_ERROR_UNAUTHORIZED,
                                          STUN_ERROR_REASON_UNAUTHORIZED);

        }
        break;

      // Response from remote peer. Does it match request sent?
      // This doesn't just check, it makes callbacks if transaction
      // id's match.
      case STUN_BINDING_RESPONSE:
      case STUN_BINDING_ERROR_RESPONSE:
        if (port_->ice_protocol() == ICEPROTO_GOOGLE ||
            msg->ValidateMessageIntegrity(
                data, size, remote_candidate().password())) {
          requests_.CheckResponse(msg.get());
          nominated_ = false;
        }
        // Otherwise silently discard the response message.
        break;

      default:
        ASSERT(false);
        break;
    }
  }
}

void Connection::Prune() {
  if (!pruned_) {
    LOG_J(LS_VERBOSE, this) << "Connection pruned";
    pruned_ = true;
    requests_.Clear();
    set_write_state(STATE_WRITE_TIMEOUT);
  }
}

void Connection::Destroy() {
  LOG_J(LS_VERBOSE, this) << "Connection destroyed";
  set_read_state(STATE_READ_TIMEOUT);
  set_write_state(STATE_WRITE_TIMEOUT);
}

void Connection::UpdateState(uint32 now) {
  uint32 rtt = ConservativeRTTEstimate(rtt_);

  std::string pings;
  for (size_t i = 0; i < pings_since_last_response_.size(); ++i) {
    char buf[32];
    talk_base::sprintfn(buf, sizeof(buf), "%u",
        pings_since_last_response_[i]);
    pings.append(buf).append(" ");
  }
  LOG_J(LS_VERBOSE, this) << "UpdateState(): pings_since_last_response_=" <<
      pings << ", rtt=" << rtt << ", now=" << now;

  // Check the readable state.
  //
  // Since we don't know how many pings the other side has attempted, the best
  // test we can do is a simple window.

  if ((read_state_ == STATE_READABLE) &&
      (last_ping_received_ + CONNECTION_READ_TIMEOUT <= now)) {
    LOG_J(LS_INFO, this) << "Unreadable after "
                         << now - last_ping_received_
                         << " ms without a ping,"
                         << " ms since last received response="
                         << now - last_ping_response_received_
                         << " ms since last received data="
                         << now - last_data_received_
                         << " rtt=" << rtt;
    set_read_state(STATE_READ_TIMEOUT);
  }

  // Check the writable state.  (The order of these checks is important.)
  //
  // Before becoming unwritable, we allow for a fixed number of pings to fail
  // (i.e., receive no response).  We also have to give the response time to
  // get back, so we include a conservative estimate of this.
  //
  // Before timing out writability, we give a fixed amount of time.  This is to
  // allow for changes in network conditions.

  if ((write_state_ == STATE_WRITABLE) &&
      TooManyFailures(pings_since_last_response_,
                      CONNECTION_WRITE_CONNECT_FAILURES,
                      rtt,
                      now) &&
      TooLongWithoutResponse(pings_since_last_response_,
                             CONNECTION_WRITE_CONNECT_TIMEOUT,
                             now)) {
    uint32 max_pings = CONNECTION_WRITE_CONNECT_FAILURES;
    LOG_J(LS_INFO, this) << "Unwritable after " << max_pings
                         << " ping failures and "
                         << now - pings_since_last_response_[0]
                         << " ms without a response,"
                         << " ms since last received ping="
                         << now - last_ping_received_
                         << " ms since last received data="
                         << now - last_data_received_
                         << " rtt=" << rtt;
    set_write_state(STATE_WRITE_CONNECT);
  }

  if ((write_state_ == STATE_WRITE_CONNECT) &&
      TooLongWithoutResponse(pings_since_last_response_,
                             CONNECTION_WRITE_TIMEOUT,
                             now)) {
    LOG_J(LS_INFO, this) << "Timed out after "
                         << now - pings_since_last_response_[0]
                         << " ms without a response, rtt=" << rtt;
    set_write_state(STATE_WRITE_TIMEOUT);
  }
}

void Connection::Ping(uint32 now) {
  ASSERT(connected_);
  last_ping_sent_ = now;
  pings_since_last_response_.push_back(now);
  ConnectionRequest *req = new ConnectionRequest(this);
  if (nominated_) {
    req->set_use_candidate(true);
  }
  LOG_J(LS_VERBOSE, this) << "Sending STUN ping " << req->id() << " at " << now;
  requests_.Send(req);
}

void Connection::ReceivedPing() {
  last_ping_received_ = talk_base::Time();
  set_read_state(STATE_READABLE);
}

std::string Connection::ToString() const {
  const char CONNECT_STATE_ABBREV[2] = {
    '-',  // not connected (false)
    'C',  // connected (true)
  };
  const char READ_STATE_ABBREV[2] = {
    'R',  // STATE_READABLE
    '-',  // STATE_READ_TIMEOUT
  };
  const char WRITE_STATE_ABBREV[3] = {
    'W',  // STATE_WRITABLE
    'w',  // STATE_WRITE_CONNECT
    '-',  // STATE_WRITE_TIMEOUT
  };
  const Candidate& local = local_candidate();
  const Candidate& remote = remote_candidate();
  std::stringstream ss;
  ss << "Conn[" << local.id() << ":" << local.component()
     << ":" << local.generation()
     << ":" << local.type() << ":" << local.protocol()
     << ":" << local.address().ToString()
     << "->" << remote.id() << ":" << remote.component()
     << ":" << remote.generation()
     << ":" << remote.type() << ":"
     << remote.protocol() << ":" << remote.address().ToString()
     << "|"
     << CONNECT_STATE_ABBREV[connected()]
     << READ_STATE_ABBREV[read_state()]
     << WRITE_STATE_ABBREV[write_state()]
     << "|";
  if (rtt_ < DEFAULT_RTT) {
    ss << rtt_ << "]";
  } else {
    ss << "-]";
  }
  return ss.str();
}

void Connection::OnConnectionRequestResponse(ConnectionRequest* request,
                                             StunMessage* response) {
  // We've already validated that this is a STUN binding response with
  // the correct local and remote username for this connection.
  // So if we're not already, become writable. We may be bringing a pruned
  // connection back to life, but if we don't really want it, we can always
  // prune it again.
  uint32 rtt = request->Elapsed();
  set_write_state(STATE_WRITABLE);

  std::string pings;
  for (size_t i = 0; i < pings_since_last_response_.size(); ++i) {
    char buf[32];
    talk_base::sprintfn(buf, sizeof(buf), "%u",
        pings_since_last_response_[i]);
    pings.append(buf).append(" ");
  }

  LOG_J(LS_VERBOSE, this) << "Received STUN ping response " << request->id()
                          << ", pings_since_last_response_=" << pings
                          << ", rtt=" << rtt;

  pings_since_last_response_.clear();
  last_ping_response_received_ = talk_base::Time();
  rtt_ = (RTT_RATIO * rtt_ + rtt) / (RTT_RATIO + 1);
}

void Connection::OnConnectionRequestErrorResponse(ConnectionRequest* request,
                                                  StunMessage* response) {
  const StunErrorCodeAttribute* error_attr = response->GetErrorCode();
  int error_code = STUN_ERROR_GLOBAL_FAILURE;
  if (error_attr) {
    if (port_->ice_protocol_ == ICEPROTO_GOOGLE) {
      // When doing GICE, the error code is written out incorrectly, so we need
      // to unmunge it here.
      error_code = error_attr->eclass() * 256 + error_attr->number();
    } else {
      error_code = error_attr->code();
    }
  }

  if (error_code == STUN_ERROR_UNKNOWN_ATTRIBUTE ||
      error_code == STUN_ERROR_SERVER_ERROR ||
      error_code == STUN_ERROR_UNAUTHORIZED) {
    // Recoverable error, retry
  } else if (error_code == STUN_ERROR_STALE_CREDENTIALS) {
    // Race failure, retry
  } else if (error_code == STUN_ERROR_ROLE_CONFLICT) {
    HandleRoleConflictFromPeer();
  } else {
    // This is not a valid connection.
    LOG_J(LS_ERROR, this) << "Received STUN error response, code="
                          << error_code << "; killing connection";
    set_write_state(STATE_WRITE_TIMEOUT);
  }
}

void Connection::OnConnectionRequestTimeout(ConnectionRequest* request) {
  // Log at LS_INFO if we miss a ping on a writable connection.
  talk_base::LoggingSeverity sev = (write_state_ == STATE_WRITABLE) ?
      talk_base::LS_INFO : talk_base::LS_VERBOSE;
  LOG_JV(sev, this) << "Timing-out STUN ping " << request->id()
                    << " after " << request->Elapsed() << " ms";
}

void Connection::CheckTimeout() {
  // If both read and write have timed out, then this connection can contribute
  // no more to p2p socket unless at some later date readability were to come
  // back.  However, we gave readability a long time to timeout, so at this
  // point, it seems fair to get rid of this connection.
  if ((read_state_ == STATE_READ_TIMEOUT) &&
      (write_state_ == STATE_WRITE_TIMEOUT)) {
    port_->thread()->Post(this, MSG_DELETE);
  }
}

void Connection::HandleRoleConflictFromPeer() {
  // Maybe we should reverse the nominated flag if we are in controlling mode.
  if (port_->role() == ROLE_CONTROLLING)
    nominated_ = false;  // Role change will be done from Transport.
  port_->SignalRoleConflict();
}

void Connection::OnMessage(talk_base::Message *pmsg) {
  ASSERT(pmsg->message_id == MSG_DELETE);

  LOG_J(LS_INFO, this) << "Connection deleted";
  SignalDestroyed(this);
  delete this;
}

size_t Connection::recv_bytes_second() {
  return recv_rate_tracker_.units_second();
}

size_t Connection::recv_total_bytes() {
  return recv_rate_tracker_.total_units();
}

size_t Connection::sent_bytes_second() {
  return send_rate_tracker_.units_second();
}

size_t Connection::sent_total_bytes() {
  return send_rate_tracker_.total_units();
}

ProxyConnection::ProxyConnection(Port* port, size_t index,
                                 const Candidate& candidate)
  : Connection(port, index, candidate), error_(0) {
}

int ProxyConnection::Send(const void* data, size_t size) {
  if (write_state() != STATE_WRITABLE) {
    error_ = EWOULDBLOCK;
    return SOCKET_ERROR;
  }
  int sent = port_->SendTo(data, size, remote_candidate_.address(), true);
  if (sent <= 0) {
    ASSERT(sent < 0);
    error_ = port_->GetError();
  } else {
    send_rate_tracker_.Update(sent);
  }
  return sent;
}

}  // namespace cricket
