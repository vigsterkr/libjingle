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

#include "talk/app/webrtc/peerconnectionsignaling.h"

#include <utility>

#include "talk/app/webrtc/mediastreamproxy.h"
#include "talk/app/webrtc/mediastreamtrackproxy.h"
#include "talk/app/webrtc/sessiondescriptionprovider.h"
#include "talk/app/webrtc/streamcollectionimpl.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/messagequeue.h"
#include "talk/session/phone/channelmanager.h"

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

// Fills a MediaSessionOptions struct with the MediaTracks we want to sent given
// the local MediaStreams.
static void InitMediaSessionOptions(
    cricket::MediaSessionOptions* options,
    StreamCollectionInterface* local_streams) {
  // In order to be able to receive video,
  // the has_video should always be true even if there are no video tracks.
  options->has_video = true;
  for (size_t i = 0; i < local_streams->count(); ++i) {
    MediaStreamInterface* stream = local_streams->at(i);

    scoped_refptr<AudioTracks> audio_tracks(stream->audio_tracks());
    // For each audio track in the stream, add it to the MediaSessionOptions.
    for (size_t j = 0; j < audio_tracks->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(audio_tracks->at(j));
      options->AddStream(cricket::MEDIA_TYPE_AUDIO, track->label(),
                         stream->label());
    }

    scoped_refptr<VideoTracks> video_tracks(stream->video_tracks());
    // For each video track in the stream, add it to the MediaSessionOptions.
    for (size_t j = 0; j <  video_tracks->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(video_tracks->at(j));
      options->AddStream(cricket::MEDIA_TYPE_VIDEO, track->label(),
                         stream->label());
    }
  }
}

PeerConnectionSignaling::PeerConnectionSignaling(
    talk_base::Thread* signaling_thread,
    SessionDescriptionProvider* provider)
    : signaling_thread_(signaling_thread),
      provider_(provider),
      state_(kInitializing),
      received_pre_offer_(false),
      remote_streams_(StreamCollection::Create()),
      local_streams_(StreamCollection::Create()) {
}

PeerConnectionSignaling::~PeerConnectionSignaling() {}

void PeerConnectionSignaling::OnCandidatesReady(
    const cricket::Candidates& candidates) {
  if (!VERIFY(state_ == kInitializing))
    return;
  // Store the candidates.
  candidates_ = candidates;
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

void PeerConnectionSignaling::ChangeState(State new_state) {
  state_ = new_state;
  SignalStateChange(state_);
}

void PeerConnectionSignaling::ProcessSignalingMessage(
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

      // If we are still Initializing we need to wait before we can handle
      // the offer. Queue it and handle it when the state changes.
      if (state_ == kInitializing) {
        received_pre_offer_ = true;
        break;
      }

      if (state_ == kWaitingForAnswer) {
        // Message received out of order or Glare occurred and the decision was
        // to use the incoming offer.
        LOG(LS_INFO) << "Received offer while waiting for answer.";
        // Be nice and handle this offer instead of the pending offer.
        signaling_thread_->Clear(this, MSG_SEND_QUEUED_OFFER);
      }
      // Post a task to handle the answer.
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

      // Hand over the remote session description and the remote candidates from
      // the parsed ROAP message to the provider_.
      // The session description ownership is transferred from roap_session_ to
      // the provider_;
      const cricket::SessionDescription* remote_desc =
          provider_->SetRemoteSessionDescription(
              roap_session_.ReleaseRemoteDescription(),
              roap_session_.RemoteCandidates());
      // Let the provider now that the negotiation is done and both local and
      // remote session description is now valid.
      provider_->NegotiationDone();

      // Update the list of known remote MediaStreams.
      UpdateRemoteStreams(remote_desc);
      // Pop the first item of queued StreamCollections containing local
      // MediaStreams that just have been negotiated.
      scoped_refptr<StreamCollectionInterface> streams(
          queued_local_streams_.front());
      queued_local_streams_.pop_front();
      // Update the state of the local MediaStreams.
      UpdateSendingLocalStreams(remote_desc, streams);

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
        // Let the provider know the negotiation is done.
        provider_->NegotiationDone();

        scoped_refptr<StreamCollectionInterface> streams(
            queued_local_streams_.front());
        queued_local_streams_.pop_front();
        // Update the state of the local streams.
        UpdateSendingLocalStreams(local_desc_, streams);
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

void PeerConnectionSignaling::CreateOffer(
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

void PeerConnectionSignaling::SendShutDown() {
  DoShutDown();
  SignalNewPeerConnectionMessage(roap_session_.CreateShutDown());
}

// Implement talk_base::MessageHandler.
void PeerConnectionSignaling::OnMessage(talk_base::Message* msg) {
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

void PeerConnectionSignaling::CreateOffer_s() {
  ASSERT(!queued_local_streams_.empty());
  scoped_refptr<StreamCollectionInterface> local_streams(
      queued_local_streams_.front());
  cricket::MediaSessionOptions options;
  InitMediaSessionOptions(&options, local_streams);

  const cricket::SessionDescription* local_desc =
      provider_->ProvideOffer(options);

  SignalNewPeerConnectionMessage(roap_session_.CreateOffer(local_desc,
                                                           candidates_));
}

void PeerConnectionSignaling::DoShutDown() {
  ChangeState(kShutingDown);
  signaling_thread_->Clear(this);  // Don't send queued offers or answers.
  queued_local_streams_.clear();
  provider_->SetRemoteSessionDescription(NULL, cricket::Candidates());
  provider_->NegotiationDone();
  UpdateRemoteStreams(NULL);
}

void PeerConnectionSignaling::CreateAnswer_s() {
  // Let the provider know about the remote offer.
  // The provider takes ownership and return a pointer for us to use.
  const cricket::SessionDescription* remote_desc =
      provider_->SetRemoteSessionDescription(
          roap_session_.ReleaseRemoteDescription(),
          roap_session_.RemoteCandidates());

  scoped_refptr<StreamCollectionInterface> streams(
      queued_local_streams_.back());
  // Clean up all queued collections of local streams except the last one.
  // The last one is kept until the ok message is received for this answer and
  // is needed for updating the state of the local streams.
  queued_local_streams_.erase(queued_local_streams_.begin(),
                              --queued_local_streams_.end());

  // Create a MediaSessionOptions object with the sources we want to send.
  cricket::MediaSessionOptions options;
  InitMediaSessionOptions(&options, streams);
  // Create an local session description based on this.
  local_desc_ = provider_->ProvideAnswer(options);

  if (!VerifyAnswer(local_desc_)) {
    SignalNewPeerConnectionMessage(roap_session_.CreateErrorMessage(kRefused));
    return;
  }

  UpdateRemoteStreams(remote_desc);
  ChangeState(kWaitingForOK);
  SignalNewPeerConnectionMessage(roap_session_.CreateAnswer(local_desc_,
                                                            candidates_));
}

// Updates or Creates remote MediaStream objects given a
// remote SessionDesription.
// If the remote SessionDesription contain new remote MediaStreams
// SignalRemoteStreamAdded is triggered. If a remote MediaStream is missing from
// the remote SessionDescription SignalRemoteStreamRemoved is triggered.
void PeerConnectionSignaling::UpdateRemoteStreams(
    const cricket::SessionDescription* remote_desc) {
  talk_base::scoped_refptr<StreamCollection> current_streams(
      StreamCollection::Create());

  const cricket::ContentInfo* audio_content = GetFirstAudioContent(remote_desc);
  if (audio_content) {
    const cricket::AudioContentDescription* audio_desc =
        static_cast<const cricket::AudioContentDescription*>(
            audio_content->description);

    for (cricket::StreamParamsVec::const_iterator it =
             audio_desc->streams().begin();
         it != audio_desc->streams().end(); ++it) {
      MediaStreamInterface* old_stream = remote_streams_->find(it->sync_label);
      scoped_refptr<MediaStreamProxy> new_stream(static_cast<MediaStreamProxy*>(
          current_streams->find(it->sync_label)));

      if (old_stream == NULL) {
        if (new_stream == NULL) {
          // New stream
          new_stream = MediaStreamProxy::Create(it->sync_label,
                                                signaling_thread_);
          current_streams->AddStream(new_stream);
        }
        scoped_refptr<AudioTrackInterface> track(
            AudioTrackProxy::CreateRemote(it->name, signaling_thread_));
        track->set_state(MediaStreamTrackInterface::kLive);
        new_stream->AddTrack(track);
      } else {
        current_streams->AddStream(old_stream);
      }
    }
  }

  const cricket::ContentInfo* video_content = GetFirstVideoContent(remote_desc);
  if (video_content) {
    const cricket::VideoContentDescription* video_desc =
        static_cast<const cricket::VideoContentDescription*>(
            video_content->description);

    for (cricket::StreamParamsVec::const_iterator it =
             video_desc->streams().begin();
         it != video_desc->streams().end(); ++it) {
      MediaStreamInterface* old_stream = remote_streams_->find(it->sync_label);
      scoped_refptr<MediaStreamProxy> new_stream(static_cast<MediaStreamProxy*>(
          current_streams->find(it->sync_label)));
      if (old_stream == NULL) {
        if (new_stream == NULL) {
          // New stream
          new_stream = MediaStreamProxy::Create(it->sync_label,
                                                signaling_thread_);
          current_streams->AddStream(new_stream);
        }
        scoped_refptr<VideoTrackInterface> track(
            VideoTrackProxy::CreateRemote(it->name, signaling_thread_));
        new_stream->AddTrack(track);
        track->set_state(MediaStreamTrackInterface::kLive);
      } else {
        current_streams->AddStream(old_stream);
      }
    }
  }

  // Iterate current_streams to find all new streams.
  // Change the state of the new stream and SignalRemoteStreamAdded.
  for (size_t i = 0; i < current_streams->count(); ++i) {
    MediaStreamInterface* new_stream = current_streams->at(i);
    MediaStreamInterface* old_stream = remote_streams_->find(
        new_stream->label());
    if (old_stream != NULL) continue;

    new_stream->set_ready_state(MediaStreamInterface::kLive);
    SignalRemoteStreamAdded(new_stream);
  }

  // Iterate the old list of remote streams.
  // If a stream is not found in the new list it have been removed.
  // Change the state of the removed stream and SignalRemoteStreamRemoved.
  for (size_t i = 0; i < remote_streams_->count(); ++i) {
    MediaStreamInterface* old_stream = remote_streams_->at(i);
    MediaStreamInterface* new_stream = current_streams->find(
        old_stream->label());
    if (new_stream != NULL) continue;

    old_stream->set_ready_state(MediaStreamInterface::kEnded);
    scoped_refptr<AudioTracks> audio_tracklist(old_stream->audio_tracks());
    for (size_t j = 0; j < audio_tracklist->count(); ++j) {
      audio_tracklist->at(j)->set_state(MediaStreamTrackInterface::kEnded);
    }
    scoped_refptr<VideoTracks> video_tracklist(old_stream->video_tracks());
    for (size_t j = 0; j < video_tracklist->count(); ++j) {
      video_tracklist->at(j)->set_state(MediaStreamTrackInterface::kEnded);
    }
    SignalRemoteStreamRemoved(old_stream);
  }
  // Prepare for next offer.
  remote_streams_ = current_streams;
}

// Update the state of all local streams we have just negotiated. If the
// negotiation succeeded the state is changed to kLive, if the negotiation
// failed the state is changed to kEnded.
void PeerConnectionSignaling::UpdateSendingLocalStreams(
    const cricket::SessionDescription* answer_desc,
    StreamCollectionInterface* negotiated_streams) {
  talk_base::scoped_refptr<StreamCollection> current_local_streams(
      StreamCollection::Create());

  for (size_t i = 0; i < negotiated_streams->count(); ++i) {
    scoped_refptr<MediaStreamInterface> stream(negotiated_streams->at(i));
    scoped_refptr<AudioTracks> audiotracklist(stream->audio_tracks());
    scoped_refptr<VideoTracks> videotracklist(stream->video_tracks());

    bool stream_ok = false;  // A stream is ok if at least one track succeed.
    // Update tracks based on its type.
    for (size_t j = 0; j < audiotracklist->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(audiotracklist->at(j));
      const cricket::ContentInfo* audio_content =
          GetFirstAudioContent(answer_desc);
      if (!audio_content) {  // The remote does not accept audio.
        track->set_state(MediaStreamTrackInterface::kFailed);
        continue;
      }

      const cricket::AudioContentDescription* audio_desc =
          static_cast<const cricket::AudioContentDescription*>(
              audio_content->description);
      if (audio_desc->codecs().size() <= 0) {
        // No common codec.
        track->set_state(MediaStreamTrackInterface::kFailed);
      }
      track->set_state(MediaStreamTrackInterface::kLive);
      stream_ok = true;
    }

    for (size_t j = 0; j < videotracklist->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(videotracklist->at(j));
      const cricket::ContentInfo* video_content =
          GetFirstVideoContent(answer_desc);
      if (!video_content) {  // The remote does not accept video.
        track->set_state(MediaStreamTrackInterface::kFailed);
        continue;
      }

      const cricket::VideoContentDescription* video_desc =
          static_cast<const cricket::VideoContentDescription*>(
              video_content->description);
      if (video_desc->codecs().size() <= 0) {
        // No common codec.
        track->set_state(MediaStreamTrackInterface::kFailed);
      }
      track->set_state(MediaStreamTrackInterface::kLive);
      stream_ok = true;
    }

    if (stream_ok) {
      // We have successfully negotiated to send this stream.
      // Change the stream and store it as successfully negotiated.
      stream->set_ready_state(MediaStreamInterface::kLive);
      current_local_streams->AddStream(stream);
    } else {
      stream->set_ready_state(MediaStreamInterface::kEnded);
    }
  }

  // Iterate the old list of local streams.
  // If a stream is not found in the new list it have been removed.
  // Change the state of the removed stream and all its tracks to kEnded.
  for (size_t i = 0; i < local_streams_->count(); ++i) {
    MediaStreamInterface* old_stream = local_streams_->at(i);
    MediaStreamInterface* new_streams =
        negotiated_streams->find(old_stream->label());

    if (new_streams != NULL) continue;

    old_stream->set_ready_state(MediaStreamInterface::kEnded);
    scoped_refptr<AudioTracks> audio_tracklist(old_stream->audio_tracks());
    for (size_t j = 0; j < audio_tracklist->count(); ++j) {
      audio_tracklist->at(j)->set_state(MediaStreamTrackInterface::kEnded);
    }
    scoped_refptr<VideoTracks> video_tracklist(old_stream->video_tracks());
    for (size_t j = 0; j < video_tracklist->count(); ++j) {
      video_tracklist->at(j)->set_state(MediaStreamTrackInterface::kEnded);
    }
  }

  // Update the local_streams_ for next update.
  local_streams_ = current_local_streams;
}

}  // namespace webrtc
