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

  // Returns a MediaSessionOptions struct with options decided by |hints| and
  // the local MediaStreams and DataChannels.
  virtual const cricket::MediaSessionOptions& GetMediaSessionOptions(
      const MediaHints& hints);

  // Updates or creates remote MediaStream objects given a
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

  // Notify the MediaStreamSignaling object that media has been received.
  // This is used by MediaStreamSignaling to decide if a default remote
  // MediaStream must be created.
  void SetMediaReceived();

  // Returns the SSRC for a given track.
  bool GetRemoteTrackSsrc(const std::string& name, uint32* ssrc) const;

  // Returns all current remote MediaStreams.
  StreamCollectionInterface* remote_streams() const {
    return remote_streams_.get(); }

 private:
  // We can use LocalMediaStreamInterface as RemoteMediaStream since the are the
  // same except that tracks can be added and removed on the
  // LocalMediaStreamInterface
  typedef LocalMediaStreamInterface RemoteMediaStream;

  // Create new MediaStreams and Tracks if they exist in |streams|
  // Both new and existing MediaStreams are added to |current_streams|.
  template <typename Track, typename TrackProxy>
  void UpdateRemoteStreamsList(
      const std::vector<cricket::StreamParams>& streams,
      StreamCollection* current_streams);
  template <typename Track, typename TrackProxy>
    void AddRemoteTrack(const std::string& track_label,
                        uint32 ssrc,
                        RemoteMediaStream* stream);

  void UpdateSessionOptions();
  void MaybeCreateDefaultStream();
  void UpdateEndedRemoteStream(MediaStreamInterface* stream);
  void UpdateLocalDataChannels(const cricket::StreamParamsVec& streams);
  void UpdateRemoteDataChannels(const cricket::StreamParamsVec& streams);
  void UpdateClosingDataChannels(
      const std::vector<std::string>& active_channels, bool is_local_update);
  void CreateRemoteDataChannel(const std::string& label, uint32 remote_ssrc);

  struct RemotePeerInfo {
    RemotePeerInfo()
        : media_received(false),
          description_set_once(false),
          supports_msid(false),
          supports_audio(false),
          supports_video(false) {
    }
    bool media_received;  // Media has been received from the remote peer.
    // UpdateRemoteStreams has been called at least once.
    bool description_set_once;
    bool supports_msid;
    // The remote peer indicates in the session description that audio is
    // supported.
    bool supports_audio;
    // The remote peer indicates in the session description that video is
    // supported.
    bool supports_video;

    bool IsDefaultMediaStreamNeeded() {
      // Returns true iff media has been received and
      // UpdateRemoteStreams has been called at least once but never with any
      // StreamParams and it support audio and/or video.
      return media_received && description_set_once && !supports_msid &&
          (supports_audio || supports_video);
    }
  };
  RemotePeerInfo remote_info_;
  talk_base::Thread* signaling_thread_;
  DataChannelFactory* data_channel_factory_;
  cricket::MediaSessionOptions options_;
  RemoteMediaStreamObserver* stream_observer_;
  talk_base::scoped_refptr<StreamCollectionInterface> local_streams_;
  talk_base::scoped_refptr<StreamCollection> remote_streams_;

  typedef std::map<std::string, talk_base::scoped_refptr<DataChannel> >
      DataChannels;
  DataChannels data_channels_;

  typedef std::map<std::string, uint32> TrackSsrcMap;
  TrackSsrcMap remote_track_ssrc_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_MEDIASTREAMSIGNALING_H_
