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

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/webrtcsession.h"
#include "talk/base/fakenetwork.h"
#include "talk/base/firewallsocketserver.h"
#include "talk/base/gunit.h"
#include "talk/base/logging.h"
#include "talk/base/network.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/media/base/fakemediaengine.h"
#include "talk/media/base/fakevideorenderer.h"
#include "talk/media/devices/fakedevicemanager.h"
#include "talk/p2p/base/stunserver.h"
#include "talk/p2p/base/teststunserver.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/session/media/channelmanager.h"
#include "talk/session/media/mediasession.h"

using cricket::BaseSession;
using talk_base::scoped_ptr;
using talk_base::SocketAddress;
using webrtc::IceCandidateCollection;
using webrtc::JsepInterface;
using webrtc::JsepSessionDescription;
using webrtc::JsepIceCandidate;
using webrtc::SessionDescriptionInterface;

using webrtc::MediaHints;

static const SocketAddress kClientAddr1("11.11.11.11", 0);
static const SocketAddress kClientAddr2("22.22.22.22", 0);
static const SocketAddress kStunAddr("99.99.99.1", cricket::STUN_SERVER_PORT);

static const char kStream1[] = "stream1";
static const char kVideoTrack1[] = "video1";
static const char kAudioTrack1[] = "audio1";

static const char kStream2[] = "stream2";
static const char kVideoTrack2[] = "video2";
static const char kAudioTrack2[] = "audio2";

// Media index of candidates belonging to the first media content.
static const int kMediaContentIndex0 = 0;
static const char kMediaContentName0[] = "audio";

// Media index of candidates belonging to the second media content.
static const int kMediaContentIndex1 = 1;

static const int kIceCandidatesTimeout = 10000;

class MockCandidateObserver : public webrtc::IceCandidateObserver {
 public:
  MockCandidateObserver()
      : oncandidatesready_(false) {
  }

  virtual void OnIceChange() {}

  // Found a new candidate.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    if (candidate->sdp_mline_index() == kMediaContentIndex0) {
      mline_0_candidates_.push_back(candidate->candidate());
    } else if (candidate->sdp_mline_index() == kMediaContentIndex1) {
      mline_1_candidates_.push_back(candidate->candidate());
    }
  }

  virtual void OnIceComplete() {
    EXPECT_FALSE(oncandidatesready_);
    oncandidatesready_ = true;
  }

  bool oncandidatesready_;
  std::vector<cricket::Candidate> mline_0_candidates_;
  std::vector<cricket::Candidate> mline_1_candidates_;
};

class WebRtcSessionForTest : public webrtc::WebRtcSession {
 public:
  WebRtcSessionForTest(cricket::ChannelManager* cmgr,
                       talk_base::Thread* signaling_thread,
                       talk_base::Thread* worker_thread,
                       cricket::PortAllocator* port_allocator,
                       webrtc::IceCandidateObserver* ice_observer,
                       webrtc::MediaStreamSignaling* mediastream_signaling)
    : WebRtcSession(cmgr, signaling_thread, worker_thread, port_allocator,
                    mediastream_signaling) {
    RegisterObserver(ice_observer);
  }
  virtual ~WebRtcSessionForTest() {}

  using cricket::BaseSession::GetTransportProxy;
  using webrtc::WebRtcSession::SetAudioPlayout;
  using webrtc::WebRtcSession::SetAudioSend;
  using webrtc::WebRtcSession::SetCaptureDevice;
  using webrtc::WebRtcSession::SetVideoPlayout;
  using webrtc::WebRtcSession::SetVideoSend;
};

class FakeMediaStreamSignaling : public webrtc::MediaStreamSignaling,
                                 public webrtc::RemoteMediaStreamObserver {
 public:
  FakeMediaStreamSignaling() :
    webrtc::MediaStreamSignaling(talk_base::Thread::Current(), this) {
  }

  // Overrides GetMediaSessionOptions in MediaStreamSignaling.
  // Instead of depending on MediaStreams this version of GetMediaSessionOptions
  // returns the options decided by MediaSessionOptions set in one of the below
  // UseOptions functions.
  virtual cricket::MediaSessionOptions GetMediaSessionOptions(
        const MediaHints& hints) const {
    return options_;
  }

  void UseOptionsWithStream1(bool bundle = false) {
    cricket::MediaSessionOptions options;
    options.bundle_enabled = bundle;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack1, kStream1);
    options_ = options;
  }

  void UseOptionsWithStream2(bool bundle = false) {
    cricket::MediaSessionOptions options;
    options.bundle_enabled = bundle;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2, kStream2);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2, kStream2);
    options_ = options;
  }

  void UseOptionsWithStream1And2() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack1, kStream1);
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2, kStream2);
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2, kStream2);
    options_ = options;
  }

  void UseOptionsReceiveOnly() {
    cricket::MediaSessionOptions options;
    options.has_video = true;
    options_ = options;
  }

  void UseOptionsAudioOnly() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2, kStream2);
    options.has_video = false;
    options_ = options;
  }

  void UseOptionsVideoOnly() {
    cricket::MediaSessionOptions options;
    options.AddStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2, kStream2);
    options.has_audio = false;
    options_ = options;
  }

  // Implements RemoteMediaStreamObserver.
  virtual void OnAddStream(webrtc::MediaStreamInterface* stream) {
  }
  virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream) {
  }

 private:
  cricket::MediaSessionOptions options_;
};

class WebRtcSessionTest : public testing::Test {
 protected:
  // TODO Investigate why ChannelManager crashes, if it's created
  // after stun_server.
  WebRtcSessionTest()
    : media_engine_(new cricket::FakeMediaEngine()),
      device_manager_(new cricket::FakeDeviceManager()),
      channel_manager_(new cricket::ChannelManager(
         media_engine_, device_manager_, talk_base::Thread::Current())),
      tdesc_factory_(new cricket::TransportDescriptionFactory()),
      desc_factory_(new cricket::MediaSessionDescriptionFactory(
          channel_manager_.get(), tdesc_factory_.get())),
      pss_(new talk_base::PhysicalSocketServer),
      vss_(new talk_base::VirtualSocketServer(pss_.get())),
      fss_(new talk_base::FirewallSocketServer(vss_.get())),
      ss_scope_(fss_.get()),
      stun_server_(talk_base::Thread::Current(), kStunAddr),
      allocator_(&network_manager_, kStunAddr,
                 SocketAddress(), SocketAddress(), SocketAddress()) {
    allocator_.set_flags(cricket::PORTALLOCATOR_DISABLE_TCP |
                         cricket::PORTALLOCATOR_DISABLE_RELAY |
                         cricket::PORTALLOCATOR_ENABLE_BUNDLE);
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
        talk_base::Thread::Current(), &allocator_,
        &observer_,
        &mediastream_signaling_));

    EXPECT_TRUE(session_->Initialize());
    mediastream_signaling_.UseOptionsReceiveOnly();
  }

  // Creates a local offer and applies it. Starts ice.
  // Call mediastream_signaling_.UseOptionsWithStreamX() before this function
  // to decide which streams to create.
  void InitiateCall() {
    SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
    EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  }

  bool ChannelsExist() {
    return (session_->voice_channel() != NULL &&
            session_->video_channel() != NULL);
  }

  void CheckTransportChannels() {
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, 1) != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, 2) != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, 1) != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, 2) != NULL);
  }

  void VerifyCryptoParams(const cricket::SessionDescription* sdp,
                          bool offer,
                          bool bundle) {
    ASSERT_TRUE(session_.get() != NULL);
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    if (offer) {
      if (bundle) {
        ASSERT_EQ(1U, audio_content->cryptos().size());
        // key(40) + inline string
        ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
                  audio_content->cryptos()[0].cipher_suite);
        ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
      } else {
        ASSERT_EQ(2U, audio_content->cryptos().size());
        // key(40) + inline string
        ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
        ASSERT_EQ("AES_CM_128_HMAC_SHA1_32",
                  audio_content->cryptos()[0].cipher_suite);
        ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
                  audio_content->cryptos()[1].cipher_suite);
        ASSERT_EQ(47U, audio_content->cryptos()[1].key_params.size());
      }
    } else {
      if (bundle) {
        ASSERT_EQ(1U, audio_content->cryptos().size());
        // key(40) + inline string
        ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
        ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
                  audio_content->cryptos()[0].cipher_suite);
      } else {
        ASSERT_EQ(1U, audio_content->cryptos().size());
        // key(40) + inline string
        ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
        ASSERT_EQ("AES_CM_128_HMAC_SHA1_32",
                  audio_content->cryptos()[0].cipher_suite);
      }
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
    scoped_ptr<JsepSessionDescription> offer(
        CreateOfferSessionDescription(options));
    ASSERT_TRUE(offer.get() != NULL);
    VerifyNoCryptoParams(offer->description());
    const webrtc::SessionDescriptionInterface* answer =
        session_->CreateAnswer(MediaHints(), offer.get());
    // Answer should be NULL as no crypto params in offer.
    ASSERT_TRUE(answer->description() == NULL);
  }

  void VerifyAnswerFromCryptoOffer() {
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    cricket::MediaSessionOptions options;
    options.has_video = true;
    scoped_ptr<JsepSessionDescription> offer(
        CreateOfferSessionDescription(options));
    ASSERT_TRUE(offer.get() != NULL);
    VerifyCryptoParams(offer->description(), true, false);
    scoped_ptr<SessionDescriptionInterface> answer(
        session_->CreateAnswer(MediaHints(), offer.get()));
    ASSERT_TRUE(answer.get() != NULL);
    VerifyCryptoParams(answer->description(), false, false);
  }
  // Creates and offer and an answer and applies it on the offer.
  // Call mediastream_signaling_.UseOptionsWithStreamX() before this function
  // to decide which streams to create.
  void SetRemoteAndLocalSessionDescription() {
    SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
    SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                                 offer);
    EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
    EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));
  }

  void SetLocalDescription(JsepInterface::Action action,
                      SessionDescriptionInterface* desc,
                      BaseSession::State expected_state) {
    EXPECT_TRUE(session_->SetLocalDescription(action, desc));
    EXPECT_EQ(expected_state, session_->state());
  }
  void SetRemoteDescription(JsepInterface::Action action,
                            SessionDescriptionInterface* desc,
                            BaseSession::State expected_state) {
    EXPECT_TRUE(session_->SetRemoteDescription(action, desc));
    EXPECT_EQ(expected_state, session_->state());
  }

  void CreateCryptoOfferAndNonCryptoAnswer(SessionDescriptionInterface** offer,
      JsepSessionDescription** nocrypto_answer) {
    mediastream_signaling_.UseOptionsWithStream2();
    *offer = session_->CreateOffer(MediaHints());

    mediastream_signaling_.UseOptionsWithStream1();
    talk_base::scoped_ptr<SessionDescriptionInterface> answer(
        session_->CreateAnswer(MediaHints(), *offer));
    std::string nocrypto_answer_str;
    answer->ToString(&nocrypto_answer_str);
    // Disable the crypto
    const std::string kCrypto = "a=crypto";
    const std::string kCryptoX = "a=cryptx";
    talk_base::replace_substrs(kCrypto.c_str(), kCrypto.length(),
                               kCryptoX.c_str(), kCryptoX.length(),
                               &nocrypto_answer_str);
    *nocrypto_answer =
        new JsepSessionDescription(JsepSessionDescription::kAnswer);
    EXPECT_TRUE((*nocrypto_answer)->Initialize(nocrypto_answer_str));
  }
  JsepSessionDescription* CreateOfferSessionDescription(
      cricket::MediaSessionOptions options) {
    const std::string session_id =
        talk_base::ToString(talk_base::CreateRandomId());
    const std::string session_version =
        talk_base::ToString(talk_base::CreateRandomId());
    JsepSessionDescription* offer(
        new JsepSessionDescription(JsepSessionDescription::kOffer));
    if (!offer->Initialize(desc_factory_->CreateOffer(options, NULL),
                           session_id, session_version)) {
      delete offer;
      offer = NULL;
    }
    return offer;
  }
  void TestSessionCandidatesWithBundleRtcpMux(bool bundle, bool rtcp_mux) {
    AddInterface(kClientAddr1);
    WebRtcSessionTest::Init();
    mediastream_signaling_.UseOptionsWithStream1(bundle);
    SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
    mediastream_signaling_.UseOptionsWithStream2(bundle);
    SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                                 offer);
    size_t expected_candidate_num = 2;
    if (!rtcp_mux) {
      // If rtcp_mux is enabled we should expect 4 candidates - host and srflex
      // for rtp and rtcp.
      expected_candidate_num = 4;
      // Disable rtcp-mux from the answer
      std::string sdp;
      EXPECT_TRUE(answer->ToString(&sdp));
      const std::string kRtcpMux = "a=rtcp-mux";
      const std::string kXRtcpMux = "a=xrtcp-mux";
      talk_base::replace_substrs(kRtcpMux.c_str(), kRtcpMux.length(),
                                 kXRtcpMux.c_str(), kXRtcpMux.length(),
                                 &sdp);
      JsepSessionDescription* new_answer(
          new JsepSessionDescription(JsepSessionDescription::kAnswer));
      EXPECT_TRUE(new_answer->Initialize(sdp));
      delete answer;
      answer = new_answer;
    }
    // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
    // and answer.
    EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
    // SetRemoteDescription to enable rtcp mux.
    EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
    EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
    EXPECT_EQ(expected_candidate_num, observer_.mline_0_candidates_.size());
    EXPECT_EQ(expected_candidate_num, observer_.mline_1_candidates_.size());
    for (size_t i = 0; i < observer_.mline_0_candidates_.size(); ++i) {
      cricket::Candidate c0 = observer_.mline_0_candidates_[i];
      cricket::Candidate c1 = observer_.mline_1_candidates_[i];
      if (bundle) {
        EXPECT_TRUE(c0.IsEquivalent(c1));
      } else {
        EXPECT_FALSE(c0.IsEquivalent(c1));
      }
    }
  }

  cricket::FakeMediaEngine* media_engine_;
  cricket::FakeDeviceManager* device_manager_;
  talk_base::scoped_ptr<cricket::ChannelManager> channel_manager_;
  talk_base::scoped_ptr<cricket::TransportDescriptionFactory> tdesc_factory_;
  talk_base::scoped_ptr<cricket::MediaSessionDescriptionFactory> desc_factory_;
  talk_base::scoped_ptr<talk_base::PhysicalSocketServer> pss_;
  talk_base::scoped_ptr<talk_base::VirtualSocketServer> vss_;
  talk_base::scoped_ptr<talk_base::FirewallSocketServer> fss_;
  talk_base::SocketServerScope ss_scope_;
  cricket::TestStunServer stun_server_;
  talk_base::FakeNetworkManager network_manager_;
  cricket::BasicPortAllocator allocator_;
  FakeMediaStreamSignaling mediastream_signaling_;
  talk_base::scoped_ptr<WebRtcSessionForTest> session_;
  MockCandidateObserver observer_;
  cricket::FakeVideoMediaChannel* video_channel_;
  cricket::FakeVoiceMediaChannel* voice_channel_;
};

TEST_F(WebRtcSessionTest, TestInitialize) {
  WebRtcSessionTest::Init();
}

TEST_F(WebRtcSessionTest, TestSessionCandidates) {
  TestSessionCandidatesWithBundleRtcpMux(false, false);
}

// Below test cases (TestSessionCandidatesWith*) verify the candidates gathered
// with rtcp-mux and/or bundle.
TEST_F(WebRtcSessionTest, TestSessionCandidatesWithRtcpMux) {
  TestSessionCandidatesWithBundleRtcpMux(false, true);
}

TEST_F(WebRtcSessionTest, TestSessionCandidatesWithBundle) {
  TestSessionCandidatesWithBundleRtcpMux(true, false);
}

TEST_F(WebRtcSessionTest, TestSessionCandidatesWithBundleRtcpMux) {
  TestSessionCandidatesWithBundleRtcpMux(true, true);
}

TEST_F(WebRtcSessionTest, TestMultihomeCandidataes) {
  AddInterface(kClientAddr1);
  AddInterface(kClientAddr2);
  WebRtcSessionTest::Init();
  WebRtcSessionTest::InitiateCall();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(8u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(8u, observer_.mline_1_candidates_.size());
}

TEST_F(WebRtcSessionTest, TestStunError) {
  AddInterface(kClientAddr1);
  AddInterface(kClientAddr2);
  fss_->AddRule(false, talk_base::FP_UDP, talk_base::FD_ANY, kClientAddr1);
  WebRtcSessionTest::Init();
  WebRtcSessionTest::InitiateCall();
  // Since kClientAddr1 is blocked, not expecting stun candidates for it.
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(6u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(6u, observer_.mline_1_candidates_.size());
}

// Test creating offers and receive answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestCreateOfferReceiveAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  const std::string session_id_orig = offer->session_id();
  const std::string session_version_orig = offer->session_version();

  mediastream_signaling_.UseOptionsWithStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].name);

  // Create new offer without send streams.
  mediastream_signaling_.UseOptionsReceiveOnly();
  offer = session_->CreateOffer(MediaHints());

  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, offer->session_id());
  EXPECT_LT(talk_base::FromString<uint64>(session_version_orig),
            talk_base::FromString<uint64>(offer->session_version()));

  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));

  mediastream_signaling_.UseOptionsWithStream2();
  answer = session_->CreateAnswer(MediaHints(), offer);
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));

  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());

  // Make sure the receive streams have not changed.
  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].name);
}

// Test receiving offers and creating answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestReceiveOfferCreateAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream2();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());

  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  const std::string session_id_orig = answer->session_id();
  const std::string session_version_orig = answer->session_version();

  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].name);

  mediastream_signaling_.UseOptionsWithStream1And2();
  offer = session_->CreateOffer(MediaHints());

  // Answer by turning off all send streams.
  mediastream_signaling_.UseOptionsReceiveOnly();
  answer = session_->CreateAnswer(MediaHints(), offer);
  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, answer->session_id());
  EXPECT_LT(talk_base::FromString<uint64>(session_version_orig),
            talk_base::FromString<uint64>(answer->session_version()));

  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));

  ASSERT_EQ(2u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->recv_streams()[0].name);
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[1].name);
  ASSERT_EQ(2u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->recv_streams()[0].name);
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[1].name);

  // Make sure we have no send streams.
  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());
}

// Test we will return fail when apply an offer that doesn't have
// crypto enabled.
TEST_F(WebRtcSessionTest, SetNonCryptoOffer) {
  WebRtcSessionTest::Init();

  desc_factory_->set_secure(cricket::SEC_DISABLED);
  cricket::MediaSessionOptions options;
  options.has_video = true;
  JsepSessionDescription* offer = CreateOfferSessionDescription(options);
  ASSERT_TRUE(offer != NULL);
  VerifyNoCryptoParams(offer->description());
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  EXPECT_FALSE(session_->SetRemoteDescription(JsepInterface::kOffer,
                                              offer));
  offer = CreateOfferSessionDescription(options);
  ASSERT_TRUE(offer != NULL);
  EXPECT_FALSE(session_->SetLocalDescription(JsepInterface::kOffer,
                                             offer));
}

// Test we will return fail when apply an answer that doesn't have
// crypto enabled.
TEST_F(WebRtcSessionTest, SetLocalNonCryptoAnswer) {
  WebRtcSessionTest::Init();
  SessionDescriptionInterface* offer = NULL;
  JsepSessionDescription* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
  EXPECT_FALSE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));
}

// Test we will return fail when apply an answer that doesn't have
// crypto enabled.
TEST_F(WebRtcSessionTest, SetRemoteNonCryptoAnswer) {
  WebRtcSessionTest::Init();
  SessionDescriptionInterface* offer = NULL;
  JsepSessionDescription* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_FALSE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
}

TEST_F(WebRtcSessionTest, TestSetLocalOfferTwice) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));

  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer2 = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer2));
}

TEST_F(WebRtcSessionTest, TestSetRemoteOfferTwice) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));

  SessionDescriptionInterface* offer2 = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer2));
}

TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  offer = session_->CreateOffer(MediaHints());
  EXPECT_FALSE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
}

TEST_F(WebRtcSessionTest, TestSetRemoteAndLocalOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
  offer = session_->CreateOffer(MediaHints());
  EXPECT_FALSE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
}

TEST_F(WebRtcSessionTest, TestSetLocalPrAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* pranswer = session_->CreateAnswer(MediaHints(),
                                                                 offer);
  SetRemoteDescription(JsepInterface::kOffer, offer,
                       BaseSession::STATE_RECEIVEDINITIATE);
  SetLocalDescription(JsepInterface::kPrAnswer, pranswer,
                      BaseSession::STATE_SENTPRACCEPT);

  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* pranswer2 = session_->CreateAnswer(MediaHints(),
                                                                  offer);
  SetLocalDescription(JsepInterface::kPrAnswer, pranswer2,
                      BaseSession::STATE_SENTPRACCEPT);

  mediastream_signaling_.UseOptionsWithStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  SetLocalDescription(JsepInterface::kAnswer, answer,
                      BaseSession::STATE_SENTACCEPT);
}

TEST_F(WebRtcSessionTest, TestSetRemotePrAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* pranswer = session_->CreateAnswer(MediaHints(),
                                                                 offer);
  SetLocalDescription(JsepInterface::kOffer, offer,
                      BaseSession::STATE_SENTINITIATE);
  SetRemoteDescription(JsepInterface::kPrAnswer, pranswer,
                       BaseSession::STATE_RECEIVEDPRACCEPT);

  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* pranswer2 = session_->CreateAnswer(MediaHints(),
                                                                  offer);
  SetRemoteDescription(JsepInterface::kPrAnswer, pranswer2,
                       BaseSession::STATE_RECEIVEDPRACCEPT);

  mediastream_signaling_.UseOptionsWithStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  SetRemoteDescription(JsepInterface::kAnswer, answer,
                       BaseSession::STATE_RECEIVEDACCEPT);
}

TEST_F(WebRtcSessionTest, TestSetLocalAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  EXPECT_FALSE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));
}

TEST_F(WebRtcSessionTest, TestSetRemoteAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  EXPECT_FALSE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
}

TEST_F(WebRtcSessionTest, TestAddRemoteCandidate) {
  WebRtcSessionTest::Init();

  cricket::Candidate candidate;
  candidate.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName0, 0, candidate);

  // Fail since we have not set a offer description.
  EXPECT_FALSE(session_->ProcessIceMessage(&ice_candidate1));

  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  // Candidate should be allowed to add before remote description.
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));
  candidate.set_component(2);
  JsepIceCandidate ice_candidate2(kMediaContentName0, 0, candidate);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));

  SessionDescriptionInterface* answer =
      session_->CreateAnswer(MediaHints(), offer);
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));

  // Verifying the candidates are copied properly from the dummy remote desc.
  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(2u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());
  EXPECT_EQ(kMediaContentName0, candidates->at(0)->sdp_mid());
  EXPECT_EQ(1, candidates->at(0)->candidate().component());
  EXPECT_EQ(2, candidates->at(1)->candidate().component());

  candidate.set_component(2);
  JsepIceCandidate ice_candidate3(kMediaContentName0, 0, candidate);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate3));
  ASSERT_EQ(3u, candidates->count());

  JsepIceCandidate bad_ice_candidate("bad content name", 99, candidate);
  EXPECT_FALSE(session_->ProcessIceMessage(&bad_ice_candidate));
}

// Test that a remote candidate is added to the remote session description and
// that it is retained if the remote session description is changed.
TEST_F(WebRtcSessionTest, TestRemoteCandidatesAddedToSessionDescription) {
  WebRtcSessionTest::Init();
  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName0, kMediaContentIndex0,
                                  candidate1);

  SetRemoteAndLocalSessionDescription();

  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));
  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(1u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());

  // Update the RemoteSessionDescription with a new session description and
  // a candidate and check that the new remote session description contains both
  // candidates.
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  cricket::Candidate candidate2;
  JsepIceCandidate ice_candidate2(kMediaContentName0, kMediaContentIndex0,
                                  candidate2);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate2));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));

  remote_desc = session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  candidates = remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(2u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());
  // Username and password have be updated with the TransportInfo of the
  // SessionDescription, won't be equal to the original one.
  candidate2.set_username(candidates->at(0)->candidate().username());
  candidate2.set_password(candidates->at(0)->candidate().password());
  EXPECT_TRUE(candidate2.IsEquivalent(candidates->at(0)->candidate()));
  EXPECT_EQ(kMediaContentIndex0, candidates->at(1)->sdp_mline_index());
  // No need to verify the username and password.
  candidate1.set_username(candidates->at(1)->candidate().username());
  candidate1.set_password(candidates->at(1)->candidate().password());
  EXPECT_TRUE(candidate1.IsEquivalent(candidates->at(1)->candidate()));

  // Test that the candidate is ignored if we can add the same candidate again.
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));
}

// Test that local candidates are added to the local session description and
// that they are retained if the local session description is changed.
TEST_F(WebRtcSessionTest, TestLocalCandidatesAddedToSessionDescription) {
  AddInterface(kClientAddr1);
  WebRtcSessionTest::Init();
  SetRemoteAndLocalSessionDescription();

  const SessionDescriptionInterface* local_desc = session_->local_description();
  const IceCandidateCollection* candidates =
      local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_EQ(0u, candidates->count());

  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);

  local_desc = session_->local_description();
  candidates = local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
  candidates = local_desc->candidates(1);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());

  // Update the session descriptions.
  mediastream_signaling_.UseOptionsWithStream1();
  SetRemoteAndLocalSessionDescription();

  local_desc = session_->local_description();
  candidates = local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
  candidates = local_desc->candidates(1);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
}

// Test that we can remove a media content from the local description even if it
// has candidates.
TEST_F(WebRtcSessionTest, TestRemoveMediaContentFromLocalSessionDesctription) {
  WebRtcSessionTest::Init();
  AddInterface(kClientAddr1);
  mediastream_signaling_.UseOptionsWithStream1(true);

  SetRemoteAndLocalSessionDescription();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);

  const SessionDescriptionInterface* local_desc = session_->local_description();
  ASSERT_EQ(2u, local_desc->number_of_mediasections());
  ASSERT_TRUE(local_desc->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, local_desc->candidates(kMediaContentIndex0)->count());
  ASSERT_TRUE(local_desc->candidates(kMediaContentIndex1) != NULL);
  EXPECT_LT(0u, local_desc->candidates(kMediaContentIndex1)->count());

  mediastream_signaling_.UseOptionsAudioOnly();
  SetRemoteAndLocalSessionDescription();

  // TODO(perkj): What can we expect here? Currently we only have one media
  // section. Shouldn't we keep the old one?
  // local_description has been updated in SetRemoteAndLocalSessionDescription.
  local_desc = session_->local_description();
  EXPECT_EQ(1u, local_desc->number_of_mediasections());
}

// Test that we can set a remote session description with remote candidates.
TEST_F(WebRtcSessionTest, TestSetRemoteSessionDescriptionWithCandidates) {
  WebRtcSessionTest::Init();

  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName0, kMediaContentIndex0,
                                 candidate1);
  mediastream_signaling_.UseOptionsReceiveOnly();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());

  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));

  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(1u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());

  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                              remote_desc);
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));
  // TODO: How do I check that the transport have got the
  // remote candidates?
}

// Test that offers and answers contains ice canidates when Ice candidates have
// been gathered.
TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteDescriptionWithCandidates) {
  AddInterface(kClientAddr1);
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsReceiveOnly();
  // Ice is started but candidates are not provided until SetLocalDescription
  // is called.
  EXPECT_EQ(0u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(0u, observer_.mline_1_candidates_.size());
  SetRemoteAndLocalSessionDescription();
  // Wait until at least one local candidate has been collected.
  EXPECT_TRUE_WAIT(0u < observer_.mline_0_candidates_.size(),
                   kIceCandidatesTimeout);
  EXPECT_TRUE_WAIT(0u < observer_.mline_1_candidates_.size(),
                   kIceCandidatesTimeout);

  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  ASSERT_TRUE(offer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, offer->candidates(kMediaContentIndex0)->count());
  ASSERT_TRUE(offer->candidates(kMediaContentIndex1) != NULL);
  EXPECT_LT(0u, offer->candidates(kMediaContentIndex1)->count());

  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  ASSERT_TRUE(answer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, answer->candidates(kMediaContentIndex0)->count());
  ASSERT_TRUE(answer->candidates(kMediaContentIndex1) != NULL);
  EXPECT_LT(0u, answer->candidates(kMediaContentIndex1)->count());

  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
}

// Verifies TransportProxy and media channels are created with content names
// present in the SessionDescription.
TEST_F(WebRtcSessionTest, TestChannelCreationsWithContentNames) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(MediaHints()));

  // CreateOffer creates session description with the content names "audio" and
  // "video". Goal is to modify these content names and verify transport channel
  // proxy in the BaseSession, as proxies are created with the content names
  // present in SDP.
  std::string sdp;
  EXPECT_TRUE(offer->ToString(&sdp));
  const std::string kAudioMid = "a=mid:audio";
  const std::string kAudioMidReplaceStr = "a=mid:audio_content_name";
  const std::string kVideoMid = "a=mid:video";
  const std::string kVideoMidReplaceStr = "a=mid:video_content_name";

  // Replacing |audio| with |audio_content_name|.
  talk_base::replace_substrs(kAudioMid.c_str(), kAudioMid.length(),
                             kAudioMidReplaceStr.c_str(),
                             kAudioMidReplaceStr.length(),
                             &sdp);
  // Replacing |video| with |video_content_name|.
  talk_base::replace_substrs(kVideoMid.c_str(), kVideoMid.length(),
                             kVideoMidReplaceStr.c_str(),
                             kVideoMidReplaceStr.length(),
                             &sdp);

  JsepSessionDescription* modified_offer(new JsepSessionDescription(
      JsepSessionDescription::kOffer));
  EXPECT_TRUE(modified_offer->Initialize(sdp));

  EXPECT_TRUE(session_->SetLocalDescription(
      JsepInterface::kOffer, modified_offer));
  EXPECT_TRUE(session_->GetTransportProxy("audio_content_name") != NULL);
  EXPECT_TRUE(session_->GetTransportProxy("video_content_name") != NULL);
  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* answer =
      session_->CreateAnswer(MediaHints(true, true), modified_offer);
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  ASSERT_TRUE((video_channel_ = media_engine_->GetVideoChannel(0)) != NULL);
  ASSERT_TRUE((voice_channel_ = media_engine_->GetVoiceChannel(0)) != NULL);

  // Trying to change the content name back to "audio" and "video". This should
  // fail as content names can be updated only in INIT state.
  EXPECT_FALSE(session_->SetLocalDescription(
      JsepInterface::kOffer, offer.get()));
}

// This test verifies the call setup when remote answer with audio only and
// later updates with video.
TEST_F(WebRtcSessionTest, TestAVOfferWithAudioOnlyAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());

  mediastream_signaling_.UseOptionsAudioOnly();
  SessionDescriptionInterface* answer =
      session_->CreateAnswer(MediaHints(true, false), offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(video_channel_ == NULL);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack1, voice_channel_->send_streams()[0].name);

  // Update the session descriptions, with Audio and Video.
  mediastream_signaling_.UseOptionsWithStream2();
  SetRemoteAndLocalSessionDescription();

  video_channel_ = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(video_channel_ != NULL);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].name);
  EXPECT_EQ(kVideoTrack2, video_channel_->send_streams()[0].name);
}

// This test verifies the call setup when remote answer with video only and
// later updates with audio.
TEST_F(WebRtcSessionTest, TestAVOfferWithVideoOnlyAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());

  mediastream_signaling_.UseOptionsVideoOnly();
  SessionDescriptionInterface* answer =
     session_->CreateAnswer(MediaHints(false, true), offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(voice_channel_ == NULL);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack1, video_channel_->send_streams()[0].name);

  // Update the session descriptions, with Audio and Video.
  mediastream_signaling_.UseOptionsWithStream2();
  SetRemoteAndLocalSessionDescription();

  voice_channel_ = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(voice_channel_ != NULL);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].name);
  EXPECT_EQ(kAudioTrack2, voice_channel_->send_streams()[0].name);
}


TEST_F(WebRtcSessionTest, TestDefaultSetSecurePolicy) {
  WebRtcSessionTest::Init();
  EXPECT_EQ(cricket::SEC_REQUIRED, session_->secure_policy());
}

TEST_F(WebRtcSessionTest, VerifyCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(MediaHints()));
  VerifyCryptoParams(offer->description(), true, true);
  const webrtc::SessionDescriptionInterface* answer =
      session_->CreateAnswer(MediaHints(), offer.get());
  VerifyCryptoParams(answer->description(), false, true);
}

TEST_F(WebRtcSessionTest, VerifyNoCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  session_->set_secure_policy(cricket::SEC_DISABLED);
  mediastream_signaling_.UseOptionsWithStream1();
  scoped_ptr<SessionDescriptionInterface> offer(
        session_->CreateOffer(MediaHints()));
  VerifyNoCryptoParams(offer->description());
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromNonCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromNonCryptoOffer();
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromCryptoOffer();
}

TEST_F(WebRtcSessionTest, VerifyBundleFlagInPA) {
  // This test verifies BUNDLE flag in PortAllocator, if BUNDLE information in
  // local description is removed by the application, BUNDLE flag should be
  // disabled in PortAllocator. By default BUNDLE is enabled in the WebRtc.
  WebRtcSessionTest::Init();
  EXPECT_TRUE((cricket::PORTALLOCATOR_ENABLE_BUNDLE & allocator_.flags()) ==
      cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(MediaHints()));
  cricket::SessionDescription* offer_copy =
      offer->description()->Copy();
  offer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  modified_offer->Initialize(offer_copy, "1", "1");

  session_->SetLocalDescription(JsepInterface::kOffer, modified_offer);
  EXPECT_FALSE(allocator_.flags() & cricket::PORTALLOCATOR_ENABLE_BUNDLE);
}

TEST_F(WebRtcSessionTest, TestDisabledBundleInAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1(true);
  EXPECT_TRUE((cricket::PORTALLOCATOR_ENABLE_BUNDLE & allocator_.flags()) ==
      cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  session_->SetLocalDescription(JsepInterface::kOffer, offer);
  mediastream_signaling_.UseOptionsWithStream2();
  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(MediaHints(), offer));
  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);
  modified_answer->Initialize(answer_copy, "1", "1");
  session_->SetRemoteDescription(JsepInterface::kAnswer, modified_answer);
  EXPECT_TRUE((cricket::PORTALLOCATOR_ENABLE_BUNDLE & allocator_.flags()) ==
      cricket::PORTALLOCATOR_ENABLE_BUNDLE);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].name);
}

TEST_F(WebRtcSessionTest, SetAudioPlayout) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->recv_streams().size());
  uint32 receive_ssrc  = channel->recv_streams()[0].first_ssrc();
  double left_vol, right_vol;
  EXPECT_TRUE(channel->GetOutputScaling(receive_ssrc, &left_vol, &right_vol));
  EXPECT_EQ(1, left_vol);
  EXPECT_EQ(1, right_vol);
  session_->SetAudioPlayout(kAudioTrack1, false);
  EXPECT_TRUE(channel->GetOutputScaling(receive_ssrc, &left_vol, &right_vol));
  EXPECT_EQ(0, left_vol);
  EXPECT_EQ(0, right_vol);
  session_->SetAudioPlayout(kAudioTrack1, true);
  EXPECT_TRUE(channel->GetOutputScaling(receive_ssrc, &left_vol, &right_vol));
  EXPECT_EQ(1, left_vol);
  EXPECT_EQ(1, right_vol);
}

TEST_F(WebRtcSessionTest, SetAudioSend) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32 send_ssrc  = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  session_->SetAudioSend(kAudioTrack1, false);
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  session_->SetAudioSend(kAudioTrack1, true);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
}

TEST_F(WebRtcSessionTest, SetVideoPlayout) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVideoMediaChannel* channel = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_LT(0u, channel->renderers().size());
  EXPECT_TRUE(channel->renderers().begin()->second == NULL);
  cricket::FakeVideoRenderer renderer;
  session_->SetVideoPlayout(kVideoTrack1, true, &renderer);
  EXPECT_TRUE(channel->renderers().begin()->second == &renderer);
  session_->SetVideoPlayout(kVideoTrack1, false, &renderer);
  EXPECT_TRUE(channel->renderers().begin()->second == NULL);
}

TEST_F(WebRtcSessionTest, SetVideoSend) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.UseOptionsWithStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVideoMediaChannel* channel = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32 send_ssrc  = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  session_->SetVideoSend(kVideoTrack1, false);
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  session_->SetVideoSend(kVideoTrack1, true);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
}

TEST_F(WebRtcSessionTest, TestInitiatorFlagAsOriginator) {
  WebRtcSessionTest::Init();
  EXPECT_FALSE(session_->initiator());
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                                   offer);
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kOffer, offer));
  EXPECT_TRUE(session_->initiator());
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kAnswer, answer));
  EXPECT_TRUE(session_->initiator());
}

TEST_F(WebRtcSessionTest, TestInitiatorFlagAsReceiver) {
  WebRtcSessionTest::Init();
  EXPECT_FALSE(session_->initiator());
  SessionDescriptionInterface* offer = session_->CreateOffer(MediaHints());
  SessionDescriptionInterface* answer = session_->CreateAnswer(MediaHints(),
                                                               offer);
  EXPECT_TRUE(session_->SetRemoteDescription(JsepInterface::kOffer, offer));
  EXPECT_FALSE(session_->initiator());
  EXPECT_TRUE(session_->SetLocalDescription(JsepInterface::kAnswer, answer));
  EXPECT_FALSE(session_->initiator());
}
