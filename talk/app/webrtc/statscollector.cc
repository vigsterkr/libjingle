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

const char StatsReport::kStatsReportTypeSsrc[] = "ssrc";

}  // namespace webrtc

namespace {

typedef std::map<std::string, webrtc::StatsReport> ReportsMap;

void AddEmptyReport(const std::string& name, ReportsMap* reports) {
  reports->insert(std::pair<std::string, webrtc::StatsReport>(
      name, webrtc::StatsReport()));
}

template <class TrackList>
void CreateTrackReports(TrackList* tracks, ReportsMap* reports) {
  for (size_t j = 0; j < tracks->count(); ++j) {
    webrtc::MediaStreamTrackInterface* track = tracks->at(j);
    // If there is no previous report for this track, add one.
    if (reports->find(track->label()) == reports->end()) {
      AddEmptyReport(track->label(), reports);
    }
  }
}

}  // namespace

namespace webrtc {

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
    ReportsMap::const_iterator it = track_reports_.find(track->label());
    if (it == track_reports_.end()) {
      LOG(LS_WARNING) << "No StatsReport is available for "<< track->label();
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

  if (session_ && session_->voice_channel())  {
    GetRemoteAudioTrackStats();
  }
}

StatsReport* StatsCollector::PrepareReport(const std::string& name,
                                           uint32 ssrc) {
  std::map<std::string, webrtc::StatsReport>::iterator it =
      track_reports_.find(name);
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

void StatsCollector::GetRemoteAudioTrackStats() {
  cricket::VoiceMediaInfo voice_info;
  if (!session_->voice_channel()->GetStats(&voice_info)) {
    LOG(LS_ERROR) << "Failed to get voice channel stats.";
    return;
  }

  std::vector<cricket::VoiceReceiverInfo>::const_iterator it =
      voice_info.receivers.begin();
  // For each receiver:
  for (; it != voice_info.receivers.end(); ++it) {
    std::string name;
    // MSID label is used as object names for remote tracks.

    if (!session_->GetRemoteTrackName(it->ssrc, &name)) {
      LOG(LS_ERROR) << "The SSRC is not associated with an audio track.";
      continue;
    }
    StatsReport* report = PrepareReport(name, it->ssrc);
    if (!report) {
      continue;
    }

    StatsElement::Value stat;
    stat.name = StatsElement::kStatsValueNameAudioOutputLevel;
    stat.value = talk_base::ToString<int>(it->audio_level);
    report->local.values.push_back(stat);
  }
}

double StatsCollector::GetTimeNow() {
  return timing_.WallTimeNow() * talk_base::kNumMillisecsPerSec;
}


}  // namespace webrtc
