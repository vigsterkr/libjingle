// libjingle
// Copyright 2004--2005, Google Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//  3. The name of the author may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Common definition for video, including fourcc and VideoFormat

#ifndef TALK_SESSION_PHONE_VIDEOCOMMON_H_
#define TALK_SESSION_PHONE_VIDEOCOMMON_H_

#include <string>

#include "talk/base/basictypes.h"

namespace cricket {

//////////////////////////////////////////////////////////////////////////////
// Definition of fourcc.
//////////////////////////////////////////////////////////////////////////////
// Convert four characters to a fourcc code.
// Needs to be a macro otherwise the OS X compiler complains when the kFormat*
// constants are used in a switch.
#define FOURCC(a, b, c, d) (\
    (static_cast<uint32>(a)) | (static_cast<uint32>(b) << 8) | \
    (static_cast<uint32>(c) << 16) | (static_cast<uint32>(d) << 24))

// Get the name, that is, string with four characters, of a fourcc code.
inline std::string GetFourccName(uint32 fourcc) {
  std::string name;
  name.push_back(static_cast<char>(fourcc & 0xFF));
  name.push_back(static_cast<char>((fourcc >> 8) & 0xFF));
  name.push_back(static_cast<char>((fourcc >> 16) & 0xFF));
  name.push_back(static_cast<char>((fourcc >> 24) & 0xFF));
  return name;
}

// FourCC codes used in Google Talk.
// Some good pages discussing FourCC codes:
//   http://developer.apple.com/quicktime/icefloe/dispatch020.html
//   http://www.fourcc.org/yuv.php
enum FourCC {
  // Canonical fourccs used in our code.
  FOURCC_I420 = FOURCC('I', '4', '2', '0'),
  FOURCC_YUY2 = FOURCC('Y', 'U', 'Y', '2'),
  FOURCC_UYVY = FOURCC('U', 'Y', 'V', 'Y'),
  FOURCC_24BG = FOURCC('2', '4', 'B', 'G'),
  FOURCC_RGBA = FOURCC('R', 'G', 'B', 'A'),
  FOURCC_BGRA = FOURCC('B', 'G', 'R', 'A'),
  FOURCC_ARGB = FOURCC('A', 'R', 'G', 'B'),
  FOURCC_MJPG = FOURCC('M', 'J', 'P', 'G'),
  FOURCC_JPEG = FOURCC('J', 'P', 'E', 'G'),
  FOURCC_RAW  = FOURCC('r', 'a', 'w', ' '),
  // Next five are Bayer RGB formats. The four characters define the order of
  // the colours in each 2x2 pixel grid, going left-to-right and top-to-bottom.
  FOURCC_RGGB = FOURCC('R', 'G', 'G', 'B'),
  FOURCC_BGGR = FOURCC('B', 'G', 'G', 'R'),
  FOURCC_GRBG = FOURCC('G', 'R', 'B', 'G'),
  FOURCC_GBRG = FOURCC('G', 'B', 'R', 'G'),

  // Aliases for canonical fourccs, replaced with their canonical equivalents by
  // CanonicalFourCC().
  FOURCC_IYUV = FOURCC('I', 'Y', 'U', 'V'),  // Alias for I420
  FOURCC_YU12 = FOURCC('Y', 'U', '1', '2'),  // Alias for I420
  FOURCC_YUYV = FOURCC('Y', 'U', 'Y', 'V'),  // Alias for YUY2
  FOURCC_YUVS = FOURCC('y', 'u', 'v', 's'),  // Alias for YUY2
  FOURCC_HDYC = FOURCC('H', 'D', 'Y', 'C'),  // Alias for UYVY
  FOURCC_2VUY = FOURCC('2', 'v', 'u', 'y'),  // Alias for UYVY
  FOURCC_RGB1 = FOURCC('R', 'G', 'B', '1'),  // Alias for ABGR
  FOURCC_RGB2 = FOURCC('R', 'G', 'B', '2'),  // Alias for BGRA
  FOURCC_BA81 = FOURCC('B', 'A', '8', '1'),  // Alias for BGGR

  // Match any fourcc.
  FOURCC_ANY  = 0xFFFFFFFF,
};

// Converts fourcc aliases into canonical ones.
uint32 CanonicalFourCC(uint32 fourcc);

//////////////////////////////////////////////////////////////////////////////
// Definition of VideoFormat.
//////////////////////////////////////////////////////////////////////////////

static const int64 kNumNanosecsPerSec = 1000000000;

struct VideoFormat {
  static const int64 kMinimumInterval = kNumNanosecsPerSec / 10000;  // 10k fps

  VideoFormat() : width(0), height(0), interval(0), fourcc(0) {}

  VideoFormat(int w, int h, int64 interval_ns, uint32 cc)
      : width(w),
        height(h),
        interval(interval_ns),
        fourcc(cc) {
  }

  VideoFormat(const VideoFormat& format)
      : width(format.width),
        height(format.height),
        interval(format.interval),
        fourcc(format.fourcc) {
  }

  static int64 FpsToInterval(int fps) {
    return fps ? kNumNanosecsPerSec / fps : kMinimumInterval;
  }

  static int IntervalToFps(int64 interval) {
    // Normalize the interval first.
    interval = talk_base::_max(interval, kMinimumInterval);
    return static_cast<int>(kNumNanosecsPerSec / interval);
  }

  bool operator==(const VideoFormat& format) const {
    return width == format.width && height == format.height &&
        interval == format.interval && fourcc == format.fourcc;
  }

  bool operator!=(const VideoFormat& format) const {
    return !(*this == format);
  }

  bool operator<(const VideoFormat& format) const {
    return (fourcc < format.fourcc) ||
        (fourcc == format.fourcc && width < format.width) ||
        (fourcc == format.fourcc && width == format.width &&
            height < format.height) ||
        (fourcc == format.fourcc && width == format.width &&
            height == format.height && interval > format.interval);
  }

  int framerate() const { return IntervalToFps(interval); }

  int    width;     // in number of pixels
  int    height;    // in number of pixels
  int64  interval;  // in nanoseconds
  uint32 fourcc;    // color space. FOURCC_ANY means that any color space is OK.
};

// Result of video capturer start.
enum CaptureResult {
  CR_SUCCESS,    // The capturer starts successfully.
  CR_PENDING,    // The capturer is pending to start the capture device.
  CR_FAILURE,    // The capturer fails to start.
  CR_NO_DEVICE,  // The capturer has no device and fails to start.
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_VIDEOCOMMON_H_
