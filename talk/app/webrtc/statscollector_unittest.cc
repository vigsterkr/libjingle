/*
 * libjingle
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

#include <stdio.h>

#include "talk/app/webrtc/statscollector.h"

#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/base/gunit.h"
#include "talk/media/base/fakemediaengine.h"
#include "talk/media/devices/fakedevicemanager.h"
#include "talk/p2p/base/fakesession.h"
#include "talk/session/media/channelmanager.h"
#include "testing/base/public/gmock.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace cricket {

class ChannelManager;
class FakeDeviceManager;

}  // namespace cricket

namespace {

class MockWebRtcSession : public webrtc::WebRtcSession {
 public:
  explicit MockWebRtcSession(cricket::ChannelManager* channel_manager)
    : WebRtcSession(channel_manager, talk_base::Thread::Current(),
                    NULL, NULL, NULL) {
  }
  MOCK_METHOD0(video_channel, cricket::VideoChannel*());
  MOCK_METHOD2(GetTrackIdBySsrc, bool(uint32, std::string*));
};

class MockVideoMediaChannel : public cricket::FakeVideoMediaChannel {
 public:
  MockVideoMediaChannel()
    : cricket::FakeVideoMediaChannel(NULL) {
  }
  MOCK_METHOD1(GetStats, bool(cricket::VideoMediaInfo*));
};

std::string ExtractStatsValue(webrtc::StatsReports reports,
                              const std::string name) {
  if (reports.empty()) {
    return "NO REPORTS";
  }
  webrtc::StatsElement::Values::const_iterator it =
    reports[0].local.values.begin();
  for (; it != reports[0].local.values.end(); ++it) {
    if (it->name == name) {
      return it->value;
    }
  }
  return "NOT FOUND";
}

// This test verifies that 64-bit counters are handled by truncation when
// they pass the 32-bit possible values.
// It documents existing behavior, it does not recommend it.
TEST(StatsCollector, BytesCounterHandles64Bits) {
  webrtc::StatsCollector stats;  // Implementation under test.
  cricket::FakeMediaEngine* media_engine = new cricket::FakeMediaEngine;
  // The media_engine is owned by the channel_manager.
  talk_base::scoped_ptr<cricket::ChannelManager> channel_manager(
      new cricket::ChannelManager(media_engine,
                                  new cricket::FakeDeviceManager(),
                                  talk_base::Thread::Current()));
  MockWebRtcSession session(channel_manager.get());
  MockVideoMediaChannel* media_channel = new MockVideoMediaChannel;
  cricket::VideoChannel video_channel(talk_base::Thread::Current(),
      media_engine, media_channel, &session, "", false, NULL);
  webrtc::StatsReports reports;  // returned values.
  cricket::VideoSenderInfo video_sender_info;
  cricket::VideoMediaInfo stats_read;
  const uint32 kSsrcOfTrack = 1234;
  const std::string kNameOfTrack("somename");
  // The number of bytes must be larger than 0xFFFFFFFF for this test.
  const int64 kBytesSent = 12345678901234LL;
  const std::string kBytesSentString("12345678901234");

  stats.set_session(&session);
  talk_base::scoped_refptr<webrtc::MediaStream> stream(
      webrtc::MediaStream::Create("streamlabel"));
  stream->AddTrack(webrtc::VideoTrack::Create(kNameOfTrack, NULL));
  stats.AddStream(stream);

  // Construct a stats value to read.
  video_sender_info.ssrcs.push_back(1234);
  video_sender_info.bytes_sent = kBytesSent;
  stats_read.senders.push_back(video_sender_info);

  EXPECT_CALL(session, video_channel())
    .WillRepeatedly(Return(&video_channel));
  EXPECT_CALL(*media_channel, GetStats(_))
    .WillOnce(DoAll(SetArgPointee<0>(stats_read),
                    Return(true)));
  EXPECT_CALL(session, GetTrackIdBySsrc(kSsrcOfTrack, _))
    .WillOnce(DoAll(SetArgPointee<1>(kNameOfTrack),
                    Return(true)));
  stats.UpdateStats();
  stats.GetStats(NULL, &reports);
  std::string result = ExtractStatsValue(reports, "bytesSent");
  EXPECT_EQ(kBytesSentString, result);
}

}  // namespace
