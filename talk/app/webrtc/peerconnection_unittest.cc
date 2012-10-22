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

#include <stdio.h>

#include <list>

#include "talk/app/webrtc/fakeportallocatorfactory.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/test/fakeaudiocapturemodule.h"
#include "talk/app/webrtc/test/fakevideocapturemodule.h"
#include "talk/app/webrtc/test/fakevideotrackrenderer.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/media/webrtc/webrtcvideocapturer.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessiondescription.h"
#include "talk/session/media/mediasession.h"

using cricket::ContentInfo;
using cricket::MediaContentDescription;
using webrtc::MediaHints;
using webrtc::SessionDescriptionInterface;

void GetAllVideoTracks(webrtc::MediaStreamInterface* media_stream,
                       std::list<webrtc::VideoTrackInterface*>* video_tracks) {
  webrtc::VideoTracks* track_list = media_stream->video_tracks();
  for (size_t i = 0; i < track_list->count(); ++i) {
    webrtc::VideoTrackInterface* track = track_list->at(i);
    video_tracks->push_back(track);
  }
}

// TODO(perkj): Refactor this test to use a cricket::FakeVideoCapturer instead.
static cricket::VideoCapturer* CreateVideoCapturer(webrtc::
                                                   VideoCaptureModule* vcm) {
  cricket::WebRtcVideoCapturer* video_capturer =
      new cricket::WebRtcVideoCapturer;
  if (!video_capturer->Init(vcm)) {
    delete video_capturer;
    video_capturer = NULL;
  }
  return video_capturer;
}

class SignalingMessageReceiver {
 public:
  // Returns the number of rendered frames.
  virtual int num_rendered_frames() = 0;

  // Makes it possible for the remote side to decide when to start capturing.
  // This makes it possible to wait with capturing until a renderer has been
  // added.
  virtual void StartCapturing() = 0;

 protected:
  SignalingMessageReceiver() {}
  virtual ~SignalingMessageReceiver() {}
};

class RoapMessageReceiver : public SignalingMessageReceiver {
 public:
  virtual void ReceiveMessage(const std::string& msg)  = 0;

 protected:
  RoapMessageReceiver() {}
  virtual ~RoapMessageReceiver() {}
};

class JsepMessageReceiver : public SignalingMessageReceiver {
 public:
  virtual void ReceiveSdpMessage(webrtc::JsepInterface::Action action,
                                 std::string& msg) = 0;
  virtual void ReceiveIceMessage(const std::string& sdp_mid,
                                 int sdp_mline_index,
                                 const std::string& msg) = 0;

 protected:
  JsepMessageReceiver() {}
  virtual ~JsepMessageReceiver() {}
};

template <typename MessageReceiver>
class PeerConnectionTestClientBase
    : public webrtc::PeerConnectionObserver,
      public MessageReceiver {
 public:
  ~PeerConnectionTestClientBase() {}

  virtual void StartSession()  = 0;

  void AddMediaStream() {
    if (video_track_) {
      // Tracks have already been set up.
      return;
    }
    // TODO: the default audio device module is used regardless of
    // the second parameter to the CreateLocalAudioTrack(..) call. Pass the
    // fake ADM anyways in case the local track is used in the future.
    talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
        peer_connection_factory_->CreateLocalAudioTrack(
            "audio_track",
            fake_audio_capture_module_));

    CreateLocalVideoTrack();

    talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface> stream =
        peer_connection_factory_->CreateLocalMediaStream("stream_label");

    if (hints_.has_audio()) {
      stream->AddTrack(audio_track);
    }
    if (hints_.has_video()) {
      stream->AddTrack(video_track_);
    }

    EXPECT_TRUE(peer_connection_->AddStream(stream, NULL));
  }

  void StartCapturing() {
    if (fake_video_capture_module_ != NULL) {
      fake_video_capture_module_->StartCapturing();
    }
  }

  bool SessionActive() {
    return peer_connection_->ready_state() ==
        webrtc::PeerConnectionInterface::kActive;
  }

  void StopSession() {
    if (fake_video_capture_module_ != NULL) {
      fake_video_capture_module_->StopCapturing();
    }
  }

  void set_signaling_message_receiver(
      MessageReceiver* signaling_message_receiver) {
    signaling_message_receiver_ = signaling_message_receiver;
  }

  bool AudioFramesReceivedCheck(int number_of_frames) const {
    return number_of_frames < fake_audio_capture_module_->frames_received();
  }

  bool VideoFramesReceivedCheck(int number_of_frames) {
    if (number_of_frames > signaling_message_receiver_->num_rendered_frames()) {
      return false;
    } else {
      EXPECT_LT(number_of_frames, fake_video_capture_module_->sent_frames());
    }
    return true;
  }

  // Verify we got local candidates for each m line.
  bool VerifyLocalCandidates() {
    const cricket::SessionDescription* local_desc =
        peer_connection_->local_description()->description();
    const cricket::SessionDescription* remote_desc =
        peer_connection_->remote_description()->description();
    // The local bundle_group will by default have audio and video in it. So
    // we are more care of what is in the remote bundle group.
    const cricket::ContentGroup* bundle_group =
        remote_desc->GetGroupByName(cricket::GROUP_TYPE_BUNDLE);

    // Use the first accepted content from |bundle_group| as the selected
    // content for bundle.
    std::string selected_content_name;
    if (bundle_group) {
      while (bundle_group->FirstContentName()) {
        const std::string* content_name = bundle_group->FirstContentName();
        if (!local_desc->GetContentByName(*content_name)->rejected &&
            !remote_desc->GetContentByName(*content_name)->rejected) {
          selected_content_name = *content_name;
          break;
        } else {
          const_cast<cricket::ContentGroup*>(bundle_group)->RemoveContentName(
              *content_name);
        }
      }
    }

    // Verify that we have candidates for the accepted content.
    int selected_candidates_index = -1;
    size_t number_of_mediasections =
        peer_connection_->local_description()->number_of_mediasections();
    for (size_t i = 0; i < number_of_mediasections; ++i) {
      const webrtc::IceCandidateCollection* candidates =
          peer_connection_->local_description()->candidates(i);
      if (!local_desc->contents()[i].rejected &&
          !remote_desc->contents()[i].rejected) {
        // If the content is not rejected,
        // we should have some candidates for it.
        EXPECT_GT(candidates->count(), 0u);
        if (local_desc->contents()[i].name == selected_content_name) {
          selected_candidates_index = i;
        }
      } else {
        EXPECT_EQ(0u, candidates->count());
      }
    }

    // Verify that all the contents in the |bundle_group| should have the same
    // candidates as the selected content has.
    if (bundle_group && (selected_candidates_index != -1)) {
      const webrtc::IceCandidateCollection* selected_candidates =
          peer_connection_->local_description()->candidates(
              selected_candidates_index);
      for (size_t i = 0; i < number_of_mediasections; ++i) {
        if (bundle_group->HasContentName(remote_desc->contents()[i].name)) {
          const webrtc::IceCandidateCollection* candidates =
              peer_connection_->local_description()->candidates(i);
          if (!local_desc->contents()[i].rejected &&
              !remote_desc->contents()[i].rejected) {
            // The candidates in |candidates| should be the same as the
            // candidates in |selected_candidates|.
            EXPECT_EQ(selected_candidates->count(), candidates->count());
            // This is assuming the order of the candidates are the same. But
            // for testing, this should be fine.
            for (size_t j = 0; j < candidates->count(); ++j) {
              EXPECT_TRUE(selected_candidates->at(j)->candidate().IsEquivalent(
                  candidates->at(j)->candidate()));
            }
          }
        }
      }
    }
    return true;
  }

  // Verify the CanSendDtmf and SendDtmf interfaces.
  bool VerifySendDtmf() {
    // An invalid audio track can't send dtmf.
    EXPECT_FALSE(peer_connection_->CanSendDtmf(NULL));

    if (hints_.has_audio() && remote_hints_.has_audio()) {
      // The local audio track should be able to send dtmf.
      const webrtc::AudioTrackInterface* send_track =
          peer_connection_->local_streams()->at(0)->audio_tracks()->at(0);
      EXPECT_TRUE(peer_connection_->CanSendDtmf(send_track));

      // The duration can not be more than 6000 or less than 70.
      EXPECT_FALSE(peer_connection_->SendDtmf(send_track, "123,aBc",
                                              30, NULL));
      EXPECT_TRUE(peer_connection_->SendDtmf(send_track, "123,aBc",
                                             100, NULL));

      // Play the dtmf at the same time.
      const webrtc::AudioTrackInterface* play_track =
          peer_connection_->remote_streams()->at(0)->audio_tracks()->at(0);
      EXPECT_TRUE(peer_connection_->SendDtmf(send_track, "123,aBc",
                                             100, play_track));
    }
    return true;
  }

  virtual int num_rendered_frames() {
    if (!fake_video_renderer_) {
      return 0;
    }
    return fake_video_renderer_->num_rendered_frames();
  }

  // PeerConnectionObserver callbacks.
  virtual void OnError() {}
  virtual void OnMessage(const std::string&) {}
  virtual void OnSignalingMessage(const std::string& /*msg*/) {}
  virtual void OnStateChange(StateType /*state_changed*/) {}
  virtual void OnAddStream(webrtc::MediaStreamInterface* media_stream) {
    if (!hints_.has_video() || !remote_hints_.has_video()) {
      return;
    }
    std::list<webrtc::VideoTrackInterface*> video_tracks;
    GetAllVideoTracks(media_stream, &video_tracks);
    // Currently only one video track is supported.
    // TODO: enable multiple video tracks.
    EXPECT_EQ(1u, video_tracks.size());
    if (video_tracks.size() > 0) {
      fake_video_renderer_.reset(
          new webrtc::FakeVideoTrackRenderer(video_tracks.front()));
    }
    // The video renderer has been added. Tell the far end to start capturing
    // frames. That way the number of captured frames should be equal to number
    // of rendered frames.
    if (signaling_message_receiver_ != NULL) {
      signaling_message_receiver_->StartCapturing();
      return;
    }
  }
  virtual void OnRemoveStream(webrtc::MediaStreamInterface* /*media_stream*/) {}
  virtual void OnRenegotiationNeeded() {}
  virtual void OnIceChange() {}
  virtual void OnIceCandidate(
      const webrtc::IceCandidateInterface* /*candidate*/) {}
  virtual void OnIceComplete() {}

 protected:
  PeerConnectionTestClientBase(int id, const MediaHints& hints,
                               const MediaHints& remote_hints)
      : id_(id),
        hints_(hints),
        remote_hints_(remote_hints),
        fake_video_capture_module_(NULL),
        fake_video_renderer_(NULL),
        signaling_message_receiver_(NULL) {
  }
  bool Init() {
    EXPECT_TRUE(!peer_connection_);
    EXPECT_TRUE(!peer_connection_factory_);
    allocator_factory_ = webrtc::FakePortAllocatorFactory::Create();
    if (!allocator_factory_) {
      return false;
    }
    fake_audio_capture_module_ = FakeAudioCaptureModule::Create(
        talk_base::Thread::Current());
    if (fake_audio_capture_module_ == NULL) {
      return false;
    }
    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
        talk_base::Thread::Current(), talk_base::Thread::Current(),
        fake_audio_capture_module_);
    if (!peer_connection_factory_) {
      return false;
    }

    peer_connection_ = CreatePeerConnection(allocator_factory_.get());
    return peer_connection_.get() != NULL;
  }
  virtual talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
      CreatePeerConnection(webrtc::PortAllocatorFactoryInterface* factory) = 0;
  MessageReceiver* signaling_message_receiver() {
    return signaling_message_receiver_;
  }
  webrtc::PeerConnectionFactoryInterface* peer_connection_factory() {
    return peer_connection_factory_.get();
  }
  webrtc::PeerConnectionInterface* peer_connection() {
    return peer_connection_.get();
  }
  const MediaHints& hints() const {
    return hints_;
  }
  const MediaHints& remote_hints() const {
    return remote_hints_;
  }

 private:
  void GenerateRecordingFileName(int track, std::string* file_name) {
    if (file_name == NULL) {
      return;
    }
    std::stringstream file_name_stream;
    file_name_stream << "p2p_test_client_" << id_ << "_videotrack_" << track <<
        ".yuv";
    file_name->clear();
    *file_name = file_name_stream.str();
  }

  void CreateLocalVideoTrack() {
    fake_video_capture_module_ = FakeVideoCaptureModule::Create(
        talk_base::Thread::Current());
    // TODO: Use FakeVideoCapturer instead of FakeVideoCaptureModule.
    video_track_ = peer_connection_factory_->CreateLocalVideoTrack(
        "video_track", CreateVideoCapturer(fake_video_capture_module_));
  }

  int id_;
  MediaHints hints_;
  MediaHints remote_hints_;
  talk_base::scoped_refptr<webrtc::PortAllocatorFactoryInterface>
      allocator_factory_;
  talk_base::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  talk_base::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;

  // Owns and ensures that fake_video_capture_module_ is available as long as
  // this class exists.  It also ensures destruction of the memory associated
  // with it when this class is deleted.
  talk_base::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  // Needed to keep track of number of frames send.
  talk_base::scoped_refptr<FakeAudioCaptureModule> fake_audio_capture_module_;
  FakeVideoCaptureModule* fake_video_capture_module_;
  // Needed to keep track of number of frames received.
  talk_base::scoped_ptr<webrtc::FakeVideoTrackRenderer> fake_video_renderer_;

  // For remote peer communication.
  MessageReceiver* signaling_message_receiver_;
};

class Jsep00TestClient
    : public PeerConnectionTestClientBase<JsepMessageReceiver> {
 public:
  static Jsep00TestClient* CreateClient(int id, const MediaHints& hints,
                                        const MediaHints& remote_hints) {
    Jsep00TestClient* client(new Jsep00TestClient(id, hints, remote_hints));
    if (!client->Init()) {
      delete client;
      return NULL;
    }
    return client;
  }
  ~Jsep00TestClient() {}

  virtual void StartSession() {
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> offer(
        peer_connection()->CreateOffer(MediaHints()));
    std::string sdp;
    EXPECT_TRUE(offer->ToString(&sdp));
    EXPECT_TRUE(peer_connection()->SetLocalDescription(
        webrtc::PeerConnectionInterface::kOffer, offer.release()));
    signaling_message_receiver()->ReceiveSdpMessage(
        webrtc::PeerConnectionInterface::kOffer, sdp);
    peer_connection()->StartIce(webrtc::PeerConnectionInterface::kUseAll);
  }
  // JsepMessageReceiver callback.
  virtual void ReceiveSdpMessage(webrtc::JsepInterface::Action action,
                                 std::string& msg) {
    if (action == webrtc::PeerConnectionInterface::kOffer) {
      HandleIncomingOffer(msg);
    } else {
      HandleIncomingAnswer(msg);
    }
  }
  // JsepMessageReceiver callback.
  virtual void ReceiveIceMessage(const std::string& sdp_mid,
                                 int sdp_mline_index,
                                 const std::string& msg) {
    talk_base::scoped_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, msg));
    EXPECT_TRUE(peer_connection()->ProcessIceMessage(candidate.get()));
  }
  // Implements PeerConnectionObserver functions needed by Jsep.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    LOG(INFO) << "OnIceCandidate " << candidate->sdp_mline_index();
    std::string ice_sdp;
    EXPECT_TRUE(candidate->ToString(&ice_sdp));
    if (signaling_message_receiver() == NULL) {
      // Remote party may be deleted.
      return;
    }
    signaling_message_receiver()->ReceiveIceMessage(candidate->sdp_mid(),
        candidate->sdp_mline_index(), ice_sdp);
  }
  virtual void OnIceComplete() {
    LOG(INFO) << "OnIceComplete";
  }

 protected:
  Jsep00TestClient(int id, const MediaHints& hints,
                   const MediaHints& remote_hints)
    : PeerConnectionTestClientBase<JsepMessageReceiver>(id, hints,
                                                        remote_hints) {}

  virtual talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
      CreatePeerConnection(webrtc::PortAllocatorFactoryInterface* factory) {
    const std::string config = "STUN stun.l.google.com:19302";
    return peer_connection_factory()->CreatePeerConnection(
        config, factory, this);
  }

  void HandleIncomingOffer(const std::string& msg) {
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription(msg));
    if (peer_connection()->local_streams()->count() == 0) {
      // If we are not sending any streams ourselves it is time to add some.
      AddMediaStream();
    }
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> answer(
        peer_connection()->CreateAnswer(hints(),
                                        desc.get()));
    ASSERT_TRUE(answer);
    // Verify that we have the rejected flag set properly.
    if (hints().has_audio() && !remote_hints().has_audio()) {
      const ContentInfo* audio_content =
          GetFirstAudioContent(answer->description());
      const MediaContentDescription* media_desc =
          static_cast<const MediaContentDescription*> (
              audio_content->description);
      EXPECT_TRUE(audio_content->rejected);
      // Below EXPECT_EQ is just our implementation, not required by RFC.
      EXPECT_EQ(cricket::MD_INACTIVE, media_desc->direction());
    }
    if (hints().has_video() && !remote_hints().has_video()) {
      const ContentInfo* video_content =
          GetFirstVideoContent(answer->description());
      const MediaContentDescription* media_desc =
          static_cast<const MediaContentDescription*> (
              video_content->description);
      EXPECT_TRUE(video_content->rejected);
      // Below EXPECT_EQ is just our implementation, not required by RFC.
      EXPECT_EQ(cricket::MD_INACTIVE, media_desc->direction());
    }
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));
    EXPECT_TRUE(peer_connection()->SetRemoteDescription(
        webrtc::PeerConnectionInterface::kOffer,
        desc.release()));
    EXPECT_TRUE(peer_connection()->SetLocalDescription(
        webrtc::PeerConnectionInterface::kAnswer,
        answer.release()));
    if (signaling_message_receiver()) {
      signaling_message_receiver()->ReceiveSdpMessage(
          webrtc::PeerConnectionInterface::kAnswer, sdp);
    }
    peer_connection()->StartIce(webrtc::PeerConnectionInterface::kUseAll);
  }

  void HandleIncomingAnswer(const std::string& msg) {
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription(msg));
    EXPECT_TRUE(peer_connection()->SetRemoteDescription(
        webrtc::PeerConnectionInterface::kAnswer, desc.release()));
  }
};

class MockCreateSessionDescriptionObserver
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  MockCreateSessionDescriptionObserver()
      : called_(false),
        result_(false) {}
  virtual ~MockCreateSessionDescriptionObserver() {}
  virtual void OnSuccess(SessionDescriptionInterface* desc) {
    called_ = true;
    result_ = true;
    desc_.reset(desc);
  }
  virtual void OnFailure(const std::string& error) {
    called_ = true;
    result_ = false;
  }
  bool called() const { return called_; }
  bool result() const { return result_; }
  SessionDescriptionInterface* release_desc() {
    return desc_.release();
  }

 private:
  bool called_;
  bool result_;
  talk_base::scoped_ptr<SessionDescriptionInterface> desc_;
};

class MockSetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
 public:
  MockSetSessionDescriptionObserver()
      : called_(false),
        result_(false) {}
  virtual ~MockSetSessionDescriptionObserver() {}
  virtual void OnSuccess() {
    called_ = true;
    result_ = true;
  }
  virtual void OnFailure(const std::string& error) {
    called_ = true;
    result_ = false;
  }
  bool called() const { return called_; }
  bool result() const { return result_; }

 private:
  bool called_;
  bool result_;
};

class JsepTestClient
    : public PeerConnectionTestClientBase<JsepMessageReceiver> {
 public:
  static JsepTestClient* CreateClient(int id, const MediaHints& hints,
                                      const MediaHints& remote_hints) {
    JsepTestClient* client(new JsepTestClient(id, hints, remote_hints));
    if (!client->Init()) {
      delete client;
      return NULL;
    }
    return client;
  }
  ~JsepTestClient() {}

  virtual void StartSession() {
    talk_base::scoped_ptr<SessionDescriptionInterface> offer;
    EXPECT_TRUE(DoCreateOffer(offer.use()));

    std::string sdp;
    EXPECT_TRUE(offer->ToString(&sdp));
    EXPECT_TRUE(DoSetLocalDescription(offer.release()));
    signaling_message_receiver()->ReceiveSdpMessage(
        webrtc::PeerConnectionInterface::kOffer, sdp);
  }
  // JsepMessageReceiver callback.
  virtual void ReceiveSdpMessage(webrtc::JsepInterface::Action action,
                                 std::string& msg) {
    if (action == webrtc::PeerConnectionInterface::kOffer) {
      HandleIncomingOffer(msg);
    } else {
      HandleIncomingAnswer(msg);
    }
  }
  // JsepMessageReceiver callback.
  virtual void ReceiveIceMessage(const std::string& sdp_mid,
                                 int sdp_mline_index,
                                 const std::string& msg) {
    talk_base::scoped_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, msg));
    EXPECT_TRUE(peer_connection()->AddIceCandidate(candidate.get()));
  }
  // Implements PeerConnectionObserver functions needed by Jsep.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    LOG(INFO) << "OnIceCandidate " << candidate->sdp_mline_index();
    std::string ice_sdp;
    EXPECT_TRUE(candidate->ToString(&ice_sdp));
    if (signaling_message_receiver() == NULL) {
      // Remote party may be deleted.
      return;
    }
    signaling_message_receiver()->ReceiveIceMessage(candidate->sdp_mid(),
        candidate->sdp_mline_index(), ice_sdp);
  }
  virtual void OnIceComplete() {
    LOG(INFO) << "OnIceComplete";
  }

 protected:
  JsepTestClient(int id, const MediaHints& hints,
      const MediaHints& remote_hints)
      : PeerConnectionTestClientBase<JsepMessageReceiver>(id, hints,
                                                          remote_hints) {}

  virtual talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
      CreatePeerConnection(webrtc::PortAllocatorFactoryInterface* factory) {
    // CreatePeerConnection with IceServers.
    webrtc::JsepInterface::IceServers ice_servers;
    webrtc::JsepInterface::IceServer ice_server;
    ice_server.uri = "stun:stun.l.google.com:19302";
    ice_servers.push_back(ice_server);
    return peer_connection_factory()->CreatePeerConnection(ice_servers,
        NULL, factory, this);
  }

  void HandleIncomingOffer(const std::string& msg) {
    if (peer_connection()->local_streams()->count() == 0) {
      // If we are not sending any streams ourselves it is time to add some.
      AddMediaStream();
    }
    talk_base::scoped_ptr<SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription("offer", msg));
    EXPECT_TRUE(DoSetRemoteDescription(desc.release()));
    talk_base::scoped_ptr<SessionDescriptionInterface> answer;
    EXPECT_TRUE(DoCreateAnswer(answer.use()));
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));
    EXPECT_TRUE(DoSetLocalDescription(answer.release()));
    if (signaling_message_receiver()) {
      signaling_message_receiver()->ReceiveSdpMessage(
          webrtc::PeerConnectionInterface::kAnswer, sdp);
    }
  }

  void HandleIncomingAnswer(const std::string& msg) {
    talk_base::scoped_ptr<SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription("answer", msg));
    EXPECT_TRUE(DoSetRemoteDescription(desc.release()));
  }

  bool DoCreateOfferAnswer(SessionDescriptionInterface** desc,
                           bool offer) {
    talk_base::scoped_refptr<MockCreateSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockCreateSessionDescriptionObserver>());
    if (offer) {
      peer_connection()->CreateOffer(observer, NULL);
    } else {
      peer_connection()->CreateAnswer(observer, NULL);
    }
    EXPECT_EQ_WAIT(true, observer->called(), 5000);
    *desc = observer->release_desc();
    return observer->result();
  }

  bool DoCreateOffer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, true);
  }

  bool DoCreateAnswer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, false);
  }

  bool DoSetSessionDescription(SessionDescriptionInterface* desc, bool local) {
    talk_base::scoped_refptr<MockSetSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockSetSessionDescriptionObserver>());
    if (local) {
      peer_connection()->SetLocalDescription(observer, desc);
    } else {
      peer_connection()->SetRemoteDescription(observer, desc);
    }
    EXPECT_EQ_WAIT(true, observer->called(), 5000);
    return observer->result();
  }

  bool DoSetLocalDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, true);
  }

  bool DoSetRemoteDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, false);
  }
};

template <typename SignalingClass>
class P2PTestConductor : public testing::Test {
 public:
  bool SessionActive() {
    return initiating_client_->SessionActive() &&
        receiving_client_->SessionActive();
  }
  // Return true if the number of frames provided have been received or it is
  // known that that will never occur (e.g. no frames will be sent or
  // captured).
  bool FramesNotPending(int audio_frames_to_receive,
                        int video_frames_to_receive) {
    if (!IsInitialized()) {
      return true;
    }
    return VideoFramesReceivedCheck(video_frames_to_receive) &&
        AudioFramesReceivedCheck(audio_frames_to_receive);
  }
  bool AudioFramesReceivedCheck(int frames_received) {
    return initiating_client_->AudioFramesReceivedCheck(frames_received) &&
        receiving_client_->AudioFramesReceivedCheck(frames_received);
  }
  bool VideoFramesReceivedCheck(int frames_received) {
    return initiating_client_->VideoFramesReceivedCheck(frames_received) &&
        receiving_client_->VideoFramesReceivedCheck(frames_received);
  }
  bool VerifyLocalCandidates() {
    return initiating_client_->VerifyLocalCandidates() &&
        receiving_client_->VerifyLocalCandidates();
  }
  bool VerifySendDtmf() {
    return initiating_client_->VerifySendDtmf() &&
        receiving_client_->VerifySendDtmf();
  }
  ~P2PTestConductor() {
    if (initiating_client_) {
      initiating_client_->set_signaling_message_receiver(NULL);
    }
    if (receiving_client_) {
      receiving_client_->set_signaling_message_receiver(NULL);
    }
  }

  bool CreateTestClients(const MediaHints& hints0, const MediaHints& hints1) {
    initiating_client_.reset(SignalingClass::CreateClient(0, hints0, hints1));
    receiving_client_.reset(SignalingClass::CreateClient(1, hints1, hints0));
    if (!initiating_client_ || !receiving_client_) {
      return false;
    }
    initiating_client_->set_signaling_message_receiver(receiving_client_.get());
    receiving_client_->set_signaling_message_receiver(initiating_client_.get());
    return true;
  }

  bool StartSession() {
    if (!IsInitialized()) {
      return false;
    }
    initiating_client_->AddMediaStream();
    initiating_client_->StartSession();
    return true;
  }

  bool StopSession() {
    if (!IsInitialized()) {
      return false;
    }
    initiating_client_->StopSession();
    receiving_client_->StopSession();
    return true;
  }

  // This test sets up a call between two parties. Both parties send static
  // frames to each other. Once the test is finished the number of sent frames
  // is compared to the number of received frames.
  void LocalP2PTest(const MediaHints& hints0, const MediaHints& hints1) {
    ASSERT_TRUE(CreateTestClients(hints0, hints1));
    EXPECT_TRUE(StartSession());
    const int kMaxWaitForActivationMs = 5000;
    // Assert true is used here since next tests are guaranteed to fail and
    // would eat up 5 seconds.
    ASSERT_TRUE(IsInitialized());
    ASSERT_TRUE_WAIT(SessionActive(), kMaxWaitForActivationMs);

    int kEndAudioFrameCount = 10;
    int kEndVideoFrameCount = 10;
    const int kMaxWaitForFramesMs = 5000;
    // TODO(ronghuawu): Add test to covert the case of sendonly and recvonly.
    if (!hints0.has_audio() || !hints1.has_audio()) {
      kEndAudioFrameCount = -1;
    }
    if (!hints0.has_video() || !hints1.has_video()) {
      kEndVideoFrameCount = -1;
    }
    EXPECT_TRUE_WAIT(FramesNotPending(kEndAudioFrameCount, kEndVideoFrameCount),
                     kMaxWaitForFramesMs);
    const int kMaxWaitForCandidatesMs = 5000;
    EXPECT_TRUE_WAIT(VerifyLocalCandidates(), kMaxWaitForCandidatesMs);
    VerifySendDtmf();
    EXPECT_TRUE(StopSession());
  }

 private:
  bool IsInitialized() const {
    return (initiating_client_ && receiving_client_);
  }

  talk_base::scoped_ptr<SignalingClass> initiating_client_;
  talk_base::scoped_ptr<SignalingClass> receiving_client_;
};

typedef P2PTestConductor<Jsep00TestClient> Jsep00PeerConnectionP2PTestClient;
typedef P2PTestConductor<JsepTestClient> JsepPeerConnectionP2PTestClient;

// This test sets up a Jsep call with deprecated jsep apis between two parties.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTest) {
  LocalP2PTest(MediaHints(), MediaHints());
}

// This test sets up a Jsep call between two parties, and the callee only
// has video.
// TODO(ronghuawu): Add these tests for Jsep01 once the
// MediaConstraintsInterface is ready.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerVideo) {
  LocalP2PTest(MediaHints(), MediaHints(false, true));
}

// This test sets up a Jsep call between two parties, and the callee only
// has audio.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerAudio) {
  LocalP2PTest(MediaHints(), MediaHints(true, false));
}

// This test sets up a Jsep call between two parties, and the callee has neither
// audio or video.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerNone) {
  LocalP2PTest(MediaHints(), MediaHints(false, false));
}

// This test sets up a Jsep call between two parties.
TEST_F(JsepPeerConnectionP2PTestClient, LocalP2PTest) {
  LocalP2PTest(MediaHints(), MediaHints());
}
