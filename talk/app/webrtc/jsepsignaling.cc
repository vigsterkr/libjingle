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

#include "talk/app/webrtc/jsepsignaling.h"

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamproxy.h"
#include "talk/app/webrtc/mediastreamtrackproxy.h"
#include "talk/app/webrtc/sessiondescriptionprovider.h"
#include "talk/app/webrtc/streamcollectionimpl.h"
#include "talk/app/webrtc/webrtcsdp.h"
#include "talk/base/logging.h"
#include "talk/base/scoped_ptr.h"

namespace webrtc {

using talk_base::scoped_ptr;
using talk_base::scoped_refptr;

IceCandidateInterface* CreateIceCandidate(const std::string& label,
                                          const std::string& sdp) {
  JsepIceCandidate* jsep_ice = new JsepIceCandidate(label);
  if (!jsep_ice->Initialize(sdp)) {
    delete jsep_ice;
    return NULL;
  }
  return jsep_ice;
}

SessionDescriptionInterface* CreateSessionDescription(const std::string& sdp) {
  JsepSessionDescription* jsep_desc = new JsepSessionDescription();
  if (!jsep_desc->Initialize(sdp)) {
    delete jsep_desc;
    return NULL;
  }
  return jsep_desc;
}


// Fills a MediaSessionOptions struct with the MediaTracks we want to sent given
// the local MediaStreams.
static void InitMediaSessionOptions(const MediaHints& hints,
                                    StreamCollectionInterface* local_streams,
                                    cricket::MediaSessionOptions* options) {
  options->has_video = hints.has_video();
  options->has_audio = hints.has_audio();
  if (local_streams == NULL)
    return;

  for (size_t i = 0; i < local_streams->count(); ++i) {
    MediaStreamInterface* stream = local_streams->at(i);

    scoped_refptr<AudioTracks> audio_tracks(stream->audio_tracks());
    if (audio_tracks->count() > 0) {
      options->has_audio = true;
    }

    // For each audio track in the stream, add it to the MediaSessionOptions.
    for (size_t j = 0; j < audio_tracks->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(audio_tracks->at(j));
      options->AddStream(cricket::MEDIA_TYPE_AUDIO, track->label(),
                         stream->label());
    }

    scoped_refptr<VideoTracks> video_tracks(stream->video_tracks());
    if (video_tracks->count() > 0) {
          options->has_video = true;
    }
    // For each video track in the stream, add it to the MediaSessionOptions.
    for (size_t j = 0; j <  video_tracks->count(); ++j) {
      scoped_refptr<MediaStreamTrackInterface> track(video_tracks->at(j));
      options->AddStream(cricket::MEDIA_TYPE_VIDEO, track->label(),
                         stream->label());
    }
  }
}

static cricket::ContentAction GetContentAction(JsepInterface::Action action) {
  switch (action) {
    case JsepInterface::kOffer:
      return cricket::CA_OFFER;
    case JsepInterface::kAnswer:
      return cricket::CA_ANSWER;
    default:
      ASSERT(!"Not supported action");
  };
  return cricket::CA_OFFER;
}

JsepSignaling::JsepSignaling(talk_base::Thread* signaling_thread,
                             SessionDescriptionProvider* provider,
                             IceCandidateObserver* observer,
                             JsepRemoteMediaStreamObserver* stream_observer)
    : signaling_thread_(signaling_thread),
      provider_(provider),
      observer_(observer),
      stream_observer_(stream_observer),
      local_description_(new JsepSessionDescription()),
      remote_streams_(StreamCollection::Create()),
      remote_description_(new JsepSessionDescription()) {
}

JsepSignaling::~JsepSignaling() {
}

void JsepSignaling::SetLocalStreams(StreamCollectionInterface* local_streams) {
  local_streams_ = local_streams;
}

SessionDescriptionInterface* JsepSignaling::CreateOffer(
    const MediaHints& hints) {
  cricket::MediaSessionOptions options;
  InitMediaSessionOptions(hints, local_streams_, &options);
  cricket::SessionDescription* offer(provider_->CreateOffer(options));

  JsepSessionDescription* desc = new JsepSessionDescription();
  desc->SetDescription(offer);
  return desc;
}

SessionDescriptionInterface* JsepSignaling::CreateAnswer(
    const MediaHints& hints,
    const SessionDescriptionInterface* offer) {
  cricket::MediaSessionOptions options;
  InitMediaSessionOptions(hints, local_streams_, &options);

  cricket::SessionDescription* answer(
      provider_->CreateAnswer(offer->description(), options));
  if (!answer) {
    LOG(LS_ERROR) << "Failed to create answer to jsep offer";
    return NULL;
  }

  JsepSessionDescription* desc = new JsepSessionDescription();
  desc->SetDescription(answer);
  return desc;
}

bool JsepSignaling::SetLocalDescription(Action action,
                                        SessionDescriptionInterface* desc) {
  cricket::ContentAction content_action = GetContentAction(action);
  bool ret = provider_->SetLocalDescription(desc->ReleaseDescription(),
                                            content_action);
  local_description_->SetConstDescription(provider_->local_description());
  return ret;
}

bool JsepSignaling::SetRemoteDescription(Action action,
                                         SessionDescriptionInterface* desc) {
  cricket::ContentAction content_action = GetContentAction(action);
  bool ret = provider_->SetRemoteDescription(desc->ReleaseDescription(),
                                             content_action);
  remote_description_->SetConstDescription(provider_->remote_description());

  // It is important that we have updated the provider with the
  // remote SessionDescription before we update the streams.
  // Otherwise a raise can occur if the remote tracks are
  // changed by the application. E.x a renderer is added.
  UpdateRemoteStreams(provider_->remote_description());
  return ret;
}

bool JsepSignaling::ProcessIceMessage(const IceCandidateInterface* candidate) {
  if (candidate == NULL)
    return false;
  return provider_->AddRemoteCandidate(candidate->label(),
                                       candidate->candidate());
}

void JsepSignaling::OnCandidatesReady() {
  observer_->OnIceComplete();
}

void JsepSignaling::OnCandidateFound(const std::string& content_name,
                                     const cricket::Candidate& candidate) {
  JsepIceCandidate jsep_candidate(content_name);
  jsep_candidate.SetCandidate(candidate);
  observer_->OnIceCandidate(&jsep_candidate);
}

// Updates or Creates remote MediaStream objects given a
// remote SessionDesription.
// If the remote SessionDesription contain new remote MediaStreams
// SignalRemoteStreamAdded is triggered. If a remote MediaStream is missing from
// the remote SessionDescription SignalRemoteStreamRemoved is triggered.
void JsepSignaling::UpdateRemoteStreams(
    const cricket::SessionDescription* remote_desc) {
  talk_base::scoped_refptr<StreamCollection> current_streams(
      StreamCollection::Create());

  const cricket::ContentInfo* audio_content = GetFirstAudioContent(remote_desc);
  if (audio_content) {
    const cricket::AudioContentDescription* desc =
          static_cast<const cricket::AudioContentDescription*>(
              audio_content->description);
    UpdateRemoteStreamsList<AudioTrackInterface, AudioTrackProxy>(
        desc->streams(), current_streams);
  }

  const cricket::ContentInfo* video_content = GetFirstVideoContent(remote_desc);
  if (video_content) {
    const cricket::VideoContentDescription* video_desc =
        static_cast<const cricket::VideoContentDescription*>(
            video_content->description);
    UpdateRemoteStreamsList<VideoTrackInterface, VideoTrackProxy>(
        video_desc->streams(), current_streams);
  }

  // Iterate current_streams to find all new streams.
  // Change the state of the new stream and SignalRemoteStreamAdded.
  for (size_t i = 0; i < current_streams->count(); ++i) {
    MediaStreamInterface* new_stream = current_streams->at(i);
    MediaStreamInterface* old_stream = remote_streams_->find(
        new_stream->label());
    if (old_stream != NULL) continue;

    new_stream->set_ready_state(MediaStreamInterface::kLive);
    stream_observer_->OnAddStream(new_stream);
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
    stream_observer_->OnRemoveStream(old_stream);
  }
  // Prepare for next offer.
  remote_streams_ = current_streams;
}

template <typename TrackInterface, typename TrackProxy>
void JsepSignaling::UpdateRemoteStreamsList(
    const cricket::StreamParamsVec& streams,
    StreamCollection* current_streams) {
  for (cricket::StreamParamsVec::const_iterator it =streams.begin();
       it != streams.end(); ++it) {
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
      scoped_refptr<TrackInterface> track(
          TrackProxy::CreateRemote(it->name, signaling_thread_));
      track->set_state(MediaStreamTrackInterface::kLive);
      new_stream->AddTrack(track);
    } else {
      current_streams->AddStream(old_stream);
    }
  }
}
}  // namespace webrtc
