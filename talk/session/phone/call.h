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

#ifndef TALK_SESSION_PHONE_CALL_H_
#define TALK_SESSION_PHONE_CALL_H_

#include <string>
#include <map>
#include <vector>
#include <deque>
#include "talk/base/messagequeue.h"
#include "talk/p2p/base/session.h"
#include "talk/p2p/client/socketmonitor.h"
#include "talk/xmpp/jid.h"
#include "talk/session/phone/audiomonitor.h"
#include "talk/session/phone/voicechannel.h"

namespace cricket {

class MediaSessionClient;
struct CallOptions;

class Call : public talk_base::MessageHandler, public sigslot::has_slots<> {
 public:
  Call(MediaSessionClient* session_client);
  ~Call();

  Session *InitiateSession(const buzz::Jid &jid, const CallOptions& options);
  void AcceptSession(BaseSession *session, const CallOptions& options);
  void RejectSession(BaseSession *session);
  void TerminateSession(BaseSession *session);
  void Terminate();
  void SetLocalRenderer(VideoRenderer* renderer);
  void SetVideoRenderer(BaseSession *session, uint32 ssrc,
                        VideoRenderer* renderer);
  void AddStream(BaseSession *session, uint32 voice_ssrc, uint32 video_ssrc);
  void RemoveStream(BaseSession *session, uint32 voice_ssrc, uint32 video_ssrc);
  void StartConnectionMonitor(BaseSession *session, int cms);
  void StopConnectionMonitor(BaseSession *session);
  void StartAudioMonitor(BaseSession *session, int cms);
  void StopAudioMonitor(BaseSession *session);
  void Mute(bool mute);
  void PressDTMF(int event);

  const std::vector<Session *> &sessions();
  uint32 id();
  bool video() const { return video_; }
  bool muted() const { return muted_; }

  // Setting this to false will cause the call to have a longer timeout and
  // for the SignalSetupToCallVoicemail to never fire.
  void set_send_to_voicemail(bool send_to_voicemail) {
    send_to_voicemail_ = send_to_voicemail;
  }
  bool send_to_voicemail() { return send_to_voicemail_; }

  // Sets a flag on the chatapp that will redirect the call to voicemail once
  // the call has been terminated
  sigslot::signal0<> SignalSetupToCallVoicemail;
  sigslot::signal2<Call *, Session *> SignalAddSession;
  sigslot::signal2<Call *, Session *> SignalRemoveSession;
  sigslot::signal3<Call *, BaseSession *, BaseSession::State>
      SignalSessionState;
  sigslot::signal3<Call *, BaseSession *, Session::Error>
      SignalSessionError;
  sigslot::signal3<Call *, Session *, const std::string &>
      SignalReceivedTerminateReason;
  sigslot::signal2<Call *, const std::vector<ConnectionInfo> &>
      SignalConnectionMonitor;
  sigslot::signal2<Call *, const VoiceMediaInfo&> SignalMediaMonitor;
  sigslot::signal2<Call *, const AudioInfo&> SignalAudioMonitor;
  sigslot::signal2<Call *, const std::vector<ConnectionInfo> &>
      SignalVideoConnectionMonitor;
  sigslot::signal2<Call *, const VideoMediaInfo&> SignalVideoMediaMonitor;

 private:
  void OnMessage(talk_base::Message *message);
  void OnSessionState(BaseSession *session, BaseSession::State state);
  void OnSessionError(BaseSession *session, Session::Error error);
  void OnReceivedTerminateReason(Session *session, const std::string &reason);
  void IncomingSession(Session *session, const SessionDescription* offer);
  // Returns true on success.
  bool AddSession(Session *session, const SessionDescription* offer);
  void RemoveSession(Session *session);
  void EnableChannels(bool enable);
  void Join(Call *call, bool enable);
  void OnConnectionMonitor(VoiceChannel *channel,
                           const std::vector<ConnectionInfo> &infos);
  void OnMediaMonitor(VoiceChannel *channel, const VoiceMediaInfo& info);
  void OnAudioMonitor(VoiceChannel *channel, const AudioInfo& info);
  void OnConnectionMonitor(VideoChannel *channel,
                           const std::vector<ConnectionInfo> &infos);
  void OnMediaMonitor(VideoChannel *channel, const VideoMediaInfo& info);
  VoiceChannel* GetVoiceChannel(BaseSession* session);
  VideoChannel* GetVideoChannel(BaseSession* session);
  void ContinuePlayDTMF();

  uint32 id_;
  MediaSessionClient *session_client_;
  std::vector<Session *> sessions_;
  std::map<std::string, VoiceChannel *> voice_channel_map_;
  std::map<std::string, VideoChannel *> video_channel_map_;
  VideoRenderer* local_renderer_;
  bool video_;
  bool muted_;
  bool send_to_voicemail_;

  // DTMF tones have to be queued up so that we don't flood the call.  We
  // keep a deque (doubely ended queue) of them around.  While one is playing we
  // set the playing_dtmf_ bit and schedule a message in XX msec to clear that
  // bit or start the next tone playing.
  std::deque<int> queued_dtmf_;
  bool playing_dtmf_;

  friend class MediaSessionClient;
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_CALL_H_
