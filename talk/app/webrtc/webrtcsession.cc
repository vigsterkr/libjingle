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

#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnection.h"
#include "talk/app/webrtc/peerconnectionsignaling.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/session/phone/channel.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/videocapturer.h"

using cricket::MediaContentDescription;

namespace webrtc {

enum {
  MSG_CANDIDATE_TIMEOUT = 101,
  MSG_CANDIDATE_DISCOVERY_TIMEOUT = 102,
};

// We allow 30 seconds to establish a connection, otherwise it's an error.
static const int kCallSetupTimeout = 30 * 1000;
static const int kCandidateDiscoveryTimeout = 2000;

// Constants for setting the default encoder size.
// TODO: Implement proper negotiation of video resolution.
static const int kDefaultVideoCodecId = 100;
static const int kDefaultVideoCodecFramerate = 30;
static const char kDefaultVideoCodecName[] = "VP8";
static const int kDefaultVideoCodecWidth = 640;
static const int kDefaultVideoCodecHeight = 480;

WebRtcSession::WebRtcSession(cricket::ChannelManager* channel_manager,
                             talk_base::Thread* signaling_thread,
                             talk_base::Thread* worker_thread,
                             cricket::PortAllocator* port_allocator)
    : cricket::BaseSession(signaling_thread, worker_thread, port_allocator,
                           talk_base::ToString(talk_base::CreateRandomId()),
                           cricket::NS_JINGLE_RTP, true),
      channel_manager_(channel_manager),
      observer_(NULL),
      session_desc_factory_(channel_manager) {
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

void WebRtcSession::StartIce() {
  // Try connecting all transport channels. This is necessary to generate
  // ICE candidates.
  SpeculativelyConnectAllTransportChannels();
  signaling_thread()->PostDelayed(
      kCandidateDiscoveryTimeout, this, MSG_CANDIDATE_DISCOVERY_TIMEOUT);
}

void WebRtcSession::set_secure_policy(
    cricket::SecureMediaPolicy secure_policy) {
  session_desc_factory_.set_secure(secure_policy);
}

cricket::SessionDescription* WebRtcSession::CreateOffer(
    const cricket::MediaSessionOptions& options) {
  return session_desc_factory_.CreateOffer(options, local_description());
}

cricket::SessionDescription* WebRtcSession::CreateAnswer(
    const cricket::SessionDescription* offer,
    const cricket::MediaSessionOptions& options) {
  return session_desc_factory_.CreateAnswer(offer, options,
                                            local_description());
}

bool WebRtcSession::SetLocalDescription(cricket::SessionDescription* desc,
                                        cricket::ContentAction type) {
  if ((type == cricket::CA_ANSWER &&
       state() != STATE_RECEIVEDINITIATE) ||
      (type == cricket::CA_OFFER &&
               (state() == STATE_RECEIVEDINITIATE ||
                state() == STATE_SENTINITIATE))) {
    LOG(LS_ERROR) << "SetLocalDescription called with action in wrong state, "
                  << "action: " << type << " state: " << state();
    return false;
  }

  set_local_description(desc);
  if (type == cricket::CA_ANSWER) {
    EnableChannels();
    SetState(STATE_SENTACCEPT);
  } else {
    SetState(STATE_SENTINITIATE);
  }
  return true;
}

bool WebRtcSession::SetRemoteDescription(cricket::SessionDescription* desc,
                                         cricket::ContentAction type) {
  if ((type == cricket::CA_ANSWER &&
       state() != STATE_SENTINITIATE) ||
      (type == cricket::CA_OFFER &&
               (state() == STATE_RECEIVEDINITIATE ||
                state() == STATE_SENTINITIATE))) {
    LOG(LS_ERROR) << "SetRemoteDescription called with action in wrong state, "
                  << "action: " << type << " state: " << state();
    return false;
  }
  set_remote_description(desc);

  if (type  == cricket::CA_ANSWER) {
    EnableChannels();
    SetState(STATE_RECEIVEDACCEPT);
  } else {
    SetState(STATE_RECEIVEDINITIATE);
  }
  return true;
}

bool WebRtcSession::AddRemoteCandidate(const std::string& remote_content_name,
                                       const cricket::Candidate& candidate) {
  if (!remote_description()) {
    LOG(LS_ERROR) << "Remote description not set";
    return false;
  }
  const cricket::ContentInfo* content =
      remote_description()->GetContentByName(remote_content_name);
  if (!content) {
    LOG(LS_ERROR) << "Remote content name does not exist";
    return false;
  }

  std::string local_content_name;
  if (cricket::IsAudioContent(content)) {
    local_content_name = cricket::CN_AUDIO;
  } else if (cricket::IsVideoContent(content)) {
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
  candidates.push_back(candidate);
  proxy->impl()->OnRemoteCandidates(candidates);
  return true;
}

void WebRtcSession::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_CANDIDATE_TIMEOUT:
      LOG(LS_ERROR) << "Transport is not in writable state.";
      SignalError();
      break;
    case MSG_CANDIDATE_DISCOVERY_TIMEOUT:
      if (observer_)
         observer_->OnCandidatesReady();
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
  const uint32 dummy_ssrc = 0;
  if (!channel_manager_->SetVideoCapturer(camera, dummy_ssrc)) {
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
      cricket::GetFirstVideoContent(remote_description());
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
  if (observer_) {
    for (cricket::Candidates::const_iterator citer = candidates.begin();
         citer != candidates.end(); ++citer) {
      observer_->OnCandidateFound(proxy->content_name(), *citer);
    }
  }
}

void WebRtcSession::OnTransportChannelGone(cricket::Transport* transport,
                                           const std::string& name) {
  ASSERT(signaling_thread()->IsCurrent());
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

}  // namespace webrtc
