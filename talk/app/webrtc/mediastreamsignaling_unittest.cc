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

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessiondescription.h"

static const char kStreams[][8] = {"stream1", "stream2"};
static const char kAudioTracks[][32] = {"audiotrack0", "audiotrack1"};
static const char kVideoTracks[][32] = {"videotrack0", "videotrack1"};

using webrtc::AudioTrack;
using webrtc::AudioTrackVector;
using webrtc::VideoTrack;
using webrtc::VideoTrackVector;
using webrtc::DataChannelInterface;
using webrtc::FakeConstraints;
using webrtc::IceCandidateInterface;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaStreamInterface;
using webrtc::SdpParseError;
using webrtc::SessionDescriptionInterface;
using webrtc::StreamCollection;
using webrtc::StreamCollectionInterface;

// Reference SDP with a MediaStream with label "stream1" and audio track with
// id "audio_1" and a video track with id "video_1;
static const char kSdpStringWithStream1[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 mslabel:stream1\r\n"
    "a=ssrc:1 label:audiotrack0\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 mslabel:stream1\r\n"
    "a=ssrc:2 label:videotrack0\r\n";

// Reference SDP with two MediaStreams with label "stream1" and "stream2. Each
// MediaStreams have one audio track and one video track.
// This uses MSID.
static const char kSdpStringWith2Stream[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS stream1 stream2\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 msid:stream1 audiotrack0\r\n"
    "a=ssrc:3 cname:stream2\r\n"
    "a=ssrc:3 msid:stream2 audiotrack1\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/0\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 msid:stream1 videotrack0\r\n"
    "a=ssrc:4 cname:stream2\r\n"
    "a=ssrc:4 msid:stream2 videotrack1\r\n";

// Reference SDP without MediaStreams. Msid is not supported.
static const char kSdpStringWithoutStreams[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

// Reference SDP without MediaStreams. Msid is supported.
static const char kSdpStringWithMsidWithoutStreams[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a:msid-semantic: WMS\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

// Reference SDP without MediaStreams and audio only.
static const char kSdpStringWithoutStreamsAudioOnly[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n";

static const char kSdpStringInit[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS\r\n";

static const char kSdpStringAudio[] =
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n";

static const char kSdpStringVideo[] =
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

static const char kSdpStringMs1Audio0[] =
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 msid:stream1 audiotrack0\r\n";

static const char kSdpStringMs1Video0[] =
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 msid:stream1 videotrack0\r\n";

static const char kSdpStringMs1Audio1[] =
    "a=ssrc:3 cname:stream1\r\n"
    "a=ssrc:3 msid:stream1 audiotrack1\r\n";

static const char kSdpStringMs1Video1[] =
    "a=ssrc:4 cname:stream1\r\n"
    "a=ssrc:4 msid:stream1 videotrack1\r\n";

// Verifies that |options| contain all tracks in |collection| and that
// the |options| has set the the has_audio and has_video flags correct.
static void VerifyMediaOptions(StreamCollectionInterface* collection,
                               const cricket::MediaSessionOptions& options) {
  if (!collection) {
    return;
  }

  size_t stream_index = 0;
  for (size_t i = 0; i < collection->count(); ++i) {
    MediaStreamInterface* stream = collection->at(i);
    ASSERT_GE(options.streams.size(), stream->audio_tracks()->count());
    for (size_t j = 0; j < stream->audio_tracks()->count(); ++j) {
      webrtc::AudioTrackInterface* audio = stream->audio_tracks()->at(j);
      EXPECT_EQ(options.streams[stream_index].sync_label, stream->label());
      EXPECT_EQ(options.streams[stream_index++].name, audio->id());
      EXPECT_TRUE(options.has_audio);
    }
    ASSERT_GE(options.streams.size(), stream->audio_tracks()->count());
    for (size_t j = 0; j < stream->video_tracks()->count(); ++j) {
      webrtc::VideoTrackInterface* video = stream->video_tracks()->at(j);
      EXPECT_EQ(options.streams[stream_index].sync_label, stream->label());
      EXPECT_EQ(options.streams[stream_index++].name, video->id());
      EXPECT_TRUE(options.has_video);
    }
  }
}

static bool CompareStreamCollections(StreamCollectionInterface* s1,
                                     StreamCollectionInterface* s2) {
  if (s1 == NULL || s2 == NULL || s1->count() != s2->count())
    return false;

  for (size_t i = 0; i != s1->count(); ++i) {
    if (s1->at(i)->label() != s2->at(i)->label())
      return false;
    webrtc::AudioTrackVector audio_tracks1 = s1->at(i)->GetAudioTracks();
    webrtc::AudioTrackVector audio_tracks2 = s2->at(i)->GetAudioTracks();
    webrtc::VideoTrackVector video_tracks1 = s1->at(i)->GetVideoTracks();
    webrtc::VideoTrackVector video_tracks2 = s2->at(i)->GetVideoTracks();

    if (audio_tracks1.size() != audio_tracks2.size())
      return false;
    for (size_t j = 0; j != audio_tracks1.size(); ++j) {
       if (audio_tracks1[j]->id() != audio_tracks2[j]->id())
         return false;
    }
    if (video_tracks1.size() != video_tracks2.size())
      return false;
    for (size_t j = 0; j != video_tracks1.size(); ++j) {
      if (video_tracks1[j]->id() != video_tracks2[j]->id())
        return false;
    }
  }
  return true;
}

// MockRemoteStreamObserver implements functions for listening to
// callbacks about added and removed remote MediaStreams.
class MockRemoteStreamObserver : public webrtc::RemoteMediaStreamObserver {
 public:
  MockRemoteStreamObserver()
      : remote_media_streams_(StreamCollection::Create()) {
  }

  virtual ~MockRemoteStreamObserver() {
  }

  // New remote stream have been discovered.
  virtual void OnAddStream(MediaStreamInterface* remote_stream) {
    remote_media_streams_->AddStream(remote_stream);
  }

  // Remote stream is no longer available.
  virtual void OnRemoveStream(MediaStreamInterface* remote_stream) {
    remote_media_streams_->RemoveStream(remote_stream);
  }

  virtual void OnAddDataChannel(DataChannelInterface* data_channel) {
  }

  MediaStreamInterface* RemoteStream(const std::string& label) {
    return remote_media_streams_->find(label);
  }

  StreamCollectionInterface* remote_streams() const {
    return remote_media_streams_;
  }

 private:
  talk_base::scoped_refptr<StreamCollection> remote_media_streams_;
};

class MediaStreamSignalingForTest : public webrtc::MediaStreamSignaling {
 public:
  explicit MediaStreamSignalingForTest(MockRemoteStreamObserver* observer)
      : webrtc::MediaStreamSignaling(talk_base::Thread::Current(), observer) {
  };
  using webrtc::MediaStreamSignaling::SetLocalStreams;
  using webrtc::MediaStreamSignaling::GetOptionsForOffer;
  using webrtc::MediaStreamSignaling::GetOptionsForAnswer;
  using webrtc::MediaStreamSignaling::UpdateRemoteStreams;
  using webrtc::MediaStreamSignaling::remote_streams;
};

class MediaStreamSignalingTest: public testing::Test {
 protected:
  virtual void SetUp() {
    observer_.reset(new MockRemoteStreamObserver());
    signaling_.reset(new MediaStreamSignalingForTest(observer_.get()));
  }

  void TestGetMediaSessionOptionsForOffer(
      StreamCollectionInterface* streams,
      const MediaConstraintsInterface* constraints) {
    signaling_->SetLocalStreams(streams);
    cricket::MediaSessionOptions options;
    EXPECT_TRUE(signaling_->GetOptionsForOffer(constraints, &options));
    VerifyMediaOptions(streams, options);
  }

  // Create a collection of streams.
  // CreateStreamCollection(1) creates a collection that
  // correspond to kSdpString1.
  // CreateStreamCollection(2) correspond to kSdpString2.
  talk_base::scoped_refptr<StreamCollection>
  CreateStreamCollection(int number_of_streams) {
    talk_base::scoped_refptr<StreamCollection> local_collection(
        StreamCollection::Create());

    for (int i = 0; i < number_of_streams; ++i) {
      talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream(
          webrtc::MediaStream::Create(kStreams[i]));

      // Add a local audio track.
      talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
          webrtc::AudioTrack::Create(kAudioTracks[i], NULL));
      stream->AddTrack(audio_track);

      // Add a local video track.
      talk_base::scoped_refptr<webrtc::VideoTrackInterface> video_track(
          webrtc::VideoTrack::Create(kVideoTracks[i], NULL));
      stream->AddTrack(video_track);

      local_collection->AddStream(stream);
    }
    return local_collection;
  }

  // This functions Creates a MediaStream with label kStreams[0] and
  // |number_of_audio_tracks| and |number_of_video_tracks| tracks and the
  // corresponding SessionDescriptionInterface. The SessionDescriptionInterface
  // is returned in |desc| and the MediaStream is stored in
  // |reference_collection_|
  void CreateSessionDescriptionAndReference(
      size_t number_of_audio_tracks,
      size_t number_of_video_tracks,
      SessionDescriptionInterface** desc) {
    ASSERT_TRUE(desc != NULL);
    ASSERT_LE(number_of_audio_tracks, 2u);
    ASSERT_LE(number_of_video_tracks, 2u);

    reference_collection_ = StreamCollection::Create();
    std::string sdp_ms1 = std::string(kSdpStringInit);

    std::string mediastream_label = kStreams[0];

    talk_base::scoped_refptr<webrtc::MediaStreamInterface> stream(
            webrtc::MediaStream::Create(mediastream_label));
    reference_collection_->AddStream(stream);

    if (number_of_audio_tracks > 0) {
      sdp_ms1 += std::string(kSdpStringAudio);
      sdp_ms1 += std::string(kSdpStringMs1Audio0);
      AddAudioTrack(kAudioTracks[0], stream);
    }
    if (number_of_audio_tracks > 1) {
      sdp_ms1 += kSdpStringMs1Audio1;
      AddAudioTrack(kAudioTracks[1], stream);
    }

    if (number_of_video_tracks > 0) {
      sdp_ms1 += std::string(kSdpStringVideo);
      sdp_ms1 += std::string(kSdpStringMs1Video0);
      AddVideoTrack(kVideoTracks[0], stream);
    }
    if (number_of_video_tracks > 1) {
      sdp_ms1 += kSdpStringMs1Video1;
      AddVideoTrack(kVideoTracks[1], stream);
    }

    *desc = webrtc::CreateSessionDescription(
        SessionDescriptionInterface::kOffer, sdp_ms1, NULL);
  }

  void AddAudioTrack(const std::string& track_id,
                     MediaStreamInterface* stream) {
    talk_base::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
        webrtc::AudioTrack::Create(track_id, NULL));
    ASSERT_TRUE(stream->AddTrack(audio_track));
  }

  void AddVideoTrack(const std::string& track_id,
                     MediaStreamInterface* stream) {
    talk_base::scoped_refptr<webrtc::VideoTrackInterface> video_track(
        webrtc::VideoTrack::Create(track_id, NULL));
    ASSERT_TRUE(stream->AddTrack(video_track));
  }

  talk_base::scoped_refptr<StreamCollection> reference_collection_;
  talk_base::scoped_ptr<MockRemoteStreamObserver> observer_;
  talk_base::scoped_ptr<MediaStreamSignalingForTest> signaling_;
};

// Test that a MediaSessionOptions is created for an offer if
// kOfferToReceiveAudio and kOfferToReceiveVideo constraints are set but no
// MediaStreams are sent.
TEST_F(MediaStreamSignalingTest, GetMediaSessionOptionsForOfferWithAudioVideo) {
  FakeConstraints constraints;
  constraints.SetMandatoryReceiveAudio(true);
  constraints.SetMandatoryReceiveVideo(true);
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&constraints, &options));
  EXPECT_TRUE(options.has_audio);
  EXPECT_TRUE(options.has_video);
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// kOfferToReceiveAudio constraints is set but no MediaStreams are sent.
TEST_F(MediaStreamSignalingTest, GetMediaSessionOptionsForOfferWithAudio) {
  FakeConstraints constraints;
  constraints.SetMandatoryReceiveAudio(true);
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&constraints, &options));
  EXPECT_TRUE(options.has_audio);
  EXPECT_FALSE(options.has_video);
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// no constraints or MediaStreams are sent.
TEST_F(MediaStreamSignalingTest, GetDefaultMediaSessionOptionsForOffer) {
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(NULL, &options));
  EXPECT_TRUE(options.has_audio);
  EXPECT_FALSE(options.has_video);
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// kOfferToReceiveVideo constraints is set but no MediaStreams are sent.
TEST_F(MediaStreamSignalingTest, GetMediaSessionOptionsForOfferWithVideo) {
  FakeConstraints constraints;
  constraints.SetMandatoryReceiveAudio(false);
  constraints.SetMandatoryReceiveVideo(true);
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&constraints, &options));
  EXPECT_FALSE(options.has_audio);
  EXPECT_TRUE(options.has_video);
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// kUseRtpMux constraints is set to false.
TEST_F(MediaStreamSignalingTest,
       GetMediaSessionOptionsForOfferWithBundleDisabled) {
  FakeConstraints constraints;
  constraints.SetMandatoryReceiveAudio(true);
  constraints.SetMandatoryReceiveVideo(true);
  constraints.SetMandatoryUseRtpMux(false);
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&constraints, &options));
  EXPECT_TRUE(options.has_audio);
  EXPECT_TRUE(options.has_video);
  EXPECT_FALSE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created to restart ice if
// kIceRestart constraints is set. It also tests that subsequent
// MediaSessionOptions don't have |transport_options.ice_restart| set.
TEST_F(MediaStreamSignalingTest,
       GetMediaSessionOptionsForOfferWithIceRestart) {
  FakeConstraints constraints;
  constraints.SetMandatoryIceRestart(true);
  cricket::MediaSessionOptions options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&constraints, &options));
  EXPECT_TRUE(options.transport_options.ice_restart);

  EXPECT_TRUE(signaling_->GetOptionsForOffer(NULL, &options));
  EXPECT_FALSE(options.transport_options.ice_restart);
}

// Test that GetMediaSessionOptionsForOffer and GetOptionsForAnswer work as
// expected if unknown constraints are used.
TEST_F(MediaStreamSignalingTest, GetMediaSessionOptionsWithBadConstraints) {
  FakeConstraints mandatory;
  mandatory.AddMandatory("bad_key", "bad_value");
  cricket::MediaSessionOptions options;
  EXPECT_FALSE(signaling_->GetOptionsForOffer(&mandatory, &options));
  EXPECT_FALSE(signaling_->GetOptionsForAnswer(&mandatory, &options));

  FakeConstraints optional;
  optional.AddOptional("bad_key", "bad_value");
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&optional, &options));
  EXPECT_TRUE(signaling_->GetOptionsForAnswer(&optional, &options));
}

// Test that a correct MediaSessionOptions are created for an offer if
// a MediaStream is sent and later updated with a new track.
// MediaConstraints are not used.
TEST_F(MediaStreamSignalingTest, AddTrackToLocalMediaStream) {
  talk_base::scoped_refptr<StreamCollection> local_streams(
      CreateStreamCollection(1));
  TestGetMediaSessionOptionsForOffer(local_streams, NULL);

  local_streams->at(0)->AddTrack(AudioTrack::Create(kAudioTracks[1], NULL));
  TestGetMediaSessionOptionsForOffer(local_streams, NULL);
}

// Test that the MediaConstraints in an answer don't affect if audio and video
// is offered in an offer but that if kOfferToReceiveAudio or
// kOfferToReceiveVideo constraints are true in an offer, the media type will be
// included in subsequent answers.
TEST_F(MediaStreamSignalingTest, MediaConstraintsInAnswer) {
  FakeConstraints answer_c;
  answer_c.SetMandatoryReceiveAudio(true);
  answer_c.SetMandatoryReceiveVideo(true);

  cricket::MediaSessionOptions answer_options;
  EXPECT_TRUE(signaling_->GetOptionsForAnswer(&answer_c, &answer_options));
  EXPECT_TRUE(answer_options.has_audio);
  EXPECT_TRUE(answer_options.has_video);

  FakeConstraints offer_c;
  offer_c.SetMandatoryReceiveAudio(false);
  offer_c.SetMandatoryReceiveVideo(false);

  cricket::MediaSessionOptions offer_options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&offer_c, &offer_options));
  EXPECT_FALSE(offer_options.has_audio);
  EXPECT_FALSE(offer_options.has_video);

  FakeConstraints updated_offer_c;
  updated_offer_c.SetMandatoryReceiveAudio(true);
  updated_offer_c.SetMandatoryReceiveVideo(true);

  cricket::MediaSessionOptions updated_offer_options;
  EXPECT_TRUE(signaling_->GetOptionsForOffer(&updated_offer_c,
                                             &updated_offer_options));
  EXPECT_TRUE(updated_offer_options.has_audio);
  EXPECT_TRUE(updated_offer_options.has_video);

  // Since an offer has been created with both audio and video, subsequent
  // offers and answers should contain both audio and video.
  // Answers will only contain the media types that exist in the offer
  // regardless of the value of |updated_answer_options.has_audio| and
  // |updated_answer_options.has_video|.
  FakeConstraints updated_answer_c;
  answer_c.SetMandatoryReceiveAudio(false);
  answer_c.SetMandatoryReceiveVideo(false);

  cricket::MediaSessionOptions updated_answer_options;
  EXPECT_TRUE(signaling_->GetOptionsForAnswer(&updated_answer_c,
                                              &updated_answer_options));
  EXPECT_TRUE(updated_answer_options.has_audio);
  EXPECT_TRUE(updated_answer_options.has_video);

  EXPECT_TRUE(signaling_->GetOptionsForOffer(NULL,
                                             &updated_offer_options));
  EXPECT_TRUE(updated_offer_options.has_audio);
  EXPECT_TRUE(updated_offer_options.has_video);
}

// This test verifies that the remote MediaStreams corresponding to a received
// SDP string is created. In this test the two separate MediaStreams are
// signaled.
TEST_F(MediaStreamSignalingTest, UpdateRemoteStreams) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithStream1, NULL));
  EXPECT_TRUE(desc != NULL);
  signaling_->UpdateRemoteStreams(desc.get());

  talk_base::scoped_refptr<StreamCollection> reference(
      CreateStreamCollection(1));
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference.get()));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference.get()));

  // Create a session description based on another SDP with another
  // MediaStream.
  talk_base::scoped_ptr<SessionDescriptionInterface> update_desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWith2Stream, NULL));
  EXPECT_TRUE(update_desc != NULL);
  signaling_->UpdateRemoteStreams(update_desc.get());

  talk_base::scoped_refptr<StreamCollection> reference2(
      CreateStreamCollection(2));
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference2.get()));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference2.get()));
}

// This test verifies that the remote MediaStreams corresponding to a received
// SDP string is created. In this test the same remote MediaStream is signaled
// but MediaStream tracks are added and removed.
TEST_F(MediaStreamSignalingTest, AddRemoveTrackFromExistingRemoteMediaStream) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc_ms1;
  CreateSessionDescriptionAndReference(1, 1, desc_ms1.use());
  signaling_->UpdateRemoteStreams(desc_ms1.get());
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference_collection_));

  // Add extra audio and video tracks to the same MediaStream.
  talk_base::scoped_ptr<SessionDescriptionInterface> desc_ms1_two_tracks;
  CreateSessionDescriptionAndReference(2, 2, desc_ms1_two_tracks.use());
  signaling_->UpdateRemoteStreams(desc_ms1_two_tracks.get());
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference_collection_));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference_collection_));

  // Remove the extra audio and video tracks again.
  CreateSessionDescriptionAndReference(1, 1, desc_ms1.use());
  signaling_->UpdateRemoteStreams(desc_ms1.get());
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference_collection_));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference_collection_));
}

// This tests that a default MediaStream is created if a remote session
// description doesn't contain any streams and no MSID support.
// It also tests that the default stream is updated if a video m-line is added
// in a subsequent session description.
TEST_F(MediaStreamSignalingTest, SdpWithoutMsidCreatesDefaultStream) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc_audio_only(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithoutStreamsAudioOnly,
                                       NULL));
  ASSERT_TRUE(desc_audio_only != NULL);
  signaling_->UpdateRemoteStreams(desc_audio_only.get());

  EXPECT_EQ(1u, signaling_->remote_streams()->count());
  ASSERT_EQ(1u, observer_->remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_->remote_streams()->at(0);

  EXPECT_EQ(1u, remote_stream->audio_tracks()->count());
  EXPECT_EQ(0u, remote_stream->video_tracks()->count());
  EXPECT_EQ("default", remote_stream->label());

  talk_base::scoped_ptr<SessionDescriptionInterface> desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithoutStreams, NULL));
  ASSERT_TRUE(desc != NULL);
  signaling_->UpdateRemoteStreams(desc.get());
  EXPECT_EQ(1u, signaling_->remote_streams()->count());
  ASSERT_EQ(1u, remote_stream->audio_tracks()->count());
  EXPECT_EQ("defaulta0", remote_stream->audio_tracks()->at(0)->id());
  ASSERT_EQ(1u, remote_stream->video_tracks()->count());
  EXPECT_EQ("defaultv0", remote_stream->video_tracks()->at(0)->id());
}

// This tests that a default MediaStream is created if the remote session
// description doesn't contain any streams and don't contain an indication if
// MSID is supported.
TEST_F(MediaStreamSignalingTest,
       SdpWithoutMsidAndStreamsCreatesDefaultStream) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithoutStreams,
                                       NULL));
  ASSERT_TRUE(desc != NULL);
  signaling_->UpdateRemoteStreams(desc.get());

  ASSERT_EQ(1u, observer_->remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_->remote_streams()->at(0);
  EXPECT_EQ(1u, remote_stream->audio_tracks()->count());
  EXPECT_EQ(1u, remote_stream->video_tracks()->count());
}

// This tests that a default MediaStream is not created if the remote session
// description doesn't contain any streams but does support MSID.
TEST_F(MediaStreamSignalingTest, SdpWitMsidDontCreatesDefaultStream) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc_msid_without_streams(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithMsidWithoutStreams,
                                       NULL));
  signaling_->UpdateRemoteStreams(desc_msid_without_streams.get());
  EXPECT_EQ(0u, observer_->remote_streams()->count());
}

// This test that a default MediaStream is not created if a remote session
// description is updated to not have any MediaStreams.
TEST_F(MediaStreamSignalingTest, VerifyDefaultStreamIsNotCreated) {
  talk_base::scoped_ptr<SessionDescriptionInterface> desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithStream1,
                                       NULL));
  ASSERT_TRUE(desc != NULL);
  signaling_->UpdateRemoteStreams(desc.get());
  talk_base::scoped_refptr<StreamCollection> reference(
      CreateStreamCollection(1));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference.get()));

  talk_base::scoped_ptr<SessionDescriptionInterface> desc_without_streams(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       kSdpStringWithoutStreams,
                                       NULL));
  signaling_->UpdateRemoteStreams(desc_without_streams.get());
  EXPECT_EQ(0u, observer_->remote_streams()->count());
}
