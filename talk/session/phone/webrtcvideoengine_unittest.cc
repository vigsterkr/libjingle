/*
 * libjingle
 * Copyright 2004--2011, Google Inc.
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

#include "talk/base/gunit.h"
#include "talk/session/phone/fakewebrtcvideoengine.h"
#include "talk/session/phone/fakewebrtcvoiceengine.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/videoengine_unittest.h"
#include "talk/session/phone/webrtcvideoengine.h"
#include "talk/session/phone/webrtcvoiceengine.h"

// Tests for the WebRtcVideoEngine/VideoChannel code.

static const cricket::VideoCodec kVP8Codec(100, "VP8", 640, 400, 30, 0);
static const cricket::VideoCodec kRedCodec(101, "red", 0, 0, 0, 0);
static const cricket::VideoCodec kUlpFecCodec(102, "ulpfec", 0, 0, 0, 0);
static const cricket::VideoCodec* const kVideoCodecs[] = {
    &kVP8Codec,
    &kRedCodec,
    &kUlpFecCodec
};

static const unsigned int kMinBandwidthKbps = 300;
static const unsigned int kMaxBandwidthKbps = 2000;

class FakeViEWrapper : public cricket::ViEWrapper {
 public:
  explicit FakeViEWrapper(cricket::FakeWebRtcVideoEngine* engine)
      : cricket::ViEWrapper(engine, engine, engine, engine,
                            engine, engine, engine) {
  }
};

// Test fixture to test WebRtcVideoEngine with a fake webrtc::VideoEngine.
// Useful for testing failure paths.
class WebRtcVideoEngineTestFake : public testing::Test {
 public:
  WebRtcVideoEngineTestFake()
      : vie_(kVideoCodecs, ARRAY_SIZE(kVideoCodecs)),
        engine_(NULL,  // cricket::WebRtcVoiceEngine
                new FakeViEWrapper(&vie_)),
        channel_(NULL),
        voice_channel_(NULL) {
  }
  bool SetupEngine() {
    bool result = engine_.Init();
    if (result) {
      channel_ = engine_.CreateChannel(voice_channel_);
      result = (channel_ != NULL);
    }
    return result;
  }
  virtual void TearDown() {
    delete channel_;
    engine_.Terminate();
  }

 protected:
  cricket::FakeWebRtcVideoEngine vie_;
  cricket::WebRtcVideoEngine engine_;
  cricket::WebRtcVideoMediaChannel* channel_;
  cricket::WebRtcVoiceMediaChannel* voice_channel_;
};

// Test fixtures to test WebRtcVideoEngine with a real webrtc::VideoEngine.
class WebRtcVideoEngineTest
    : public VideoEngineTest<cricket::WebRtcVideoEngine> {
 protected:
  typedef VideoEngineTest<cricket::WebRtcVideoEngine> Base;
};
class WebRtcVideoMediaChannelTest
    : public VideoMediaChannelTest<
        cricket::WebRtcVideoEngine, cricket::WebRtcVideoMediaChannel> {
 protected:
  typedef VideoMediaChannelTest<cricket::WebRtcVideoEngine,
       cricket::WebRtcVideoMediaChannel> Base;
  virtual cricket::VideoCodec DefaultCodec() { return kVP8Codec; }
  virtual void SetUp() {
    Base::SetUp();
    // Need to start the capturer to allow us to pump in frames.
    engine_.SetCapture(true);
  }
  virtual void TearDown() {
    engine_.SetCapture(false);
    Base::TearDown();
  }
};

/////////////////////////
// Tests with fake ViE //
/////////////////////////

// Tests that our stub library "works".
TEST_F(WebRtcVideoEngineTestFake, StartupShutdown) {
  EXPECT_FALSE(vie_.IsInited());
  EXPECT_TRUE(engine_.Init());
  EXPECT_TRUE(vie_.IsInited());
  engine_.Terminate();
}

// Tests that we can create and destroy a channel.
TEST_F(WebRtcVideoEngineTestFake, CreateChannel) {
  EXPECT_TRUE(engine_.Init());
  channel_ = engine_.CreateChannel(voice_channel_);
  EXPECT_TRUE(channel_ != NULL);
}

// Tests that we properly handle failures in CreateChannel.
TEST_F(WebRtcVideoEngineTestFake, CreateChannelFail) {
  vie_.set_fail_create_channel(true);
  EXPECT_TRUE(engine_.Init());
  channel_ = engine_.CreateChannel(voice_channel_);
  EXPECT_TRUE(channel_ == NULL);
}

// Test that we apply plain old VP8 codecs properly.
TEST_F(WebRtcVideoEngineTestFake, SetSendCodecs) {
  EXPECT_TRUE(SetupEngine());
  int channel_num = vie_.GetLastChannel();
  std::vector<cricket::VideoCodec> codecs(engine_.codecs());
  codecs.resize(1);  // toss out red and ulpfec
  EXPECT_TRUE(channel_->SetSendCodecs(codecs));
  webrtc::VideoCodec gcodec;
  EXPECT_EQ(0, vie_.GetSendCodec(channel_num, gcodec));
  EXPECT_EQ(kVP8Codec.id, gcodec.plType);
  EXPECT_EQ(kVP8Codec.width, gcodec.width);
  EXPECT_EQ(kVP8Codec.height, gcodec.height);
  EXPECT_STREQ(kVP8Codec.name.c_str(), gcodec.plName);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.minBitrate);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.startBitrate);
  EXPECT_EQ(kMaxBandwidthKbps, gcodec.maxBitrate);
  // TODO: Check HybridNackFecStatus.
  // TODO: Check RTCP, PLI, TMMBR.
}

// TODO: Add test for FEC.

// Test that we set bandwidth properly when using full auto bandwidth mode.
TEST_F(WebRtcVideoEngineTestFake, SetBandwidthAuto) {
  EXPECT_TRUE(SetupEngine());
  int channel_num = vie_.GetLastChannel();
  EXPECT_TRUE(channel_->SetSendCodecs(engine_.codecs()));
  EXPECT_TRUE(channel_->SetSendBandwidth(true, cricket::kAutoBandwidth));
  webrtc::VideoCodec gcodec;
  EXPECT_EQ(0, vie_.GetSendCodec(channel_num, gcodec));
  EXPECT_EQ(kVP8Codec.id, gcodec.plType);
  EXPECT_STREQ(kVP8Codec.name.c_str(), gcodec.plName);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.minBitrate);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.startBitrate);
  EXPECT_EQ(kMaxBandwidthKbps, gcodec.maxBitrate);
}

// Test that we set bandwidth properly when using auto with upper bound.
TEST_F(WebRtcVideoEngineTestFake, SetBandwidthAutoCapped) {
  EXPECT_TRUE(SetupEngine());
  int channel_num = vie_.GetLastChannel();
  EXPECT_TRUE(channel_->SetSendCodecs(engine_.codecs()));
  EXPECT_TRUE(channel_->SetSendBandwidth(true, 768000));
  webrtc::VideoCodec gcodec;
  EXPECT_EQ(0, vie_.GetSendCodec(channel_num, gcodec));
  EXPECT_EQ(kVP8Codec.id, gcodec.plType);
  EXPECT_STREQ(kVP8Codec.name.c_str(), gcodec.plName);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.minBitrate);
  EXPECT_EQ(kMinBandwidthKbps, gcodec.startBitrate);
  EXPECT_EQ(768U, gcodec.maxBitrate);
}

// Test that we set bandwidth properly when using a fixed bandwidth.
TEST_F(WebRtcVideoEngineTestFake, SetBandwidthFixed) {
  EXPECT_TRUE(SetupEngine());
  int channel_num = vie_.GetLastChannel();
  EXPECT_TRUE(channel_->SetSendCodecs(engine_.codecs()));
  EXPECT_TRUE(channel_->SetSendBandwidth(false, 768000));
  webrtc::VideoCodec gcodec;
  EXPECT_EQ(0, vie_.GetSendCodec(channel_num, gcodec));
  EXPECT_EQ(kVP8Codec.id, gcodec.plType);
  EXPECT_STREQ(kVP8Codec.name.c_str(), gcodec.plName);
  EXPECT_EQ(768U, gcodec.minBitrate);
  EXPECT_EQ(768U, gcodec.startBitrate);
  EXPECT_EQ(768U, gcodec.maxBitrate);
}

/////////////////////////
// Tests with real ViE //
/////////////////////////

// Tests that we can find codecs by name or id.
TEST_F(WebRtcVideoEngineTest, FindCodec) {
  // We should not need to init engine in order to get codecs.
  const std::vector<cricket::VideoCodec>& c = engine_.codecs();
  EXPECT_EQ(1U, c.size());

  cricket::VideoCodec vp8(104, "VP8", 320, 200, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(vp8));

  cricket::VideoCodec vp8_ci(104, "vp8", 320, 200, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(vp8));

  cricket::VideoCodec vp8_diff_fr_diff_pref(104, "VP8", 320, 200, 50, 50);
  EXPECT_TRUE(engine_.FindCodec(vp8_diff_fr_diff_pref));

  cricket::VideoCodec vp8_diff_id(95, "VP8", 320, 200, 30, 0);
  EXPECT_FALSE(engine_.FindCodec(vp8_diff_id));
  vp8_diff_id.id = 97;
  EXPECT_TRUE(engine_.FindCodec(vp8_diff_id));

  cricket::VideoCodec vp8_diff_res(104, "VP8", 320, 111, 30, 0);
  EXPECT_FALSE(engine_.FindCodec(vp8_diff_res));

  // PeerConnection doesn't negotiate the resolution at this point.
  // Test that FindCodec can handle the case when width/height is 0.
  cricket::VideoCodec vp8_zero_res(104, "VP8", 0, 0, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(vp8_zero_res));

  // TODO: Re-enable when we re-enable FEC.
#if 0
  cricket::VideoCodec red(101, "RED", 0, 0, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(red));

  cricket::VideoCodec red_ci(101, "red", 0, 0, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(red));

  cricket::VideoCodec fec(102, "ULPFEC", 0, 0, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(fec));

  cricket::VideoCodec fec_ci(102, "ulpfec", 0, 0, 30, 0);
  EXPECT_TRUE(engine_.FindCodec(fec));
#endif
}

TEST_F(WebRtcVideoEngineTest, StartupShutdown) {
  EXPECT_TRUE(engine_.Init());
  engine_.Terminate();
}

// TODO: Figure out why ViE is munging the COM refcount.
#ifdef WIN32
TEST_F(WebRtcVideoEngineTest, DISABLED_CheckCoInitialize) {
  Base::CheckCoInitialize();
}
#endif

TEST_F(WebRtcVideoEngineTest, CreateChannel) {
  EXPECT_TRUE(engine_.Init());
  cricket::VideoMediaChannel* channel = engine_.CreateChannel(NULL);
  EXPECT_TRUE(channel != NULL);
  delete channel;
}

TEST_F(WebRtcVideoMediaChannelTest, SetRecvCodecs) {
  std::vector<cricket::VideoCodec> codecs;
  codecs.push_back(kVP8Codec);
  EXPECT_TRUE(channel_->SetRecvCodecs(codecs));
}
TEST_F(WebRtcVideoMediaChannelTest, SetRecvCodecsWrongPayloadType) {
  std::vector<cricket::VideoCodec> codecs;
  codecs.push_back(kVP8Codec);
  codecs[0].id = 99;
  EXPECT_TRUE(channel_->SetRecvCodecs(codecs));
}
TEST_F(WebRtcVideoMediaChannelTest, SetRecvCodecsUnsupportedCodec) {
  std::vector<cricket::VideoCodec> codecs;
  codecs.push_back(kVP8Codec);
  codecs.push_back(cricket::VideoCodec(101, "VP1", 640, 400, 30, 0));
  EXPECT_FALSE(channel_->SetRecvCodecs(codecs));
}

TEST_F(WebRtcVideoMediaChannelTest, SetSend) {
  Base::SetSend();
}
TEST_F(WebRtcVideoMediaChannelTest, SetSendWithoutCodecs) {
  Base::SetSendWithoutCodecs();
}
TEST_F(WebRtcVideoMediaChannelTest, SetSendSetsTransportBufferSizes) {
  Base::SetSendSetsTransportBufferSizes();
}

TEST_F(WebRtcVideoMediaChannelTest, SendAndReceiveVp8Vga) {
  SendAndReceive(cricket::VideoCodec(100, "VP8", 640, 400, 30, 0));
}
TEST_F(WebRtcVideoMediaChannelTest, SendAndReceiveVp8Qvga) {
  SendAndReceive(cricket::VideoCodec(100, "VP8", 320, 200, 30, 0));
}
TEST_F(WebRtcVideoMediaChannelTest, SendAndReceiveH264SvcQqvga) {
  SendAndReceive(cricket::VideoCodec(100, "VP8", 160, 100, 30, 0));
}
// TODO: Figure out why this test doesn't work.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_SendManyResizeOnce) {
  SendManyResizeOnce();
}

// TODO: Fix this test to tolerate missing stats.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_GetStats) {
  Base::GetStats();
}
// TODO: Restore this test once we support multiple recv streams.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_GetStatsMultipleRecvStreams) {
  Base::GetStatsMultipleRecvStreams();
}
// TODO: Restore this test once we support multiple send streams.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_GetStatsMultipleSendStreams) {
  Base::GetStatsMultipleSendStreams();
}

TEST_F(WebRtcVideoMediaChannelTest, SetSendBandwidth) {
  Base::SetSendBandwidth();
}
TEST_F(WebRtcVideoMediaChannelTest, SetSendSsrc) {
  Base::SetSendSsrc();
}
TEST_F(WebRtcVideoMediaChannelTest, SetSendSsrcAfterSetCodecs) {
  Base::SetSendSsrcAfterSetCodecs();
}

// TODO: Restore this test once we support GetRenderer.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_SetRenderer) {
  Base::SetRenderer();
}
// TODO: Restore this test once we support multiple recv streams.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_AddRemoveRecvStreams) {
  Base::AddRemoveRecvStreams();
}
// TODO: Restore this test once we support multiple recv streams.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_SimulateConference) {
  Base::SimulateConference();
}

TEST_F(WebRtcVideoMediaChannelTest, AdaptResolution16x10) {
  Base::AdaptResolution16x10();
}
TEST_F(WebRtcVideoMediaChannelTest, AdaptResolution4x3) {
  Base::AdaptResolution4x3();
}
// TODO: Restore this test once we support sending 0 fps.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_AdaptDropAllFrames) {
  Base::AdaptDropAllFrames();
}
// TODO: Understand why we get decode errors on this test.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_AdaptFramerate) {
  Base::AdaptFramerate();
}
// TODO: Understand why we receive a not-quite-black frame.
TEST_F(WebRtcVideoMediaChannelTest, DISABLED_Mute) {
  Base::Mute();
}

