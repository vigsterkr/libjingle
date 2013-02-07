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

#include "talk/app/webrtc/statscollector.h"

#include <utility>
#include <vector>

#include "talk/session/media/channel.h"

namespace webrtc {

const char StatsElement::kStatsValueNameAudioOutputLevel[] = "audioOutputLevel";
const char StatsElement::kStatsValueNameAudioInputLevel[] = "audioInputLevel";
const char StatsElement::kStatsValueNameBytesSent[] = "bytesSent";
const char StatsElement::kStatsValueNamePacketsSent[] = "packetsSent";
const char StatsElement::kStatsValueNameBytesReceived[] = "bytesReceived";
const char StatsElement::kStatsValueNamePacketsReceived[] = "packetsReceived";
const char StatsElement::kStatsValueNamePacketsLost[] = "packetsLost";

const char StatsElement::kStatsValueNameFirsReceived[] = "googFirsReceived";
const char StatsElement::kStatsValueNameFirsSent[] = "googFirsSent";
const char StatsElement::kStatsValueNameFrameHeightReceived[] =
    "googFrameHeightReceived";
const char StatsElement::kStatsValueNameFrameHeightSent[] =
    "googFrameHeightSent";
const char StatsElement::kStatsValueNameFrameRateReceived[] =
    "googFrameRateReceived";
const char StatsElement::kStatsValueNameFrameRateSent[] = "googFrameRateSent";
const char StatsElement::kStatsValueNameFrameWidthReceived[] =
    "googFrameWidthReceived";
const char StatsElement::kStatsValueNameFrameWidthSent[] = "googFrameWidthSent";
const char StatsElement::kStatsValueNameJitterReceived[] = "googJitterReceived";
const char StatsElement::kStatsValueNameNacksReceived[] = "googNacksReceived";
const char StatsElement::kStatsValueNameNacksSent[] = "googNacksSent";
const char StatsElement::kStatsValueNameRtt[] = "googRtt";

const char StatsReport::kStatsReportTypeSsrc[] = "ssrc";

// Implementations of functions in statstypes.h
void StatsElement::AddValue(const std::string& name, const std::string& value) {
  Value temp;
  temp.name = name;
  temp.value = value;
  values.push_back(temp);
}

void StatsElement::AddValue(const std::string& name, int64 value) {
  AddValue(name, talk_base::ToString<int64>(value));
}

namespace {

typedef std::map<std::string, webrtc::StatsReport> ReportsMap;

void AddEmptyReport(const std::string& label, ReportsMap* reports) {
  reports->insert(std::pair<std::string, webrtc::StatsReport>(
      label, webrtc::StatsReport()));
}

template <class TrackList>
void CreateTrackReports(TrackList* tracks, ReportsMap* reports) {
  for (size_t j = 0; j < tracks->count(); ++j) {
    webrtc::MediaStreamTrackInterface* track = tracks->at(j);
    // If there is no previous report for this track, add one.
    if (reports->find(track->id()) == reports->end()) {
      AddEmptyReport(track->id(), reports);
    }
  }
}

void ExtractStats(const cricket::VoiceReceiverInfo& info, StatsReport* report) {
  report->local.AddValue(StatsElement::kStatsValueNameAudioOutputLevel,
                         info.audio_level);
  report->local.AddValue(StatsElement::kStatsValueNameBytesReceived,
                         info.bytes_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNameJitterReceived,
                         info.jitter_ms);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsReceived,
                         info.packets_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsLost,
                         info.packets_lost);
}

void ExtractStats(const cricket::VoiceSenderInfo& info, StatsReport* report) {
  report->local.AddValue(StatsElement::kStatsValueNameAudioInputLevel,
                         info.audio_level);
  report->local.AddValue(StatsElement::kStatsValueNameBytesSent,
                         info.bytes_sent);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsSent,
                         info.packets_sent);

  // TODO(jiayl): Move the remote stuff into a separate function to extract them to
  // a different stats element for v2.
  report->remote.AddValue(StatsElement::kStatsValueNameJitterReceived,
                          info.jitter_ms);
  report->remote.AddValue(StatsElement::kStatsValueNameRtt, info.rtt_ms);
}

void ExtractStats(const cricket::VideoReceiverInfo& info, StatsReport* report) {
  report->local.AddValue(StatsElement::kStatsValueNameBytesReceived,
                         info.bytes_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsReceived,
                         info.packets_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsLost,
                         info.packets_lost);

  report->local.AddValue(StatsElement::kStatsValueNameFirsSent,
                         info.firs_sent);
  report->local.AddValue(StatsElement::kStatsValueNameNacksSent,
                         info.nacks_sent);
  report->local.AddValue(StatsElement::kStatsValueNameFrameWidthReceived,
                         info.frame_width);
  report->local.AddValue(StatsElement::kStatsValueNameFrameHeightReceived,
                         info.frame_height);
  report->local.AddValue(StatsElement::kStatsValueNameFrameRateReceived,
                         info.framerate_rcvd);
}

void ExtractStats(const cricket::VideoSenderInfo& info, StatsReport* report) {
  report->local.AddValue(StatsElement::kStatsValueNameBytesSent,
                         info.bytes_sent);
  report->local.AddValue(StatsElement::kStatsValueNamePacketsSent,
                         info.packets_sent);

  report->local.AddValue(StatsElement::kStatsValueNameFirsReceived,
                         info.firs_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNameNacksReceived,
                         info.nacks_rcvd);
  report->local.AddValue(StatsElement::kStatsValueNameFrameWidthSent,
                         info.frame_width);
  report->local.AddValue(StatsElement::kStatsValueNameFrameHeightSent,
                         info.frame_height);
  report->local.AddValue(StatsElement::kStatsValueNameFrameRateSent,
                         info.framerate_sent);

  // TODO(jiayl): Move the remote stuff into a separate function to extract them to
  // a different stats element for v2.
  report->remote.AddValue(StatsElement::kStatsValueNameRtt, info.rtt_ms);
}

uint32 ExtractSsrc(const cricket::VoiceReceiverInfo& info) {
  return info.ssrc;
}

uint32 ExtractSsrc(const cricket::VoiceSenderInfo& info) {
  return info.ssrc;
}

uint32 ExtractSsrc(const cricket::VideoReceiverInfo& info) {
  return info.ssrcs[0];
}

uint32 ExtractSsrc(const cricket::VideoSenderInfo& info) {
  return info.ssrcs[0];
}

// Template to extract stats from a data vector.
// ExtractSsrc and ExtractStats must be defined and overloaded for each type.
template<typename T>
void ExtractStatsFromList(const std::vector<T>& data,
                          StatsCollector* collector) {
  typename std::vector<T>::const_iterator it = data.begin();
  for (; it != data.end(); ++it) {
    std::string label;
    uint32 ssrc = ExtractSsrc(*it);
    if (!collector->session()->GetTrackIdBySsrc(ssrc, &label)) {
      LOG(LS_ERROR) << "The SSRC " << ssrc
                    << " is not associated with a track";
      continue;
    }
    StatsReport* report = collector->PrepareReport(label, ssrc);
    if (!report) {
      continue;
    }
    ExtractStats(*it, report);
  }
};

}  // namespace

StatsCollector::StatsCollector()
    : session_(NULL), stats_gathering_started_(0) {
}

// Adds a MediaStream with tracks that can be used as a |selector| in a call
// to GetStats.
void StatsCollector::AddStream(MediaStreamInterface* stream) {
  ASSERT(stream != NULL);

  CreateTrackReports<AudioTracks>(stream->audio_tracks(), &track_reports_);
  CreateTrackReports<VideoTracks>(stream->video_tracks(), &track_reports_);
}

bool StatsCollector::GetStats(MediaStreamTrackInterface* track,
                              StatsReports* reports) {
  ASSERT(reports != NULL);
  reports->clear();

  if (track) {
    ReportsMap::const_iterator it = track_reports_.find(track->id());
    if (it == track_reports_.end()) {
      LOG(LS_WARNING) << "No StatsReport is available for "<< track->id();
      return false;
    }
    reports->push_back(it->second);
    return true;
  }

  // If no selector given, add all Stats to |reports|.
  ReportsMap::const_iterator it = track_reports_.begin();
  for (; it != track_reports_.end(); ++it) {
    reports->push_back(it->second);
  }

  return true;
}

void StatsCollector::UpdateStats() {
  double time_now = GetTimeNow();
  // Calls to UpdateStats() that occur less than kMinGatherStatsPeriod number of
  // ms apart will be ignored.
  const double kMinGatherStatsPeriod = 50;
  if (stats_gathering_started_ + kMinGatherStatsPeriod > time_now) {
    return;
  }
  stats_gathering_started_ = time_now;

  if (session_) {
    ExtractVoiceInfo();
    ExtractVideoInfo();
  }
}

StatsReport* StatsCollector::PrepareReport(const std::string& label,
                                           uint32 ssrc) {
  std::map<std::string, webrtc::StatsReport>::iterator it =
      track_reports_.find(label);
  if (!VERIFY(it != track_reports_.end())) {
    return NULL;
  }

  StatsReport* report= &(it->second);

  report->id = talk_base::ToString<uint32>(ssrc);
  report->type = webrtc::StatsReport::kStatsReportTypeSsrc;

  // Clear out stats from previous GatherStats calls if any.
  if (report->local.timestamp != stats_gathering_started_) {
    report->local.values.clear();
    report->local.timestamp = stats_gathering_started_;
  }
  return report;
}

void StatsCollector::ExtractVoiceInfo() {
  if (!session_->voice_channel()) {
    return;
  }
  cricket::VoiceMediaInfo voice_info;
  if (!session_->voice_channel()->GetStats(&voice_info)) {
    LOG(LS_ERROR) << "Failed to get voice channel stats.";
    return;
  }
  ExtractStatsFromList(voice_info.receivers, this);
  ExtractStatsFromList(voice_info.senders, this);
}

void StatsCollector::ExtractVideoInfo() {
  if (!session_->video_channel()) {
    return;
  }
  cricket::VideoMediaInfo video_info;
  if (!session_->video_channel()->GetStats(&video_info)) {
    LOG(LS_ERROR) << "Failed to get video channel stats.";
    return;
  }
  ExtractStatsFromList(video_info.receivers, this);
  ExtractStatsFromList(video_info.senders, this);
}

double StatsCollector::GetTimeNow() {
  return timing_.WallTimeNow() * talk_base::kNumMillisecsPerSec;
}


}  // namespace webrtc
