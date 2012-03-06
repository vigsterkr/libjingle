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
#include "talk/app/webrtc/jsepsignaling.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/sessiondescriptionprovider.h"
#include "talk/app/webrtc/streamcollectionimpl.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/thread.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessiondescription.h"

static const char kStreams[][8] = {"stream1", "stream2"};
static const char kAudioTracks[][8] = {"audio_1", "audio_2"};
static const char kVideoTracks[][8] = {"video_1", "video_2"};

using webrtc::IceCandidateInterface;
using webrtc::MediaStreamInterface;
using webrtc::SessionDescriptionInterface;

using webrtc::StreamCollection;
using webrtc::StreamCollectionInterface;

// Reference SDP with a MediaStream with label "stream1" and audio track with
// label "audio_1" and a video track with label "video_1;
static const char kSdpString1[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 mslabel:stream1\r\n"
    "a=ssrc:1 label:audio_1\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 mslabel:stream1\r\n"
    "a=ssrc:2 label:video_1\r\n";

// Reference SDP with two MediaStreams with label "stream1" and "stream2. Each
// MediaStreams have one audio track and one video track.
static const char kSdpString2[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=\r\n"
    "t=0 0\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 mslabel:stream1\r\n"
    "a=ssrc:1 label:audio_1\r\n"
    "a=ssrc:3 cname:stream2\r\n"
    "a=ssrc:3 mslabel:stream2\r\n"
    "a=ssrc:3 label:audio_2\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=rtpmap:120 VP8/0\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 mslabel:stream1\r\n"
    "a=ssrc:2 label:video_1\r\n"
    "a=ssrc:4 cname:stream2\r\n"
    "a=ssrc:4 mslabel:stream2\r\n"
    "a=ssrc:4 label:video_2\r\n";

static const char kSdpCandidates[] =
    "a=candidate:1 1 udp 1 127.0.0.1 1234 typ host name rtp network_name "
    "eth0 username user_rtp password password_rtp generation 0\r\n";

// Creates a SessionDescription with StreamParams.
// CreateMockSessionDescription(1) creates a CreateMockSessionDescription that
// correspond to kSdpString1.
// CreateMockSessionDescription(2) correspond to  kSdpString2.
static cricket::SessionDescription* CreateMockSessionDescription(
    int number_of_streams) {
  cricket::SessionDescription* desc(new cricket::SessionDescription());
  // AudioContentDescription
  talk_base::scoped_ptr<cricket::AudioContentDescription> audio(
      new cricket::AudioContentDescription());

  // VideoContentDescription
  talk_base::scoped_ptr<cricket::VideoContentDescription> video(
      new cricket::VideoContentDescription());

  for (int i = 0; i < number_of_streams; ++i) {
    cricket::StreamParams audio_stream;
    audio_stream.name = kAudioTracks[i];
    audio_stream.cname = kStreams[i];
    audio_stream.sync_label = kStreams[i];
    audio_stream.ssrcs.push_back((i*2)+1);
    audio->AddStream(audio_stream);

    cricket::StreamParams video_stream;
    video_stream.name = kVideoTracks[i];
    video_stream.cname = kStreams[i];
    video_stream.sync_label = kStreams[i];
    video_stream.ssrcs.push_back((i*2)+2);
    video->AddStream(video_stream);
  }

  audio->AddCodec(cricket::AudioCodec(103, "ISAC", 16000, 0, 0, 0));
  desc->AddContent(cricket::CN_AUDIO, cricket::NS_JINGLE_RTP,
                   audio.release());

  video->AddCodec(cricket::VideoCodec(120, "VP8", 640, 480, 30, 0));
  desc->AddContent(cricket::CN_VIDEO, cricket::NS_JINGLE_RTP,
                   video.release());
  return desc;
}

// Create a collection of streams.
// CreateStreamCollection(1) creates a collection that
// correspond to kSdpString1.
// CreateStreamCollection(2) correspond to kSdpString2.
static talk_base::scoped_refptr<StreamCollection>
CreateStreamCollection(int number_of_streams) {
  talk_base::scoped_refptr<StreamCollection> local_collection(
      StreamCollection::Create());

  for (int i = 0; i < number_of_streams; ++i) {
    talk_base::scoped_refptr<webrtc::LocalMediaStreamInterface> stream(
        webrtc::MediaStream::Create(kStreams[i]));

    // Add a local audio track.
    talk_base::scoped_refptr<webrtc::LocalAudioTrackInterface>
    audio_track(webrtc::AudioTrack::CreateLocal(kAudioTracks[i], NULL));
    stream->AddTrack(audio_track);

    // Add a local video track.
    talk_base::scoped_refptr<webrtc::LocalVideoTrackInterface>
    video_track(webrtc::VideoTrack::CreateLocal(kVideoTracks[i], NULL));
    stream->AddTrack(video_track);

    local_collection->AddStream(stream);
  }
  return local_collection;
}

static cricket::Candidate CreateMockCandidate() {
  talk_base::SocketAddress address("127.0.0.1", 1234);
  return cricket::Candidate("rtp", "udp", address, 1, "user_rtp",
                            "password_rtp", "local", "eth0", 0);
}


// Verifies that |options| contain all tracks in |collection| if |hints| allow
// them.
static void VerifyMediaOptions(StreamCollectionInterface* collection,
                               const webrtc::MediaHints hints,
                               const cricket::MediaSessionOptions& options) {
  EXPECT_EQ(hints.has_audio(), options.has_audio);
  EXPECT_EQ(hints.has_video(), options.has_video);

  if (!collection)
    return;

  size_t stream_index = 0;
  for (size_t i = 0; i < collection->count(); ++i) {
    MediaStreamInterface* stream = collection->at(i);
    ASSERT_GE(options.streams.size(), stream->audio_tracks()->count());
    for (size_t j = 0; j < stream->audio_tracks()->count(); ++j) {
      webrtc::AudioTrackInterface* audio = stream->audio_tracks()->at(j);
      EXPECT_EQ(options.streams[stream_index].sync_label, stream->label());
      EXPECT_EQ(options.streams[stream_index++].name, audio->label());
    }
    ASSERT_GE(options.streams.size(), stream->audio_tracks()->count());
    for (size_t j = 0; j < stream->video_tracks()->count(); ++j) {
      webrtc::VideoTrackInterface* video = stream->video_tracks()->at(j);
      EXPECT_EQ(options.streams[stream_index].sync_label, stream->label());
      EXPECT_EQ(options.streams[stream_index++].name, video->label());
    }
  }
}

// Checks that two SessionDescription have the same audio and video
// StreamParams.
static bool CompareSessionDescripionStreams(
    const cricket::SessionDescription* desc1,
    const cricket::SessionDescription* desc2) {
  const cricket::ContentInfo* ac1 = desc1->GetContentByName("audio");
  const cricket::AudioContentDescription* acd1 =
      static_cast<const cricket::AudioContentDescription*>(ac1->description);
  const cricket::ContentInfo* vc1 = desc1->GetContentByName("video");
  const cricket::VideoContentDescription* vcd1 =
      static_cast<const cricket::VideoContentDescription*>(vc1->description);

  const cricket::ContentInfo* ac2 = desc2->GetContentByName("audio");
  const cricket::AudioContentDescription* acd2 =
      static_cast<const cricket::AudioContentDescription*>(ac2->description);
  const cricket::ContentInfo* vc2 = desc2->GetContentByName("video");
  const cricket::VideoContentDescription* vcd2 =
      static_cast<const cricket::VideoContentDescription*>(vc2->description);

  if ((acd1 == NULL && acd2 != NULL) ||
      (acd2 == NULL && acd1 != NULL))
    return false;
  if ((vcd1 == NULL && vcd2 != NULL) ||
      (vcd2 == NULL && vcd1 != NULL))
    return false;
  if ((acd1 != NULL && acd2 != NULL) &&
      acd1->streams() != acd2->streams())
    return false;
  if ((vcd1 != NULL && vcd2 != NULL) &&
      vcd1->streams() != vcd2->streams())
    return false;
  return true;
}

static bool CompareStreamCollections(StreamCollectionInterface* s1,
                                     StreamCollectionInterface* s2) {
  if (s1 == NULL || s2 == NULL || s1->count() != s2->count())
    return false;

  for (size_t i = 0; i != s1->count(); ++i) {
    if (s1->at(i)->label() != s2->at(i)->label())
      return false;
    webrtc::AudioTracks* audio_tracks1 = s1->at(i)->audio_tracks();
    webrtc::AudioTracks* audio_tracks2 = s2->at(i)->audio_tracks();
    webrtc::VideoTracks* video_tracks1 = s1->at(i)->video_tracks();
    webrtc::VideoTracks* video_tracks2 = s2->at(i)->video_tracks();

    if (audio_tracks1->count() != audio_tracks2->count())
      return false;
    for (size_t j = 0; j != audio_tracks1->count(); ++j) {
       if (audio_tracks1->at(j)->label() != audio_tracks2->at(j)->label())
         return false;
    }
    if (video_tracks1->count() != video_tracks2->count())
      return false;
    for (size_t j = 0; j != video_tracks1->count(); ++j) {
       if (video_tracks1->at(j)->label() != video_tracks2->at(j)->label())
         return false;
    }
  }
  return true;
}

// Fake implementation of SessionDescriptionProvider.
// JsepSignaling uses this object to create session descriptions.
class FakeSessionDescriptionProvider
    : public webrtc::SessionDescriptionProvider {
 public:
  explicit FakeSessionDescriptionProvider(int number_of_streams)
      : number_of_streams_(number_of_streams) {
  }

  virtual cricket::SessionDescription* CreateOffer(
      const cricket::MediaSessionOptions& options) {
    options_ = options;
    return CreateMockSessionDescription(number_of_streams_);
  }

  virtual cricket::SessionDescription* CreateAnswer(
      const cricket::SessionDescription* offer,
      const cricket::MediaSessionOptions& options) {
    options_ = options;
    return CreateMockSessionDescription(number_of_streams_);
  }

  virtual bool SetLocalDescription(cricket::SessionDescription* desc,
                           cricket::ContentAction type) {
    local_desc_.reset(desc);
    return true;
  }

  virtual bool SetRemoteDescription(cricket::SessionDescription* desc,
                            cricket::ContentAction type) {
    remote_desc_.reset(desc);
    return true;
  }

  virtual bool AddRemoteCandidate(const std::string& content_name,
                                  const cricket::Candidate& candidate) {
    content_name_ = content_name;
    candidate_ = candidate;
    return true;
  }

  virtual const cricket::SessionDescription* local_description() const {
    return local_desc_.get();
  }

  virtual const cricket::SessionDescription* remote_description() const {
    return remote_desc_.get();
  }

  cricket::MediaSessionOptions options_;
  talk_base::scoped_ptr<const cricket::SessionDescription> local_desc_;
  talk_base::scoped_ptr<const cricket::SessionDescription> remote_desc_;
  cricket::Candidate candidate_;
  std::string content_name_;
  int number_of_streams_;
};

// MockSignalingObserver implements functions for listening to all signals from
// a JsepSignaling instance.
class MockSignalingObserver : public webrtc::JsepRemoteMediaStreamObserver {
 public:
  MockSignalingObserver()
      : ice_complete_(true),
        remote_media_streams_(StreamCollection::Create()) {
  }

  virtual ~MockSignalingObserver() {
  }

  // New remote stream have been discovered.
  virtual void OnAddStream(MediaStreamInterface* remote_stream) {
    EXPECT_EQ(MediaStreamInterface::kLive, remote_stream->ready_state());
    remote_media_streams_->AddStream(remote_stream);
  }

  // Remote stream is no longer available.
  virtual void OnRemoveStream(MediaStreamInterface* remote_stream) {
    EXPECT_EQ(MediaStreamInterface::kEnded, remote_stream->ready_state());
    remote_media_streams_->RemoveStream(remote_stream);
  }

  MediaStreamInterface* RemoteStream(const std::string& label) {
    return remote_media_streams_->find(label);
  }

  StreamCollectionInterface* remote_streams() const {
    return remote_media_streams_;
  }

  bool ice_complete() const { return ice_complete_; }
  const cricket::Candidate& candidate() {
    return candidate_;
  }

  const std::string& candidate_string() {
    return candidate_string_;
  }
  const std::string& candidate_label() {
    return candidate_label_;
  }

 private:
  bool ice_complete_;
  std::string candidate_label_;
  cricket::Candidate candidate_;
  std::string candidate_string_;

  talk_base::scoped_refptr<StreamCollection> remote_media_streams_;
};

class JsepSignalingForTest : public webrtc::JsepSignaling {
 public:
  explicit JsepSignalingForTest(webrtc::SessionDescriptionProvider* provider,
                                MockSignalingObserver* observer)
      : webrtc::JsepSignaling(talk_base::Thread::Current(), provider,
                              observer) {
  };
  using webrtc::JsepSignaling::CreateOffer;
  using webrtc::JsepSignaling::CreateAnswer;
  using webrtc::JsepSignaling::SetLocalDescription;
  using webrtc::JsepSignaling::SetRemoteDescription;
  using webrtc::JsepSignaling::ProcessIceMessage;
};

class JsepSignalingTest: public testing::Test {
 protected:
  virtual void SetUp() {
    provider_.reset(new FakeSessionDescriptionProvider(1));
    observer_.reset(new MockSignalingObserver());
    signaling_.reset(new JsepSignalingForTest(provider_.get(),
                                              observer_.get()));

    local_streams_ = CreateStreamCollection(1);
    signaling_->SetLocalStreams(local_streams_);
  }

  talk_base::scoped_ptr<MockSignalingObserver> observer_;
  talk_base::scoped_refptr<StreamCollection> local_streams_;
  talk_base::scoped_ptr<FakeSessionDescriptionProvider> provider_;
  talk_base::scoped_ptr<JsepSignalingForTest> signaling_;

  void TestOffer(const webrtc::MediaHints& hints,
                 StreamCollectionInterface* streams) {
    talk_base::scoped_ptr<SessionDescriptionInterface> offer(
        signaling_->CreateOffer(hints));
    VerifyMediaOptions(streams, hints, provider_->options_);
    // Verify that the returned offer can be Serialized to a SDP string.
    // Since the provider_ is a mock it always return the same string.
    std::string sdp;
    EXPECT_TRUE(offer->ToString(&sdp));
    EXPECT_EQ(kSdpString1, sdp);
  }

  void TestAnswer(const webrtc::MediaHints& hints,
                  StreamCollectionInterface* streams) {
    talk_base::scoped_ptr<SessionDescriptionInterface> offer(
        webrtc::CreateSessionDescription(kSdpString1));
    ASSERT_TRUE(offer.get() != NULL);

    talk_base::scoped_ptr<SessionDescriptionInterface> answer(
        signaling_->CreateAnswer(hints, offer.get()));
    VerifyMediaOptions(streams, hints, provider_->options_);
    // Verify a SDP string is returned. Since the provider_ is a mock it always
    // return the same string.
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));
    EXPECT_EQ(kSdpString1, sdp);
  }
};

TEST_F(JsepSignalingTest, CreateAudioVideoOffer) {
  webrtc::MediaHints hints;
  TestOffer(hints, local_streams_.get());
}

TEST_F(JsepSignalingTest, CreateAudioOffer) {
  webrtc::MediaHints hints(true, false);
  // Remove all MediaStreams so we only create offer based on hints without
  // sending streams.
  signaling_->SetLocalStreams(NULL);
  TestOffer(hints, NULL);
}

TEST_F(JsepSignalingTest, CreateVideoOffer) {
  webrtc::MediaHints hints(false, true);
  // Remove all MediaStreams so we only create offer based on hints without
  // sending streams.
  signaling_->SetLocalStreams(NULL);
  TestOffer(hints, NULL);
}

TEST_F(JsepSignalingTest, CreateAudioVideoAnswer) {
  webrtc::MediaHints hints;
  TestAnswer(hints, local_streams_.get());
}

TEST_F(JsepSignalingTest, CreateAudioAnswer) {
  webrtc::MediaHints hints(true, false);
  // Remove all MediaStreams so we only create answer based on hints without
  // sending streams.
  signaling_->SetLocalStreams(NULL);
  TestAnswer(hints, NULL);
}

TEST_F(JsepSignalingTest, CreateVideoAnswer) {
  webrtc::MediaHints hints(false, true);
  // Remove all MediaStreams so we only create answer based on hints without
  // sending streams.
  signaling_->SetLocalStreams(NULL);
  TestAnswer(hints, NULL);
}

TEST_F(JsepSignalingTest, SetLocalDescription) {
  SessionDescriptionInterface* desc =
      webrtc::CreateSessionDescription(kSdpString1);
  EXPECT_TRUE(desc != NULL);
  EXPECT_TRUE(signaling_->SetLocalDescription(webrtc::JsepInterface::kOffer,
                                              desc));
  talk_base::scoped_ptr< cricket::SessionDescription> reference_description(
      CreateMockSessionDescription(1));
  EXPECT_TRUE(CompareSessionDescripionStreams(provider_->local_desc_.get(),
                                              reference_description.get()));
}

TEST_F(JsepSignalingTest, SetRemoteDescription) {
  SessionDescriptionInterface* desc = webrtc::CreateSessionDescription(
      kSdpString1);
  EXPECT_TRUE(desc != NULL);
  EXPECT_TRUE(signaling_->SetRemoteDescription(webrtc::JsepInterface::kOffer,
                                               desc));

  talk_base::scoped_ptr< cricket::SessionDescription> reference_description(
      CreateMockSessionDescription(1));
  EXPECT_TRUE(CompareSessionDescripionStreams(provider_->remote_desc_.get(),
                                              reference_description.get()));

  talk_base::scoped_refptr<StreamCollection> reference(
      CreateStreamCollection(1));
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference.get()));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference.get()));

  // Update the remote streams.
  SessionDescriptionInterface* update_desc =
      webrtc::CreateSessionDescription(kSdpString2);
  EXPECT_TRUE(update_desc != NULL);
  EXPECT_TRUE(signaling_->SetRemoteDescription(webrtc::JsepInterface::kOffer,
                                               update_desc));

  talk_base::scoped_refptr<StreamCollection> reference2(
      CreateStreamCollection(2));
  EXPECT_TRUE(CompareStreamCollections(signaling_->remote_streams(),
                                       reference2.get()));
  EXPECT_TRUE(CompareStreamCollections(observer_->remote_streams(),
                                       reference2.get()));
}

TEST_F(JsepSignalingTest, ProcessIceMessage) {
  cricket::Candidate candidate = CreateMockCandidate();
  talk_base::scoped_ptr<IceCandidateInterface> ice_candidate(
      webrtc::CreateIceCandidate(cricket::CN_AUDIO, kSdpCandidates));
  EXPECT_TRUE(signaling_->ProcessIceMessage(ice_candidate.get()));
  EXPECT_TRUE(candidate.IsEquivalent(provider_->candidate_));
}
