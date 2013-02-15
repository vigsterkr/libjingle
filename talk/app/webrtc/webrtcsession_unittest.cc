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

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/webrtcsession.h"
#include "talk/base/fakenetwork.h"
#include "talk/base/firewallsocketserver.h"
#include "talk/base/gunit.h"
#include "talk/base/logging.h"
#include "talk/base/network.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/sslstreamadapter.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/base/virtualsocketserver.h"
#include "talk/media/base/fakemediaengine.h"
#include "talk/media/base/fakevideorenderer.h"
#include "talk/media/base/mediachannel.h"
#include "talk/media/devices/fakedevicemanager.h"
#include "talk/p2p/base/stunserver.h"
#include "talk/p2p/base/teststunserver.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/session/media/channelmanager.h"
#include "talk/session/media/mediasession.h"

#define MAYBE_SKIP_TEST(feature)                    \
  if (!(feature())) {                               \
    LOG(LS_INFO) << "Feature disabled... skipping"; \
    return;                                         \
  }

using cricket::kDtmfDelay;
using cricket::kDtmfReset;
using cricket::BaseSession;
using cricket::DF_PLAY;
using cricket::DF_SEND;
using cricket::FakeVoiceMediaChannel;
using cricket::NS_JINGLE_ICE_UDP;
using cricket::NS_GINGLE_P2P;
using cricket::TransportInfo;
using talk_base::scoped_ptr;
using talk_base::SocketAddress;
using webrtc::CreateSessionDescription;
using webrtc::IceCandidateCollection;
using webrtc::FakeConstraints;
using webrtc::JsepSessionDescription;
using webrtc::JsepIceCandidate;
using webrtc::kMlineMismatch;
using webrtc::kSdpWithoutCrypto;
using webrtc::kUpdateStateFailed;
using webrtc::SessionDescriptionInterface;
using webrtc::PeerConnectionInterface;

static const SocketAddress kClientAddr1("11.11.11.11", 0);
static const SocketAddress kClientAddr2("22.22.22.22", 0);
static const SocketAddress kStunAddr("99.99.99.1", cricket::STUN_SERVER_PORT);

static const char kSessionVersion[] = "1";

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
static const char kMediaContentName1[] = "video";

static const int kIceCandidatesTimeout = 10000;

static const cricket::AudioCodec
    kTelephoneEventCodec(106, "telephone-event", 8000, 0, 1, 0);

// Add some extra |newlines| to the |message| after |line|.
static void InjectAfter(const std::string& line,
                        const std::string& newlines,
                        std::string* message) {
  const std::string tmp = line + newlines;
  talk_base::replace_substrs(line.c_str(), line.length(),
                             tmp.c_str(), tmp.length(), message);
}

class MockIceObserver : public webrtc::IceObserver {
 public:
  MockIceObserver()
      : oncandidatesready_(false),
        ice_connection_state_(PeerConnectionInterface::kIceConnectionNew),
        ice_gathering_state_(PeerConnectionInterface::kIceGatheringNew) {
  }

  virtual void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) {
    ice_connection_state_ = new_state;
  }
  virtual void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) {
    // We can never transition back to "new".
    EXPECT_NE(PeerConnectionInterface::kIceGatheringNew, new_state);
    ice_gathering_state_ = new_state;

    // oncandidatesready_ really means "ICE gathering is complete".
    // This if statement ensures that this value remains correct when we
    // transition from kIceGatheringComplete to kIceGatheringGathering.
    if (new_state == PeerConnectionInterface::kIceGatheringGathering) {
      oncandidatesready_ = false;
    }
  }

  // Found a new candidate.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    if (candidate->sdp_mline_index() == kMediaContentIndex0) {
      mline_0_candidates_.push_back(candidate->candidate());
    } else if (candidate->sdp_mline_index() == kMediaContentIndex1) {
      mline_1_candidates_.push_back(candidate->candidate());
    }
    // The ICE gathering state should always be Gathering when a candidate is
    // received (or possibly Completed in the case of the final candidate).
    EXPECT_NE(PeerConnectionInterface::kIceGatheringNew, ice_gathering_state_);
  }

  // TODO(bemasc): Remove this once callers transition to OnIceGatheringChange.
  virtual void OnIceComplete() {
    EXPECT_FALSE(oncandidatesready_);
    oncandidatesready_ = true;

    // OnIceGatheringChange(IceGatheringCompleted) and OnIceComplete() should
    // be called approximately simultaneously.  For ease of testing, this
    // check additionally requires that they be called in the above order.
    EXPECT_EQ(PeerConnectionInterface::kIceGatheringComplete,
              ice_gathering_state_);
  }

  bool oncandidatesready_;
  std::vector<cricket::Candidate> mline_0_candidates_;
  std::vector<cricket::Candidate> mline_1_candidates_;
  PeerConnectionInterface::IceConnectionState ice_connection_state_;
  PeerConnectionInterface::IceGatheringState ice_gathering_state_;
};

class WebRtcSessionForTest : public webrtc::WebRtcSession {
 public:
  WebRtcSessionForTest(cricket::ChannelManager* cmgr,
                       talk_base::Thread* signaling_thread,
                       talk_base::Thread* worker_thread,
                       cricket::PortAllocator* port_allocator,
                       webrtc::IceObserver* ice_observer,
                       webrtc::MediaStreamSignaling* mediastream_signaling)
    : WebRtcSession(cmgr, signaling_thread, worker_thread, port_allocator,
                    mediastream_signaling) {
    RegisterIceObserver(ice_observer);
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

  void SendAudioVideoStream1() {
    talk_base::scoped_refptr<webrtc::StreamCollection> streams(
        webrtc::StreamCollection::Create());
    streams->AddStream(CreateStream(kStream1, kAudioTrack1, kVideoTrack1));
    SetLocalStreams(streams);
  }

  void SendAudioVideoStream2() {
    talk_base::scoped_refptr<webrtc::StreamCollection> streams(
        webrtc::StreamCollection::Create());
    streams->AddStream(CreateStream(kStream2, kAudioTrack2, kVideoTrack2));
    SetLocalStreams(streams);
  }

  void SendAudioVideoStream1And2() {
    talk_base::scoped_refptr<webrtc::StreamCollection> streams(
        webrtc::StreamCollection::Create());
    streams->AddStream(CreateStream(kStream1, kAudioTrack1, kVideoTrack1));
    streams->AddStream(CreateStream(kStream2, kAudioTrack2, kVideoTrack2));
    SetLocalStreams(streams);
  }

  void SendNothing() {
    SetLocalStreams(webrtc::StreamCollection::Create());
  }

  void UseOptionsAudioOnly() {
    talk_base::scoped_refptr<webrtc::StreamCollection> streams(
        webrtc::StreamCollection::Create());
    streams->AddStream(CreateStream(kStream2, kAudioTrack2, ""));
    SetLocalStreams(streams);
  }

  void UseOptionsVideoOnly() {
    talk_base::scoped_refptr<webrtc::StreamCollection> streams(
        webrtc::StreamCollection::Create());
    streams->AddStream(CreateStream(kStream2, kAudioTrack1, kVideoTrack2));
    SetLocalStreams(streams);
  }

  // Implements RemoteMediaStreamObserver.
  virtual void OnAddStream(webrtc::MediaStreamInterface* stream) {
  }
  virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream) {
  }
  virtual void OnAddDataChannel(webrtc::DataChannelInterface* data_channel) {
  }


 private:
  talk_base::scoped_refptr<webrtc::MediaStreamInterface> CreateStream(
      const std::string& stream_label,
      const std::string& audio_track_id,
      const std::string& video_track_id) {
    talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream(
        webrtc::MediaStream::Create(stream_label));

    if (!audio_track_id.empty()) {
      talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
          webrtc::AudioTrack::Create(audio_track_id, NULL));
      stream->AddTrack(audio_track);
    }

    if (!video_track_id.empty()) {
      talk_base::scoped_refptr<webrtc::VideoTrackInterface> video_track(
          webrtc::VideoTrack::Create(video_track_id, NULL));
      stream->AddTrack(video_track);
    }
    return stream;
  }

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
    tdesc_factory_->set_protocol(cricket::ICEPROTO_HYBRID);
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

    EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
        observer_.ice_connection_state_);
    EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
        observer_.ice_gathering_state_);

    EXPECT_TRUE(session_->Initialize(constraints_.get()));
  }

  void InitWithDtmfCodec() {
    // Add kTelephoneEventCodec for dtmf test.
    std::vector<cricket::AudioCodec> codecs;
    codecs.push_back(kTelephoneEventCodec);
    media_engine_->SetAudioCodecs(codecs);
    Init();
  }

  void InitWithDtls() {
    constraints_.reset(new FakeConstraints());
    constraints_->AddOptional(
        webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
        webrtc::MediaConstraintsInterface::kValueTrue);

    Init();
  }

  // Creates a local offer and applies it. Starts ice.
  // Call mediastream_signaling_.UseOptionsWithStreamX() before this function
  // to decide which streams to create.
  void InitiateCall() {
    SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
    SetLocalDescriptionWithoutError(offer);
    EXPECT_TRUE_WAIT(PeerConnectionInterface::kIceGatheringNew !=
        observer_.ice_gathering_state_,
        kIceCandidatesTimeout);
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

  void VerifyCryptoParams(const cricket::SessionDescription* sdp) {
    ASSERT_TRUE(session_.get() != NULL);
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    ASSERT_EQ(1U, audio_content->cryptos().size());
    ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
    ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
              audio_content->cryptos()[0].cipher_suite);
    EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
              audio_content->protocol());

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
    EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
              video_content->protocol());
  }

  void VerifyNoCryptoParams(const cricket::SessionDescription* sdp, bool dtls) {
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

    if (dtls) {
      EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
                audio_content->protocol());
      EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
                video_content->protocol());
    } else {
      EXPECT_EQ(std::string(cricket::kMediaProtocolAvpf),
                audio_content->protocol());
      EXPECT_EQ(std::string(cricket::kMediaProtocolAvpf),
                video_content->protocol());
    }
  }

  // Set the internal fake description factories to do DTLS-SRTP.
  void SetFactoryDtlsSrtp() {
    desc_factory_->set_secure(cricket::SEC_ENABLED);
    std::string identity_name = "WebRTC" +
        talk_base::ToString(talk_base::CreateRandomId());
    tdesc_factory_->set_identity(talk_base::SSLIdentity::Generate(
        identity_name));
    tdesc_factory_->set_digest_algorithm(talk_base::DIGEST_SHA_256);
    tdesc_factory_->set_secure(cricket::SEC_REQUIRED);
  }

  void VerifyFingerprintStatus(const cricket::SessionDescription* sdp,
                               bool expected) {
    const TransportInfo* audio = sdp->GetTransportInfoByName("audio");
    ASSERT_TRUE(audio != NULL);
    ASSERT_EQ(expected, audio->description.identity_fingerprint.get() != NULL);
    if (expected) {
      ASSERT_EQ(std::string(talk_base::DIGEST_SHA_256), audio->description.
                identity_fingerprint->algorithm);
    }
    const TransportInfo* video = sdp->GetTransportInfoByName("video");
    ASSERT_TRUE(video != NULL);
    ASSERT_EQ(expected, video->description.identity_fingerprint.get() != NULL);
    if (expected) {
      ASSERT_EQ(std::string(talk_base::DIGEST_SHA_256), video->description.
                identity_fingerprint->algorithm);
    }
  }

  void VerifyAnswerFromNonCryptoOffer() {
    // Create a SDP without Crypto.
    cricket::MediaSessionOptions options;
    options.has_video = true;
    scoped_ptr<JsepSessionDescription> offer(
        CreateOfferSessionDescription(options, cricket::SEC_DISABLED));
    ASSERT_TRUE(offer.get() != NULL);
    VerifyNoCryptoParams(offer->description(), false);
    const webrtc::SessionDescriptionInterface* answer =
        session_->CreateAnswer(NULL, offer.get());
    // Answer should be NULL as no crypto params in offer.
    ASSERT_TRUE(answer == NULL);
  }

  void VerifyAnswerFromCryptoOffer() {
    cricket::MediaSessionOptions options;
    options.has_video = true;
    options.bundle_enabled = true;
    scoped_ptr<JsepSessionDescription> offer(
        CreateOfferSessionDescription(options, cricket::SEC_REQUIRED));
    ASSERT_TRUE(offer.get() != NULL);
    VerifyCryptoParams(offer->description());
    scoped_ptr<SessionDescriptionInterface> answer(
        session_->CreateAnswer(NULL, offer.get()));
    ASSERT_TRUE(answer.get() != NULL);
    VerifyCryptoParams(answer->description());
  }

  void CompareIceUfragAndPassword(const cricket::SessionDescription* desc1,
                                  const cricket::SessionDescription* desc2,
                                  bool expect_equal) {
    if (desc1->contents().size() != desc2->contents().size()) {
      EXPECT_FALSE(expect_equal);
      return;
    }

    const cricket::ContentInfos& contents = desc1->contents();
    cricket::ContentInfos::const_iterator it = contents.begin();

    for (; it != contents.end(); ++it) {
      const cricket::TransportDescription* transport_desc1 =
          desc1->GetTransportDescriptionByName(it->name);
      const cricket::TransportDescription* transport_desc2 =
          desc2->GetTransportDescriptionByName(it->name);
      if (!transport_desc1 || !transport_desc2) {
        EXPECT_FALSE(expect_equal);
        return;
      }
      if (transport_desc1->ice_pwd != transport_desc2->ice_pwd ||
          transport_desc1->ice_ufrag != transport_desc2->ice_ufrag) {
        EXPECT_FALSE(expect_equal);
        return;
      }
    }
    EXPECT_TRUE(expect_equal);
  }
  // Creates and offer and an answer and applies it on the offer.
  // Call mediastream_signaling_.UseOptionsWithStreamX() before this function
  // to decide which streams to create.
  void SetRemoteAndLocalSessionDescription() {
    SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
    SessionDescriptionInterface* answer = session_->CreateAnswer(NULL, offer);
    SetRemoteDescriptionWithoutError(offer);
    SetLocalDescriptionWithoutError(answer);
  }
  void SetLocalDescriptionWithoutError(SessionDescriptionInterface* desc) {
    EXPECT_TRUE(session_->SetLocalDescription(desc, NULL));
  }
  void SetLocalDescriptionExpectState(SessionDescriptionInterface* desc,
                                      BaseSession::State expected_state) {
    SetLocalDescriptionWithoutError(desc);
    EXPECT_EQ(expected_state, session_->state());
  }
  void SetLocalDescriptionExpectError(const std::string& expected_error,
                                      SessionDescriptionInterface* desc) {
    std::string error;
    EXPECT_FALSE(session_->SetLocalDescription(desc, &error));
    EXPECT_NE(std::string::npos, error.find("SetLocalDescription failed"));
    EXPECT_NE(std::string::npos, error.find(expected_error));
  }
  void SetRemoteDescriptionWithoutError(SessionDescriptionInterface* desc) {
    EXPECT_TRUE(session_->SetRemoteDescription(desc, NULL));
  }
  void SetRemoteDescriptionExpectState(SessionDescriptionInterface* desc,
                                       BaseSession::State expected_state) {
    SetRemoteDescriptionWithoutError(desc);
    EXPECT_EQ(expected_state, session_->state());
  }
  void SetRemoteDescriptionExpectError(const std::string& expected_error,
                                       SessionDescriptionInterface* desc) {
    std::string error;
    EXPECT_FALSE(session_->SetRemoteDescription(desc, &error));
    EXPECT_NE(std::string::npos, error.find("SetRemoteDescription failed"));
    EXPECT_NE(std::string::npos, error.find(expected_error));
  }

  void CreateCryptoOfferAndNonCryptoAnswer(SessionDescriptionInterface** offer,
      SessionDescriptionInterface** nocrypto_answer) {
    mediastream_signaling_.SendAudioVideoStream2();
    *offer = session_->CreateOffer(NULL);

    mediastream_signaling_.SendAudioVideoStream1();
    talk_base::scoped_ptr<SessionDescriptionInterface> answer(
        session_->CreateAnswer(NULL, *offer));
    std::string nocrypto_answer_str;
    answer->ToString(&nocrypto_answer_str);
    // Disable the crypto
    const std::string kCrypto = "a=crypto";
    const std::string kCryptoX = "a=cryptx";
    talk_base::replace_substrs(kCrypto.c_str(), kCrypto.length(),
                               kCryptoX.c_str(), kCryptoX.length(),
                               &nocrypto_answer_str);
    *nocrypto_answer = CreateSessionDescription(
        JsepSessionDescription::kAnswer, nocrypto_answer_str, NULL);
    EXPECT_TRUE(*nocrypto_answer != NULL);
  }
  JsepSessionDescription* CreateOfferSessionDescriptionWithVersion(
        cricket::MediaSessionOptions options,
        cricket::SecurePolicy secure_policy,
        const std::string& session_version,
        const SessionDescriptionInterface* current_desc) {
    std::string session_id = talk_base::ToString(talk_base::CreateRandomId());
    const cricket::SessionDescription* cricket_desc = NULL;
    if (current_desc) {
      cricket_desc = current_desc->description();
      session_id = current_desc->session_id();
    }

    desc_factory_->set_secure(secure_policy);
    JsepSessionDescription* offer(
        new JsepSessionDescription(JsepSessionDescription::kOffer));
    if (!offer->Initialize(desc_factory_->CreateOffer(options, cricket_desc),
                           session_id, session_version)) {
      delete offer;
      offer = NULL;
    }
    return offer;
  }
  JsepSessionDescription* CreateOfferSessionDescription(
      cricket::MediaSessionOptions options) {
    return CreateOfferSessionDescriptionWithVersion(options,
                                                    cricket::SEC_ENABLED,
                                                    kSessionVersion, NULL);
  }
  JsepSessionDescription* CreateOfferSessionDescription(
      cricket::MediaSessionOptions options, cricket::SecurePolicy policy) {
    return CreateOfferSessionDescriptionWithVersion(options,
                                                    policy,
                                                    kSessionVersion, NULL);
  }
  JsepSessionDescription* CreateUpdatedOfferSessionDescription(
      cricket::MediaSessionOptions options,
      const SessionDescriptionInterface* current_desc) {
    return CreateOfferSessionDescriptionWithVersion(options,
                                                    cricket::SEC_ENABLED,
                                                    kSessionVersion,
                                                    current_desc);
  }

  JsepSessionDescription* CreateAnswerSessionDescription(
      SessionDescriptionInterface* offer,
      cricket::MediaSessionOptions options) {
    desc_factory_->set_secure(cricket::SEC_REQUIRED);
    const std::string session_id =
        talk_base::ToString(talk_base::CreateRandomId());
    JsepSessionDescription* answer(
        new JsepSessionDescription(JsepSessionDescription::kAnswer));
    if (!answer->Initialize(desc_factory_->CreateAnswer(offer->description(),
                                                        options, NULL),
                            session_id, kSessionVersion)) {
      delete answer;
      answer = NULL;
    }
    return answer;
  }

  void TestSessionCandidatesWithBundleRtcpMux(bool bundle, bool rtcp_mux) {
    AddInterface(kClientAddr1);
    WebRtcSessionTest::Init();
    mediastream_signaling_.SendAudioVideoStream1();
    FakeConstraints constraints;
    constraints.SetMandatoryUseRtpMux(bundle);
    SessionDescriptionInterface* offer = session_->CreateOffer(&constraints);
    mediastream_signaling_.SendAudioVideoStream2();
    SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                                 offer);
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));

    size_t expected_candidate_num = 2;
    if (!rtcp_mux) {
      // If rtcp_mux is enabled we should expect 4 candidates - host and srflex
      // for rtp and rtcp.
      expected_candidate_num = 4;
      // Disable rtcp-mux from the answer

      const std::string kRtcpMux = "a=rtcp-mux";
      const std::string kXRtcpMux = "a=xrtcp-mux";
      talk_base::replace_substrs(kRtcpMux.c_str(), kRtcpMux.length(),
                                 kXRtcpMux.c_str(), kXRtcpMux.length(),
                                 &sdp);
    }

    SessionDescriptionInterface* new_answer = CreateSessionDescription(
        JsepSessionDescription::kAnswer, sdp, NULL);
    delete answer;
    answer = new_answer;
    // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
    // and answer.
    SetLocalDescriptionWithoutError(offer);
    // SetRemoteDescription to enable rtcp mux.
    SetRemoteDescriptionWithoutError(answer);
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
  // Tests that we can only send DTMF when the dtmf codec is supported.
  void TestCanInsertDtmf(bool can) {
    if (can) {
      WebRtcSessionTest::InitWithDtmfCodec();
    } else {
      WebRtcSessionTest::Init();
    }
    mediastream_signaling_.SendAudioVideoStream1();
    SetRemoteAndLocalSessionDescription();
    EXPECT_FALSE(session_->CanInsertDtmf(""));
    EXPECT_EQ(can, session_->CanInsertDtmf(kAudioTrack1));
  }

  // The method sets up a call from the session to itself, in a loopback
  // arrangement.  It also uses a firewall rule to create a temporary
  // disconnection.  This code is placed as a method so that it can be invoked
  // by multiple tests with different allocators (e.g. with and without BUNDLE).
  // While running the call, this method also checks if the session goes through
  // the correct sequence of ICE states when a connection is established,
  // broken, and re-established.
  // The Connection state should go:
  // New -> Checking -> Connected -> Disconnected -> Connected.
  // The Gathering state should go: New -> Gathering -> Completed.
  void TestLoopbackCall() {
    AddInterface(kClientAddr1);
    Init();
    mediastream_signaling_.SendAudioVideoStream1();
    SessionDescriptionInterface* offer = session_->CreateOffer(NULL);

    EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
              observer_.ice_gathering_state_);
    SetLocalDescriptionWithoutError(offer);
    EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
              observer_.ice_connection_state_);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceGatheringGathering,
                   observer_.ice_gathering_state_,
                   kIceCandidatesTimeout);
    EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceGatheringComplete,
                   observer_.ice_gathering_state_,
                   kIceCandidatesTimeout);

    std::string sdp;
    offer->ToString(&sdp);
    SessionDescriptionInterface* desc =
        webrtc::CreateSessionDescription(JsepSessionDescription::kAnswer, sdp);
    ASSERT_TRUE(desc != NULL);
    SetRemoteDescriptionWithoutError(desc);

    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionChecking,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionConnected,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);
    // TODO(bemasc): EXPECT(Completed) once the details are standardized.

    // Adding firewall rule to block ping requests, which should cause
    // transport channel failure.
    fss_->AddRule(false, talk_base::FP_ANY, talk_base::FD_ANY, kClientAddr1);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionDisconnected,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);

    // Clearing the rules, session should move back to completed state.
    fss_->ClearRules();
    // Session is automatically calling OnSignalingReady after creation of
    // new portallocator session which will allocate new set of candidates.

    // TODO(bemasc): Change this to Completed once the details are standardized.
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionConnected,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);
  }

  void VerifyTransportType(const std::string& content_name,
                           cricket::TransportProtocol protocol) {
    const cricket::Transport* transport = session_->GetTransport(content_name);
    ASSERT_TRUE(transport != NULL);
    EXPECT_EQ(protocol, transport->protocol());
  }

  // Create a remote offer with audio and video content.
  JsepSessionDescription* CreateRemoteOffer() {
    cricket::MediaSessionOptions options;
    options.has_audio = true;
    options.has_video = true;
    return CreateOfferSessionDescription(options);
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
  talk_base::scoped_ptr<FakeConstraints> constraints_;
  FakeMediaStreamSignaling mediastream_signaling_;
  talk_base::scoped_ptr<WebRtcSessionForTest> session_;
  MockIceObserver observer_;
  cricket::FakeVideoMediaChannel* video_channel_;
  cricket::FakeVoiceMediaChannel* voice_channel_;
};

TEST_F(WebRtcSessionTest, TestInitialize) {
  WebRtcSessionTest::Init();
}

TEST_F(WebRtcSessionTest, TestInitializeWithDtls) {
  WebRtcSessionTest::InitWithDtls();
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

TEST_F(WebRtcSessionTest, TestMultihomeCandidates) {
  AddInterface(kClientAddr1);
  AddInterface(kClientAddr2);
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
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
  mediastream_signaling_.SendAudioVideoStream1();
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
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  const std::string session_id_orig = offer->session_id();
  const std::string session_version_orig = offer->session_version();

  mediastream_signaling_.SendAudioVideoStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

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
  mediastream_signaling_.SendNothing();
  offer = session_->CreateOffer(NULL);

  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, offer->session_id());
  EXPECT_LT(talk_base::FromString<uint64>(session_version_orig),
            talk_base::FromString<uint64>(offer->session_version()));

  SetLocalDescriptionWithoutError(offer);

  mediastream_signaling_.SendAudioVideoStream2();
  answer = session_->CreateAnswer(NULL, offer);
  SetRemoteDescriptionWithoutError(answer);

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
  mediastream_signaling_.SendAudioVideoStream2();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);

  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  const std::string session_id_orig = answer->session_id();
  const std::string session_version_orig = answer->session_version();

  SetRemoteDescriptionWithoutError(offer);
  SetLocalDescriptionWithoutError(answer);

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

  mediastream_signaling_.SendAudioVideoStream1And2();
  offer = session_->CreateOffer(NULL);

  // Answer by turning off all send streams.
  mediastream_signaling_.SendNothing();
  answer = session_->CreateAnswer(NULL, offer);
  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, answer->session_id());
  EXPECT_LT(talk_base::FromString<uint64>(session_version_orig),
            talk_base::FromString<uint64>(answer->session_version()));

  SetRemoteDescriptionWithoutError(offer);
  SetLocalDescriptionWithoutError(answer);

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
  cricket::MediaSessionOptions options;
  options.has_video = true;
  JsepSessionDescription* offer = CreateOfferSessionDescription(
      options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  VerifyNoCryptoParams(offer->description(), false);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionExpectError(kSdpWithoutCrypto, offer);
  offer = CreateOfferSessionDescription(options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  SetLocalDescriptionExpectError(kSdpWithoutCrypto, offer);
}

// Test we will return fail when apply an answer that doesn't have
// crypto enabled.
TEST_F(WebRtcSessionTest, SetLocalNonCryptoAnswer) {
  WebRtcSessionTest::Init();
  SessionDescriptionInterface* offer = NULL;
  SessionDescriptionInterface* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionWithoutError(offer);
  SetLocalDescriptionExpectError(kSdpWithoutCrypto, answer);
}

// Test we will return fail when apply an answer that doesn't have
// crypto enabled.
TEST_F(WebRtcSessionTest, SetRemoteNonCryptoAnswer) {
  WebRtcSessionTest::Init();
  SessionDescriptionInterface* offer = NULL;
  SessionDescriptionInterface* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionExpectError(kSdpWithoutCrypto, answer);
}

// Test that we can create and set an offer with a DTLS fingerprint.
TEST_F(WebRtcSessionTest, CreateSetDtlsOffer) {
  MAYBE_SKIP_TEST(talk_base::SSLStreamAdapter::HaveDtlsSrtp);
  WebRtcSessionTest::InitWithDtls();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), true);
  // SetLocalDescription will take the ownership of the offer.
  SetLocalDescriptionWithoutError(offer);
}

// Test that we can process an offer with a DTLS fingerprint
// and that we return an answer with a fingerprint.
TEST_F(WebRtcSessionTest, ReceiveDtlsOfferCreateAnswer) {
  MAYBE_SKIP_TEST(talk_base::SSLStreamAdapter::HaveDtlsSrtp);
  WebRtcSessionTest::InitWithDtls();
  mediastream_signaling_.SendAudioVideoStream1();
  SetFactoryDtlsSrtp();
  cricket::MediaSessionOptions options;
  options.has_video = true;
  JsepSessionDescription* offer = CreateOfferSessionDescription(options);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), true);

  // SetRemoteDescription will take the ownership of the offer.
  SetRemoteDescriptionWithoutError(offer);

  // Verify that we get a crypto fingerprint in the answer.
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), true);
  // Check that we don't have an a=crypto line in the answer.
  VerifyNoCryptoParams(answer->description(), true);

  // Now set the local description, which should work, even without a=crypto.
  SetLocalDescriptionWithoutError(answer);
}

// Test that even if we support DTLS, if the other side didn't offer a
// fingerprint, we don't either.
TEST_F(WebRtcSessionTest, ReceiveNoDtlsOfferCreateAnswer) {
  MAYBE_SKIP_TEST(talk_base::SSLStreamAdapter::HaveDtlsSrtp);
  WebRtcSessionTest::InitWithDtls();
  mediastream_signaling_.SendAudioVideoStream1();
  cricket::MediaSessionOptions options;
  options.has_video = true;
  JsepSessionDescription* offer = CreateOfferSessionDescription(
      options, cricket::SEC_REQUIRED);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), false);

  // SetRemoteDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionWithoutError(offer);

  // Verify that we don't get a crypto fingerprint in the answer.
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), false);

  // Now set the local description.
  SetLocalDescriptionWithoutError(answer);
}

TEST_F(WebRtcSessionTest, TestSetLocalOfferTwice) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);

  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer2 = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer2);
}

TEST_F(WebRtcSessionTest, TestSetRemoteOfferTwice) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer);

  SessionDescriptionInterface* offer2 = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer2);
}

TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);
  offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionExpectError(
      "Called with type in wrong state, type: offer state: STATE_SENTINITIATE",
      offer);
}

TEST_F(WebRtcSessionTest, TestSetRemoteAndLocalOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer);
  offer = session_->CreateOffer(NULL);
  SetLocalDescriptionExpectError(
      "Called with type in wrong state, type: "
      "offer state: STATE_RECEIVEDINITIATE",
      offer);
}

TEST_F(WebRtcSessionTest, TestSetLocalPrAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  JsepSessionDescription* pranswer = static_cast<JsepSessionDescription*>(
      session_->CreateAnswer(NULL, offer));
  pranswer->set_type(SessionDescriptionInterface::kPrAnswer);
  SetRemoteDescriptionExpectState(offer, BaseSession::STATE_RECEIVEDINITIATE);
  SetLocalDescriptionExpectState(pranswer, BaseSession::STATE_SENTPRACCEPT);

  mediastream_signaling_.SendAudioVideoStream1();
  JsepSessionDescription* pranswer2 = static_cast<JsepSessionDescription*>(
      session_->CreateAnswer(NULL, offer));
  pranswer2->set_type(SessionDescriptionInterface::kPrAnswer);

  SetLocalDescriptionExpectState(pranswer2, BaseSession::STATE_SENTPRACCEPT);

  mediastream_signaling_.SendAudioVideoStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetLocalDescriptionExpectState(answer, BaseSession::STATE_SENTACCEPT);
}

TEST_F(WebRtcSessionTest, TestSetRemotePrAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  JsepSessionDescription* pranswer = static_cast<JsepSessionDescription*>(
      session_->CreateAnswer(NULL, offer));
  pranswer->set_type(SessionDescriptionInterface::kPrAnswer);

  SetLocalDescriptionExpectState(offer, BaseSession::STATE_SENTINITIATE);
  SetRemoteDescriptionExpectState(pranswer,
                                  BaseSession::STATE_RECEIVEDPRACCEPT);

  mediastream_signaling_.SendAudioVideoStream1();

  JsepSessionDescription* pranswer2 = static_cast<JsepSessionDescription*>(
      session_->CreateAnswer(NULL, offer));
  pranswer2->set_type(SessionDescriptionInterface::kPrAnswer);

  SetRemoteDescriptionExpectState(pranswer2,
                                  BaseSession::STATE_RECEIVEDPRACCEPT);

  mediastream_signaling_.SendAudioVideoStream2();
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetRemoteDescriptionExpectState(answer, BaseSession::STATE_RECEIVEDACCEPT);
}

TEST_F(WebRtcSessionTest, TestSetLocalAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetLocalDescriptionExpectError(
      "Called with type in wrong state, type: answer state: STATE_INIT",
      answer);
}

TEST_F(WebRtcSessionTest, TestSetRemoteAnswerWithoutOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendNothing();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetRemoteDescriptionExpectError(
      "Called with type in wrong state, type: answer state: STATE_INIT",
      answer);
}

TEST_F(WebRtcSessionTest, TestAddRemoteCandidate) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();

  cricket::Candidate candidate;
  candidate.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName0, 0, candidate);

  // Fail since we have not set a offer description.
  EXPECT_FALSE(session_->ProcessIceMessage(&ice_candidate1));

  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);
  // Candidate should be allowed to add before remote description.
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));
  candidate.set_component(2);
  JsepIceCandidate ice_candidate2(kMediaContentName0, 0, candidate);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));

  SessionDescriptionInterface* answer =
      session_->CreateAnswer(NULL, offer);
  SetRemoteDescriptionWithoutError(answer);

  // Verifying the candidates are copied properly from internal vector.
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
  mediastream_signaling_.SendAudioVideoStream1();
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
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  cricket::Candidate candidate2;
  JsepIceCandidate ice_candidate2(kMediaContentName0, kMediaContentIndex0,
                                  candidate2);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate2));
  SetRemoteDescriptionWithoutError(offer);

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
  mediastream_signaling_.SendAudioVideoStream1();
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
  mediastream_signaling_.SendAudioVideoStream1();
  SetRemoteAndLocalSessionDescription();

  local_desc = session_->local_description();
  candidates = local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
  candidates = local_desc->candidates(1);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
}

// Test that we can set a remote session description with remote candidates.
TEST_F(WebRtcSessionTest, TestSetRemoteSessionDescriptionWithCandidates) {
  WebRtcSessionTest::Init();

  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName0, kMediaContentIndex0,
                                 candidate1);
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);

  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  SetRemoteDescriptionWithoutError(offer);

  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(1u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());

  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                              remote_desc);
  SetLocalDescriptionWithoutError(answer);
  // TODO: How do I check that the transport have got the
  // remote candidates?
}

// Test that offers and answers contains ice candidates when Ice candidates have
// been gathered.
TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteDescriptionWithCandidates) {
  AddInterface(kClientAddr1);
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
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

  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  ASSERT_TRUE(offer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, offer->candidates(kMediaContentIndex0)->count());
  ASSERT_TRUE(offer->candidates(kMediaContentIndex1) != NULL);
  EXPECT_LT(0u, offer->candidates(kMediaContentIndex1)->count());

  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  ASSERT_TRUE(answer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, answer->candidates(kMediaContentIndex0)->count());
  ASSERT_TRUE(answer->candidates(kMediaContentIndex1) != NULL);
  EXPECT_LT(0u, answer->candidates(kMediaContentIndex1)->count());

  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);
}

// Verifies TransportProxy and media channels are created with content names
// present in the SessionDescription.
TEST_F(WebRtcSessionTest, TestChannelCreationsWithContentNames) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));

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

  SessionDescriptionInterface* modified_offer =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);

  SetLocalDescriptionWithoutError(modified_offer);
  EXPECT_TRUE(session_->GetTransportProxy("audio_content_name") != NULL);
  EXPECT_TRUE(session_->GetTransportProxy("video_content_name") != NULL);
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* answer =
      session_->CreateAnswer(NULL, modified_offer);
  SetRemoteDescriptionWithoutError(answer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  ASSERT_TRUE((video_channel_ = media_engine_->GetVideoChannel(0)) != NULL);
  ASSERT_TRUE((voice_channel_ = media_engine_->GetVoiceChannel(0)) != NULL);
}

// Test that an offer contains the correct media content descriptions based on
// the send streams when no constraints have been set.
TEST_F(WebRtcSessionTest, CreateOfferWithoutConstraintsOrStreams) {
  WebRtcSessionTest::Init();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));
  ASSERT_TRUE(offer != NULL);
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains the correct media content descriptions based on
// the send streams when no constraints have been set.
TEST_F(WebRtcSessionTest, CreateOfferWithoutConstraints) {
  WebRtcSessionTest::Init();
  // Test Audio only offer.
  mediastream_signaling_.UseOptionsAudioOnly();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
        session_->CreateOffer(NULL));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);

  // Test Audio / Video offer.
  mediastream_signaling_.SendAudioVideoStream1();
  offer.reset(session_->CreateOffer(NULL));
  content = cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content != NULL);
}

// Test that an offer contains no media content descriptions if
// kOfferToReceiveVideo and kOfferToReceiveAudio constraints are set to false.
TEST_F(WebRtcSessionTest, CreateOfferWithConstraintsWithoutStreams) {
  WebRtcSessionTest::Init();
  webrtc::FakeConstraints constraints_no_receive;
  constraints_no_receive.SetMandatoryReceiveAudio(false);
  constraints_no_receive.SetMandatoryReceiveVideo(false);

  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(&constraints_no_receive));
  ASSERT_TRUE(offer != NULL);
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content == NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains only audio media content descriptions if
// kOfferToReceiveAudio constraints are set to true.
TEST_F(WebRtcSessionTest, CreateAudioOnlyOfferWithConstraints) {
  WebRtcSessionTest::Init();
  webrtc::FakeConstraints constraints_audio_only;
  constraints_audio_only.SetMandatoryReceiveAudio(true);
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
        session_->CreateOffer(&constraints_audio_only));

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains audio and video media content descriptions if
// kOfferToReceiveAudio and kOfferToReceiveVideo constraints are set to true.
TEST_F(WebRtcSessionTest, CreateOfferWithConstraints) {
  WebRtcSessionTest::Init();
  // Test Audio / Video offer.
  webrtc::FakeConstraints constraints_audio_video;
  constraints_audio_video.SetMandatoryReceiveAudio(true);
  constraints_audio_video.SetMandatoryReceiveVideo(true);
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(&constraints_audio_video));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());

  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content != NULL);

  // TODO(perkj): Should the direction be set to SEND_ONLY if
  // The constraints is set to not receive audio or video but a track is added?
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set.
TEST_F(WebRtcSessionTest, CreateAnswerWithoutConstraintsOrStreams) {
  WebRtcSessionTest::Init();
  // Create a remote offer with audio and video content.
  talk_base::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());

  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(NULL, offer.get()));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set and the offer only contain audio.
TEST_F(WebRtcSessionTest, CreateAudioAnswerWithoutConstraintsOrStreams) {
  WebRtcSessionTest::Init();
  // Create a remote offer with audio only.
  cricket::MediaSessionOptions options;
  options.has_audio = true;
  options.has_video = false;
  talk_base::scoped_ptr<JsepSessionDescription> offer(
      CreateOfferSessionDescription(options));
  ASSERT_TRUE(cricket::GetFirstVideoContent(offer->description()) == NULL);
  ASSERT_TRUE(cricket::GetFirstAudioContent(offer->description()) != NULL);

  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(NULL, offer.get()));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  EXPECT_TRUE(cricket::GetFirstVideoContent(answer->description()) == NULL);
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set.
TEST_F(WebRtcSessionTest, CreateAnswerWithoutConstraints) {
  WebRtcSessionTest::Init();
  // Create a remote offer with audio and video content.
  talk_base::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());

  // Test with a stream with tracks.
  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(NULL, offer.get()));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when
// constraints have been set but no stream is sent.
TEST_F(WebRtcSessionTest, CreateAnswerWithConstraintsWithoutStreams) {
  WebRtcSessionTest::Init();
  // Create a remote offer with audio and video content.
  talk_base::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());

  webrtc::FakeConstraints constraints_no_receive;
  constraints_no_receive.SetMandatoryReceiveAudio(false);
  constraints_no_receive.SetMandatoryReceiveVideo(false);

  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(&constraints_no_receive, offer.get()));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when
// constraints have been set and streams are sent.
TEST_F(WebRtcSessionTest, CreateAnswerWithConstraints) {
  WebRtcSessionTest::Init();
  // Create a remote offer with audio and video content.
  talk_base::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());

  webrtc::FakeConstraints constraints_no_receive;
  constraints_no_receive.SetMandatoryReceiveAudio(false);
  constraints_no_receive.SetMandatoryReceiveVideo(false);

  // Test with a stream with tracks.
  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(&constraints_no_receive, offer.get()));

  // TODO(perkj): Should the direction be set to SEND_ONLY?
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  // TODO(perkj): Should the direction be set to SEND_ONLY?
  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

// This test verifies the call setup when remote answer with audio only and
// later updates with video.
TEST_F(WebRtcSessionTest, TestAVOfferWithAudioOnlyAnswer) {
  WebRtcSessionTest::Init();
  EXPECT_TRUE(media_engine_->GetVideoChannel(0) == NULL);
  EXPECT_TRUE(media_engine_->GetVoiceChannel(0) == NULL);

  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);

  cricket::MediaSessionOptions options;
  options.has_video = false;
  SessionDescriptionInterface* answer = CreateAnswerSessionDescription(offer,
                                                                       options);

  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer;
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(video_channel_ == NULL);

  ASSERT_EQ(0u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack1, voice_channel_->send_streams()[0].name);

  // Update the session descriptions, with Audio and Video.
  mediastream_signaling_.SendAudioVideoStream2();
  SetRemoteAndLocalSessionDescription();

  video_channel_ = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(video_channel_ != NULL);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].name);
  EXPECT_EQ(kVideoTrack2, video_channel_->send_streams()[0].name);

  // Change session back to audio only.
  mediastream_signaling_.SendAudioVideoStream1();
  offer = session_->CreateOffer(NULL);
  mediastream_signaling_.UseOptionsAudioOnly();
  answer = session_->CreateAnswer(NULL, offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_EQ(0u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].name);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack1, voice_channel_->send_streams()[0].name);

  // Updating the session back to Audio and Video.
  mediastream_signaling_.SendAudioVideoStream2();
  SetRemoteAndLocalSessionDescription();

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].name);
  EXPECT_EQ(kVideoTrack2, video_channel_->send_streams()[0].name);
}

// This test verifies the call setup when remote answer with video only and
// later updates with audio.
TEST_F(WebRtcSessionTest, TestAVOfferWithVideoOnlyAnswer) {
  WebRtcSessionTest::Init();
  EXPECT_TRUE(media_engine_->GetVideoChannel(0) == NULL);
  EXPECT_TRUE(media_engine_->GetVoiceChannel(0) == NULL);
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);

  cricket::MediaSessionOptions options;
  options.has_audio = false;
  options.has_video = true;
  SessionDescriptionInterface* answer = CreateAnswerSessionDescription(offer,
                                                                       options);

  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(voice_channel_ == NULL);
  ASSERT_TRUE(video_channel_ != NULL);

  EXPECT_EQ(0u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack1, video_channel_->send_streams()[0].name);

  // Update the session descriptions, with Audio and Video.
  mediastream_signaling_.SendAudioVideoStream2();
  SetRemoteAndLocalSessionDescription();

  voice_channel_ = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(voice_channel_ != NULL);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].name);
  EXPECT_EQ(kAudioTrack2, voice_channel_->send_streams()[0].name);

  // Change session back to video only.
  mediastream_signaling_.SendAudioVideoStream1();
  offer = session_->CreateOffer(NULL);
  mediastream_signaling_.UseOptionsVideoOnly();
  answer = session_->CreateAnswer(NULL, offer);
  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);
  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].name);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack1, video_channel_->send_streams()[0].name);
}

TEST_F(WebRtcSessionTest, TestDefaultSetSecurePolicy) {
  WebRtcSessionTest::Init();
  EXPECT_EQ(cricket::SEC_REQUIRED, session_->secure_policy());
}

TEST_F(WebRtcSessionTest, VerifyCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));
  VerifyCryptoParams(offer->description());
  const webrtc::SessionDescriptionInterface* answer =
      session_->CreateAnswer(NULL, offer.get());
  VerifyCryptoParams(answer->description());
}

TEST_F(WebRtcSessionTest, VerifyNoCryptoParamsInSDP) {
  WebRtcSessionTest::Init();
  session_->set_secure_policy(cricket::SEC_DISABLED);
  mediastream_signaling_.SendAudioVideoStream1();
  scoped_ptr<SessionDescriptionInterface> offer(
        session_->CreateOffer(NULL));
  VerifyNoCryptoParams(offer->description(), false);
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
      session_->CreateOffer(NULL));
  cricket::SessionDescription* offer_copy =
      offer->description()->Copy();
  offer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  modified_offer->Initialize(offer_copy, "1", "1");

  SetLocalDescriptionWithoutError(modified_offer);
  EXPECT_FALSE(allocator_.flags() & cricket::PORTALLOCATOR_ENABLE_BUNDLE);
}

TEST_F(WebRtcSessionTest, TestDisabledBundleInAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  EXPECT_TRUE((cricket::PORTALLOCATOR_ENABLE_BUNDLE & allocator_.flags()) ==
      cricket::PORTALLOCATOR_ENABLE_BUNDLE);
  FakeConstraints constraints;
  constraints.SetMandatoryUseRtpMux(true);
  SessionDescriptionInterface* offer = session_->CreateOffer(&constraints);
  SetLocalDescriptionWithoutError(offer);
  mediastream_signaling_.SendAudioVideoStream2();
  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(NULL, offer));
  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);
  modified_answer->Initialize(answer_copy, "1", "1");
  SetRemoteDescriptionWithoutError(modified_answer);
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
  mediastream_signaling_.SendAudioVideoStream1();
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
  mediastream_signaling_.SendAudioVideoStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32 send_ssrc  = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));

  cricket::AudioOptions options;
  options.echo_cancellation.Set(true);

  session_->SetAudioSend(kAudioTrack1, false, options);
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  EXPECT_FALSE(channel->options().echo_cancellation.IsSet());

  session_->SetAudioSend(kAudioTrack1, true, options);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  bool value;
  EXPECT_TRUE(channel->options().echo_cancellation.Get(&value));
  EXPECT_TRUE(value);
}

TEST_F(WebRtcSessionTest, SetVideoPlayout) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
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
  mediastream_signaling_.SendAudioVideoStream1();
  SetRemoteAndLocalSessionDescription();
  cricket::FakeVideoMediaChannel* channel = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32 send_ssrc  = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  cricket::VideoOptions* options = NULL;
  session_->SetVideoSend(kVideoTrack1, false, options);
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  session_->SetVideoSend(kVideoTrack1, true, options);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
}

TEST_F(WebRtcSessionTest, CanNotInsertDtmf) {
  TestCanInsertDtmf(false);
}

TEST_F(WebRtcSessionTest, CanInsertDtmf) {
  TestCanInsertDtmf(true);
}

TEST_F(WebRtcSessionTest, InsertDtmf) {
  // Setup
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SetRemoteAndLocalSessionDescription();
  FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  EXPECT_EQ(0U, channel->dtmf_info_queue().size());

  // Insert DTMF
  const int expected_flags = DF_SEND;
  const int expected_duration = 90;
  session_->InsertDtmf(kAudioTrack1, 0, expected_duration);
  session_->InsertDtmf(kAudioTrack1, 1, expected_duration);
  session_->InsertDtmf(kAudioTrack1, 2, expected_duration);

  // Verify
  ASSERT_EQ(3U, channel->dtmf_info_queue().size());
  const uint32 send_ssrc  = channel->send_streams()[0].first_ssrc();
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[0], send_ssrc, 0,
                              expected_duration, expected_flags));
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[1], send_ssrc, 1,
                              expected_duration, expected_flags));
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[2], send_ssrc, 2,
                              expected_duration, expected_flags));
}

// This test verifies the |initiator| flag when session initiates the call.
TEST_F(WebRtcSessionTest, TestInitiatorFlagAsOriginator) {
  WebRtcSessionTest::Init();
  EXPECT_FALSE(session_->initiator());
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                                   offer);
  SetLocalDescriptionWithoutError(offer);
  EXPECT_TRUE(session_->initiator());
  SetRemoteDescriptionWithoutError(answer);
  EXPECT_TRUE(session_->initiator());
}

// This test verifies the |initiator| flag when session receives the call.
TEST_F(WebRtcSessionTest, TestInitiatorFlagAsReceiver) {
  WebRtcSessionTest::Init();
  EXPECT_FALSE(session_->initiator());
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetRemoteDescriptionWithoutError(offer);
  EXPECT_FALSE(session_->initiator());
  SetLocalDescriptionWithoutError(answer);
  EXPECT_FALSE(session_->initiator());
}

// This test verifies the ice protocol type at initiator of the call
// if |a=ice-options:google-ice| is present in answer.
TEST_F(WebRtcSessionTest, TestInitiatorGIceInAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  std::string sdp;
  EXPECT_TRUE(answer->ToString(&sdp));
  // Adding ice-options to the session level.
  InjectAfter("t=0 0\r\n",
              "a=ice-options:google-ice\r\n",
              &sdp);
  SessionDescriptionInterface* answer_with_gice =
      CreateSessionDescription(JsepSessionDescription::kAnswer, sdp, NULL);
  SetRemoteDescriptionWithoutError(answer_with_gice);
  VerifyTransportType("audio", cricket::ICEPROTO_GOOGLE);
  VerifyTransportType("video", cricket::ICEPROTO_GOOGLE);
}

// This test verifies the ice protocol type at initiator of the call
// if ICE RFC5245 is supported in answer.
TEST_F(WebRtcSessionTest, TestInitiatorIceInAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  SetRemoteDescriptionWithoutError(answer);
  VerifyTransportType("audio", cricket::ICEPROTO_RFC5245);
  VerifyTransportType("video", cricket::ICEPROTO_RFC5245);
}

// This test verifies the ice protocol type at receiver side of the call if
// receiver decides to use google-ice.
TEST_F(WebRtcSessionTest, TestReceiverGIceInOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = session_->CreateAnswer(NULL,
                                                               offer);
  std::string sdp;
  EXPECT_TRUE(answer->ToString(&sdp));
  // Adding ice-options to the session level.
  InjectAfter("t=0 0\r\n",
              "a=ice-options:google-ice\r\n",
              &sdp);
  SessionDescriptionInterface* answer_with_gice =
      CreateSessionDescription(JsepSessionDescription::kAnswer, sdp, NULL);
  SetLocalDescriptionWithoutError(answer_with_gice);
  VerifyTransportType("audio", cricket::ICEPROTO_GOOGLE);
  VerifyTransportType("video", cricket::ICEPROTO_GOOGLE);
}

// This test verifies the ice protocol type at receiver side of the call if
// receiver decides to use ice RFC 5245.
TEST_F(WebRtcSessionTest, TestReceiverIceInOffer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer =
      session_->CreateAnswer(NULL, offer);
  SetLocalDescriptionWithoutError(answer);
  VerifyTransportType("audio", cricket::ICEPROTO_RFC5245);
  VerifyTransportType("video", cricket::ICEPROTO_RFC5245);
}

// This test verifies the session state when ICE RFC5245 in offer and
// ICE google-ice in answer.
TEST_F(WebRtcSessionTest, TestIceOfferGIceOnlyAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));
  std::string offer_str;
  offer->ToString(&offer_str);
  // Disable google-ice
  const std::string gice_option = "google-ice";
  const std::string xgoogle_xice = "xgoogle-xice";
  talk_base::replace_substrs(gice_option.c_str(), gice_option.length(),
                             xgoogle_xice.c_str(), xgoogle_xice.length(),
                             &offer_str);
  JsepSessionDescription *ice_only_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  EXPECT_TRUE((ice_only_offer)->Initialize(offer_str, NULL));
  SetLocalDescriptionWithoutError(ice_only_offer);
  std::string original_offer_sdp;
  EXPECT_TRUE(offer->ToString(&original_offer_sdp));
  SessionDescriptionInterface* answer_with_gice =
      CreateSessionDescription(JsepSessionDescription::kAnswer,
                               original_offer_sdp, NULL);
  SetRemoteDescriptionExpectError(kUpdateStateFailed, answer_with_gice);
}

// Verifing local offer and remote answer have matching m-lines as per RFC 3264.
TEST_F(WebRtcSessionTest, TestIncorrectMLinesInRemoteAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetLocalDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer =
     session_->CreateAnswer(NULL, offer);

  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveContentByName("video");
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);

  EXPECT_TRUE(modified_answer->Initialize(answer_copy,
                                          answer->session_id(),
                                          answer->session_version()));
  SetRemoteDescriptionExpectError(kMlineMismatch, modified_answer);

  // Modifying content names.
  std::string sdp;
  EXPECT_TRUE(answer->ToString(&sdp));
  const std::string kAudioMid = "a=mid:audio";
  const std::string kAudioMidReplaceStr = "a=mid:audio_content_name";

  // Replacing |audio| with |audio_content_name|.
  talk_base::replace_substrs(kAudioMid.c_str(), kAudioMid.length(),
                             kAudioMidReplaceStr.c_str(),
                             kAudioMidReplaceStr.length(),
                             &sdp);

  SessionDescriptionInterface* modified_answer1 =
      CreateSessionDescription(JsepSessionDescription::kAnswer, sdp, NULL);
  SetRemoteDescriptionExpectError(kMlineMismatch, modified_answer1);

  SetRemoteDescriptionWithoutError(answer);
}

// Verifying remote offer and local answer have matching m-lines as per
// RFC 3264.
TEST_F(WebRtcSessionTest, TestIncorrectMLinesInLocalAnswer) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  SetRemoteDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer =
     session_->CreateAnswer(NULL, offer);

  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveContentByName("video");
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);

  EXPECT_TRUE(modified_answer->Initialize(answer_copy,
                                          answer->session_id(),
                                          answer->session_version()));
  SetLocalDescriptionExpectError(kMlineMismatch, modified_answer);
  SetLocalDescriptionWithoutError(answer);
}

// This test verifies that WebRtcSession does not start candidate allocation
// before SetLocalDescription is called.
TEST_F(WebRtcSessionTest, TestIceStartAfterSetLocalDescriptionOnly) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  SessionDescriptionInterface* offer = session_->CreateOffer(NULL);
  cricket::Candidate candidate;
  candidate.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName0, kMediaContentIndex0,
                                 candidate);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName1, kMediaContentIndex1,
                                  candidate1);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate1));
  SetRemoteDescriptionWithoutError(offer);
  ASSERT_TRUE(session_->GetTransportProxy("audio") != NULL);
  ASSERT_TRUE(session_->GetTransportProxy("video") != NULL);

  // Pump for 1 second and verify that no candidates are generated.
  talk_base::Thread::Current()->ProcessMessages(1000);
  EXPECT_TRUE(observer_.mline_0_candidates_.empty());
  EXPECT_TRUE(observer_.mline_1_candidates_.empty());

  SessionDescriptionInterface* answer =
      session_->CreateAnswer(NULL, offer);
  SetLocalDescriptionWithoutError(answer);
  EXPECT_TRUE(session_->GetTransportProxy("audio")->negotiated());
  EXPECT_TRUE(session_->GetTransportProxy("video")->negotiated());
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
}

// This test verifies that crypto parameter is updated in local session
// description as per security policy set in MediaSessionDescriptionFactory.
TEST_F(WebRtcSessionTest, TestCryptoAfterSetLocalDescription) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));

  // Making sure SetLocalDescription correctly sets crypto value in
  // SessionDescription object after de-serialization of sdp string. The value
  // will be set as per MediaSessionDescriptionFactory.
  std::string offer_str;
  offer->ToString(&offer_str);
  SessionDescriptionInterface* jsep_offer_str =
      CreateSessionDescription(JsepSessionDescription::kOffer, offer_str, NULL);
  SetLocalDescriptionWithoutError(jsep_offer_str);
  EXPECT_TRUE(session_->voice_channel()->secure_required());
  EXPECT_TRUE(session_->video_channel()->secure_required());
}

// This test verifies the crypto parameter when security is disabled.
TEST_F(WebRtcSessionTest, TestCryptoAfterSetLocalDescriptionWithDisabled) {
  WebRtcSessionTest::Init();
  mediastream_signaling_.SendAudioVideoStream1();
  session_->set_secure_policy(cricket::SEC_DISABLED);
  talk_base::scoped_ptr<SessionDescriptionInterface> offer(
      session_->CreateOffer(NULL));

  // Making sure SetLocalDescription correctly sets crypto value in
  // SessionDescription object after de-serialization of sdp string. The value
  // will be set as per MediaSessionDescriptionFactory.
  std::string offer_str;
  offer->ToString(&offer_str);
  SessionDescriptionInterface *jsep_offer_str =
      CreateSessionDescription(JsepSessionDescription::kOffer, offer_str, NULL);
  SetLocalDescriptionWithoutError(jsep_offer_str);
  EXPECT_FALSE(session_->voice_channel()->secure_required());
  EXPECT_FALSE(session_->video_channel()->secure_required());
}

// This test verifies that an answer contains new ufrag and password if an offer
// with new ufrag and password is received.
TEST_F(WebRtcSessionTest, TestCreateAnswerWithNewUfragAndPassword) {
  WebRtcSessionTest::Init();
  cricket::MediaSessionOptions options;
  options.has_audio = true;
  options.has_video = true;
  talk_base::scoped_ptr<JsepSessionDescription> offer(
      CreateOfferSessionDescription(options));
  SetRemoteDescriptionWithoutError(offer.release());

  mediastream_signaling_.SendAudioVideoStream1();
  talk_base::scoped_ptr<SessionDescriptionInterface> answer(
      session_->CreateAnswer(NULL, session_->remote_description()));
  SetLocalDescriptionWithoutError(answer.release());

  // Receive an offer with new ufrag and password.
  options.transport_options.ice_restart = true;
  talk_base::scoped_ptr<JsepSessionDescription> updated_offer1(
      CreateUpdatedOfferSessionDescription(options,
                                           session_->remote_description()));
  SetRemoteDescriptionWithoutError(updated_offer1.release());

  talk_base::scoped_ptr<SessionDescriptionInterface> updated_answer1(
      session_->CreateAnswer(NULL, session_->remote_description()));

  CompareIceUfragAndPassword(updated_answer1->description(),
                             session_->local_description()->description(),
                             false);

  SetLocalDescriptionWithoutError(updated_answer1.release());

  // Receive yet an offer without changed ufrag or password.
  options.transport_options.ice_restart = false;
  talk_base::scoped_ptr<JsepSessionDescription> updated_offer2(
      CreateUpdatedOfferSessionDescription(options,
                                           session_->remote_description()));
  SetRemoteDescriptionWithoutError(updated_offer2.release());

  talk_base::scoped_ptr<SessionDescriptionInterface> updated_answer2(
      session_->CreateAnswer(NULL, session_->remote_description()));

  CompareIceUfragAndPassword(updated_answer2->description(),
                             session_->local_description()->description(),
                             true);

  SetLocalDescriptionWithoutError(updated_answer2.release());
}

// Runs the loopback call test with BUNDLE and STUN disabled.
TEST_F(WebRtcSessionTest, TestIceStatesBasic) {
  // Lets try with only UDP ports.
  allocator_.set_flags(cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG |
                       cricket::PORTALLOCATOR_DISABLE_TCP |
                       cricket::PORTALLOCATOR_DISABLE_STUN |
                       cricket::PORTALLOCATOR_DISABLE_RELAY);
  TestLoopbackCall();
}

// TODO(bemasc): Add a TestIceStatesBundle with BUNDLE enabled.  That test
// currently fails because upon disconnection and reconnection OnIceComplete is
// called more than once without returning to IceGatheringGathering.
