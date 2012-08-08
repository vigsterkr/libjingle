// Copyright 2008 Google Inc.

#include <stdio.h>
#include <vector>

#include "talk/base/gunit.h"
#include "talk/base/logging.h"
#include "talk/base/thread.h"
#include "talk/media/base/fakevideocapturer.h"
#include "talk/media/base/testutils.h"
#include "talk/media/base/videocapturer.h"

using cricket::FakeVideoCapturer;

class VideoCapturerTest
    : public sigslot::has_slots<>,
      public testing::Test {
 public:
  VideoCapturerTest() : video_frame_received_(false) {
    capturer_.SignalVideoFrame.connect(this, &VideoCapturerTest::OnVideoFrame);
  }

 protected:
  void OnVideoFrame(cricket::VideoCapturer*, const cricket::VideoFrame*) {
    video_frame_received_ = true;
  }
  bool video_frame_received() {
    const bool ret_val = video_frame_received_;
    video_frame_received_ = false;
    return ret_val;
  }

  cricket::FakeVideoCapturer capturer_;
  bool video_frame_received_;
};

TEST_F(VideoCapturerTest, VideoFrame) {
  EXPECT_EQ(cricket::CR_SUCCESS, capturer_.Start(cricket::VideoFormat(
      640,
      480,
      cricket::VideoFormat::FpsToInterval(30),
      cricket::FOURCC_I420)));
  EXPECT_TRUE(capturer_.IsRunning());
  EXPECT_FALSE(video_frame_received());
  EXPECT_TRUE(capturer_.CaptureFrame());
  EXPECT_TRUE(video_frame_received());
}

TEST_F(VideoCapturerTest, TestFourccMatch) {
  cricket::VideoFormat desired(640, 480,
                               cricket::VideoFormat::FpsToInterval(30),
                               cricket::FOURCC_ANY);
  cricket::VideoFormat best;
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.fourcc = cricket::FOURCC_MJPG;
  EXPECT_FALSE(capturer_.GetBestCaptureFormat(desired, &best));

  desired.fourcc = cricket::FOURCC_I420;
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
}

TEST_F(VideoCapturerTest, TestResolutionMatch) {
  cricket::VideoFormat desired(960, 720,
                               cricket::VideoFormat::FpsToInterval(30),
                               cricket::FOURCC_ANY);
  cricket::VideoFormat best;
  // Ask for 960x720. Get VGA which is the highest.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 360;
  desired.height = 250;
  // Ask for a little higher than QVGA. Get QVGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(320, best.width);
  EXPECT_EQ(240, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 480;
  desired.height = 270;
  // Ask for HVGA. Get VGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 320;
  desired.height = 240;
  // Ask for QVGA. Get QVGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(320, best.width);
  EXPECT_EQ(240, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 80;
  desired.height = 60;
  // Ask for lower than QQVGA. Get QQVGA, which is the lowest.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(160, best.width);
  EXPECT_EQ(120, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);
}

TEST_F(VideoCapturerTest, TestHDResolutionMatch) {
  // Add some HD formats
  std::vector<cricket::VideoFormat> formats;
  formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  formats.push_back(cricket::VideoFormat(960, 544,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  formats.push_back(cricket::VideoFormat(2592, 1944,
      cricket::VideoFormat::FpsToInterval(15), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(formats);

  cricket::VideoFormat desired(960, 720,
                               cricket::VideoFormat::FpsToInterval(30),
                               cricket::FOURCC_ANY);
  cricket::VideoFormat best;
  // Ask for 960x720. Get qHD
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(960, best.width);
  EXPECT_EQ(544, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 360;
  desired.height = 250;
  // Ask for a litter higher than QVGA. Get QVGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(320, best.width);
  EXPECT_EQ(240, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 480;
  desired.height = 270;
  // Ask for HVGA. Get VGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 320;
  desired.height = 240;
  // Ask for QVGA. Get QVGA.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(320, best.width);
  EXPECT_EQ(240, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 160;
  desired.height = 120;
  // Ask for lower than QVGA. Get QVGA, which is the lowest.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(320, best.width);
  EXPECT_EQ(240, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 1280;
  desired.height = 720;
  // Ask for HD. Get qHD.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(960, best.width);
  EXPECT_EQ(544, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  desired.width = 1920;
  desired.height = 1080;
  // Ask for 1080p. Get 2592x1944x15.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(desired, &best));
  EXPECT_EQ(2592, best.width);
  EXPECT_EQ(1944, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(15), best.interval);
}

// Some cameras support 320x240 and 320x640. Verify we choose 320x240.
TEST_F(VideoCapturerTest, TestStrangeFormats) {
  std::vector<cricket::VideoFormat> supported_formats;
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(320, 640,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  std::vector<cricket::VideoFormat> required_formats;
  required_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  required_formats.push_back(cricket::VideoFormat(320, 200,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  required_formats.push_back(cricket::VideoFormat(320, 180,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  cricket::VideoFormat best;
  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(320, best.width);
    EXPECT_EQ(240, best.height);
  }

  supported_formats.clear();
  supported_formats.push_back(cricket::VideoFormat(320, 640,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(320, best.width);
    EXPECT_EQ(240, best.height);
  }
}

// Some cameras only have very low fps. Verify we choose something sensible.
TEST_F(VideoCapturerTest, TestPoorFpsFormats) {
  // all formats are low framerate
  std::vector<cricket::VideoFormat> supported_formats;
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(10), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(7), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(1280, 720,
      cricket::VideoFormat::FpsToInterval(2), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  std::vector<cricket::VideoFormat> required_formats;
  required_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  required_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  cricket::VideoFormat best;
  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(required_formats[i].width, best.width);
    EXPECT_EQ(required_formats[i].height, best.height);
  }

  // Increase framerate of 320x240.  Expect low fps VGA avoided.
  // Except on Mac, where QVGA is avoid due to aspect ratio.
  supported_formats.clear();
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(15), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(7), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(1280, 720,
      cricket::VideoFormat::FpsToInterval(2), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(320, best.width);
    EXPECT_EQ(240, best.height);
  }
}

// Some cameras support same size with different frame rates. Verify we choose
// the frame rate properly.
TEST_F(VideoCapturerTest, TestSameSizeDifferentFpsFormats) {
  std::vector<cricket::VideoFormat> supported_formats;
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(10), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(20), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(320, 240,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  std::vector<cricket::VideoFormat> required_formats = supported_formats;
  cricket::VideoFormat best;
  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(320, best.width);
    EXPECT_EQ(240, best.height);
    EXPECT_EQ(required_formats[i].interval, best.interval);
  }
}

// Some cameras support the correct resolution but at a lower fps than
// we'd like.  This tests we get the expected resolution and fps.
TEST_F(VideoCapturerTest, TestFpsFormats) {
  // We have VGA but low fps.  Choose VGA, not HD
  std::vector<cricket::VideoFormat> supported_formats;
  supported_formats.push_back(cricket::VideoFormat(1280, 720,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(15), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 400,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 360,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  std::vector<cricket::VideoFormat> required_formats;
  required_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_ANY));
  required_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(20), cricket::FOURCC_ANY));
  required_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(10), cricket::FOURCC_ANY));
  cricket::VideoFormat best;

  // expect 30 fps to choose 15 fps format
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[0], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(15), best.interval);

  // expect 20 fps to choose 15 fps format
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[1], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(15), best.interval);

  // expect 10 fps to choose 15 fps format but set fps to 10
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[2], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(10), best.interval);

  // We have VGA 60 fps and 15 fps.  Choose best fps.
  supported_formats.clear();
  supported_formats.push_back(cricket::VideoFormat(1280, 720,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(60), cricket::FOURCC_MJPG));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(15), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 400,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 360,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  // expect 30 fps to choose 60 fps format, but will set best fps to 30
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[0], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(30), best.interval);

  // expect 20 fps to choose 60 fps format, but will set best fps to 20
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[1], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(20), best.interval);

  // expect 10 fps to choose 10 fps
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[2], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(480, best.height);
  EXPECT_EQ(cricket::VideoFormat::FpsToInterval(10), best.interval);
}

TEST_F(VideoCapturerTest, TestRequest16x10_9) {
  std::vector<cricket::VideoFormat> supported_formats;
  // We do not support HD, expect 4x3 for 4x3, 16x10, and 16x9 requests.
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 400,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 360,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  std::vector<cricket::VideoFormat> required_formats = supported_formats;
  cricket::VideoFormat best;
  // Expect 4x3, 16x10, and 16x9 requests are respected.
  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(required_formats[i].width, best.width);
    EXPECT_EQ(required_formats[i].height, best.height);
  }

  // We do not support 16x9 HD, expect 4x3 for 4x3, 16x10, and 16x9 requests.
  supported_formats.clear();
  supported_formats.push_back(cricket::VideoFormat(960, 720,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 400,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 360,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  // Expect 4x3, 16x10, and 16x9 requests are respected.
  for (size_t i = 0; i < required_formats.size(); ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(required_formats[i].width, best.width);
    EXPECT_EQ(required_formats[i].height, best.height);
  }

  // We support 16x9HD, Expect 4x3, 16x10, and 16x9 requests are respected.
  supported_formats.clear();
  supported_formats.push_back(cricket::VideoFormat(1280, 720,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 480,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 400,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  supported_formats.push_back(cricket::VideoFormat(640, 360,
      cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
  capturer_.ResetSupportedFormats(supported_formats);

  // Expect 4x3 for 4x3 and 16x10 requests.
  for (size_t i = 0; i < required_formats.size() - 1; ++i) {
    EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[i], &best));
    EXPECT_EQ(required_formats[i].width, best.width);
    EXPECT_EQ(required_formats[i].height, best.height);
  }

  // Expect 16x9 for 16x9 request.
  EXPECT_TRUE(capturer_.GetBestCaptureFormat(required_formats[2], &best));
  EXPECT_EQ(640, best.width);
  EXPECT_EQ(360, best.height);
}
