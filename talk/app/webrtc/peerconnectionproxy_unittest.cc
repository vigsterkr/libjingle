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

#include "talk/app/webrtc/peerconnectionproxy.h"

#include <string>

#include "talk/base/refcount.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/base/gunit.h"
#include "testing/base/public/gmock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;

namespace webrtc {

class MockPeerConnection : public PeerConnectionInterface {
 public:
  MOCK_METHOD0(local_streams,
      talk_base::scoped_refptr<StreamCollectionInterface>());
  MOCK_METHOD0(remote_streams,
      talk_base::scoped_refptr<StreamCollectionInterface>());
  MOCK_METHOD1(AddStream,
      void(LocalMediaStreamInterface* stream));
  MOCK_METHOD2(AddStream,
      bool(MediaStreamInterface* stream,
          const MediaConstraintsInterface* constraints));
  MOCK_METHOD1(RemoveStream,
      void(MediaStreamInterface* stream));
  MOCK_METHOD1(CreateDtmfSender,
      talk_base::scoped_refptr<DtmfSenderInterface>(
          AudioTrackInterface* track));
  MOCK_METHOD2(GetStats,
      bool(StatsObserver* observer, MediaStreamTrackInterface* track));
  MOCK_METHOD0(ready_state,
        ReadyState());
  MOCK_METHOD0(signaling_state,
      SignalingState());
  MOCK_METHOD0(ice_state,
      IceState());
  MOCK_METHOD0(ice_connection_state,
      IceConnectionState());
  MOCK_METHOD0(ice_gathering_state,
      IceGatheringState());
  MOCK_METHOD2(CreateDataChannel,
        talk_base::scoped_refptr<DataChannelInterface>(
            const std::string& label,
            const DataChannelInit* config));
  MOCK_CONST_METHOD0(local_description,
      const SessionDescriptionInterface*());
  MOCK_CONST_METHOD0(remote_description,
      const SessionDescriptionInterface*());
  MOCK_METHOD2(CreateOffer,
      void(CreateSessionDescriptionObserver* observer,
           const MediaConstraintsInterface* constraints));
  MOCK_METHOD2(CreateAnswer,
      void(CreateSessionDescriptionObserver* observer,
           const MediaConstraintsInterface* constraints));
  MOCK_METHOD2(SetLocalDescription,
      void(SetSessionDescriptionObserver* observer,
           SessionDescriptionInterface* desc));
  MOCK_METHOD2(SetRemoteDescription,
      void(SetSessionDescriptionObserver* observer,
           SessionDescriptionInterface* desc));
  MOCK_METHOD2(UpdateIce,
      bool(const IceServers& configuration,
           const MediaConstraintsInterface* constraints));
  MOCK_METHOD1(AddIceCandidate,
      bool(const IceCandidateInterface* candidate));

 protected:
  MockPeerConnection() {}
  ~MockPeerConnection() {}
};

class PeerConnectionProxyTest: public testing::Test {
 public:
  PeerConnectionProxyTest()
      : dummy1_(new talk_base::RefCountedObject<DummyRefCountedObject>()),
        dummy2_(new talk_base::RefCountedObject<DummyRefCountedObject>()),
        fake_pointer1_(dummy1_.get()),
        fake_pointer2_(dummy2_.get()) {
  }

  // Checks that the functions is called on the |signaling_thread|.
  void CheckThread() {
    EXPECT_EQ(talk_base::Thread::Current(), signaling_thread_.get());
  }

  // Returns an arbitrary pointer type. The pointer may not be used but can be
  // reference counted. Ie - the returned pointer implements RefCountInterface
  // and can be used in a talk_base::scoped_refptr.
  template <typename T>
  T* GetFakePointer1() {
    return static_cast<T*>(fake_pointer1_);
  }
  template <typename T>
  T* GetFakePointer2() {
    return static_cast<T*>(fake_pointer2_);
  }

 protected:
  virtual void SetUp() {
    signaling_thread_.reset(new talk_base::Thread());
    ASSERT_TRUE(signaling_thread_->Start());
    pc_ = new talk_base::RefCountedObject<MockPeerConnection>();
    pc_proxy_ = PeerConnectionProxy::Create(signaling_thread_.get(), pc_);
  }

 protected:
  class DummyRefCountedObject : public talk_base::RefCountInterface {
  };

  talk_base::scoped_ptr<talk_base::Thread> signaling_thread_;
  talk_base::scoped_refptr<PeerConnectionInterface> pc_proxy_;
  talk_base::scoped_refptr<MockPeerConnection> pc_;
  talk_base::scoped_refptr<DummyRefCountedObject> dummy1_;
  talk_base::scoped_refptr<DummyRefCountedObject> dummy2_;
  void* fake_pointer1_;
  void* fake_pointer2_;
};

TEST_F(PeerConnectionProxyTest, local_streams) {
  EXPECT_CALL(*pc_, local_streams())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(GetFakePointer1<StreamCollectionInterface>())));
  EXPECT_EQ(GetFakePointer1<StreamCollectionInterface>(),
            pc_proxy_->local_streams());
}

TEST_F(PeerConnectionProxyTest, remote_streams) {
  EXPECT_CALL(*pc_, remote_streams())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(GetFakePointer1<StreamCollectionInterface>())));
  EXPECT_EQ(GetFakePointer1<StreamCollectionInterface>(),
              pc_proxy_->remote_streams());
}

TEST_F(PeerConnectionProxyTest, AddStream) {
  MediaStreamInterface* fake_stream =
      GetFakePointer1<MediaStreamInterface>();
  MediaConstraintsInterface* constraints =
      GetFakePointer2<MediaConstraintsInterface>();
  EXPECT_CALL(*pc_, AddStream(fake_stream, constraints))
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(true)));
  EXPECT_TRUE(pc_proxy_->AddStream(fake_stream, constraints));
}

TEST_F(PeerConnectionProxyTest, RemoveStream) {
  MediaStreamInterface* fake_stream =
      GetFakePointer1<MediaStreamInterface>();
  EXPECT_CALL(*pc_, RemoveStream(fake_stream))
      .Times(Exactly(1))
      .WillOnce(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread));
  pc_proxy_->RemoveStream(fake_stream);
}

TEST_F(PeerConnectionProxyTest, CreateDtmfSender) {
  AudioTrackInterface* fake_track =
      GetFakePointer1<AudioTrackInterface>();
  EXPECT_CALL(*pc_, CreateDtmfSender(fake_track))
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(GetFakePointer2<DtmfSenderInterface>())));
  EXPECT_EQ(GetFakePointer2<DtmfSenderInterface>(),
            pc_proxy_->CreateDtmfSender(fake_track));
}

TEST_F(PeerConnectionProxyTest, GetStats) {
  StatsObserver* fake_stats_observer =
      GetFakePointer1<StatsObserver>();
  MediaStreamTrackInterface* fake_track =
      GetFakePointer2<MediaStreamTrackInterface>();
  EXPECT_CALL(*pc_, GetStats(fake_stats_observer, fake_track))
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(true)));
  EXPECT_TRUE(pc_proxy_->GetStats(fake_stats_observer, fake_track));
}

TEST_F(PeerConnectionProxyTest, signaling_state) {
  EXPECT_CALL(*pc_, signaling_state())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(PeerConnectionInterface::kStable)));
  EXPECT_EQ(PeerConnectionInterface::kStable, pc_proxy_->signaling_state());
}

TEST_F(PeerConnectionProxyTest, ice_state) {
  EXPECT_CALL(*pc_, ice_state())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(PeerConnectionInterface::kIceCompleted)));
  EXPECT_EQ(PeerConnectionInterface::kIceCompleted, pc_proxy_->ice_state());
}

TEST_F(PeerConnectionProxyTest, ice_connection_state) {
  EXPECT_CALL(*pc_, ice_connection_state())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(PeerConnectionInterface::kIceConnectionCompleted)));
  EXPECT_EQ(PeerConnectionInterface::kIceConnectionCompleted,
            pc_proxy_->ice_connection_state());
}

TEST_F(PeerConnectionProxyTest, ice_gathering_state) {
  EXPECT_CALL(*pc_, ice_gathering_state())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(PeerConnectionInterface::kIceGatheringGathering)));
  EXPECT_EQ(PeerConnectionInterface::kIceGatheringGathering,
            pc_proxy_->ice_gathering_state());
}

TEST_F(PeerConnectionProxyTest, local_description) {
  EXPECT_CALL(*pc_, local_description())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(GetFakePointer1<SessionDescriptionInterface>())));
  EXPECT_EQ(GetFakePointer1<SessionDescriptionInterface>(),
            pc_proxy_->local_description());
}

TEST_F(PeerConnectionProxyTest, remote_description) {
  EXPECT_CALL(*pc_, remote_description())
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(GetFakePointer1<SessionDescriptionInterface>())));
  EXPECT_EQ(GetFakePointer1<SessionDescriptionInterface>(),
            pc_proxy_->remote_description());
}

TEST_F(PeerConnectionProxyTest, CreateOffer) {
  CreateSessionDescriptionObserver* fake_observer =
      GetFakePointer1<CreateSessionDescriptionObserver>();
  MediaConstraintsInterface* constraints =
      GetFakePointer2<MediaConstraintsInterface>();
  EXPECT_CALL(*pc_, CreateOffer(fake_observer, constraints))
      .Times(Exactly(1))
      .WillOnce(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread));
  pc_proxy_->CreateOffer(fake_observer, constraints);
}

TEST_F(PeerConnectionProxyTest, CreateAnswer) {
  CreateSessionDescriptionObserver* fake_observer =
      GetFakePointer1<CreateSessionDescriptionObserver>();
  MediaConstraintsInterface* constraints =
      GetFakePointer2<MediaConstraintsInterface>();
  EXPECT_CALL(*pc_, CreateAnswer(fake_observer, constraints))
      .Times(Exactly(1))
      .WillOnce(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread));
  pc_proxy_->CreateAnswer(fake_observer, constraints);
}

TEST_F(PeerConnectionProxyTest, SetLocalDescription) {
  SessionDescriptionInterface* fake_desc =
      GetFakePointer1<SessionDescriptionInterface>();
  SetSessionDescriptionObserver* fake_observer =
      GetFakePointer2<SetSessionDescriptionObserver>();
  EXPECT_CALL(*pc_, SetLocalDescription(fake_observer, fake_desc))
      .Times(Exactly(1))
      .WillOnce(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread));
  pc_proxy_->SetLocalDescription(fake_observer, fake_desc);
}

TEST_F(PeerConnectionProxyTest, SetRemoteDescription) {
  SessionDescriptionInterface* fake_desc =
      GetFakePointer1<SessionDescriptionInterface>();
  SetSessionDescriptionObserver* fake_observer =
      GetFakePointer2<SetSessionDescriptionObserver>();
  EXPECT_CALL(*pc_, SetRemoteDescription(fake_observer, fake_desc))
      .Times(Exactly(1))
      .WillOnce(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread));
  pc_proxy_->SetRemoteDescription(fake_observer, fake_desc);
}

TEST_F(PeerConnectionProxyTest, UpdateIce) {
  MediaConstraintsInterface* constraints =
      GetFakePointer1<MediaConstraintsInterface>();
  EXPECT_CALL(*pc_, UpdateIce(_, constraints))
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(true)));
  EXPECT_TRUE(pc_proxy_->UpdateIce(PeerConnectionInterface::IceServers(),
                                   constraints));
}

TEST_F(PeerConnectionProxyTest, AddIceCandidate) {
  IceCandidateInterface* fake_candidate =
      GetFakePointer1<IceCandidateInterface>();
  EXPECT_CALL(*pc_, AddIceCandidate(fake_candidate))
      .Times(Exactly(1))
      .WillOnce(
          DoAll(InvokeWithoutArgs(this, &PeerConnectionProxyTest::CheckThread),
                Return(true)));
  EXPECT_TRUE(pc_proxy_->AddIceCandidate(fake_candidate));
}

}  // namespace webrtc
