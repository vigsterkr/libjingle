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

#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/mediastreamhandler.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/base/thread.h"
#include "talk/base/gunit.h"
#include "testing/base/public/gmock.h"

using testing::_;
using ::testing::Exactly;

static const char kStreamLabel1[] = "local_stream_1";
static const char kVideoDeviceName[] = "dummy_video_cam_1";

namespace webrtc {

// Helper class to test MediaStreamHandler.
class MockMediaProvider : public MediaProviderInterface {
 public:
  virtual ~MockMediaProvider() {}
  MOCK_METHOD1(SetCaptureDevice, bool(const std::string& name));
  MOCK_METHOD1(SetLocalRenderer, void(const std::string& name));
  MOCK_METHOD2(SetRemoteRenderer, void(const std::string& name,
                                       cricket::VideoRenderer* renderer));

  virtual bool SetCaptureDevice(const std::string& name,
                                cricket::VideoCapturer* camera) {
    return SetCaptureDevice(name);
  }
  virtual void SetLocalRenderer(const std::string& name,
                                cricket::VideoRenderer* renderer) {
    SetLocalRenderer(name);
  }
};

class MediaStreamHandlerTest : public testing::Test {
 public:
  MediaStreamHandlerTest()
      : handlers_(&provider_) {
  }

  virtual void SetUp() {
    stream_ = MediaStream::Create(kStreamLabel1);
    video_track_ = VideoTrack::CreateLocal(kVideoDeviceName, NULL);
    EXPECT_TRUE(stream_->AddTrack(video_track_));
  }

 protected:
  MockMediaProvider provider_;
  MediaStreamHandlers handlers_;
  talk_base::scoped_refptr<LocalMediaStreamInterface> stream_;
  talk_base::scoped_refptr<LocalVideoTrackInterface> video_track_;
};

TEST_F(MediaStreamHandlerTest, LocalStreams) {
  talk_base::scoped_refptr<VideoRendererWrapperInterface> renderer(
      CreateVideoRenderer(NULL));
  video_track_->SetRenderer(renderer);

  talk_base::scoped_refptr<StreamCollection> collection(
      StreamCollection::Create());
  collection->AddStream(stream_);

  EXPECT_CALL(provider_, SetLocalRenderer(kVideoDeviceName))
      .Times(Exactly(2));  // SetLocalRender will also be called from dtor of
                           // LocalVideoTrackHandler
  EXPECT_CALL(provider_, SetCaptureDevice(kVideoDeviceName))
      .Times(Exactly(2));  // SetCaptureDevice will also be called from dtor of
                           // LocalVideoTrackHandler
  handlers_.CommitLocalStreams(collection);
  collection->RemoveStream(stream_);
  handlers_.CommitLocalStreams(collection);
}

TEST_F(MediaStreamHandlerTest, RemoteStreams) {
  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName,
                                           video_track_->FrameInput()));
  // SetRemoteRenderer called with the VideoTrack frame input.
  handlers_.AddRemoteStream(stream_);

  // SetRemoteRenderer called with the new renderer. Note, this is the
  // deprecated way of setting a renderer. But we need to support it until all
  // clients have changed.
  talk_base::scoped_refptr<VideoRendererWrapperInterface> renderer(
      CreateVideoRenderer(NULL));
  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName,
                                           renderer->renderer()));
  video_track_->SetRenderer(renderer);

  // Change the already set renderer.
  talk_base::scoped_refptr<VideoRendererWrapperInterface> renderer2(
        CreateVideoRenderer(NULL));
  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName,
                                           renderer2->renderer()));
  video_track_->SetRenderer(renderer2);

  // SetRemoteRenderer called since nothing should be rendered to the
  // VideoTrack.
  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName, NULL));
  handlers_.RemoveRemoteStream(stream_);

  // Change the renderer after the stream have been removed from handler.
  // This should not trigger a call to SetRemoteRenderer.
  renderer = CreateVideoRenderer(NULL);
  video_track_->SetRenderer(renderer);
}

// Test that the VideoTrack frame input is disconnected from the
// MediaProvider if a remote track is disabled.
TEST_F(MediaStreamHandlerTest, RemoteVideoTrackDisable) {
  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName,
                                           video_track_->FrameInput()));
  handlers_.AddRemoteStream(stream_);

  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName, NULL));
  video_track_->set_enabled(false);

  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName,
                                           video_track_->FrameInput()));
  video_track_->set_enabled(true);

  EXPECT_CALL(provider_, SetRemoteRenderer(kVideoDeviceName, NULL));
  handlers_.RemoveRemoteStream(stream_);
}

}  // namespace webrtc
