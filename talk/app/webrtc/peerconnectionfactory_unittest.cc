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
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>

#include "talk/app/webrtc/fakeportallocatorfactory.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnectionfactory.h"
#include "talk/app/webrtc/videosourceinterface.h"
#include "talk/app/webrtc/test/fakevideotrackrenderer.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/media/base/fakevideocapturer.h"
#include "talk/media/webrtc/webrtccommon.h"
#include "talk/media/webrtc/webrtcvoe.h"

using webrtc::FakeVideoTrackRenderer;
using webrtc::MediaStreamInterface;
using webrtc::PeerConnectionFactoryInterface;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;
using webrtc::PortAllocatorFactoryInterface;
using webrtc::VideoSourceInterface;
using webrtc::VideoTrackInterface;

namespace {

static const char kStunIceServer[] = "stun:stun.l.google.com:19302";
static const char kTurnIceServer[] = "turn:test@test.com:1234";
static const char kInvalidTurnIceServer[] = "turn:test.com:1234";
static const char kTurnPassword[] = "turnpassword";

class NullPeerConnectionObserver : public PeerConnectionObserver {
 public:
  virtual void OnError() {}
  virtual void OnMessage(const std::string& msg) {}
  virtual void OnSignalingMessage(const std::string& msg) {}
  virtual void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) {}
  virtual void OnAddStream(MediaStreamInterface* stream) {}
  virtual void OnRemoveStream(MediaStreamInterface* stream) {}
  virtual void OnRenegotiationNeeded() {}
  virtual void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) {}
  virtual void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) {}
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {}
};

}  // namespace

class PeerConnectionFactoryTest : public testing::Test {
  void SetUp() {
    factory_ = webrtc::CreatePeerConnectionFactory(talk_base::Thread::Current(),
                                                   talk_base::Thread::Current(),
                                                   NULL);

    ASSERT_TRUE(factory_.get() != NULL);
    allocator_factory_ =  webrtc::FakePortAllocatorFactory::Create();
  }

 protected:
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory_;
  NullPeerConnectionObserver observer_;
  talk_base::scoped_refptr<PortAllocatorFactoryInterface> allocator_factory_;
};

TEST(PeerConnectionFactoryTestInternal, CreatePCUsingInternalModules) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      webrtc::CreatePeerConnectionFactory());

  NullPeerConnectionObserver observer;
  webrtc::PeerConnectionInterface::IceServers servers;

  talk_base::scoped_refptr<PeerConnectionInterface> pc(
      factory->CreatePeerConnection(servers, NULL, &observer));

  EXPECT_TRUE(pc.get() != NULL);
}

TEST_F(PeerConnectionFactoryTest, CreatePCUsingIceServers) {
  webrtc::PeerConnectionInterface::IceServers ice_servers;
  webrtc::PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kStunIceServer;
  ice_servers.push_back(ice_server);
  ice_server.uri = kTurnIceServer;
  ice_server.password = kTurnPassword;
  ice_servers.push_back(ice_server);
  talk_base::scoped_refptr<PeerConnectionInterface> pc(
      factory_->CreatePeerConnection(ice_servers, NULL,
                                     allocator_factory_.get(),
                                     &observer_));
  EXPECT_TRUE(pc.get() != NULL);
}

TEST_F(PeerConnectionFactoryTest, CreatePCUsingInvalidTurnUrl) {
  webrtc::PeerConnectionInterface::IceServers ice_servers;
  webrtc::PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = kInvalidTurnIceServer;
  ice_server.password = kTurnPassword;
  ice_servers.push_back(ice_server);
  talk_base::scoped_refptr<PeerConnectionInterface> pc(
      factory_->CreatePeerConnection(ice_servers, NULL,
                                     allocator_factory_.get(),
                                     &observer_));
  EXPECT_TRUE(pc.get() == NULL);
}


TEST_F(PeerConnectionFactoryTest, LocalRendering) {
  cricket::FakeVideoCapturer* capturer = new cricket::FakeVideoCapturer();
  // The source take ownership of |capturer|.
  talk_base::scoped_refptr<VideoSourceInterface> source(
      factory_->CreateVideoSource(capturer, NULL));
  ASSERT_TRUE(source.get() != NULL);
  talk_base::scoped_refptr<VideoTrackInterface> track(
      factory_->CreateVideoTrack("testlabel", source));
  ASSERT_TRUE(track.get() != NULL);
  FakeVideoTrackRenderer local_renderer(track);

  EXPECT_EQ(0, local_renderer.num_rendered_frames());
  EXPECT_TRUE(capturer->CaptureFrame());
  EXPECT_EQ(1, local_renderer.num_rendered_frames());

  track->set_enabled(false);
  EXPECT_TRUE(capturer->CaptureFrame());
  EXPECT_EQ(1, local_renderer.num_rendered_frames());

  track->set_enabled(true);
  EXPECT_TRUE(capturer->CaptureFrame());
  EXPECT_EQ(2, local_renderer.num_rendered_frames());
}

