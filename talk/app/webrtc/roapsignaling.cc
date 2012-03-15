/*
 * libjingle
 * Copyright 2011, Google Inc.
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

#include "talk/app/webrtc/roapsignaling.h"

#include <utility>

#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/streamcollectionimpl.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/messagequeue.h"
#include "talk/session/phone/channelmanager.h"

// TODO - These are magic strings that names the candidates.
// These will be removed when this ROAP implementation is based on JSEP.
static const char kRtpVideoChannelStr[] = "video_rtp";
static const char kRtcpVideoChannelStr[] = "video_rtcp";
static const char kRtpAudioChannelStr[] = "rtp";
static const char kRtcpAudioChannelStr[] = "rtcp";

using talk_base::scoped_refptr;

namespace webrtc {

enum {
  MSG_SEND_QUEUED_OFFER = 1,
  MSG_GENERATE_ANSWER = 2,
};

// Verifies that a SessionDescription contains as least one valid media content
// and a valid codec.
static bool VerifyAnswer(const cricket::SessionDescription* answer_desc) {
  // We need to verify that at least one media content with
  // a codec is available.
  const cricket::ContentInfo* audio_content =
      GetFirstAudioContent(answer_desc);
  if (audio_content) {
    const cricket::AudioContentDescription* audio_desc =
        static_cast<const cricket::AudioContentDescription*>(
            audio_content->description);
    if (audio_desc->codecs().size() > 0) {
      return true;
    }
  }
  const cricket::ContentInfo* video_content =
      GetFirstVideoContent(answer_desc);
  if (video_content) {
    const cricket::VideoContentDescription* video_desc =
        static_cast<const cricket::VideoContentDescription*>(
            video_content->description);
    if (video_desc->codecs().size() > 0) {
      return true;
    }
  }
  return false;
}

RoapSignaling::RoapSignaling(
    talk_base::Thread* signaling_thread,
    MediaStreamSignaling* mediastream_signaling,
    JsepInterface* provider)
    : signaling_thread_(signaling_thread),
      stream_signaling_(mediastream_signaling),
      provider_(provider),
      state_(kInitializing),
      received_pre_offer_(false),
      local_streams_(StreamCollection::Create()) {
}

RoapSignaling::~RoapSignaling() {}

void RoapSignaling::OnIceComplete() {
  if (!VERIFY(state_ == kInitializing))
    return;
  // If we have a queued remote offer we need to handle this first.
  if (received_pre_offer_) {
    received_pre_offer_ = false;
    ChangeState(kWaitingForOK);
    signaling_thread_->Post(this, MSG_GENERATE_ANSWER);
  } else if (!queued_local_streams_.empty()) {
    // Else if we have local queued offers.
    ChangeState(kWaitingForAnswer);
    signaling_thread_->Post(this, MSG_SEND_QUEUED_OFFER);
  } else {
    ChangeState(kIdle);
  }
}

// TODO: OnIceCandidate is called from webrtcsession when a new
// IceCandidate is found. Here we don't care about the content name since
// we can create a valid SDP based on the candidate names.
// This function will be removed if we implement ROAP on top of JSEP.
void RoapSignaling::OnIceCandidate(
    const IceCandidateInterface* candidate) {
  candidates_.push_back(candidate->candidate());
}

void RoapSignaling::ChangeState(State new_state) {
  state_ = new_state;
  SignalStateChange(state_);
}

void RoapSignaling::ProcessSignalingMessage(
    const std::string& message,
    StreamCollectionInterface* local_streams) {
  ASSERT(talk_base::Thread::Current() == signaling_thread_);

  RoapSession::ParseResult result = roap_session_.Parse(message);

  // Signal an error message and return if a message is received after shutdown
  // or it is not an ok message that is received during shutdown.
  // No other messages from the remote peer can be processed in these states.
  if (state_ == kShutdownComplete ||
      (state_ == kShutingDown && result != RoapSession::kOk)) {
    SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(kNoMatch));
    return;
  }

  switch (result) {
    case RoapSession::kOffer: {
      queued_local_streams_.clear();
      queued_local_streams_.push_back(local_streams);

      if (state_ == kWaitingForAnswer) {
        // Message received out of order or Glare occurred and the decision was
        // to use the incoming offer.
        LOG(LS_INFO) << "Received offer while waiting for answer.";
        // Be nice and handle this offer instead of the pending offer.
        signaling_thread_->Clear(this, MSG_SEND_QUEUED_OFFER);
      }

      // Provide the remote session description and the remote candidates from
      // the parsed ROAP message to the |provider_|.
      // The session description ownership is transferred from |roap_session_|
      // to |provider_|.
      ProcessRemoteDescription(roap_session_.ReleaseRemoteDescription(),
                               JsepInterface::kOffer,
                               roap_session_.RemoteCandidates());

      // If we are still Initializing we need to wait until we have our local
      // candidates before we can handle the offer. Queue it and handle it when
      // the state changes.
      if (state_ == kInitializing) {
        received_pre_offer_ = true;
        break;
      }

      // Post a task to generate the answer.
      signaling_thread_->Post(this, MSG_GENERATE_ANSWER);
      ChangeState(kWaitingForOK);
      break;
    }
    case RoapSession::kAnswerMoreComing: {
      // We ignore this message for now and wait for the complete result.
      LOG(LS_INFO) << "Received answer more coming.";
      break;
    }
    case RoapSession::kAnswer: {
      if (state_ != kWaitingForAnswer) {
        LOG(LS_WARNING) << "Received an unexpected answer.";
        return;
      }

      talk_base::scoped_ptr<cricket::SessionDescription> remote_desc(
          roap_session_.ReleaseRemoteDescription());

      // Pop the first item of queued StreamCollections containing local
      // MediaStreams that just have been negotiated.
      scoped_refptr<StreamCollectionInterface> streams(
          queued_local_streams_.front());
      queued_local_streams_.pop_front();

      // Hand the ownership of the local session description to |provider_|.
      provider_->SetLocalDescription(JsepInterface::kOffer,
                                     local_desc_.release());

      // Provide the remote session description and the remote candidates from
      // the parsed ROAP message to the |provider_|.
      // The session description ownership is transferred from |roap_session_|
      // to |provider_|.
      ProcessRemoteDescription(remote_desc.release(),
                               JsepInterface::kAnswer,
                               roap_session_.RemoteCandidates());

      // Let the remote peer know we have received the answer.
      SignalNewPeerConnectionMessage(roap_session_.CreateOk());
      // Check if we have more offers waiting in the queue.
      if (!queued_local_streams_.empty()) {
        // Send the next offer.
        signaling_thread_->Post(this, MSG_SEND_QUEUED_OFFER);
      } else {
        ChangeState(kIdle);
      }
      break;
    }
    case RoapSession::kOk: {
      if (state_ == kWaitingForOK) {
        scoped_refptr<StreamCollectionInterface> streams(
            queued_local_streams_.front());
        queued_local_streams_.pop_front();

        // Hand over the ownership of the local description to the provider.
        provider_->SetLocalDescription(JsepInterface::kAnswer,
                                       local_desc_.release());
        ChangeState(kIdle);
        // Check if we have an updated offer waiting in the queue.
        if (!queued_local_streams_.empty())
          signaling_thread_->Post(this, MSG_SEND_QUEUED_OFFER);
      } else if (state_ == kShutingDown) {
        ChangeState(kShutdownComplete);
      }
      break;
    }
    case RoapSession::kConflict: {
      SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(
          kConflict));
      break;
    }
    case RoapSession::kDoubleConflict: {
      SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(
          kDoubleConflict));

      // Recreate the offer with new sequence values etc.
      ChangeState(kWaitingForAnswer);
      signaling_thread_->Post(this, MSG_SEND_QUEUED_OFFER);
      break;
    }
    case RoapSession::kError: {
      if (roap_session_.RemoteError() != kConflict &&
          roap_session_.RemoteError() != kDoubleConflict) {
        SignalErrorMessageReceived(roap_session_.RemoteError());
        // An error have occurred that we can't do anything about.
        // Reset the state and wait for user action.
        signaling_thread_->Clear(this);
        queued_local_streams_.clear();
        ChangeState(kIdle);
      }
      break;
    }
    case RoapSession::kShutDown: {
      DoShutDown();
      SignalNewPeerConnectionMessage(roap_session_.CreateOk());
      ChangeState(kShutdownComplete);
      break;
    }
    case RoapSession::kInvalidMessage: {
      SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(
          kNoMatch));
      return;
    }
  }
}

void RoapSignaling::CreateOffer(
    StreamCollectionInterface* local_streams) {
  if (!VERIFY(talk_base::Thread::Current() == signaling_thread_ &&
              state_ != kShutingDown && state_ != kShutdownComplete)) {
    return;
  }

  queued_local_streams_.push_back(local_streams);
  if (state_ == kIdle) {
    // Check if we can send a new offer.
    // Only one offer is allowed at the time.
    ChangeState(kWaitingForAnswer);
    signaling_thread_->Post(this, MSG_SEND_QUEUED_OFFER);
  }
}

void RoapSignaling::SendShutDown() {
  DoShutDown();
  SignalNewPeerConnectionMessage(roap_session_.CreateShutDown());
}

// Implement talk_base::MessageHandler.
void RoapSignaling::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_SEND_QUEUED_OFFER:
      CreateOffer_s();
      break;
    case MSG_GENERATE_ANSWER:
      CreateAnswer_s();
      break;
    default:
      ASSERT(!"Invalid value in switch statement.");
      break;
  }
}

void RoapSignaling::CreateOffer_s() {
  ASSERT(!queued_local_streams_.empty());
  scoped_refptr<StreamCollectionInterface> local_streams(
      queued_local_streams_.front());
  stream_signaling_->SetLocalStreams(local_streams);
  local_desc_.reset(provider_->CreateOffer(MediaHints()));
  SignalNewPeerConnectionMessage(
      roap_session_.CreateOffer(local_desc_->description(), candidates_));
}

void RoapSignaling::DoShutDown() {
  ChangeState(kShutingDown);
  signaling_thread_->Clear(this);  // Don't send queued offers or answers.
  queued_local_streams_.clear();

  stream_signaling_->SetLocalStreams(NULL);
  // Create new empty session descriptions without StreamParams.
  // By applying these descriptions we don't send or receive any streams.
  SessionDescriptionInterface* local_desc =
      provider_->CreateOffer(MediaHints());
  SessionDescriptionInterface* remote_desc =
      provider_->CreateAnswer(MediaHints(), local_desc);

  provider_->SetRemoteDescription(JsepInterface::kOffer, remote_desc);
  provider_->SetLocalDescription(JsepInterface::kAnswer, local_desc);
}

void RoapSignaling::CreateAnswer_s() {
  scoped_refptr<StreamCollectionInterface> streams(
      queued_local_streams_.back());
  // Clean up all queued collections of local streams except the last one.
  // The last one is kept until the ok message is received for this answer and
  // is needed for updating the state of the local streams.
  queued_local_streams_.erase(queued_local_streams_.begin(),
                              --queued_local_streams_.end());

  stream_signaling_->SetLocalStreams(streams);
  // Create an local session description based on this.
  local_desc_.reset(provider_->CreateAnswer(MediaHints(),
                                            provider_->remote_description()));
  if (!VerifyAnswer(local_desc_->description())) {
    SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(kRefused));
    return;
  }

  SignalNewPeerConnectionMessage(
      roap_session_.CreateAnswer(local_desc_->description(), candidates_));
}

void RoapSignaling::ProcessRemoteDescription(
    cricket::SessionDescription* remote_description,
    JsepInterface::Action action,
    const cricket::Candidates& candidates) {

  // Provide the remote session description and the remote candidates from
  // the parsed ROAP message to the |provider_|.
  // The session description ownership is transferred from |roap_session_|
  // to |provider_|.
  provider_->SetRemoteDescription(
      action, new JsepSessionDescription(remote_description));

  // Process all the remote candidates.
  // TODO: Remove this once JsepInterface
  // can take a JsepSessionDescription including candidates as input.
  for (cricket::Candidates::const_iterator citer = candidates.begin();
      citer != candidates.end(); ++citer) {
    if ((*citer).name().compare(kRtpVideoChannelStr) == 0 ||
        (*citer).name().compare(kRtcpVideoChannelStr) == 0) {
      // Candidate names for video rtp and rtcp channel
      JsepIceCandidate candidate(cricket::CN_VIDEO, *citer);
      provider_->ProcessIceMessage(&candidate);
    } else if ((*citer).name().compare(kRtpAudioChannelStr) == 0 ||
        (*citer).name().compare(kRtcpAudioChannelStr) == 0) {
      // Candidates for audio rtp and rtcp channel
      // Channel name will be "rtp" and "rtcp"
      JsepIceCandidate candidate(cricket::CN_AUDIO, *citer);
      provider_->ProcessIceMessage(&candidate);
    }
  }
}

}  // namespace webrtc
