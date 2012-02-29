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

#ifndef TALK_APP_WEBRTC_WEBRTCSESSION_H_
#define TALK_APP_WEBRTC_WEBRTCSESSION_H_

#include <string>

#include "talk/app/webrtc/candidateobserver.h"
#include "talk/app/webrtc/mediastreamprovider.h"
#include "talk/app/webrtc/sessiondescriptionprovider.h"
#include "talk/base/sigslot.h"
#include "talk/base/thread.h"
#include "talk/p2p/base/session.h"
#include "talk/session/phone/mediasession.h"

namespace cricket {

class ChannelManager;
class Transport;
class VideoCapturer;
class VideoChannel;
class VoiceChannel;

}  // namespace cricket

namespace webrtc {

class WebRtcSession : public cricket::BaseSession,
                      public MediaProviderInterface,
                      public SessionDescriptionProvider {
 public:
  WebRtcSession(cricket::ChannelManager* channel_manager,
                talk_base::Thread* signaling_thread,
                talk_base::Thread* worker_thread,
                cricket::PortAllocator* port_allocator);
  virtual ~WebRtcSession();

  bool Initialize();

  void RegisterObserver(CandidateObserver* observer) {
    observer_ = observer;
  }

  void StartIce();

  const cricket::VoiceChannel* voice_channel() const {
    return voice_channel_.get();
  }
  const cricket::VideoChannel* video_channel() const {
    return video_channel_.get();
  }

  void set_secure_policy(cricket::SecureMediaPolicy secure_policy);
  cricket::SecureMediaPolicy secure_policy() const {
    return session_desc_factory_.secure();
  }

  // Generic error message callback from WebRtcSession.
  // TODO - It may be necessary to supply error code as well.
  sigslot::signal0<> SignalError;

 protected:
  // Implements SessionDescriptionProvider
  virtual cricket::SessionDescription* CreateOffer(
      const cricket::MediaSessionOptions& options);
  virtual cricket::SessionDescription* CreateAnswer(
      const cricket::SessionDescription* offer,
      const cricket::MediaSessionOptions& options);
  virtual bool SetLocalDescription(cricket::SessionDescription* desc,
                                   cricket::ContentAction type);
  virtual bool SetRemoteDescription(cricket::SessionDescription* desc,
                                    cricket::ContentAction type);
  virtual bool AddRemoteCandidate(const std::string& remote_content_name,
                                  const cricket::Candidate& candidate);
  virtual const cricket::SessionDescription* local_description() const {
    return cricket::BaseSession::local_description();
  }
  virtual const cricket::SessionDescription* remote_description() const {
    return cricket::BaseSession::remote_description();
  }

 private:
  virtual void OnMessage(talk_base::Message* msg);

  // Implements MediaProviderInterface.
  virtual bool SetCaptureDevice(const std::string& name,
                                cricket::VideoCapturer* camera);
  virtual void SetLocalRenderer(const std::string& name,
                                cricket::VideoRenderer* renderer);
  virtual void SetRemoteRenderer(const std::string& name,
                                 cricket::VideoRenderer* renderer);

  // Transport related callbacks, override from cricket::BaseSession.
  virtual void OnTransportRequestSignaling(cricket::Transport* transport);
  virtual void OnTransportConnecting(cricket::Transport* transport);
  virtual void OnTransportWritable(cricket::Transport* transport);
  virtual void OnTransportCandidatesReady(
      cricket::Transport* transport,
      const cricket::Candidates& candidates);
  virtual void OnTransportChannelGone(cricket::Transport* transport,
                                      const std::string& name);

  bool CreateChannels();  // Creates channels for voice and video.
  void EnableChannels();  // Enables sending of media.

  talk_base::scoped_ptr<cricket::VoiceChannel> voice_channel_;
  talk_base::scoped_ptr<cricket::VideoChannel> video_channel_;
  cricket::ChannelManager* channel_manager_;
  CandidateObserver* observer_;
  cricket::MediaSessionDescriptionFactory session_desc_factory_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_WEBRTCSESSION_H_
