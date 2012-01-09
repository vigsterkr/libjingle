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
#include "talk/base/thread.h"
#include "talk/base/gunit.h"
#include "talk/p2p/client/fakeportallocator.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/mediasession.h"

class MockWebRtcSessionObserver : public webrtc::WebRtcSessionObserver {
 public:
  virtual void OnCandidatesReady(
      const std::vector<cricket::Candidate>& candidates) {
    for (cricket::Candidates::const_iterator iter = candidates.begin();
         iter != candidates.end(); ++iter) {
      candidates_.push_back(*iter);
    }
  }
  std::vector<cricket::Candidate> candidates_;
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

  using webrtc::WebRtcSession::ProvideOffer;
  using webrtc::WebRtcSession::SetRemoteSessionDescription;
  using webrtc::WebRtcSession::ProvideAnswer;
  using webrtc::WebRtcSession::NegotiationDone;
};

class WebRtcSessionTest : public testing::Test {
 protected:
  virtual void SetUp() {
    channel_manager_.reset(
        new cricket::ChannelManager(talk_base::Thread::Current()));
    port_allocator_.reset(
        new cricket::FakePortAllocator(talk_base::Thread::Current(), NULL));
    desc_factory_.reset(
        new cricket::MediaSessionDescriptionFactory(channel_manager_.get()));
  }

  bool InitializeSession() {
    return session_.get()->Initialize();
  }

  bool CheckChannels() {
    return (session_->voice_channel() != NULL &&
            session_->video_channel() != NULL);
  }

  void CheckTransportChannels() {
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, "rtp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_AUDIO, "rtcp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, "video_rtp") != NULL);
    EXPECT_TRUE(session_->GetChannel(cricket::CN_VIDEO, "video_rtcp") != NULL);
  }

  void Init() {
    ASSERT_TRUE(channel_manager_.get() != NULL);
    ASSERT_TRUE(session_.get() == NULL);
    EXPECT_TRUE(channel_manager_.get()->Init());
    session_.reset(new WebRtcSessionForTest(
        channel_manager_.get(), talk_base::Thread::Current(),
        talk_base::Thread::Current(), port_allocator_.get()));
    session_->RegisterObserver(&observer_);
    desc_provider_ = session_.get();
    EXPECT_TRUE(InitializeSession());
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

  void CreateOffer() {
    cricket::MediaSessionOptions options;
    options.has_video = true;
    session_->ProvideOffer(options);
    ASSERT_TRUE(session_->local_description() != NULL);
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
    // Change security parameter to SEC_REQUIRED.
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    PopulateFakeCandidates();
    session_->SetRemoteSessionDescription(offer, candidates_);
    const cricket::SessionDescription* answer =
        session_->ProvideAnswer(options);
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
    PopulateFakeCandidates();
    session_->SetRemoteSessionDescription(offer, candidates_);
    const cricket::SessionDescription* answer =
        session_->ProvideAnswer(options);
    ASSERT_TRUE(answer != NULL);
    VerifyCryptoParams(answer, false);
  }

  talk_base::scoped_ptr<cricket::PortAllocator> port_allocator_;
  webrtc::SessionDescriptionProvider* desc_provider_;
  talk_base::scoped_ptr<cricket::ChannelManager> channel_manager_;
  talk_base::scoped_ptr<cricket::MediaSessionDescriptionFactory> desc_factory_;
  talk_base::scoped_ptr<WebRtcSessionForTest> session_;
  MockWebRtcSessionObserver observer_;
  std::vector<cricket::Candidate> candidates_;
};

TEST_F(WebRtcSessionTest, TestInitialize) {
  WebRtcSessionTest::Init();
  EXPECT_TRUE(CheckChannels());
  CheckTransportChannels();
  talk_base::Thread::Current()->ProcessMessages(1000);
  EXPECT_EQ(4u, observer_.candidates_.size());
}

// TODO - Adding test cases for session.
TEST_F(WebRtcSessionTest, DISABLE_TestOfferAnswer) {
  WebRtcSessionTest::Init();
  EXPECT_TRUE(CheckChannels());
  CheckTransportChannels();
  talk_base::Thread::Current()->ProcessMessages(1);
}

TEST_F(WebRtcSessionTest, TestDefaultSetSecurePolicy) {
  WebRtcSessionTest::Init();
  EXPECT_EQ(cricket::SEC_REQUIRED, session_->secure_policy());
}

TEST_F(WebRtcSessionTest, VerifyCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  CreateOffer();
  VerifyCryptoParams(session_->local_description(), true);
}

TEST_F(WebRtcSessionTest, VerifyNoCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  session_->set_secure_policy(cricket::SEC_DISABLED);
  CreateOffer();
  VerifyNoCryptoParams(session_->local_description());
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromNonCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromNonCryptoOffer();
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromCryptoOffer) {
  WebRtcSessionTest::Init();
  VerifyAnswerFromCryptoOffer();
}
