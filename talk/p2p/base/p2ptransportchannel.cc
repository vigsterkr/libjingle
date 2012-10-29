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

#include "talk/p2p/base/p2ptransportchannel.h"

#include <set>
#include "talk/base/common.h"
#include "talk/base/crc32.h"
#include "talk/base/logging.h"
#include "talk/base/stringencode.h"
#include "talk/p2p/base/common.h"
#include "talk/p2p/base/relayport.h"  // For RELAY_PORT_TYPE.
#include "talk/p2p/base/stunport.h"  // For STUN_PORT_TYPE.

namespace {

// messages for queuing up work for ourselves
enum {
  MSG_SORT = 1,
  MSG_PING,
};

// When the socket is unwritable, we will use 10 Kbps (ignoring IP+UDP headers)
// for pinging.  When the socket is writable, we will use only 1 Kbps because
// we don't want to degrade the quality on a modem.  These numbers should work
// well on a 28.8K modem, which is the slowest connection on which the voice
// quality is reasonable at all.
static const uint32 PING_PACKET_SIZE = 60 * 8;
static const uint32 WRITABLE_DELAY = 1000 * PING_PACKET_SIZE / 1000;  // 480ms
static const uint32 UNWRITABLE_DELAY = 1000 * PING_PACKET_SIZE / 10000;  // 50ms

// If there is a current writable connection, then we will also try hard to
// make sure it is pinged at this rate.
static const uint32 MAX_CURRENT_WRITABLE_DELAY = 900;  // 2*WRITABLE_DELAY - bit

// The minimum improvement in RTT that justifies a switch.
static const double kMinImprovement = 10;

cricket::PortInterface::CandidateOrigin GetOrigin(cricket::PortInterface* port,
                                         cricket::PortInterface* origin_port) {
  if (!origin_port)
    return cricket::PortInterface::ORIGIN_MESSAGE;
  else if (port == origin_port)
    return cricket::PortInterface::ORIGIN_THIS_PORT;
  else
    return cricket::PortInterface::ORIGIN_OTHER_PORT;
}

// Compares two connections based only on static information about them.
int CompareConnectionCandidates(cricket::Connection* a,
                                cricket::Connection* b) {
  // Compare connection priority. Lower values get sorted last.
  if (a->priority() > b->priority())
    return 1;
  if (a->priority() < b->priority())
    return -1;

  // If we're still tied at this point, prefer a younger generation.
  return (a->remote_candidate().generation() + a->port()->generation()) -
         (b->remote_candidate().generation() + b->port()->generation());
}

// Compare two connections based on their writability and static preferences.
int CompareConnections(cricket::Connection *a, cricket::Connection *b) {
  // Sort based on write-state.  Better states have lower values.
  if (a->write_state() < b->write_state())
    return 1;
  if (a->write_state() > b->write_state())
    return -1;

  // Compare the candidate information.
  return CompareConnectionCandidates(a, b);
}

// Wraps the comparison connection into a less than operator that puts higher
// priority writable connections first.
class ConnectionCompare {
 public:
  bool operator()(const cricket::Connection *ca,
                  const cricket::Connection *cb) {
    cricket::Connection* a = const_cast<cricket::Connection*>(ca);
    cricket::Connection* b = const_cast<cricket::Connection*>(cb);

    ASSERT(a->port()->IceProtocol() == b->port()->IceProtocol());

    // Compare first on writability and static preferences.
    int cmp = CompareConnections(a, b);
    if (cmp > 0)
      return true;
    if (cmp < 0)
      return false;

    // Otherwise, sort based on latency estimate.
    return a->rtt() < b->rtt();

    // Should we bother checking for the last connection that last received
    // data? It would help rendezvous on the connection that is also receiving
    // packets.
    //
    // TODO: Yes we should definitely do this.  The TCP protocol gains
    // efficiency by being used bidirectionally, as opposed to two separate
    // unidirectional streams.  This test should probably occur before
    // comparison of local prefs (assuming combined prefs are the same).  We
    // need to be careful though, not to bounce back and forth with both sides
    // trying to rendevous with the other.
  }
};

// Determines whether we should switch between two connections, based first on
// static preferences and then (if those are equal) on latency estimates.
bool ShouldSwitch(cricket::Connection* a_conn, cricket::Connection* b_conn) {
  if (a_conn == b_conn)
    return false;

  if (!a_conn || !b_conn)  // don't think the latter should happen
    return true;

  int prefs_cmp = CompareConnections(a_conn, b_conn);
  if (prefs_cmp < 0)
    return true;
  if (prefs_cmp > 0)
    return false;

  return b_conn->rtt() <= a_conn->rtt() + kMinImprovement;
}

}  // unnamed namespace

namespace cricket {

P2PTransportChannel::P2PTransportChannel(const std::string& content_name,
                                         int component,
                                         P2PTransport* transport,
                                         PortAllocator *allocator) :
    TransportChannelImpl(content_name, component),
    transport_(transport),
    allocator_(allocator),
    worker_thread_(talk_base::Thread::Current()),
    incoming_only_(false),
    waiting_for_signaling_(false),
    error_(0),
    best_connection_(NULL),
    sort_dirty_(false),
    was_writable_(false),
    was_timed_out_(true),
    protocol_type_(ICEPROTO_GOOGLE),
    role_(ROLE_UNKNOWN),
    tiebreaker_(0) {
}

P2PTransportChannel::~P2PTransportChannel() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  for (uint32 i = 0; i < allocator_sessions_.size(); ++i)
    delete allocator_sessions_[i];
}

// Add the allocator session to our list so that we know which sessions
// are still active.
void P2PTransportChannel::AddAllocatorSession(PortAllocatorSession* session) {
  session->set_generation(static_cast<uint32>(allocator_sessions_.size()));
  allocator_sessions_.push_back(session);

  // We now only want to apply new candidates that we receive to the ports
  // created by this new session because these are replacing those of the
  // previous sessions.
  ports_.clear();

  session->SignalPortReady.connect(this, &P2PTransportChannel::OnPortReady);
  session->SignalCandidatesReady.connect(
      this, &P2PTransportChannel::OnCandidatesReady);
  session->SignalCandidatesAllocationDone.connect(
      this, &P2PTransportChannel::OnCandidatesAllocationDone);
  session->GetInitialPorts();
  session->StartGetAllPorts();
}

void P2PTransportChannel::SetRole(TransportRole role) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  role_ = role;
  for (std::vector<PortInterface *>::iterator it = ports_.begin();
       it != ports_.end(); ++it) {
    (*it)->SetRole(role_);
  }
  // TODO - Recompute the priorites for the connections.
}

void P2PTransportChannel::SetTiebreaker(uint64 tiebreaker) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!ports_.empty()) {
    LOG(LS_ERROR)
        << "Attempt to change tiebreaker after Port has been allocated.";
    return;
  }

  tiebreaker_ = tiebreaker;
}

void P2PTransportChannel::SetIceProtocolType(IceProtocolType type) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  protocol_type_ = type;
  for (std::vector<PortInterface *>::iterator it = ports_.begin();
       it != ports_.end(); ++it) {
    (*it)->SetIceProtocolType(protocol_type_);
  }
}

void P2PTransportChannel::SetIceUfrag(const std::string& ice_ufrag) {
  ice_ufrag_ = ice_ufrag;
}

void P2PTransportChannel::SetIcePwd(const std::string& ice_pwd) {
  ice_pwd_ = ice_pwd;
}

// Go into the state of processing candidates, and running in general
void P2PTransportChannel::Connect() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (ice_ufrag_.empty() || ice_pwd_.empty()) {
    ASSERT(false);
    LOG(LS_ERROR) << "P2PTransportChannel::Connect: The ice_ufrag_ and the "
                  << "ice_pwd_ are not set.";
    return;
  }

  // Kick off an allocator session
  Allocate();

  // Start pinging as the ports come in.
  thread()->Post(this, MSG_PING);
}

// Reset the socket, clear up any previous allocations and start over
void P2PTransportChannel::Reset() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Get rid of all the old allocators.  This should clean up everything.
  for (uint32 i = 0; i < allocator_sessions_.size(); ++i)
    delete allocator_sessions_[i];

  allocator_sessions_.clear();
  ports_.clear();
  connections_.clear();
  best_connection_ = NULL;

  // Forget about all of the candidates we got before.
  remote_candidates_.clear();

  // Revert to the initial state.
  set_readable(false);
  set_writable(false);

  // Reinitialize the rest of our state.
  waiting_for_signaling_ = false;
  sort_dirty_ = false;
  was_writable_ = false;
  was_timed_out_ = true;

  // If we allocated before, start a new one now.
  if (transport_->connect_requested())
    Allocate();

  // Start pinging as the ports come in.
  thread()->Clear(this);
  thread()->Post(this, MSG_PING);
}

// A new port is available, attempt to make connections for it
void P2PTransportChannel::OnPortReady(PortAllocatorSession *session,
                                      PortInterface* port) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Set in-effect options on the new port
  for (OptionMap::const_iterator it = options_.begin();
       it != options_.end();
       ++it) {
    int val = port->SetOption(it->first, it->second);
    if (val < 0) {
      LOG_J(LS_WARNING, port) << "SetOption(" << it->first
                              << ", " << it->second
                              << ") failed: " << port->GetError();
    }
  }

  // Remember the ports and candidates, and signal that candidates are ready.
  // The session will handle this, and send an initiate/accept/modify message
  // if one is pending.

  port->SetIceProtocolType(protocol_type_);
  port->SetRole(role_);
  port->SetTiebreaker(tiebreaker_);
  ports_.push_back(port);
  port->SignalUnknownAddress.connect(
      this, &P2PTransportChannel::OnUnknownAddress);
  port->SignalDestroyed.connect(this, &P2PTransportChannel::OnPortDestroyed);
  port->SignalRoleConflict.connect(
      this, &P2PTransportChannel::OnRoleConflict);

  // Attempt to create a connection from this new port to all of the remote
  // candidates that we were given so far.

  std::vector<RemoteCandidate>::iterator iter;
  for (iter = remote_candidates_.begin(); iter != remote_candidates_.end();
       ++iter) {
    CreateConnection(port, *iter, iter->origin_port(), false);
  }

  SortConnections();
}

// A new candidate is available, let listeners know
void P2PTransportChannel::OnCandidatesReady(
    PortAllocatorSession *session, const std::vector<Candidate>& candidates) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  for (size_t i = 0; i < candidates.size(); ++i) {
    SignalCandidateReady(this, candidates[i]);
  }
}

void P2PTransportChannel::OnCandidatesAllocationDone(
    PortAllocatorSession* session) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  SignalCandidatesAllocationDone(this);
}

// Handle stun packets
void P2PTransportChannel::OnUnknownAddress(
    PortInterface* port,
    const talk_base::SocketAddress& address, ProtocolType proto,
    IceMessage* stun_msg, const std::string &remote_username,
    bool port_muxed) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Port has received a valid stun packet from an address that no Connection
  // is currently available for. See if we already have a candidate with the
  // address. If it isn't we need to create new candidate for it.

  // Determine if the remote candidates use shared ufrag.
  bool ufrag_per_port = false;
  std::vector<RemoteCandidate>::iterator it;
  if (remote_candidates_.size() > 0) {
    it = remote_candidates_.begin();
    std::string username = it->username();
    for (; it != remote_candidates_.end(); ++it) {
      if (it->username() != username) {
        ufrag_per_port = true;
        break;
      }
    }
  }

  const Candidate* candidate = NULL;
  bool known_username = false;
  std::string remote_password;
  for (it = remote_candidates_.begin(); it != remote_candidates_.end(); ++it) {
    if (it->username() == remote_username) {
      remote_password = it->password();
      known_username = true;
      if (ufrag_per_port ||
          (it->address() == address &&
           it->protocol() == ProtoToString(proto))) {
        candidate = &(*it);
        break;
      }
      // We don't want to break here because we may find a match of the address
      // later.
    }
  }

  if (!known_username) {
    if (port_muxed) {
      // When Ports are muxed, SignalUnknownAddress is delivered to all
      // P2PTransportChannel belong to a session. Return from here will
      // save us from sending stun binding error message from incorrect channel.
      return;
    }
    // Don't know about this username, the request is bogus
    // This sometimes happens if a binding response comes in before the ACCEPT
    // message.  It is totally valid; the retry state machine will try again.
    port->SendBindingErrorResponse(stun_msg, address,
        STUN_ERROR_STALE_CREDENTIALS, STUN_ERROR_REASON_STALE_CREDENTIALS);
    return;
  }

  Candidate new_remote_candidate;
  if (candidate != NULL) {
    new_remote_candidate = *candidate;
    if (ufrag_per_port) {
      new_remote_candidate.set_address(address);
    }
  } else {
    // Create a new candidate with this address.

    // Unless the binding request came from a relay port, we use the port
    // type as the candidate type. If the binding request comes from a relay
    // port we always set type to STUN_PORT_TYPE.
    // TODO: Fix this by adding a new type as peer-reflexive
    // candidate. So that all the new candidates created here will be the
    // peer-reflexive candidate.
    std::string type = port->Type();
    if (type == RELAY_PORT_TYPE || port->SharedSocket()) {
      type = STUN_PORT_TYPE;
    }

    // TODO: Change the preference to the preference of
    // the peer-reflexive candidate when it's ready.
    // For now just default to a STUN preference.
    // TODO(ronghuawu): Use Port::ComputeFoundation to calculate foundation.
    std::string id = talk_base::CreateRandomString(8);
    new_remote_candidate = Candidate(
        id, component(), ProtoToString(proto), address,
        0, remote_username, remote_password, type,
        port->Network()->name(), 0U,
        talk_base::ToString<uint32>(talk_base::ComputeCrc32(id)));
    new_remote_candidate.set_priority(
        new_remote_candidate.GetPriority(ICE_TYPE_PREFERENCE_SRFLX));
  }

  // Check for connectivity to this address. Create connections
  // to this address across all local ports. First, add this as a new remote
  // address
  if (CreateConnections(new_remote_candidate, port, true)) {
    // Send the pinger a successful stun response.
    port->SendBindingResponse(stun_msg, address);

    // Update the list of connections since we just added another.  We do this
    // after sending the response since it could (in principle) delete the
    // connection in question.
    SortConnections();
  } else {
    // Hopefully this won't occur, because changing a destination address
    // shouldn't cause a new connection to fail
    ASSERT(false);
    port->SendBindingErrorResponse(stun_msg, address, STUN_ERROR_SERVER_ERROR,
        STUN_ERROR_REASON_SERVER_ERROR);
  }
}

void P2PTransportChannel::OnRoleConflict() {
  SignalRoleConflict(this);  // STUN ping will be sent when SetRole is called
                             // from Transport.
}

// When the signalling channel is ready, we can really kick off the allocator
void P2PTransportChannel::OnSignalingReady() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (waiting_for_signaling_) {
    waiting_for_signaling_ = false;
    AddAllocatorSession(allocator_->CreateSession(
        SessionId(), content_name(), component(), ice_ufrag_, ice_pwd_));
  }
}

void P2PTransportChannel::OnUseCandidate(Connection* conn) {
  ASSERT(role_ == ROLE_CONTROLLED);
  if (conn->state() == Connection::STATE_SUCCEEDED) {
    // Set the nominated flag.
    conn->set_nominated(true);
    SwitchBestConnectionTo(conn);
  }
}

void P2PTransportChannel::OnCandidate(const Candidate& candidate) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Create connections to this remote candidate.
  CreateConnections(candidate, NULL, false);

  // Resort the connections list, which may have new elements.
  SortConnections();
}

// Creates connections from all of the ports that we care about to the given
// remote candidate.  The return value is true if we created a connection from
// the origin port.
bool P2PTransportChannel::CreateConnections(const Candidate &remote_candidate,
                                            PortInterface* origin_port,
                                            bool readable) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Add a new connection for this candidate to every port that allows such a
  // connection (i.e., if they have compatible protocols) and that does not
  // already have a connection to an equivalent candidate.  We must be careful
  // to make sure that the origin port is included, even if it was pruned,
  // since that may be the only port that can create this connection.

  bool created = false;

  std::vector<PortInterface *>::reverse_iterator it;
  for (it = ports_.rbegin(); it != ports_.rend(); ++it) {
    if (CreateConnection(*it, remote_candidate, origin_port, readable)) {
      if (*it == origin_port)
        created = true;
    }
  }

  if ((origin_port != NULL) &&
      std::find(ports_.begin(), ports_.end(), origin_port) == ports_.end()) {
    if (CreateConnection(origin_port, remote_candidate, origin_port, readable))
      created = true;
  }

  // Remember this remote candidate so that we can add it to future ports.
  RememberRemoteCandidate(remote_candidate, origin_port);

  return created;
}

// Setup a connection object for the local and remote candidate combination.
// And then listen to connection object for changes.
bool P2PTransportChannel::CreateConnection(PortInterface* port,
                                           const Candidate& remote_candidate,
                                           PortInterface* origin_port,
                                           bool readable) {
  // Look for an existing connection with this remote address.  If one is not
  // found, then we can create a new connection for this address.
  Connection* connection = port->GetConnection(remote_candidate.address());
  if (connection != NULL) {
    // It is not legal to try to change any of the parameters of an existing
    // connection; however, the other side can send a duplicate candidate.
    if (!remote_candidate.IsEquivalent(connection->remote_candidate())) {
      LOG(INFO) << "Attempt to change a remote candidate";
      return false;
    }
  } else {
    PortInterface::CandidateOrigin origin = GetOrigin(port, origin_port);

    // Don't create connection if this is a candidate we received in a
    // message and we are not allowed to make outgoing connections.
    if (origin == cricket::PortInterface::ORIGIN_MESSAGE && incoming_only_)
      return false;

    connection = port->CreateConnection(remote_candidate, origin);
    if (!connection)
      return false;

    connections_.push_back(connection);
    connection->SignalReadPacket.connect(
        this, &P2PTransportChannel::OnReadPacket);
    connection->SignalStateChange.connect(
        this, &P2PTransportChannel::OnConnectionStateChange);
    connection->SignalDestroyed.connect(
        this, &P2PTransportChannel::OnConnectionDestroyed);
    connection->SignalUseCandidate.connect(
        this, &P2PTransportChannel::OnUseCandidate);

    LOG_J(LS_INFO, this) << "Created connection with origin=" << origin << ", ("
                         << connections_.size() << " total)";
  }

  // If we are readable, it is because we are creating this in response to a
  // ping from the other side.  This will cause the state to become readable.
  if (readable)
    connection->ReceivedPing();

  return true;
}

bool P2PTransportChannel::FindConnection(
    cricket::Connection* connection) const {
  std::vector<Connection*>::const_iterator citer =
      std::find(connections_.begin(), connections_.end(), connection);
  return citer != connections_.end();
}

// Maintain our remote candidate list, adding this new remote one.
void P2PTransportChannel::RememberRemoteCandidate(
    const Candidate& remote_candidate, PortInterface* origin_port) {
  // Remove any candidates whose generation is older than this one.  The
  // presence of a new generation indicates that the old ones are not useful.
  uint32 i = 0;
  while (i < remote_candidates_.size()) {
    if (remote_candidates_[i].generation() < remote_candidate.generation()) {
      LOG(INFO) << "Pruning candidate from old generation: "
                << remote_candidates_[i].address().ToString();
      remote_candidates_.erase(remote_candidates_.begin() + i);
    } else {
      i += 1;
    }
  }

  // Make sure this candidate is not a duplicate.
  for (uint32 i = 0; i < remote_candidates_.size(); ++i) {
    if (remote_candidates_[i].IsEquivalent(remote_candidate)) {
      LOG(INFO) << "Duplicate candidate: "
                << remote_candidate.address().ToString();
      return;
    }
  }

  // Try this candidate for all future ports.
  remote_candidates_.push_back(RemoteCandidate(remote_candidate, origin_port));
}


// Set options on ourselves is simply setting options on all of our available
// port objects.
int P2PTransportChannel::SetOption(talk_base::Socket::Option opt, int value) {
  OptionMap::iterator it = options_.find(opt);
  if (it == options_.end()) {
    options_.insert(std::make_pair(opt, value));
  } else if (it->second == value) {
    return 0;
  } else {
    it->second = value;
  }

  for (uint32 i = 0; i < ports_.size(); ++i) {
    int val = ports_[i]->SetOption(opt, value);
    if (val < 0) {
      // Because this also occurs deferred, probably no point in reporting an
      // error
      LOG(WARNING) << "SetOption(" << opt << ", " << value << ") failed: "
                   << ports_[i]->GetError();
    }
  }
  return 0;
}

// Send data to the other side, using our best connection.
int P2PTransportChannel::SendPacket(const char *data, size_t len, int flags) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (flags != 0) {
    error_ = EINVAL;
    return -1;
  }
  if (best_connection_ == NULL) {
    error_ = EWOULDBLOCK;
    return -1;
  }
  int sent = best_connection_->Send(data, len);
  if (sent <= 0) {
    ASSERT(sent < 0);
    error_ = best_connection_->GetError();
  }
  return sent;
}

bool P2PTransportChannel::GetStats(ConnectionInfos *infos) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  // Gather connection infos.
  infos->clear();

  std::vector<Connection *>::const_iterator it;
  for (it = connections_.begin(); it != connections_.end(); ++it) {
    Connection *connection = *it;
    ConnectionInfo info;
    info.best_connection = (best_connection_ == connection);
    info.readable =
        (connection->read_state() == Connection::STATE_READABLE);
    info.writable =
        (connection->write_state() == Connection::STATE_WRITABLE);
    info.timeout =
        (connection->write_state() == Connection::STATE_WRITE_TIMEOUT);
    info.new_connection = !connection->reported();
    connection->set_reported(true);
    info.rtt = connection->rtt();
    info.sent_total_bytes = connection->sent_total_bytes();
    info.sent_bytes_second = connection->sent_bytes_second();
    info.recv_total_bytes = connection->recv_total_bytes();
    info.recv_bytes_second = connection->recv_bytes_second();
    info.local_candidate = connection->local_candidate();
    info.remote_candidate = connection->remote_candidate();
    info.key = connection;
    infos->push_back(info);
  }

  return true;
}

// Begin allocate (or immediately re-allocate, if MSG_ALLOCATE pending)
void P2PTransportChannel::Allocate() {
  // Time for a new allocator, lets make sure we have a signalling channel
  // to communicate candidates through first.
  waiting_for_signaling_ = true;
  SignalRequestSignaling(this);
}

// Monitor connection states.
void P2PTransportChannel::UpdateConnectionStates() {
  uint32 now = talk_base::Time();

  // We need to copy the list of connections since some may delete themselves
  // when we call UpdateState.
  for (uint32 i = 0; i < connections_.size(); ++i)
    connections_[i]->UpdateState(now);
}

// Prepare for best candidate sorting.
void P2PTransportChannel::RequestSort() {
  if (!sort_dirty_) {
    worker_thread_->Post(this, MSG_SORT);
    sort_dirty_ = true;
  }
}

// Sort the available connections to find the best one.  We also monitor
// the number of available connections and the current state so that we
// can possibly kick off more allocators (for more connections).
void P2PTransportChannel::SortConnections() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Make sure the connection states are up-to-date since this affects how they
  // will be sorted.
  UpdateConnectionStates();

  // Any changes after this point will require a re-sort.
  sort_dirty_ = false;

  // Get a list of the networks that we are using.
  std::set<talk_base::Network*> networks;
  for (uint32 i = 0; i < connections_.size(); ++i)
    networks.insert(connections_[i]->port()->Network());

  // Find the best alternative connection by sorting.  It is important to note
  // that amongst equal preference, writable connections, this will choose the
  // one whose estimated latency is lowest.  So it is the only one that we
  // need to consider switching to.

  ConnectionCompare cmp;
  std::stable_sort(connections_.begin(), connections_.end(), cmp);
  LOG(LS_VERBOSE) << "Sorting available connections:";
  for (uint32 i = 0; i < connections_.size(); ++i) {
    LOG(LS_VERBOSE) << connections_[i]->ToString();
  }

  Connection* top_connection = NULL;
  if (connections_.size() > 0)
    top_connection = connections_[0];

  // If necessary, switch to the new choice.
  if (ShouldSwitch(best_connection_, top_connection))
    SwitchBestConnectionTo(top_connection);

  // We can prune any connection for which there is a writable connection on
  // the same network with better or equal prefences.  We leave those with
  // better preference just in case they become writable later (at which point,
  // we would prune out the current best connection).  We leave connections on
  // other networks because they may not be using the same resources and they
  // may represent very distinct paths over which we can switch.
  std::set<talk_base::Network*>::iterator network;
  for (network = networks.begin(); network != networks.end(); ++network) {
    Connection* primier = GetBestConnectionOnNetwork(*network);
    if (!primier || (primier->write_state() != Connection::STATE_WRITABLE))
      continue;

    for (uint32 i = 0; i < connections_.size(); ++i) {
      if ((connections_[i] != primier) &&
          (connections_[i]->port()->Network() == *network) &&
          (CompareConnectionCandidates(primier, connections_[i]) >= 0)) {
        connections_[i]->Prune();
      }
    }
  }

  // Count the number of connections in the various states.
  int writable = 0;
  int not_writable = 0;

  for (uint32 i = 0; i < connections_.size(); ++i) {
    switch (connections_[i]->write_state()) {
    case Connection::STATE_WRITABLE:
      ++writable;
      break;
    case Connection::STATE_WRITE_UNRELIABLE:
    case Connection::STATE_WRITE_INIT:
      ++not_writable;
      break;
    case Connection::STATE_WRITE_TIMEOUT:
      // Don't need to count these.
      break;
    default:
      ASSERT(false);
    }
  }

  if (writable > 0) {
    HandleWritable();
  } else if (not_writable > 0) {
    HandleNotWritable();
  } else {
    HandleAllTimedOut();
  }

  // Update the state of this channel.  This method is called whenever the
  // state of any connection changes, so this is a good place to do this.
  UpdateChannelState();
}

// Track the best connection, and let listeners know
void P2PTransportChannel::SwitchBestConnectionTo(Connection* conn) {
  // Note: if conn is NULL, the previous best_connection_ has been destroyed,
  // so don't use it.
  // use it.
  Connection* old_best_connection = best_connection_;
  best_connection_ = conn;
  if (best_connection_) {
    if (old_best_connection) {
      LOG_J(LS_INFO, this) << "Previous best connection: "
                           << old_best_connection->ToString();
    }
    LOG_J(LS_INFO, this) << "New best connection: "
                         << best_connection_->ToString();
    SignalRouteChange(this, best_connection_->remote_candidate());
    NominateBestConnection();
  } else {
    LOG_J(LS_INFO, this) << "No best connection";
  }
}

void P2PTransportChannel::UpdateChannelState() {
  // The Handle* functions already set the writable state.  We'll just double-
  // check it here.
  bool writable = ((best_connection_ != NULL)  &&
      (best_connection_->write_state() ==
      Connection::STATE_WRITABLE));
  ASSERT(writable == this->writable());
  if (writable != this->writable())
    LOG(LS_ERROR) << "UpdateChannelState: writable state mismatch";

  bool readable = false;
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (connections_[i]->read_state() == Connection::STATE_READABLE)
      readable = true;
  }
  set_readable(readable);
}

// We checked the status of our connections and we had at least one that
// was writable, go into the writable state.
void P2PTransportChannel::HandleWritable() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!writable()) {
    for (uint32 i = 0; i < allocator_sessions_.size(); ++i) {
      if (allocator_sessions_[i]->IsGettingAllPorts()) {
        allocator_sessions_[i]->StopGetAllPorts();
      }
    }
  }

  // We're writable, obviously we aren't timed out
  was_writable_ = true;
  was_timed_out_ = false;
  set_writable(true);
}

// We checked the status of our connections and we didn't have any that
// were fully writable, go into the connecting state (kick off a new allocator
// session).
void P2PTransportChannel::HandleNotWritable() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (was_writable_) {
    // If we were writable, let's kick off an allocator session immediately
    was_writable_ = false;
    Allocate();
  }

  // We were connecting, obviously not ALL timed out.
  was_timed_out_ = false;
  set_writable(false);
}

// We checked the status of our connections and not only weren't they writable
// but they were also timed out, we really need a new allocator.
void P2PTransportChannel::HandleAllTimedOut() {
  if (!was_timed_out_) {
    // We weren't timed out before, so kick off an allocator now (we'll still
    // be in the fully timed out state until the allocator actually gives back
    // new ports)
    Allocate();
  }

  // NOTE: we start was_timed_out_ in the true state so that we don't get
  // another allocator created WHILE we are in the process of building up
  // our first allocator.
  was_timed_out_ = true;
  was_writable_ = false;
  set_writable(false);
}

// If we have a best connection, return it, otherwise return top one in the
// list (later we will mark it best).
Connection* P2PTransportChannel::GetBestConnectionOnNetwork(
    talk_base::Network* network) {
  // If the best connection is on this network, then it wins.
  if (best_connection_ && (best_connection_->port()->Network() == network))
    return best_connection_;

  // Otherwise, we return the top-most in sorted order.
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (connections_[i]->port()->Network() == network)
      return connections_[i];
  }

  return NULL;
}

void P2PTransportChannel::NominateBestConnection() {
  // If we have best possible connection, which may not be in writable state
  // yet, that means we can cease connection checks and time to send stun ping
  // with USE-CANDIDATE.
  // As per RFC 5245 we should't do any further connection checks,
  // but libjingle may send new connection requests if candidates are still
  // trickling down from the remote. Final candidate pair should be decided on
  // the priority, but until we have priorities for candidates we will stick
  // with best_connection.
  if ((best_connection_ != NULL) &&
      (best_connection_->port()->IceProtocol() == ICEPROTO_RFC5245) &&
      (role_ == ROLE_CONTROLLING)) {
    best_connection_->set_nominated(true);
  }
}

// Handle any queued up requests
void P2PTransportChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_SORT:
      OnSort();
      break;
    case MSG_PING:
      OnPing();
      break;
    default:
      ASSERT(false);
      break;
  }
}

// Handle queued up sort request
void P2PTransportChannel::OnSort() {
  // Resort the connections based on the new statistics.
  SortConnections();
}

// Handle queued up ping request
void P2PTransportChannel::OnPing() {
  // Make sure the states of the connections are up-to-date (since this affects
  // which ones are pingable).
  UpdateConnectionStates();

  // Find the oldest pingable connection and have it do a ping.
  Connection* conn = FindNextPingableConnection();
  if (conn)
    conn->Ping(talk_base::Time());

  // Post ourselves a message to perform the next ping.
  uint32 delay = writable() ? WRITABLE_DELAY : UNWRITABLE_DELAY;
  thread()->PostDelayed(delay, this, MSG_PING);
}

// Is the connection in a state for us to even consider pinging the other side?
bool P2PTransportChannel::IsPingable(Connection* conn) {
  // An unconnected connection cannot be written to at all, so pinging is out
  // of the question.
  if (!conn->connected())
    return false;

  if (writable()) {
    // If we are writable, then we only want to ping connections that could be
    // better than this one, i.e., the ones that were not pruned.
    return (conn->write_state() != Connection::STATE_WRITE_TIMEOUT);
  } else {
    // If we are not writable, then we need to try everything that might work.
    // This includes both connections that do not have write timeout as well as
    // ones that do not have read timeout.  A connection could be readable but
    // be in write-timeout if we pruned it before.  Since the other side is
    // still pinging it, it very well might still work.
    return (conn->write_state() != Connection::STATE_WRITE_TIMEOUT) ||
           (conn->read_state() != Connection::STATE_READ_TIMEOUT);
  }
}

// Returns the next pingable connection to ping.  This will be the oldest
// pingable connection unless we have a writable connection that is past the
// maximum acceptable ping delay.
Connection* P2PTransportChannel::FindNextPingableConnection() {
  uint32 now = talk_base::Time();
  if (best_connection_ &&
      (best_connection_->write_state() == Connection::STATE_WRITABLE) &&
      (best_connection_->last_ping_sent()
       + MAX_CURRENT_WRITABLE_DELAY <= now)) {
    return best_connection_;
  }

  Connection* oldest_conn = NULL;
  uint32 oldest_time = 0xFFFFFFFF;
  for (uint32 i = 0; i < connections_.size(); ++i) {
    if (IsPingable(connections_[i])) {
      if (connections_[i]->last_ping_sent() < oldest_time) {
        oldest_time = connections_[i]->last_ping_sent();
        oldest_conn = connections_[i];
      }
    }
  }
  return oldest_conn;
}

// return the number of "pingable" connections
int P2PTransportChannel::NumPingableConnections() {
  int count = 0;
  for (size_t i = 0; i < connections_.size(); ++i) {
    if (IsPingable(connections_[i]))
      ++count;
  }
  return count;
}

// When a connection's state changes, we need to figure out who to use as
// the best connection again.  It could have become usable, or become unusable.
void P2PTransportChannel::OnConnectionStateChange(Connection *connection) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // We have to unroll the stack before doing this because we may be changing
  // the state of connections while sorting.
  RequestSort();
}

// When a connection is removed, edit it out, and then update our best
// connection.
void P2PTransportChannel::OnConnectionDestroyed(Connection *connection) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Note: the previous best_connection_ may be destroyed by now, so don't
  // use it.

  // Remove this connection from the list.
  std::vector<Connection*>::iterator iter =
      std::find(connections_.begin(), connections_.end(), connection);
  ASSERT(iter != connections_.end());
  connections_.erase(iter);

  LOG_J(LS_INFO, this) << "Removed connection ("
    << static_cast<int>(connections_.size()) << " remaining)";

  // If this is currently the best connection, then we need to pick a new one.
  // The call to SortConnections will pick a new one.  It looks at the current
  // best connection in order to avoid switching between fairly similar ones.
  // Since this connection is no longer an option, we can just set best to NULL
  // and re-choose a best assuming that there was no best connection.
  if (best_connection_ == connection) {
    SwitchBestConnectionTo(NULL);
    RequestSort();
  }
}

// When a port is destroyed remove it from our list of ports to use for
// connection attempts.
void P2PTransportChannel::OnPortDestroyed(PortInterface* port) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Remove this port from the list (if we didn't drop it already).
  std::vector<PortInterface*>::iterator iter =
      std::find(ports_.begin(), ports_.end(), port);
  if (iter != ports_.end())
    ports_.erase(iter);

  LOG(INFO) << "Removed port from p2p socket: "
            << static_cast<int>(ports_.size()) << " remaining";
}

// We data is available, let listeners know
void P2PTransportChannel::OnReadPacket(Connection *connection, const char *data,
                                       size_t len) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // Do not deliver, if packet doesn't belong to the correct transport channel.
  if (!FindConnection(connection))
    return;

  // Let the client know of an incoming packet
  SignalReadPacket(this, data, len, 0);
}

}  // namespace cricket
