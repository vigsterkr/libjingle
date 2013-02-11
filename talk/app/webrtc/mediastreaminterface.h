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

// This file contains interfaces for MediaStream, MediaTrack and MediaSource.
// These interfaces are used for implementing MediaStream and MediaTrack as
// defined in http://dev.w3.org/2011/webrtc/editor/webrtc.html#stream-api. These
// interfaces must be used only with PeerConnection. PeerConnectionManager
// interface provides the factory methods to create MediaStream and MediaTracks.

#ifndef TALK_APP_WEBRTC_MEDIASTREAMINTERFACE_H_
#define TALK_APP_WEBRTC_MEDIASTREAMINTERFACE_H_

#include <string>
#include <vector>

#include "talk/base/basictypes.h"
#include "talk/base/refcount.h"
#include "talk/base/scoped_ref_ptr.h"

namespace cricket {

class VideoCapturer;
class VideoRenderer;
class VideoFrame;

}  // namespace cricket

namespace webrtc {

// Generic observer interface.
class ObserverInterface {
 public:
  virtual void OnChanged() = 0;

 protected:
  virtual ~ObserverInterface() {}
};

class NotifierInterface {
 public:
  virtual void RegisterObserver(ObserverInterface* observer) = 0;
  virtual void UnregisterObserver(ObserverInterface* observer) = 0;

  virtual ~NotifierInterface() {}
};

// Base class for sources. A MediaStreamTrack have an underlying source that
// provide media. A source can be shared with multiple tracks.
// TODO(perkj): Implement sources for local and remote audio tracks and
// remote video tracks.
class MediaSourceInterface :  public talk_base::RefCountInterface,
                              public NotifierInterface {
 public:
  enum SourceState {
    kInitializing,
    kLive,
    kEnded,
    kMuted
  };

  virtual SourceState state() const = 0;

 protected:
  virtual ~MediaSourceInterface() {}
};

// Information about a track.
class MediaStreamTrackInterface : public talk_base::RefCountInterface,
                                  public NotifierInterface {
 public:
  enum TrackState {
    kInitializing,  // Track is beeing negotiated.
    kLive = 1,  // Track alive
    kEnded = 2,  // Track have ended
    kFailed = 3,  // Track negotiation failed.
  };

  virtual std::string kind() const = 0;
  virtual std::string id() const = 0;
  virtual bool enabled() const = 0;
  virtual TrackState state() const = 0;
  virtual bool set_enabled(bool enable) = 0;
  // These methods should be called by implementation only.
  virtual bool set_state(TrackState new_state) = 0;
};

// Interface for rendering VideoFrames from a VideoTrack
class VideoRendererInterface {
 public:
  virtual void SetSize(int width, int height) = 0;
  virtual void RenderFrame(const cricket::VideoFrame* frame) = 0;

 protected:
  // The destructor is protected to prevent deletion via the interface.
  // This is so that we allow reference counted classes, where the destructor
  // should never be public, to implement the interface.
  virtual ~VideoRendererInterface() {}
};

class VideoSourceInterface;

class VideoTrackInterface : public MediaStreamTrackInterface {
 public:
  // Register a renderer that will render all frames received on this track.
  virtual void AddRenderer(VideoRendererInterface* renderer) = 0;
  // Deregister a renderer.
  virtual void RemoveRenderer(VideoRendererInterface* renderer) = 0;

  // Gets a pointer to the frame input of this VideoTrack.
  // The pointer is valid for the lifetime of this VideoTrack.
  // VideoFrames rendered to the cricket::VideoRenderer will be rendered on all
  // registered renderers.
  virtual cricket::VideoRenderer* FrameInput() = 0;

  virtual VideoSourceInterface* GetSource() const = 0;

 protected:
  virtual ~VideoTrackInterface() {}
};

// TODO(perkj): Deprecate and remove LocalAudioTrackInterface when no clients
// use it.
typedef VideoTrackInterface LocalVideoTrackInterface;

// AudioSourceInterface is a reference counted source used for AudioTracks.
// The same source can be used in multiple AudioTracks.
// TODO(perkj): Extend this class with necessary methods to allow separate
// sources for each audio track.
class AudioSourceInterface : public MediaSourceInterface {
};

class AudioTrackInterface : public MediaStreamTrackInterface {
 public:
  virtual AudioSourceInterface* GetSource() const =  0;

 protected:
  virtual ~AudioTrackInterface() {}
};

// TODO(perkj): Deprecate and remove LocalAudioTrackInterface when no clients
// use it.
typedef AudioTrackInterface LocalAudioTrackInterface;

// List of of tracks.
// Deprecated: Please use TrackVectors.
template <class TrackType>
class MediaStreamTrackListInterface : public talk_base::RefCountInterface {
 public:
  virtual size_t count() const = 0;
  virtual TrackType* at(size_t index) = 0;
  virtual TrackType* Find(const std::string& id) = 0;

 protected:
  virtual ~MediaStreamTrackListInterface() {}
};

typedef std::vector<talk_base::scoped_refptr<AudioTrackInterface> >
    AudioTrackVector;
typedef std::vector<talk_base::scoped_refptr<VideoTrackInterface> >
    VideoTrackVector;

// Deprecated: Please use AudioTrackVector.
typedef MediaStreamTrackListInterface<AudioTrackInterface> AudioTracks;
// Deprecated: Please use VideoTrackVector.
typedef MediaStreamTrackListInterface<VideoTrackInterface> VideoTracks;

class MediaStreamInterface : public talk_base::RefCountInterface,
                             public NotifierInterface {
 public:
  virtual std::string label() const = 0;

  virtual AudioTrackVector GetAudioTracks() = 0;
  virtual VideoTrackVector GetVideoTracks() = 0;
  virtual talk_base::scoped_refptr<AudioTrackInterface>
      FindAudioTrack(const std::string& track_id) = 0;
  virtual talk_base::scoped_refptr<VideoTrackInterface>
      FindVideoTrack(const std::string& track_id) = 0;

  virtual bool AddTrack(AudioTrackInterface* track) = 0;
  virtual bool AddTrack(VideoTrackInterface* track) = 0;
  virtual bool RemoveTrack(AudioTrackInterface* track) = 0;
  virtual bool RemoveTrack(VideoTrackInterface* track) = 0;

  // Deprecated: Please use GetAudioTracks.
  virtual AudioTracks* audio_tracks() = 0;
  // Deprecated: Please use GetVideoTracks.
  virtual VideoTracks* video_tracks() = 0;

 protected:
  virtual ~MediaStreamInterface() {}
};

// Currently there is no difference between LocalMediaStreams and MediaStreams
// but the class is kept since Chrome use this type to distinguish between
// local and remote streams.
// TODO(perkj): Decide if LocalMediaStreamInterface is needed or not once Chrome
// don't care about the type.
class LocalMediaStreamInterface : public MediaStreamInterface {
 public:
};

// MediaConstraintsInterface
// Interface used for passing arguments about media constraints
// to the MediaStream and PeerConnection implementation.
class MediaConstraintsInterface {
 public:
  struct Constraint {
    Constraint() {}
    Constraint(const std::string& key, const std::string value)
        : key(key), value(value) {
    }
    std::string key;
    std::string value;
  };
  typedef std::vector<Constraint> Constraints;

  virtual const Constraints& GetMandatory() const = 0;
  virtual const Constraints& GetOptional() const = 0;

  // Constraint keys used by a local video source.
  // Specified by draft-alvestrand-constraints-resolution-00b
  static const char kMinAspectRatio[];  // minAspectRatio
  static const char kMaxAspectRatio[];  // maxAspectRatio
  static const char kMaxWidth[];  // maxWidth
  static const char kMinWidth[];  // minWidth
  static const char kMaxHeight[];  // maxHeight
  static const char kMinHeight[];  // minHeight
  static const char kMaxFrameRate[];  // maxFrameRate
  static const char kMinFrameRate[];  // minFrameRate

  // Constraint keys used by a local audio source.
  // These keys are google specific.
  static const char kEchoCancellation[];  // googEchoCancellation
  static const char kAutoGainControl[];  // googAutoGainControl
  static const char kNoiseSuppression[];  // googNoiseSuppression
  static const char kHighpassFilter[];  // googHighpassFilter

  // Google-specific constraint keys for a local video source
  static const char kNoiseReduction[];  // googNoiseReduction
  static const char kLeakyBucket[];  // googLeakyBucket

  // Constraint keys for CreateOffer / CreateAnswer
  // Specified by the W3C PeerConnection spec
  static const char kOfferToReceiveVideo[];  // OfferToReceiveVideo
  static const char kOfferToReceiveAudio[];  // OfferToReceiveAudio
  static const char kIceRestart[];  // IceRestart
  // These keys are google specific.
  static const char kUseRtpMux[];  // googUseRtpMUX

  // Constraints values.
  static const char kValueTrue[];  // true
  static const char kValueFalse[];  // false

  // Temporary pseudo-constraints used to enable DTLS-SRTP
  static const char kEnableDtlsSrtp[];  // Enable DTLS-SRTP
  // Temporary pseudo-constraints used to enable DataChannels
  static const char kEnableRtpDataChannels[];  // Enable DataChannels

 protected:
  // Dtor protected as objects shouldn't be deleted via this interface
  virtual ~MediaConstraintsInterface() {}
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_MEDIASTREAMINTERFACE_H_
