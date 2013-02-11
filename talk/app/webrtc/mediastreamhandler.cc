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

#include "talk/app/webrtc/mediastreamhandler.h"

#include "talk/app/webrtc/localaudiosource.h"
#include "talk/app/webrtc/localvideosource.h"
#include "talk/app/webrtc/videosourceinterface.h"

namespace webrtc {

BaseTrackHandler::BaseTrackHandler(MediaStreamTrackInterface* track)
    : track_(track),
      state_(track->state()),
      enabled_(track->enabled()) {
  track_->RegisterObserver(this);
}

BaseTrackHandler::~BaseTrackHandler() {
  track_->UnregisterObserver(this);
}

void BaseTrackHandler::OnChanged() {
  if (state_ != track_->state()) {
    state_ = track_->state();
    OnStateChanged();
  }
  if (enabled_ != track_->enabled()) {
    enabled_ = track_->enabled();
    OnEnabledChanged();
  }
}

LocalAudioTrackHandler::LocalAudioTrackHandler(
    AudioTrackInterface* track,
    AudioProviderInterface* provider)
    : BaseTrackHandler(track),
      audio_track_(track),
      provider_(provider) {
  OnEnabledChanged();
}

LocalAudioTrackHandler::~LocalAudioTrackHandler() {
}

void LocalAudioTrackHandler::OnStateChanged() {
  // TODO(perkj): What should happen when the state change?
}

void LocalAudioTrackHandler::OnEnabledChanged() {
  cricket::AudioOptions options;
  if (audio_track_->enabled() && audio_track_->GetSource()) {
    options = static_cast<LocalAudioSource*>(
        audio_track_->GetSource())->options();
  }
  provider_->SetAudioSend(audio_track_->id(), audio_track_->enabled(),
                          options);
}

RemoteAudioTrackHandler::RemoteAudioTrackHandler(
    AudioTrackInterface* track,
    AudioProviderInterface* provider)
    : BaseTrackHandler(track),
      audio_track_(track),
      provider_(provider) {
  OnEnabledChanged();
}

RemoteAudioTrackHandler::~RemoteAudioTrackHandler() {
}

void RemoteAudioTrackHandler::OnStateChanged() {
  // TODO(perkj): What should happen when the state change?
}

void RemoteAudioTrackHandler::OnEnabledChanged() {
  provider_->SetAudioPlayout(audio_track_->id(), audio_track_->enabled());
}

LocalVideoTrackHandler::LocalVideoTrackHandler(
    VideoTrackInterface* track,
    VideoProviderInterface* provider)
    : BaseTrackHandler(track),
      local_video_track_(track),
      provider_(provider) {
  VideoSourceInterface* source = local_video_track_->GetSource();
  if (source)
    provider_->SetCaptureDevice(local_video_track_->id(),
                                source->GetVideoCapturer());
  OnEnabledChanged();
}

LocalVideoTrackHandler::~LocalVideoTrackHandler() {
}

void LocalVideoTrackHandler::OnStateChanged() {
  // TODO(perkj): What should happen when the state change?
}

void LocalVideoTrackHandler::OnEnabledChanged() {
  const cricket::VideoOptions* options = NULL;
  VideoSourceInterface* source = local_video_track_->GetSource();
  if (local_video_track_->enabled() && source) {
    options = source->options();
  }
  provider_->SetVideoSend(local_video_track_->id(),
                          local_video_track_->enabled(),
                          options);
}

RemoteVideoTrackHandler::RemoteVideoTrackHandler(
    VideoTrackInterface* track,
    VideoProviderInterface* provider)
    : BaseTrackHandler(track),
      remote_video_track_(track),
      provider_(provider) {
  OnEnabledChanged();
}

RemoteVideoTrackHandler::~RemoteVideoTrackHandler() {
  // Since cricket::VideoRenderer is not reference counted
  // we need to remove the renderer before we are deleted.
  provider_->SetVideoPlayout(remote_video_track_->id(), false, NULL);
}

void RemoteVideoTrackHandler::OnStateChanged() {
  // TODO(perkj): What should happen when the state change?
}

void RemoteVideoTrackHandler::OnEnabledChanged() {
  provider_->SetVideoPlayout(remote_video_track_->id(),
                             remote_video_track_->enabled(),
                             remote_video_track_->FrameInput());
}

MediaStreamHandler::MediaStreamHandler(MediaStreamInterface* stream,
                                       AudioProviderInterface* audio_provider,
                                       VideoProviderInterface* video_provider)
    : stream_(stream),
      audio_provider_(audio_provider),
      video_provider_(video_provider) {
}

MediaStreamHandler::~MediaStreamHandler() {
  for (TrackHandlers::iterator it = track_handlers_.begin();
       it != track_handlers_.end(); ++it) {
    delete *it;
  }
}

MediaStreamInterface* MediaStreamHandler::stream() {
  return stream_.get();
}

void MediaStreamHandler::OnChanged() {
}

LocalMediaStreamHandler::LocalMediaStreamHandler(
    MediaStreamInterface* stream,
    AudioProviderInterface* audio_provider,
    VideoProviderInterface* video_provider)
    : MediaStreamHandler(stream, audio_provider, video_provider) {
  // Create an AudioTrack handler for all audio tracks in the MediaStream.
  AudioTracks* audio_tracklist(stream->audio_tracks());
  for (size_t j = 0; j < audio_tracklist->count(); ++j) {
    BaseTrackHandler* handler(new LocalAudioTrackHandler(audio_tracklist->at(j),
                                                         audio_provider));
    track_handlers_.push_back(handler);
  }
  // Create a VideoTrack handler for all video tracks in the MediaStream.
  VideoTracks* video_tracklist(stream->video_tracks());
  for (size_t j = 0; j < video_tracklist->count(); ++j) {
    VideoTrackInterface* track = video_tracklist->at(j);
    BaseTrackHandler* handler(new LocalVideoTrackHandler(track,
                                                         video_provider));
    track_handlers_.push_back(handler);
  }
}

LocalMediaStreamHandler::~LocalMediaStreamHandler() {
}

RemoteMediaStreamHandler::RemoteMediaStreamHandler(
    MediaStreamInterface* stream,
    AudioProviderInterface* audio_provider,
    VideoProviderInterface* video_provider)
    : MediaStreamHandler(stream, audio_provider, video_provider) {
  // Create an AudioTrack handler for all audio tracks  in the MediaStream.
  AudioTracks* audio_tracklist(stream->audio_tracks());
  for (size_t j = 0; j < audio_tracklist->count(); ++j) {
    BaseTrackHandler* handler(
        new RemoteAudioTrackHandler(audio_tracklist->at(j), audio_provider));
    track_handlers_.push_back(handler);
  }

  // Create a VideoTrack handler for all video tracks  in the MediaStream.
  VideoTracks* tracklist(stream->video_tracks());
  for (size_t j = 0; j < tracklist->count(); ++j) {
    VideoTrackInterface* track =
        static_cast<VideoTrackInterface*>(tracklist->at(j));
    BaseTrackHandler* handler(
        new RemoteVideoTrackHandler(track, video_provider));
    track_handlers_.push_back(handler);
  }
}

RemoteMediaStreamHandler::~RemoteMediaStreamHandler() {
}

MediaStreamHandlers::MediaStreamHandlers(
    AudioProviderInterface* audio_provider,
    VideoProviderInterface* video_provider)
    : audio_provider_(audio_provider),
      video_provider_(video_provider) {
}

MediaStreamHandlers::~MediaStreamHandlers() {
  for (StreamHandlerList::iterator it = remote_streams_handlers_.begin();
       it != remote_streams_handlers_.end(); ++it) {
    delete *it;
  }
  for (StreamHandlerList::iterator it = local_streams_handlers_.begin();
       it != local_streams_handlers_.end(); ++it) {
    delete *it;
  }
}

void MediaStreamHandlers::AddRemoteStream(MediaStreamInterface* stream) {
  RemoteMediaStreamHandler* handler =
      new RemoteMediaStreamHandler(stream, audio_provider_, video_provider_);
  remote_streams_handlers_.push_back(handler);
}

void MediaStreamHandlers::RemoveRemoteStream(MediaStreamInterface* stream) {
  StreamHandlerList::iterator it = remote_streams_handlers_.begin();
  for (; it != remote_streams_handlers_.end(); ++it) {
    if ((*it)->stream() == stream) {
      delete *it;
      break;
    }
  }
  ASSERT(it != remote_streams_handlers_.end());
  remote_streams_handlers_.erase(it);
}

void MediaStreamHandlers::CommitLocalStreams(
    StreamCollectionInterface* streams) {
  // Iterate the old list of local streams.
  // If its not found in the new collection it have been removed.
  // We can not erase from the old collection at the same time as we iterate.
  // That is what the ugly while(1) fix.
  while (1) {
    StreamHandlerList::iterator it = local_streams_handlers_.begin();
    for (; it != local_streams_handlers_.end(); ++it) {
      if (streams->find((*it)->stream()->label()) == NULL) {
        delete *it;
        break;
      }
    }
    if (it != local_streams_handlers_.end()) {
      local_streams_handlers_.erase(it);
      continue;
    }
    break;
  }

  // Iterate the new collection of local streams.
  // If its not found in the old collection it have been added.
  for (size_t j = 0; j < streams->count(); ++j) {
    MediaStreamInterface* stream = streams->at(j);
    StreamHandlerList::iterator it = local_streams_handlers_.begin();
    for (; it != local_streams_handlers_.end(); ++it) {
      if (stream == (*it)->stream())
        break;
    }
    if (it == local_streams_handlers_.end()) {
      LocalMediaStreamHandler* handler =
          new LocalMediaStreamHandler(stream, audio_provider_, video_provider_);
      local_streams_handlers_.push_back(handler);
    }
  }
};


}  // namespace webrtc
