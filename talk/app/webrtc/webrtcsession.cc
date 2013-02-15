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

#include "talk/app/webrtc/webrtcsession.h"

#include <algorithm>
#include <vector>

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/stringencode.h"
#include "talk/media/base/videocapturer.h"
#include "talk/session/media/channel.h"
#include "talk/session/media/channelmanager.h"
#include "talk/session/media/mediasession.h"

using cricket::ContentInfo;
using cricket::ContentInfos;
using cricket::MediaContentDescription;
using cricket::SessionDescription;
using cricket::TransportInfo;

typedef cricket::MediaSessionOptions::Stream Stream;
typedef cricket::MediaSessionOptions::Streams Streams;

namespace webrtc {

enum {
  MSG_CANDIDATE_TIMEOUT = 101,
};

static const uint64 kInitSessionVersion = 2;

// We allow 30 seconds to establish a connection, otherwise it's an error.
static const int kCallSetupTimeout = 30 * 1000;

// Supported MediaConstraints.
// DTLS-SRTP pseudo-constraints.
const char MediaConstraintsInterface::kEnableDtlsSrtp[] =
    "DtlsSrtpKeyAgreement";
// DataChannel pseudo constraints.
const char MediaConstraintsInterface::kEnableRtpDataChannels[] =
    "RtpDataChannels";

// Arbitrary constant used as prefix for the identity.
// Chosen to make the certificates more readable.
const char kWebRTCIdentityPrefix[] = "WebRTC";

const char MediaConstraintsInterface::kValueTrue[] = "true";
const char MediaConstraintsInterface::kValueFalse[] = "false";

// Error messages
const char kInvalidSdp[] = "Invalid session description.";
const char kSdpWithoutCrypto[] =
    "Called with a SDP without crypto enabled.";
const char kCreateChannelFailed[] = "Failed to create channels.";
const char kUpdateStateFailed[] = "Failed to update session state.";
const char kMlineMismatch[] =
    "Offer and answer descriptions m-lines are not matching. "
    "Rejecting answer.";
const char kInvalidCandidates[] =
    "Description contains invalid candidates.";

// Compares |answer| against |offer|. Comparision is done
// for number of m-lines in answer against offer. If matches true will be
// returned otherwise false.
static bool VerifyMediaDescriptions(
    const SessionDescription* answer, const SessionDescription* offer) {
  if (offer->contents().size() != answer->contents().size())
    return false;

  for (size_t i = 0; i < offer->contents().size(); ++i) {
    if ((offer->contents()[i].name) != answer->contents()[i].name) {
      return false;
    }
  }
  return true;
}

static void CopyCandidatesFromSessionDescription(
    const SessionDescriptionInterface* source_desc,
    SessionDescriptionInterface* dest_desc) {
  if (!source_desc)
    return;
  for (size_t m = 0; m < source_desc->number_of_mediasections() &&
                     m < dest_desc->number_of_mediasections(); ++m) {
    const IceCandidateCollection* source_candidates =
        source_desc->candidates(m);
    const IceCandidateCollection* dest_candidates = dest_desc->candidates(m);
    for  (size_t n = 0; n < source_candidates->count(); ++n) {
      const IceCandidateInterface* new_candidate = source_candidates->at(n);
      if (!dest_candidates->HasCandidate(new_candidate))
        dest_desc->AddCandidate(source_candidates->at(n));
    }
  }
}

// Checks that each non-rejected content has SDES crypto keys or a DTLS
// fingerprint. Mismatches, such as replying with a DTLS fingerprint to SDES
// keys, will be caught in Transport negotiation, and backstopped by Channel's
// |secure_required| check.
static bool VerifyCrypto(const SessionDescription* desc) {
  if (!desc) {
    return false;
  }
  const ContentInfos& contents = desc->contents();
  for (size_t index = 0; index < contents.size(); ++index) {
    const ContentInfo* cinfo = &contents[index];
    if (cinfo->rejected) {
      continue;
    }

    // If the content isn't rejected, crypto must be present.
    const MediaContentDescription* media =
        static_cast<const MediaContentDescription*>(cinfo->description);
    const TransportInfo* tinfo = desc->GetTransportInfoByName(cinfo->name);
    if (!media || !tinfo) {
      // Something is not right.
      LOG(LS_ERROR) << kInvalidSdp;
      return false;
    }
    if (media->cryptos().empty() &&
        !tinfo->description.identity_fingerprint) {
      // Crypto must be supplied.
      LOG(LS_WARNING) << "Session description must have SDES or DTLS-SRTP.";
      return false;
    }
  }

  return true;
}

static bool CompareStream(const Stream& stream1, const Stream& stream2) {
  return (stream1.name < stream2.name);
}

static bool SameName(const Stream& stream1, const Stream& stream2) {
  return (stream1.name == stream2.name);
}

// Checks if each Stream within the |streams| has unique name.
static bool ValidStreams(const Streams& streams) {
  Streams sorted_streams = streams;
  std::sort(sorted_streams.begin(), sorted_streams.end(), CompareStream);
  Streams::iterator it =
      std::adjacent_find(sorted_streams.begin(), sorted_streams.end(),
                         SameName);
  return (it == sorted_streams.end());
}

static bool GetAudioSsrcByName(const SessionDescription* session_description,
                               const std::string& name, uint32 *ssrc) {
  const cricket::ContentInfo* audio_info =
      cricket::GetFirstAudioContent(session_description);
  if (!audio_info) {
    LOG(LS_ERROR) << "Audio not used in this call";
    return false;
  }

  const cricket::MediaContentDescription* audio_content =
      static_cast<const cricket::MediaContentDescription*>(
          audio_info->description);
  cricket::StreamParams stream;
  if (!cricket::GetStreamByNickAndName(audio_content->streams(), "", name,
                                       &stream)) {
    return false;
  }
  *ssrc = stream.first_ssrc();
  return true;
}

static bool GetVideoSsrcByName(const SessionDescription* session_description,
                               const std::string& name, uint32 *ssrc) {
  const cricket::ContentInfo* video_info =
      cricket::GetFirstVideoContent(session_description);
  if (!video_info) {
    LOG(LS_ERROR) << "Video not used in this call";
    return false;
  }

  const cricket::MediaContentDescription* video_content =
      static_cast<const cricket::MediaContentDescription*>(
          video_info->description);
  cricket::StreamParams stream;
  if (!cricket::GetStreamByNickAndName(video_content->streams(), "", name,
                                      &stream)) {
    return false;
  }
  *ssrc = stream.first_ssrc();
  return true;
}

static bool GetNameBySsrc(const SessionDescription* session_description,
                          uint32 ssrc, std::string* name) {
  ASSERT(name != NULL);

  cricket::StreamParams stream_out;
  const cricket::ContentInfo* audio_info =
      cricket::GetFirstAudioContent(session_description);
  if (!audio_info) {
    return false;
  }
  const cricket::MediaContentDescription* audio_content =
      static_cast<const cricket::MediaContentDescription*>(
          audio_info->description);

  if (cricket::GetStreamBySsrc(audio_content->streams(), ssrc, &stream_out)) {
    *name = stream_out.name;
    return true;
  }

  const cricket::ContentInfo* video_info =
      cricket::GetFirstVideoContent(session_description);
  if (!video_info) {
    return false;
  }
  const cricket::MediaContentDescription* video_content =
      static_cast<const cricket::MediaContentDescription*>(
          video_info->description);

  if (cricket::GetStreamBySsrc(video_content->streams(), ssrc, &stream_out)) {
    *name = stream_out.name;
    return true;
  }
  return false;
}

static bool FindConstraint(const MediaConstraintsInterface::Constraints&
  constraints, const std::string& key, std::string* value) {
  for (MediaConstraintsInterface::Constraints::const_iterator iter =
           constraints.begin(); iter != constraints.end(); ++iter) {
    if (iter->key == key) {
      if (value)
        *value = iter->value;

      return true;
    }
  }

  return false;
}

static bool FindConstraint(const MediaConstraintsInterface* constraints,
  const std::string& key, std::string* value, bool* mandatory) {
  if (!constraints)
    return false;

  if (FindConstraint(constraints->GetMandatory(), key, value)) {
    if (mandatory)
      *mandatory = true;
    return true;
  }

  if (FindConstraint(constraints->GetOptional(), key, value)) {
    if (mandatory)
      *mandatory = false;
    return true;
  }

  return false;
}

static bool BadSdp(const std::string& desc, std::string* err_desc) {
  if (err_desc) {
    *err_desc = desc;
  }
  LOG(LS_ERROR) << desc;
  return false;
}

static bool BadLocalSdp(const std::string& desc, std::string* err_desc) {
  std::string set_local_sdp_failed = "SetLocalDescription failed: ";
  set_local_sdp_failed.append(desc);
  return BadSdp(set_local_sdp_failed, err_desc);
}

static bool BadRemoteSdp(const std::string& desc, std::string* err_desc) {
  std::string set_remote_sdp_failed = "SetRemoteDescription failed: ";
  set_remote_sdp_failed.append(desc);
  return BadSdp(set_remote_sdp_failed, err_desc);
}

static std::string SessionErrorMsg(cricket::BaseSession::Error error) {
  std::ostringstream desc;
  desc << "Session error code: " << error;
  return desc.str();
}

#define GET_STRING_OF_STATE(state)  \
  case cricket::BaseSession::state:  \
    result = #state;  \
    break;

static std::string GetStateString(cricket::BaseSession::State state) {
  std::string result;
  switch (state) {
    GET_STRING_OF_STATE(STATE_INIT)
    GET_STRING_OF_STATE(STATE_SENTINITIATE)
    GET_STRING_OF_STATE(STATE_RECEIVEDINITIATE)
    GET_STRING_OF_STATE(STATE_SENTPRACCEPT)
    GET_STRING_OF_STATE(STATE_SENTACCEPT)
    GET_STRING_OF_STATE(STATE_RECEIVEDPRACCEPT)
    GET_STRING_OF_STATE(STATE_RECEIVEDACCEPT)
    GET_STRING_OF_STATE(STATE_SENTMODIFY)
    GET_STRING_OF_STATE(STATE_RECEIVEDMODIFY)
    GET_STRING_OF_STATE(STATE_SENTREJECT)
    GET_STRING_OF_STATE(STATE_RECEIVEDREJECT)
    GET_STRING_OF_STATE(STATE_SENTREDIRECT)
    GET_STRING_OF_STATE(STATE_SENTTERMINATE)
    GET_STRING_OF_STATE(STATE_RECEIVEDTERMINATE)
    GET_STRING_OF_STATE(STATE_INPROGRESS)
    GET_STRING_OF_STATE(STATE_DEINIT)
    default:
      ASSERT(false);
      break;
  }
  return result;
}

// Help class used to remember if a a remote peer has requested ice restart by
// by sending a description with new ice ufrag and password.
class IceRestartAnswerLatch {
 public:
  IceRestartAnswerLatch() : ice_restart_(false) { }

  // Returns true if CheckForRemoteIceRestart has been called since last
  // time this method was called with a new session description where
  // ice password and ufrag has changed.
  bool AnswerWithIceRestartLatch() {
    if (ice_restart_) {
      ice_restart_ = false;
      return true;
    }
    return false;
  }

  void CheckForRemoteIceRestart(
      const SessionDescriptionInterface* old_desc,
      const SessionDescriptionInterface* new_desc) {
    if (!old_desc || new_desc->type() != SessionDescriptionInterface::kOffer) {
      return;
    }
    const SessionDescription* new_sd = new_desc->description();
    const SessionDescription* old_sd = old_desc->description();
    const ContentInfos& contents = new_sd->contents();
    for (size_t index = 0; index < contents.size(); ++index) {
      const ContentInfo* cinfo = &contents[index];
      if (cinfo->rejected) {
        continue;
      }
      // If the content isn't rejected, check if ufrag and password has
      // changed.
      const cricket::TransportDescription* new_transport_desc =
          new_sd->GetTransportDescriptionByName(cinfo->name);
      const cricket::TransportDescription* old_transport_desc =
          old_sd->GetTransportDescriptionByName(cinfo->name);
      if (!new_transport_desc || !old_transport_desc) {
        // No transport description exist. This is not an ice restart.
        continue;
      }
      if (new_transport_desc->ice_pwd != old_transport_desc->ice_pwd &&
          new_transport_desc->ice_ufrag != old_transport_desc->ice_ufrag) {
        LOG(LS_INFO) << "Remote peer request ice restart.";
        ice_restart_ = true;
        break;
      }
    }
  }

 private:
  bool ice_restart_;
};

WebRtcSession::WebRtcSession(cricket::ChannelManager* channel_manager,
                             talk_base::Thread* signaling_thread,
                             talk_base::Thread* worker_thread,
                             cricket::PortAllocator* port_allocator,
                             MediaStreamSignaling* mediastream_signaling)
    : cricket::BaseSession(signaling_thread, worker_thread, port_allocator,
                           talk_base::ToString(talk_base::CreateRandomId()),
                           cricket::NS_JINGLE_RTP, false),
      channel_manager_(channel_manager),
      session_desc_factory_(channel_manager, &transport_desc_factory_),
      mediastream_signaling_(mediastream_signaling),
      ice_observer_(NULL),
      ice_connection_state_(PeerConnectionInterface::kIceConnectionNew),
      // RFC 4566 suggested a Network Time Protocol (NTP) format timestamp
      // as the session id and session version. To simplify, it should be fine
      // to just use a random number as session id and start version from
      // |kInitSessionVersion|.
      session_id_(talk_base::ToString(talk_base::CreateRandomId())),
      session_version_(kInitSessionVersion),
      older_version_remote_peer_(false),
      allow_rtp_data_engine_(false),
      ice_restart_latch_(new IceRestartAnswerLatch) {
  transport_desc_factory_.set_protocol(cricket::ICEPROTO_HYBRID);
}

WebRtcSession::~WebRtcSession() {
  if (voice_channel_.get()) {
    channel_manager_->DestroyVoiceChannel(voice_channel_.release());
  }
  if (video_channel_.get()) {
    channel_manager_->DestroyVideoChannel(video_channel_.release());
  }
  if (data_channel_.get()) {
    channel_manager_->DestroyDataChannel(data_channel_.release());
  }
  for (size_t i = 0; i < saved_candidates_.size(); ++i) {
    delete saved_candidates_[i];
  }
}

bool WebRtcSession::Initialize(const MediaConstraintsInterface* constraints) {
  // TODO(perkj): Take |constraints| into consideration. Return false if not all
  // mandatory constraints can be fulfilled. Note that |constraints|
  // can be null.

  // By default SRTP-SDES is enabled in WebRtc.
  set_secure_policy(cricket::SEC_REQUIRED);

  // Enable DTLS-SRTP if the constraint is set.
  std::string value;
  if (FindConstraint(constraints, MediaConstraintsInterface::kEnableDtlsSrtp,
      &value, NULL) && value == MediaConstraintsInterface::kValueTrue) {
    LOG(LS_INFO) << "DTLS-SRTP enabled; generating identity";
    std::string identity_name = kWebRTCIdentityPrefix +
        talk_base::ToString(talk_base::CreateRandomId());
    transport_desc_factory_.set_identity(talk_base::SSLIdentity::Generate(
        identity_name));
    LOG(LS_INFO) << "Finished generating identity";
    set_identity(transport_desc_factory_.identity());
    transport_desc_factory_.set_digest_algorithm(talk_base::DIGEST_SHA_256);

    transport_desc_factory_.set_secure(cricket::SEC_ENABLED);
  }

  // Enable creation of RTP data channels if the kEnableRtpDataChannels is set.
  allow_rtp_data_engine_ = FindConstraint(
      constraints, MediaConstraintsInterface::kEnableRtpDataChannels,
      &value, NULL) && value == MediaConstraintsInterface::kValueTrue;
  if (allow_rtp_data_engine_)
    mediastream_signaling_->SetDataChannelFactory(this);

  // Make sure SessionDescriptions only contains the StreamParams we negotiate.
  session_desc_factory_.set_add_legacy_streams(false);

  const cricket::VideoCodec default_codec(
      JsepSessionDescription::kDefaultVideoCodecId,
      JsepSessionDescription::kDefaultVideoCodecName,
      JsepSessionDescription::kMaxVideoCodecWidth,
      JsepSessionDescription::kMaxVideoCodecHeight,
      JsepSessionDescription::kDefaultVideoCodecFramerate,
      JsepSessionDescription::kDefaultVideoCodecPreference);
  channel_manager_->SetDefaultVideoEncoderConfig(
      cricket::VideoEncoderConfig(default_codec));
  return true;
}

bool WebRtcSession::StartCandidatesAllocation() {
  // SpeculativelyConnectTransportChannels, will call ConnectChannels method
  // from TransportProxy to start gathering ice candidates.
  SpeculativelyConnectAllTransportChannels();
  if (!saved_candidates_.empty()) {
    // If there are saved candidates which arrived before local description is
    // set, copy those to remote description.
    CopySavedCandidates(remote_desc_.get());
  }
  // Push remote candidates present in remote description to transport channels.
  UseCandidatesInSessionDescription(remote_desc_.get());
  return true;
}

void WebRtcSession::set_secure_policy(
    cricket::SecureMediaPolicy secure_policy) {
  session_desc_factory_.set_secure(secure_policy);
}

SessionDescriptionInterface* WebRtcSession::CreateOffer(
    const MediaConstraintsInterface* constraints) {
  cricket::MediaSessionOptions options;

  if (!mediastream_signaling_->GetOptionsForOffer(constraints, &options)) {
    LOG(LS_ERROR) << "CreateOffer called with invalid constraints.";
    return NULL;
  }

  if (!ValidStreams(options.streams)) {
    LOG(LS_ERROR) << "CreateOffer called with invalid media streams.";
    return NULL;
  }
  SessionDescription* desc(
      session_desc_factory_.CreateOffer(options,
                                        BaseSession::local_description()));
  // RFC 3264
  // When issuing an offer that modifies the session,
  // the "o=" line of the new SDP MUST be identical to that in the
  // previous SDP, except that the version in the origin field MUST
  // increment by one from the previous SDP.

  // Just increase the version number by one each time when a new offer
  // is created regardless if it's identical to the previous one or not.
  // The |session_version_| is a uint64, the wrap around should not happen.
  ASSERT(session_version_ + 1 > session_version_);
  JsepSessionDescription* offer(new JsepSessionDescription(
      JsepSessionDescription::kOffer));
  if (!offer->Initialize(desc, session_id_,
                         talk_base::ToString(session_version_++))) {
    delete offer;
    return NULL;
  }
  if (local_description())
    CopyCandidatesFromSessionDescription(local_description(), offer);
  return offer;
}

SessionDescriptionInterface* WebRtcSession::CreateAnswer(
    const MediaConstraintsInterface* constraints,
    const SessionDescriptionInterface* offer) {
  cricket::MediaSessionOptions options;
  if (!mediastream_signaling_->GetOptionsForAnswer(constraints, &options)) {
    LOG(LS_ERROR) << "CreateAnswer called with invalid constraints.";
    return NULL;
  }

  if (!offer) {
    LOG(LS_ERROR) << "Offer can't be NULL in CreateAnswer.";
    return NULL;
  }
  if (!ValidStreams(options.streams)) {
    LOG(LS_ERROR) << "CreateAnswer called with invalid media streams.";
    return NULL;
  }

  // According to http://tools.ietf.org/html/rfc5245#section-9.2.1.1
  // an answer should also contain new ice ufrag and password if an offer has
  // been received with new ufrag and password.
  options.transport_options.ice_restart =
      ice_restart_latch_->AnswerWithIceRestartLatch();
  SessionDescription* desc(
      session_desc_factory_.CreateAnswer(offer->description(), options,
                                         BaseSession::local_description()));
  // RFC 3264
  // If the answer is different from the offer in any way (different IP
  // addresses, ports, etc.), the origin line MUST be different in the answer.
  // In that case, the version number in the "o=" line of the answer is
  // unrelated to the version number in the o line of the offer.
  // Get a new version number by increasing the |session_version_answer_|.
  // The |session_version_| is a uint64, the wrap around should not happen.
  ASSERT(session_version_ + 1 > session_version_);
  JsepSessionDescription* answer(new JsepSessionDescription(
      JsepSessionDescription::kAnswer));
  if (!answer->Initialize(desc, session_id_,
                          talk_base::ToString(session_version_++))) {
    delete answer;
    return NULL;
  }
  if (local_description())
    CopyCandidatesFromSessionDescription(local_description(), answer);
  return answer;
}

bool WebRtcSession::SetLocalDescription(SessionDescriptionInterface* desc,
                                        std::string* err_desc) {
  if (!desc || !desc->description()) {
    delete desc;
    return BadLocalSdp(kInvalidSdp, err_desc);
  }
  Action action = GetAction(desc->type());
  if (!ExpectSetLocalDescription(action)) {
    std::string type = desc->type();
    delete desc;
    return BadLocalSdp(BadStateErrMsg(type, state()), err_desc);
  }

  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !VerifyCrypto(desc->description())) {
    delete desc;
    return BadLocalSdp(kSdpWithoutCrypto, err_desc);
  }

  if (action == kAnswer && !VerifyMediaDescriptions(
      desc->description(), remote_description()->description())) {
    return BadLocalSdp(kMlineMismatch, err_desc);
  }

  // Update the initiator flag if this session is the initiator.
  if (state() == STATE_INIT && action == kOffer) {
    set_initiator(true);
  }

  // Update the MediaContentDescription crypto settings as per the policy set.
  UpdateSessionDescriptionSecurePolicy(desc->description());

  set_local_description(desc->description()->Copy());
  local_desc_.reset(desc);

  // Transport and Media channels will be created only when local description is
  // set.
  if (!CreateChannels(action, desc->description())) {
    // TODO(mallinath) - Handle CreateChannel failure, as new local description
    // is applied. Restore back to old description.
    return BadLocalSdp(kCreateChannelFailed, err_desc);
  }

  if (!UpdateSessionState(action, cricket::CS_LOCAL, desc->description())) {
    return BadLocalSdp(kUpdateStateFailed, err_desc);
  }
  // Kick starting the ice candidates allocation.
  StartCandidatesAllocation();

  // Update state and SSRC of local MediaStreams and DataChannels based on the
  // local session description.
  mediastream_signaling_->UpdateLocalStreams(local_desc_.get());

  if (error() != cricket::BaseSession::ERROR_NONE) {
    return BadLocalSdp(SessionErrorMsg(error()), err_desc);
  }
  return true;
}

bool WebRtcSession::SetRemoteDescription(SessionDescriptionInterface* desc,
                                         std::string* err_desc) {
  if (!desc || !desc->description()) {
    delete desc;
    return BadRemoteSdp(kInvalidSdp, err_desc);
  }
  Action action = GetAction(desc->type());
  if (!ExpectSetRemoteDescription(action)) {
    std::string type = desc->type();
    delete desc;
    return BadRemoteSdp(BadStateErrMsg(type, state()), err_desc);
  }

  if (action == kAnswer && !VerifyMediaDescriptions(
      desc->description(), local_description()->description())) {
    return BadRemoteSdp(kMlineMismatch, err_desc);
  }

  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !VerifyCrypto(desc->description())) {
    delete desc;
    return BadRemoteSdp(kSdpWithoutCrypto, err_desc);
  }

  // Transport and Media channels will be created only when local description is
  // set.
  if (!CreateChannels(action, desc->description())) {
    // TODO(mallinath) - Handle CreateChannel failure, as new local description
    // is applied. Restore back to old description.
    return BadRemoteSdp(kCreateChannelFailed, err_desc);
  }

  // NOTE: Candidates allocation will be initiated only when SetLocalDescription
  // is called.
  set_remote_description(desc->description()->Copy());
  if (!UpdateSessionState(action, cricket::CS_REMOTE, desc->description())) {
    return BadRemoteSdp(kUpdateStateFailed, err_desc);
  }

  // Update remote MediaStreams.
  mediastream_signaling_->UpdateRemoteStreams(desc);
  if (local_description() && !UseCandidatesInSessionDescription(desc)) {
    delete desc;
    return BadRemoteSdp(kInvalidCandidates, err_desc);
  }

  // Copy all saved candidates.
  CopySavedCandidates(desc);
  // We retain all received candidates.
  CopyCandidatesFromSessionDescription(remote_desc_.get(), desc);
  // Check if this new SessionDescription contains new ice ufrag and password
  // that indicates the remote peer requests ice restart.
  ice_restart_latch_->CheckForRemoteIceRestart(remote_desc_.get(),
                                               desc);
  remote_desc_.reset(desc);
  if (error() != cricket::BaseSession::ERROR_NONE) {
    return BadRemoteSdp(SessionErrorMsg(error()), err_desc);
  }
  return true;
}

bool WebRtcSession::UpdateSessionState(
    Action action, cricket::ContentSource source,
    const cricket::SessionDescription* desc) {
  bool ret = false;
  if (action == kOffer) {
    if (PushdownTransportDescription(source, cricket::CA_OFFER)) {
      SetState(source == cricket::CS_LOCAL ?
        STATE_SENTINITIATE : STATE_RECEIVEDINITIATE);
      ret = (error() == cricket::BaseSession::ERROR_NONE);
    }
  } else if (action == kPrAnswer) {
    if (PushdownTransportDescription(source, cricket::CA_PRANSWER)) {
      EnableChannels();
      SetState(source == cricket::CS_LOCAL ?
          STATE_SENTPRACCEPT : STATE_RECEIVEDPRACCEPT);
      ret = (error() == cricket::BaseSession::ERROR_NONE);
    }
  } else if (action == kAnswer) {
    // Remove channel and transport proxies, if MediaContentDescription is
    // rejected in local session description.
    RemoveUnusedChannelsAndTransports(desc);
    if (PushdownTransportDescription(source, cricket::CA_ANSWER)) {
      MaybeEnableMuxingSupport();
      EnableChannels();
      SetState(source == cricket::CS_LOCAL ?
          STATE_SENTACCEPT : STATE_RECEIVEDACCEPT);
      ret = (error() == cricket::BaseSession::ERROR_NONE);
    }
  }
  return ret;
}

WebRtcSession::Action WebRtcSession::GetAction(const std::string& type) {
  if (type == SessionDescriptionInterface::kOffer) {
    return WebRtcSession::kOffer;
  } else if (type == SessionDescriptionInterface::kPrAnswer) {
    return WebRtcSession::kPrAnswer;
  } else if (type == SessionDescriptionInterface::kAnswer) {
    return WebRtcSession::kAnswer;
  }
  ASSERT(!"unknown action type");
  return WebRtcSession::kOffer;
}

bool WebRtcSession::ProcessIceMessage(const IceCandidateInterface* candidate) {
  if (state() == STATE_INIT) {
     LOG(LS_ERROR) << "ProcessIceMessage: ICE candidates can't be added "
                   << "without any offer (local or remote) "
                   << "session description.";
     return false;
  }

  if (!candidate) {
    LOG(LS_ERROR) << "ProcessIceMessage: Candidate is NULL";
    return false;
  }

  if (!local_description() || !remote_description()) {
    LOG(LS_INFO) << "ProcessIceMessage: Remote description not set, "
                 << "save the candidate for later use.";
    saved_candidates_.push_back(
        new JsepIceCandidate(candidate->sdp_mid(), candidate->sdp_mline_index(),
                             candidate->candidate()));
    return true;
  }

  // Add this candidate to the remote session description.
  if (!remote_desc_->AddCandidate(candidate)) {
    LOG(LS_ERROR) << "ProcessIceMessage: Candidate cannot be used";
    return false;
  }

  return UseCandidatesInSessionDescription(remote_desc_.get());
}

bool WebRtcSession::GetTrackIdBySsrc(uint32 ssrc, std::string* name) {
  if (GetLocalTrackName(ssrc, name)) {
    if (GetRemoteTrackName(ssrc, name)) {
      LOG(LS_WARNING) << "SSRC " << ssrc
                      << " exists in both local and remote descriptions";
      return true;  // We return the remote track name.
    }
    return true;
  } else {
    return GetRemoteTrackName(ssrc, name);
  }
}

bool WebRtcSession::GetLocalTrackName(uint32 ssrc, std::string* name) {
  if (!BaseSession::local_description())
    return false;
  return GetNameBySsrc(BaseSession::local_description(), ssrc, name);
}

bool WebRtcSession::GetRemoteTrackName(uint32 ssrc, std::string* name) {
  if (!BaseSession::remote_description())
      return false;
  return GetNameBySsrc(BaseSession::remote_description(), ssrc, name);
}

std::string WebRtcSession::BadStateErrMsg(
    const std::string& type, State state) {
  std::ostringstream desc;
  desc << "Called with type in wrong state, "
       << "type: " << type << " state: " << GetStateString(state);
  return desc.str();
}

void WebRtcSession::SetAudioPlayout(const std::string& name, bool enable) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_) {
    LOG(LS_ERROR) << "SetAudioPlayout: No audio channel exists.";
    return;
  }
  uint32 ssrc = 0;
  if (!VERIFY(mediastream_signaling_->GetRemoteAudioTrackSsrc(name, &ssrc))) {
    LOG(LS_ERROR) << "Trying to enable/disable an unexisting audio SSRC.";
    return;
  }
  voice_channel_->SetOutputScaling(ssrc, enable ? 1 : 0, enable ? 1 : 0);
}

void WebRtcSession::SetAudioSend(const std::string& name, bool enable,
                                 const cricket::AudioOptions& options) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_) {
    LOG(LS_ERROR) << "SetAudioSend: No audio channel exists.";
    return;
  }
  uint32 ssrc = 0;
  if (!VERIFY(GetAudioSsrcByName(BaseSession::local_description(),
                                 name, &ssrc))) {
    LOG(LS_ERROR) << "SetAudioSend: SSRC does not exist.";
    return;
  }
  voice_channel_->MuteStream(ssrc, !enable);
  if (enable)
    voice_channel_->SetChannelOptions(options);
}

bool WebRtcSession::SetCaptureDevice(const std::string& name,
                                     cricket::VideoCapturer* camera) {
  ASSERT(signaling_thread()->IsCurrent());

  if (!video_channel_.get()) {
    // |video_channel_| doesnt't exist. Probably because the remote end doesnt't
    // support video.
    LOG(LS_WARNING) << "Video not used in this call.";
    return false;
  }
  uint32 ssrc = 0;
  if (!VERIFY(GetVideoSsrcByName(BaseSession::local_description(),
                                 name, &ssrc))) {
    LOG(LS_ERROR) << "Trying to set camera device on a unknown  SSRC.";
    return false;
  }

  if (!video_channel_->SetCapturer(ssrc, camera)) {
    LOG(LS_ERROR) << "Failed to set capture device.";
    return false;
  }
  return true;
}

void WebRtcSession::SetVideoPlayout(const std::string& name,
                                    bool enable,
                                    cricket::VideoRenderer* renderer) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!video_channel_) {
    LOG(LS_ERROR) << "SetVideoPlayout: No video channel exists.";
    return;
  }

  uint32 ssrc = 0;
  if (mediastream_signaling_->GetRemoteVideoTrackSsrc(name, &ssrc)) {
    video_channel_->SetRenderer(ssrc, enable ? renderer : NULL);
  } else {
    // Allow that |name| does not exist if renderer is null but assert
    // otherwise.
    VERIFY(renderer == NULL);
  }
}

void WebRtcSession::SetVideoSend(const std::string& name, bool enable,
                                 const cricket::VideoOptions* options) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!video_channel_) {
    LOG(LS_ERROR) << "SetVideoSend: No video channel exists.";
    return;
  }
  uint32 ssrc = 0;
  if (!VERIFY(GetVideoSsrcByName(BaseSession::local_description(),
                                 name, &ssrc))) {
    LOG(LS_ERROR) << "SetVideoSend: SSRC does not exist.";
    return;
  }
  video_channel_->MuteStream(ssrc, !enable);
  if (enable && options)
    video_channel_->SetChannelOptions(*options);
}

bool WebRtcSession::CanInsertDtmf(const std::string& track_id) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_) {
    LOG(LS_ERROR) << "CanInsertDtmf: No audio channel exists.";
    return false;
  }
  uint32 send_ssrc = 0;
  // The Dtmf is negotiated per channel not ssrc, so we only check if the ssrc
  // exists.
  if (!GetAudioSsrcByName(BaseSession::local_description(), track_id,
                          &send_ssrc)) {
    LOG(LS_ERROR) << "CanInsertDtmf: Track does not exist: " << track_id;
    return false;
  }
  return voice_channel_->CanInsertDtmf();
}

bool WebRtcSession::InsertDtmf(const std::string& track_id,
                               int code, int duration) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_) {
    LOG(LS_ERROR) << "InsertDtmf: No audio channel exists.";
    return false;
  }
  uint32 send_ssrc = 0;
  if (!VERIFY(GetAudioSsrcByName(BaseSession::local_description(),
                                 track_id, &send_ssrc))) {
    LOG(LS_ERROR) << "InsertDtmf: Track does not exist: " << track_id;
    return false;
  }
  if (!voice_channel_->InsertDtmf(send_ssrc, code, duration,
                                  cricket::DF_SEND)) {
    LOG(LS_ERROR) << "Failed to insert DTMF to channel.";
    return false;
  }
  return true;
}

talk_base::scoped_refptr<DataChannel> WebRtcSession::CreateDataChannel(
      const std::string& label,
      const DataChannelInit* config) {
  if (!allow_rtp_data_engine_) {
    LOG(LS_ERROR) << "CreateDataChannel: Data is not supported in this call.";
    return NULL;
  }
  talk_base::scoped_refptr<DataChannel> channel(
      DataChannel::Create(this, label, config));
  if (channel == NULL)
    return NULL;
  if (!mediastream_signaling_->AddDataChannel(channel))
    return NULL;
  return channel;
}

void WebRtcSession::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_CANDIDATE_TIMEOUT:
      LOG(LS_ERROR) << "Transport is not in writable state.";
      SignalError();
      break;
    default:
      cricket::BaseSession::OnMessage(msg);
      break;
  }
}

void WebRtcSession::SetIceConnectionState(
      PeerConnectionInterface::IceConnectionState state) {
  if (ice_connection_state_ == state) {
    return;
  }

  // ASSERT that the requested transition is allowed.  Note that
  // WebRtcSession does not implement "kIceConnectionClosed" (that is handled
  // within PeerConnection).  This switch statement should compile away when
  // ASSERTs are disabled.
  switch (ice_connection_state_) {
    case PeerConnectionInterface::kIceConnectionNew:
      ASSERT(state == PeerConnectionInterface::kIceConnectionChecking);
      break;
    case PeerConnectionInterface::kIceConnectionChecking:
      ASSERT(state == PeerConnectionInterface::kIceConnectionFailed ||
             state == PeerConnectionInterface::kIceConnectionConnected);
      break;
    case PeerConnectionInterface::kIceConnectionConnected:
      ASSERT(state == PeerConnectionInterface::kIceConnectionDisconnected ||
             state == PeerConnectionInterface::kIceConnectionChecking ||
             state == PeerConnectionInterface::kIceConnectionCompleted);
      break;
    case PeerConnectionInterface::kIceConnectionCompleted:
      ASSERT(state == PeerConnectionInterface::kIceConnectionConnected ||
             state == PeerConnectionInterface::kIceConnectionDisconnected);
      break;
    case PeerConnectionInterface::kIceConnectionFailed:
      ASSERT(state == PeerConnectionInterface::kIceConnectionNew);
      break;
    case PeerConnectionInterface::kIceConnectionDisconnected:
      ASSERT(state == PeerConnectionInterface::kIceConnectionChecking ||
             state == PeerConnectionInterface::kIceConnectionConnected ||
             state == PeerConnectionInterface::kIceConnectionCompleted ||
             state == PeerConnectionInterface::kIceConnectionFailed);
      break;
    case PeerConnectionInterface::kIceConnectionClosed:
      ASSERT(false);
      break;
    default:
      ASSERT(false);
      break;
  }

  ice_connection_state_ = state;
  if (ice_observer_) {
    ice_observer_->OnIceConnectionChange(ice_connection_state_);
  }
}

void WebRtcSession::OnTransportRequestSignaling(
    cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  transport->OnSignalingReady();
  if (ice_observer_) {
    ice_observer_->OnIceGatheringChange(
      PeerConnectionInterface::kIceGatheringGathering);
  }
}

void WebRtcSession::OnTransportConnecting(cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  // start monitoring for the write state of the transport.
  OnTransportWritable(transport);
}

void WebRtcSession::OnTransportWritable(cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  // TODO(bemasc): Expose more API from Transport to detect when
  // candidate selection starts or stops, due to success or failure.
  if (transport->all_channels_writable()) {
    if (ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionChecking ||
        ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionDisconnected) {
      SetIceConnectionState(PeerConnectionInterface::kIceConnectionConnected);
    }
  } else if (transport->HasChannels()) {
    // If the current state is Connected or Completed, then there were writable
    // channels but now there are not, so the next state must be Disconnected.
    if (ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionConnected ||
        ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionCompleted) {
      SetIceConnectionState(
          PeerConnectionInterface::kIceConnectionDisconnected);
    }
  }
  // If the transport is not in writable state, start a timer to monitor
  // the state. If the transport doesn't become writable state in 30 seconds
  // then we are assuming call can't be continued.
  signaling_thread()->Clear(this, MSG_CANDIDATE_TIMEOUT);
  if (transport->HasChannels() && !transport->writable()) {
    signaling_thread()->PostDelayed(
        kCallSetupTimeout, this, MSG_CANDIDATE_TIMEOUT);
  }
}

void WebRtcSession::OnTransportProxyCandidatesReady(
    cricket::TransportProxy* proxy, const cricket::Candidates& candidates) {
  ASSERT(signaling_thread()->IsCurrent());
  ProcessNewLocalCandidate(proxy->content_name(), candidates);
}

bool WebRtcSession::ExpectSetLocalDescription(Action action) {
  return ((action == kOffer && state() == STATE_INIT) ||
          // update local offer
          (action == kOffer && state() == STATE_SENTINITIATE) ||
          // update the current ongoing session.
          (action == kOffer && state() == STATE_RECEIVEDACCEPT) ||
          (action == kOffer && state() == STATE_SENTACCEPT) ||
          (action == kOffer && state() == STATE_INPROGRESS) ||
          // accept remote offer
          (action == kAnswer && state() == STATE_RECEIVEDINITIATE) ||
          (action == kAnswer && state() == STATE_SENTPRACCEPT) ||
          (action == kPrAnswer && state() == STATE_RECEIVEDINITIATE) ||
          (action == kPrAnswer && state() == STATE_SENTPRACCEPT));
}

bool WebRtcSession::ExpectSetRemoteDescription(Action action) {
  return ((action == kOffer && state() == STATE_INIT) ||
          // update remote offer
          (action == kOffer && state() == STATE_RECEIVEDINITIATE) ||
          // update the current ongoing session
          (action == kOffer && state() == STATE_RECEIVEDACCEPT) ||
          (action == kOffer && state() == STATE_SENTACCEPT) ||
          (action == kOffer && state() == STATE_INPROGRESS) ||
          // accept local offer
          (action == kAnswer && state() == STATE_SENTINITIATE) ||
          (action == kAnswer && state() == STATE_RECEIVEDPRACCEPT) ||
          (action == kPrAnswer && state() == STATE_SENTINITIATE) ||
          (action == kPrAnswer && state() == STATE_RECEIVEDPRACCEPT));
}

void WebRtcSession::OnCandidatesAllocationDone() {
  ASSERT(signaling_thread()->IsCurrent());
  if (ice_observer_) {
    ice_observer_->OnIceGatheringChange(
      PeerConnectionInterface::kIceGatheringComplete);
    ice_observer_->OnIceComplete();
  }
}

// Enabling voice and video channel.
void WebRtcSession::EnableChannels() {
  if (voice_channel_ && !voice_channel_->enabled())
    voice_channel_->Enable(true);

  if (video_channel_ && !video_channel_->enabled())
    video_channel_->Enable(true);

  if (data_channel_.get() && !data_channel_->enabled())
    data_channel_->Enable(true);
}

void WebRtcSession::ProcessNewLocalCandidate(
    const std::string& content_name,
    const cricket::Candidates& candidates) {
  int sdp_mline_index;
  if (!GetLocalCandidateMediaIndex(content_name, &sdp_mline_index)) {
    LOG(LS_ERROR) << "ProcessNewLocalCandidate: content name "
                  << content_name << " not found";
    return;
  }

  for (cricket::Candidates::const_iterator citer = candidates.begin();
      citer != candidates.end(); ++citer) {
    // Use content_name as the candidate media id.
    JsepIceCandidate candidate(content_name, sdp_mline_index, *citer);
    if (ice_observer_) {
      ice_observer_->OnIceCandidate(&candidate);
    }
    if (local_desc_) {
      local_desc_->AddCandidate(&candidate);
    }
  }
}

// Returns the media index for a local ice candidate given the content name.
bool WebRtcSession::GetLocalCandidateMediaIndex(const std::string& content_name,
                                                int* sdp_mline_index) {
  if (!BaseSession::local_description() || !sdp_mline_index)
    return false;

  bool content_found = false;
  const ContentInfos& contents = BaseSession::local_description()->contents();
  for (size_t index = 0; index < contents.size(); ++index) {
    if (contents[index].name == content_name) {
      *sdp_mline_index = index;
      content_found = true;
      break;
    }
  }
  return content_found;
}

bool WebRtcSession::UseCandidatesInSessionDescription(
    const SessionDescriptionInterface* remote_desc) {
  if (!remote_desc)
    return true;
  bool ret = true;
  for (size_t m = 0; m < remote_desc->number_of_mediasections(); ++m) {
    const IceCandidateCollection* candidates = remote_desc->candidates(m);
    for  (size_t n = 0; n < candidates->count(); ++n) {
      ret = UseCandidate(candidates->at(n));
      if (!ret)
        break;
    }
  }
  return ret;
}

bool WebRtcSession::UseCandidate(
    const IceCandidateInterface* candidate) {

  size_t mediacontent_index = static_cast<size_t>(candidate->sdp_mline_index());
  size_t remote_content_size =
      BaseSession::remote_description()->contents().size();
  if (mediacontent_index >= remote_content_size) {
    LOG(LS_ERROR)
        << "UseRemoteCandidateInSession: Invalid candidate media index.";
    return false;
  }

  cricket::ContentInfo content =
      BaseSession::remote_description()->contents()[mediacontent_index];
  std::vector<cricket::Candidate> candidates;
  candidates.push_back(candidate->candidate());
  // Invoking BaseSession method to handle remote candidates.
  std::string error;
  if (OnRemoteCandidates(content.name, candidates, &error)) {
    // Candidates successfully submitted for checking.
    if (ice_connection_state_ == PeerConnectionInterface::kIceConnectionNew ||
        ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionDisconnected) {
      // If state is New, then the session has just gotten its first remote ICE
      // candidates, so go to Checking.
      // If state is Disconnected, the session is re-using old candidates or
      // receiving additional ones, so go to Checking.
      // If state is Connected, stay Connected.
      // TODO(bemasc): If state is Connected, and the new candidates are for a
      // newly added transport, then the state actually _should_ move to
      // checking.  Add a way to distinguish that case.
      SetIceConnectionState(PeerConnectionInterface::kIceConnectionChecking);
    }
    // TODO(bemasc): If state is Completed, go back to Connected.
  } else {
    LOG(LS_WARNING) << error;
  }
  return true;
}

void WebRtcSession::RemoveUnusedChannelsAndTransports(
    const SessionDescription* desc) {
  if (!desc) {
    return;
  }

  const cricket::ContentInfo* voice_info =
      cricket::GetFirstAudioContent(desc);
  if ((!voice_info || voice_info->rejected) && voice_channel_) {
    const std::string content_name = voice_channel_->content_name();
    voice_channel_->SignalFirstPacketReceived.disconnect(this);
    channel_manager_->DestroyVoiceChannel(voice_channel_.release());
    DestroyTransportProxy(content_name);
  }

  const cricket::ContentInfo* video_info =
      cricket::GetFirstVideoContent(desc);
  if ((!video_info || video_info->rejected) && video_channel_) {
    const std::string content_name = video_channel_->content_name();
    video_channel_->SignalFirstPacketReceived.disconnect(this);
    channel_manager_->DestroyVideoChannel(video_channel_.release());
    DestroyTransportProxy(content_name);
  }

  const cricket::ContentInfo* data_info =
      cricket::GetFirstDataContent(desc);
  if ((!data_info || data_info->rejected) && data_channel_.get()) {
    const std::string content_name = data_channel_->content_name();
    channel_manager_->DestroyDataChannel(data_channel_.release());
    DestroyTransportProxy(content_name);
  }
}

bool WebRtcSession::CreateChannels(Action action,
                                   const SessionDescription* desc) {
  // Disabling the BUNDLE flag in PortAllocator if offer disabled it.
  if (state() == STATE_INIT && action == kOffer &&
      !desc->HasGroup(cricket::GROUP_TYPE_BUNDLE)) {
    port_allocator()->set_flags(port_allocator()->flags() &
                                ~cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  }

  // Creating the media channels and transport proxies.
  const cricket::ContentInfo* voice = cricket::GetFirstAudioContent(desc);
  if (voice && !voice->rejected && !voice_channel_) {
    if (!CreateVoiceChannel(desc)) {
      LOG(LS_ERROR) << "Failed to create voice channel.";
      return false;
    }
  }

  const cricket::ContentInfo* video = cricket::GetFirstVideoContent(desc);
  if (video && !video->rejected && !video_channel_) {
    if (!CreateVideoChannel(desc)) {
      LOG(LS_ERROR) << "Failed to create video channel.";
      return false;
    }
  }

  const cricket::ContentInfo* data = cricket::GetFirstDataContent(desc);
  if (allow_rtp_data_engine_ && data && !data->rejected &&
      !data_channel_.get()) {
    if (!CreateDataChannel(desc)) {
      LOG(LS_ERROR) << "Failed to create data channel.";
      return false;
    }
  }

  return true;
}

bool WebRtcSession::CreateVoiceChannel(const SessionDescription* desc) {
  const cricket::ContentInfo* voice = cricket::GetFirstAudioContent(desc);
  voice_channel_.reset(channel_manager_->CreateVoiceChannel(
      this, voice->name, true));
  return voice_channel_ ? true : false;
}

bool WebRtcSession::CreateVideoChannel(const SessionDescription* desc) {
  const cricket::ContentInfo* video = cricket::GetFirstVideoContent(desc);
  video_channel_.reset(channel_manager_->CreateVideoChannel(
      this, video->name, true, voice_channel_.get()));
  return video_channel_ ? true : false;
}

bool WebRtcSession::CreateDataChannel(const SessionDescription* desc) {
  const cricket::ContentInfo* data = cricket::GetFirstDataContent(desc);
  data_channel_.reset(channel_manager_->CreateDataChannel(
      this, data->name, true));
  if (!data_channel_.get()) {
    return false;
  }
  return true;
}

void WebRtcSession::CopySavedCandidates(
    SessionDescriptionInterface* dest_desc) {
  if (!dest_desc) {
    ASSERT(false);
    return;
  }
  for (size_t i = 0; i < saved_candidates_.size(); ++i) {
    dest_desc->AddCandidate(saved_candidates_[i]);
    delete saved_candidates_[i];
  }
  saved_candidates_.clear();
}

void WebRtcSession::UpdateSessionDescriptionSecurePolicy(
    SessionDescription* sdesc) {
  if (!sdesc) {
    return;
  }

  // Updating the |crypto_required_| in MediaContentDescription to the
  // appropriate state based on the current security policy.
  for (cricket::ContentInfos::iterator iter = sdesc->contents().begin();
       iter != sdesc->contents().end(); ++iter) {
    if (cricket::IsMediaContent(&*iter)) {
      MediaContentDescription* mdesc =
          static_cast<MediaContentDescription*> (iter->description);
      if (mdesc) {
        mdesc->set_crypto_required(
            session_desc_factory_.secure() == cricket::SEC_REQUIRED);
      }
    }
  }
}

}  // namespace webrtc
