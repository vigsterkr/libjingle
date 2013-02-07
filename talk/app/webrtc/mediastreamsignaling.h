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

#ifndef TALK_APP_WEBRTC_MEDIASTREAMSIGNALING_H_
#define TALK_APP_WEBRTC_MEDIASTREAMSIGNALING_H_

#include <string>
#include <vector>
#include <map>

#include "talk/app/webrtc/datachannel.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/base/scoped_ref_ptr.h"
#include "talk/session/media/mediasession.h"

namespace talk_base {
class Thread;
}  // namespace talk_base

namespace webrtc {

class RemoteTracksInterface;

// RemoteMediaStreamObserver is triggered when
// MediaStreamSignaling::UpdateRemoteStreams is called with a new
// SessionDescription with a new set of MediaStreams and DataChannels.
class RemoteMediaStreamObserver {
 public:
  // Triggered when the remote SessionDescription has a new stream.
  virtual void OnAddStream(MediaStreamInterface* stream) = 0;

  // Triggered when the remote SessionDescription removes a stream.
  virtual void OnRemoveStream(MediaStreamInterface* stream) = 0;

  // Triggered when the remote SessionDescription has a new data channel.
  virtual void OnAddDataChannel(DataChannelInterface* data_channel) = 0;

 protected:
  ~RemoteMediaStreamObserver() {}
};

// MediaStreamSignaling works as a glue between MediaStreams and a cricket
// classes for SessionDescriptions.
// It is used for creating cricket::MediaSessionOptions given the local
// MediaStreams and data channels.
//
// It is responsible for creating remote MediaStreams given a remote
// SessionDescription and creating cricket::MediaSessionOptions given
// local MediaStreams.
//
// To signal that a DataChannel should be established:
// 1. Call AddDataChannel with the new DataChannel. Next time
//    GetMediaSessionOptions will include the description of the DataChannel.
// 2. When a local session description is set, call UpdateLocalStreams with the
//    session description. This will set the SSRC used for sending data on
//    this DataChannel.
// 3. When remote session description is set, call UpdateRemoteStream with the
//    session description. If the DataChannel label and a SSRC is included in
//    the description, the DataChannel is updated with SSRC that will be used
//    for receiving data.
// 4. When both the local and remote SSRC of a DataChannel is set the state of
//    the DataChannel change to kOpen.
//
// To setup a DataChannel initialized by the remote end.
// 1. When remote session description is set, call UpdateRemoteStream with the
//    session description. If a label and a SSRC of a new DataChannel is found
//    RemoteMediaStreamObserver::OnAddDataChannel with the label and SSRC is
//    triggered.
// 2. Create a DataChannel instance with the label and set the remote SSRC.
// 3. Call AddDataChannel with this new DataChannel.  GetMediaSessionOptions
//    will include the description of the DataChannel.
// 4. Create a local session description and call UpdateLocalStreams. This will
//    set the local SSRC used by the DataChannel.
// 5. When both the local and remote SSRC of a DataChannel is set the state of
//    the DataChannel change to kOpen.
//
// To close a DataChannel:
// 1. Call DataChannel::Close. This will change the state of the DataChannel to
//    kClosing. GetMediaSessionOptions will not
//    include the description of the DataChannel.
// 2. When a local session description is set, call UpdateLocalStreams with the
//    session description. The description will no longer contain the
//    DataChannel label or SSRC.
// 3. When remote session description is set, call UpdateRemoteStream with the
//    session description. The description will no longer contain the
//    DataChannel label or SSRC. The DataChannel SSRC is updated with SSRC=0.
//    The DataChannel change state to kClosed.

class MediaStreamSignaling {
 public:
  MediaStreamSignaling(talk_base::Thread* signaling_thread,
                       RemoteMediaStreamObserver* stream_observer);
  virtual ~MediaStreamSignaling();

  // Set a factory for creating data channels that are initiated by the remote
  // peer.
  void SetDataChannelFactory(DataChannelFactory* data_channel_factory) {
    data_channel_factory_ = data_channel_factory;
  }

  // Set the collection of local MediaStreams included in the
  // cricket::MediaSessionOption returned by GetMediaSessionOptions.
  void SetLocalStreams(StreamCollectionInterface* local_streams);

  // Adds |data_channel| to the collection of DataChannels included in the
  // cricket::MediaSessionOptions returned by GetMediaSessionOptions().
  bool AddDataChannel(DataChannel* data_channel);

  // Returns a MediaSessionOptions struct with options decided by |constraints|,
  // the local MediaStreams and DataChannels.
  virtual bool GetOptionsForOffer(
      const MediaConstraintsInterface* constraints,
      cricket::MediaSessionOptions* options);

  // Returns a MediaSessionOptions struct with options decided by
  // |constraints|, the local MediaStreams and DataChannels.
  virtual bool GetOptionsForAnswer(
      const MediaConstraintsInterface* constraints,
      cricket::MediaSessionOptions* options);

  // Updates or creates remote MediaStream objects and Tracks given a
  // remote SessionDescription.
  // If the remote SessionDescription contain information about a new remote
  // MediaStreams a new remote MediaStream is created and
  // RemoteMediaStreamObserver::OnAddStream is called.
  // If a remote MediaStream is missing from
  // the remote SessionDescription RemoteMediaStreamObserver::OnRemoveStream
  // is called.
  //
  // If the SessionDescription contains information about a new DataChannel,
  // RemoteMediaStreamObserver::OnAddDataChannel is called with the DataChannel.
  void UpdateRemoteStreams(const SessionDescriptionInterface* desc);

  // Updates local DataChannels with information about its local SSRC.
  void UpdateLocalStreams(const SessionDescriptionInterface* desc);

  // Returns the SSRC for a given track.
  bool GetRemoteAudioTrackSsrc(const std::string& track_id, uint32* ssrc) const;
  bool GetRemoteVideoTrackSsrc(const std::string& track_id, uint32* ssrc) const;

  // Returns all current remote MediaStreams.
  StreamCollectionInterface* remote_streams() const {
    return remote_streams_.get();
  }

 private:
  struct RemotePeerInfo {
    RemotePeerInfo()
        : msid_supported(false),
          default_audio_track_needed(false),
          default_video_track_needed(false) {
    }
    // True if it has been discovered that the remote peer support MSID.
    bool msid_supported;
    // The remote peer indicates in the session description that audio will be
    // sent but no MSID is given.
    bool default_audio_track_needed;
    // The remote peer indicates in the session description that video will be
    // sent but no MSID is given.
    bool default_video_track_needed;

    bool IsDefaultMediaStreamNeeded() {
      return !msid_supported && (default_audio_track_needed ||
          default_video_track_needed);
    }
  };
  void UpdateSessionOptions();
  // Makes sure a MediaStream Track is created for each StreamParam in
  // |streams|. |media_type| is the type of the |streams| and can be either
  // audio or video.
  // If a new MediaStream is created it is added to |new_streams|.
  void UpdateRemoteStreamsList(
      const std::vector<cricket::StreamParams>& streams,
      cricket::MediaType media_type,
      StreamCollection* new_streams);
  // Finds remote MediaStreams without any tracks and removes them from
  // |remote_streams_| and notifies the observer that the MediaStream no longer
  // exist.
  void UpdateEndedRemoteMediaStreams();
  void MaybeCreateDefaultStream();
  RemoteTracksInterface* GetRemoteTracks(cricket::MediaType type);

  void UpdateLocalDataChannels(const cricket::StreamParamsVec& streams);
  void UpdateRemoteDataChannels(const cricket::StreamParamsVec& streams);
  void UpdateClosingDataChannels(
      const std::vector<std::string>& active_channels, bool is_local_update);
  void CreateRemoteDataChannel(const std::string& label, uint32 remote_ssrc);

  RemotePeerInfo remote_info_;
  talk_base::Thread* signaling_thread_;
  DataChannelFactory* data_channel_factory_;
  cricket::MediaSessionOptions options_;
  RemoteMediaStreamObserver* stream_observer_;
  talk_base::scoped_refptr<StreamCollectionInterface> local_streams_;
  talk_base::scoped_refptr<StreamCollection> remote_streams_;
  talk_base::scoped_ptr<RemoteTracksInterface> remote_audio_tracks_;
  talk_base::scoped_ptr<RemoteTracksInterface> remote_video_tracks_;

  typedef std::map<std::string, talk_base::scoped_refptr<DataChannel> >
      DataChannels;
  DataChannels data_channels_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_MEDIASTREAMSIGNALING_H_
