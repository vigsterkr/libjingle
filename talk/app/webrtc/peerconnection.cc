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

#include "talk/app/webrtc/peerconnection.h"

#include <vector>

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamhandler.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/base/logging.h"
#include "talk/base/stringencode.h"
#include "talk/session/media/channelmanager.h"

namespace {

// The min number of tokens in the ice uri.
static const size_t kMinIceUriTokens = 2;
// The min number of tokens must present in Turn host uri.
// e.g. user@turn.example.org
static const size_t kTurnHostTokensNum = 2;
// The default stun port.
static const int kDefaultPort = 3478;

// NOTE: Must be in the same order as the ServiceType enum.
static const char* kValidIceServiceTypes[] = {
    "stun", "stuns", "turn", "turns", "invalid" };

enum ServiceType {
  STUN,     // Indicates a STUN server.
  STUNS,    // Indicates a STUN server used with a TLS session.
  TURN,     // Indicates a TURN server
  TURNS,    // Indicates a TURN server used with a TLS session.
  INVALID,  // Unknown.
};

enum {
  MSG_CREATE_SESSIONDESCRIPTION_SUCCESS = 0,
  MSG_CREATE_SESSIONDESCRIPTION_FAILED,
  MSG_SET_SESSIONDESCRIPTION_SUCCESS,
  MSG_SET_SESSIONDESCRIPTION_FAILED,
  MSG_GETSTATS,
  MSG_ICECHANGE,
  MSG_ICECANDIDATE,
  MSG_ICECOMPLETE,
};

struct CandidateMsg : public talk_base::MessageData {
  explicit CandidateMsg(const webrtc::JsepIceCandidate* candidate)
      : candidate(candidate) {
  }
  talk_base::scoped_ptr<const webrtc::JsepIceCandidate> candidate;
};

struct CreateSessionDescriptionMsg : public talk_base::MessageData {
  explicit CreateSessionDescriptionMsg(
      webrtc::CreateSessionDescriptionObserver* observer)
      : observer(observer) {
  }

  talk_base::scoped_refptr<webrtc::CreateSessionDescriptionObserver> observer;
  std::string error;
  talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> description;
};

struct SetSessionDescriptionMsg : public talk_base::MessageData {
  explicit SetSessionDescriptionMsg(
      webrtc::SetSessionDescriptionObserver* observer)
      : observer(observer) {
  }

  talk_base::scoped_refptr<webrtc::SetSessionDescriptionObserver> observer;
  std::string error;
};

struct GetStatsMsg : public talk_base::MessageData {
  explicit GetStatsMsg(webrtc::StatsObserver* observer)
      : observer(observer) {
  }
  webrtc::StatsReports reports;
  talk_base::scoped_refptr<webrtc::StatsObserver> observer;
};

typedef webrtc::PortAllocatorFactoryInterface::StunConfiguration
    StunConfiguration;
typedef webrtc::PortAllocatorFactoryInterface::TurnConfiguration
    TurnConfiguration;

bool ParseIceServers(const webrtc::JsepInterface::IceServers& configuration,
                     std::vector<StunConfiguration>* stun_config,
                     std::vector<TurnConfiguration>* turn_config) {
  // draft-nandakumar-rtcweb-stun-uri-01
  // stunURI       = scheme ":" stun-host [ ":" stun-port ]
  // scheme        = "stun" / "stuns"
  // stun-host     = IP-literal / IPv4address / reg-name
  // stun-port     = *DIGIT

  // draft-petithuguenin-behave-turn-uris-01
  // turnURI       = scheme ":" turn-host [ ":" turn-port ]
  //                 [ "?transport=" transport ]
  // scheme        = "turn" / "turns"
  // transport     = "udp" / "tcp" / transport-ext
  // transport-ext = 1*unreserved
  // turn-host     = IP-literal / IPv4address / reg-name
  // turn-port     = *DIGIT

  // TODO(ronghuawu): Handle IPV6 address
  for (size_t i = 0; i < configuration.size(); ++i) {
    webrtc::JsepInterface::IceServer server = configuration[i];
    if (server.uri.empty()) {
      LOG(WARNING) << "Empty uri.";
      continue;
    }
    std::vector<std::string> tokens;
    talk_base::tokenize(server.uri, '?', &tokens);
    // TODO(ronghuawu): Handle [ "?transport=" transport ].
    std::string uri_without_transport = tokens[0];
    tokens.clear();
    talk_base::tokenize(uri_without_transport, ':', &tokens);
    if (tokens.size() < kMinIceUriTokens) {
      LOG(WARNING) << "Invalid uri: " << server.uri;
      continue;
    }
    ServiceType service_type = INVALID;
    const std::string& type = tokens[0];
    for (size_t i = 0; i < ARRAY_SIZE(kValidIceServiceTypes); ++i) {
      if (type.compare(kValidIceServiceTypes[i]) == 0) {
        service_type = static_cast<ServiceType>(i);
        break;
      }
    }
    if (service_type == INVALID) {
      LOG(WARNING) << "Invalid service type: " << type;
      continue;
    }
    std::string address = tokens[1];
    int port = kDefaultPort;
    if (tokens.size() > kMinIceUriTokens) {
      if (!talk_base::FromString(tokens[2], &port)) {
        LOG(LS_WARNING)  << "Failed to parse port string: " << tokens[2];
        continue;
      }

      if (port <= 0 || port > 0xffff) {
        LOG(WARNING) << "Invalid port: " << port;
        continue;
      }
    }

    switch (service_type) {
      case STUN:
      case STUNS:
        stun_config->push_back(StunConfiguration(address, port));
        break;
      case TURN:
      case TURNS: {
        // Turn url example from the spec |url:"turn:user@turn.example.org"|.
        std::vector<std::string> turn_tokens;
        talk_base::tokenize(address, '@', &turn_tokens);
        if (turn_tokens.size() != kTurnHostTokensNum) {
          LOG(LS_ERROR) << "Invalid TURN configuration : "
                        << address << " can't proceed.";
          return false;
        }
        std::string username = turn_tokens[0];
        address = turn_tokens[1];
        turn_config->push_back(TurnConfiguration(address, port,
                                                 username, server.password));
        // STUN functionality is part of TURN.
        stun_config->push_back(StunConfiguration(address, port));
        break;
      }
      case INVALID:
      default:
        LOG(WARNING) << "Configuration not supported: " << server.uri;
        return false;
    }
  }
  return true;
}

// Check if we can send |new_stream| on a PeerConnection.
// Currently only one audio but multiple video track is supported per
// PeerConnection.
bool CanAddLocalMediaStream(webrtc::StreamCollectionInterface* current_streams,
                            webrtc::MediaStreamInterface* new_stream) {
  if (!new_stream || !current_streams)
    return false;

  bool audio_track_exist = false;
  for (size_t j = 0; j < current_streams->count(); ++j) {
    if (!audio_track_exist) {
      audio_track_exist = current_streams->at(j)->audio_tracks()->count() > 0;
    }
  }
  if (audio_track_exist && (new_stream->audio_tracks()->count() > 0)) {
    LOG(LS_ERROR) << "AddStream - Currently only one audio track is supported"
                  << "per PeerConnection.";
    return false;
  }
  return true;
}

}  // namespace

namespace webrtc {

PeerConnection::PeerConnection(PeerConnectionFactory* factory)
    : factory_(factory),
      observer_(NULL),
      ready_state_(kNew),
      ice_state_(kIceNew),
      local_media_streams_(StreamCollection::Create()) {
}

PeerConnection::~PeerConnection() {
}

bool PeerConnection::Initialize(
    const JsepInterface::IceServers& configuration,
    const MediaConstraintsInterface* constraints,
    webrtc::PortAllocatorFactoryInterface* allocator_factory,
    PeerConnectionObserver* observer) {
  std::vector<PortAllocatorFactoryInterface::StunConfiguration> stun_config;
  std::vector<PortAllocatorFactoryInterface::TurnConfiguration> turn_config;
  if (!ParseIceServers(configuration, &stun_config, &turn_config)) {
    return false;
  }

  return DoInitialize(stun_config, turn_config, constraints,
                      allocator_factory, observer);
}

bool PeerConnection::DoInitialize(
    const StunConfigurations& stun_config,
    const TurnConfigurations& turn_config,
    const MediaConstraintsInterface* constraints,
    webrtc::PortAllocatorFactoryInterface* allocator_factory,
    PeerConnectionObserver* observer) {
  ASSERT(observer != NULL);
  if (!observer)
    return false;
  observer_ = observer;
  port_allocator_.reset(
      allocator_factory->CreatePortAllocator(stun_config, turn_config));
  // To handle both internal and externally created port allocator, we will
  // enable BUNDLE here. Also enabling TURN and disable legacy relay service.
  port_allocator_->set_flags(cricket::PORTALLOCATOR_ENABLE_BUNDLE |
                             cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG |
                             cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET);

  mediastream_signaling_.reset(new MediaStreamSignaling(
      factory_->signaling_thread(), this));

  session_.reset(new WebRtcSession(factory_->channel_manager(),
                                   factory_->signaling_thread(),
                                   factory_->worker_thread(),
                                   port_allocator_.get(),
                                   mediastream_signaling_.get()));
  stream_handler_.reset(new MediaStreamHandlers(session_.get(),
                                                session_.get()));
  stats_.set_session(session_.get());

  // Initialize the WebRtcSession. It creates transport channels etc.
  if (!session_->Initialize(constraints))
    return false;


  // Register PeerConnection as receiver of local ice candidates.
  // All the callbacks will be posted to the application from PeerConnection.
  session_->RegisterObserver(this);
  session_->SignalState.connect(this, &PeerConnection::OnSessionStateChange);
  return true;
}

talk_base::scoped_refptr<StreamCollectionInterface>
PeerConnection::local_streams() {
  return local_media_streams_;
}

talk_base::scoped_refptr<StreamCollectionInterface>
PeerConnection::remote_streams() {
  return mediastream_signaling_->remote_streams();
}

bool PeerConnection::AddStream(MediaStreamInterface* local_stream,
                               const MediaConstraintsInterface* constraints) {
  if (!CanAddLocalMediaStream(local_media_streams_, local_stream))
    return false;

  // TODO(perkj): Implement support for MediaConstraints in AddStream.
  local_media_streams_->AddStream(local_stream);
  mediastream_signaling_->SetLocalStreams(local_media_streams_);
  stats_.AddStream(local_stream);
  observer_->OnRenegotiationNeeded();
  return true;
}

void PeerConnection::RemoveStream(MediaStreamInterface* remove_stream) {
  local_media_streams_->RemoveStream(remove_stream);
  mediastream_signaling_->SetLocalStreams(local_media_streams_);
  observer_->OnRenegotiationNeeded();
}

bool PeerConnection::GetStats(StatsObserver* observer,
                              MediaStreamTrackInterface* track) {
  if (!VERIFY(observer != NULL)) {
    LOG(LS_ERROR) << "GetStats - observer is NULL.";
    return false;
  }

  stats_.UpdateStats();
  talk_base::scoped_ptr<GetStatsMsg> msg(new GetStatsMsg(observer));
  if (!stats_.GetStats(track, &(msg->reports))) {
    return false;
  }
  signaling_thread()->Post(this, MSG_GETSTATS, msg.release());
  return true;
}

PeerConnectionInterface::ReadyState PeerConnection::ready_state() {
  return ready_state_;
}

PeerConnectionInterface::IceState PeerConnection::ice_state() {
  return ice_state_;
}

bool PeerConnection::CanSendDtmf(const AudioTrackInterface* track) {
  if (!track) {
    return false;
  }
  return session_->CanSendDtmf(track->label());
}

bool PeerConnection::SendDtmf(const AudioTrackInterface* send_track,
                              const std::string& tones, int duration,
                              const AudioTrackInterface* play_track) {
  if (!send_track) {
    return false;
  }
  std::string play_name;
  if (play_track) {
    play_name = play_track->label();
  }

  return session_->SendDtmf(send_track->label(), tones, duration, play_name);
}

talk_base::scoped_refptr<DataChannelInterface>
PeerConnection::CreateDataChannel(
    const std::string& label,
    const DataChannelInit* config) {
  talk_base::scoped_refptr<DataChannelInterface> channel(
      session_->CreateDataChannel(label, config));
  observer_->OnRenegotiationNeeded();
  return channel;
}

bool PeerConnection::StartIce(IceOptions options) {
  // Ice will be started by default and will be removed in Jsep01.
  // TODO: Remove this method once fully migrated to JSEP01.
  return true;
}

SessionDescriptionInterface* PeerConnection::CreateOffer(
    const MediaHints& hints) {
  return session_->CreateOffer(hints);
}

void PeerConnection::CreateOffer(CreateSessionDescriptionObserver* observer,
                                 const MediaConstraintsInterface* constraints) {
  if (!VERIFY(observer != NULL)) {
    LOG(LS_ERROR) << "CreateOffer - observer is NULL.";
    return;
  }

  CreateSessionDescriptionMsg* msg = new CreateSessionDescriptionMsg(observer);
  msg->description.reset(
      session_->CreateOffer(constraints));

  if (!msg->description) {
    msg->error = "CreateOffer failed.";
    signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_FAILED, msg);
    return;
  }

  signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_SUCCESS, msg);
}

SessionDescriptionInterface* PeerConnection::CreateAnswer(
    const MediaHints& hints,
    const SessionDescriptionInterface* offer) {
  return session_->CreateAnswer(hints, offer);
}

void PeerConnection::CreateAnswer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  if (!VERIFY(observer != NULL)) {
    LOG(LS_ERROR) << "CreateAnswer - observer is NULL.";
    return;
  }

  CreateSessionDescriptionMsg* msg = new CreateSessionDescriptionMsg(observer);
  // TODO(perkj): This checks should be done by the session. Not here.
  // Clean this up once the old Jsep API has been removed.
  const SessionDescriptionInterface* offer = session_->remote_description();
  std::string error;
  if (!offer) {
    msg->error = "CreateAnswer can't be called before SetRemoteDescription.";
    signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_FAILED, msg);
    return;
  }
  if (offer->type() != JsepSessionDescription::kOffer) {
    msg->error =
        "CreateAnswer failed because remote_description is not an offer.";
    signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_FAILED, msg);
    return;
  }

  msg->description.reset(session_->CreateAnswer(constraints, offer));
  if (!msg->description) {
    msg->error = "CreateAnswer failed.";
    signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_FAILED, msg);
    return;
  }

  signaling_thread()->Post(this, MSG_CREATE_SESSIONDESCRIPTION_SUCCESS, msg);
}

bool PeerConnection::SetLocalDescription(Action action,
                                         SessionDescriptionInterface* desc) {
  bool result =  session_->SetLocalDescription(action, desc);
  stream_handler_->CommitLocalStreams(local_media_streams_);
  return result;
}

void PeerConnection::SetLocalDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  if (!VERIFY(observer != NULL)) {
    LOG(LS_ERROR) << "SetLocalDescription - observer is NULL.";
    return;
  }
  if (!desc) {
    PostSetSessionDescriptionFailure(observer, "SessionDescription is NULL.");
    return;
  }
  // Update stats here so that we have the most recent stats for tracks and
  // streams that might be removed by updating the session description.
  stats_.UpdateStats();
  if (!SetLocalDescription(JsepSessionDescription::GetAction(desc->type()),
                           desc)) {
    PostSetSessionDescriptionFailure(observer, "SetLocalDescription failed.");
    return;
  }
  stream_handler_->CommitLocalStreams(local_media_streams_);
  SetSessionDescriptionMsg* msg =  new SetSessionDescriptionMsg(observer);
  signaling_thread()->Post(this, MSG_SET_SESSIONDESCRIPTION_SUCCESS, msg);
}

bool PeerConnection::SetRemoteDescription(Action action,
                                          SessionDescriptionInterface* desc) {
  return session_->SetRemoteDescription(action, desc);
}

void PeerConnection::SetRemoteDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  if (!VERIFY(observer != NULL)) {
    LOG(LS_ERROR) << "SetRemoteDescription - observer is NULL.";
    return;
  }

  if (!desc) {
    PostSetSessionDescriptionFailure(observer, "SessionDescription is NULL.");
    return;
  }
  // Update stats here so that we have the most recent stats for tracks and
  // streams that might be removed by updating the session description.
  stats_.UpdateStats();
  if (!SetRemoteDescription(JsepSessionDescription::GetAction(desc->type()),
                            desc)) {
    PostSetSessionDescriptionFailure(observer, "SetRemoteDescription failed.");
    return;
  }
  SetSessionDescriptionMsg* msg  = new SetSessionDescriptionMsg(observer);
  signaling_thread()->Post(this, MSG_SET_SESSIONDESCRIPTION_SUCCESS, msg);
}

void PeerConnection::PostSetSessionDescriptionFailure(
    SetSessionDescriptionObserver* observer,
    const std::string& error) {
  SetSessionDescriptionMsg* msg  = new SetSessionDescriptionMsg(observer);
  msg->error = error;
  signaling_thread()->Post(this, MSG_SET_SESSIONDESCRIPTION_FAILED, msg);
}

bool PeerConnection::UpdateIce(const IceServers& configuration,
                               const MediaConstraintsInterface* constraints) {
  // TODO(ronghuawu): Implement UpdateIce.
  LOG(LS_ERROR) << "UpdateIce is not implemented.";
  return false;
}

bool PeerConnection::ProcessIceMessage(
    const IceCandidateInterface* ice_candidate) {
  return session_->ProcessIceMessage(ice_candidate);
}

bool PeerConnection::AddIceCandidate(
    const IceCandidateInterface* ice_candidate) {
  return ProcessIceMessage(ice_candidate);
}

const SessionDescriptionInterface* PeerConnection::local_description() const {
  return session_->local_description();
}

const SessionDescriptionInterface* PeerConnection::remote_description() const {
  return session_->remote_description();
}

void PeerConnection::OnSessionStateChange(cricket::BaseSession* /*session*/,
                                          cricket::BaseSession::State state) {
  switch (state) {
    case cricket::BaseSession::STATE_INIT:
      ChangeReadyState(PeerConnectionInterface::kNew);
    case cricket::BaseSession::STATE_SENTINITIATE:
    case cricket::BaseSession::STATE_RECEIVEDINITIATE:
      ChangeReadyState(PeerConnectionInterface::kOpening);
      break;
    case cricket::BaseSession::STATE_SENTACCEPT:
    case cricket::BaseSession::STATE_RECEIVEDACCEPT:
      ChangeReadyState(PeerConnectionInterface::kActive);
      break;
    default:
      break;
  }
}

void PeerConnection::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_CREATE_SESSIONDESCRIPTION_SUCCESS: {
      CreateSessionDescriptionMsg* param =
          static_cast<CreateSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnSuccess(param->description.release());
      delete param;
      break;
    }
    case MSG_CREATE_SESSIONDESCRIPTION_FAILED: {
      CreateSessionDescriptionMsg* param =
          static_cast<CreateSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnFailure(param->error);
      delete param;
      break;
    }
    case MSG_SET_SESSIONDESCRIPTION_SUCCESS: {
      SetSessionDescriptionMsg* param =
          static_cast<SetSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnSuccess();
      delete param;
      break;
    }
    case MSG_SET_SESSIONDESCRIPTION_FAILED: {
      SetSessionDescriptionMsg* param =
          static_cast<SetSessionDescriptionMsg*>(msg->pdata);
      param->observer->OnFailure(param->error);
      delete param;
      break;
    }
    case MSG_GETSTATS: {
      GetStatsMsg* param = static_cast<GetStatsMsg*>(msg->pdata);
      param->observer->OnComplete(param->reports);
      delete param;
      break;
    }
    case MSG_ICECHANGE: {
      observer_->OnIceChange();
      break;
    }
    case MSG_ICECANDIDATE: {
      CandidateMsg* data = static_cast<CandidateMsg*>(msg->pdata);
      observer_->OnIceCandidate(data->candidate.get());
      delete data;
      break;
    }
    case MSG_ICECOMPLETE: {
      observer_->OnIceComplete();
      break;
    }
    default:
      ASSERT(!"Not implemented");
      break;
  }
}

void PeerConnection::OnAddStream(MediaStreamInterface* stream) {
  stream_handler_->AddRemoteStream(stream);
  stats_.AddStream(stream);
  observer_->OnAddStream(stream);
}

void PeerConnection::OnRemoveStream(MediaStreamInterface* stream) {
  stream_handler_->RemoveRemoteStream(stream);
  observer_->OnRemoveStream(stream);
}

void PeerConnection::OnAddDataChannel(DataChannelInterface* data_channel) {
  observer_->OnDataChannel(data_channel);
}

void PeerConnection::OnIceChange() {
  signaling_thread()->Post(this, MSG_ICECHANGE);
}

void PeerConnection::OnIceCandidate(const IceCandidateInterface* candidate) {
  JsepIceCandidate* candidate_copy = NULL;
  if (candidate) {
    // TODO(ronghuawu): Make IceCandidateInterface reference counted instead
    // of making a copy.
    candidate_copy = new JsepIceCandidate(candidate->sdp_mid(),
                                          candidate->sdp_mline_index(),
                                          candidate->candidate());
  }
  // The Post takes the ownership of the |candidate_copy|.
  signaling_thread()->Post(this, MSG_ICECANDIDATE,
                           new CandidateMsg(candidate_copy));
}

void PeerConnection::OnIceComplete() {
  signaling_thread()->Post(this, MSG_ICECOMPLETE);
}

void PeerConnection::ChangeReadyState(
    PeerConnectionInterface::ReadyState ready_state) {
  ready_state_ = ready_state;
  observer_->OnStateChange(PeerConnectionObserver::kReadyState);
}

}  // namespace webrtc
