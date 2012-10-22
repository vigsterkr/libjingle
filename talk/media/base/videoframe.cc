/*
 * libjingle
 * Copyright 2011 Google Inc.
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

#include "talk/media/base/videoframe.h"

#include <cstring>

#ifdef HAVE_YUV
#include "libyuv/compare.h"
#include "libyuv/planar_functions.h"
#include "libyuv/scale.h"
#endif

#include "talk/base/logging.h"
#include "talk/media/base/videocommon.h"

namespace cricket {

// Round to 2 pixels because Chroma channels are half size.
#define ROUNDTO2(v) (v & ~1)

talk_base::StreamResult VideoFrame::Write(talk_base::StreamInterface* stream,
                                          int* error) {
  talk_base::StreamResult result = talk_base::SR_SUCCESS;
  const uint8* in_y = GetYPlane();
  const uint8* in_u = GetUPlane();
  const uint8* in_v = GetVPlane();
  if (!in_y || !in_u || !in_v) {
    return result;  // Nothing to write.
  }
  const int32 y_pitch = GetYPitch();
  const int32 u_pitch = GetUPitch();
  const int32 v_pitch = GetVPitch();
  const size_t width = GetWidth();
  const size_t height = GetHeight();
  const size_t half_width = (width + 1) >> 1;
  const size_t half_height = (height + 1) >> 1;
  // Write Y.
  for (size_t row = 0; row < height; ++row) {
    result = stream->Write(in_y + row * y_pitch, width, NULL, error);
    if (result != talk_base::SR_SUCCESS) {
      return result;
    }
  }
  // Write U.
  for (size_t row = 0; row < half_height; ++row) {
    result = stream->Write(in_u + row * u_pitch, half_width, NULL, error);
    if (result != talk_base::SR_SUCCESS) {
      return result;
    }
  }
  // Write V.
  for (size_t row = 0; row < half_height; ++row) {
    result = stream->Write(in_v + row * v_pitch, half_width, NULL, error);
    if (result != talk_base::SR_SUCCESS) {
      return result;
    }
  }
  return result;
}

// TODO(fbarchard): Handle odd width/height with rounding.
void VideoFrame::StretchToPlanes(
    uint8* y, uint8* u, uint8* v,
    int32 dst_pitch_y, int32 dst_pitch_u, int32 dst_pitch_v,
    size_t width, size_t height, bool interpolate, bool vert_crop) const {
#ifdef HAVE_YUV
  if (!GetYPlane() || !GetUPlane() || !GetVPlane())
    return;

  const uint8* in_y = GetYPlane();
  const uint8* in_u = GetUPlane();
  const uint8* in_v = GetVPlane();
  int32 iwidth = GetWidth();
  int32 iheight = GetHeight();

  if (vert_crop) {
    // Adjust the input width:height ratio to be the same as the output ratio.
    if (iwidth * height > iheight * width) {
      // Reduce the input width, but keep size/position aligned for YuvScaler
      iwidth = ROUNDTO2(iheight * width / height);
      int32 iwidth_offset = ROUNDTO2((GetWidth() - iwidth) / 2);
      in_y += iwidth_offset;
      in_u += iwidth_offset / 2;
      in_v += iwidth_offset / 2;
    } else if (iwidth * height < iheight * width) {
      // Reduce the input height.
      iheight = iwidth * height / width;
      int32 iheight_offset = (GetHeight() - iheight) >> 2;
      iheight_offset <<= 1;  // Ensure that iheight_offset is even.
      in_y += iheight_offset * GetYPitch();
      in_u += iheight_offset / 2 * GetUPitch();
      in_v += iheight_offset / 2 * GetVPitch();
    }
  }

  // Scale to the output I420 frame.
  libyuv::Scale(in_y, in_u, in_v,
                GetYPitch(),
                GetUPitch(),
                GetVPitch(),
                iwidth, iheight,
                y, u, v, dst_pitch_y, dst_pitch_u, dst_pitch_v,
                width, height, interpolate);
#endif
}

size_t VideoFrame::StretchToBuffer(size_t w, size_t h,
                                   uint8* buffer, size_t size,
                                   bool interpolate, bool vert_crop) const {
  if (!buffer) return 0;

  size_t needed = SizeOf(w, h);
  if (needed <= size) {
    uint8* bufy = buffer;
    uint8* bufu = bufy + w * h;
    uint8* bufv = bufu + ((w + 1) >> 1) * ((h + 1) >> 1);
    StretchToPlanes(bufy, bufu, bufv, w, (w + 1) >> 1, (w + 1) >> 1, w, h,
                    interpolate, vert_crop);
  }
  return needed;
}

void VideoFrame::StretchToFrame(VideoFrame *target,
                                bool interpolate, bool vert_crop) const {
  if (!target) return;

  StretchToPlanes(target->GetYPlane(),
                  target->GetUPlane(),
                  target->GetVPlane(),
                  target->GetYPitch(),
                  target->GetUPitch(),
                  target->GetVPitch(),
                  target->GetWidth(),
                  target->GetHeight(),
                  interpolate, vert_crop);
  target->SetElapsedTime(GetElapsedTime());
  target->SetTimeStamp(GetTimeStamp());
}

VideoFrame* VideoFrame::Stretch(size_t w, size_t h,
                                bool interpolate, bool vert_crop) const {
  VideoFrame* dest = CreateEmptyFrame(w, h, GetPixelWidth(), GetPixelHeight(),
                                      GetElapsedTime(), GetTimeStamp());
  if (dest) {
    StretchToFrame(dest, interpolate, vert_crop);
  }
  return dest;
}

bool VideoFrame::SetToBlack() {
#ifdef HAVE_YUV
  return libyuv::I420Rect(GetYPlane(), GetYPitch(),
                          GetUPlane(), GetUPitch(),
                          GetVPlane(), GetVPitch(),
                          0, 0, GetWidth(), GetHeight(),
                          16, 128, 128) == 0;
#else
  int uv_size = GetUPitch() * GetChromaHeight();
  memset(GetYPlane(), 16, GetWidth() * GetHeight());
  memset(GetUPlane(), 128, uv_size);
  memset(GetVPlane(), 128, uv_size);
  return true;
#endif
}

static const size_t kMaxSampleSize = 1000000000u;
// Returns whether a sample is valid
bool VideoFrame::Validate(uint32 fourcc, int w, int h,
                          const uint8 *sample, size_t sample_size) {
  if (h < 0) {
    h = -h;
  }
  // 16384 is maximum resolution for VP8 codec.
  if (w < 1 || w > 16384 || h < 1 || h > 16384) {
    LOG(LS_ERROR) << "Invalid dimensions: " << w << "x" << h;
    return false;
  }
  uint32 format = CanonicalFourCC(fourcc);
  int expected_bpp = 8;
  switch (format) {
    case FOURCC_I400:
    case FOURCC_RGGB:
    case FOURCC_BGGR:
    case FOURCC_GRBG:
    case FOURCC_GBRG:
      expected_bpp = 8;
      break;
    case FOURCC_I420:
    case FOURCC_I411:
    case FOURCC_YU12:
    case FOURCC_YV12:
    case FOURCC_M420:
    case FOURCC_Q420:
    case FOURCC_NV21:
    case FOURCC_NV12:
      expected_bpp = 12;
      break;
    case FOURCC_I422:
    case FOURCC_YV16:
    case FOURCC_YUY2:
    case FOURCC_UYVY:
    case FOURCC_RGBP:
    case FOURCC_RGBO:
    case FOURCC_R444:
      expected_bpp = 16;
      break;
    case FOURCC_V210:
      expected_bpp = 22;  // 22.5 actually.
      break;
    case FOURCC_I444:
    case FOURCC_YV24:
    case FOURCC_24BG:
    case FOURCC_RAW:
      expected_bpp = 24;
      break;

    case FOURCC_ABGR:
    case FOURCC_BGRA:
    case FOURCC_ARGB:
      expected_bpp = 32;
      break;

    case FOURCC_MJPG:
    case FOURCC_H264:
      expected_bpp = 0;
      break;
    default:
      expected_bpp = 8;  // Expect format is at least 8 bits per pixel.
      break;
  }
  size_t expected_size = (w * expected_bpp + 7) / 8 * h;
  // For compressed formats, expect 4 bits per 16 x 16 macro.  I420 would be
  // 6 bits, but grey can be 4 bits.
  if (expected_bpp == 0) {
    expected_size = ((w + 15) / 16) * ((h + 15) / 16) * 4 / 8;
  }
  if (sample == NULL) {
    LOG(LS_ERROR) << "NULL sample pointer."
                  << " format: " << GetFourccName(format)
                  << " bpp: " << expected_bpp
                  << " size: " << w << "x" << h
                  << " expected: " << expected_size
                  << " " << sample_size;
    return false;
  }
  if (sample_size < expected_size) {
    LOG(LS_ERROR) << "Size field is too small."
                  << " format: " << GetFourccName(format)
                  << " bpp: " << expected_bpp
                  << " size: " << w << "x" << h
                  << " " << sample_size
                  << " expected: " << expected_size
                  << " sample[0..3]: " << static_cast<int>(sample[0])
                  << ", " << static_cast<int>(sample[1])
                  << ", " << static_cast<int>(sample[2])
                  << ", " << static_cast<int>(sample[3]);
    return false;
  }
  if (sample_size > kMaxSampleSize) {
    LOG(LS_WARNING) << "Size field is invalid."
                    << " format: " << GetFourccName(format)
                    << " bpp: " << expected_bpp
                    << " size: " << w << "x" << h
                    << " " << sample_size
                    << " expected: " << 2 * expected_size
                    << " sample[0..3]: " << static_cast<int>(sample[0])
                    << ", " << static_cast<int>(sample[1])
                    << ", " << static_cast<int>(sample[2])
                    << ", " << static_cast<int>(sample[3]);
    return false;
  }
  // Show large size warning once every 100 frames.
  static int large_warn100 = 0;
  size_t large_expected_size = expected_size * 2;
  if (expected_bpp >= 8 &&
      (sample_size > large_expected_size || sample_size > kMaxSampleSize) &&
      large_warn100 % 100 == 0) {
    ++large_warn100;
    LOG(LS_WARNING) << "Size field is too large."
                    << " format: " << GetFourccName(format)
                    << " bpp: " << expected_bpp
                    << " size: " << w << "x" << h
                    << " bytes: " << sample_size
                    << " expected: " << large_expected_size
                    << " sample[0..3]: " << static_cast<int>(sample[0])
                    << ", " << static_cast<int>(sample[1])
                    << ", " << static_cast<int>(sample[2])
                    << ", " << static_cast<int>(sample[3]);
  }
  // Scan pages to ensure they are there and don't contain a single value and
  // to generate an error.
  if (!memcmp(sample + sample_size - 8, sample + sample_size - 4, 4) &&
      !memcmp(sample, sample + 4, sample_size - 4)) {
    LOG(LS_WARNING) << "Duplicate value for all pixels."
                    << " format: " << GetFourccName(format)
                    << " bpp: " << expected_bpp
                    << " size: " << w << "x" << h
                    << " bytes: " << sample_size
                    << " expected: " << expected_size
                    << " sample[0..3]: " << static_cast<int>(sample[0])
                    << ", " << static_cast<int>(sample[1])
                    << ", " << static_cast<int>(sample[2])
                    << ", " << static_cast<int>(sample[3]);
  }

  static bool valid_once = true;
  if (valid_once) {
    valid_once = false;
    LOG(LS_INFO) << "Validate frame passed."
                 << " format: " << GetFourccName(format)
                 << " bpp: " << expected_bpp
                 << " size: " << w << "x" << h
                 << " bytes: " << sample_size
                 << " expected: " << expected_size
                 << " sample[0..3]: " << static_cast<int>(sample[0])
                 << ", " << static_cast<int>(sample[1])
                 << ", " << static_cast<int>(sample[2])
                 << ", " << static_cast<int>(sample[3]);
  }
  return true;
}

}  // namespace cricket
