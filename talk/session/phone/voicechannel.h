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

#ifndef _VOICECHANNEL_H_
#define _VOICECHANNEL_H_

#include "talk/base/asyncudpsocket.h"
#include "talk/base/network.h"
#include "talk/base/sigslot.h"
#include "talk/p2p/client/socketmonitor.h"
#include "talk/p2p/base/session.h"
#include "talk/session/phone/audiomonitor.h"
#include "talk/session/phone/mediaengine.h"
#include "talk/session/phone/mediachannel.h"

namespace cricket {

const uint32 MSG_ENABLE = 1;
const uint32 MSG_DISABLE = 2;
const uint32 MSG_MUTE = 3;
const uint32 MSG_UNMUTE = 4;
const uint32 MSG_SETSENDCODEC = 5;


class ChannelManager;

class VoiceChannel
  : public talk_base::MessageHandler, public sigslot::has_slots<>,
    public MediaChannel::NetworkInterface {
 public:
  VoiceChannel(ChannelManager *manager, Session *session, 
      MediaChannel *channel);
  ~VoiceChannel();

  void Enable(bool enable);
  void Mute(bool mute);


  MediaChannel *channel();
  Session *session();

  // Monitoring

  void StartConnectionMonitor(int cms);
  void StopConnectionMonitor();
  sigslot::signal2<VoiceChannel *, const std::vector<ConnectionInfo> &> 
      SignalConnectionMonitor;

  void StartAudioMonitor(int cms);
  void StopAudioMonitor();
  sigslot::signal2<VoiceChannel *, const AudioInfo&> SignalAudioMonitor;
  talk_base::Thread* worker_thread();

  void StartMediaMonitor(int cms);
  void StopMediaMonitor();
  sigslot::signal2<VoiceChannel *, const MediaInfo&> SignalMediaMonitor;

  // Pausing so that the ChannelManager can change the audio devices.  These
  // should only be called from the worker thread
  void PauseMedia_w();
  void UnpauseMedia_w();

  int GetInputLevel_w();
  int GetOutputLevel_w();

  // MediaEngine calls this
  virtual void SendPacket(const void *data, size_t len);

private:
  void ChangeState();
  void EnableMedia_w();
  void DisableMedia_w();
  void MuteMedia_w();
  void UnmuteMedia_w();
  void ChannelWritable_w();
  void ChannelNotWritable_w();


  void OnConnectionMonitorUpdate(SocketMonitor *monitor, 
      const std::vector<ConnectionInfo> &infos);
  void OnAudioMonitorUpdate(AudioMonitor *monitor, const AudioInfo& info);
  void OnMediaMonitorUpdate(MediaChannel *media_channel,
    const MediaInfo& info);

  // From MessageHandler

  void OnMessage(talk_base::Message *pmsg);

  // Setting the send codec based on the remote description.
  void OnSessionState(Session* session, Session::State state);
  void SetSendCodec_w();

  // From TransportChannel

  void OnWritableState(TransportChannel* channel);
  void OnChannelRead(TransportChannel* channel, const char *data, size_t len);
  

  bool enabled_;
  bool paused_;
  bool writable_;
  bool muted_;
  MediaChannel *media_channel_;
  Session *session_;
  TransportChannel *transport_channel_;
  ChannelManager *channel_manager_;
  SocketMonitor *socket_monitor_;
  AudioMonitor *audio_monitor_;
};

}

#endif // _VOICECHANNEL_H_
