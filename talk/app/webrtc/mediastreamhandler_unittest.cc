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

#include "talk/app/webrtc/mediastreamhandler.h"

#include <string>

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/localvideosource.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/base/gunit.h"
#include "talk/media/base/fakevideocapturer.h"
#include "talk/media/base/mediachannel.h"
#include "testing/base/public/gmock.h"

using ::testing::_;
using ::testing::Exactly;

static const char kStreamLabel1[] = "local_stream_1";
static const char kVideoTrackId[] = "video_1";
static const char kAudioTrackId[] = "audio_1";

namespace webrtc {

// Helper class to test MediaStreamHandler.
class MockAudioProvider : public AudioProviderInterface {
 public:
  virtual ~MockAudioProvider() {}
  MOCK_METHOD2(SetAudioPlayout, void(const std::string& name, bool enable));
  MOCK_METHOD3(SetAudioSend, void(const std::string& name, bool enable,
                                  const cricket::AudioOptions& options));
};

// Helper class to test MediaStreamHandler.
class MockVideoProvider : public VideoProviderInterface {
 public:
  virtual ~MockVideoProvider() {}
  MOCK_METHOD2(SetCaptureDevice, bool(const std::string& name,
                                      cricket::VideoCapturer* camera));
  MOCK_METHOD3(SetVideoPlayout, void(const std::string& name,
                                     bool enable,
                                     cricket::VideoRenderer* renderer));
  MOCK_METHOD3(SetVideoSend, void(const std::string& name, bool enable,
                                  const cricket::VideoOptions* options));
};

class FakeVideoSource : public Notifier<VideoSourceInterface> {
 public:
  static talk_base::scoped_refptr<FakeVideoSource> Create() {
    return new talk_base::RefCountedObject<FakeVideoSource>();
  }
  virtual cricket::VideoCapturer* GetVideoCapturer() {
    return &fake_capturer_;
  }
  virtual void AddSink(cricket::VideoRenderer* output) {}
  virtual void RemoveSink(cricket::VideoRenderer* output) {}
  virtual SourceState state() const { return state_; }
  virtual const cricket::VideoOptions* options() const { return &options_; }

 protected:
  FakeVideoSource() : state_(kLive) {}
  ~FakeVideoSource() {}

 private:
  cricket::FakeVideoCapturer fake_capturer_;
  SourceState state_;
  cricket::VideoOptions options_;
};

class MediaStreamHandlerTest : public testing::Test {
 public:
  MediaStreamHandlerTest()
      : handlers_(&audio_provider_, &video_provider_) {
  }

  virtual void SetUp() {
    collection_ = StreamCollection::Create();
    stream_ = MediaStream::Create(kStreamLabel1);
    talk_base::scoped_refptr<VideoSourceInterface> source(
        FakeVideoSource::Create());
    video_track_ = VideoTrack::Create(kVideoTrackId, source);
    EXPECT_TRUE(stream_->AddTrack(video_track_));
    audio_track_ = AudioTrack::Create(kAudioTrackId,
                                           NULL);
    EXPECT_TRUE(stream_->AddTrack(audio_track_));
  }

  void AddLocalStream() {
    collection_->AddStream(stream_);
    EXPECT_CALL(video_provider_, SetCaptureDevice(
        kVideoTrackId, video_track_->GetSource()->GetVideoCapturer()));
    EXPECT_CALL(video_provider_, SetVideoSend(kVideoTrackId, true, _));
    EXPECT_CALL(audio_provider_, SetAudioSend(kAudioTrackId, true, _));
    handlers_.CommitLocalStreams(collection_);
  }

  void RemoveLocalStream() {
    collection_->RemoveStream(stream_);
    handlers_.CommitLocalStreams(collection_);
  }

  void AddRemoteStream() {
    EXPECT_CALL(video_provider_, SetVideoPlayout(kVideoTrackId, true,
                                                 video_track_->FrameInput()));
    EXPECT_CALL(audio_provider_, SetAudioPlayout(kAudioTrackId, true));
    handlers_.AddRemoteStream(stream_);
  }

  void RemoveRemoteStream() {
    EXPECT_CALL(video_provider_, SetVideoPlayout(kVideoTrackId, false,
                                                 NULL));
    handlers_.RemoveRemoteStream(stream_);
  }

 protected:
  MockAudioProvider audio_provider_;
  MockVideoProvider video_provider_;
  MediaStreamHandlers handlers_;
  talk_base::scoped_refptr<StreamCollection> collection_;
  talk_base::scoped_refptr<LocalMediaStreamInterface> stream_;
  talk_base::scoped_refptr<VideoTrackInterface> video_track_;
  talk_base::scoped_refptr<AudioTrackInterface> audio_track_;
};

TEST_F(MediaStreamHandlerTest, AddRemoveLocalMediaStream) {
  AddLocalStream();
  RemoveLocalStream();
}

TEST_F(MediaStreamHandlerTest, AddRemoveRemoteMediaStream) {
  AddRemoteStream();
  RemoveRemoteStream();
}

TEST_F(MediaStreamHandlerTest, LocalAudioTrackDisable) {
  AddLocalStream();

  EXPECT_CALL(audio_provider_, SetAudioSend(kAudioTrackId, false, _));
  audio_track_->set_enabled(false);

  EXPECT_CALL(audio_provider_, SetAudioSend(kAudioTrackId, true, _));
  audio_track_->set_enabled(true);

  RemoveLocalStream();
}

TEST_F(MediaStreamHandlerTest, RemoteAudioTrackDisable) {
  AddRemoteStream();

  EXPECT_CALL(audio_provider_, SetAudioPlayout(kAudioTrackId, false));
  audio_track_->set_enabled(false);

  EXPECT_CALL(audio_provider_, SetAudioPlayout(kAudioTrackId, true));
  audio_track_->set_enabled(true);

  RemoveRemoteStream();
}

TEST_F(MediaStreamHandlerTest, LocalVideoTrackDisable) {
  AddLocalStream();

  EXPECT_CALL(video_provider_, SetVideoSend(kVideoTrackId, false, _));
  video_track_->set_enabled(false);

  EXPECT_CALL(video_provider_, SetVideoSend(kVideoTrackId, true, _));
  video_track_->set_enabled(true);

  RemoveLocalStream();
}

TEST_F(MediaStreamHandlerTest, RemoteVideoTrackDisable) {
  AddRemoteStream();

  EXPECT_CALL(video_provider_, SetVideoPlayout(kVideoTrackId, false, _));
  video_track_->set_enabled(false);

  EXPECT_CALL(video_provider_, SetVideoPlayout(kVideoTrackId, true,
                                               video_track_->FrameInput()));
  video_track_->set_enabled(true);

  RemoveRemoteStream();
}

}  // namespace webrtc
