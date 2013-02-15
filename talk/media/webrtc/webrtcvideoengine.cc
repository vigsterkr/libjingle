/*
 * libjingle
 * Copyright 2004 Google Inc.
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

#ifdef HAVE_WEBRTC_VIDEO
#include "talk/media/webrtc/webrtcvideoengine.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include "talk/base/basictypes.h"
#include "talk/base/buffer.h"
#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/base/timeutils.h"
#include "talk/media/base/rtputils.h"
#include "talk/media/base/streamparams.h"
#include "talk/media/base/videorenderer.h"
#include "talk/media/devices/filevideocapturer.h"
#include "talk/media/webrtc/webrtcpassthroughrender.h"
#include "talk/media/webrtc/webrtcvideocapturer.h"
#include "talk/media/webrtc/webrtcvideoframe.h"
#include "talk/media/webrtc/webrtcvie.h"
#include "talk/media/webrtc/webrtcvoe.h"
#include "talk/media/webrtc/webrtcvoiceengine.h"


namespace cricket {


static const int kDefaultLogSeverity = talk_base::LS_WARNING;

static const int kMinVideoBitrate = 50;
static const int kStartVideoBitrate = 300;
static const int kMaxVideoBitrate = 2000;
static const int kDefaultConferenceModeMaxVideoBitrate = 500;

static const int kVideoMtu = 1200;

static const int kVideoRtpBufferSize = 65536;

static const char kVp8PayloadName[] = "VP8";
static const char kRedPayloadName[] = "red";
static const char kFecPayloadName[] = "ulpfec";

static const int kDefaultNumberOfTemporalLayers = 1;  // 1:1

static void LogMultiline(talk_base::LoggingSeverity sev, char* text) {
  const char* delim = "\r\n";
  // TODO(fbarchard): Fix strtok lint warning.
  for (char* tok = strtok(text, delim); tok; tok = strtok(NULL, delim)) {
    LOG_V(sev) << tok;
  }
}

static const bool kRembNotSending = false;
static const bool kRembSending = true;
// static const bool kRembNotReceiving = false;  // Not used for now.
static const bool kRembReceiving = true;

// Extension header for RTP timestamp offset, see RFC 5450 for details:
// http://tools.ietf.org/html/rfc5450
static const char kRtpTimestampOffsetHeaderExtension[] =
    "urn:ietf:params:rtp-hdrext:toffset";

struct FlushBlackFrameData : public talk_base::MessageData {
  FlushBlackFrameData(uint32 s, int64 t) : ssrc(s), timestamp(t) {
  }
  uint32 ssrc;
  int64 timestamp;
};

class WebRtcRenderAdapter : public webrtc::ExternalRenderer {
 public:
  explicit WebRtcRenderAdapter(VideoRenderer* renderer)
      : renderer_(renderer), width_(0), height_(0), watermark_enabled_(false) {
  }
  virtual ~WebRtcRenderAdapter() {
  }
  void set_watermark_enabled(bool enable) {
    talk_base::CritScope cs(&crit_);
    watermark_enabled_ = enable;
  }
  void SetRenderer(VideoRenderer* renderer) {
    talk_base::CritScope cs(&crit_);
    renderer_ = renderer;
    // FrameSizeChange may have already been called when renderer was not set.
    // If so we should call SetSize here.
    // TODO(ronghuawu): Add unit test for this case. Didn't do it now
    // because the WebRtcRenderAdapter is currently hiding in cc file. No
    // good way to get access to it from the unit test.
    if (width_ > 0 && height_ > 0 && renderer_ != NULL) {
      if (!renderer_->SetSize(width_, height_, 0)) {
        LOG(LS_ERROR)
            << "WebRtcRenderAdapter SetRenderer failed to SetSize to: "
            << width_ << "x" << height_;
      }
    }
  }
  // Implementation of webrtc::ExternalRenderer.
  virtual int FrameSizeChange(unsigned int width, unsigned int height,
                              unsigned int /*number_of_streams*/) {
    talk_base::CritScope cs(&crit_);
    width_ = width;
    height_ = height;
    LOG(LS_INFO) << "WebRtcRenderAdapter frame size changed to: "
                 << width << "x" << height;
    if (renderer_ == NULL) {
      LOG(LS_VERBOSE) << "WebRtcRenderAdapter the renderer has not been set. "
                      << "SetSize will be called later in SetRenderer.";
      return 0;
    }
    return renderer_->SetSize(width_, height_, 0) ? 0 : -1;
  }
  virtual int DeliverFrame(unsigned char* buffer, int buffer_size,
                           uint32_t time_stamp, int64_t render_time) {
    talk_base::CritScope cs(&crit_);
    frame_rate_tracker_.Update(1);
    if (renderer_ == NULL) {
      return 0;
    }
    WebRtcVideoFrame video_frame;
    // Convert 90K rtp timestamp to ns timestamp.
    int64 rtp_time_stamp_in_ns = (time_stamp / 90) *
        talk_base::kNumNanosecsPerMillisec;
    // Convert milisecond render time to ns timestamp.
    int64 render_time_stamp_in_ns = render_time *
        talk_base::kNumNanosecsPerMillisec;
    // Send the rtp timestamp to renderer as the VideoFrame timestamp.
    // and the render timestamp as the VideoFrame elapsed_time.
    video_frame.Attach(buffer, buffer_size, width_, height_,
                       1, 1, render_time_stamp_in_ns,
                       rtp_time_stamp_in_ns, 0);


    // Sanity check on decoded frame size.
    if (buffer_size != static_cast<int>(VideoFrame::SizeOf(width_, height_))) {
      LOG(LS_WARNING) << "WebRtcRenderAdapter received a strange frame size: "
                      << buffer_size;
    }

    int ret = renderer_->RenderFrame(&video_frame) ? 0 : -1;
    uint8* buffer_temp;
    size_t buffer_size_temp;
    video_frame.Detach(&buffer_temp, &buffer_size_temp);
    return ret;
  }

  unsigned int width() {
    talk_base::CritScope cs(&crit_);
    return width_;
  }
  unsigned int height() {
    talk_base::CritScope cs(&crit_);
    return height_;
  }
  int framerate() {
    talk_base::CritScope cs(&crit_);
    return frame_rate_tracker_.units_second();
  }
  VideoRenderer* renderer() {
    talk_base::CritScope cs(&crit_);
    return renderer_;
  }

 private:
  talk_base::CriticalSection crit_;
  VideoRenderer* renderer_;
  unsigned int width_;
  unsigned int height_;
  talk_base::RateTracker frame_rate_tracker_;
  bool watermark_enabled_;
};

class WebRtcDecoderObserver : public webrtc::ViEDecoderObserver {
 public:
  explicit WebRtcDecoderObserver(int video_channel)
       : video_channel_(video_channel),
         framerate_(0),
         bitrate_(0),
         firs_requested_(0) {
  }

  // virtual functions from VieDecoderObserver.
  virtual void IncomingCodecChanged(const int videoChannel,
                                    const webrtc::VideoCodec& videoCodec) {}
  virtual void IncomingRate(const int videoChannel,
                            const unsigned int framerate,
                            const unsigned int bitrate) {
    ASSERT(video_channel_ == videoChannel);
    framerate_ = framerate;
    bitrate_ = bitrate;
  }
  virtual void RequestNewKeyFrame(const int videoChannel) {
    ASSERT(video_channel_ == videoChannel);
    ++firs_requested_;
  }

  int framerate() const { return framerate_; }
  int bitrate() const { return bitrate_; }
  int firs_requested() const { return firs_requested_; }

 private:
  int video_channel_;
  int framerate_;
  int bitrate_;
  int firs_requested_;
};

class WebRtcEncoderObserver : public webrtc::ViEEncoderObserver {
 public:
  explicit WebRtcEncoderObserver(int video_channel)
      : video_channel_(video_channel),
        framerate_(0),
        bitrate_(0) {
  }

  // virtual functions from VieEncoderObserver.
  virtual void OutgoingRate(const int videoChannel,
                            const unsigned int framerate,
                            const unsigned int bitrate) {
    ASSERT(video_channel_ == videoChannel);
    framerate_ = framerate;
    bitrate_ = bitrate;
  }

  int framerate() const { return framerate_; }
  int bitrate() const { return bitrate_; }

 private:
  int video_channel_;
  int framerate_;
  int bitrate_;
};

class WebRtcLocalStreamInfo {
 public:
  int width() {
    talk_base::CritScope cs(&crit_);
    return width_;
  }
  int height() {
    talk_base::CritScope cs(&crit_);
    return height_;
  }
  int framerate() {
    talk_base::CritScope cs(&crit_);
    return rate_tracker_.units_second();
  }

  void UpdateFrame(int width, int height) {
    talk_base::CritScope cs(&crit_);
    width_ = width;
    height_ = height;
    rate_tracker_.Update(1);
  }

 private:
  talk_base::CriticalSection crit_;
  unsigned int width_;
  unsigned int height_;
  talk_base::RateTracker rate_tracker_;
};

// WebRtcVideoChannelRecvInfo is a container class with members such as renderer
// and a decoder observer that is used by receive channels.
// It must exist as long as the receive channel is connected to renderer or a
// decoder observer in this class and methods in the class should only be called
// from the worker thread.
class WebRtcVideoChannelRecvInfo  {
 public:
  explicit WebRtcVideoChannelRecvInfo(int channel_id)
      : channel_id_(channel_id),
        render_adapter_(NULL),
        decoder_observer_(channel_id) {
  }
  int channel_id() { return channel_id_; }
  void SetRenderer(VideoRenderer* renderer) {
    render_adapter_.SetRenderer(renderer);
  }
  WebRtcRenderAdapter* render_adapter() { return &render_adapter_; }
  WebRtcDecoderObserver* decoder_observer() { return &decoder_observer_; }

 private:
  int channel_id_;  // Webrtc video channel number.
  // Renderer for this channel.
  WebRtcRenderAdapter render_adapter_;
  WebRtcDecoderObserver decoder_observer_;
};

class WebRtcVideoChannelSendInfo  {
 public:
  WebRtcVideoChannelSendInfo(int channel_id, int capture_id,
                             webrtc::ViEExternalCapture* external_capture)
      : channel_id_(channel_id),
        capture_id_(capture_id),
        sending_(false),
        muted_(false),
        video_capturer_(NULL),
        encoder_observer_(channel_id),
        external_capture_(external_capture),
        capturer_updated_(false),
        reference_timestamp_(0),
        timestamp_delta_(0),
        interval_(0),
        last_frame_width_(0),
        last_frame_height_(0),
        last_frame_elapsed_time_(0),
        last_frame_time_stamp_(0) {
  }

  int channel_id() const { return channel_id_; }
  int capture_id() const { return capture_id_; }
  void set_sending(bool sending) { sending_ = sending; }
  bool sending() const { return sending_; }
  void set_muted(bool on) { muted_ = on; }
  bool muted() {return muted_; }

  WebRtcEncoderObserver* encoder_observer() { return &encoder_observer_; }
  webrtc::ViEExternalCapture* external_capture() { return external_capture_; }
  const VideoFormat& video_format() const {
    return video_format_;
  }
  void set_video_format(const VideoFormat& video_format) {
    video_format_ = video_format;
    if (video_format_ != cricket::VideoFormat()) {
      interval_ = video_format_.interval;
    }
  }
  void set_interval(int64 interval) {
    if (video_format() == cricket::VideoFormat()) {
      interval_ = interval;
    }
  }

  StreamParams* stream_params() { return stream_params_.get(); }
  void set_stream_params(const StreamParams& sp) {
    stream_params_.reset(new StreamParams(sp));
  }
  void ClearStreamParams() { stream_params_.reset(); }
  bool has_ssrc(uint32 local_ssrc) const {
    return !stream_params_ ? false :
        stream_params_->has_ssrc(local_ssrc);
  }
  WebRtcLocalStreamInfo* local_stream_info() {
    return &local_stream_info_;
  }
  VideoCapturer* video_capturer() {
    return video_capturer_;
  }
  void set_video_capturer(VideoCapturer* video_capturer) {
    if (video_capturer == video_capturer_) {
      return;
    }
    capturer_updated_ = true;
    video_capturer_ = video_capturer;
  }
  int64 last_frame_time_stamp() {
    talk_base::CritScope cs(&crit_);
    return last_frame_time_stamp_;
  }
  void GetLastFrameInfo(size_t* last_frame_width,
                        size_t* last_frame_height,
                        int64* last_frame_elapsed_time) const {
    talk_base::CritScope cs(&crit_);
    *last_frame_width = last_frame_width_;
    *last_frame_height = last_frame_height_;
    *last_frame_elapsed_time = last_frame_elapsed_time_;
  }
  void RecalculateTimestamp(VideoFrame* frame, WebRtc_Word64* clocks) {
    ASSERT(frame != NULL);
    ASSERT(clocks != NULL);
    if (!reference_timestamp_) {
      // ViE will use the first received timestamp as reference. Do that here
      // too.
      reference_timestamp_ = frame->GetTimeStamp();
      ASSERT(!timestamp_delta_);
    }
    // TODO(hellner): compensate for wrapparound.
    if (capturer_updated_) {
      capturer_updated_ = false;
      // A new capturer has been added. The new and old capturer will most
      // likely have a discrepancy in timestamp. Compensate for this.
      timestamp_delta_ = reference_timestamp_ - frame->GetTimeStamp();
    }
    // Update the reference timestamp as a new frame has arrived.
    reference_timestamp_ = frame->GetTimeStamp() + timestamp_delta_;
    frame->SetTimeStamp(reference_timestamp_);

    // It's better to let webrtc estimate the timestamp than trying to do it
    // here since webrtc knows better how it wants the timestamp to be
    // estimated.
    // TODO(hellner): revisit setting *clocks to 0 when BWE is not dependent on
    // RTP timestamp.
    *clocks = 0;
    // Calculate next expected timestamp in case next frame is provided by a new
    // capturer.
    reference_timestamp_ += interval_;
  }
  void ProcessFrame(const VideoFrame& original_frame, bool mute,
                    VideoFrame** processed_frame, WebRtc_Word64* clocks) {
    if (!mute) {
      *processed_frame = original_frame.Copy();
    } else {
      WebRtcVideoFrame* black_frame = new WebRtcVideoFrame();
      black_frame->InitToBlack(original_frame.GetWidth(),
                               original_frame.GetHeight(), 1, 1,
                               original_frame.GetElapsedTime(),
                               original_frame.GetTimeStamp());
      *processed_frame = black_frame;
    }

    RecalculateTimestamp(*processed_frame, clocks);
    {
      talk_base::CritScope cs(&crit_);
      last_frame_width_ = (*processed_frame)->GetWidth();
      last_frame_height_ = (*processed_frame)->GetHeight();
      last_frame_elapsed_time_ = (*processed_frame)->GetElapsedTime();
      last_frame_time_stamp_ = (*processed_frame)->GetTimeStamp();
    }
  }

 private:
  int channel_id_;
  int capture_id_;
  bool sending_;
  bool muted_;
  VideoCapturer* video_capturer_;
  WebRtcEncoderObserver encoder_observer_;
  webrtc::ViEExternalCapture* external_capture_;

  VideoFormat video_format_;

  talk_base::scoped_ptr<StreamParams> stream_params_;

  WebRtcLocalStreamInfo local_stream_info_;

  bool capturer_updated_;

  // To compensate for timestamp jumps when switching capturers.
  int64 reference_timestamp_;  // The timestamp that ViE is expecting.
  int64 timestamp_delta_;  // The offset in timestamp between the capturers
                           // capturers timestamp and |reference_timestamp_|.
  int64 interval_;

  // Used for black frame generation.
  // |crit_| protects |last_frame_*| from concurrent access as they are
  // written to by the capturer thread but also read by the black frame thread.
  mutable talk_base::CriticalSection crit_;
  size_t last_frame_width_;
  size_t last_frame_height_;
  int64 last_frame_elapsed_time_;
  int64 last_frame_time_stamp_;
};

const WebRtcVideoEngine::VideoCodecPref
    WebRtcVideoEngine::kVideoCodecPrefs[] = {
    {kVp8PayloadName, 100, 0},
    {kRedPayloadName, 116, 1},
    {kFecPayloadName, 117, 2},
};

// The formats are sorted by the descending order of width. We use the order to
// find the next format for CPU and bandwidth adaptation.
const VideoFormatPod WebRtcVideoEngine::kVideoFormats[] = {
  {1280, 800, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {1280, 720, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {960, 600, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {960, 540, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {640, 400, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {640, 360, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {640, 480, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {480, 300, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {480, 270, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {480, 360, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {320, 200, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {320, 180, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {320, 240, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {240, 150, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {240, 135, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {240, 180, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {160, 100, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {160, 90, FPS_TO_INTERVAL(30), FOURCC_ANY},
  {160, 120, FPS_TO_INTERVAL(30), FOURCC_ANY},
};

const VideoFormatPod WebRtcVideoEngine::kDefaultVideoFormat =
  {640, 400, FPS_TO_INTERVAL(30), FOURCC_ANY};

static void UpdateVideoCodec(const cricket::VideoFormat& video_format,
                             webrtc::VideoCodec* target_codec) {
  if ((target_codec == NULL) || (video_format == cricket::VideoFormat())) {
    return;
  }
  target_codec->width = video_format.width;
  target_codec->height = video_format.height;
  target_codec->maxFramerate = cricket::VideoFormat::IntervalToFps(
      video_format.interval);
}

WebRtcVideoEngine::WebRtcVideoEngine() {
  Construct(new ViEWrapper(), new ViETraceWrapper(), NULL);
}

WebRtcVideoEngine::WebRtcVideoEngine(WebRtcVoiceEngine* voice_engine,
                                     ViEWrapper* vie_wrapper) {
  Construct(vie_wrapper, new ViETraceWrapper(), voice_engine);
}

WebRtcVideoEngine::WebRtcVideoEngine(WebRtcVoiceEngine* voice_engine,
                                     ViEWrapper* vie_wrapper,
                                     ViETraceWrapper* tracing) {
  Construct(vie_wrapper, tracing, voice_engine);
}

void WebRtcVideoEngine::Construct(ViEWrapper* vie_wrapper,
                                  ViETraceWrapper* tracing,
                                  WebRtcVoiceEngine* voice_engine) {
  LOG(LS_INFO) << "WebRtcVideoEngine::WebRtcVideoEngine";
  vie_wrapper_.reset(vie_wrapper);
  vie_wrapper_base_initialized_ = false;
  tracing_.reset(tracing);
  voice_engine_ = voice_engine;
  initialized_ = false;
  log_level_ = kDefaultLogSeverity;
  render_module_.reset(new WebRtcPassthroughRender());
  local_renderer_w_ = local_renderer_h_ = 0;
  local_renderer_ = NULL;
  video_capturer_ = NULL;
  frame_listeners_ = 0;
  capture_started_ = false;

  ApplyLogging("");
  if (tracing_->SetTraceCallback(this) != 0) {
    LOG_RTCERR1(SetTraceCallback, this);
  }

  // Set default quality levels for our supported codecs. We override them here
  // if we know your cpu performance is low, and they can be updated explicitly
  // by calling SetDefaultCodec.  For example by a flute preference setting, or
  // by the server with a jec in response to our reported system info.
  VideoCodec max_codec(kVideoCodecPrefs[0].payload_type,
                       kVideoCodecPrefs[0].name,
                       kDefaultVideoFormat.width,
                       kDefaultVideoFormat.height,
                       VideoFormat::IntervalToFps(kDefaultVideoFormat.interval),
                       0);
  if (!SetDefaultCodec(max_codec)) {
    LOG(LS_ERROR) << "Failed to initialize list of supported codec types";
  }

}

WebRtcVideoEngine::~WebRtcVideoEngine() {
  ClearCapturer();
  LOG(LS_INFO) << "WebRtcVideoEngine::~WebRtcVideoEngine";
  if (initialized_) {
    Terminate();
  }
  tracing_->SetTraceCallback(NULL);
  // Test to see if the media processor was deregistered properly.
  ASSERT(SignalMediaFrame.is_empty());
}

bool WebRtcVideoEngine::Init() {
  LOG(LS_INFO) << "WebRtcVideoEngine::Init";
  bool result = InitVideoEngine();
  if (result) {
    LOG(LS_INFO) << "VideoEngine Init done";
  } else {
    LOG(LS_ERROR) << "VideoEngine Init failed, releasing";
    Terminate();
  }
  return result;
}

bool WebRtcVideoEngine::InitVideoEngine() {
  LOG(LS_INFO) << "WebRtcVideoEngine::InitVideoEngine";

  // Init WebRTC VideoEngine.
  if (!vie_wrapper_base_initialized_) {
    if (vie_wrapper_->base()->Init() != 0) {
      LOG_RTCERR0(Init);
      return false;
    }
    vie_wrapper_base_initialized_ = true;
  }

  // Log the VoiceEngine version info.
  char buffer[1024] = "";
  if (vie_wrapper_->base()->GetVersion(buffer) != 0) {
    LOG_RTCERR0(GetVersion);
    return false;
  }

  LOG(LS_INFO) << "WebRtc VideoEngine Version:";
  LogMultiline(talk_base::LS_INFO, buffer);

  // Hook up to VoiceEngine for sync purposes, if supplied.
  if (!voice_engine_) {
    LOG(LS_WARNING) << "NULL voice engine";
  } else if ((vie_wrapper_->base()->SetVoiceEngine(
      voice_engine_->voe()->engine())) != 0) {
    LOG_RTCERR0(SetVoiceEngine);
    return false;
  }

  // Register our custom render module.
  if (vie_wrapper_->render()->RegisterVideoRenderModule(
      *render_module_.get()) != 0) {
    LOG_RTCERR0(RegisterVideoRenderModule);
    return false;
  }


  initialized_ = true;
  return true;
}

void WebRtcVideoEngine::Terminate() {
  LOG(LS_INFO) << "WebRtcVideoEngine::Terminate";
  initialized_ = false;
  SetCapture(false);

  if (vie_wrapper_->render()->DeRegisterVideoRenderModule(
      *render_module_.get()) != 0) {
    LOG_RTCERR0(DeRegisterVideoRenderModule);
  }

  if (vie_wrapper_->base()->SetVoiceEngine(NULL) != 0) {
    LOG_RTCERR0(SetVoiceEngine);
  }
}

int WebRtcVideoEngine::GetCapabilities() {
  return VIDEO_RECV | VIDEO_SEND;
}

bool WebRtcVideoEngine::SetOptions(int options) {
  return true;
}

bool WebRtcVideoEngine::SetDefaultEncoderConfig(
    const VideoEncoderConfig& config) {
  return SetDefaultCodec(config.max_codec);
}

// SetDefaultCodec may be called while the capturer is running. For example, a
// test call is started in a page with QVGA default codec, and then a real call
// is started in another page with VGA default codec. This is the corner case
// and happens only when a session is started. We ignore this case currently.
bool WebRtcVideoEngine::SetDefaultCodec(const VideoCodec& codec) {
  if (!RebuildCodecList(codec)) {
    LOG(LS_WARNING) << "Failed to RebuildCodecList";
    return false;
  }

  default_codec_format_ = VideoFormat(
      video_codecs_[0].width,
      video_codecs_[0].height,
      VideoFormat::FpsToInterval(video_codecs_[0].framerate),
      FOURCC_ANY);
  return true;
}

WebRtcVideoMediaChannel* WebRtcVideoEngine::CreateChannel(
    VoiceMediaChannel* voice_channel) {
  WebRtcVideoMediaChannel* channel =
      new WebRtcVideoMediaChannel(this, voice_channel);
  if (!channel->Init()) {
    delete channel;
    channel = NULL;
  }
  return channel;
}

bool WebRtcVideoEngine::SetVideoCapturer(VideoCapturer* capturer) {
  return SetCapturer(capturer);
}

VideoCapturer* WebRtcVideoEngine::GetVideoCapturer() const {
  return video_capturer_;
}

bool WebRtcVideoEngine::SetLocalRenderer(VideoRenderer* renderer) {
  local_renderer_w_ = local_renderer_h_ = 0;
  local_renderer_ = renderer;
  return true;
}

bool WebRtcVideoEngine::SetCapture(bool capture) {
  bool old_capture = capture_started_;
  capture_started_ = capture;
  CaptureState result = UpdateCapturingState();
  if (result == CS_FAILED || result == CS_NO_DEVICE) {
    capture_started_ = old_capture;
    return false;
  }
  return true;
}

CaptureState WebRtcVideoEngine::UpdateCapturingState() {
  bool capture = capture_started_ && frame_listeners_;
  CaptureState result = CS_RUNNING;
  if (!IsCapturing() && capture) {  // Start capturing.
    if (video_capturer_ == NULL) {
      return CS_NO_DEVICE;
    }

    VideoFormat capture_format;
    if (!video_capturer_->GetBestCaptureFormat(default_codec_format_,
                                               &capture_format)) {
      LOG(LS_WARNING) << "Unsupported format:"
                      << " width=" << default_codec_format_.width
                      << " height=" << default_codec_format_.height
                      << ". Supported formats are:";
      const std::vector<VideoFormat>* formats =
          video_capturer_->GetSupportedFormats();
      if (formats) {
        for (std::vector<VideoFormat>::const_iterator i = formats->begin();
             i != formats->end(); ++i) {
          const VideoFormat& format = *i;
          LOG(LS_WARNING) << "  " << GetFourccName(format.fourcc) << ":"
                          << format.width << "x" << format.height << "x"
                          << format.framerate();
        }
      }
      return CS_FAILED;
    }

    // Start the video capturer.
    result = video_capturer_->Start(capture_format);
    if (CS_RUNNING != result && CS_STARTING != result) {
      LOG(LS_ERROR) << "Failed to start the video capturer";
      return result;
    }
  } else if (IsCapturing() && !capture) {  // Stop capturing.
    video_capturer_->Stop();
    result = CS_STOPPED;
  }

  return result;
}

bool WebRtcVideoEngine::IsCapturing() const {
  return (video_capturer_ != NULL) && video_capturer_->IsRunning();
}

void WebRtcVideoEngine::OnFrameCaptured(VideoCapturer* capturer,
                                        const CapturedFrame* frame) {
  // Crop to desired aspect ratio.
  int cropped_width, cropped_height;
  ComputeCrop(default_codec_format_.width, default_codec_format_.height,
              frame->width, abs(frame->height),
              frame->pixel_width, frame->pixel_height,
              frame->rotation, &cropped_width, &cropped_height);

  // This CapturedFrame* will already be in I420. In the future, when
  // WebRtcVideoFrame has support for independent planes, we can just attach
  // to it and update the pointers when cropping.
  WebRtcVideoFrame i420_frame;
  if (!i420_frame.Init(frame, cropped_width, cropped_height)) {
    LOG(LS_ERROR) << "Couldn't convert to I420! "
                  << cropped_width << " x " << cropped_height;
    return;
  }

  // TODO(janahan): This is the trigger point for Tx video processing.
  // Once the capturer refactoring is done, we will move this into the
  // capturer...it's not there right now because that image is in not in the
  // I420 color space.
  // The clients that subscribe will obtain meta info from the frame.
  // When this trigger is switched over to capturer, need to pass in the real
  // ssrc.
  bool drop_frame = false;
  {
    talk_base::CritScope cs(&signal_media_critical_);
    SignalMediaFrame(kDummyVideoSsrc, &i420_frame, &drop_frame);
  }
  if (drop_frame) {
    LOG(LS_VERBOSE) << "Media Effects dropped a frame.";
    return;
  }

  // Send I420 frame to the local renderer.
  if (local_renderer_) {
    if (local_renderer_w_ != static_cast<int>(i420_frame.GetWidth()) ||
        local_renderer_h_ != static_cast<int>(i420_frame.GetHeight())) {
      local_renderer_->SetSize(local_renderer_w_ = i420_frame.GetWidth(),
                               local_renderer_h_ = i420_frame.GetHeight(), 0);
    }
    local_renderer_->RenderFrame(&i420_frame);
  }
  // Send I420 frame to the registered senders.
  talk_base::CritScope cs(&channels_crit_);
  for (VideoChannels::iterator it = channels_.begin();
      it != channels_.end(); ++it) {
    if ((*it)->sending()) (*it)->SendFrame(capturer, &i420_frame);
  }
}

const std::vector<VideoCodec>& WebRtcVideoEngine::codecs() const {
  return video_codecs_;
}

void WebRtcVideoEngine::SetLogging(int min_sev, const char* filter) {
  // if min_sev == -1, we keep the current log level.
  if (min_sev >= 0) {
    log_level_ = min_sev;
  }
  ApplyLogging(filter);
}

int WebRtcVideoEngine::GetLastEngineError() {
  return vie_wrapper_->error();
}

// Checks to see whether we comprehend and could receive a particular codec
bool WebRtcVideoEngine::FindCodec(const VideoCodec& in) {
  for (int i = 0; i < ARRAY_SIZE(kVideoFormats); ++i) {
    const VideoFormat fmt(kVideoFormats[i]);
    if ((in.width == 0 && in.height == 0) ||
        (fmt.width == in.width && fmt.height == in.height)) {
      for (int j = 0; j < ARRAY_SIZE(kVideoCodecPrefs); ++j) {
        VideoCodec codec(kVideoCodecPrefs[j].payload_type,
                         kVideoCodecPrefs[j].name, 0, 0, 0, 0);
        if (codec.Matches(in)) {
          return true;
        }
      }
    }
  }
  return false;
}

// Given the requested codec, returns true if we can send that codec type and
// updates out with the best quality we could send for that codec. If current is
// not empty, we constrain out so that its aspect ratio matches current's.
bool WebRtcVideoEngine::CanSendCodec(const VideoCodec& requested,
                                     const VideoCodec& current,
                                     VideoCodec* out) {
  if (!out) {
    return false;
  }

  std::vector<VideoCodec>::const_iterator local_max;
  for (local_max = video_codecs_.begin();
       local_max < video_codecs_.end();
       ++local_max) {
    // First match codecs by payload type
    if (!requested.Matches(local_max->id, local_max->name)) {
      continue;
    }

    out->id = requested.id;
    out->name = requested.name;
    out->preference = requested.preference;
    out->framerate = talk_base::_min(requested.framerate, local_max->framerate);
    out->width = 0;
    out->height = 0;

    if (0 == requested.width && 0 == requested.height) {
      // Special case with resolution 0. The channel should not send frames.
      return true;
    } else if (0 == requested.width || 0 == requested.height) {
      // 0xn and nx0 are invalid resolutions.
      return false;
    }

    // Pick the best quality that is within their and our bounds and has the
    // correct aspect ratio.
    for (int j = 0; j < ARRAY_SIZE(kVideoFormats); ++j) {
      const VideoFormat format(kVideoFormats[j]);

      // Skip any format that is larger than the local or remote maximums, or
      // smaller than the current best match
      if (format.width > requested.width || format.height > requested.height ||
          format.width > local_max->width ||
          (format.width < out->width && format.height < out->height)) {
        continue;
      }

      bool better = false;

      // Check any further constraints on this prospective format
      if (!out->width || !out->height) {
        // If we don't have any matches yet, this is the best so far.
        better = true;
      } else if (current.width && current.height) {
        // current is set so format must match its ratio exactly.
        better =
            (format.width * current.height == format.height * current.width);
      } else {
        // Prefer closer aspect ratios i.e
        // format.aspect - requested.aspect < out.aspect - requested.aspect
        better = abs(format.width * requested.height * out->height -
                     requested.width * format.height * out->height) <
                 abs(out->width * format.height * requested.height -
                     requested.width * format.height * out->height);
      }

      if (better) {
        out->width = format.width;
        out->height = format.height;
      }
    }
    if (out->width > 0) {
      return true;
    }
  }
  return false;
}

static void ConvertToCricketVideoCodec(
    const webrtc::VideoCodec& in_codec, VideoCodec* out_codec) {
  out_codec->id = in_codec.plType;
  out_codec->name = in_codec.plName;
  out_codec->width = in_codec.width;
  out_codec->height = in_codec.height;
  out_codec->framerate = in_codec.maxFramerate;
}

bool WebRtcVideoEngine::ConvertFromCricketVideoCodec(
    const VideoCodec& in_codec, webrtc::VideoCodec* out_codec) {
  bool found = false;
  int ncodecs = vie_wrapper_->codec()->NumberOfCodecs();
  for (int i = 0; i < ncodecs; ++i) {
    if (vie_wrapper_->codec()->GetCodec(i, *out_codec) == 0 &&
        _stricmp(in_codec.name.c_str(), out_codec->plName) == 0) {
      found = true;
      break;
    }
  }

  if (!found) {
    LOG(LS_ERROR) << "invalid codec type";
    return false;
  }

  if (in_codec.id != 0)
    out_codec->plType = in_codec.id;

  if (in_codec.width != 0)
    out_codec->width = in_codec.width;

  if (in_codec.height != 0)
    out_codec->height = in_codec.height;

  if (in_codec.framerate != 0)
    out_codec->maxFramerate = in_codec.framerate;

  // Init the codec with the default bandwidth options.
  out_codec->minBitrate = kMinVideoBitrate;
  out_codec->startBitrate = kStartVideoBitrate;
  out_codec->maxBitrate = kMaxVideoBitrate;

  return true;
}

void WebRtcVideoEngine::RegisterChannel(WebRtcVideoMediaChannel *channel) {
  talk_base::CritScope cs(&channels_crit_);
  channels_.push_back(channel);
}

void WebRtcVideoEngine::UnregisterChannel(WebRtcVideoMediaChannel *channel) {
  talk_base::CritScope cs(&channels_crit_);
  channels_.erase(std::remove(channels_.begin(), channels_.end(), channel),
                  channels_.end());
}

bool WebRtcVideoEngine::SetVoiceEngine(WebRtcVoiceEngine* voice_engine) {
  if (initialized_) {
    LOG(LS_WARNING) << "SetVoiceEngine can not be called after Init";
    return false;
  }
  voice_engine_ = voice_engine;
  return true;
}

bool WebRtcVideoEngine::EnableTimedRender() {
  if (initialized_) {
    LOG(LS_WARNING) << "EnableTimedRender can not be called after Init";
    return false;
  }
  render_module_.reset(webrtc::VideoRender::CreateVideoRender(0, NULL,
      false, webrtc::kRenderExternal));
  return true;
}

// See https://sites.google.com/a/google.com/wavelet/
//     Home/Magic-Flute--RTC-Engine-/Magic-Flute-Command-Line-Parameters
// for all supported command line setttings.
void WebRtcVideoEngine::ApplyLogging(const std::string& log_filter) {
  int filter = 0;
  switch (log_level_) {
    case talk_base::LS_VERBOSE: filter |= webrtc::kTraceAll;
    case talk_base::LS_INFO: filter |=
        webrtc::kTraceStateInfo | webrtc::kTraceInfo;
    case talk_base::LS_WARNING: filter |=
        webrtc::kTraceWarning | webrtc::kTraceTerseInfo;
    case talk_base::LS_ERROR: filter |=
        webrtc::kTraceError | webrtc::kTraceCritical;
  }
  tracing_->SetTraceFilter(filter);

  // Set WebRTC trace file.
  std::vector<std::string> opts;
  talk_base::tokenize(log_filter, ' ', '"', '"', &opts);
  std::vector<std::string>::iterator tracefile =
      std::find(opts.begin(), opts.end(), "tracefile");
  if (tracefile != opts.end() && ++tracefile != opts.end()) {
    // Write WebRTC debug output (at same loglevel) to file
    if (tracing_->SetTraceFile(tracefile->c_str()) == -1) {
      LOG_RTCERR1(SetTraceFile, *tracefile);
    }
  }
}

// Rebuilds the codec list to be only those that are less intensive
// than the specified codec.
bool WebRtcVideoEngine::RebuildCodecList(const VideoCodec& in_codec) {
  if (!FindCodec(in_codec))
    return false;

  video_codecs_.clear();

  bool found = false;
  for (size_t i = 0; i < ARRAY_SIZE(kVideoCodecPrefs); ++i) {
    const VideoCodecPref& pref(kVideoCodecPrefs[i]);
    if (!found)
      found = (in_codec.name == pref.name);
    if (found) {
      VideoCodec codec(pref.payload_type, pref.name,
                       in_codec.width, in_codec.height, in_codec.framerate,
                       ARRAY_SIZE(kVideoCodecPrefs) - i);
      video_codecs_.push_back(codec);
    }
  }
  ASSERT(found);
  return true;
}

bool WebRtcVideoEngine::SetCapturer(VideoCapturer* capturer) {
  if (capturer == NULL) {
    // Stop capturing before clearing the capturer.
    if (!SetCapture(false)) {
      LOG(LS_WARNING) << "Camera failed to stop";
      return false;
    }
    ClearCapturer();
    return true;
  }
  // Hook up signals and install the supplied capturer.
  SignalCaptureStateChange.repeat(capturer->SignalStateChange);
  capturer->SignalFrameCaptured.connect(this,
      &WebRtcVideoEngine::OnFrameCaptured);
  ClearCapturer();
  video_capturer_ = capturer;
  // Possibly restart the capturer if it is supposed to be running.
  CaptureState result = UpdateCapturingState();
  if (result == CS_FAILED || result == CS_NO_DEVICE) {
    LOG(LS_WARNING) << "Camera failed to restart";
    return false;
  }
  return true;
}

// Ignore spammy trace messages, mostly from the stats API when we haven't
// gotten RTCP info yet from the remote side.
bool WebRtcVideoEngine::ShouldIgnoreTrace(const std::string& trace) {
  static const char* const kTracesToIgnore[] = {
    NULL
  };
  for (const char* const* p = kTracesToIgnore; *p; ++p) {
    if (trace.find(*p) == 0) {
      return true;
    }
  }
  return false;
}

int WebRtcVideoEngine::GetNumOfChannels() {
  talk_base::CritScope cs(&channels_crit_);
  return channels_.size();
}

void WebRtcVideoEngine::IncrementFrameListeners() {
  if (++frame_listeners_ == 1) {
    UpdateCapturingState();
  }
  // In the unlikely event of wrapparound.
  ASSERT(frame_listeners_ >= 0);
}

void WebRtcVideoEngine::DecrementFrameListeners() {
  if (--frame_listeners_ == 0) {
    UpdateCapturingState();
  }
  ASSERT(frame_listeners_ >= 0);
}

void WebRtcVideoEngine::Print(webrtc::TraceLevel level, const char* trace,
                              int length) {
  talk_base::LoggingSeverity sev = talk_base::LS_VERBOSE;
  if (level == webrtc::kTraceError || level == webrtc::kTraceCritical)
    sev = talk_base::LS_ERROR;
  else if (level == webrtc::kTraceWarning || level == webrtc::kTraceTerseInfo)
    sev = talk_base::LS_WARNING;
  else if (level == webrtc::kTraceStateInfo || level == webrtc::kTraceInfo)
    sev = talk_base::LS_INFO;

  if (sev >= log_level_) {
    if (level == webrtc::kTraceTerseInfo) {
      // Actually use LS_INFO for TerseInfo.
      sev = talk_base::LS_INFO;
    }
    // Skip past boilerplate prefix text
    if (length < 72) {
      std::string msg(trace, length);
      LOG(LS_ERROR) << "Malformed webrtc log message: ";
      LOG_V(sev) << msg;
    } else {
      std::string msg(trace + 71, length - 72);
      if (!ShouldIgnoreTrace(msg) &&
          (!voice_engine_ || !voice_engine_->ShouldIgnoreTrace(msg))) {
        LOG_V(sev) << "webrtc: " << msg;
      }
    }
  }
}

bool WebRtcVideoEngine::RegisterProcessor(
    VideoProcessor* video_processor) {
  talk_base::CritScope cs(&signal_media_critical_);
  SignalMediaFrame.connect(video_processor,
                           &VideoProcessor::OnFrame);
  return true;
}
bool WebRtcVideoEngine::UnregisterProcessor(
    VideoProcessor* video_processor) {
  talk_base::CritScope cs(&signal_media_critical_);
  SignalMediaFrame.disconnect(video_processor);
  return true;
}

void WebRtcVideoEngine::ClearCapturer() {
  video_capturer_ = NULL;
}

// WebRtcVideoMediaChannel

WebRtcVideoMediaChannel::WebRtcVideoMediaChannel(
    WebRtcVideoEngine* engine,
    VoiceMediaChannel* channel)
    : engine_(engine),
      voice_channel_(channel),
      vie_channel_(-1),
      render_started_(false),
      first_receive_ssrc_(0),
      send_red_type_(-1),
      send_fec_type_(-1),
      send_min_bitrate_(kMinVideoBitrate),
      send_start_bitrate_(kStartVideoBitrate),
      send_max_bitrate_(kMaxVideoBitrate),
      sending_(false),
      ratio_w_(0),
      ratio_h_(0) {
  engine->RegisterChannel(this);
}

bool WebRtcVideoMediaChannel::Init() {
  const uint32 ssrc_key = 0;
  return CreateChannel(ssrc_key, MD_SENDRECV, &vie_channel_);
}

WebRtcVideoMediaChannel::~WebRtcVideoMediaChannel() {
  const bool send = false;
  SetSend(send);
  const bool render = false;
  SetRender(render);

  while (!send_channels_.empty()) {
    if (!DeleteSendChannel(send_channels_.begin()->first)) {
      LOG(LS_ERROR) << "Unable to delete channel with ssrc key "
                    << send_channels_.begin()->first;
      ASSERT(false);
      break;
    }
  }

  // Remove all receive streams and the default channel.
  while (!recv_channels_.empty()) {
    RemoveRecvStream(recv_channels_.begin()->first);
  }

  // Unregister the channel from the engine.
  engine()->UnregisterChannel(this);
}

bool WebRtcVideoMediaChannel::SetRecvCodecs(
    const std::vector<VideoCodec>& codecs) {
  receive_codecs_.clear();
  for (std::vector<VideoCodec>::const_iterator iter = codecs.begin();
      iter != codecs.end(); ++iter) {
    if (engine()->FindCodec(*iter)) {
      webrtc::VideoCodec wcodec;
      if (engine()->ConvertFromCricketVideoCodec(*iter, &wcodec)) {
        receive_codecs_.push_back(wcodec);
      }
    } else {
      LOG(LS_INFO) << "Unknown codec " << iter->name;
      return false;
    }
  }

  for (RecvChannelMap::iterator it = recv_channels_.begin();
      it != recv_channels_.end(); ++it) {
    if (!SetReceiveCodecs(it->second->channel_id()))
      return false;
  }
  return true;
}

bool WebRtcVideoMediaChannel::SetSendCodecs(
    const std::vector<VideoCodec>& codecs) {
  // Match with local video codec list.
  std::vector<webrtc::VideoCodec> send_codecs;
  VideoCodec checked_codec;
  VideoCodec current;  // defaults to 0x0
  if (sending_) {
    ConvertToCricketVideoCodec(*send_codec_, &current);
  }
  for (std::vector<VideoCodec>::const_iterator iter = codecs.begin();
      iter != codecs.end(); ++iter) {
    if (_stricmp(iter->name.c_str(), kRedPayloadName) == 0) {
      send_red_type_ = iter->id;
    } else if (_stricmp(iter->name.c_str(), kFecPayloadName) == 0) {
      send_fec_type_ = iter->id;
    } else if (engine()->CanSendCodec(*iter, current, &checked_codec)) {
      webrtc::VideoCodec wcodec;
      if (engine()->ConvertFromCricketVideoCodec(checked_codec, &wcodec)) {
        send_codecs.push_back(wcodec);
      }
    } else {
      LOG(LS_WARNING) << "Unknown codec " << iter->name;
    }
  }

  // Fail if we don't have a match.
  if (send_codecs.empty()) {
    LOG(LS_WARNING) << "No matching codecs available";
    return false;
  }

  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    int channel_id = send_channel->channel_id();
    // Configure video protection.
    if (!SetNackFec(channel_id, send_red_type_, send_fec_type_)) {
      return false;
    }
  }

  // Select the first matched codec.
  webrtc::VideoCodec& codec(send_codecs[0]);

  if (!SetSendCodec(
          codec, send_min_bitrate_, send_start_bitrate_, send_max_bitrate_)) {
    return false;
  }

  LogSendCodecChange("SetSendCodecs()");

  return true;
}

bool WebRtcVideoMediaChannel::GetSendCodec(VideoCodec* send_codec) {
  if (!send_codec_) {
    return false;
  }
  ConvertToCricketVideoCodec(*send_codec_, send_codec);
  return true;
}

bool WebRtcVideoMediaChannel::SetSendStreamFormat(uint32 ssrc,
                                                  const VideoFormat& format) {
  if (!send_codec_) {
    LOG(LS_ERROR) << "The send codec has not been set yet.";
    return false;
  }
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(ssrc);
  if (!send_channel) {
    LOG(LS_ERROR) << "The specified ssrc " << ssrc << " is not in use.";
    return false;
  }

  const VideoFormat old_format = send_channel->video_format();
  // The video format must be called before SetSendCodec since it will use the
  // registered format to set the resolution.
  send_channel->set_video_format(format);

  const bool ret_val = SetSendCodec(send_channel, *send_codec_.get(),
                                    send_min_bitrate_, send_start_bitrate_,
                                    send_max_bitrate_);
  if (!ret_val) {
    // Rollback
    send_channel->set_video_format(old_format);
    return false;
  }
  LogSendCodecChange("SetSendStreamFormat()");
  return true;
}

bool WebRtcVideoMediaChannel::SetRender(bool render) {
  if (render == render_started_) {
    return true;  // no action required
  }

  bool ret = true;
  for (RecvChannelMap::iterator it = recv_channels_.begin();
      it != recv_channels_.end(); ++it) {
    if (render) {
      if (engine()->vie()->render()->StartRender(
          it->second->channel_id()) != 0) {
        LOG_RTCERR1(StartRender, it->second->channel_id());
        ret = false;
      }
    } else {
      if (engine()->vie()->render()->StopRender(
          it->second->channel_id()) != 0) {
        LOG_RTCERR1(StopRender, it->second->channel_id());
        ret = false;
      }
    }
  }
  if (ret) {
    render_started_ = render;
  }

  return ret;
}

bool WebRtcVideoMediaChannel::SetSend(bool send) {
  if (!HasReadySendChannels() && send) {
    LOG(LS_ERROR) << "No stream added";
    return false;
  }
  if (send == sending()) {
    return true;  // No action required.
  }

  if (send) {
    // We've been asked to start sending.
    // SetSendCodecs must have been called already.
    if (!send_codec_) {
      return false;
    }
    // Start send now.
    if (!StartSend()) {
      return false;
    }
  } else {
    // We've been asked to stop sending.
    if (!StopSend()) {
      return false;
    }
  }
  sending_ = send;

  return true;
}

bool WebRtcVideoMediaChannel::AddSendStream(const StreamParams& sp) {
  LOG(LS_INFO) << "AddSendStream " << sp.ToString();

  if (!IsOneSsrcStream(sp)) {
      LOG(LS_ERROR) << "AddSendStream: bad local stream parameters";
      return false;
  }

  uint32 ssrc_key;
  if (!CreateSendChannelKey(sp.first_ssrc(), &ssrc_key)) {
    LOG(LS_ERROR) << "Trying to register duplicate ssrc: " << sp.first_ssrc();
    return false;
  }
  // If the default channel is already used for sending create a new channel
  // otherwise use the default channel for sending.
  int channel_id = -1;
  if (send_channels_[0]->stream_params() == NULL) {
    channel_id = vie_channel_;
  } else {
    if (!CreateChannel(ssrc_key, MD_SEND, &channel_id)) {
      LOG(LS_ERROR) << "AddSendStream: unable to create channel";
      return false;
    }
  }
  WebRtcVideoChannelSendInfo* send_channel = send_channels_[ssrc_key];
  // Set the send (local) SSRC.
  // If there are multiple send SSRCs, we can only set the first one here, and
  // the rest of the SSRC(s) need to be set after SetSendCodec has been called
  // (with a codec requires multiple SSRC(s)).
  if (engine()->vie()->rtp()->SetLocalSSRC(channel_id,
                                           sp.first_ssrc()) != 0) {
    LOG_RTCERR2(SetLocalSSRC, channel_id, sp.first_ssrc());
    return false;
  }

  // Set RTCP CName.
  if (engine()->vie()->rtp()->SetRTCPCName(channel_id,
                                           sp.cname.c_str()) != 0) {
    LOG_RTCERR2(SetRTCPCName, channel_id, sp.cname.c_str());
    return false;
  }

  // At this point the channel's local SSRC has been updated. If the channel is
  // the default channel make sure that all the receive channels are updated as
  // well. Receive channels have to have the same SSRC as the default channel in
  // order to send receiver reports with this SSRC.
  if (IsDefaultChannel(channel_id)) {
    for (RecvChannelMap::const_iterator it = recv_channels_.begin();
         it != recv_channels_.end(); ++it) {
      WebRtcVideoChannelRecvInfo* info = it->second;
      int channel_id = info->channel_id();
      if (engine()->vie()->rtp()->SetLocalSSRC(channel_id,
                                               sp.first_ssrc()) != 0) {
        LOG_RTCERR1(SetLocalSSRC, it->first);
        return false;
      }
    }
  }

  send_channel->set_stream_params(sp);

  // Reset send codec after stream parameters changed.
  if (send_codec_) {
    if (!SetSendCodec(send_channel, *send_codec_, send_min_bitrate_,
                      send_start_bitrate_, send_max_bitrate_)) {
      return false;
    }
    LogSendCodecChange("SetSendStreamFormat()");
  }

  if (sending_) {
    return StartSend(send_channel);
  }
  return true;
}

bool WebRtcVideoMediaChannel::RemoveSendStream(uint32 ssrc) {
  uint32 ssrc_key;
  if (!GetSendChannelKey(ssrc, &ssrc_key)) {
    LOG(LS_WARNING) << "Try to remove stream with ssrc " << ssrc
                    << " which doesn't exist.";
    return false;
  }
  WebRtcVideoChannelSendInfo* send_channel = send_channels_[ssrc_key];
  int channel_id = send_channel->channel_id();
  if (IsDefaultChannel(channel_id) && (send_channel->stream_params() == NULL)) {
    // Default channel will still exist. However, if stream_params() is NULL
    // there is no stream to remove.
    return false;
  }
  if (sending_) {
    StopSend(send_channel);
  }
  // The receive channels depend on the default channel, recycle it instead.
  if (IsDefaultChannel(channel_id)) {
    SetCapturer(GetDefaultChannelSsrc(), NULL);
    send_channel->ClearStreamParams();
  } else {
    return DeleteSendChannel(ssrc_key);
  }
  return true;
}

bool WebRtcVideoMediaChannel::AddRecvStream(const StreamParams& sp) {
  // TODO(zhurunz) Remove this once BWE works properly across different send
  // and receive channels.
  // Reuse default channel for recv stream in 1:1 call.
  if (!InConferenceMode() && first_receive_ssrc_ == 0) {
    LOG(LS_INFO) << "Recv stream " << sp.first_ssrc()
                 << " reuse default channel #"
                 << vie_channel_;
    first_receive_ssrc_ = sp.first_ssrc();
    if (render_started_) {
      if (engine()->vie()->render()->StartRender(vie_channel_) !=0) {
        LOG_RTCERR1(StartRender, vie_channel_);
      }
    }
    return true;
  }

  if (recv_channels_.find(sp.first_ssrc()) != recv_channels_.end() ||
      first_receive_ssrc_ == sp.first_ssrc()) {
    LOG(LS_ERROR) << "Stream already exists";
    return false;
  }

  // TODO(perkj): Implement recv media from multiple SSRCs per stream.
  if (sp.ssrcs.size() != 1) {
    LOG(LS_ERROR) << "WebRtcVideoMediaChannel supports one receiving SSRC per"
                  << " stream";
    return false;
  }

  // Create a new channel for receiving video data.
  // In order to get the bandwidth estimation work fine for
  // receive only channels, we connect all receiving channels
  // to our master send channel.
  int channel_id = -1;
  if (!CreateChannel(sp.first_ssrc(), MD_RECV, &channel_id)) {
    return false;
  }

  // Get the default renderer.
  VideoRenderer* default_renderer = NULL;
  if (InConferenceMode()) {
    // The recv_channels_ size start out being 1, so if it is two here this
    // is the first receive channel created (vie_channel_ is not used for
    // receiving in a conference call). This means that the renderer stored
    // inside vie_channel_ should be used for the just created channel.
    if (recv_channels_.size() == 2 &&
        recv_channels_.find(0) != recv_channels_.end()) {
      GetRenderer(0, &default_renderer);
    }
  }

  // The first recv stream reuses the default renderer (if a default renderer
  // has been set).
  if (default_renderer) {
    SetRenderer(sp.first_ssrc(), default_renderer);
  }

  LOG(LS_INFO) << "New video stream " << sp.first_ssrc()
               << " registered to VideoEngine channel #"
               << channel_id << " and connected to channel #" << vie_channel_;

  return true;
}

bool WebRtcVideoMediaChannel::RemoveRecvStream(uint32 ssrc) {
  RecvChannelMap::iterator it = recv_channels_.find(ssrc);

  if (it == recv_channels_.end()) {
    // TODO(perkj): Remove this once BWE works properly across different send
    // and receive channels.
    // The default channel is reused for recv stream in 1:1 call.
    if (first_receive_ssrc_ == ssrc) {
      first_receive_ssrc_ = 0;
      // Need to stop the renderer and remove it since the render window can be
      // deleted after this.
      if (render_started_) {
        if (engine()->vie()->render()->StopRender(vie_channel_) !=0) {
          LOG_RTCERR1(StopRender, it->second->channel_id());
        }
      }
      recv_channels_[0]->SetRenderer(NULL);
      return true;
    }
    return false;
  }
  WebRtcVideoChannelRecvInfo* info = it->second;
  int channel_id = info->channel_id();
  if (engine()->vie()->render()->RemoveRenderer(channel_id) != 0) {
    LOG_RTCERR1(RemoveRenderer, channel_id);
  }

  if (engine()->vie()->network()->DeregisterSendTransport(channel_id) !=0) {
    LOG_RTCERR1(DeRegisterSendTransport, channel_id);
  }

  if (engine()->vie()->codec()->DeregisterDecoderObserver(
      channel_id) != 0) {
    LOG_RTCERR1(DeregisterDecoderObserver, channel_id);
  }

  LOG(LS_INFO) << "Removing video stream " << ssrc
               << " with VideoEngine channel #"
               << channel_id;
  if (engine()->vie()->base()->DeleteChannel(channel_id) == -1) {
    LOG_RTCERR1(DeleteChannel, channel_id);
    // Leak the WebRtcVideoChannelRecvInfo owned by |it| but remove the channel
    // from recv_channels_.
    recv_channels_.erase(it);
    return false;
  }
  // Delete the WebRtcVideoChannelRecvInfo pointed to by it->second.
  delete info;
  recv_channels_.erase(it);
  return true;
}

bool WebRtcVideoMediaChannel::StartSend() {
  bool success = true;
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    if (!StartSend(send_channel)) {
      success = false;
    }
  }
  return success;
}

bool WebRtcVideoMediaChannel::StartSend(
    WebRtcVideoChannelSendInfo* send_channel) {
  const int channel_id = send_channel->channel_id();
  if (engine()->vie()->base()->StartSend(channel_id) != 0) {
    LOG_RTCERR1(StartSend, channel_id);
    return false;
  }

  const bool remb_receiving = !InConferenceMode() &&
      IsDefaultChannel(channel_id);
  if (engine_->vie()->rtp()->SetRembStatus(channel_id,
                                           kRembSending,
                                           remb_receiving) != 0) {
    LOG_RTCERR3(SetRembStatus, channel_id, kRembSending, remb_receiving);
    return false;
  }
  send_channel->set_sending(true);
  if (!send_channel->video_capturer()) {
    engine_->IncrementFrameListeners();
  }
  return true;
}

bool WebRtcVideoMediaChannel::StopSend() {
  bool success = true;
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    if (!StopSend(send_channel)) {
      success = false;
    }
  }
  return success;
}

bool WebRtcVideoMediaChannel::StopSend(
    WebRtcVideoChannelSendInfo* send_channel) {
  const int channel_id = send_channel->channel_id();
  if (engine()->vie()->base()->StopSend(channel_id) != 0) {
    LOG_RTCERR1(StopSend, channel_id);
    return false;
  }

  // All send channels are send only, except for the default channel in 1:1
  // calls. Remb needs to be notified that the channel is still receiving in
  // that case.
  const bool receiving = IsDefaultChannel(channel_id) && !InConferenceMode();
  if (engine_->vie()->rtp()->SetRembStatus(channel_id,
                                           kRembNotSending,
                                           receiving) != 0) {
    LOG_RTCERR3(SetRembStatus, channel_id, kRembNotSending, receiving);
    return false;
  }
  send_channel->set_sending(false);
  if (!send_channel->video_capturer()) {
    engine_->DecrementFrameListeners();
  }
  return true;
}

bool WebRtcVideoMediaChannel::SendIntraFrame() {
  bool success = true;
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end();
       ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    const int channel_id = send_channel->channel_id();
    if (engine()->vie()->codec()->SendKeyFrame(channel_id) != 0) {
      LOG_RTCERR1(SendKeyFrame, channel_id);
      success = false;
    }
  }
  return success;
}

bool WebRtcVideoMediaChannel::IsOneSsrcStream(const StreamParams& sp) {
  return (sp.ssrcs.size() == 1 && sp.ssrc_groups.size() == 0);
}

bool WebRtcVideoMediaChannel::HasReadySendChannels() {
  return !send_channels_.empty() &&
      ((send_channels_.size() > 1) ||
       (send_channels_[0]->stream_params() != NULL));
}

bool WebRtcVideoMediaChannel::GetSendChannelKey(uint32 local_ssrc,
                                                uint32* key) {
  *key = 0;
  // If a send channel is not ready to send it will not have local_ssrc
  // registered to it.
  if (!HasReadySendChannels()) {
    return false;
  }
  // The default channel is stored with key 0. The key therefore does not match
  // the SSRC associated with the default channel. Check if the SSRC provided
  // corresponds to the default channel's SSRC.
  if (local_ssrc == GetDefaultChannelSsrc()) {
    return true;
  }
  if (send_channels_.find(local_ssrc) == send_channels_.end()) {
    for (SendChannelMap::iterator iter = send_channels_.begin();
         iter != send_channels_.end(); ++iter) {
      WebRtcVideoChannelSendInfo* send_channel = iter->second;
      if (send_channel->has_ssrc(local_ssrc)) {
        *key = iter->first;
        return true;
      }
    }
    return false;
  }
  // The key was found in the above std::map::find call. This means that the
  // ssrc is the key.
  *key = local_ssrc;
  return true;
}

WebRtcVideoChannelSendInfo* WebRtcVideoMediaChannel::GetSendChannel(
    VideoCapturer* video_capturer) {
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    if (send_channel->video_capturer() == video_capturer) {
      return send_channel;
    }
  }
  return NULL;
}

WebRtcVideoChannelSendInfo* WebRtcVideoMediaChannel::GetSendChannel(
    uint32 local_ssrc) {
  uint32 key;
  if (!GetSendChannelKey(local_ssrc, &key)) {
    return NULL;
  }
  return send_channels_[key];
}

bool WebRtcVideoMediaChannel::CreateSendChannelKey(uint32 local_ssrc,
                                                   uint32* key) {
  if (GetSendChannelKey(local_ssrc, key)) {
    // If there is a key corresponding to |local_ssrc|, the SSRC is already in
    // use. SSRCs need to be unique in a session and at this point a duplicate
    // SSRC has been detected.
    return false;
  }
  if (send_channels_[0]->stream_params() == NULL) {
    // key should be 0 here as the default channel should be re-used whenever it
    // is not used.
    *key = 0;
    return true;
  }
  // SSRC is currently not in use and the default channel is already in use. Use
  // the SSRC as key since it is supposed to be unique in a session.
  *key = local_ssrc;
  return true;
}

uint32 WebRtcVideoMediaChannel::GetDefaultChannelSsrc() {
  WebRtcVideoChannelSendInfo* send_channel = send_channels_[0];
  const StreamParams* sp = send_channel->stream_params();
  if (sp == NULL) {
    // This happens if no send stream is currently registered.
    return 0;
  }
  return sp->first_ssrc();
}

bool WebRtcVideoMediaChannel::DeleteSendChannel(uint32 ssrc_key) {
  if (send_channels_.find(ssrc_key) == send_channels_.end()) {
    return false;
  }
  WebRtcVideoChannelSendInfo* send_channel = send_channels_[ssrc_key];
  VideoCapturer* capturer = send_channel->video_capturer();
  if (capturer != NULL) {
    capturer->SignalVideoFrame.disconnect(this);
    send_channel->set_video_capturer(NULL);
  }

  int channel_id = send_channel->channel_id();
  int capture_id = send_channel->capture_id();
  if (engine()->vie()->codec()->DeregisterEncoderObserver(
          channel_id) != 0) {
    LOG_RTCERR1(DeregisterEncoderObserver, channel_id);
  }

  // Destroy the external capture interface.
  if (engine()->vie()->capture()->DisconnectCaptureDevice(
          channel_id) != 0) {
    LOG_RTCERR1(DisconnectCaptureDevice, channel_id);
  }
  if (engine()->vie()->capture()->ReleaseCaptureDevice(
          capture_id) != 0) {
    LOG_RTCERR1(ReleaseCaptureDevice, capture_id);
  }

  // The default channel is stored in both |send_channels_| and
  // |recv_channels_|. To make sure it is only deleted once from vie let the
  // delete call happen when tearing down |recv_channels_| and not here.
  if (!IsDefaultChannel(channel_id)) {
    engine_->vie()->base()->DeleteChannel(channel_id);
  }
  delete send_channel;
  send_channels_.erase(ssrc_key);
  return true;
}

bool WebRtcVideoMediaChannel::RemoveCapturer(uint32 ssrc) {
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(ssrc);
  if (!send_channel) {
    return false;
  }
  VideoCapturer* capturer = send_channel->video_capturer();
  if (capturer == NULL) {
    return false;
  }
  capturer->SignalVideoFrame.disconnect(this);
  send_channel->set_video_capturer(NULL);
  if (send_channel->sending()) {
    engine_->IncrementFrameListeners();
  }
  const int64 timestamp = send_channel->last_frame_time_stamp();
  if (send_codec_) {
    QueueBlackFrame(ssrc, timestamp, send_codec_->maxFramerate);
  }
  return true;
}

bool WebRtcVideoMediaChannel::SetRenderer(uint32 ssrc,
                                          VideoRenderer* renderer) {
  if (recv_channels_.find(ssrc) == recv_channels_.end()) {
    // TODO(perkj): Remove this once BWE works properly across different send
    // and receive channels.
    // The default channel is reused for recv stream in 1:1 call.
    if (first_receive_ssrc_ == ssrc &&
        recv_channels_.find(0) != recv_channels_.end()) {
      LOG(LS_INFO) << "SetRenderer " << ssrc
                   << " reuse default channel #"
                   << vie_channel_;
      recv_channels_[0]->SetRenderer(renderer);
      return true;
    }
    return false;
  }

  recv_channels_[ssrc]->SetRenderer(renderer);
  return true;
}

bool WebRtcVideoMediaChannel::GetStats(VideoMediaInfo* info) {
  // Get sender statistics and build VideoSenderInfo.
  unsigned int total_bitrate_sent = 0;
  unsigned int video_bitrate_sent = 0;
  unsigned int fec_bitrate_sent = 0;
  unsigned int nack_bitrate_sent = 0;
  unsigned int estimated_send_bandwidth = 0;
  unsigned int target_enc_bitrate = 0;
  if (send_codec_) {
    for (SendChannelMap::const_iterator iter = send_channels_.begin();
         iter != send_channels_.end(); ++iter) {
      WebRtcVideoChannelSendInfo* send_channel = iter->second;
      const int channel_id = send_channel->channel_id();
      VideoSenderInfo sinfo;
      const StreamParams* send_params = send_channel->stream_params();
      if (send_params == NULL) {
        // This should only happen if the default vie channel is not in use.
        // This can happen if no streams have ever been added or the stream
        // corresponding to the default channel has been removed. Note that
        // there may be non-default vie channels in use when this happen so
        // asserting send_channels_.size() == 1 is not correct and neither is
        // breaking out of the loop.
        ASSERT(channel_id == vie_channel_);
        continue;
      }
      unsigned int bytes_sent, packets_sent, bytes_recv, packets_recv;
      if (engine_->vie()->rtp()->GetRTPStatistics(channel_id, bytes_sent,
                                                  packets_sent, bytes_recv,
                                                  packets_recv) != 0) {
        LOG_RTCERR1(GetRTPStatistics, vie_channel_);
        continue;
      }
      WebRtcLocalStreamInfo* channel_stream_info =
          send_channel->local_stream_info();

      sinfo.ssrcs = send_params->ssrcs;
      sinfo.codec_name = send_codec_->plName;
      sinfo.bytes_sent = bytes_sent;
      sinfo.packets_sent = packets_sent;
      sinfo.packets_cached = -1;
      sinfo.packets_lost = -1;
      sinfo.fraction_lost = -1;
      sinfo.firs_rcvd = -1;
      sinfo.nacks_rcvd = -1;
      sinfo.rtt_ms = -1;
      sinfo.frame_width = channel_stream_info->width();
      sinfo.frame_height = channel_stream_info->height();
      sinfo.framerate_input = channel_stream_info->framerate();
      sinfo.framerate_sent = send_channel->encoder_observer()->framerate();
      sinfo.nominal_bitrate = send_channel->encoder_observer()->bitrate();
      sinfo.preferred_bitrate = send_max_bitrate_;

      // Get received RTCP statistics for the sender, if available.
      // It's not a fatal error if we can't, since RTCP may not have arrived
      // yet.
      uint16 r_fraction_lost;
      unsigned int r_cumulative_lost;
      unsigned int r_extended_max;
      unsigned int r_jitter;
      int r_rtt_ms;

      if (engine_->vie()->rtp()->GetSentRTCPStatistics(
              channel_id,
              r_fraction_lost,
              r_cumulative_lost,
              r_extended_max,
              r_jitter, r_rtt_ms) == 0) {
        // Convert Q8 to float.
        sinfo.packets_lost = r_cumulative_lost;
        sinfo.fraction_lost = static_cast<float>(r_fraction_lost) / (1 << 8);
        sinfo.rtt_ms = r_rtt_ms;
      }
      info->senders.push_back(sinfo);

      unsigned int channel_total_bitrate_sent = 0;
      unsigned int channel_video_bitrate_sent = 0;
      unsigned int channel_fec_bitrate_sent = 0;
      unsigned int channel_nack_bitrate_sent = 0;
      if (engine_->vie()->rtp()->GetBandwidthUsage(
          channel_id, channel_total_bitrate_sent, channel_video_bitrate_sent,
          channel_fec_bitrate_sent, channel_nack_bitrate_sent) == 0) {
        total_bitrate_sent += channel_total_bitrate_sent;
        video_bitrate_sent += channel_video_bitrate_sent;
        fec_bitrate_sent += channel_fec_bitrate_sent;
        nack_bitrate_sent += channel_nack_bitrate_sent;
      } else {
        LOG_RTCERR1(GetBandwidthUsage, channel_id);
      }

      unsigned int estimated_stream_send_bandwidth = 0;
      if (engine_->vie()->rtp()->GetEstimatedSendBandwidth(
          channel_id, &estimated_stream_send_bandwidth) == 0) {
        estimated_send_bandwidth += estimated_stream_send_bandwidth;
      } else {
        LOG_RTCERR1(GetEstimatedSendBandwidth, channel_id);
      }
      unsigned int target_enc_stream_bitrate = 0;
      if (engine_->vie()->codec()->GetCodecTargetBitrate(
          channel_id, &target_enc_stream_bitrate) == 0) {
        target_enc_bitrate += target_enc_stream_bitrate;
      } else {
        LOG_RTCERR1(GetCodecTargetBitrate, channel_id);
      }
    }
  } else {
    LOG(LS_WARNING) << "GetStats: sender information not ready.";
  }

  // Get the SSRC and stats for each receiver, based on our own calculations.
  unsigned int estimated_recv_bandwidth = 0;
  for (RecvChannelMap::const_iterator it = recv_channels_.begin();
       it != recv_channels_.end(); ++it) {
    // Don't report receive statistics from the default channel if we have
    // specified receive channels.
    if (it->first == 0 && recv_channels_.size() > 1)
      continue;
    WebRtcVideoChannelRecvInfo* channel = it->second;

    unsigned int ssrc;
    // Get receiver statistics and build VideoReceiverInfo, if we have data.
    if (engine_->vie()->rtp()->GetRemoteSSRC(channel->channel_id(), ssrc) != 0)
      continue;

    unsigned int bytes_sent, packets_sent, bytes_recv, packets_recv;
    if (engine_->vie()->rtp()->GetRTPStatistics(
        channel->channel_id(), bytes_sent, packets_sent, bytes_recv,
        packets_recv) != 0) {
      LOG_RTCERR1(GetRTPStatistics, channel->channel_id());
      return false;
    }
    VideoReceiverInfo rinfo;
    rinfo.ssrcs.push_back(ssrc);
    rinfo.bytes_rcvd = bytes_recv;
    rinfo.packets_rcvd = packets_recv;
    rinfo.packets_lost = -1;
    rinfo.packets_concealed = -1;
    rinfo.fraction_lost = -1;  // from SentRTCP
    rinfo.firs_sent = channel->decoder_observer()->firs_requested();
    rinfo.nacks_sent = -1;
    rinfo.frame_width = channel->render_adapter()->width();
    rinfo.frame_height = channel->render_adapter()->height();
    rinfo.framerate_rcvd = channel->decoder_observer()->framerate();
    int fps = channel->render_adapter()->framerate();
    rinfo.framerate_decoded = fps;
    rinfo.framerate_output = fps;

    // Get sent RTCP statistics.
    uint16 s_fraction_lost;
    unsigned int s_cumulative_lost;
    unsigned int s_extended_max;
    unsigned int s_jitter;
    int s_rtt_ms;
    if (engine_->vie()->rtp()->GetReceivedRTCPStatistics(channel->channel_id(),
            s_fraction_lost, s_cumulative_lost, s_extended_max,
            s_jitter, s_rtt_ms) == 0) {
      // Convert Q8 to float.
      rinfo.packets_lost = s_cumulative_lost;
      rinfo.fraction_lost = static_cast<float>(s_fraction_lost) / (1 << 8);
    }
    info->receivers.push_back(rinfo);

    unsigned int estimated_recv_stream_bandwidth = 0;
    if (engine_->vie()->rtp()->GetEstimatedReceiveBandwidth(
        channel->channel_id(), &estimated_recv_stream_bandwidth) == 0) {
      estimated_recv_bandwidth += estimated_recv_stream_bandwidth;
    } else {
      LOG_RTCERR1(GetEstimatedReceiveBandwidth, channel->channel_id());
    }
  }

  // Build BandwidthEstimationInfo.
  // TODO(zhurunz): Add real unittest for this.
  BandwidthEstimationInfo bwe;

  // Calculations done above per send/receive stream.
  bwe.actual_enc_bitrate = video_bitrate_sent;
  bwe.transmit_bitrate = total_bitrate_sent;
  bwe.retransmit_bitrate = nack_bitrate_sent;
  bwe.available_send_bandwidth = estimated_send_bandwidth;
  bwe.available_recv_bandwidth = estimated_recv_bandwidth;
  bwe.target_enc_bitrate = target_enc_bitrate;

  info->bw_estimations.push_back(bwe);

  return true;
}

bool WebRtcVideoMediaChannel::SetCapturer(uint32 ssrc,
                                          VideoCapturer* capturer) {
  ASSERT(ssrc != 0);
  if (!capturer) {
    return RemoveCapturer(ssrc);
  }
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(ssrc);
  if (!send_channel) {
    return false;
  }
  VideoCapturer* old_capturer = send_channel->video_capturer();
  if (send_channel->sending() && !old_capturer) {
    engine_->DecrementFrameListeners();
  }
  if (old_capturer) {
    old_capturer->SignalVideoFrame.disconnect(this);
  }

  send_channel->set_video_capturer(capturer);
  capturer->SignalVideoFrame.connect(
      this,
      &WebRtcVideoMediaChannel::SendFrame);
  if (!capturer->IsScreencast()) {
    capturer->UpdateAspectRatio(ratio_w_, ratio_h_);
  }
  const int64 timestamp = send_channel->last_frame_time_stamp();
  if (send_codec_) {
    QueueBlackFrame(ssrc, timestamp, send_codec_->maxFramerate);
  }
  return true;
}

bool WebRtcVideoMediaChannel::RequestIntraFrame() {
  // There is no API exposed to application to request a key frame
  // ViE does this internally when there are errors from decoder
  return false;
}

void WebRtcVideoMediaChannel::OnPacketReceived(talk_base::Buffer* packet) {
  // Pick which channel to send this packet to. If this packet doesn't match
  // any multiplexed streams, just send it to the default channel. Otherwise,
  // send it to the specific decoder instance for that stream.
  uint32 ssrc = 0;
  if (!GetRtpSsrc(packet->data(), packet->length(), &ssrc))
    return;
  int which_channel = GetRecvChannelNum(ssrc);
  if (which_channel == -1) {
    which_channel = video_channel();
  }

  engine()->vie()->network()->ReceivedRTPPacket(which_channel,
                                                packet->data(),
                                                packet->length());
}

void WebRtcVideoMediaChannel::OnRtcpReceived(talk_base::Buffer* packet) {
// Sending channels need all RTCP packets with feedback information.
// Even sender reports can contain attached report blocks.
// Receiving channels need sender reports in order to create
// correct receiver reports.

  uint32 ssrc = 0;
  if (!GetRtcpSsrc(packet->data(), packet->length(), &ssrc)) {
    LOG(LS_WARNING) << "Failed to parse SSRC from received RTCP packet";
    return;
  }
  int type = 0;
  if (!GetRtcpType(packet->data(), packet->length(), &type)) {
    LOG(LS_WARNING) << "Failed to parse type from received RTCP packet";
    return;
  }

  // If it is a sender report, find the channel that is listening.
  if (type == kRtcpTypeSR) {
    int which_channel = GetRecvChannelNum(ssrc);
    if (which_channel != -1 && !IsDefaultChannel(which_channel)) {
      engine_->vie()->network()->ReceivedRTCPPacket(which_channel,
                                                    packet->data(),
                                                    packet->length());
    }
  }
  // SR may continue RR and any RR entry may correspond to any one of the send
  // channels. So all RTCP packets must be forwarded all send channels. ViE
  // will filter out RR internally.
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    int channel_id = send_channel->channel_id();
    engine_->vie()->network()->ReceivedRTCPPacket(channel_id,
                                                  packet->data(),
                                                  packet->length());
  }
}

bool WebRtcVideoMediaChannel::MuteStream(uint32 ssrc, bool on) {
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(ssrc);
  if (!send_channel) {
    LOG(LS_ERROR) << "The specified ssrc " << ssrc << " is not in use.";
    return false;
  }
  send_channel->set_muted(on);
  return true;
}

bool WebRtcVideoMediaChannel::SetRecvRtpHeaderExtensions(
    const std::vector<RtpHeaderExtension>& extensions) {
  // Enable RTP timestamp offset extension if requested.
  receive_extensions_ = extensions;

  bool enable = false;
  int id = 0;
  const RtpHeaderExtension* offset_extension = FindHeaderExtension(
      extensions, kRtpTimestampOffsetHeaderExtension);
  if (offset_extension) {
    enable = true;
    id = offset_extension->id;
  }

  // Loop through all receive channels and enable/disable the extension.
  for (RecvChannelMap::iterator channel_it = recv_channels_.begin();
       channel_it != recv_channels_.end(); ++channel_it) {
    WebRtcVideoChannelRecvInfo* recv_channel = channel_it->second;
    int channel_id = recv_channel->channel_id();
    if (engine_->vie()->rtp()->SetReceiveTimestampOffsetStatus(channel_id,
                                                               enable,
                                                               id) != 0) {
      LOG_RTCERR3(SetReceiveTimestampOffsetStatus, channel_id, true, id);
      return false;
    }
  }
  return true;
}

bool WebRtcVideoMediaChannel::SetSendRtpHeaderExtensions(
    const std::vector<RtpHeaderExtension>& extensions) {
  // Enable RTP timestamp offset extension if requested.
  send_extensions_ = extensions;

  bool enable = false;
  int id = 0;
  const RtpHeaderExtension* offset_extension = FindHeaderExtension(
      extensions, kRtpTimestampOffsetHeaderExtension);
  if (offset_extension) {
    enable = true;
    id = offset_extension->id;
  }

  // Loop through all send channels and enable the extension.
  for (SendChannelMap::iterator channel_it = send_channels_.begin();
       channel_it != send_channels_.end(); ++channel_it) {
    WebRtcVideoChannelSendInfo* send_channel = channel_it->second;
    int channel_id = send_channel->channel_id();
    if (engine_->vie()->rtp()->SetSendTimestampOffsetStatus(channel_id, enable,
                                                            id) != 0) {
      LOG_RTCERR3(SetSendTimestampOffsetStatus, channel_id, enable, id);
      return false;
    }
  }
  return true;
}

bool WebRtcVideoMediaChannel::SetSendBandwidth(bool autobw, int bps) {
  LOG(LS_INFO) << "WebRtcVideoMediaChanne::SetSendBandwidth";

  if (InConferenceMode()) {
    LOG(LS_INFO) << "Conference mode ignores SetSendBandWidth";
    return true;
  }

  if (!send_codec_) {
    LOG(LS_INFO) << "The send codec has not been set up yet";
    return true;
  }

  int min_bitrate;
  int start_bitrate;
  int max_bitrate;
  if (autobw) {
    // Use the default values for min bitrate.
    min_bitrate = kMinVideoBitrate;
    // Use the default value or the bps for the max
    max_bitrate = (bps <= 0) ? send_max_bitrate_ : (bps / 1000);
    // Maximum start bitrate can be kStartVideoBitrate.
    start_bitrate = talk_base::_min(kStartVideoBitrate, max_bitrate);
  } else {
    // Use the default start or the bps as the target bitrate.
    int target_bitrate = (bps <= 0) ? kStartVideoBitrate : (bps / 1000);
    min_bitrate = target_bitrate;
    start_bitrate = target_bitrate;
    max_bitrate = target_bitrate;
  }

  if (!SetSendCodec(*send_codec_, min_bitrate, start_bitrate, max_bitrate)) {
    return false;
  }
  LogSendCodecChange("SetSendBandwidth()");

  return true;
}

bool WebRtcVideoMediaChannel::SetOptions(const VideoOptions &options) {
  // Always accept options that are unchanged.
  if (options_ == options) {
    return true;
  }

  // Reject new options if we're already sending.
  if (sending()) {
    LOG(LS_INFO) << "Not setting options - already sending | "
                 << options.ToString();
    return false;
  }

  // Trigger SetSendCodec to set correct noise reduction state if the option has
  // changed.
  bool denoiser_changed =
      (options_.video_noise_reduction != options.video_noise_reduction);

  bool leaky_bucket_changed =
      (options_.video_leaky_bucket != options.video_leaky_bucket);

  bool buffer_latency_changed =
      (options_.buffered_mode_latency != options.buffered_mode_latency);

  // Save the options, to be interpreted where appropriate.
  options_ = options;

  // Adjust send codec bitrate if needed.
  int conf_max_bitrate = kDefaultConferenceModeMaxVideoBitrate;
  int expected_bitrate = InConferenceMode() ?
      conf_max_bitrate : kMaxVideoBitrate;

  if (send_codec_ &&
      (send_max_bitrate_ != expected_bitrate || denoiser_changed)) {
    // On success, SetSendCodec() will reset send_max_bitrate_ to
    // expected_bitrate.
    if (!SetSendCodec(*send_codec_,
                      send_min_bitrate_,
                      send_start_bitrate_,
                      expected_bitrate)) {
      return false;
    }
    LogSendCodecChange("SetOptions()");
  }
  if (leaky_bucket_changed) {
    bool enable_leaky_bucket =
        options_.video_leaky_bucket.GetWithDefaultIfUnset(false);
    for (SendChannelMap::iterator it = send_channels_.begin();
        it != send_channels_.end(); ++it) {
      if (engine()->vie()->rtp()->SetTransmissionSmoothingStatus(
          it->second->channel_id(), enable_leaky_bucket) != 0) {
        LOG_RTCERR2(SetTransmissionSmoothingStatus, it->second->channel_id(),
                    enable_leaky_bucket);
      }
    }
  }
  if (buffer_latency_changed) {
    int buffer_latency =
        options_.buffered_mode_latency.GetWithDefaultIfUnset(
            cricket::kBufferedModeDisabled);
    for (SendChannelMap::iterator it = send_channels_.begin();
        it != send_channels_.end(); ++it) {
      if (engine()->vie()->rtp()->EnableSenderStreamingMode(
          it->second->channel_id(), buffer_latency) != 0) {
        LOG_RTCERR2(EnableSenderStreamingMode, it->second->channel_id(),
                    buffer_latency);
      }
    }
  }
  return true;
}

void WebRtcVideoMediaChannel::SetInterface(NetworkInterface* iface) {
  MediaChannel::SetInterface(iface);
  // Set the RTP recv/send buffer to a bigger size
  if (network_interface_) {
    network_interface_->SetOption(NetworkInterface::ST_RTP,
                                  talk_base::Socket::OPT_RCVBUF,
                                  kVideoRtpBufferSize);

    // TODO(sriniv): Remove or re-enable this.
    // As part of b/8030474, send-buffer is size now controlled through
    // portallocator flags.
    // network_interface_->SetOption(NetworkInterface::ST_RTP,
    //                              talk_base::Socket::OPT_SNDBUF,
    //                              kVideoRtpBufferSize);
  }
}

void WebRtcVideoMediaChannel::UpdateAspectRatio(int ratio_w, int ratio_h) {
  ratio_w_ = ratio_w;
  ratio_h_ = ratio_h;
  // For now assume that all streams want the same aspect ratio.
  // TODO(hellner): remove the need for this assumption.
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    VideoCapturer* capturer = send_channel->video_capturer();
    if (capturer) {
      capturer->UpdateAspectRatio(ratio_w, ratio_h);
    }
  }
}

bool WebRtcVideoMediaChannel::GetRenderer(uint32 ssrc,
                                          VideoRenderer** renderer) {
  RecvChannelMap::const_iterator it = recv_channels_.find(ssrc);
  if (it == recv_channels_.end()) {
    if (first_receive_ssrc_ == ssrc &&
        recv_channels_.find(0) != recv_channels_.end()) {
      LOG(LS_INFO) << " GetRenderer " << ssrc
                   << " reuse default renderer #"
                   << vie_channel_;
      *renderer = recv_channels_[0]->render_adapter()->renderer();
      return true;
    }
    return false;
  }

  *renderer = it->second->render_adapter()->renderer();
  return true;
}

// TODO(zhurunz): Add unittests to test this function.
void WebRtcVideoMediaChannel::SendFrame(VideoCapturer* capturer,
                                        const VideoFrame* frame) {
  // If there's send channel registers to the |capturer|, then only send the
  // frame to that channel and return. Otherwise send the frame to the default
  // channel, which currently taking frames from the engine.
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(capturer);
  if (send_channel) {
    SendFrame(send_channel, frame, capturer->IsScreencast());
    return;
  }
  // TODO(hellner): Remove below for loop once the captured frame no longer
  // come from the engine, i.e. the engine no longer owns a capturer.
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    if (send_channel->video_capturer() == NULL) {
      SendFrame(send_channel, frame, capturer->IsScreencast());
    }
  }
}

bool WebRtcVideoMediaChannel::SendFrame(
    WebRtcVideoChannelSendInfo* send_channel,
    const VideoFrame* frame,
    bool is_screencast) {
  if (!send_channel) {
    return false;
  }
  const VideoFormat& video_format = send_channel->video_format();
  // If the frame should be dropped.
  const bool video_format_set = video_format != cricket::VideoFormat();
  if (video_format_set &&
      (video_format.width == 0 && video_format.height == 0)) {
    return true;
  }

  WebRtcLocalStreamInfo* channel_stream_info =
      send_channel->local_stream_info();

  // Update local stream statistics.
  channel_stream_info->UpdateFrame(frame->GetWidth(), frame->GetHeight());

  // Checks if we need to reset vie send codec.
  if (!MaybeResetVieSendCodec(send_channel, frame->GetWidth(),
                              frame->GetHeight(), is_screencast, NULL)) {
    LOG(LS_ERROR) << "MaybeResetVieSendCodec failed with "
                  << frame->GetWidth() << "x" << frame->GetHeight();
    return false;
  }
  const VideoFrame* frame_out = frame;
  talk_base::scoped_ptr<VideoFrame> processed_frame;
  WebRtc_Word64 clocks = 0;
  // Disable muting for screencast.
  const bool mute = (send_channel->muted() && !is_screencast);
  send_channel->ProcessFrame(*frame_out, mute, processed_frame.use(),
                             &clocks);
  if (processed_frame) {
    frame_out = processed_frame.get();
  }

  webrtc::ViEVideoFrameI420 frame_i420;
  // TODO(ronghuawu): Update the webrtc::ViEVideoFrameI420
  // to use const unsigned char*
  frame_i420.y_plane = const_cast<unsigned char*>(frame_out->GetYPlane());
  frame_i420.u_plane = const_cast<unsigned char*>(frame_out->GetUPlane());
  frame_i420.v_plane = const_cast<unsigned char*>(frame_out->GetVPlane());
  frame_i420.y_pitch = frame_out->GetYPitch();
  frame_i420.u_pitch = frame_out->GetUPitch();
  frame_i420.v_pitch = frame_out->GetVPitch();
  frame_i420.width = frame_out->GetWidth();
  frame_i420.height = frame_out->GetHeight();

  return send_channel->external_capture()->IncomingFrameI420(
      frame_i420, clocks) == 0;
}

bool WebRtcVideoMediaChannel::CreateChannel(uint32 ssrc_key,
                                            MediaDirection direction,
                                            int* channel_id) {
  // There are 3 types of channels. Sending only, receiving only and
  // sending and receiving. The sending and receiving channel is the
  // default channel and there is only one. All other channels that are created
  // are associated with the default channel which must exist. The default
  // channel id is stored in |vie_channel_|. All channels need to know about
  // the default channel to properly handle remb which is why there are
  // different ViE create channel calls.
  // For this channel the local and remote ssrc key is 0. However, it may
  // have a non-zero local and/or remote ssrc depending on if it is currently
  // sending and/or receiving.
  if ((vie_channel_ == -1 || direction == MD_SENDRECV) &&
      (!send_channels_.empty() || !recv_channels_.empty())) {
    ASSERT(false);
    return false;
  }

  *channel_id = -1;
  if (direction == MD_RECV) {
    // All rec channels are associated with the default channel |vie_channel_|
    if (engine_->vie()->base()->CreateReceiveChannel(*channel_id,
                                                     vie_channel_) != 0) {
      LOG_RTCERR2(CreateReceiveChannel, *channel_id, vie_channel_);
      return false;
    }
  } else if (direction == MD_SEND) {
    if (engine_->vie()->base()->CreateChannel(*channel_id,
                                              vie_channel_) != 0) {
      LOG_RTCERR2(CreateChannel, *channel_id, vie_channel_);
      return false;
    }
  } else {
    ASSERT(direction == MD_SENDRECV);
    if (engine_->vie()->base()->CreateChannel(*channel_id) != 0) {
      LOG_RTCERR1(CreateChannel, *channel_id);
      return false;
    }
  }
  if (!ConfigureChannel(*channel_id, direction, ssrc_key)) {
    engine_->vie()->base()->DeleteChannel(*channel_id);
    *channel_id = -1;
    return false;
  }

  return true;
}

bool WebRtcVideoMediaChannel::ConfigureChannel(int channel_id,
                                               MediaDirection direction,
                                               uint32 ssrc_key) {
  const bool receiving = (direction == MD_RECV) || (direction == MD_SENDRECV);
  const bool sending = (direction == MD_SEND) || (direction == MD_SENDRECV);
  // Register external transport.
  if (engine_->vie()->network()->RegisterSendTransport(
      channel_id, *this) != 0) {
    LOG_RTCERR1(RegisterSendTransport, channel_id);
    return false;
  }

  // Set MTU.
  if (engine_->vie()->network()->SetMTU(channel_id, kVideoMtu) != 0) {
    LOG_RTCERR2(SetMTU, channel_id, kVideoMtu);
    return false;
  }
  // Turn on RTCP and loss feedback reporting.
  if (engine()->vie()->rtp()->SetRTCPStatus(
      channel_id, webrtc::kRtcpCompound_RFC4585) != 0) {
    LOG_RTCERR2(SetRTCPStatus, channel_id, webrtc::kRtcpCompound_RFC4585);
    return false;
  }
  // Enable pli as key frame request method.
  if (engine_->vie()->rtp()->SetKeyFrameRequestMethod(
      channel_id, webrtc::kViEKeyFrameRequestPliRtcp) != 0) {
    LOG_RTCERR2(SetKeyFrameRequestMethod,
                channel_id, webrtc::kViEKeyFrameRequestPliRtcp);
    return false;
  }
  if (receiving) {
    if (!ConfigureReceiving(channel_id, ssrc_key)) {
      return false;
    }
  }
  if (sending) {
    if (!ConfigureSending(channel_id, ssrc_key)) {
      return false;
    }
  }

  return true;
}

bool WebRtcVideoMediaChannel::ConfigureReceiving(int channel_id,
                                                 uint32 remote_ssrc_key) {
  // Make sure that an SSRC/key isn't registered more than once.
  if (recv_channels_.find(remote_ssrc_key) != recv_channels_.end()) {
    return false;
  }
  // Connect the voice channel, if there is one.
  // TODO(perkj): The A/V is synched by the receiving channel. So we need to
  // know the SSRC of the remote audio channel in order to fetch the correct
  // webrtc VoiceEngine channel. For now- only sync the default channel used
  // in 1-1 calls.
  if (remote_ssrc_key == 0 && voice_channel_) {
    WebRtcVoiceMediaChannel* voice_channel =
        static_cast<WebRtcVoiceMediaChannel*>(voice_channel_);
    if (engine_->vie()->base()->ConnectAudioChannel(
        vie_channel_, voice_channel->voe_channel()) != 0) {
      LOG_RTCERR2(ConnectAudioChannel, channel_id,
                  voice_channel->voe_channel());
      LOG(LS_WARNING) << "A/V not synchronized";
      // Not a fatal error.
    }
  }

  talk_base::scoped_ptr<WebRtcVideoChannelRecvInfo> channel_info(
      new WebRtcVideoChannelRecvInfo(channel_id));

  // Install a render adapter.
  if (engine_->vie()->render()->AddRenderer(channel_id,
      webrtc::kVideoI420, channel_info->render_adapter()) != 0) {
    LOG_RTCERR3(AddRenderer, channel_id, webrtc::kVideoI420,
                channel_info->render_adapter());
    return false;
  }


  if (engine_->vie()->rtp()->SetRembStatus(channel_id,
                                           kRembNotSending,
                                           kRembReceiving) != 0) {
    LOG_RTCERR3(SetRembStatus, channel_id, kRembNotSending, kRembReceiving);
    return false;
  }

  const RtpHeaderExtension* offset_extension = FindHeaderExtension(
      receive_extensions_, kRtpTimestampOffsetHeaderExtension);
  if (offset_extension) {
    if (engine_->vie()->rtp()->SetReceiveTimestampOffsetStatus(
        channel_id, true, offset_extension->id) != 0) {
      LOG_RTCERR3(SetReceiveTimestampOffsetStatus, channel_id, true,
                  offset_extension->id);
      return false;
    }
  }

  if (remote_ssrc_key != 0) {
    // Use the same SSRC as our default channel
    // (so the RTCP reports are correct).
    unsigned int send_ssrc = 0;
    webrtc::ViERTP_RTCP* rtp = engine()->vie()->rtp();
    if (rtp->GetLocalSSRC(vie_channel_, send_ssrc) == -1) {
      LOG_RTCERR2(GetLocalSSRC, vie_channel_, send_ssrc);
      return false;
    }
    if (rtp->SetLocalSSRC(channel_id, send_ssrc) == -1) {
      LOG_RTCERR2(SetLocalSSRC, channel_id, send_ssrc);
      return false;
    }
  }  // Else this is the the default channel and we don't change the SSRC.

  // Disable color enhancement since it is a bit too aggressive.
  if (engine()->vie()->image()->EnableColorEnhancement(channel_id,
                                                       false) != 0) {
    LOG_RTCERR1(EnableColorEnhancement, channel_id);
    return false;
  }

  if (!SetReceiveCodecs(channel_id)) {
    return false;
  }

  if (render_started_) {
    if (engine_->vie()->render()->StartRender(channel_id) != 0) {
      LOG_RTCERR1(StartRender, channel_id);
      return false;
    }
  }

  // Register decoder observer for incoming framerate and bitrate.
  if (engine()->vie()->codec()->RegisterDecoderObserver(
      channel_id, *channel_info->decoder_observer()) != 0) {
    LOG_RTCERR1(RegisterDecoderObserver, channel_info->decoder_observer());
    return false;
  }

  recv_channels_[remote_ssrc_key] = channel_info.release();
  return true;
}

bool WebRtcVideoMediaChannel::ConfigureSending(int channel_id,
                                               uint32 local_ssrc_key) {
  // The ssrc key can be zero or correspond to an SSRC.
  // Make sure the default channel isn't configured more than once.
  if (local_ssrc_key == 0 && send_channels_.find(0) != send_channels_.end()) {
    return false;
  }
  // Make sure that the SSRC is not already in use.
  uint32 dummy_key;
  if (GetSendChannelKey(local_ssrc_key, &dummy_key)) {
    return false;
  }
  int vie_capture = 0;
  webrtc::ViEExternalCapture* external_capture = NULL;
  // Register external capture.
  if (engine()->vie()->capture()->AllocateExternalCaptureDevice(
      vie_capture, external_capture) != 0) {
    LOG_RTCERR0(AllocateExternalCaptureDevice);
    return false;
  }

  // Connect external capture.
  if (engine()->vie()->capture()->ConnectCaptureDevice(
      vie_capture, channel_id) != 0) {
    LOG_RTCERR2(ConnectCaptureDevice, vie_capture, channel_id);
    return false;
  }
  talk_base::scoped_ptr<WebRtcVideoChannelSendInfo> send_channel(
      new WebRtcVideoChannelSendInfo(channel_id, vie_capture,
                                     external_capture));

  // Register encoder observer for outgoing framerate and bitrate.
  if (engine()->vie()->codec()->RegisterEncoderObserver(
      channel_id, *send_channel->encoder_observer()) != 0) {
    LOG_RTCERR1(RegisterEncoderObserver, send_channel->encoder_observer());
    return false;
  }

  const RtpHeaderExtension* offset_extension = FindHeaderExtension(
      send_extensions_, kRtpTimestampOffsetHeaderExtension);
  if (offset_extension) {
      if (engine_->vie()->rtp()->SetSendTimestampOffsetStatus(
          channel_id, true, offset_extension->id) != 0) {
      LOG_RTCERR3(SetSendTimestampOffsetStatus, channel_id, true,
                  offset_extension->id);
      return false;
    }
  }

  if (options_.video_leaky_bucket.GetWithDefaultIfUnset(false)) {
    if (engine()->vie()->rtp()->SetTransmissionSmoothingStatus(channel_id,
                                                               true) != 0) {
      LOG_RTCERR2(SetTransmissionSmoothingStatus, channel_id, true);
      return false;
    }
  }

  int buffer_latency =
      options_.buffered_mode_latency.GetWithDefaultIfUnset(
          cricket::kBufferedModeDisabled);
  if (buffer_latency != cricket::kBufferedModeDisabled) {
    if (engine()->vie()->rtp()->EnableSenderStreamingMode(
            channel_id, buffer_latency) != 0) {
      LOG_RTCERR2(EnableSenderStreamingMode, channel_id, buffer_latency);
    }
  }

  if (!SetNackFec(channel_id, send_red_type_, send_fec_type_)) {
    // Logged in SetNackFec. Don't spam the logs.
    return false;
  }

  send_channels_[local_ssrc_key] = send_channel.release();

  return true;
}

bool WebRtcVideoMediaChannel::SetNackFec(int channel_id,
                                         int red_payload_type,
                                         int fec_payload_type) {
  // Enable hybrid NACK/FEC if negotiated and not in a conference, use only NACK
  // otherwise.
  bool enable = (red_payload_type != -1 && fec_payload_type != -1 &&
      !InConferenceMode());
  if (enable) {
    if (engine_->vie()->rtp()->SetHybridNACKFECStatus(
        channel_id, enable, red_payload_type, fec_payload_type) != 0) {
      LOG_RTCERR4(SetHybridNACKFECStatus,
                  channel_id, enable, red_payload_type, fec_payload_type);
      return false;
    }
    LOG(LS_INFO) << "Hybrid NACK/FEC enabled for channel " << channel_id;
  } else {
    if (engine_->vie()->rtp()->SetNACKStatus(channel_id, true) != 0) {
      LOG_RTCERR1(SetNACKStatus, channel_id);
      return false;
    }
    LOG(LS_INFO) << "NACK enabled for channel " << channel_id;
  }
  return true;
}

bool WebRtcVideoMediaChannel::SetSendCodec(const webrtc::VideoCodec& codec,
                                           int min_bitrate,
                                           int start_bitrate,
                                           int max_bitrate) {
  bool ret_val = true;
  for (SendChannelMap::iterator iter = send_channels_.begin();
       iter != send_channels_.end(); ++iter) {
    WebRtcVideoChannelSendInfo* send_channel = iter->second;
    ret_val = SetSendCodec(send_channel, codec, min_bitrate, start_bitrate,
                           max_bitrate) && ret_val;
  }
  if (ret_val) {
    // All SetSendCodec calls were successful. Update the global state
    // accordingly.
    send_codec_.reset(new webrtc::VideoCodec(codec));
    send_min_bitrate_ = min_bitrate;
    send_start_bitrate_ = start_bitrate;
    send_max_bitrate_ = max_bitrate;
  } else {
    // At least one SetSendCodec call failed, rollback.
    for (SendChannelMap::iterator iter = send_channels_.begin();
         iter != send_channels_.end(); ++iter) {
      WebRtcVideoChannelSendInfo* send_channel = iter->second;
      if (send_codec_) {
        SetSendCodec(send_channel, *send_codec_.get(), send_min_bitrate_,
                     send_start_bitrate_, send_max_bitrate_);
      }
    }
  }
  return ret_val;
}

bool WebRtcVideoMediaChannel::SetSendCodec(
    WebRtcVideoChannelSendInfo* send_channel,
    const webrtc::VideoCodec& codec,
    int min_bitrate,
    int start_bitrate,
    int max_bitrate) {
  if (!send_channel) {
    return false;
  }
  const int channel_id = send_channel->channel_id();
  // Make a copy of the codec
  webrtc::VideoCodec target_codec = codec;
  target_codec.startBitrate = start_bitrate;
  target_codec.minBitrate = min_bitrate;
  target_codec.maxBitrate = max_bitrate;

  // Set the default number of temporal layers for VP8.
  if (webrtc::kVideoCodecVP8 == codec.codecType) {
    target_codec.codecSpecific.VP8.numberOfTemporalLayers =
        kDefaultNumberOfTemporalLayers;

    // Turn off the VP8 error resilience
    target_codec.codecSpecific.VP8.resilience = webrtc::kResilienceOff;

    bool enable_denoising =
        options_.video_noise_reduction.GetWithDefaultIfUnset(false);
    target_codec.codecSpecific.VP8.denoisingOn = enable_denoising;
  }

  // Resolution and framerate may vary for different send channels.
  const VideoFormat& video_format = send_channel->video_format();
  UpdateVideoCodec(video_format, &target_codec);

  if (target_codec.width == 0 && target_codec.height == 0) {
    const uint32 ssrc = send_channel->stream_params()->first_ssrc();
    LOG(LS_INFO) << "0x0 resolution selected. Captured frames will be dropped "
                 << "for ssrc: " << ssrc << ".";
  } else {
    // Make sure startBitrate is less or equal to maxBitrate;
    target_codec.startBitrate = talk_base::_min(target_codec.startBitrate,
                                                target_codec.maxBitrate);

    if (0 != engine()->vie()->codec()->SetSendCodec(channel_id, target_codec)) {
      LOG_RTCERR2(SetSendCodec, channel_id, target_codec.plName);
      return false;
    }

  }
  send_channel->set_interval(
      cricket::VideoFormat::FpsToInterval(target_codec.maxFramerate));
  return true;
}


void WebRtcVideoMediaChannel::LogSendCodecChange(const std::string& reason) {
  webrtc::VideoCodec vie_codec;
  if (engine()->vie()->codec()->GetSendCodec(vie_channel_, vie_codec) != 0) {
    LOG_RTCERR1(GetSendCodec, vie_channel_);
    return;
  }

  LOG(LS_INFO) << reason << " : selected video codec "
               << vie_codec.plName << "/"
               << vie_codec.width << "x" << vie_codec.height << "x"
               << static_cast<int>(vie_codec.maxFramerate) << "fps"
               << "@" << vie_codec.maxBitrate << "kbps";
  if (webrtc::kVideoCodecVP8 == vie_codec.codecType) {
    LOG(LS_INFO) << "VP8 number of temporal layers: "
                 << static_cast<int>(
                    vie_codec.codecSpecific.VP8.numberOfTemporalLayers);
  }

}

bool WebRtcVideoMediaChannel::SetReceiveCodecs(int channel_id) {
  int red_type = -1;
  int fec_type = -1;
  for (std::vector<webrtc::VideoCodec>::iterator it = receive_codecs_.begin();
       it != receive_codecs_.end(); ++it) {
    if (it->codecType == webrtc::kVideoCodecRED) {
      red_type = it->plType;
    } else if (it->codecType == webrtc::kVideoCodecULPFEC) {
      fec_type = it->plType;
    }
    if (engine()->vie()->codec()->SetReceiveCodec(channel_id, *it) != 0) {
      LOG_RTCERR2(SetReceiveCodec, channel_id, it->plName);
      return false;
    }
  }

  // Enable video protection. For a sending channel, this will be taken care of
  // in SetSendCodecs.
  if (!IsDefaultChannel(channel_id)) {
    if (!SetNackFec(channel_id, red_type, fec_type)) {
      return false;
    }
  }

  // Start receiving packets if at least one receive codec has been set.
  if (!receive_codecs_.empty()) {
    if (engine()->vie()->base()->StartReceive(channel_id) != 0) {
      LOG_RTCERR1(StartReceive, channel_id);
      return false;
    }
  }
  return true;
}

int WebRtcVideoMediaChannel::GetRecvChannelNum(uint32 ssrc) {
  if (ssrc == first_receive_ssrc_) {
    return vie_channel_;
  }
  RecvChannelMap::iterator it = recv_channels_.find(ssrc);
  return (it != recv_channels_.end()) ? it->second->channel_id() : -1;
}

// If the new frame size is different from the send codec size we set on vie,
// we need to reset the send codec on vie.
// The new send codec size should not exceed send_codec_ which is controlled
// only by the 'jec' logic.
bool WebRtcVideoMediaChannel::MaybeResetVieSendCodec(
    WebRtcVideoChannelSendInfo* send_channel,
    int new_width,
    int new_height,
    bool is_screencast,
    bool* reset) {
  if (reset) {
    *reset = false;
  }

  if (!send_codec_) {
    return false;
  }
  webrtc::VideoCodec target_codec = *send_codec_.get();
  const VideoFormat& video_format = send_channel->video_format();
  UpdateVideoCodec(video_format, &target_codec);

  // Vie send codec size should not exceed target_codec.
  int target_width = new_width;
  int target_height = new_height;
  if (!is_screencast &&
      (new_width > target_codec.width || new_height > target_codec.height)) {
    target_width = target_codec.width;
    target_height = target_codec.height;
  }

  // Get current vie codec.
  webrtc::VideoCodec vie_codec;
  const int channel_id = send_channel->channel_id();
  if (engine()->vie()->codec()->GetSendCodec(channel_id, vie_codec) != 0) {
    LOG_RTCERR1(GetSendCodec, channel_id);
    return false;
  }
  const int cur_width = vie_codec.width;
  const int cur_height = vie_codec.height;

  // Only reset send codec when there is a size change. Additionally,
  // automatic resize needs to be turned of when screencasting and on when
  // not screencasting.
  // Don't allow automatic resizing for screencasting.
  bool automatic_resize = !is_screencast;
  // Turn off VP8 frame dropping when screensharing as the current model does
  // not work well at low fps.
  bool vp8_frame_dropping = !is_screencast;
  // Disable denoising for screencasting.
  bool enable_denoising =
      options_.video_noise_reduction.GetWithDefaultIfUnset(false);
  bool denoising = !is_screencast && enable_denoising;
  bool reset_send_codec =
      target_width != cur_width || target_height != cur_height ||
      automatic_resize != vie_codec.codecSpecific.VP8.automaticResizeOn ||
      denoising != vie_codec.codecSpecific.VP8.denoisingOn ||
      vp8_frame_dropping != vie_codec.codecSpecific.VP8.frameDroppingOn;

  if (reset_send_codec) {
    // Set the new codec on vie.
    vie_codec.width = target_width;
    vie_codec.height = target_height;
    vie_codec.maxFramerate = target_codec.maxFramerate;
    vie_codec.startBitrate = target_codec.startBitrate;
    vie_codec.codecSpecific.VP8.automaticResizeOn = automatic_resize;
    vie_codec.codecSpecific.VP8.denoisingOn = denoising;
    vie_codec.codecSpecific.VP8.frameDroppingOn = vp8_frame_dropping;

    // Make sure startBitrate is less or equal to maxBitrate;
    vie_codec.startBitrate = talk_base::_min(vie_codec.startBitrate,
                                             vie_codec.maxBitrate);

    if (engine()->vie()->codec()->SetSendCodec(channel_id, vie_codec) != 0) {
      LOG_RTCERR1(SetSendCodec, channel_id);
      return false;
    }
    if (reset) {
      *reset = true;
    }
    LogSendCodecChange("Capture size changed");
  }

  return true;
}

void WebRtcVideoMediaChannel::OnMessage(talk_base::Message* msg) {
  FlushBlackFrameData* black_frame_data =
      static_cast<FlushBlackFrameData*> (msg->pdata);
  FlushBlackFrame(black_frame_data->ssrc, black_frame_data->timestamp);
  delete black_frame_data;
}

int WebRtcVideoMediaChannel::SendPacket(int channel, const void* data,
                                        int len) {
  if (!network_interface_) {
    return -1;
  }
  talk_base::Buffer packet(data, len, kMaxRtpPacketLen);
  return network_interface_->SendPacket(&packet) ? len : -1;
}

int WebRtcVideoMediaChannel::SendRTCPPacket(int channel,
                                            const void* data,
                                            int len) {
  if (!network_interface_) {
    return -1;
  }
  talk_base::Buffer packet(data, len, kMaxRtpPacketLen);
  return network_interface_->SendRtcp(&packet) ? len : -1;
}

void WebRtcVideoMediaChannel::QueueBlackFrame(uint32 ssrc, int64 timestamp,
                                              int framerate) {
  if (timestamp) {
    FlushBlackFrameData* black_frame_data = new FlushBlackFrameData(
        ssrc,
        timestamp);
    const int delay_ms = static_cast<int> (
        2 * cricket::VideoFormat::FpsToInterval(framerate) *
        talk_base::kNumMillisecsPerSec / talk_base::kNumNanosecsPerSec);
    talk_base::Thread::Current()->PostDelayed(delay_ms, this, 0,
                                              black_frame_data);
  }
}

void WebRtcVideoMediaChannel::FlushBlackFrame(uint32 ssrc, int64 timestamp) {
  WebRtcVideoChannelSendInfo* send_channel = GetSendChannel(ssrc);
  if (!send_channel) {
    return;
  }
  talk_base::scoped_ptr<const VideoFrame> black_frame_ptr;

  int64 last_frame_time_stamp = send_channel->last_frame_time_stamp();
  if (last_frame_time_stamp == timestamp) {
    size_t last_frame_width = 0;
    size_t last_frame_height = 0;
    int64 last_frame_elapsed_time = 0;
    send_channel->GetLastFrameInfo(&last_frame_width, &last_frame_height,
                                   &last_frame_elapsed_time);
    if (!last_frame_width || !last_frame_height) {
      return;
    }
    WebRtcVideoFrame black_frame;
    // Black frame is not screencast.
    const bool screencasting = false;
    if (!black_frame.InitToBlack(send_codec_->width, send_codec_->height, 1, 1,
                                 last_frame_elapsed_time,
                                 last_frame_time_stamp) ||
        !SendFrame(send_channel, &black_frame, screencasting)) {
      LOG(LS_ERROR) << "Failed to send black frame.";
    }
  }
}

}  // namespace cricket

#endif  // HAVE_WEBRTC_VIDEO
