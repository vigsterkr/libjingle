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

#include "talk/session/phone/filemediaengine.h"

#include "talk/base/event.h"
#include "talk/base/logging.h"
#include "talk/base/pathutils.h"
#include "talk/base/stream.h"
#include "talk/session/phone/rtpdump.h"

namespace cricket {

///////////////////////////////////////////////////////////////////////////
// Implementation of FileMediaEngine.
///////////////////////////////////////////////////////////////////////////
int FileMediaEngine::GetCapabilities() {
  int capabilities = 0;
  if (!voice_input_filename_.empty()) {
    capabilities |= MediaEngine::AUDIO_SEND;
  }
  if (!voice_output_filename_.empty()) {
    capabilities |= MediaEngine::AUDIO_RECV;
  }
  if (!video_input_filename_.empty()) {
    capabilities |= MediaEngine::VIDEO_SEND;
  }
  if (!video_output_filename_.empty()) {
    capabilities |= MediaEngine::VIDEO_RECV;
  }
  return capabilities;
}

VoiceMediaChannel* FileMediaEngine::CreateChannel() {
  if (!voice_input_filename_.empty() || !voice_output_filename_.empty()) {
    return new FileVoiceChannel(voice_input_filename_, voice_output_filename_);
  } else {
    return NULL;
  }
}

VideoMediaChannel* FileMediaEngine::CreateVideoChannel(
    VoiceMediaChannel* voice_ch) {
  if (!video_input_filename_.empty() || !video_output_filename_.empty()) {
    return new FileVideoChannel(video_input_filename_, video_output_filename_);
  } else {
    return NULL;
  }
}

///////////////////////////////////////////////////////////////////////////
// Definition of RtpSenderReceiver.
///////////////////////////////////////////////////////////////////////////
class RtpSenderReceiver
    : public talk_base::Thread, public talk_base::MessageHandler {
 public:
  RtpSenderReceiver(MediaChannel* channel, const std::string& in_file,
                    const std::string& out_file);

  // Called by media channel. Context: media channel thread.
  bool SetSend(bool send);
  void OnPacketReceived(const void* data, int len);

  // Override virtual method of parent MessageHandler. Context: Worker Thread.
  virtual void OnMessage(talk_base::Message* pmsg);

 private:
  // Send a RTP packet to the network. The input parameter data points to the
  // start of the RTP packet and len is the packet size. Return true if the sent
  // size is equal to len.
  bool SendRtpPacket(const void* data, size_t len);

  MediaChannel* media_channel_;
  talk_base::scoped_ptr<talk_base::StreamInterface> input_stream_;
  talk_base::scoped_ptr<talk_base::StreamInterface> output_stream_;
  talk_base::scoped_ptr<RtpDumpLoopReader> rtp_dump_reader_;
  talk_base::scoped_ptr<RtpDumpWriter> rtp_dump_writer_;
  // RTP dump packet read from the input stream.
  RtpDumpPacket rtp_dump_packet_;
  bool sending_;
  bool first_packet_;

  DISALLOW_COPY_AND_ASSIGN(RtpSenderReceiver);
};

///////////////////////////////////////////////////////////////////////////
// Implementation of RtpSenderReceiver.
///////////////////////////////////////////////////////////////////////////
RtpSenderReceiver::RtpSenderReceiver(MediaChannel* channel,
                                     const std::string& in_file,
                                     const std::string& out_file)
    : media_channel_(channel),
      sending_(false),
      first_packet_(true) {
  input_stream_.reset(talk_base::Filesystem::OpenFile(
      talk_base::Pathname(in_file), "rb"));
  if (input_stream_.get()) {
    rtp_dump_reader_.reset(new RtpDumpLoopReader(input_stream_.get()));
    // Start the sender thread, which reads rtp dump records, waits based on
    // the record timestamps, and sends the RTP packets to the network.
    Thread::Start();
  }

  // Create a rtp dump writer for the output RTP dump stream.
  output_stream_.reset(talk_base::Filesystem::OpenFile(
      talk_base::Pathname(out_file), "wb"));
  if (output_stream_.get()) {
    rtp_dump_writer_.reset(new RtpDumpWriter(output_stream_.get()));
  }
}

bool RtpSenderReceiver::SetSend(bool send) {
  bool was_sending = sending_;
  sending_ = send;
  if (!was_sending && sending_) {
    PostDelayed(0, this);  // Wake up the send thread.
  }
  return true;
}

void RtpSenderReceiver::OnPacketReceived(const void* data, int len) {
  if (rtp_dump_writer_.get()) {
    rtp_dump_writer_->WriteRtpPacket(data, len);
  }
}

void RtpSenderReceiver::OnMessage(talk_base::Message* pmsg) {
  if (!sending_) {
    // If the sender thread is not sending, ignore this message. The thread goes
    // to sleep until SetSend(true) wakes it up.
    return;
  }

  uint32 prev_elapsed_time = 0xFFFFFFFF;
  if (!first_packet_) {
    prev_elapsed_time = rtp_dump_packet_.elapsed_time;
    SendRtpPacket(&rtp_dump_packet_.data[0], rtp_dump_packet_.data.size());
  } else {
    first_packet_ = false;
  }

  // Read a dump packet and wait for the elapsed time.
  if (talk_base::SR_SUCCESS ==
      rtp_dump_reader_->ReadPacket(&rtp_dump_packet_)) {
    int waiting_time_ms = rtp_dump_packet_.elapsed_time > prev_elapsed_time ?
        rtp_dump_packet_.elapsed_time - prev_elapsed_time : 0;
    PostDelayed(waiting_time_ms, this);
  } else {
    Quit();
  }
}

bool RtpSenderReceiver::SendRtpPacket(const void* data, size_t len) {
  if (!media_channel_ || !media_channel_->network_interface()) {
    return false;
  }

  return media_channel_->network_interface()->SendPacket(data, len) ==
        static_cast<int>(len);
}

///////////////////////////////////////////////////////////////////////////
// Implementation of FileVoiceChannel.
///////////////////////////////////////////////////////////////////////////
FileVoiceChannel::FileVoiceChannel(const std::string& in_file,
                                   const std::string& out_file)
    : rtp_sender_receiver_(new RtpSenderReceiver(this, in_file, out_file)) {
}

FileVoiceChannel::~FileVoiceChannel() {}

bool FileVoiceChannel::SetSendCodecs(const std::vector<AudioCodec>& codecs) {
  // TODO: Check the format of RTP dump input.
  return true;
}

bool FileVoiceChannel::SetSend(SendFlags flag) {
  return rtp_sender_receiver_->SetSend(flag != SEND_NOTHING);
}

void FileVoiceChannel::OnPacketReceived(const void* data, int len) {
  rtp_sender_receiver_->OnPacketReceived(data, len);
}

///////////////////////////////////////////////////////////////////////////
// Implementation of FileVideoChannel.
///////////////////////////////////////////////////////////////////////////
FileVideoChannel::FileVideoChannel(const std::string& in_file,
                                   const std::string& out_file)
    : rtp_sender_receiver_(new RtpSenderReceiver(this, in_file, out_file)) {
}

FileVideoChannel::~FileVideoChannel() {}

bool FileVideoChannel::SetSendCodecs(const std::vector<VideoCodec>& codecs) {
  // TODO: Check the format of RTP dump input.
  return true;
}

bool FileVideoChannel::SetSend(bool send) {
  return rtp_sender_receiver_->SetSend(send);
}

void FileVideoChannel::OnPacketReceived(const void* data, int len) {
  rtp_sender_receiver_->OnPacketReceived(data, len);
}

}  // namespace cricket
