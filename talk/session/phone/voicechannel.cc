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

#include "talk/session/phone/voicechannel.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/phonesessionclient.h"
#include <cassert>
#undef SetPort

namespace cricket {


VoiceChannel::VoiceChannel(ChannelManager *manager, Session *session, 
                           MediaChannel *channel) {
  channel_manager_ = manager;
  assert(channel_manager_->worker_thread() == talk_base::Thread::Current());
  media_channel_ = channel;
  session_ = session;
  socket_monitor_ = NULL;
  audio_monitor_ = NULL;
  transport_channel_ = session_->CreateChannel("rtp");
  transport_channel_->SignalWritableState.connect(
      this, &VoiceChannel::OnWritableState);
  transport_channel_->SignalReadPacket.connect(
      this, &VoiceChannel::OnChannelRead);
  media_channel_->SetInterface(this);
  enabled_ = false;
  paused_ = false;
  writable_ = false;
  muted_ = false;
  LOG(INFO) << "Created voice channel";

  session->SignalState.connect(this, &VoiceChannel::OnSessionState);
  OnSessionState(session, session->state());
}

VoiceChannel::~VoiceChannel() {
  assert(channel_manager_->worker_thread() == talk_base::Thread::Current());
  enabled_ = false;
  ChangeState();
  delete socket_monitor_;
  delete audio_monitor_;
  talk_base::Thread::Current()->Clear(this);
  if (transport_channel_ != NULL)
    session_->DestroyChannel(transport_channel_);
  LOG(INFO) << "Destroyed voice channel";
}

void VoiceChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
  case MSG_ENABLE:
    EnableMedia_w();
    break;

  case MSG_DISABLE:
    DisableMedia_w();
    break;

  case MSG_MUTE:
    MuteMedia_w();
    break;

  case MSG_UNMUTE:
    UnmuteMedia_w();
    break;

  case MSG_SETSENDCODEC:
    SetSendCodec_w();
    break;
  }
}

void VoiceChannel::Enable(bool enable) {
  // Can be called from thread other than worker thread
  channel_manager_->worker_thread()->Post(this, 
    enable ? MSG_ENABLE : MSG_DISABLE);
}

void VoiceChannel::Mute(bool mute) {
  // Can be called from thread other than worker thread
  channel_manager_->worker_thread()->Post(this, mute ? MSG_MUTE : MSG_UNMUTE);
}


MediaChannel * VoiceChannel::channel() {
  return media_channel_;
}

void VoiceChannel::OnSessionState(Session* session, Session::State state) {
  if ((state == Session::STATE_RECEIVEDACCEPT) ||
      (state == Session::STATE_RECEIVEDINITIATE)) {
    channel_manager_->worker_thread()->Post(this, MSG_SETSENDCODEC);
  }
}

void VoiceChannel::SetSendCodec_w() {
  assert(channel_manager_->worker_thread() == talk_base::Thread::Current());

  const PhoneSessionDescription* desc =
      static_cast<const PhoneSessionDescription*>(
        session()->remote_description());

  media_channel_->SetCodecs(desc->codecs());
}

void VoiceChannel::OnWritableState(TransportChannel* channel) {
  ASSERT(channel == transport_channel_);
  if (transport_channel_->writable()) {
    ChannelWritable_w();
  } else {
    ChannelNotWritable_w();
  }
}

void VoiceChannel::OnChannelRead(TransportChannel* channel,
                                 const char* data,
                                 size_t len) {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  // OnChannelRead gets called from P2PSocket; now pass data to MediaEngine
  media_channel_->OnPacketReceived(data, (int)len);
}

void VoiceChannel::SendPacket(const void *data, size_t len) {
  // SendPacket gets called from MediaEngine; send to socket
  // MediaEngine will call us on a random thread.  The Send operation on the
  // socket is special in that it can handle this.
  transport_channel_->SendPacket(static_cast<const char *>(data), len);
}

void VoiceChannel::ChangeState() {
  if (paused_ || !enabled_ || !writable_) {
    media_channel_->SetPlayout(false);
    media_channel_->SetSend(false);
  } else {
    if (muted_) {
      media_channel_->SetSend(false);
      media_channel_->SetPlayout(true);
    } else {
      media_channel_->SetSend(true);
      media_channel_->SetPlayout(true);
    }
  }
}

void VoiceChannel::PauseMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  ASSERT(!paused_);

  LOG(INFO) << "Voice channel paused";
  paused_ = true;
  ChangeState();
}

void VoiceChannel::UnpauseMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  ASSERT(paused_);

  LOG(INFO) << "Voice channel unpaused";
  paused_ = false;
  ChangeState();
}

void VoiceChannel::EnableMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (enabled_)
    return;

  LOG(INFO) << "Voice channel enabled";
  enabled_ = true;
  ChangeState();
}

void VoiceChannel::DisableMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (!enabled_)
    return;

  LOG(INFO) << "Voice channel disabled";
  enabled_ = false;
  ChangeState();
}

void VoiceChannel::MuteMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (muted_)
    return;

  LOG(INFO) << "Voice channel muted";
  muted_ = true;
  ChangeState();
}

void VoiceChannel::UnmuteMedia_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (!muted_)
    return;

  LOG(INFO) << "Voice channel unmuted";
  muted_ = false;
  ChangeState();
}

void VoiceChannel::ChannelWritable_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (writable_)
    return;

  LOG(INFO) << "Voice channel socket writable";
  writable_ = true;
  ChangeState();
}

void VoiceChannel::ChannelNotWritable_w() {
  ASSERT(channel_manager_->worker_thread() == talk_base::Thread::Current());
  if (!writable_)
    return;

  LOG(INFO) << "Voice channel socket not writable";
  writable_ = false;
  ChangeState();
}


void VoiceChannel::StartConnectionMonitor(int cms) {
  delete socket_monitor_;
  socket_monitor_ =
      new SocketMonitor(session_, transport_channel_, 
        talk_base::Thread::Current());
  socket_monitor_->SignalUpdate.connect(
      this, &VoiceChannel::OnConnectionMonitorUpdate);
  socket_monitor_->Start(cms);
}

void VoiceChannel::StopConnectionMonitor() {
  if (socket_monitor_ != NULL) {
    socket_monitor_->Stop();
    socket_monitor_->SignalUpdate.disconnect(this);
    delete socket_monitor_;
    socket_monitor_ = NULL;
  }
}

void VoiceChannel::OnConnectionMonitorUpdate(
    SocketMonitor *monitor, const std::vector<ConnectionInfo> &infos) {
  SignalConnectionMonitor(this, infos);
}

void VoiceChannel::StartAudioMonitor(int cms) {
  delete audio_monitor_;
  audio_monitor_ = new AudioMonitor(this, talk_base::Thread::Current());
  audio_monitor_
    ->SignalUpdate.connect(this, &VoiceChannel::OnAudioMonitorUpdate);
  audio_monitor_->Start(cms);
}

void VoiceChannel::StopAudioMonitor() {
  if (audio_monitor_ != NULL) {
    audio_monitor_ ->Stop();
    audio_monitor_ ->SignalUpdate.disconnect(this);
    delete audio_monitor_ ;
    audio_monitor_  = NULL;
  }
}

void VoiceChannel::OnAudioMonitorUpdate(AudioMonitor *monitor,
                                        const AudioInfo& info) {
  SignalAudioMonitor(this, info);
}

void VoiceChannel::StartMediaMonitor(int cms) {
  media_channel_
    ->SignalMediaMonitor.connect(this, &VoiceChannel::OnMediaMonitorUpdate);
  media_channel_->StartMediaMonitor(this, cms);
}

void VoiceChannel::StopMediaMonitor() {
  media_channel_->SignalMediaMonitor.disconnect(this);
  media_channel_->StopMediaMonitor();
}

void VoiceChannel::OnMediaMonitorUpdate(
    MediaChannel *media_channel, const MediaInfo &info) {
  ASSERT(media_channel == media_channel_);
  SignalMediaMonitor(this, info);
}

Session *VoiceChannel::session() {
  return session_;
}

int VoiceChannel::GetInputLevel_w() {
  return channel_manager_->media_engine()->GetInputLevel();
}

int VoiceChannel::GetOutputLevel_w() {
  return media_channel_->GetOutputLevel();
}

talk_base::Thread* VoiceChannel::worker_thread() {
  return channel_manager_->worker_thread();
}

}
