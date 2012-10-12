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

#include <string>

#include "talk/app/webrtc/fakeportallocatorfactory.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/session/media/mediasession.h"

static const char kStreamLabel1[] = "local_stream_1";
static const char kStreamLabel2[] = "local_stream_2";
static const char kStreamLabel3[] = "local_stream_3";
static const char kStunConfiguration[] = "STUN stun.l.google.com:19302";
static const char kInvalidConfiguration[] = "a13151913541234:19302";
static const int kDefaultStunPort = 3478;
static const char kStunAddressOnly[] = "STUN address";
static const char kStunInvalidPort[] = "STUN address:-1";
static const char kStunAddressPortAndMore1[] = "STUN address:port:more";
static const char kStunAddressPortAndMore2[] = "STUN address:port more";
static const char kTurnAddressOnly[] = "TURN address";
static const char kTurnInvalidPort[] = "TURN address:-1";
static const char kTurnAddressPortAndMore1[] = "TURN address:port:more";
static const char kTurnAddressPortAndMore2[] = "TURN address:port more";
static const uint32 kTimeout = 5000U;

using talk_base::scoped_ptr;
using talk_base::scoped_refptr;
using webrtc::AudioSourceInterface;
using webrtc::FakePortAllocatorFactory;
using webrtc::IceCandidateInterface;
using webrtc::AudioTrackInterface;
using webrtc::LocalMediaStreamInterface;
using webrtc::MediaStreamInterface;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;
using webrtc::PortAllocatorFactoryInterface;
using webrtc::SessionDescriptionInterface;
using webrtc::VideoSourceInterface;
using webrtc::VideoTrackInterface;

// Gets the first ssrc of given content type from the ContentInfo.
static bool GetFirstSsrc(const cricket::ContentInfo* content_info, int* ssrc) {
  if (!content_info || !ssrc) {
    return false;
  }
  const cricket::MediaContentDescription* media_desc =
      static_cast<const cricket::MediaContentDescription*> (
          content_info->description);
  if (!media_desc || media_desc->streams().empty()) {
    return false;
  }
  *ssrc = media_desc->streams().begin()->first_ssrc();
  return true;
}

class MockPeerConnectionObserver : public PeerConnectionObserver {
 public:
  MockPeerConnectionObserver()
      : renegotiation_needed_(false),
        ice_complete_(false) {
  }
  ~MockPeerConnectionObserver() {
  }
  void SetPeerConnectionInterface(PeerConnectionInterface* pc) {
    pc_ = pc;
    state_ = pc_->ready_state();
  }
  virtual void OnError() {}
  virtual void OnStateChange(StateType state_changed) {
    if (pc_.get() == NULL)
      return;
    switch (state_changed) {
      case kReadyState:
        state_ = pc_->ready_state();
        break;
      case kIceState:
        ADD_FAILURE();
        break;
      default:
        ADD_FAILURE();
        break;
    }
  }
  virtual void OnAddStream(MediaStreamInterface* stream) {
    last_added_stream_ = stream;
  }
  virtual void OnRemoveStream(MediaStreamInterface* stream) {
    last_removed_stream_ = stream;
  }
  virtual void OnRenegotiationNeeded() {
    renegotiation_needed_ = true;
  }
  virtual void OnIceChange() {}
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string sdp;
    EXPECT_TRUE(candidate->ToString(&sdp));
    EXPECT_LT(0u, sdp.size());
    last_candidate_.reset(webrtc::CreateIceCandidate(candidate->sdp_mid(),
        candidate->sdp_mline_index(), sdp));
    EXPECT_TRUE(last_candidate_.get() != NULL);
  }
  virtual void OnIceComplete() {
    ice_complete_ = true;
  }

  // Returns the label of the last added stream.
  // Empty string if no stream have been added.
  std::string GetLastAddedStreamLabel() {
    if (last_added_stream_.get())
      return last_added_stream_->label();
    return "";
  }
  std::string GetLastRemovedStreamLabel() {
    if (last_removed_stream_.get())
      return last_removed_stream_->label();
    return "";
  }

  scoped_refptr<PeerConnectionInterface> pc_;
  PeerConnectionInterface::ReadyState state_;
  scoped_ptr<IceCandidateInterface> last_candidate_;
  bool renegotiation_needed_;
  bool ice_complete_;

 private:
  scoped_refptr<MediaStreamInterface> last_added_stream_;
  scoped_refptr<MediaStreamInterface> last_removed_stream_;
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


class PeerConnectionInterfaceTest : public testing::Test {
 protected:
  virtual void SetUp() {
    port_allocator_factory_ = FakePortAllocatorFactory::Create();

    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        talk_base::Thread::Current(), talk_base::Thread::Current(), NULL);
    ASSERT_TRUE(pc_factory_.get() != NULL);
  }

  void CreatePeerConnection() {
    pc_ = pc_factory_->CreatePeerConnection(kStunConfiguration,
                                            port_allocator_factory_.get(),
                                            &observer_);
    ASSERT_TRUE(pc_.get() != NULL);
    observer_.SetPeerConnectionInterface(pc_.get());
    EXPECT_EQ(PeerConnectionInterface::kNew, observer_.state_);
  }

  void CreatePeerConnectionWithInvalidConfiguration() {
    pc_ = pc_factory_->CreatePeerConnection(kInvalidConfiguration,
                                            port_allocator_factory_.get(),
                                            &observer_);
    ASSERT_TRUE(pc_.get() != NULL);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());
    observer_.SetPeerConnectionInterface(pc_.get());
    EXPECT_EQ(PeerConnectionInterface::kNew, observer_.state_);
  }

  void CreatePeerConnectionWithDifferentConfigurations() {
    pc_ = pc_factory_->CreatePeerConnection(kStunAddressOnly,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(1u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ("address",
        port_allocator_factory_->stun_configs()[0].server.hostname());
    EXPECT_EQ(kDefaultStunPort,
        port_allocator_factory_->stun_configs()[0].server.port());

    pc_ = pc_factory_->CreatePeerConnection(kStunInvalidPort,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    pc_ = pc_factory_->CreatePeerConnection(kStunAddressPortAndMore1,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    pc_ = pc_factory_->CreatePeerConnection(kStunAddressPortAndMore2,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    pc_ = pc_factory_->CreatePeerConnection(kTurnAddressOnly,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(1u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ("address",
        port_allocator_factory_->turn_configs()[0].server.hostname());
    EXPECT_EQ(kDefaultStunPort,
        port_allocator_factory_->turn_configs()[0].server.port());

    pc_ = pc_factory_->CreatePeerConnection(kTurnInvalidPort,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    pc_ = pc_factory_->CreatePeerConnection(kTurnAddressPortAndMore1,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    pc_ = pc_factory_->CreatePeerConnection(kTurnAddressPortAndMore2,
                                            port_allocator_factory_.get(),
                                            &observer_);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());
  }

  void AddStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(label, NULL));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddVoiceStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(label, NULL));
    stream->AddTrack(audio_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddAudioVideoStream(const std::string& stream_label,
                           const std::string& audio_track_label,
                           const std::string& video_track_label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(stream_label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(
            audio_track_label, static_cast<AudioSourceInterface*>(NULL)));
    stream->AddTrack(audio_track.get());
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(video_track_label, NULL));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  bool DoCreateOfferAnswer(SessionDescriptionInterface** desc, bool offer) {
    talk_base::scoped_refptr<MockCreateSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockCreateSessionDescriptionObserver>());
    if (offer) {
      pc_->CreateOffer(observer, NULL);
    } else {
      pc_->CreateAnswer(observer, NULL);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
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
      pc_->SetLocalDescription(observer, desc);
    } else {
      pc_->SetRemoteDescription(observer, desc);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
    return observer->result();
  }

  bool DoSetLocalDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, true);
  }

  bool DoSetRemoteDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, false);
  }

  scoped_refptr<FakePortAllocatorFactory> port_allocator_factory_;
  scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;
  scoped_refptr<PeerConnectionInterface> pc_;
  MockPeerConnectionObserver observer_;
};

TEST_F(PeerConnectionInterfaceTest,
       CreatePeerConnectionWithInvalidConfiguration) {
  CreatePeerConnectionWithInvalidConfiguration();
  AddStream(kStreamLabel1);
}

TEST_F(PeerConnectionInterfaceTest,
       CreatePeerConnectionWithDifferentConfigurations) {
  CreatePeerConnectionWithDifferentConfigurations();
}

TEST_F(PeerConnectionInterfaceTest, AddStreams) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);
  AddVoiceStream(kStreamLabel2);
  ASSERT_EQ(2u, pc_->local_streams()->count());

  // Fail to add another stream with audio since we already have an audio track.
  scoped_refptr<LocalMediaStreamInterface> stream(
      pc_factory_->CreateLocalMediaStream(kStreamLabel3));
  scoped_refptr<AudioTrackInterface> audio_track(
      pc_factory_->CreateAudioTrack(
          kStreamLabel3, static_cast<AudioSourceInterface*>(NULL)));
  stream->AddTrack(audio_track.get());
  EXPECT_FALSE(pc_->AddStream(stream, NULL));

  // Remove the stream with the audio track.
  pc_->RemoveStream(pc_->local_streams()->at(1));

  // Test that we now can add the stream with the audio track.
  EXPECT_TRUE(pc_->AddStream(stream, NULL));
}

TEST_F(PeerConnectionInterfaceTest, RemoveStream) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);
  ASSERT_EQ(1u, pc_->local_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  EXPECT_EQ(0u, pc_->local_streams()->count());
}

TEST_F(PeerConnectionInterfaceTest, InitiateCall) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);

  // SetRemoteDescription takes ownership of offer.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(DoSetRemoteDescription(offer));

  EXPECT_EQ(PeerConnectionInterface::kOpening, observer_.state_);
  // SetLocalDescription takes ownership of answer.
  SessionDescriptionInterface* answer = NULL;
  EXPECT_TRUE(DoCreateAnswer(&answer));
  EXPECT_TRUE(DoSetLocalDescription(answer));
  EXPECT_EQ(PeerConnectionInterface::kActive, observer_.state_);

  // Since we answer with the same session description as we offer we can
  // check if OnAddStream have been called.
  EXPECT_EQ_WAIT(kStreamLabel1, observer_.GetLastAddedStreamLabel(), kTimeout);
}

TEST_F(PeerConnectionInterfaceTest, ReceiveCall) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);

  // SetRemoteDescription takes ownership of offer.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(DoSetRemoteDescription(offer));
  EXPECT_EQ(PeerConnectionInterface::kOpening, observer_.state_);
  // SetLocalDescription takes ownership of answer.
  SessionDescriptionInterface* answer = NULL;
  EXPECT_TRUE(DoCreateAnswer(&answer));
  EXPECT_TRUE(DoSetLocalDescription(answer));
  EXPECT_EQ(PeerConnectionInterface::kActive, observer_.state_);

  // Since we answer with the same session description as we offer we can
  // check if OnAddStream have been called.
  EXPECT_EQ_WAIT(kStreamLabel1, observer_.GetLastAddedStreamLabel(), kTimeout);
}

// Test that candidates are generated and that we can parse our own candidates.
TEST_F(PeerConnectionInterfaceTest, IceCandidates) {
  CreatePeerConnection();

  EXPECT_FALSE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
  // SetRemoteDescription takes ownership of offer.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(DoSetRemoteDescription(offer));

  // SetLocalDescription takes ownership of answer.
  SessionDescriptionInterface* answer = NULL;
  EXPECT_TRUE(DoCreateAnswer(&answer));
  EXPECT_TRUE(DoSetLocalDescription(answer));

  EXPECT_TRUE_WAIT(observer_.last_candidate_.get() != NULL, kTimeout);
  EXPECT_TRUE_WAIT(observer_.ice_complete_, kTimeout);

  EXPECT_TRUE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
}

// Test that the CreateOffer and CreatAnswer will fail if the track labels are
// not unique.
TEST_F(PeerConnectionInterfaceTest, CreateOfferAnswerWithInvalidStream) {
  CreatePeerConnection();
  // Create a regular offer for the CreateAnswer test later.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(offer != NULL);
  delete offer;
  offer = NULL;

  // Create a local stream with audio&video tracks having same label.
  AddAudioVideoStream(kStreamLabel1, "track_label", "track_label");

  // Test CreateOffer
  EXPECT_FALSE(DoCreateOffer(&offer));

  // Test CreateAnswer
  SessionDescriptionInterface* answer = NULL;
  EXPECT_FALSE(DoCreateAnswer(&answer));
}

// Test that we will get different SSRCs for each tracks in the offer and answer
// we created.
TEST_F(PeerConnectionInterfaceTest, SsrcInOfferAnswer) {
  CreatePeerConnection();
  // Create a local stream with audio&video tracks having different labels.
  AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");

  // Test CreateOffer
  scoped_ptr<SessionDescriptionInterface> offer;
  EXPECT_TRUE(DoCreateOffer(offer.use()));
  int audio_ssrc = 0;
  int video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(offer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(offer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);

  // Test CreateAnswer
  EXPECT_TRUE(DoSetRemoteDescription(offer.release()));
  scoped_ptr<SessionDescriptionInterface> answer;
  EXPECT_TRUE(DoCreateAnswer(answer.use()));
  audio_ssrc = 0;
  video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(answer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(answer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);
}
