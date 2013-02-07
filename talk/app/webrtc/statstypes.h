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

// This file contains structures used for retrieving statistics from an ongoing
// libjingle session.

#ifndef TALK_APP_WEBRTC_STATSTYPES_H_
#define TALK_APP_WEBRTC_STATSTYPES_H_

#include <string>
#include <vector>

#include "talk/base/basictypes.h"
#include "talk/base/stringencode.h"

namespace webrtc {

// StatsElement contains a time stamped list of name/value pairs.
class StatsElement {
 public:
  struct Value {
    std::string name;
    std::string value;
  };

  StatsElement() : timestamp(0) { }

  void AddValue(const std::string& name, const std::string& value);
  void AddValue(const std::string& name, int64 value);

  double timestamp;  // Time since 1970-01-01T00:00:00Z in milliseconds.
  typedef std::vector<Value> Values;
  Values values;

  // StatsValue names
  static const char kStatsValueNameAudioOutputLevel[];
  static const char kStatsValueNameAudioInputLevel[];
  static const char kStatsValueNameBytesSent[];
  static const char kStatsValueNamePacketsSent[];
  static const char kStatsValueNameBytesReceived[];
  static const char kStatsValueNamePacketsReceived[];
  static const char kStatsValueNamePacketsLost[];

  // Internal StatsValue names
  static const char kStatsValueNameFirsReceived[];
  static const char kStatsValueNameFirsSent[];
  static const char kStatsValueNameFrameHeightReceived[];
  static const char kStatsValueNameFrameHeightSent[];
  static const char kStatsValueNameFrameRateReceived[];
  static const char kStatsValueNameFrameRateSent[];
  static const char kStatsValueNameFrameWidthReceived[];
  static const char kStatsValueNameFrameWidthSent[];
  static const char kStatsValueNameJitterReceived[];
  static const char kStatsValueNameNacksReceived[];
  static const char kStatsValueNameNacksSent[];
  static const char kStatsValueNameRtt[];
};

// StatsReport contains local and remote StatsElements that pertain to the same
// object, for instance a SSRC.
struct StatsReport {
  std::string id;  // SSRC in decimal for SSRCs
  std::string type;  // "SSRC" for SSRCs
  StatsElement local;  // Statistics gathered locally.
  StatsElement remote;  // Statistics received in a RTCP report.

  // StatsReport of |type| = "ssrc" is statistics for a specific rtp stream.
  // The |id| field is the SSRC in decimal form of the rtp stream.
  static const char kStatsReportTypeSsrc[];
};

typedef std::vector<StatsReport> StatsReports;

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_STATSTYPES_H_
