/*
 * libjingle
 * Copyright 2011, Google Inc.
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

#include "talk/app/webrtc/webrtcsession.h"
#include "talk/base/logging.h"
#include "talk/base/fakenetwork.h"
#include "talk/base/firewallsocketserver.h"
#include "talk/base/gunit.h"
#include "talk/base/network.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/p2p/base/stunserver.h"
#include "talk/p2p/base/teststunserver.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/fakedevicemanager.h"
#include "talk/session/phone/fakemediaengine.h"
#include "talk/session/phone/mediasession.h"

using talk_base::SocketAddress;
static const SocketAddress kClientAddr1("11.11.11.11", 0);
static const SocketAddress kClientAddr2("22.22.22.22", 0);
static const SocketAddress kStunAddr("99.99.99.1", cricket::STUN_SERVER_PORT);

static const char kStream1[] = "stream1";
static const char kVideoTrack1[] = "video1";
static const char kAudioTrack1[] = "audio1";

static const char kStream2[] = "stream2";
static const char kVideoTrack2[] = "video2";
static const char kAudioTrack2[] = "audio2";

static const int kIceCandidatesTimeout = 3000;


class MockCandidateObserver : public webrtc::CandidateObserver {
 public:
  MockCandidateObserver()
      : oncandidatesready_(false) {
  }

  // Found a new candidate.
  virtual void OnCandidateFound(const std::string& content_name,
                                const cricket::Candidate& candidate) {
    if (content_name == cricket::CN_AUDIO) {
      audio_candidates_.push_back(candidate);
    } else if (content_name == cricket::CN_VIDEO) {
      video_candidates_.push_back(candidate);
    }
  }

  virtual void OnCandidatesReady() {
    EXPECT_FALSE(oncandidatesready_);
    oncandidatesready_ = true;
  }

  bool oncandidatesready_;
  std::vector<cricket::Candidate> audio_candidates_;
  std::vector<cricket::Candidate> video_candidates_;
};

class WebRtcSessionForTest : public webrtc::WebRtcSession {
 public:
  WebRtcSessionForTest(cricket::ChannelManager* cmgr,
                       talk_base::Thread* signaling_thread,
                       talk_base::Thread* worker_thread,
                       cricket::PortAllocator* port_allocator)
    : WebRtcSession(cmgr, signaling_thread, worker_thread, port_allocator) {
  }
  virtual ~WebRtcSessionForTest() {}

  using webrtc::WebRtcSession::CreateOffer;
  using webrtc::WebRtcSession::CreateAnswer;
  using webrtc::WebRtcSession::SetLocalDescription;
  using webrtc::WebRtcSession::SetRemoteDescription;
  using webrtc::WebRtcSession::AddRemoteCandidate;
};

class WebRtcSessionTest : public testing::Test {
 protected:
  // TODO Investigate why ChannelManager crashes, if it's created
  // after stun_server.
  WebRtcSessionTest()
    : media_engine(new cricket::FakeMediaEngine()),
      device_manager(new cricket::FakeDeviceManager()),
     channel_manager_(new cricket::ChannelManager(
         media_engine, device_manager, talk_base::Thread::Current())),
      desc_factory_(new cricket::MediaSessionDescriptionFactory(
          channel_manager_.get())),
      pss_(new talk_base::PhysicalSocketServer),
      vss_(new talk_base::VirtualSocketServer(pss_.get())),
      fss_(new talk_base::FirewallSocketServer(vss_.get())),
      ss_scope_(fss_.get()),
      stun_server_(talk_base::Thread::Current(), kStunAddr),
      allocator_(&network_manager_, kStunAddr,
                 SocketAddress(), SocketAddress(), SocketAddress()) {
    EXPECT_TRUE(channel_manager_->Init());
    desc_factory_->set_add_legacy_streams(false);
  }

  void AddInterface(const SocketAddress& addr) {
    network_manager_.AddInterface(addr);
  }

  void Init() {
    ASSERT_TRUE(session_.get() == NULL);
    session_.reset(new WebRtcSessionForTest(
        channel_manager_.get(), talk_base::Thread::Current(),
        talk_base::Thread::Current(), &allocator_));
    session_->RegisterObserver(&observer_);

    EXPECT_TRUE(session_->Initialize());
    session_->StartIce();

    video_channel_ = media_engine->GetVideoChannel(0);
    voice_channel_ = media_engine->GetVoiceChannel(0);
  }

  void PopulateFakeCandidates() {
    const int num_of_channels = 4;
    const char* const channel_names[num_of_channels] = {
        "rtp", "rtcp", "video_rtp", "video_rtcp"
    };

    // max 4 transport channels;
    candidates_.clear();
    for (int i = 0; i < num_of_channels; ++i) {
      cricket::Candidate candidate;
      candidate.set_name(channel_names[i]);
      candidates_.push_back(candidate);
    }
  }

  // Create a session description based on options. Used for testing but don't
  // test WebRtcSession.
  cricket::SessionDescription* CreateTestOffer(
      const cricket::MediaSessionOptions& options) {
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    return desc_factory_->CreateOffer(options, NULL);
  }

  // Create a session description based on options. Used for testing but don't
  // test WebRtcSession.
  cricket::SessionDescription* CreateTestAnswer(
      const cricket::SessionDescription* offer,
      const cricket::MediaSessionOptions& options) {
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    return desc_factory_->CreateAnswer(offer, options, NULL);
  }

  cricket::MediaSessionOptions OptionsWithStream1() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack1, kStream1);
    return options;
  }

  cricket::MediaSessionOptions OptionsWithStream2() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2, kStream2);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2, kStream2);
    return options;
  }

  cricket::MediaSessionOptions OptionsWithStream1And2() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2, kStream2);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2, kStream2);
    return options;
  }

  cricket::MediaSessionOptions OptionsReceiveOnly() {
    cricket::MediaSessionOptions options;
    options.has_video = true;
    return options;
  }

  bool ChannelsExist() {
    return (session_->voice_channel() != NULL &&
            session_->video_channel() != NULL);
  }

  void CheckTransportChannels() {
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, "rtp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, "rtcp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, "video_rtp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, "video_rtcp") != NULL);
  }

  void VerifyCryptoParams(const cricket::SessionDescription* sdp,
                          bool offer) {
    ASSERT_TRUE(session_.get() != NULL);
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    if (offer) {
      ASSERT_EQ(2U, audio_content->cryptos().size());
      // key(40) + inline string
      ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
      ASSERT_EQ("AES_CM_128_HMAC_SHA1_32",
                audio_content->cryptos()[0].cipher_suite);
      ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
                audio_content->cryptos()[1].cipher_suite);
      ASSERT_EQ(47U, audio_content->cryptos()[1].key_params.size());
    } else {
      ASSERT_EQ(1U, audio_content->cryptos().size());
      // key(40) + inline string
      ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
      ASSERT_EQ("AES_CM_128_HMAC_SHA1_32",
                audio_content->cryptos()[0].cipher_suite);
    }

    content = cricket::GetFirstVideoContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::VideoContentDescription* video_content =
        static_cast<const cricket::VideoContentDescription*>(
            content->description);
    ASSERT_TRUE(video_content != NULL);
    ASSERT_EQ(1U, video_content->cryptos().size());
    ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
              video_content->cryptos()[0].cipher_suite);
    ASSERT_EQ(47U, video_content->cryptos()[0].key_params.size());
  }

  void VerifyNoCryptoParams(const cricket::SessionDescription* sdp) {
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    ASSERT_EQ(0U, audio_content->cryptos().size());

    content = cricket::GetFirstVideoContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::VideoContentDescription* video_content =
        static_cast<const cricket::VideoContentDescription*>(
            content->description);
    ASSERT_TRUE(video_content != NULL);
    ASSERT_EQ(0U, video_content->cryptos().size());
  }

  void VerifyAnswerFromNonCryptoOffer() {
    // Create a SDP without Crypto.
    desc_factory_->set_secure(cricket::SEC_DISABLED);
    cricket::MediaSessionOptions options;
    options.has_video = true;
    cricket::SessionDescription* offer =
        desc_factory_->CreateOffer(options, NULL);
    ASSERT_TRUE(offer != NULL);
    VerifyNoCryptoParams(offer);
    const cricket::SessionDescription* answer =
        session_->CreateAnswer(offer, options);
    // Answer should be NULL as no crypto params in offer.
    ASSERT_TRUE(answer == NULL);
  }

  void VerifyAnswerFromCryptoOffer() {
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    cricket::MediaSessionOptions options;
    options.has_video = true;
    cricket::SessionDescription* offer =
        desc_factory_->CreateOffer(options, NULL);
    ASSERT_TRUE(offer != NULL);
    VerifyCryptoParams(offer, true);
    const cricket::SessionDescription* answer =
        session_->CreateAnswer(offer, options);
    ASSERT_TRUE(answer != NULL);
    VerifyCryptoParams(answer, false);
  }

  cricket::FakeMediaEngine* media_engine;
  cricket::FakeDeviceManager* device_manager;
  talk_base::scoped_ptr<cricket::ChannelManager> channel_manager_;
  talk_base::scoped_ptr<cricket::MediaSessionDescriptionFactory> desc_factory_;
  talk_base::scoped_ptr<talk_base::PhysicalSocketServer> pss_;
  talk_base::scoped_ptr<talk_base::VirtualSocketServer> vss_;
  talk_base::scoped_ptr<talk_base::FirewallSocketServer> fss_;
  talk_base::SocketServerScope ss_scope_;
  cricket::TestStunServer stun_server_;
  talk_base::FakeNetworkManager network_manager_;
  cricket::BasicPortAllocator allocator_;
  talk_base::scoped_ptr<WebRtcSessionForTest> session_;
  MockCandidateObserver observer_;
  std::vector<cricket::Candidate> candidates_;
  cricket::FakeVideoMediaChannel* video_channel_;
  cricket::FakeVoiceMediaChannel* voice_channel_;
};

TEST_F(WebRtcSessionTest, TestInitialize) {
  WebRtcSessionTest::Init();
  EXPECT_TRUE(ChannelsExist());
  CheckTransportChannels();
}

TEST_F(WebRtcSessionTest, TestSessionCandidates) {
  AddInterface(kClientAddr1);
  WebRtcSessionTest::Init();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(4u, observer_.audio_candidates_.size());
  EXPECT_EQ(4u, observer_.video_candidates_.size());
}

TEST_F(WebRtcSessionTest, TestMultihomeCandidataes) {
  AddInterface(kClientAddr1);
  AddInterface(kClientAddr2);
  WebRtcSessionTest::Init();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(8u, observer_.audio_candidates_.size());
  EXPECT_EQ(8u, observer_.video_candidates_.size());
}

TEST_F(WebRtcSessionTest, TestStunError) {
  AddInterface(kClientAddr1);
  AddInterface(kClientAddr2);
  fss_->AddRule(false, talk_base::FP_UDP, talk_base::FD_ANY, kClientAddr1);
  WebRtcSessionTest::Init();
  // Since kClientAddr1 is blocked, not expecting stun candidates for it.
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(6u, observer_.audio_candidates_.size());
  EXPECT_EQ(6u, observer_.video_candidates_.size());
}

// Test creating offers and receive answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestCreateOfferReceiveAnswer) {
  WebRtcSessionTest::Init();
  cricket::MediaSessionOptions options = OptionsWithStream1();
  cricket::SessionDescription* offer = session_->CreateOffer(options);

  cricket::MediaSessionOptions options2 = OptionsWithStream2();
  cricket::SessionDescription* answer =  CreateTestAnswer(offer, options2);

  EXPECT_TRUE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
  EXPECT_TRUE(session_->SetRemoteDescription(answer, cricket::CA_ANSWER));

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  cricket::StreamParams recv_video_stream =
      video_channel_->recv_streams()[0];
  EXPECT_TRUE(kVideoTrack2 == recv_video_stream.name);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  cricket::StreamParams recv_audio_stream =
      voice_channel_->recv_streams()[0];
  EXPECT_TRUE(kAudioTrack2 == recv_audio_stream.name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].name);

  // Create new offer without send streams.
  offer = session_->CreateOffer(OptionsReceiveOnly());
  // Test with same answer.
  EXPECT_TRUE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
  EXPECT_TRUE(session_->SetRemoteDescription(answer, cricket::CA_ANSWER));

  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());

  // Make sure the receive streams have not changed.
  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_EQ(recv_video_stream, video_channel_->recv_streams()[0]);
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_EQ(recv_audio_stream, voice_channel_->recv_streams()[0]);
}

// Test receiving offers and creating answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestReceiveOfferCreateAnswer) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsWithStream2());

  cricket::MediaSessionOptions answer_options = OptionsWithStream1();
  cricket::SessionDescription* answer =
        session_->CreateAnswer(offer, answer_options);
  EXPECT_TRUE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
  EXPECT_TRUE(session_->SetLocalDescription(answer, cricket::CA_ANSWER));

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].name);

  offer = CreateTestOffer(OptionsWithStream1And2());

  // Answer by turning off all send streams.
  answer = session_->CreateAnswer(offer, OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
  EXPECT_TRUE(session_->SetLocalDescription(answer, cricket::CA_ANSWER));

  ASSERT_EQ(2u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->recv_streams()[0].name);
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[1].name);
  ASSERT_EQ(2u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->recv_streams()[0].name);
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[1].name);

  // Make we have no send streams.
  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());
}

TEST_F(WebRtcSessionTest, TestSetLocalOfferTwice) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
  EXPECT_FALSE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
}

TEST_F(WebRtcSessionTest, TestSetRemoteOfferTwice) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
  EXPECT_FALSE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
}

TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteOffer) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
  EXPECT_FALSE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
}

TEST_F(WebRtcSessionTest, TestSetRemoteAndLocalOffer) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));
  EXPECT_FALSE(session_->SetLocalDescription(offer, cricket::CA_OFFER));
}

TEST_F(WebRtcSessionTest, TestSetLocalAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  cricket::SessionDescription* answer =
        session_->CreateAnswer(offer, OptionsReceiveOnly());
  EXPECT_FALSE(session_->SetLocalDescription(answer, cricket::CA_ANSWER));
}

TEST_F(WebRtcSessionTest, TestSetRemoteAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  cricket::SessionDescription* answer =
        session_->CreateAnswer(offer, OptionsReceiveOnly());
  EXPECT_FALSE(session_->SetRemoteDescription(answer, cricket::CA_ANSWER));
}

TEST_F(WebRtcSessionTest, TestAddRemoteCandidate) {
  WebRtcSessionTest::Init();

  cricket::Candidate candidate1;
  candidate1.set_name("fake_candidate1");

  // Fail since we have not set a remote description
  EXPECT_FALSE(session_->AddRemoteCandidate(cricket::CN_AUDIO, candidate1));

  cricket::SessionDescription* offer = CreateTestOffer(OptionsReceiveOnly());
  EXPECT_TRUE(session_->SetRemoteDescription(offer, cricket::CA_OFFER));

  EXPECT_TRUE(session_->AddRemoteCandidate(cricket::CN_AUDIO, candidate1));

  cricket::Candidate candidate2;
  candidate1.set_name("fake_candidate2");

  EXPECT_FALSE(session_->AddRemoteCandidate("bad content name", candidate2));
  EXPECT_TRUE(session_->AddRemoteCandidate(cricket::CN_VIDEO, candidate2));
}

TEST_F(WebRtcSessionTest, TestDefaultSetSecurePolicy) {
  WebRtcSessionTest::Init();
  EXPECT_EQ(cricket::SEC_REQUIRED, session_->secure_policy());
}

TEST_F(WebRtcSessionTest, VerifyCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  VerifyCryptoParams(session_->CreateOffer(OptionsWithStream1()), true);
}

TEST_F(WebRtcSessionTest, VerifyNoCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  session_->set_secure_policy(cricket::SEC_DISABLED);
  VerifyNoCryptoParams(session_->CreateOffer(OptionsWithStream1()));
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromNonCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromNonCryptoOffer();
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromCryptoOffer();
}
