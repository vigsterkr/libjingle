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

#include "talk/session/phone/ssrcmuxfilter.h"

#include <algorithm>

#include "talk/base/logging.h"
#include "talk/session/phone/rtputils.h"

namespace {

using cricket::StreamParams;

// TODO: Remove this function once cl 25538785 is landed.
bool GetStreamBySsrc(const std::vector<StreamParams>& streams, uint32 ssrc,
                     StreamParams* stream_out) {
  for (std::vector<StreamParams>::const_iterator stream = streams.begin();
       stream != streams.end(); ++stream) {
    if (std::find(stream->ssrcs.begin(), stream->ssrcs.end(),
                  ssrc) != stream->ssrcs.end()) {
      if (stream_out != NULL) {
        *stream_out = *stream;
      }
      return true;
    }
  }
  return false;
}

// TODO: Remove this function once cl 25538785 is landed.
bool RemoveStreamBySsrc(std::vector<StreamParams>* streams, uint32 ssrc) {
  bool ret = false;
  for (std::vector<StreamParams>::iterator stream = streams->begin();
      stream != streams->end(); ) {
    if (std::find(stream->ssrcs.begin(), stream->ssrcs.end(),
                  ssrc) != stream->ssrcs.end()) {
      stream = streams->erase(stream);
      ret = true;
    } else {
      ++stream;
    }
  }
  return ret;
}

}  // namespace

namespace cricket {

static const uint32 kSsrc01 = 0x01;

SsrcMuxFilter::SsrcMuxFilter() {
}

SsrcMuxFilter::~SsrcMuxFilter() {
}

bool SsrcMuxFilter::IsActive() const {
  return !streams_.empty();
}

bool SsrcMuxFilter::DemuxPacket(const char* data, size_t len, bool rtcp) {
  uint32 ssrc = 0;
  if (!rtcp) {
    GetRtpSsrc(data, len, &ssrc);
  } else {
    int pl_type = 0;
    if (!GetRtcpType(data, len, &pl_type)) return false;
    if (pl_type == kRtcpTypeSDES) {
      // SDES packet parsing not supported.
      LOG(LS_INFO) << "SDES packet received for demux.";
      return true;
    } else {
      if (!GetRtcpSsrc(data, len, &ssrc)) return false;
      if (ssrc == kSsrc01) {
        // SSRC 1 has a special meaning and indicates generic feedback on
        // some systems and should never be dropped.  If it is forwarded
        // incorrectly it will be ignored by lower layers anyway.
        return true;
      }
    }
  }
  return FindStream(ssrc);
}

bool SsrcMuxFilter::AddStream(const StreamParams& stream) {
  if (GetStreamBySsrc(streams_, stream.first_ssrc(), NULL)) {
      LOG(LS_WARNING) << "Stream already added to filter";
      return false;
  }
  streams_.push_back(stream);
  return true;
}

bool SsrcMuxFilter::RemoveStream(uint32 ssrc) {
  return RemoveStreamBySsrc(&streams_, ssrc);
}

bool SsrcMuxFilter::FindStream(uint32 ssrc) const {
  return (GetStreamBySsrc(streams_, ssrc, NULL));
}

}  // namespace cricket
