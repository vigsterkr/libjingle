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

// The number of tokens in the config string.
static const size_t kConfigTokens = 2;
// The min number of tokens in the ice uri.
static const size_t kMinIceUriTokens = 2;
// The min number of tokens must present in Turn host uri.
// e.g. user@turn.example.org
static const size_t kTurnHostTokensNum = 2;
// Only the STUN or TURN server address appears in the config string.
static const size_t kConfigAddress = 1;
// Both of the STUN or TURN server address and port appear in the config string.
static const size_t kConfigAddressAndPort = 2;
static const size_t kServiceCount = 5;
// The default stun port.
static const int kDefaultPort = 3478;

// Deprecated (jsep00)
// NOTE: Must be in the same order as the ServiceType enum.
static const char* kValidServiceTypes[kServiceCount] = {
    "STUN", "STUNS", "TURN", "TURNS", "INVALID" };

// NOTE: Must be in the same order as the ServiceType enum.
static const char* kValidIceServiceTypes[kServiceCount] = {
    "stun", "stuns", "turn", "turns", "invalid" };

enum ServiceType {
  STUN,     // Indicates a STUN server.
  STUNS,    // Indicates a STUN server used with a TLS session.
  TURN,     // Indicates a TURN server
  TURNS,    // Indicates a TURN server used with a TLS session.
  INVALID,  // Unknown.
};

enum {
  MSG_ICECHANGE = 0,
  MSG_ICECANDIDATE,
  MSG_ICECOMPLETE,
};

struct CandidateMsg : public talk_base::MessageData {
  explicit CandidateMsg(const webrtc::JsepIceCandidate* candidate)
      : candidate(candidate) {
  }
  const webrtc::JsepIceCandidate* candidate;
};

typedef webrtc::PortAllocatorFactoryInterface::StunConfiguration
    StunConfiguration;
typedef webrtc::PortAllocatorFactoryInterface::TurnConfiguration
    TurnConfiguration;

bool static ParseConfigString(const std::string& config,
                              std::vector<StunConfiguration>* stun_config,
                              std::vector<TurnConfiguration>* turn_config) {
  std::vector<std::string> tokens;
  talk_base::tokenize(config, ' ', &tokens);

  if (tokens.size() != kConfigTokens) {
    LOG(WARNING) << "Invalid config string";
    return false;
  }

  ServiceType service_type = INVALID;

  const std::string& type = tokens[0];
  for (size_t i = 0; i < kServiceCount; ++i) {
    if (type.compare(kValidServiceTypes[i]) == 0) {
      service_type = static_cast<ServiceType>(i);
      break;
    }
  }

  if (service_type == INVALID) {
    LOG(WARNING) << "Invalid service type: " << type;
    return false;
  }
  std::string service_address = tokens[1];

  std::string address;
  int port;
  tokens.clear();
  talk_base::tokenize(service_address, ':', &tokens);
  if (tokens.size() != kConfigAddress &&
      tokens.size() != kConfigAddressAndPort) {
    LOG(WARNING) << "Invalid server address and port: " << service_address;
    return false;
  }

  if (tokens.size() == kConfigAddress) {
    address = tokens[0];
    port = kDefaultPort;
  } else {
    address = tokens[0];
    port = talk_base::FromString<int>(tokens[1]);
    if (port <= 0 || port > 0xffff) {
      LOG(WARNING) << "Invalid port: " << tokens[1];
      return false;
    }
  }

  // TODO: Currently the specification does not tell us how to parse
  // multiple addresses, username and password from the configuration string.
  switch (service_type) {
    case STUN:
      stun_config->push_back(StunConfiguration(address, port));
      break;
    case TURN:
      turn_config->push_back(TurnConfiguration(address, port, "", ""));
      break;
    case TURNS:
    case STUNS:
    case INVALID:
    default:
      LOG(WARNING) << "Configuration not supported";
      return false;
  }
  return true;
}

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
    for (size_t i = 0; i < kServiceCount; ++i) {
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
      port = talk_base::FromString<int>(tokens[2]);
      if (port <= 0 || port > 0xffff) {
        LOG(WARNING) << "Invalid port: " << tokens[2];
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
// Currently only one audio and one video track is supported per PeerConnection.
bool CanAddLocalMediaStream(webrtc::StreamCollectionInterface* current_streams,
                            webrtc::MediaStreamInterface* new_stream) {
  if (!new_stream || !current_streams)
    return false;

  bool audio_track_exist = false;
  bool video_track_exist = false;
  for (size_t j = 0; j < current_streams->count(); ++j) {
    if (!audio_track_exist) {
      audio_track_exist = current_streams->at(j)->audio_tracks()->count() > 0;
    }
    if (!video_track_exist) {
      video_track_exist = current_streams->at(j)->video_tracks()->count() > 0;
    }
  }
  if ((audio_track_exist && (new_stream->audio_tracks()->count() > 0)) ||
      (video_track_exist && (new_stream->video_tracks()->count() > 0))) {
    LOG(LS_ERROR) << "AddStream - Currently only one audio track and one"
                  << "video track is supported per PeerConnection.";
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
    const std::string& configuration,
    webrtc::PortAllocatorFactoryInterface* allocator_factory,
    PeerConnectionObserver* observer) {
  std::vector<PortAllocatorFactoryInterface::StunConfiguration> stun_config;
  std::vector<PortAllocatorFactoryInterface::TurnConfiguration> turn_config;
  ParseConfigString(configuration, &stun_config, &turn_config);
  return DoInitialize(stun_config, turn_config, NULL,
                      allocator_factory, observer);
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
  // enable BUNDLE here.
  port_allocator_->set_flags(cricket::PORTALLOCATOR_ENABLE_BUNDLE |
                             cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG);

  mediastream_signaling_.reset(new MediaStreamSignaling(
      factory_->signaling_thread(), this));

  session_.reset(new WebRtcSession(factory_->channel_manager(),
                                   factory_->signaling_thread(),
                                   factory_->worker_thread(),
                                   port_allocator_.get(),
                                   mediastream_signaling_.get()));
  stream_handler_.reset(new MediaStreamHandlers(session_.get(),
                                                session_.get()));

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

void PeerConnection::AddStream(LocalMediaStreamInterface* local_stream) {
  AddStream(local_stream, NULL);
}

bool PeerConnection::AddStream(MediaStreamInterface* local_stream,
                               const MediaConstraintsInterface* constraints) {
  if (!CanAddLocalMediaStream(local_media_streams_, local_stream))
    return false;

  // TODO(perkj): Implement support for MediaConstraints in AddStream.
  local_media_streams_->AddStream(local_stream);
  mediastream_signaling_->SetLocalStreams(local_media_streams_);
  observer_->OnRenegotiationNeeded();
  return true;
}

void PeerConnection::RemoveStream(MediaStreamInterface* remove_stream) {
  local_media_streams_->RemoveStream(remove_stream);
  mediastream_signaling_->SetLocalStreams(local_media_streams_);
  observer_->OnRenegotiationNeeded();
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
  if (!observer) {
    LOG(LS_ERROR) << "CreateOffer - observer is NULL.";
    return;
  }
  talk_base::scoped_refptr<CreateSessionDescriptionObserver> observer_copy =
      observer;
  // TODO(perkj): Take |constraints| into consideration.
  SessionDescriptionInterface* desc =
      CreateOffer(MediaHints(true, true));
  if (!desc) {
    std::string error = "CreateOffer failed.";
    observer_copy->OnFailure(error);
    return;
  }
  observer_copy->OnSuccess(desc);
}

SessionDescriptionInterface* PeerConnection::CreateAnswer(
    const MediaHints& hints,
    const SessionDescriptionInterface* offer) {
  return session_->CreateAnswer(hints, offer);
}

void PeerConnection::CreateAnswer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  if (!observer) {
    LOG(LS_ERROR) << "CreateAnswer - observer is NULL.";
    return;
  }
  talk_base::scoped_refptr<CreateSessionDescriptionObserver> observer_copy =
      observer;
  const SessionDescriptionInterface* offer = session_->remote_description();
  std::string error;
  if (!offer) {
    error = "CreateAnswer can't be called before SetRemoteDescription.";
    observer_copy->OnFailure(error);
    return;
  } else if (offer->type() != JsepSessionDescription::kOffer) {
    error = "CreateAnswer failed because remote_description is not an offer.";
    observer_copy->OnFailure(error);
    return;
  }
  // TODO(perkj): Take |constraints| into consideration.
  SessionDescriptionInterface* desc =
      CreateAnswer(MediaHints(true, true), offer);
  if (!desc) {
    error = "CreateAnswer failed.";
    observer_copy->OnFailure(error);
    return;
  }
  observer_copy->OnSuccess(desc);
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
  if (!observer) {
    LOG(LS_ERROR) << "SetLocalDescription - observer is NULL.";
    return;
  }
  talk_base::scoped_refptr<SetSessionDescriptionObserver> observer_copy =
      observer;
  std::string error;
  if (!desc) {
    error = "SessionDescription is NULL.";
    observer_copy->OnFailure(error);
    return;
  }

  if (!SetLocalDescription(JsepSessionDescription::GetAction(desc->type()),
                           desc)) {
    error = "SetLocalDescription failed.";
    observer_copy->OnFailure(error);
    return;
  }

  observer_copy->OnSuccess();
}

bool PeerConnection::SetRemoteDescription(Action action,
                                          SessionDescriptionInterface* desc) {
  return session_->SetRemoteDescription(action, desc);
}

void PeerConnection::SetRemoteDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  bool result = false;
  if (desc) {
    result = SetRemoteDescription(
        JsepSessionDescription::GetAction(desc->type()), desc);
  }
  if (!observer) {
    LOG(LS_ERROR) << "SetRemoteDescription - observer is NULL.";
    return;
  }
  talk_base::scoped_refptr<SetSessionDescriptionObserver> observer_copy =
      observer;
  if (!result) {
    std::string error = "SetRemoteDescription failed.";
    observer_copy->OnFailure(error);
    return;
  }
  observer_copy->OnSuccess();
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
    case MSG_ICECHANGE: {
      observer_->OnIceChange();
      break;
    }
    case MSG_ICECANDIDATE: {
      CandidateMsg* data = static_cast<CandidateMsg*>(msg->pdata);
      observer_->OnIceCandidate(data->candidate);
      delete data->candidate;
      delete data;
      break;
    }
    case MSG_ICECOMPLETE: {
      observer_->OnIceComplete();
      break;
    }
    default:
      break;
  }
}

void PeerConnection::OnAddStream(MediaStreamInterface* stream) {
  stream_handler_->AddRemoteStream(stream);
  observer_->OnAddStream(stream);
}

void PeerConnection::OnRemoveStream(MediaStreamInterface* stream) {
  stream_handler_->RemoveRemoteStream(stream);
  observer_->OnRemoveStream(stream);
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
