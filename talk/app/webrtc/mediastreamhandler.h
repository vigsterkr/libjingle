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

// This file contains classes for listening on changes on MediaStreams and
// MediaTracks that are connected to a certain PeerConnection.
// Example: If a user sets a rendererer on a remote video track the renderer is
// connected to the appropriate remote video stream.

#ifndef TALK_APP_WEBRTC_MEDIASTREAMHANDLER_H_
#define TALK_APP_WEBRTC_MEDIASTREAMHANDLER_H_

#include <list>
#include <vector>

#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/mediastreamprovider.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/base/thread.h"

namespace webrtc {

// BaseTrackHandler listen to events on a MediaStreamTrackInterface that are
// connected to a certain PeerConnection.
class BaseTrackHandler : public ObserverInterface {
 public:
  explicit BaseTrackHandler(MediaStreamTrackInterface* track);
  virtual ~BaseTrackHandler();
  virtual void OnChanged();

 protected:
  virtual void OnStateChanged() = 0;
  virtual void OnEnabledChanged() = 0;

 private:
  talk_base::scoped_refptr<MediaStreamTrackInterface> track_;
  MediaStreamTrackInterface::TrackState state_;
  bool enabled_;
};

// LocalAudioTrackHandler listen to events on a local AudioTrack instance
// connected to a PeerConnection and orders the |provider| to executes the
// requested change.
class LocalAudioTrackHandler : public BaseTrackHandler {
 public:
  LocalAudioTrackHandler(AudioTrackInterface* track,
                         AudioProviderInterface* provider);
  virtual ~LocalAudioTrackHandler();

 protected:
  virtual void OnStateChanged();
  virtual void OnEnabledChanged();

 private:
  AudioTrackInterface* audio_track_;
  AudioProviderInterface* provider_;
};

// RemoteAudioTrackHandler listen to events on a remote AudioTrack instance
// connected to a PeerConnection and orders the |provider| to executes the
// requested change.
class RemoteAudioTrackHandler : public BaseTrackHandler {
 public:
  RemoteAudioTrackHandler(AudioTrackInterface* track,
                          AudioProviderInterface* provider);
  virtual ~RemoteAudioTrackHandler();

 protected:
  virtual void OnStateChanged();
  virtual void OnEnabledChanged();

 private:
  AudioTrackInterface* audio_track_;
  AudioProviderInterface* provider_;
};

// LocalVideoTrackHandler listen to events on a local VideoTrack instance
// connected to a PeerConnection and orders the |provider| to executes the
// requested change.
class LocalVideoTrackHandler : public BaseTrackHandler {
 public:
  LocalVideoTrackHandler(VideoTrackInterface* track,
                         VideoProviderInterface* provider);
  virtual ~LocalVideoTrackHandler();

 protected:
  virtual void OnStateChanged();
  virtual void OnEnabledChanged();

 private:
  VideoTrackInterface* local_video_track_;
  VideoProviderInterface* provider_;
};

// RemoteVideoTrackHandler listen to events on a remote VideoTrack instance
// connected to a PeerConnection and orders the |provider| to executes the
// requested change.
class RemoteVideoTrackHandler : public BaseTrackHandler {
 public:
  RemoteVideoTrackHandler(VideoTrackInterface* track,
                          VideoProviderInterface* provider);
  virtual ~RemoteVideoTrackHandler();

 protected:
  virtual void OnStateChanged();
  virtual void OnEnabledChanged();

 private:
  VideoTrackInterface* remote_video_track_;
  VideoProviderInterface* provider_;
};

class MediaStreamHandler : public ObserverInterface {
 public:
  MediaStreamHandler(MediaStreamInterface* stream,
                     AudioProviderInterface* audio_provider,
                     VideoProviderInterface* video_provider);
  ~MediaStreamHandler();
  MediaStreamInterface* stream();
  virtual void OnChanged();

 protected:
  talk_base::scoped_refptr<MediaStreamInterface> stream_;
  AudioProviderInterface* audio_provider_;
  VideoProviderInterface* video_provider_;
  typedef std::vector<BaseTrackHandler*> TrackHandlers;
  TrackHandlers track_handlers_;
};

class LocalMediaStreamHandler : public MediaStreamHandler {
 public:
  LocalMediaStreamHandler(MediaStreamInterface* stream,
                          AudioProviderInterface* audio_provider,
                          VideoProviderInterface* video_provider);
  ~LocalMediaStreamHandler();
};

class RemoteMediaStreamHandler : public MediaStreamHandler {
 public:
  RemoteMediaStreamHandler(MediaStreamInterface* stream,
                           AudioProviderInterface* audio_provider,
                           VideoProviderInterface* video_provider);
  ~RemoteMediaStreamHandler();
};

class MediaStreamHandlers {
 public:
  MediaStreamHandlers(AudioProviderInterface* audio_provider,
                      VideoProviderInterface* video_provider);
  ~MediaStreamHandlers();
  void AddRemoteStream(MediaStreamInterface* stream);
  void RemoveRemoteStream(MediaStreamInterface* stream);
  void CommitLocalStreams(StreamCollectionInterface* streams);

 private:
  typedef std::list<MediaStreamHandler*> StreamHandlerList;
  StreamHandlerList local_streams_handlers_;
  StreamHandlerList remote_streams_handlers_;
  AudioProviderInterface* audio_provider_;
  VideoProviderInterface* video_provider_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_MEDIASTREAMHANDLER_H_
