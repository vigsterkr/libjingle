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

#include "talk/app/webrtc/mediastreamsignaling.h"

#include <vector>

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/mediastreamproxy.h"
#include "talk/app/webrtc/mediastreamtrackproxy.h"
#include "talk/app/webrtc/videotrack.h"

static const char kDefaultStreamLabel[] = "default";
static const char kDefaultAudioTrackLabel[] = "defaulta0";
static const char kDefaultVideoTrackLabel[] = "defaultv0";

namespace webrtc {

using talk_base::scoped_ptr;
using talk_base::scoped_refptr;

MediaStreamSignaling::MediaStreamSignaling(
    talk_base::Thread* signaling_thread,
    RemoteMediaStreamObserver* stream_observer)
    : signaling_thread_(signaling_thread),
      data_channel_factory_(NULL),
      stream_observer_(stream_observer),
      remote_streams_(StreamCollection::Create()) {
  options_.has_video = false;
  options_.has_audio = false;
}

MediaStreamSignaling::~MediaStreamSignaling() {
}

void MediaStreamSignaling::SetLocalStreams(
    StreamCollectionInterface* local_streams) {
  local_streams_ = local_streams;
}

bool MediaStreamSignaling::AddDataChannel(DataChannel* data_channel) {
  ASSERT(data_channel != NULL);
  if (data_channels_.find(data_channel->label()) != data_channels_.end()) {
    LOG(LS_ERROR) << "DataChannel with label " << data_channel->label()
                  << " already exists.";
    return false;
  }
  data_channels_[data_channel->label()] = data_channel;
  return true;
}


const cricket::MediaSessionOptions&
MediaStreamSignaling::GetMediaSessionOptions(const MediaHints& hints) {
  UpdateSessionOptions();
  // has_video and has_audio can only change from false to true,
  // but never change from true to false. This is to make sure CreateOffer and
  // CreateAnswer dont't remove a media content description that has been
  // created.
  options_.has_video |= hints.has_video();
  options_.has_audio |= hints.has_audio();
  // Enable BUNDLE feature by default if at least one media content is present.
  options_.bundle_enabled =
      options_.has_audio || options_.has_video || options_.has_data;
  return options_;
}

bool MediaStreamSignaling::GetRemoteTrackSsrc(
    const std::string& name, uint32* ssrc) const {
  TrackSsrcMap::const_iterator it=  remote_track_ssrc_.find(name);
  if (it == remote_track_ssrc_.end())
    return false;
  *ssrc = it->second;
  return true;
}

// Updates or creates remote MediaStream objects given a
// remote SessionDesription.
// If the remote SessionDesription contains new remote MediaStreams
// the observer OnAddStream method is called. If a remote MediaStream is missing
// from the remote SessionDescription OnRemoveStream is called.
void MediaStreamSignaling::UpdateRemoteStreams(
    const SessionDescriptionInterface* desc) {
  const cricket::SessionDescription* remote_desc = desc->description();
  talk_base::scoped_refptr<StreamCollection> current_streams(
      StreamCollection::Create());

  const cricket::ContentInfo* audio_content = GetFirstAudioContent(remote_desc);
  if (audio_content) {
    remote_info_.supports_audio = true;
    const cricket::AudioContentDescription* desc =
          static_cast<const cricket::AudioContentDescription*>(
              audio_content->description);
    UpdateRemoteStreamsList<AudioTrack, AudioTrackProxy>(desc->streams(),
                                                         current_streams);
  }

  const cricket::ContentInfo* video_content = GetFirstVideoContent(remote_desc);
  if (video_content) {
    remote_info_.supports_video = true;
    const cricket::VideoContentDescription* video_desc =
        static_cast<const cricket::VideoContentDescription*>(
            video_content->description);
    UpdateRemoteStreamsList<VideoTrack, VideoTrackProxy>(video_desc->streams(),
                                                         current_streams);
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

  // Find removed streams.
  if (remote_info_.IsDefaultMediaStreamNeeded() &&
      remote_streams_->find(kDefaultStreamLabel) != NULL) {
    // The default media stream already exists. No need to do anything.
  } else {
    // Iterate the old list of remote streams.
    // If a stream is not found in the new list, it has been removed.
    // Change the state of the removed stream tracks and call the observer
    // OnRemoveStream.
    for (size_t i = 0; i < remote_streams_->count(); ++i) {
      MediaStreamInterface* old_stream = remote_streams_->at(i);
      MediaStreamInterface* new_stream = current_streams->find(
          old_stream->label());
      if (new_stream != NULL) continue;
      UpdateEndedRemoteStream(old_stream);
      stream_observer_->OnRemoveStream(old_stream);
    }

    // Prepare for next description.
    remote_streams_ = current_streams;
    remote_info_.description_set_once = true;
    remote_info_.supports_msid |= remote_streams_->count() > 0;
  }
  MaybeCreateDefaultStream();


  // Update the DataChannels with the information from the remote peer.
  const cricket::ContentInfo* data_content = GetFirstDataContent(remote_desc);
  if (data_content) {
    const cricket::DataContentDescription* data_desc =
        static_cast<const cricket::DataContentDescription*>(
            data_content->description);
    UpdateRemoteDataChannels(data_desc->streams());
  }
}

void MediaStreamSignaling::SetMediaReceived() {
  remote_info_.media_received = true;
  MaybeCreateDefaultStream();
}

void MediaStreamSignaling::UpdateLocalStreams(
    const SessionDescriptionInterface* desc) {
  const cricket::ContentInfo* data_content =
      GetFirstDataContent(desc->description());
  if (data_content) {
    const cricket::DataContentDescription* data_desc =
        static_cast<const cricket::DataContentDescription*>(
            data_content->description);
    UpdateLocalDataChannels(data_desc->streams());
  }
}

void MediaStreamSignaling::UpdateSessionOptions() {
  options_.streams.clear();
  if (local_streams_ != NULL) {
    for (size_t i = 0; i < local_streams_->count(); ++i) {
      MediaStreamInterface* stream = local_streams_->at(i);

      scoped_refptr<AudioTracks> audio_tracks(stream->audio_tracks());
      if (audio_tracks->count() > 0) {
        options_.has_audio = true;
      }

      // For each audio track in the stream, add it to the MediaSessionOptions.
      for (size_t j = 0; j < audio_tracks->count(); ++j) {
        scoped_refptr<MediaStreamTrackInterface> track(audio_tracks->at(j));
        options_.AddStream(cricket::MEDIA_TYPE_AUDIO, track->label(),
                           stream->label());
      }

      scoped_refptr<VideoTracks> video_tracks(stream->video_tracks());
      if (video_tracks->count() > 0) {
        options_.has_video = true;
      }
      // For each video track in the stream, add it to the MediaSessionOptions.
      for (size_t j = 0; j < video_tracks->count(); ++j) {
        scoped_refptr<MediaStreamTrackInterface> track(video_tracks->at(j));
        options_.AddStream(cricket::MEDIA_TYPE_VIDEO, track->label(),
                           stream->label());
      }
    }
  }

  // Check for data channels.
  DataChannels::const_iterator data_channel_it = data_channels_.begin();
  for (; data_channel_it != data_channels_.end(); ++data_channel_it) {
    const DataChannel* channel = data_channel_it->second;
    if (channel->state() == DataChannel::kConnecting ||
        channel->state() == DataChannel::kOpen) {
      // |stream_name| and |sync_label| are both set to the DataChannel label
      // here so they can be signaled the same way as MediaStreams and Tracks.
      // For MediaStreams, the sync_label is the MediaStream label and the
      // track label is the same as |stream_name|.
      const std::string& stream_name = channel->label();
      const std::string& sync_label = channel->label();
      options_.AddStream(cricket::MEDIA_TYPE_DATA, stream_name, sync_label);
    }
  }
}

template <typename Track, typename TrackProxy>
void MediaStreamSignaling::UpdateRemoteStreamsList(
    const cricket::StreamParamsVec& streams,
    StreamCollection* current_streams) {
  for (cricket::StreamParamsVec::const_iterator it = streams.begin();
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
      const std::string track_label = it->name;
      uint32 track_ssrc = it->first_ssrc();
      AddRemoteTrack<Track, TrackProxy>(track_label, track_ssrc, new_stream);
    } else {
      current_streams->AddStream(old_stream);
    }
  }
}

template <typename Track, typename TrackProxy>
void MediaStreamSignaling::AddRemoteTrack(const std::string& track_label,
                                          uint32 ssrc,
                                          RemoteMediaStream* stream) {
  if (remote_track_ssrc_.find(track_label) != remote_track_ssrc_.end()) {
    LOG(LS_WARNING) << "Remote track with label " << track_label
                    << " already exists.";
    return;
  }
  scoped_refptr<TrackProxy> track(
      TrackProxy::Create(Track::Create(track_label, NULL), signaling_thread_));
  track->set_state(MediaStreamTrackInterface::kLive);
  stream->AddTrack(track);
  remote_track_ssrc_[track_label] = ssrc;
}

void MediaStreamSignaling::UpdateEndedRemoteStream(
    MediaStreamInterface* stream) {
  scoped_refptr<AudioTracks> audio_tracklist(stream->audio_tracks());
  for (size_t j = 0; j < audio_tracklist->count(); ++j) {
    MediaStreamTrackInterface* track = audio_tracklist->at(j);
    track->set_state(MediaStreamTrackInterface::kEnded);
    remote_track_ssrc_.erase(track->label());
  }
  scoped_refptr<VideoTracks> video_tracklist(stream->video_tracks());
  for (size_t j = 0; j < video_tracklist->count(); ++j) {
    MediaStreamTrackInterface* track = video_tracklist->at(j);
    track->set_state(MediaStreamTrackInterface::kEnded);
    remote_track_ssrc_.erase(track->label());
  }
}

void MediaStreamSignaling::MaybeCreateDefaultStream() {
  if (!remote_info_.IsDefaultMediaStreamNeeded())
    return;

  bool default_created = false;

  scoped_refptr<RemoteMediaStream> default_remote_stream =
      static_cast<RemoteMediaStream*>(
      remote_streams_->find(kDefaultStreamLabel));
  if (default_remote_stream == NULL) {
    default_created = true;
    default_remote_stream = MediaStreamProxy::Create(kDefaultStreamLabel,
                                                      signaling_thread_);
  }
  if (remote_info_.supports_audio &&
      default_remote_stream->audio_tracks()->count() == 0) {
    AddRemoteTrack<AudioTrack, AudioTrackProxy>(kDefaultAudioTrackLabel, 0,
                                                default_remote_stream);
  }
  if (remote_info_.supports_video &&
      default_remote_stream->video_tracks()->count() == 0) {
    AddRemoteTrack<VideoTrack, VideoTrackProxy>(kDefaultVideoTrackLabel, 0,
                                                default_remote_stream);
  }
  if (default_created) {
    remote_streams_->AddStream(default_remote_stream);
    stream_observer_->OnAddStream(default_remote_stream);
  }
}

void MediaStreamSignaling::UpdateLocalDataChannels(
    const cricket::StreamParamsVec& streams) {
  std::vector<std::string> existing_channels;

  // Find new and active data channels.
  for (cricket::StreamParamsVec::const_iterator it =streams.begin();
       it != streams.end(); ++it) {
    // |it->sync_label| is actually the data channel label. The reason is that
    // we use the same naming of data channels as we do for
    // MediaStreams and Tracks.
    // For MediaStreams, the sync_label is the MediaStream label and the
    // track label is the same as |stream_name|.
    const std::string& channel_label = it->sync_label;
    DataChannels::iterator data_channel_it = data_channels_.find(channel_label);
    if (!VERIFY(data_channel_it != data_channels_.end())) {
      continue;
    }
    // Set the SSRC the data channel should use for sending.
    data_channel_it->second->SetSendSsrc(it->first_ssrc());
    existing_channels.push_back(data_channel_it->first);
  }

  UpdateClosingDataChannels(existing_channels, true);
}

void MediaStreamSignaling::UpdateRemoteDataChannels(
    const cricket::StreamParamsVec& streams) {
  std::vector<std::string> existing_channels;

  // Find new and active data channels.
  for (cricket::StreamParamsVec::const_iterator it = streams.begin();
       it != streams.end(); ++it) {
    // The data channel label is either the mslabel or the SSRC if the mslabel
    // does not exist. Ex a=ssrc:444330170 mslabel:test1.
    std::string label = it->sync_label.empty() ?
        talk_base::ToString(it->first_ssrc()) : it->sync_label;
    DataChannels::iterator data_channel_it =
        data_channels_.find(label);
    if (data_channel_it == data_channels_.end()) {
      // This is a new data channel.
      CreateRemoteDataChannel(label, it->first_ssrc());
    } else {
      data_channel_it->second->SetReceiveSsrc(it->first_ssrc());
    }
    existing_channels.push_back(label);
  }

  UpdateClosingDataChannels(existing_channels, false);
}

void MediaStreamSignaling::UpdateClosingDataChannels(
    const std::vector<std::string>& active_channels, bool is_local_update) {
  DataChannels::iterator it = data_channels_.begin();
  while (it != data_channels_.end()) {
    DataChannel* data_channel = it->second;
    if (std::find(active_channels.begin(), active_channels.end(),
                  data_channel->label()) != active_channels.end()) {
      ++it;
      continue;
    }

    if (is_local_update)
      data_channel->SetSendSsrc(0);
    else
      data_channel->RemotePeerRequestClose();

    if (data_channel->state() == DataChannel::kClosed) {
      data_channels_.erase(it);
      it = data_channels_.begin();
    } else {
      ++it;
    }
  }
}

void MediaStreamSignaling::CreateRemoteDataChannel(const std::string& label,
                                                   uint32 remote_ssrc) {
  if (!data_channel_factory_) {
    LOG(LS_WARNING) << "Remote peer requested a DataChannel but DataChannels "
                    << "are not supported.";
    return;
  }
  scoped_refptr<DataChannel> channel(
      data_channel_factory_->CreateDataChannel(label, NULL));
  channel->SetReceiveSsrc(remote_ssrc);
  stream_observer_->OnAddDataChannel(channel);
}

}  // namespace webrtc
