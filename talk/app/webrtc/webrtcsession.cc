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

using cricket::MediaContentDescription;

typedef cricket::MediaSessionOptions::Stream Stream;
typedef cricket::MediaSessionOptions::Streams Streams;

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
    const IceCandidateCollection* source_candidates =
        source_desc->candidates(m);
    const IceCandidateCollection* desc_candidates = dest_desc->candidates(m);
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

static bool GetAudioSsrcByName(
    const cricket::SessionDescription* session_description,
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

static bool GetVideoSsrcByName(
    const cricket::SessionDescription* session_description,
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
      allocation_complete_(false),
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

  if (!CreateDefaultLocalDescription() ||
      !CreateChannels(BaseSession::local_description())) {
    return false;
  }

  return StartCandidatesAllocation();
}

bool WebRtcSession::StartCandidatesAllocation() {
  // Flushing any unsent candidates from the transports.
  for (cricket::TransportMap::const_iterator iter = transport_proxies().begin();
       iter != transport_proxies().end(); ++iter) {
    iter->second->ClearUnsentCandidates();
  }

  // SpeculativelyConnectTransportChannels, will call ConnectChannels method
  // from TransportProxy to start gathering ice candidates.
  SpeculativelyConnectAllTransportChannels();
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
  if (!ValidStreams(options.streams)) {
    LOG(LS_ERROR) << "CreateOffer called with invalid media streams.";
    return NULL;
  }
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
  JsepSessionDescription* offer(new JsepSessionDescription(
      JsepSessionDescription::kOffer));
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
  if (!ValidStreams(options.streams)) {
    LOG(LS_ERROR) << "CreateAnswer called with invalid media streams.";
    return NULL;
  }
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
  JsepSessionDescription* answer(new JsepSessionDescription(
      JsepSessionDescription::kAnswer));
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
    delete desc;
    return false;
  }
  if (!desc || !desc->description()) {
    LOG(LS_ERROR) << "SetLocalDescription called with an invalid session"
                  <<" description";
    delete desc;
    return false;
  }
  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !HasCrypto(desc->description())) {
    LOG(LS_ERROR) << "SetLocalDescription called with a session"
                  <<" description without crypto enabled";
    delete desc;
    return false;
  }

  if (state() == STATE_INIT &&
      !desc->description()->HasGroup(cricket::GROUP_TYPE_BUNDLE)) {
    // Our default transport channels are created with BUNDLE flag on.
    // So easiest way to handle this scenario is to delete all existing
    // transport proxies and channels and recreate.
    // DisableBundleAndCreateChannels also disables this BUNDLE in
    // PortAllocator.
    CreateChannels(desc->description());
  }

  // Remove channel and transport proxies, if MediaContentDescription is
  // not present in local session description.
  RemoveUnusedChannelsAndTransports(desc->description());
  // Updating the content names for channels, if it differs from default.
  if (!MaybeUpdateChannelsAndTransports(desc->description())) {
    LOG(LS_ERROR) << "Failed to update content names from the description.";
    return false;
  }

  set_local_description(desc->description()->Copy());
  local_desc_.reset(desc);

  if (state() == STATE_INIT) {
    // Creating a dummy remote session description for parking remote candidates
    // if happen to arrive before remote session description.
    remote_desc_.reset(CreateAnswer(MediaHints(), local_desc_.get()));
    set_remote_description(remote_desc_->description()->Copy());
  }

  switch (action) {
    case kOffer:
      SetState(STATE_SENTINITIATE);
      break;
    case kAnswer:
      if (!transport_muxed()) {
        MaybeEnableMuxingSupport();
      }
      EnableChannels();
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
    delete desc;
    return false;
  }
  if (!desc || !desc->description()) {
    LOG(LS_ERROR) << "SetRemoteDescription called with an invalid session"
                  <<" description";
    delete desc;
    return false;
  }
  if (session_desc_factory_.secure() == cricket::SEC_REQUIRED &&
      !HasCrypto(desc->description())) {
    LOG(LS_ERROR) << "SetRemoteDescription called with a session"
                  <<" description without crypto enabled";
    delete desc;
    return false;
  }

  // Remove channel and transport proxies, if MediaContentDescription is
  // not present in remote session description.
  RemoveUnusedChannelsAndTransports(desc->description());
  // Updating the content names for channels, if it differs from default.
  if (!MaybeUpdateChannelsAndTransports(desc->description())) {
    LOG(LS_ERROR) << "Failed to update content names from the description.";
    return false;
  }

  set_remote_description(desc->description()->Copy());
  switch (action) {
    case kOffer:
      SetState(STATE_RECEIVEDINITIATE);
      break;
    case kAnswer:
      if (!transport_muxed()) {
        MaybeEnableMuxingSupport();
      }
      EnableChannels();
      SetState(STATE_RECEIVEDACCEPT);
      break;
    case kPrAnswer:
      EnableChannels();
      SetState(STATE_RECEIVEDPRACCEPT);
      break;
  }

  // Update remote MediaStreams.
  mediastream_signaling_->UpdateRemoteStreams(desc);
  // Use all candidates in this new session description.
  if (!UseCandidatesInSessionDescription(desc)) {
    LOG(LS_ERROR) << "SetRemoteDescription: Argument |desc| contains "
                  << "invalid candidates";
    delete desc;
    return false;
  }

  // We retain all received candidates.
  CopyCandidatesFromSessionDescription(remote_desc_.get(), desc);
  remote_desc_.reset(desc);
  return error() == cricket::BaseSession::ERROR_NONE;
}

bool WebRtcSession::ProcessIceMessage(const IceCandidateInterface* candidate) {
  if (state() == STATE_INIT) {
    LOG(LS_INFO) << "ProcessIceMessage: ICE candidates can't be added "
                 << "without any offer (local or remote) session description.";
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

  return UseCandidatesInSessionDescription(remote_desc_.get());
}

void WebRtcSession::SetAudioPlayout(const std::string& name, bool enable) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_.get()) {
    LOG(LS_ERROR) << "SetAudioPlayout: No audio channel exists.";
    return;
  }
  uint32 ssrc = 0;
  if (!VERIFY(GetAudioSsrcByName(BaseSession::remote_description(),
                                 name, &ssrc))) {
    LOG(LS_ERROR) << "Trying to enable/disable an unexisting audio SSRC.";
    return;
  }
  voice_channel_->SetOutputScaling(ssrc, enable ? 1 : 0, enable ? 1 : 0);
}

void WebRtcSession::SetAudioSend(const std::string& name, bool enable) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!voice_channel_.get()) {
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
}

bool WebRtcSession::SetCaptureDevice(const std::string& name,
                                     cricket::VideoCapturer* camera) {
  ASSERT(signaling_thread()->IsCurrent());
  uint32 ssrc = 0;
  if (!VERIFY(GetVideoSsrcByName(BaseSession::local_description(),
                                 name, &ssrc) || camera == NULL)) {
    LOG(LS_ERROR) << "Trying to set camera device on a unknown  SSRC.";
    return false;
  }

  // TODO: Refactor this when there is support for multiple cameras.
  if (!channel_manager_->SetVideoCapturer(camera)) {
    LOG(LS_ERROR) << "Failed to set capture device.";
    return false;
  }

  const bool start_capture = (camera != NULL);
  bool ret = channel_manager_->SetVideoCapture(start_capture);
  if (!ret) {
    LOG(LS_ERROR) << "Failed to start the capture device.";
    return false;
  }

  return true;
}

void WebRtcSession::SetVideoPlayout(const std::string& name,
                                    bool enable,
                                    cricket::VideoRenderer* renderer) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!video_channel_.get()) {
    LOG(LS_ERROR) << "SetVideoPlayout: No video channel exists.";
    return;
  }

  uint32 ssrc = 0;
  if (GetVideoSsrcByName(BaseSession::remote_description(), name, &ssrc)) {
    video_channel_->SetRenderer(ssrc, enable ? renderer : NULL);
  } else {
    // Allow that |name| does not exist if renderer is null but assert
    // otherwise.
    VERIFY(renderer == NULL);
  }
}

void WebRtcSession::SetVideoSend(const std::string& name, bool enable) {
  ASSERT(signaling_thread()->IsCurrent());
  if (!video_channel_.get()) {
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
}

void WebRtcSession::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_CANDIDATE_TIMEOUT:
      LOG(LS_ERROR) << "Transport is not in writable state.";
      SignalError();
      break;
    case cricket::BaseSession::MSG_STATE:
      MaybeSendAllUnsentCandidates();
      BaseSession::OnMessage(msg);
      break;
    default:
      break;
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

void WebRtcSession::OnTransportProxyCandidatesReady(
    cricket::TransportProxy* proxy, const cricket::Candidates& candidates) {
  ASSERT(signaling_thread()->IsCurrent());
  // If local description is not set yet, push these allocated candidates
  // back to the transport proxy, in unsent vector. These will be removed
  // once initiator of the session is ready to accept.
  if (!local_desc_.get()) {
    proxy->AddUnsentCandidates(candidates);
  } else {
    ProcessNewLocalCandidate(proxy->content_name(), candidates);
  }
}

void WebRtcSession::MaybeSendAllUnsentCandidates() {
  if (state() != STATE_SENTINITIATE &&
      state() != STATE_SENTACCEPT &&
      state() != STATE_SENTPRACCEPT) {
    return;
  }

  for (cricket::TransportMap::const_iterator iter = transport_proxies().begin();
       iter != transport_proxies().end(); ++iter) {
    cricket::TransportProxy* proxy = iter->second;
    if (proxy->unsent_candidates().size() > 0) {
      ProcessNewLocalCandidate(
          proxy->content_name(), proxy->unsent_candidates());
      proxy->ClearUnsentCandidates();
    }
  }
  // If |allocation_complete_| flag is true, we should send allocation complete
  // signal to the provider.
  if (allocation_complete_)
    OnCandidatesAllocationDone();
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
  allocation_complete_ = true;
  if (ice_observer_ && local_desc_.get()) {
    ice_observer_->OnIceComplete();
  }
}

bool WebRtcSession::CreateDefaultLocalDescription() {
  ASSERT(state() == cricket::BaseSession::STATE_INIT);

  // Creating a default initial offer with audio and video support.
  cricket::MediaSessionOptions options;
  options.has_video = true;
  options.has_audio = true;
  // Enable BUNDLE feature by default.
  options.bundle_enabled = true;

  cricket::SessionDescription* desc(
      session_desc_factory_.CreateOffer(options, NULL));
  if (!desc) {
    LOG(LS_ERROR) << "Failed to create default initial offer.";
    return false;
  }
  set_local_description(desc);
  return true;
}

// Enabling voice and video channel.
void WebRtcSession::EnableChannels() {
  if (voice_channel_.get() && !voice_channel_->enabled())
    voice_channel_->Enable(true);

  if (video_channel_.get() && !video_channel_->enabled())
    video_channel_->Enable(true);
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
    if (local_desc_.get()) {
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
  const cricket::ContentInfos& contents =
      BaseSession::local_description()->contents();
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
  if (!OnRemoteCandidates(content.name, candidates, &error)) {
    LOG(LS_WARNING) << error;
  }
  return true;
}

void WebRtcSession::RemoveUnusedChannelsAndTransports(
    const cricket::SessionDescription* desc) {
  if (!desc) {
    return;
  }

  const cricket::ContentInfo* voice_info =
      cricket::GetFirstAudioContent(desc);
  if ((!voice_info || voice_info->rejected) && voice_channel_.get()) {
    const std::string content_name = voice_channel_->content_name();
    channel_manager_->DestroyVoiceChannel(voice_channel_.release());
    DestroyTransportProxy(content_name);
  }

  const cricket::ContentInfo* video_info =
      cricket::GetFirstVideoContent(desc);
  if ((!video_info || video_info->rejected) && video_channel_.get()) {
    const std::string content_name = video_channel_->content_name();
    channel_manager_->DestroyVideoChannel(video_channel_.release());
    DestroyTransportProxy(content_name);
  }
}

bool WebRtcSession::MaybeUpdateChannelsAndTransports(
    const cricket::SessionDescription* sdesc) {
  // Creating or updating content name for voice transport proxy.
  const cricket::ContentInfo* voice_info = cricket::GetFirstAudioContent(sdesc);
  if (voice_info && !voice_info->rejected && !voice_channel_.get()) {
    CreateVoiceChannel(sdesc);
  } else if (voice_info && voice_channel_.get() &&
             (voice_channel_->content_name() != voice_info->name)) {
    // Update content name if the description has different from
    // default CN_AUDIO(default value). This is allowed only in the INIT state.
    if (state() == cricket::BaseSession::STATE_INIT) {
      voice_channel_->set_content_name(voice_info->name);
      UpdateContentName(voice_channel_->content_name(), voice_info->name);
    } else {
      LOG(LS_ERROR) << "Content names can be updated only in STATE."
                    << " Current state : "
                    << cricket::BaseSession::StateToString(state());
      return false;
    }
  }

  // Creating or updating content name for video transport proxy.
  const cricket::ContentInfo* video_info = cricket::GetFirstVideoContent(sdesc);
  if (video_info && !video_info->rejected && !video_channel_.get()) {
    CreateVideoChannel(sdesc);
  } else if (video_info && video_channel_.get() &&
             (video_channel_->content_name() != video_info->name)) {
    // Update content name if the description has different from
    // default CN_VIDEO (default value). This is allowed only in the INIT state.
    if (state() == cricket::BaseSession::STATE_INIT) {
      video_channel_->set_content_name(video_info->name);
      UpdateContentName(video_channel_->content_name(), video_info->name);
    } else {
      LOG(LS_ERROR) << "Content names can be updated only in STATE."
                    << " Current state : "
                    << cricket::BaseSession::StateToString(state());
      return false;
    }
  }
  return true;
}

bool WebRtcSession::CreateChannels(const cricket::SessionDescription* desc) {
  if (state() != cricket::BaseSession::STATE_INIT)
    return false;

  if (!desc->HasGroup(cricket::GROUP_TYPE_BUNDLE)) {
    // Disabling the BUNDLE flag in PortAllocator.
    port_allocator()->set_flags(port_allocator()->flags() &
                                ~cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  }

  allocation_complete_ = false;
  // Our default transport channels are created with BUNDLE flag on.
  // So easiest way to handle this scenario is to delete all existing
  // transport proxies and channels and recreate.
  // Destroying existing media channel and transport proxies.
  if (voice_channel_.get()) {
    const std::string content_name = voice_channel_->content_name();
    channel_manager_->DestroyVoiceChannel(voice_channel_.release());
    DestroyTransportProxy(content_name);
  }

  if (video_channel_.get()) {
    const std::string content_name = video_channel_->content_name();
    channel_manager_->DestroyVideoChannel(video_channel_.release());
    DestroyTransportProxy(content_name);
  }

  // Creating the media channels and transport proxies.
  const cricket::ContentInfo* voice = cricket::GetFirstAudioContent(desc);
  if (voice && !voice->rejected) {
    if (!CreateVoiceChannel(desc)) {
      LOG(LS_ERROR) << "Failed to create voice channel.";
      return false;
    }
  }

  const cricket::ContentInfo* video = cricket::GetFirstVideoContent(desc);
  if (video && !video->rejected) {
    if (!CreateVideoChannel(desc)) {
      LOG(LS_ERROR) << "Failed to create video channel.";
      return false;
    }
  }

  // Kick starting the ice candidates allocation.
  StartCandidatesAllocation();
  return true;
}

bool WebRtcSession::CreateVoiceChannel(
    const cricket::SessionDescription* desc) {
  const cricket::ContentInfo* voice = cricket::GetFirstAudioContent(desc);
  if (voice_channel_.get() || !voice)
    return false;

  voice_channel_.reset(channel_manager_->CreateVoiceChannel(
      this, voice->name, true));
  return voice_channel_.get() ? true : false;
}

bool WebRtcSession::CreateVideoChannel(
    const cricket::SessionDescription* desc) {
  const cricket::ContentInfo* video = cricket::GetFirstVideoContent(desc);
  if (video_channel_.get() || !video)
    return false;

  video_channel_.reset(channel_manager_->CreateVideoChannel(
      this, video->name, true, voice_channel_.get()));
  return video_channel_.get() ? true : false;
}

}  // namespace webrtc
