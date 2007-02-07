/*
 * libjingle
 * Copyright 2004--2005, Google Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// GipsLiteMediaEngine is a GIPS Voice Engine Lite implementation of MediaEngine
#include "talk/base/logging.h"
#include <cassert>
#include <iostream>
#include "gipslitemediaengine.h"
using namespace cricket;

#if 0
#define TRACK(x) LOG(LS_VERBOSE) << x
#else
#define TRACK(x)
#endif

//#define GIPS_TRACING

namespace {
struct CodecPref { const char* name; int clockrate; int pref; };
const CodecPref kGIPSCodecPrefs[] = {
  { "ISAC", 1600, 7 },
  { "speex", 1600, 6 },
  { "IPCMWB", 1600, 6},
  { "speex", 8000, 4},
  { "iLBC", 8000, 1 },
  { "G723", 8000, 4 },
  { "EG711U", 8000, 3 },
  { "EG711A", 8000, 3 },
  { "PCMU", 8000, 2 },
  { "PCMA", 8000, 2 },
  { "CN", 8000, 2 },
  { "red", 8000, -1 },
  { "telephone-event", 8000, -1 }
};
const size_t kNumGIPSCodecs = sizeof(kGIPSCodecPrefs) / sizeof(CodecPref);
}


void GipsLiteMediaChannel::SetCodecs(const std::vector<Codec> &codecs) {
  GIPS_CodecInst c;
  std::vector<Codec>::const_iterator i;


  bool first = true;
  for (i = codecs.begin(); i < codecs.end(); i++) {
    if (engine_->FindGIPSCodec(*i, &c) == false)
      continue;

    if (c.pltype != i->id) {
      c.pltype = i->id;
      engine_->gips().GIPSVE_SetRecPayloadType(gips_channel_, &c);
    }

    if (first) {
      LOG(LS_INFO) << "Using " << c.plname << "/" << c.plfreq;
      engine_->gips().GIPSVE_SetSendCodec(gips_channel_, &c);
      first = false;
    }
  }
  if (first) {
    // We're being asked to set an empty list of codecs. This will only happen when
    // dealing with a buggy client. We'll send them the most common format: PCMU
    Codec codec(0, "PCMU", 8000, 0, 1, 0);
    LOG(LS_WARNING) << "Received empty list of codces; using PCMU/8000";
    engine_->FindGIPSCodec(codec, &c);
    engine_->gips().GIPSVE_SetSendCodec(gips_channel_, &c);
  }
}


void GipsLiteMediaChannel::OnPacketReceived(const void *data, int len) {
  engine_->gips().GIPSVE_ReceivedRTPPacket(gips_channel_, data, (int)len);
}

void GipsLiteMediaChannel::SetPlayout(bool playout) {
  if (playout)
    engine_->gips().GIPSVE_StartPlayout(gips_channel_);
  else
    engine_->gips().GIPSVE_StopPlayout(gips_channel_);
}

void GipsLiteMediaChannel::SetSend(bool send) {
  if (send)
    engine_->gips().GIPSVE_StartSend(gips_channel_);
  else
    engine_->gips().GIPSVE_StopSend(gips_channel_);
}

GipsLiteMediaChannel::GipsLiteMediaChannel(GipsLiteMediaEngine *engine) {
  network_interface_ = NULL;
  engine_ = engine;
  gips_channel_ = engine_->gips().GIPSVE_CreateChannel();
  engine_->gips().GIPSVE_SetSendTransport(gips_channel_, *this);
}


int GipsLiteMediaEngine::GetGIPSCodecPreference(const char *name, int clockrate) {
  for (size_t i = 0; i < kNumGIPSCodecs; ++i) {
    if ((strcmp(kGIPSCodecPrefs[i].name, name) == 0) &&
        (kGIPSCodecPrefs[i].clockrate == clockrate))
      return kGIPSCodecPrefs[i].pref;
  }
  assert(false);
  return -1;
}

GipsLiteMediaEngine::GipsLiteMediaEngine() :
  gips_(GetGipsVoiceEngineLite()) {}

bool GipsLiteMediaEngine::Init() {

  TRACK("GIPSVE_Init");
  if (gips_.GIPSVE_Init() == -1)
    return false;

  char buffer[1024];
  TRACK("GIPSVE_GetVersion");
  int r = gips_.GIPSVE_GetVersion(buffer, sizeof(buffer));
  LOG(LS_INFO) << "GIPS Version: " << r << ": " << buffer;
  
  // Set auto gain control on
  TRACK("GIPSVE_SetAGCStatus");
  if (gips_.GIPSVE_SetAGCStatus(1) == -1)
    return false;
 
  TRACK("GIPSVE_GetNofCodecs");
  int ncodecs = gips_.GIPSVE_GetNofCodecs();
  for (int i = 0; i < ncodecs; ++i) {
    GIPS_CodecInst gips_codec;
    if (gips_.GIPSVE_GetCodec(i, &gips_codec) >= 0) {
      Codec codec(gips_codec.pltype, gips_codec.plname, gips_codec.plfreq, gips_codec.rate, 
                  gips_codec.channels, GetGIPSCodecPreference(gips_codec.plname, gips_codec.plfreq));
      LOG(LS_INFO) << gips_codec.plname << "/" << gips_codec.plfreq << "/" << gips_codec.channels << " " << gips_codec.pltype;
      codecs_.push_back(codec);
    }
  }
  return true;
}

void GipsLiteMediaEngine::Terminate() {
  gips_.GIPSVE_Terminate();
}

MediaChannel * GipsLiteMediaEngine::CreateChannel() {
  return new GipsLiteMediaChannel(this);
}

bool GipsLiteMediaEngine::FindGIPSCodec(Codec codec, GIPS_CodecInst* gips_codec) {
  int ncodecs = gips_.GIPSVE_GetNofCodecs();
  for (int i = 0; i < ncodecs; ++i) {
    GIPS_CodecInst gc;
    if (gips_.GIPSVE_GetCodec(i, &gc) >= 0) {
      if (codec.id < 96) {
        // Compare by id
        if (codec.id != gc.pltype)
          continue;
      } else {
        // Compare by name
        if (strcmp(codec.name.c_str(), gc.plname) != 0)
          continue;
      }

      // If the clockrate is specified, make sure it matches
      if (codec.clockrate > 0 && codec.clockrate != gc.plfreq)
        continue;

      // If the bitrate is specified, make sure it matches
      if (codec.bitrate > 0 && codec.bitrate != gc.rate)
        continue;

      // Make sure the channels match
      if (codec.channels != gc.channels)
        continue;

      // If we got this far, we match.
      if (gips_codec)
        *gips_codec = gc;
      return true;
    }
  }
  return false;
}

int GipsLiteMediaEngine::SetAudioOptions(int options) {
  // Set auto gain control on
  if (gips_.GIPSVE_SetAGCStatus(options & AUTO_GAIN_CONTROL ? 1 : 0) == -1) {
    return -1;
    // TODO: We need to log these failures.
  }
  return 0;
}
 
int GipsLiteMediaEngine::SetSoundDevices(int wave_in_device, int wave_out_device) {
  if (gips_.GIPSVE_SetSoundDevices(wave_in_device, wave_out_device) == -1) {
    int error = gips_.GIPSVE_GetLastError();
    // TODO: We need to log these failures.
    return error;
    }
  return 0;
}

bool GipsLiteMediaEngine::FindCodec(const Codec &codec)
{
  return FindGIPSCodec(codec, NULL);
}

std::vector<Codec> GipsLiteMediaEngine::codecs()
{
  return codecs_;
}