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

// Supported MediaConstraints.
const char MediaConstraintsInterface::kOfferToReceiveAudio[] =
    "OfferToReceiveAudio";
const char MediaConstraintsInterface::kOfferToReceiveVideo[] =
    "OfferToReceiveVideo";
const char MediaConstraintsInterface::kIceRestart[] =
    "IceRestart";
const char MediaConstraintsInterface::kUseRtpMux[] =
    "googUseRtpMUX";

static bool FindConstraint(
    const MediaConstraintsInterface::Constraints& constraints,
    const std::string& key, std::string* value) {
  for (MediaConstraintsInterface::Constraints::const_iterator iter =
           constraints.begin(); iter != constraints.end(); ++iter) {
    if (iter->key == key) {
      if (value) {
        *value = iter->value;
      }

      return true;
    }
  }
  return false;
}

// Finds a a constraint key and its value. |constraints| can be null..
// |mandatory_constraints| is increased by one if the constraint is mandatory.
static bool FindConstraint(const MediaConstraintsInterface* constraints,
                           const std::string& key, std::string* value,
                           size_t* mandatory_constraints) {
  if (!constraints) {
    return false;
  }
  if (FindConstraint(constraints->GetMandatory(), key, value)) {
    ++*mandatory_constraints;
    return true;
  }
  if (FindConstraint(constraints->GetOptional(), key, value)) {
    return true;
  }
  return false;
}

static bool ParseConstraints(
    const MediaConstraintsInterface* constraints,
    cricket::MediaSessionOptions* options, bool is_answer) {
  std::string value;
  size_t mandatory_constraints_satisfied = 0;

  if (FindConstraint(constraints,
                     MediaConstraintsInterface::kOfferToReceiveAudio,
                     &value, &mandatory_constraints_satisfied)) {
    // |options-|has_audio| can only change from false to
    // true, but never change from true to false. This is to make sure
    // CreateOffer / CreateAnswer doesn't remove a media content
    // description that has been created.
    options->has_audio |=
        (value == MediaConstraintsInterface::kValueTrue);
  } else {
    // kOfferToReceiveAudio is non mandatory true according to spec.
    options->has_audio = true;
  }

  if (FindConstraint(constraints,
                     MediaConstraintsInterface::kOfferToReceiveVideo,
                     &value, &mandatory_constraints_satisfied)) {
    // |options->has_video| can only change from false to
    // true, but never change from true to false. This is to make sure
    // CreateOffer / CreateAnswer doesn't remove a media content
    // description that has been created.
    options->has_video |=
        (value == MediaConstraintsInterface::kValueTrue);
  } else {
    // kOfferToReceiveVideo is non mandatory false according to spec. But
    // if it is an answer and video is offered, we should still accept video
    // per default.
    options->has_video |= is_answer ? true : false;
  }

  if (FindConstraint(constraints,
                     MediaConstraintsInterface::kUseRtpMux,
                     &value, &mandatory_constraints_satisfied)) {
    options->bundle_enabled = (value == MediaConstraintsInterface::kValueTrue);
  } else {
    // kUseRtpMux is non mandatory true according to spec.
    options->bundle_enabled = true;
  }
  if (FindConstraint(constraints,
                     MediaConstraintsInterface::kIceRestart,
                     &value, &mandatory_constraints_satisfied)) {
    options->transport_options.ice_restart =
        (value == MediaConstraintsInterface::kValueTrue);;
  } else {
    // kIceRestart is non mandatory false according to spec.
    options->transport_options.ice_restart = false;
  }

  if (!constraints) {
    return true;
  }
  return mandatory_constraints_satisfied == constraints->GetMandatory().size();
}

// Returns true if if at least one media content is present and
// |options.bundle_enabled| is true.
// Bundle will be enabled  by default if at least one media content is present
// and the constraint kUseRtpMux has not disabled bundle.
static bool EvaluateNeedForBundle(const cricket::MediaSessionOptions& options) {
  return options.bundle_enabled &&
      (options.has_audio || options.has_video || options.has_data);
}

// Helper class used for tracking the mapping between a rtp stream and a
// remote MediaStreamTrack and MediaStream.
class RemoteTracksInterface {
 public:
  // Add a new MediaStreamTrack with |track_id| and |ssrc| and add it to
  // |stream|.
  virtual bool AddRemoteTrack(const std::string& track_id,
                              MediaStreamInterface* stream,
                              uint32 ssrc) = 0;
  // End all MediaStreamTracks that don't exist in |rtp_streams|.
  virtual void RemoveDisappearedTracks(
      const cricket::StreamParamsVec& rtp_streams) = 0;
  virtual bool GetSsrc(const std::string& track_id, uint32* ssrc) const = 0;
  virtual ~RemoteTracksInterface() {}
};

template <typename Track, typename TrackProxy>
class RemoteTracks : public RemoteTracksInterface {
 public:
  explicit RemoteTracks(talk_base::Thread* signaling_thread);
  virtual bool AddRemoteTrack(const std::string& track_id,
                              webrtc::MediaStreamInterface* stream,
                              uint32 ssrc) OVERRIDE;
  virtual void RemoveDisappearedTracks(
      const cricket::StreamParamsVec& rtp_streams) OVERRIDE;
  virtual bool GetSsrc(const std::string& track_id,
                       uint32* ssrc) const OVERRIDE;

 private:
  struct TrackInfo {
    talk_base::scoped_refptr<TrackProxy> track;
    // The MediaStream |track| belongs to.
    talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream;
    // The SSRC the track is identified by. Note that the track can use more
    // SSRCs.
    uint32 ssrc;
  };
  talk_base::Thread* signaling_thread_;
  std::map<std::string, TrackInfo> remote_tracks_;
};

typedef RemoteTracks<webrtc::AudioTrack, webrtc::AudioTrackProxy>
    RemoteAudioTracks;
typedef RemoteTracks<webrtc::VideoTrack, webrtc::VideoTrackProxy>
    RemoteVideoTracks;

template <typename T, typename TP>
RemoteTracks<T, TP>::RemoteTracks(
    talk_base::Thread* signaling_thread)
    : signaling_thread_(signaling_thread) {
}

template <typename T, typename TP>
bool RemoteTracks<T, TP>::AddRemoteTrack(
    const std::string& track_id,
    webrtc::MediaStreamInterface* stream,
    uint32 ssrc) {
  if (remote_tracks_.find(track_id) != remote_tracks_.end()) {
    LOG(LS_WARNING) << "Remote track with id " << track_id
        << " already exists.";
    return false;
  }

  TrackInfo info;
  info.track = TP::Create(T::Create(track_id, NULL), signaling_thread_);
  info.track->set_state(webrtc::MediaStreamTrackInterface::kLive);
  info.stream = stream;
  info.stream->AddTrack(info.track);
  info.ssrc = ssrc;

  remote_tracks_[track_id] = info;
  return true;
}

template <typename T, typename TP>
bool RemoteTracks<T, TP>::GetSsrc(
    const std::string& track_id,
    uint32* ssrc) const {
  typename std::map<std::string, TrackInfo>::const_iterator it =
      remote_tracks_.find(track_id);
  if (it == remote_tracks_.end()) {
      LOG(LS_WARNING) << "Remote track with id " << track_id
          << " does not exists.";
      return false;
  }
  *ssrc = it->second.ssrc;
  return true;
}

template <typename T, typename TP>
void RemoteTracks<T, TP>::RemoveDisappearedTracks(
    const cricket::StreamParamsVec& rtp_streams) {

  std::vector<std::string> track_ids_to_remove;

  // Find all tracks in |remote_tracks_| that don't exist in |rtp_streams|.
  typename std::map<std::string, TrackInfo>::iterator info_it;
  for (info_it = remote_tracks_.begin();
       info_it != remote_tracks_.end(); ++info_it) {
    const TrackInfo& info = info_it->second;
    if (!cricket::GetStreamBySsrc(rtp_streams, info.ssrc, NULL)) {
      track_ids_to_remove.push_back(info.track->id());
    }
  }

  // End all tracks in |tracks_to_remove|.
  std::vector<std::string>::const_iterator track_id_it;
  for (track_id_it = track_ids_to_remove.begin();
      track_id_it != track_ids_to_remove.end(); ++track_id_it) {
    info_it = remote_tracks_.find(*track_id_it);
    TrackInfo& info = info_it->second;
    info.track->set_state(webrtc::MediaStreamTrackInterface::kEnded);
    info.stream->RemoveTrack(info.track);
    remote_tracks_.erase(info_it);
  }
}

MediaStreamSignaling::MediaStreamSignaling(
    talk_base::Thread* signaling_thread,
    RemoteMediaStreamObserver* stream_observer)
    : signaling_thread_(signaling_thread),
      data_channel_factory_(NULL),
      stream_observer_(stream_observer),
      remote_streams_(StreamCollection::Create()),
      remote_audio_tracks_(new RemoteAudioTracks(signaling_thread)),
      remote_video_tracks_(new RemoteVideoTracks(signaling_thread)) {
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

bool MediaStreamSignaling::GetOptionsForOffer(
    const MediaConstraintsInterface* constraints,
    cricket::MediaSessionOptions* options) {
  UpdateSessionOptions();
  if (!ParseConstraints(constraints, &options_, false)) {
    return false;
  }
  options_.bundle_enabled = EvaluateNeedForBundle(options_);
  *options = options_;
  return true;
}

bool MediaStreamSignaling::GetOptionsForAnswer(
    const MediaConstraintsInterface* constraints,
    cricket::MediaSessionOptions* options) {
  UpdateSessionOptions();

  // Copy the |options_| to not let the flag MediaSessionOptions::has_audio and
  // MediaSessionOptions::has_video affect subsequent offers.
  cricket::MediaSessionOptions current_options = options_;
  if (!ParseConstraints(constraints, &current_options, true)) {
    return false;
  }
  current_options.bundle_enabled = EvaluateNeedForBundle(current_options);
  *options = current_options;
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
  talk_base::scoped_refptr<StreamCollection> new_streams(
      StreamCollection::Create());

  // Find all audio rtp streams and create corresponding remote AudioTracks
  // and MediaStreams.
  const cricket::ContentInfo* audio_content = GetFirstAudioContent(remote_desc);
  if (audio_content) {
    const cricket::AudioContentDescription* desc =
        static_cast<const cricket::AudioContentDescription*>(
            audio_content->description);
    UpdateRemoteStreamsList(desc->streams(), desc->type(), new_streams);
    remote_info_.default_audio_track_needed =
        desc->direction() == cricket::MD_SENDRECV && desc->streams().empty();
  }

  // Find all video rtp streams and create corresponding remote VideoTracks
  // and MediaStreams.
  const cricket::ContentInfo* video_content = GetFirstVideoContent(remote_desc);
  if (video_content) {
    const cricket::VideoContentDescription* desc =
        static_cast<const cricket::VideoContentDescription*>(
            video_content->description);
    UpdateRemoteStreamsList(desc->streams(), desc->type(), new_streams);
    remote_info_.default_video_track_needed =
        desc->direction() == cricket::MD_SENDRECV && desc->streams().empty();
  }

  // Update the DataChannels with the information from the remote peer.
  const cricket::ContentInfo* data_content = GetFirstDataContent(remote_desc);
  if (data_content) {
    const cricket::DataContentDescription* data_desc =
        static_cast<const cricket::DataContentDescription*>(
            data_content->description);
    UpdateRemoteDataChannels(data_desc->streams());
  }

  // Iterate new_streams and notify the observer about new MediaStreams.
  for (size_t i = 0; i < new_streams->count(); ++i) {
    MediaStreamInterface* new_stream = new_streams->at(i);
    stream_observer_->OnAddStream(new_stream);
  }

  // Find removed MediaStreams.
  if (remote_info_.IsDefaultMediaStreamNeeded() &&
      remote_streams_->find(kDefaultStreamLabel) != NULL) {
    // The default media stream already exists. No need to do anything.
  } else {
    UpdateEndedRemoteMediaStreams();
    remote_info_.msid_supported |= remote_streams_->count() > 0;
  }
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

bool MediaStreamSignaling::GetRemoteAudioTrackSsrc(
    const std::string& track_id, uint32* ssrc) const {
  return remote_audio_tracks_->GetSsrc(track_id, ssrc);
}

bool MediaStreamSignaling::GetRemoteVideoTrackSsrc(
    const std::string& track_id, uint32* ssrc) const {
  return remote_video_tracks_->GetSsrc(track_id, ssrc);
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
        options_.AddStream(cricket::MEDIA_TYPE_AUDIO, track->id(),
                           stream->label());
      }

      scoped_refptr<VideoTracks> video_tracks(stream->video_tracks());
      if (video_tracks->count() > 0) {
        options_.has_video = true;
      }
      // For each video track in the stream, add it to the MediaSessionOptions.
      for (size_t j = 0; j < video_tracks->count(); ++j) {
        scoped_refptr<MediaStreamTrackInterface> track(video_tracks->at(j));
        options_.AddStream(cricket::MEDIA_TYPE_VIDEO, track->id(),
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

void MediaStreamSignaling::UpdateRemoteStreamsList(
    const cricket::StreamParamsVec& rtp_streams,
    cricket::MediaType media_type,
    StreamCollection* new_streams) {
  RemoteTracksInterface* remote_tracks = GetRemoteTracks(media_type);
  ASSERT(remote_tracks != NULL);

  // Find all new MediaStreams and Tracks.
  for (cricket::StreamParamsVec::const_iterator it = rtp_streams.begin();
       it != rtp_streams.end(); ++it) {
    const std::string mediastream_label = it->sync_label;
    const std::string track_id = it->name;

    talk_base::scoped_refptr<MediaStreamInterface> media_stream(
        remote_streams_->find(mediastream_label));
    if (media_stream == NULL) {
      // This is a new MediaStream. Create a new remote MediaStream.
      media_stream =  MediaStreamProxy::Create(mediastream_label,
                                               signaling_thread_);
      new_streams->AddStream(media_stream);
      remote_streams_->AddStream(media_stream);
    }
    remote_tracks->AddRemoteTrack(track_id, media_stream,
                                     it->first_ssrc());
  }
  // Find all ended MediaStream Tracks.
  remote_tracks->RemoveDisappearedTracks(rtp_streams);
}

void MediaStreamSignaling::UpdateEndedRemoteMediaStreams() {
  std::vector<scoped_refptr<MediaStreamInterface> > streams_to_remove;
  for (size_t i = 0; i < remote_streams_->count(); ++i) {
    MediaStreamInterface*stream = remote_streams_->at(i);
    if (stream->GetAudioTracks().empty() && stream->GetVideoTracks().empty()) {
      streams_to_remove.push_back(stream);
    }
  }

  std::vector<scoped_refptr<MediaStreamInterface> >::const_iterator it;
  for (it = streams_to_remove.begin(); it != streams_to_remove.end(); ++it) {
    remote_streams_->RemoveStream(*it);
    stream_observer_->OnRemoveStream(*it);
  }
}

void MediaStreamSignaling::MaybeCreateDefaultStream() {
  if (!remote_info_.IsDefaultMediaStreamNeeded())
    return;

  bool default_created = false;

  scoped_refptr<MediaStreamInterface> default_remote_stream =
      remote_streams_->find(kDefaultStreamLabel);
  if (default_remote_stream == NULL) {
    default_created = true;
    default_remote_stream = MediaStreamProxy::Create(kDefaultStreamLabel,
                                                      signaling_thread_);
  }
  if (remote_info_.default_audio_track_needed &&
      default_remote_stream->audio_tracks()->count() == 0) {
    remote_audio_tracks_->AddRemoteTrack(kDefaultAudioTrackLabel,
                                         default_remote_stream,
                                         0);
  }
  if (remote_info_.default_video_track_needed &&
      default_remote_stream->video_tracks()->count() == 0) {
    remote_video_tracks_->AddRemoteTrack(kDefaultVideoTrackLabel,
                                         default_remote_stream,
                                         0);
  }
  if (default_created) {
    remote_streams_->AddStream(default_remote_stream);
    stream_observer_->OnAddStream(default_remote_stream);
  }
}

RemoteTracksInterface*
MediaStreamSignaling::GetRemoteTracks(cricket::MediaType type) {
  if (type == cricket::MEDIA_TYPE_AUDIO)
    return remote_audio_tracks_.get();
  else if (type == cricket::MEDIA_TYPE_VIDEO)
    return remote_video_tracks_.get();
  ASSERT(!"Unknown MediaType");
  return NULL;
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
