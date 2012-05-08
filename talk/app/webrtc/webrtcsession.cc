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

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnection.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/stringencode.h"
#include "talk/session/phone/channel.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/videocapturer.h"

using cricket::MediaContentDescription;

namespace webrtc {

enum {
  MSG_CANDIDATE_TIMEOUT = 101,
};

// We allow 30 seconds to establish a connection, otherwise it's an error.
static const int kCallSetupTimeout = 30 * 1000;

// Constants for setting the default encoder size.
// TODO: Implement proper negotiation of video resolution.
static const int kDefaultVideoCodecId = 100;
static const int kDefaultVideoCodecFramerate = 30;
static const char kDefaultVideoCodecName[] = "VP8";
static const int kDefaultVideoCodecWidth = 640;
static const int kDefaultVideoCodecHeight = 480;

static void CopyCandidatesFromSessionDescription(
    const SessionDescriptionInterface* source_desc,
    SessionDescriptionInterface* dest_desc) {
  if (!source_desc)
    return;
  for (size_t m = 0; m < source_desc->number_of_mediasections(); ++m) {
    const IceCandidateColletion* source_candidates = source_desc->candidates(m);
    const IceCandidateColletion* desc_candidates = dest_desc->candidates(m);
    for  (size_t n = 0; n < source_candidates->count(); ++n) {
      const IceCandidateInterface* new_candidate = source_candidates->at(n);
      if (!desc_candidates->HasCandidate(new_candidate))
        dest_desc->AddCandidate(source_candidates->at(n));
    }
  }
}

static bool HasCrypto(const cricket::SessionDescription* desc) {
  if (!desc) {
    return false;
  }
  const cricket::ContentInfos& contents = desc->contents();
  for (size_t index = 0; index < contents.size(); ++index) {
    const MediaContentDescription* media =
        static_cast<const MediaContentDescription*>(
            contents[index].description);
    if (media && media->cryptos().empty()) {
      return false;
    }
  }
  return true;
}

WebRtcSession::WebRtcSession(cricket::ChannelManager* channel_manager,
                             talk_base::Thread* signaling_thread,
                             talk_base::Thread* worker_thread,
                             cricket::PortAllocator* port_allocator,
                             MediaStreamSignaling* mediastream_signaling)
    : cricket::BaseSession(signaling_thread, worker_thread, port_allocator,
                           talk_base::ToString(talk_base::CreateRandomId()),
                           cricket::NS_JINGLE_RTP, true),
      channel_manager_(channel_manager),
      session_desc_factory_(channel_manager),
      ice_started_(false),
      mediastream_signaling_(mediastream_signaling),
      ice_observer_(NULL),
      // RFC 4566 suggested a Network Time Protocol (NTP) format timestamp
      // as the session id and session version. To simplify, it should be fine
      // to just use a random number as session id and start version from 0.
      session_id_(talk_base::ToString(talk_base::CreateRandomId())),
      session_version_(0) {
}

WebRtcSession::~WebRtcSession() {
  if (voice_channel_.get()) {
    channel_manager_->DestroyVoiceChannel(voice_channel_.release());
  }
  if (video_channel_.get()) {
    channel_manager_->DestroyVideoChannel(video_channel_.release());
  }
}

bool WebRtcSession::Initialize() {
  // By default SRTP-SDES is enabled in WebRtc.
  set_secure_policy(cricket::SEC_REQUIRED);
  // Make sure SessionDescriptions only contains the StreamParams we negotiate.
  session_desc_factory_.set_add_legacy_streams(false);

  const cricket::VideoCodec default_codec(kDefaultVideoCodecId,
      kDefaultVideoCodecName, kDefaultVideoCodecWidth, kDefaultVideoCodecHeight,
      kDefaultVideoCodecFramerate, 0);
  channel_manager_->SetDefaultVideoEncoderConfig(
      cricket::VideoEncoderConfig(default_codec));

  return CreateChannels();
}

bool WebRtcSession::StartIce(IceOptions /*options*/) {
  if (!local_description()) {
    LOG(LS_ERROR) << "StartIce called before SetLocalDescription";
    return false;
  }

  // TODO: Take IceOptions into consideration and restart of the
  // ice agent.
  if (ice_started_) {
    return true;
  }

  // Try connecting all transport channels. This is necessary to generate
  // ICE candidates.
  SpeculativelyConnectAllTransportChannels();

  ice_started_ = true;

  // If SDP is already negotiated, then it's the time to try enabling the
  // BUNDLE option if both agents support the feature.
  if (ReadyToEnableBundle() && !transport_muxed()) {
    MaybeEnableMuxingSupport();
  }

  if (!UseCandidatesInSessionDescription(remote_desc_.get())) {
    LOG(LS_WARNING) << "StartIce: Can't use candidates in remote session"
                    << " description";
  }
  return true;
}

void WebRtcSession::set_secure_policy(
    cricket::SecureMediaPolicy secure_policy) {
  session_desc_factory_.set_secure(secure_policy);
}

SessionDescriptionInterface* WebRtcSession::CreateOffer(
    const MediaHints& hints) {
  cricket::MediaSessionOptions options =
      mediastream_signaling_->GetMediaSessionOptions(hints);
  cricket::SessionDescription* desc(
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
  JsepSessionDescription* offer = new JsepSessionDescription();
  if (!offer->Initialize(desc, session_id_,
                         talk_base::ToString(++session_version_))) {
    delete offer;
    return NULL;
  }
  if (local_description())
    CopyCandidatesFromSessionDescription(local_description(), offer);
  return offer;
}

SessionDescriptionInterface* WebRtcSession::CreateAnswer(
    const MediaHints& hints,
    const SessionDescriptionInterface* offer) {
  cricket::MediaSessionOptions options =
      mediastream_signaling_->GetMediaSessionOptions(hints);
  cricket::SessionDescription* desc(
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
  JsepSessionDescription* answer = new JsepSessionDescription();
  if (!answer->Initialize(desc, session_id_,
                          talk_base::ToString(++session_version_))) {
    delete answer;
    return NULL;
  }
  if (local_description())
    CopyCandidatesFromSessionDescription(local_description(), answer);
  return answer;
}

bool WebRtcSession::SetLocalDescription(Action action,
                                        SessionDescriptionInterface* desc) {
  if (!ExpectSetLocalDescription(action)) {
    LOG(LS_ERROR) << "SetLocalDescription called with action in wrong state, "
                  << "action: " << action << " state: " << state();
    return false;
  }
  if (!desc || !desc->description()) {
    LOG(LS_ERROR) << "SetLocalDescription called with an invalid session"
                  <<" description";
    return false;
  }
  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !HasCrypto(desc->description())) {
    LOG(LS_ERROR) << "SetLocalDescription called with a session"
                  <<" description without crypto enabled";
    return false;
  }

  if (!desc->description()->HasGroup(cricket::GROUP_TYPE_BUNDLE)) {
    // Disabling the BUNDLE flag in PortAllocator.
    // TODO - Implement to enable BUNDLE feature,
    // if SetLocalDescription is called again with BUNDLE info in it.
    port_allocator()->set_flags(port_allocator()->flags() &
                                ~cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  }

  set_local_description(desc->description()->Copy());
  local_desc_.reset(desc);

  switch (action) {
    case kOffer:
      SetState(STATE_SENTINITIATE);
      break;
    case kAnswer:
      EnableChannels();

    if (ReadyToEnableBundle() && !transport_muxed()) {
      MaybeEnableMuxingSupport();
    }

      SetState(STATE_SENTACCEPT);
      break;
    case kPrAnswer:
      EnableChannels();
      SetState(STATE_SENTPRACCEPT);
      break;
  }
  return error() == cricket::BaseSession::ERROR_NONE;
}

bool WebRtcSession::SetRemoteDescription(Action action,
                                         SessionDescriptionInterface* desc) {
  if (!ExpectSetRemoteDescription(action)) {
    LOG(LS_ERROR) << "SetRemoteDescription called with action in wrong state, "
                  << "action: " << action << " state: " << state();
    return false;
  }
  if (!desc || !desc->description()) {
    LOG(LS_ERROR) << "SetRemoteDescription called with an invalid session"
                  <<" description";
    return false;
  }
  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !HasCrypto(desc->description())) {
    LOG(LS_ERROR) << "SetRemoteDescription called with a session"
                  <<" description without crypto enabled";
    return false;
  }

  set_remote_description(desc->description()->Copy());
  switch (action) {
    case kOffer:
      SetState(STATE_RECEIVEDINITIATE);
      break;
    case kAnswer:
      EnableChannels();

    if (ReadyToEnableBundle() && !transport_muxed()) {
      MaybeEnableMuxingSupport();
    }

      SetState(STATE_RECEIVEDACCEPT);
      break;
    case kPrAnswer:
      EnableChannels();
      SetState(STATE_RECEIVEDPRACCEPT);
      break;
  }

  // Update remote MediaStreams.
  mediastream_signaling_->UpdateRemoteStreams(desc);

  // Use all candidates in this new session description if ice is started.
  if (ice_started_ && !UseCandidatesInSessionDescription(desc)) {
    LOG(LS_ERROR) << "SetRemoteDescription: Argument |desc| contains "
                  << "invalid candidates";
    return false;
  }
  // We retain all received candidates.
  CopyCandidatesFromSessionDescription(remote_desc_.get(), desc);
  remote_desc_.reset(desc);
  return error() == cricket::BaseSession::ERROR_NONE;
}

bool WebRtcSession::ProcessIceMessage(const IceCandidateInterface* candidate) {
  if (!remote_description()) {
    LOG(LS_ERROR) << "Remote description not set";
    return false;
  }

  if (!candidate) {
    LOG(LS_ERROR) << "ProcessIceMessage: Candidate is NULL";
    return false;
  }

  // Add this candidate to the remote session description.
  if (!remote_desc_->AddCandidate(candidate)) {
    LOG(LS_ERROR) << "ProcessIceMessage: Candidate cannot be used";
    return false;
  }

  if (ice_started_) {  // Use this candidate now if we have started ice.
    return UseCandidate(candidate);
  }
  return true;
}

void WebRtcSession::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_CANDIDATE_TIMEOUT:
      LOG(LS_ERROR) << "Transport is not in writable state.";
      SignalError();
      break;
    default:
      break;
  }
}

bool WebRtcSession::SetCaptureDevice(const std::string& name,
                                     cricket::VideoCapturer* camera) {
  // should be called from a signaling thread
  ASSERT(signaling_thread()->IsCurrent());

  // TODO: Refactor this when there is support for multiple cameras.
  if (!channel_manager_->SetVideoCapturer(camera)) {
    LOG(LS_ERROR) << "Failed to set capture device.";
    return false;
  }

  const bool start_capture = (camera != NULL);
  cricket::CaptureResult ret = channel_manager_->SetVideoCapture(start_capture);
  if (ret != cricket::CR_SUCCESS && ret != cricket::CR_PENDING) {
    LOG(LS_ERROR) << "Failed to start the capture device.";
    return false;
  }

  return true;
}

void WebRtcSession::SetLocalRenderer(const std::string& name,
                                     cricket::VideoRenderer* renderer) {
  ASSERT(signaling_thread()->IsCurrent());
  // TODO: Fix SetLocalRenderer.
  // video_channel_->SetLocalRenderer(0, renderer);
}

void WebRtcSession::SetRemoteRenderer(const std::string& name,
                                      cricket::VideoRenderer* renderer) {
  ASSERT(signaling_thread()->IsCurrent());

  const cricket::ContentInfo* video_info =
      cricket::GetFirstVideoContent(BaseSession::remote_description());
  if (!video_info) {
    LOG(LS_ERROR) << "Video not received in this call";
  }

  const cricket::MediaContentDescription* video_content =
      static_cast<const cricket::MediaContentDescription*>(
          video_info->description);
  cricket::StreamParams stream;
  if (cricket::GetStreamByNickAndName(video_content->streams(), "", name,
                                      &stream)) {
    video_channel_->SetRenderer(stream.first_ssrc(), renderer);
  } else {
    // Allow that |stream| does not exist if renderer is null but assert
    // otherwise.
    VERIFY(renderer == NULL);
  }
}

void WebRtcSession::OnTransportRequestSignaling(
    cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  transport->OnSignalingReady();
}

void WebRtcSession::OnTransportConnecting(cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  // start monitoring for the write state of the transport.
  OnTransportWritable(transport);
}

void WebRtcSession::OnTransportWritable(cricket::Transport* transport) {
  ASSERT(signaling_thread()->IsCurrent());
  // If the transport is not in writable state, start a timer to monitor
  // the state. If the transport doesn't become writable state in 30 seconds
  // then we are assuming call can't be continued.
  signaling_thread()->Clear(this, MSG_CANDIDATE_TIMEOUT);
  if (transport->HasChannels() && !transport->writable()) {
    signaling_thread()->PostDelayed(
        kCallSetupTimeout, this, MSG_CANDIDATE_TIMEOUT);
  }
}

void WebRtcSession::OnTransportCandidatesReady(
    cricket::Transport* transport, const cricket::Candidates& candidates) {
  ASSERT(signaling_thread()->IsCurrent());

  cricket::TransportProxy* proxy = GetTransportProxy(transport);
  if (!VERIFY(proxy != NULL)) {
    LOG(LS_ERROR) << "No Proxy found";
    return;
  }
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
    ice_observer_->OnIceComplete();
  }
}

bool WebRtcSession::CreateChannels() {
  voice_channel_.reset(channel_manager_->CreateVoiceChannel(
      this, cricket::CN_AUDIO, true));
  if (!voice_channel_.get()) {
    LOG(LS_ERROR) << "Failed to create voice channel";
    return false;
  }

  video_channel_.reset(channel_manager_->CreateVideoChannel(
      this, cricket::CN_VIDEO, true, voice_channel_.get()));
  if (!video_channel_.get()) {
    LOG(LS_ERROR) << "Failed to create video channel";
    return false;
  }

  // TransportProxies and TransportChannels will be created when
  // CreateVoiceChannel and CreateVideoChannel are called.
  return true;
}

// Enabling voice and video channel.
void WebRtcSession::EnableChannels() {
  if (!voice_channel_->enabled())
    voice_channel_->Enable(true);

  if (!video_channel_->enabled())
    video_channel_->Enable(true);
}

void WebRtcSession::ProcessNewLocalCandidate(
    const std::string& content_name,
    const cricket::Candidates& candidates) {
  std::string candidate_label;

  if (!GetLocalCandidateLabel(content_name, &candidate_label)) {
    LOG(LS_ERROR) << "ProcessNewLocalCandidate: content name "
                  << content_name << " not found";
    return;
  }

  for (cricket::Candidates::const_iterator citer = candidates.begin();
      citer != candidates.end(); ++citer) {
    JsepIceCandidate candidate(candidate_label, *citer);
    if (ice_observer_) {
      ice_observer_->OnIceCandidate(&candidate);
    }
    if (local_desc_.get()) {
      local_desc_->AddCandidate(&candidate);
    }
  }
}

// Returns a label for a local ice candidate given the content name.
bool WebRtcSession::GetLocalCandidateLabel(const std::string& content_name,
                                           std::string* label) {
  if (!BaseSession::local_description() || !label)
    return false;

  bool content_found = false;
  const cricket::ContentInfos& contents =
      BaseSession::local_description()->contents();
  for (size_t index = 0; index < contents.size(); ++index) {
    if (contents[index].name == content_name) {
      *label = talk_base::ToString(index);
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
    const IceCandidateColletion* candidates = remote_desc->candidates(m);
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

  size_t mediacontent_index;
  size_t remote_content_size =
      BaseSession::remote_description()->contents().size();
  if ((!talk_base::FromString<size_t>(candidate->label(),
                                      &mediacontent_index)) ||
      (mediacontent_index >= remote_content_size)) {
    LOG(LS_ERROR) << "UseRemoteCandidateInSession: Invalid candidate label";
    return false;
  }

  cricket::ContentInfo content =
      BaseSession::remote_description()->contents()[mediacontent_index];

  std::string local_content_name;
  if (cricket::IsAudioContent(&content)) {
    local_content_name = cricket::CN_AUDIO;
  } else if (cricket::IsVideoContent(&content)) {
    local_content_name = cricket::CN_VIDEO;
  }

  // TODO: Justins comment:This is bad encapsulation, suggest we add a
  // helper to BaseSession to allow us to
  // pass in candidates without touching the transport proxies.
  cricket::TransportProxy* proxy = GetTransportProxy(local_content_name);
  if (!proxy) {
    LOG(LS_ERROR) << "No TransportProxy exists with name "
                  << local_content_name;
    return false;
  }
  // CompleteNegotiation will set actual impl's in Proxy.
  if (!proxy->negotiated())
    proxy->CompleteNegotiation();

  // TODO - Add a interface to TransportProxy to accept
  // a remote candidate.
  std::vector<cricket::Candidate> candidates;
  candidates.push_back(candidate->candidate());
  proxy->impl()->OnRemoteCandidates(candidates);
  return true;
}

bool WebRtcSession::ReadyToEnableBundle() const {
  return ((BaseSession::local_description() != NULL)  &&
          (BaseSession::remote_description() != NULL) &&
          ice_started_);
}

}  // namespace webrtc
