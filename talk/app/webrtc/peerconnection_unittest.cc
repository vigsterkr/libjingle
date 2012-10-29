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
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/test/fakevideotrackrenderer.h"
#include "talk/app/webrtc/test/fakeperiodicvideocapturer.h"
#include "talk/app/webrtc/videosourceinterface.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessiondescription.h"
#include "talk/session/media/mediasession.h"

using cricket::ContentInfo;
using cricket::MediaContentDescription;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaHints;
using webrtc::SessionDescriptionInterface;

static const int kMaxWaitMs = 1000;

static void GetAllVideoTracks(
    webrtc::MediaStreamInterface* media_stream,
    std::list<webrtc::VideoTrackInterface*>* video_tracks) {
  webrtc::VideoTracks* track_list = media_stream->video_tracks();
  for (size_t i = 0; i < track_list->count(); ++i) {
    webrtc::VideoTrackInterface* track = track_list->at(i);
    video_tracks->push_back(track);
  }
}

class SignalingMessageReceiver {
 public:
 protected:
  SignalingMessageReceiver() {}
  virtual ~SignalingMessageReceiver() {}
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

  virtual void SetVideoConstraints(
      const webrtc::FakeConstraints& video_constraint) {
    video_constraints_ = video_constraint;
  }

  void AddMediaStream() {
    if (video_track_) {
      // Tracks have already been set up.
      return;
    }
    talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface> stream =
        peer_connection_factory_->CreateLocalMediaStream("stream_label");

    if (can_receive_audio()) {
      // TODO(perkj): Test audio source when it is implemented. Currently audio
      // always use the default input.
      talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
              peer_connection_factory_->CreateAudioTrack("audio_track", NULL));
      stream->AddTrack(audio_track);
    }
    if (can_receive_video()) {
      CreateLocalVideoTrack();
      stream->AddTrack(video_track_);
    }

    EXPECT_TRUE(peer_connection_->AddStream(stream, NULL));
  }

  bool SessionActive() {
    return peer_connection_->ready_state() ==
        webrtc::PeerConnectionInterface::kActive;
  }

  void set_signaling_message_receiver(
      MessageReceiver* signaling_message_receiver) {
    signaling_message_receiver_ = signaling_message_receiver;
  }

  bool AudioFramesReceivedCheck(int number_of_frames) const {
    return number_of_frames <= fake_audio_capture_module_->frames_received();
  }

  bool VideoFramesReceivedCheck(int number_of_frames) {
    if (!fake_video_renderer_) {
      return number_of_frames <= 0;
    }
    return number_of_frames <= fake_video_renderer_->num_rendered_frames();
  }

  // Verify the CanSendDtmf and SendDtmf interfaces.
  void VerifySendDtmf() {
    // An invalid audio track can't send dtmf.
    EXPECT_FALSE(peer_connection_->CanSendDtmf(NULL));

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
    // TODO(perkj): Talk to ronghuawu about how to verify if a DTMF tone is
    // received or not.
  }

  int rendered_width() {
    EXPECT_TRUE(fake_video_renderer_.get() != NULL);
    return fake_video_renderer_ ? fake_video_renderer_->width() : 1;
  }

  int rendered_height() {
    EXPECT_TRUE(fake_video_renderer_.get() != NULL);
    return fake_video_renderer_ ? fake_video_renderer_->height() : 1;
  }

  // PeerConnectionObserver callbacks.
  virtual void OnError() {}
  virtual void OnMessage(const std::string&) {}
  virtual void OnSignalingMessage(const std::string& /*msg*/) {}
  virtual void OnStateChange(StateType /*state_changed*/) {}
  virtual void OnAddStream(webrtc::MediaStreamInterface* media_stream) {
    std::list<webrtc::VideoTrackInterface*> video_tracks;
    GetAllVideoTracks(media_stream, &video_tracks);
    // Currently only one video track is supported.
    // TODO: enable multiple video tracks.
    if (video_tracks.size() > 0) {
      fake_video_renderer_.reset(
          new webrtc::FakeVideoTrackRenderer(video_tracks.front()));
    }
  }
  virtual void OnRemoveStream(webrtc::MediaStreamInterface* /*media_stream*/) {}
  virtual void OnRenegotiationNeeded() {}
  virtual void OnIceChange() {}
  virtual void OnIceCandidate(
      const webrtc::IceCandidateInterface* /*candidate*/) {}
  virtual void OnIceComplete() {}

 protected:
  explicit PeerConnectionTestClientBase(const std::string& id)
      : id_(id),
        periodic_video_capturer_(NULL),
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
  virtual bool can_receive_audio() = 0;
  virtual bool can_receive_video() = 0;
  const std::string& id() const { return id_; }

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
    periodic_video_capturer_ = new webrtc::FakePeriodicVideoCapturer();
    talk_base::scoped_refptr<webrtc::VideoSourceInterface> source =
        peer_connection_factory_->CreateVideoSource(periodic_video_capturer_,
                                                    &video_constraints_);
    video_track_ = peer_connection_factory_->CreateVideoTrack(
        "video_track", source);
  }

  std::string id_;
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
  webrtc::FakePeriodicVideoCapturer* periodic_video_capturer_;
  // Needed to keep track of number of frames received.
  talk_base::scoped_ptr<webrtc::FakeVideoTrackRenderer> fake_video_renderer_;
  webrtc::FakeConstraints video_constraints_;

  // For remote peer communication.
  MessageReceiver* signaling_message_receiver_;
};

class Jsep00TestClient
    : public PeerConnectionTestClientBase<JsepMessageReceiver> {
 public:
  static Jsep00TestClient* CreateClient(const std::string& id) {
    Jsep00TestClient* client(new Jsep00TestClient(id));
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
    LOG(INFO) << id() << "ReceiveIceMessage ";
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

  virtual bool can_receive_audio() { return hints_.has_audio(); }
  virtual bool can_receive_video() { return hints_.has_video(); }

  void SetHints(const MediaHints& local, const MediaHints& remote) {
    hints_ = local;
    remote_hints_ = remote;
  }

  virtual void OnIceComplete() {
    LOG(INFO) << "OnIceComplete";
  }

 protected:
  explicit Jsep00TestClient(const std::string& id)
    : PeerConnectionTestClientBase<JsepMessageReceiver>(id) {}

  virtual talk_base::scoped_refptr<webrtc::PeerConnectionInterface>
      CreatePeerConnection(webrtc::PortAllocatorFactoryInterface* factory) {
    const std::string config = "STUN stun.l.google.com:19302";
    return peer_connection_factory()->CreatePeerConnection(
        config, factory, this);
  }

  void HandleIncomingOffer(const std::string& msg) {
    LOG(INFO) << id() << "HandleIncomingOffer ";
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription(msg));
    if (peer_connection()->local_streams()->count() == 0) {
      // If we are not sending any streams ourselves it is time to add some.
      AddMediaStream();
    }
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> answer(
        peer_connection()->CreateAnswer(hints_,
                                        desc.get()));
    ASSERT_TRUE(answer);
    // Verify that we have the rejected flag set properly.
    if (hints_.has_audio() && !remote_hints_.has_audio()) {
      const ContentInfo* audio_content =
          GetFirstAudioContent(answer->description());
      const MediaContentDescription* media_desc =
          static_cast<const MediaContentDescription*> (
              audio_content->description);
      EXPECT_TRUE(audio_content->rejected);
      // Below EXPECT_EQ is just our implementation, not required by RFC.
      EXPECT_EQ(cricket::MD_INACTIVE, media_desc->direction());
    }
    if (hints_.has_video() && !remote_hints_.has_video()) {
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
    LOG(INFO) << id() << "HandleIncomingOffer ";
    talk_base::scoped_ptr<webrtc::SessionDescriptionInterface> desc(
           webrtc::CreateSessionDescription(msg));
    EXPECT_TRUE(peer_connection()->SetRemoteDescription(
        webrtc::PeerConnectionInterface::kAnswer, desc.release()));
  }

 private:
  MediaHints hints_;
  MediaHints remote_hints_;
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
  static JsepTestClient* CreateClient(const std::string& id) {
    JsepTestClient* client(new JsepTestClient(id));
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
    LOG(INFO) << id() << "ReceiveIceMessage";
    talk_base::scoped_ptr<webrtc::IceCandidateInterface> candidate(
        webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, msg));
    EXPECT_TRUE(peer_connection()->AddIceCandidate(candidate.get()));
  }
  // Implements PeerConnectionObserver functions needed by Jsep.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    LOG(INFO) << id() << "OnIceCandidate";
    std::string ice_sdp;
    EXPECT_TRUE(candidate->ToString(&ice_sdp));
    if (signaling_message_receiver() == NULL) {
      // Remote party may be deleted.
      return;
    }
    signaling_message_receiver()->ReceiveIceMessage(candidate->sdp_mid(),
        candidate->sdp_mline_index(), ice_sdp);
  }

  // TODO(perkj) Implement constraints for rejecting audio.
  virtual bool can_receive_audio() { return true; }
  // TODO(perkj) Implement constraints for rejecting video.
  virtual bool can_receive_video() { return true; }

  virtual void OnIceComplete() {
    LOG(INFO) << id() << "OnIceComplete";
  }

 protected:
  explicit JsepTestClient(const std::string& id)
      : PeerConnectionTestClientBase<JsepMessageReceiver>(id) {}

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
    LOG(INFO) << id() << "HandleIncomingOffer ";
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
    LOG(INFO) << id() << "HandleIncomingAnswer";
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
    EXPECT_EQ_WAIT(true, observer->called(), kMaxWaitMs);
    *desc = observer->release_desc();
    return observer->result();
  }

  bool DoCreateOffer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, true);
  }

  bool DoCreateAnswer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, false);
  }

  bool DoSetLocalDescription(SessionDescriptionInterface* desc) {
    talk_base::scoped_refptr<MockSetSessionDescriptionObserver>
            observer(new talk_base::RefCountedObject<
                MockSetSessionDescriptionObserver>());
    LOG(INFO) << id() << "SetLocalDescription ";
    peer_connection()->SetLocalDescription(observer, desc);
    // Ignore the observer result. If we wait for the result with
    // EXPECT_TRUE_WAIT, local ice candidates might be sent to the remote peer
    // before the offer which is an error.
    // The reason is that EXPECT_TRUE_WAIT uses
    // talk_base::Thread::Current()->ProcessMessages(1);
    // ProcessMessages waits at least 1ms but processes all messages before
    // returning. Since this test is synchronous and send messages to the remote
    // peer whenever a callback is invoked, this can lead to messages being
    // sent to the remote peer in the wrong order.
    // TODO(perkj): Find a way to check the result without risking that the
    // order of sent messages are changed. Ex- by posting all messages that are
    // sent to the remote peer.
    return true;
  }

  bool DoSetRemoteDescription(SessionDescriptionInterface* desc) {
    talk_base::scoped_refptr<MockSetSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockSetSessionDescriptionObserver>());
    LOG(INFO) << id() << "SetRemoteDescription ";
    peer_connection()->SetRemoteDescription(observer, desc);
    EXPECT_TRUE_WAIT(observer->called(), kMaxWaitMs);
    return observer->result();
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
  void VerifySendDtmf() {
    initiating_client_->VerifySendDtmf();
    receiving_client_->VerifySendDtmf();
  }

  void VerifyRenderedSize(int width, int height) {
    EXPECT_EQ(width, receiving_client()->rendered_width());
    EXPECT_EQ(height, receiving_client()->rendered_height());
    EXPECT_EQ(width, initializing_client()->rendered_width());
    EXPECT_EQ(height, initializing_client()->rendered_height());
  }

  ~P2PTestConductor() {
    if (initiating_client_) {
      initiating_client_->set_signaling_message_receiver(NULL);
    }
    if (receiving_client_) {
      receiving_client_->set_signaling_message_receiver(NULL);
    }
  }

  bool CreateTestClients() {
    initiating_client_.reset(SignalingClass::CreateClient("Caller: "));
    receiving_client_.reset(SignalingClass::CreateClient("Callee: "));
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

  void SetHints(const MediaHints& init_hints,
                const MediaHints& receiving_hints) {
    initiating_client_->SetHints(init_hints, receiving_hints);
    receiving_client_->SetHints(receiving_hints, init_hints);
  }

  void SetVideoConstraints(const webrtc::FakeConstraints& init_constraints,
                           const webrtc::FakeConstraints& recv_constraints) {
    initiating_client_->SetVideoConstraints(init_constraints);
    receiving_client_->SetVideoConstraints(recv_constraints);
  }

  // This test sets up a call between two parties. Both parties send static
  // frames to each other. Once the test is finished the number of sent frames
  // is compared to the number of received frames.
  void LocalP2PTest() {
    EXPECT_TRUE(StartSession());
    const int kMaxWaitForActivationMs = 5000;
    // Assert true is used here since next tests are guaranteed to fail and
    // would eat up 5 seconds.
    ASSERT_TRUE(IsInitialized());
    ASSERT_TRUE_WAIT(SessionActive(), kMaxWaitForActivationMs);

    int kEndAudioFrameCount = 10;
    int kEndVideoFrameCount = 10;
    const int kMaxWaitForFramesMs = 5000;
    // TODO(ronghuawu): Add test to cover the case of sendonly and recvonly.
    if (!initiating_client_->can_receive_audio() ||
        !receiving_client_->can_receive_audio()) {
      kEndAudioFrameCount = -1;
    }
    if (!initiating_client_->can_receive_video() ||
        !receiving_client_->can_receive_video()) {
      kEndVideoFrameCount = -1;
    }
    EXPECT_TRUE_WAIT(FramesNotPending(kEndAudioFrameCount, kEndVideoFrameCount),
                     kMaxWaitForFramesMs);
  }

  SignalingClass* initializing_client() { return initiating_client_.get(); }
  SignalingClass* receiving_client() { return receiving_client_.get(); }

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
  ASSERT_TRUE(CreateTestClients());
  LocalP2PTest();
}

// This test sets up a Jsep call between two parties, and the callee only
// has video.
// TODO(ronghuawu): Add these tests for Jsep01 once the
// MediaConstraintsInterface is ready.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerVideo) {
  ASSERT_TRUE(CreateTestClients());
  SetHints(MediaHints(), MediaHints(false, true));
  LocalP2PTest();
}

// This test sets up a Jsep call between two parties, and the callee only
// has audio.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerAudio) {
  ASSERT_TRUE(CreateTestClients());
  SetHints(MediaHints(), MediaHints(true, false));
  LocalP2PTest();
}

// This test sets up a Jsep call between two parties, and the callee has neither
// audio or video.
TEST_F(Jsep00PeerConnectionP2PTestClient, LocalP2PTestAnswerNone) {
  ASSERT_TRUE(CreateTestClients());
  SetHints(MediaHints(), MediaHints(false, false));
  LocalP2PTest();
}

TEST_F(JsepPeerConnectionP2PTestClient, LocalP2PTestDtmf) {
  ASSERT_TRUE(CreateTestClients());
  LocalP2PTest();
  VerifySendDtmf();
  VerifyRenderedSize(640, 480);
}

TEST_F(JsepPeerConnectionP2PTestClient, LocalP2PTest16To9) {
  ASSERT_TRUE(CreateTestClients());
  webrtc::FakeConstraints constraint;
  double requested_ratio = 640.0/360;
  constraint.SetMandatoryMinAspectRatio(requested_ratio);
  SetVideoConstraints(constraint, constraint);
  LocalP2PTest();

  ASSERT_LE(0, initializing_client()->rendered_height());
  double initiating_video_ratio =
      static_cast<double> (initializing_client()->rendered_width()) /
      initializing_client()->rendered_height();
  EXPECT_LE(requested_ratio, initiating_video_ratio);

  ASSERT_LE(0, receiving_client()->rendered_height());
  double receiving_video_ratio =
      static_cast<double> (receiving_client()->rendered_width()) /
      receiving_client()->rendered_height();
  EXPECT_LE(requested_ratio, receiving_video_ratio);
}

// This test sets up a Jsep call between two parties and test that the
// received video has a resolution of 1280*720.
// TODO(mallinath): Enable when
// http://code.google.com/p/webrtc/issues/detail?id=981 is fixed.
TEST_F(JsepPeerConnectionP2PTestClient, DISABLED_LocalP2PTest1280By720) {
  ASSERT_TRUE(CreateTestClients());
  webrtc::FakeConstraints constraint;
  constraint.SetMandatoryMinWidth(1280);
  constraint.SetMandatoryMinHeight(720);
  SetVideoConstraints(constraint, constraint);
  LocalP2PTest();
  VerifyRenderedSize(1280, 720);
}
