/*
 * Jingle call example
 * Copyright 2004--2005, Google Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// LinphoneMediaEngine is a Linphone implementation of MediaEngine
extern "C" {
#include "talk/third_party/mediastreamer/mediastream.h"
#ifdef HAVE_ILBC
#include "talk/third_party/mediastreamer/msilbcdec.h"
#endif
#ifdef HAVE_SPEEX
#include "talk/third_party/mediastreamer/msspeexdec.h"
#endif
}
#include <ortp/ortp.h>
#include <ortp/telephonyevents.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include "talk/base/logging.h"
#include "talk/base/thread.h"
#include "talk/session/phone/codec.h"
#include "talk/session/phone/linphonemediaengine.h"

using namespace cricket;

void LinphoneMediaChannel::OnIncomingData(talk_base::AsyncSocket *s)
{
  char *buf[2048];
  int len;
  len = s->Recv(buf, sizeof(buf));
  if (network_interface_ && !mute_)
    network_interface_->SendPacket(buf, len);
}

LinphoneMediaChannel::LinphoneMediaChannel(LinphoneMediaEngine*eng) :
  pt_(-1),
  audio_stream_(0),
  engine_(eng) {
  
  talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
  talk_base::SocketServer *ss = thread->socketserver();
  socket_.reset(ss->CreateAsyncSocket(SOCK_DGRAM));
  
  socket_->Bind(talk_base::SocketAddress("localhost",3000));
  socket_->SignalReadEvent.connect(this, &LinphoneMediaChannel::OnIncomingData);
}

LinphoneMediaChannel::~LinphoneMediaChannel() {
  audio_stream_stop(audio_stream_);
}

void LinphoneMediaChannel::SetCodecs(const std::vector<Codec> &codecs) {
 bool first = true;
 std::vector<Codec>::const_iterator i;

  for (i = codecs.begin(); i < codecs.end(); i++) {

    if (!engine_->FindCodec(*i))
      continue;
#ifdef HAVE_ILBC	
    if (i->name == payload_type_ilbc.mime_type) {
      rtp_profile_set_payload(&av_profile, i->id, &payload_type_ilbc);
    } 
#endif
#ifdef HAVE_SPEEX
    if (i->name == speex_wb.mime_type && i->clockrate == speex_wb.clock_rate) {
      rtp_profile_set_payload(&av_profile, i->id, &speex_wb);
    } else if (i->name == speex_nb.mime_type && i->clockrate == speex_nb.clock_rate) {
      rtp_profile_set_payload(&av_profile, i->id, &speex_nb);
    }
#endif

    if (i->id == 0)
      rtp_profile_set_payload(&av_profile, 0, &pcmu8000);

    if (i->name == telephone_event.mime_type) {
      rtp_profile_set_payload(&av_profile, i->id, &telephone_event);
    }
    
    if (first) {
      LOG(LS_INFO) << "Using " << i->name << "/" << i->clockrate;
      pt_ = i->id;
      audio_stream_ = audio_stream_start(&av_profile, 2000, "127.0.0.1", 3000, i->id, 250);
      first = false;
    }
  }
  
  if (first) {
    // We're being asked to set an empty list of codecs. This will only happen when
    // working with a buggy client; let's try PCMU.
     LOG(LS_WARNING) << "Received empty list of codces; using PCMU/8000";
    audio_stream_ = audio_stream_start(&av_profile, 2000, "127.0.0.1", 3000, 0, 250);
  }
 
}

bool LinphoneMediaEngine::FindCodec(const Codec &c) {
  if (c.id == 0)
    return true;
  if (c.name == telephone_event.mime_type)
    return true;
#ifdef HAVE_SPEEX
  if (c.name == speex_wb.mime_type && c.clockrate == speex_wb.clock_rate)
    return true;
  if (c.name == speex_nb.mime_type && c.clockrate == speex_nb.clock_rate)
    return true;
#endif
#ifdef HAVE_ILBC
  if (c.name == payload_type_ilbc.mime_type)
    return true;
#endif
return false;
}

void LinphoneMediaChannel::OnPacketReceived(const void *data, int len) {
  uint8 buf[2048];
  memcpy(buf, data, len);
  
  /* We may receive packets with payload type 13: comfort noise. Linphone can't
   * handle them, so let's ignore those packets.
   */
  int payloadtype = buf[1] & 0x7f;
  if (play_ && payloadtype != 13)
    socket_->SendTo(buf, len, talk_base::SocketAddress("localhost",2000));
}

void LinphoneMediaChannel::SetPlayout(bool playout) {
  play_ = playout;
}

void LinphoneMediaChannel::SetSend(bool send) {
  mute_ = !send;
}

int LinphoneMediaChannel::GetOutputLevel() {}

LinphoneMediaEngine::LinphoneMediaEngine() {}
LinphoneMediaEngine::~LinphoneMediaEngine() {}

static void null_log_handler(const gchar *log_domain,
	     		     GLogLevelFlags log_level,
			     const gchar *message,
			     gpointer user_data) {
  LOG(LS_INFO) << log_domain << " " << message;
}

bool LinphoneMediaEngine::Init() {
  g_log_set_handler("MediaStreamer", G_LOG_LEVEL_MASK, null_log_handler, this);
  g_log_set_handler("oRTP", G_LOG_LEVEL_MASK, null_log_handler, this);
  g_log_set_handler("oRTP-stats", G_LOG_LEVEL_MASK, null_log_handler, this);
  ortp_init();
  ms_init();
 
#ifdef HAVE_SPEEX
  ms_speex_codec_init();

  codecs_.push_back(Codec(110, speex_wb.mime_type, speex_wb.clock_rate, 0, 1, 8));
  codecs_.push_back(Codec(111, speex_nb.mime_type, speex_nb.clock_rate, 0, 1, 7));
  
#endif

#ifdef HAVE_ILBC
  ms_ilbc_codec_init();
  codecs_.push_back(Codec(102, payload_type_ilbc.mime_type, payload_type_ilbc.clock_rate, 0, 1, 4));
#endif

  codecs_.push_back(Codec(0, pcmu8000.mime_type, pcmu8000.clock_rate, 0, 1, 2));
  codecs_.push_back(Codec(101, telephone_event.mime_type, telephone_event.clock_rate, 0, 1, 1));
  return true;
}

void LinphoneMediaEngine::Terminate() {
 
}
  
MediaChannel *LinphoneMediaEngine::CreateChannel() {
  return new LinphoneMediaChannel(this);
}

int LinphoneMediaEngine::SetAudioOptions(int options) {}
int LinphoneMediaEngine::SetSoundDevices(int wave_in_device, int wave_out_device) {}

float LinphoneMediaEngine::GetCurrentQuality() {}
int LinphoneMediaEngine::GetInputLevel() {}
