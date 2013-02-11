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

#include "talk/app/webrtc/localvideosource.h"

#include <vector>

#include "talk/session/media/channelmanager.h"

using cricket::CaptureState;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaSourceInterface;

namespace webrtc {

// Constraint keys. Specified by draft-alvestrand-constraints-resolution-00b
// They are declared as static members in mediastreaminterface.h
const char MediaConstraintsInterface::kMinAspectRatio[] = "minAspectRatio";
const char MediaConstraintsInterface::kMaxAspectRatio[] = "maxAspectRatio";
const char MediaConstraintsInterface::kMaxWidth[] = "maxWidth";
const char MediaConstraintsInterface::kMinWidth[] = "minWidth";
const char MediaConstraintsInterface::kMaxHeight[] = "maxHeight";
const char MediaConstraintsInterface::kMinHeight[] = "minHeight";
const char MediaConstraintsInterface::kMaxFrameRate[] = "maxFrameRate";
const char MediaConstraintsInterface::kMinFrameRate[] = "minFrameRate";

// Google-specific keys
const char MediaConstraintsInterface::kNoiseReduction[] = "googNoiseReduction";
const char MediaConstraintsInterface::kLeakyBucket[] = "googLeakyBucket";

}  // namespace webrtc

namespace {

const double kRoundingTruncation = 0.0005;

enum {
  MSG_VIDEOCAPTURESTATECONNECT,
  MSG_VIDEOCAPTURESTATEDISCONNECT,
  MSG_VIDEOCAPTURESTATECHANGE,
};

// Default resolution. If no constraint is specified, this is the resolution we
// will use.
static const cricket::VideoFormatPod kDefaultResolution =
    {640, 480, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY};

// List of formats used if the camera don't support capability enumeration.
static const cricket::VideoFormatPod kVideoFormats[] = {
  {1280, 720, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY},
  {960, 720, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY},
  {640, 360, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY},
  {640, 480, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY},
  {320, 240, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY},
  {320, 180, FPS_TO_INTERVAL(30), cricket::FOURCC_ANY}
};

MediaSourceInterface::SourceState
GetReadyState(cricket::CaptureState state) {
  switch (state) {
    case cricket::CS_STARTING:
      return MediaSourceInterface::kInitializing;
    case cricket::CS_RUNNING:
      return MediaSourceInterface::kLive;
    case cricket::CS_FAILED:
    case cricket::CS_NO_DEVICE:
    case cricket::CS_STOPPED:
      return MediaSourceInterface::kEnded;
    case cricket::CS_PAUSED:
      return MediaSourceInterface::kMuted;
    default:
      ASSERT(!"GetReadyState unknown state");
  }
  return MediaSourceInterface::kEnded;
}

// Returns true if |constraint| is fulfilled. |format_out| can differ from
// |format_in| if the format is changed by the constraint. Ie - the frame rate
// can be changed by setting maxFrameRate.
bool NewFormatWithConstraints(
    const MediaConstraintsInterface::Constraint& constraint,
    const cricket::VideoFormat& format_in,
    cricket::VideoFormat* format_out) {
  ASSERT(format_out != NULL);
  *format_out = format_in;

  if (constraint.key == MediaConstraintsInterface::kMinWidth) {
    int value = talk_base::FromString<int>(constraint.value);
    return (value <= format_in.width);
  } else if (constraint.key == MediaConstraintsInterface::kMaxWidth) {
    int value = talk_base::FromString<int>(constraint.value);
    return (value >= format_in.width);
  } else if (constraint.key == MediaConstraintsInterface::kMinHeight) {
    int value = talk_base::FromString<int>(constraint.value);
    return (value <= format_in.height);
  } else if (constraint.key == MediaConstraintsInterface::kMaxHeight) {
    int value = talk_base::FromString<int>(constraint.value);
    return (value >= format_in.height);
  } else if (constraint.key == MediaConstraintsInterface::kMinFrameRate) {
    int value = talk_base::FromString<int>(constraint.value);
    return (value <= cricket::VideoFormat::IntervalToFps(format_in.interval));
  } else if (constraint.key == MediaConstraintsInterface::kMaxFrameRate) {
    int value = talk_base::FromString<int>(constraint.value);
    if (value <= cricket::VideoFormat::IntervalToFps(format_in.interval)) {
      format_out->interval = cricket::VideoFormat::FpsToInterval(value);
      return true;
    } else {
      return false;
    }
  } else if (constraint.key == MediaConstraintsInterface::kMinAspectRatio) {
    double value = talk_base::FromString<double>(constraint.value);
    // The aspect ratio in |constraint.value| has been converted to a string and
    // back to a double, so it may have a rounding error.
    // E.g if the value 1/3 is converted to a string, the string will not have
    // infinite length.
    // We add a margin of 0.0005 which is high enough to detect the same aspect
    // ratio but small enough to avoid matching wrong aspect ratios.
    double ratio = static_cast<double>(format_in.width) / format_in.height;
    return  (value <= ratio + kRoundingTruncation);
  } else if (constraint.key == MediaConstraintsInterface::kMaxAspectRatio) {
    double value = talk_base::FromString<double>(constraint.value);
    double ratio = static_cast<double>(format_in.width) / format_in.height;
    // Subtract 0.0005 to avoid rounding problems. Same as above.
    const double kRoundingTruncation = 0.0005;
    return  (value >= ratio - kRoundingTruncation);
  } else if (constraint.key == MediaConstraintsInterface::kNoiseReduction ||
             constraint.key == MediaConstraintsInterface::kLeakyBucket) {
    // These are actually options, not constraints, so they can be satisfied
    // regardless of the format.
    return true;
  }
  LOG(LS_WARNING) << "Found unknown MediaStream constraint. Name:"
      <<  constraint.key << " Value:" << constraint.value;
  return false;
}

// Removes cricket::VideoFormats from |formats| that don't meet |constraint|.
void FilterFormatsByConstraint(
    const MediaConstraintsInterface::Constraint& constraint,
    std::vector<cricket::VideoFormat>* formats) {
  std::vector<cricket::VideoFormat>::iterator format_it =
      formats->begin();
  while (format_it != formats->end()) {
    // Modify the format_it to fulfill the constraint if possible.
    // Delete it otherwise.
    if (!NewFormatWithConstraints(constraint, (*format_it), &(*format_it))) {
      format_it = formats->erase(format_it);
    } else {
      ++format_it;
    }
  }
}

// Returns a vector of cricket::VideoFormat that best match |constraints|.
std::vector<cricket::VideoFormat> FilterFormats(
    const MediaConstraintsInterface::Constraints& mandatory,
    const MediaConstraintsInterface::Constraints& optional,
    const std::vector<cricket::VideoFormat>& supported_formats) {
  typedef MediaConstraintsInterface::Constraints::const_iterator
      ConstraintsIterator;
  std::vector<cricket::VideoFormat> candidates = supported_formats;

  for (ConstraintsIterator constraints_it = mandatory.begin();
       constraints_it != mandatory.end(); ++constraints_it)
    FilterFormatsByConstraint(*constraints_it, &candidates);

  if (candidates.size() == 0)
    return candidates;

  // Ok - all mandatory checked and we still have a candidate.
  // Let's try filtering using the optional constraints.
  for (ConstraintsIterator  constraints_it = optional.begin();
       constraints_it != optional.end(); ++constraints_it) {
    std::vector<cricket::VideoFormat> current_candidates = candidates;
    FilterFormatsByConstraint(*constraints_it, &current_candidates);
    if (current_candidates.size() > 0) {
      candidates = current_candidates;
    }
  }

  // We have done as good as we can to filter the supported resolutions.
  return candidates;
}

// Find the format that best matches the default video size.
// Constraints are optional and since the performance of a video call
// might be bad due to bitrate limitations, CPU, and camera performance,
// it is better to select a resolution that is as close as possible to our
// default and still meets the contraints.
const cricket::VideoFormat& GetBestCaptureFormat(
    const std::vector<cricket::VideoFormat>& formats) {
  ASSERT(formats.size() > 0);

  int default_area = kDefaultResolution.width * kDefaultResolution.height;

  std::vector<cricket::VideoFormat>::const_iterator it = formats.begin();
  std::vector<cricket::VideoFormat>::const_iterator best_it = formats.begin();
  int best_diff = abs(default_area - it->width* it->height);
  for (; it != formats.end(); ++it) {
    int diff = abs(default_area - it->width* it->height);
    if (diff < best_diff) {
      best_diff = diff;
      best_it = it;
    }
  }
  return *best_it;
}

// Convert constraint value to a boolean. Return false if the value
// is invalid.
bool FromConstraint(const std::string& value, bool* output) {
  if (value == MediaConstraintsInterface::kValueTrue)
    *output = true;
  else if (value == MediaConstraintsInterface::kValueFalse)
    *output = false;
  else
    return false;
  return true;
}

// Search the constraints for video options.  Apply all options that are found
// with valid values, and return false if any video option was found with an
// invalid value.
bool FromConstraints(const MediaConstraintsInterface::Constraints& constraints,
                     cricket::VideoOptions* options) {
  MediaConstraintsInterface::Constraints::const_iterator iter;
  bool all_valid = true;

  for (iter = constraints.begin(); iter != constraints.end(); ++iter) {
    bool value = false;
    bool got_value = FromConstraint(iter->value, &value);
    bool is_option = true;

    if (iter->key == MediaConstraintsInterface::kNoiseReduction) {
      if (got_value)
        options->video_noise_reduction.Set(value);
    } else if (iter->key == MediaConstraintsInterface::kLeakyBucket) {
      if (got_value)
        options->video_leaky_bucket.Set(value);
    } else {
      is_option = false;
    }

    if (is_option && !got_value) {
      LOG(LS_WARNING) << "Option " << iter->key << " has unexpected value " <<
          iter->value;
      all_valid = false;
    }
  }
  return all_valid;
}

}  // anonymous namespace

namespace webrtc {

talk_base::scoped_refptr<LocalVideoSource> LocalVideoSource::Create(
    cricket::ChannelManager* channel_manager,
    cricket::VideoCapturer* capturer,
    const webrtc::MediaConstraintsInterface* constraints) {
  ASSERT(channel_manager != NULL);
  ASSERT(capturer != NULL);
  talk_base::scoped_refptr<LocalVideoSource> source(
      new talk_base::RefCountedObject<LocalVideoSource>(channel_manager,
                                                        capturer));
  source->Initialize(constraints);
  return source;
}

LocalVideoSource::LocalVideoSource(cricket::ChannelManager* channel_manager,
                                   cricket::VideoCapturer* capturer)
    : channel_manager_(channel_manager),
      video_capturer_(capturer),
      state_(kInitializing) {
  channel_manager_->SignalVideoCaptureStateChange.connect(
      this, &LocalVideoSource::OnStateChange);
}

LocalVideoSource::~LocalVideoSource() {
  channel_manager_->StopVideoCapture(video_capturer_.get(), format_);
  channel_manager_->SignalVideoCaptureStateChange.disconnect(this);
}

void LocalVideoSource::Initialize(
    const webrtc::MediaConstraintsInterface* constraints) {

  std::vector<cricket::VideoFormat> formats;
  if (video_capturer_->GetSupportedFormats() &&
      video_capturer_->GetSupportedFormats()->size() > 0) {
    formats = *video_capturer_->GetSupportedFormats();
  } else {
    // The VideoCapturer implementation doesn't support capability enumeration.
    // We need to guess what the camera support.
    for (int i = 0; i < ARRAY_SIZE(kVideoFormats); ++i) {
      formats.push_back(cricket::VideoFormat(kVideoFormats[i]));
    }
  }

  if (constraints) {
    MediaConstraintsInterface::Constraints mandatory_constraints =
        constraints->GetMandatory();
    MediaConstraintsInterface::Constraints optional_constraints;
    optional_constraints = constraints->GetOptional();
    formats = FilterFormats(mandatory_constraints, optional_constraints,
                            formats);

    if (formats.size() > 0) {
      cricket::VideoOptions options;
      // Apply optional options first.
      // They will be overwritten by mandatory options.
      FromConstraints(optional_constraints, &options);

      if (!FromConstraints(mandatory_constraints, &options)) {
        LOG(LS_WARNING) << "Could not satisfy mandatory options.";
        SetState(kEnded);
        return;
      }
      options_.SetAll(options);
    }
  }

  if (formats.size() == 0) {
    LOG(LS_WARNING) << "Failed to find a suitable video format.";
    SetState(kEnded);
    return;
  }

  format_ = GetBestCaptureFormat(formats);
  // Start the camera with our best guess.
  // TODO(perkj): Should we try again with another format it it turns out that
  // the camera doesn't produce frames with the correct format? Or will
  // cricket::VideCapturer be able to re-scale / crop to the requested
  // resolution?
  if (!channel_manager_->StartVideoCapture(video_capturer_.get(), format_)) {
    SetState(kEnded);
    return;
  }
  // Initialize hasn't succeeded until a successful state change has occurred.
}

void LocalVideoSource::AddSink(cricket::VideoRenderer* output) {
  channel_manager_->AddVideoRenderer(video_capturer_.get(), output);
}

void LocalVideoSource::RemoveSink(cricket::VideoRenderer* output) {
  channel_manager_->RemoveVideoRenderer(video_capturer_.get(), output);
}

// OnStateChange listens to the ChannelManager::SignalVideoCaptureStateChange.
// This signal is triggered for all video capturers. Not only the one we are
// interested in.
void LocalVideoSource::OnStateChange(cricket::VideoCapturer* capturer,
                                     cricket::CaptureState capture_state) {
  if (capturer == video_capturer_.get()) {
    SetState(GetReadyState(capture_state));
  }
}

void LocalVideoSource::SetState(SourceState new_state) {
  if (VERIFY(state_ != new_state)) {
    state_ = new_state;
    FireOnChanged();
  }
}

}  // namespace webrtc
