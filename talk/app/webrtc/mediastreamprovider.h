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

#ifndef TALK_APP_WEBRTC_MEDIASTREAMPROVIDER_H_
#define TALK_APP_WEBRTC_MEDIASTREAMPROVIDER_H_

namespace cricket {

struct AudioOptions;
struct VideoOptions;
class VideoCapturer;
class VideoRenderer;

}  // namespace cricket

namespace webrtc {

// This interface is called by AudioTrackHandler classes in mediastreamhandler.h
// to change the settings of an audio track connected to certain PeerConnection.
class AudioProviderInterface {
 public:
  // Enable/disable the audio playout of a remote audio track with name |name|.
  virtual void SetAudioPlayout(const std::string& name, bool enable) = 0;
  // Enable/disable sending audio on the local audio track with name |name|.
  // When |enable| is true |options| should be applied to the audio track.
  virtual void SetAudioSend(const std::string& name, bool enable,
                            const cricket::AudioOptions& options) = 0;

 protected:
  virtual ~AudioProviderInterface() {}
};

// This interface is called by VideoTrackHandler classes in mediastreamhandler.h
// to change the settings of a video track connected to a certain
// PeerConnection.
class VideoProviderInterface {
 public:
  virtual bool SetCaptureDevice(const std::string& name,
                                cricket::VideoCapturer* camera) = 0;
  // Enable/disable the video playout of a remote video track with name |name|.
  virtual void SetVideoPlayout(const std::string& name,
                               bool enable,
                               cricket::VideoRenderer* renderer) = 0;
  // Enable sending video on the local video track with name |name|.
  virtual void SetVideoSend(const std::string& name, bool enable,
                            const cricket::VideoOptions* options) = 0;

 protected:
  virtual ~VideoProviderInterface() {}
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_MEDIASTREAMPROVIDER_H_
