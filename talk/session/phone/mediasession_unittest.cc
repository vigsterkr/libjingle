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

#include <string>
#include <vector>

#include "talk/base/gunit.h"
#include "talk/p2p/base/constants.h"
#include "talk/session/phone/codec.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/srtpfilter.h"
#include "talk/session/phone/testutils.h"

#ifdef HAVE_SRTP
#define ASSERT_CRYPTO(cd, r, s, cs) \
    ASSERT_EQ(r, cd->crypto_required()); \
    ASSERT_EQ(s, cd->cryptos().size()); \
    ASSERT_EQ(std::string(cs), cd->cryptos()[0].cipher_suite)
#else
#define ASSERT_CRYPTO(c, r, s, cs) \
  ASSERT_EQ(false, cd->crypto_required()); \
  ASSERT_EQ(0U, cd->cryptos().size());
#endif

using cricket::MediaSessionDescriptionFactory;
using cricket::MediaSessionOptions;
using cricket::SessionDescription;
using cricket::ContentInfo;
using cricket::AudioContentDescription;
using cricket::VideoContentDescription;
using cricket::kAutoBandwidth;
using cricket::AudioCodec;
using cricket::VideoCodec;
using cricket::NS_JINGLE_RTP;
using cricket::MEDIA_TYPE_AUDIO;
using cricket::MEDIA_TYPE_VIDEO;
using cricket::SEC_ENABLED;
using cricket::CS_AES_CM_128_HMAC_SHA1_32;
using cricket::CS_AES_CM_128_HMAC_SHA1_80;

static const AudioCodec kAudioCodecs1[] = {
  AudioCodec(103, "ISAC",   16000, -1,    1, 5),
  AudioCodec(102, "iLBC",   8000,  13300, 1, 4),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 3),
  AudioCodec(8,   "PCMA",   8000,  64000, 1, 2),
  AudioCodec(117, "red",    8000,  0,     1, 1),
};

static const AudioCodec kAudioCodecs2[] = {
  AudioCodec(126, "speex",  16000, 22000, 1, 3),
  AudioCodec(127, "iLBC",   8000,  13300, 1, 2),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 1),
};

static const AudioCodec kAudioCodecsAnswer[] = {
  AudioCodec(102, "iLBC",   8000,  13300, 1, 2),
  AudioCodec(0,   "PCMU",   8000,  64000, 1, 1),
};

static const VideoCodec kVideoCodecs1[] = {
  VideoCodec(96, "H264-SVC", 320, 200, 30, 2),
  VideoCodec(97, "H264", 320, 200, 30, 1)
};

static const VideoCodec kVideoCodecs2[] = {
  VideoCodec(126, "H264", 320, 200, 30, 2),
  VideoCodec(127, "H263", 320, 200, 30, 1)
};

static const VideoCodec kVideoCodecsAnswer[] = {
  VideoCodec(97, "H264", 320, 200, 30, 2)
};

class MediaSessionDescriptionFactoryTest : public testing::Test {
 public:
  MediaSessionDescriptionFactoryTest() {
    f1_.set_audio_codecs(MAKE_VECTOR(kAudioCodecs1));
    f1_.set_video_codecs(MAKE_VECTOR(kVideoCodecs1));
    f2_.set_audio_codecs(MAKE_VECTOR(kAudioCodecs2));
    f2_.set_video_codecs(MAKE_VECTOR(kVideoCodecs2));
  }
 protected:
  MediaSessionDescriptionFactory f1_;
  MediaSessionDescriptionFactory f2_;
};

// Create a typical audio offer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioOffer) {
  f1_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(
      f1_.CreateOffer(MediaSessionOptions()));
  ASSERT_TRUE(offer.get() != NULL);
  const ContentInfo* ac = offer->GetContentByName("audio");
  const ContentInfo* vc = offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(f1_.audio_codecs(), acd->codecs());
  EXPECT_NE(0U, acd->ssrc());                   // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);
}

// Create a typical video offer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateVideoOffer) {
  MediaSessionOptions opts;
  opts.has_video = true;
  f1_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription>
      offer(f1_.CreateOffer(opts));
  ASSERT_TRUE(offer.get() != NULL);
  const ContentInfo* ac = offer->GetContentByName("audio");
  const ContentInfo* vc = offer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), vc->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(f1_.audio_codecs(), acd->codecs());
  EXPECT_NE(0U, acd->ssrc());                   // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(acd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(acd, false, 2U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(f1_.video_codecs(), vcd->codecs());
  EXPECT_NE(0U, vcd->ssrc());                   // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, vcd->bandwidth());  // default bandwidth (auto)
  EXPECT_TRUE(vcd->rtcp_mux());                 // rtcp-mux defaults on
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
}

// Create a typical audio answer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioAnswer) {
  f1_.set_secure(SEC_ENABLED);
  f2_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(
      f1_.CreateOffer(MediaSessionOptions()));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), MediaSessionOptions()));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(MAKE_VECTOR(kAudioCodecsAnswer), acd->codecs());
  EXPECT_NE(0U, acd->ssrc());                   // a random nonzero ssrc
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // negotiated auto bw
  EXPECT_TRUE(acd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
}

// Create a typical video answer, and ensure it matches what we expect.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateVideoAnswer) {
  MediaSessionOptions opts;
  opts.has_video = true;
  f1_.set_secure(SEC_ENABLED);
  f2_.set_secure(SEC_ENABLED);
  talk_base::scoped_ptr<SessionDescription> offer(f1_.CreateOffer(opts));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), opts));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc != NULL);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), ac->type);
  EXPECT_EQ(std::string(NS_JINGLE_RTP), vc->type);
  const AudioContentDescription* acd =
      static_cast<const AudioContentDescription*>(ac->description);
  const VideoContentDescription* vcd =
      static_cast<const VideoContentDescription*>(vc->description);
  EXPECT_EQ(MEDIA_TYPE_AUDIO, acd->type());
  EXPECT_EQ(MAKE_VECTOR(kAudioCodecsAnswer), acd->codecs());
  EXPECT_EQ(kAutoBandwidth, acd->bandwidth());  // negotiated auto bw
  EXPECT_NE(0U, acd->ssrc());                   // a random nonzero ssrc
  EXPECT_TRUE(acd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(acd, false, 1U, CS_AES_CM_128_HMAC_SHA1_32);
  EXPECT_EQ(MEDIA_TYPE_VIDEO, vcd->type());
  EXPECT_EQ(MAKE_VECTOR(kVideoCodecsAnswer), vcd->codecs());
  EXPECT_NE(0U, vcd->ssrc());                    // a random nonzero ssrc
  EXPECT_TRUE(vcd->rtcp_mux());                 // negotiated rtcp-mux
  ASSERT_CRYPTO(vcd, false, 1U, CS_AES_CM_128_HMAC_SHA1_80);
}

// Create an audio-only answer to a video offer.
TEST_F(MediaSessionDescriptionFactoryTest, TestCreateAudioAnswerToVideo) {
  MediaSessionOptions opts;
  opts.has_video = true;
  talk_base::scoped_ptr<SessionDescription>
      offer(f1_.CreateOffer(opts));
  ASSERT_TRUE(offer.get() != NULL);
  talk_base::scoped_ptr<SessionDescription> answer(
      f2_.CreateAnswer(offer.get(), MediaSessionOptions()));
  const ContentInfo* ac = answer->GetContentByName("audio");
  const ContentInfo* vc = answer->GetContentByName("video");
  ASSERT_TRUE(ac != NULL);
  ASSERT_TRUE(vc == NULL);
}
