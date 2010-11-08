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

#ifndef TALK_EXAMPLES_CALL_CALLCLIENT_H_
#define TALK_EXAMPLES_CALL_CALLCLIENT_H_

#include <map>
#include <string>
#include <vector>

#include "talk/p2p/base/session.h"
#include "talk/session/phone/mediachannel.h"
#include "talk/xmpp/xmppclient.h"
#include "talk/examples/call/status.h"
#include "talk/examples/call/console.h"
#ifdef USE_TALK_SOUND
#include "talk/sound/soundsystemfactory.h"
#endif

namespace buzz {
class PresencePushTask;
class PresenceOutTask;
class MucInviteRecvTask;
class MucInviteSendTask;
class FriendInviteSendTask;
class VoicemailJidRequester;
class DiscoInfoQueryTask;
class Muc;
class Status;
class MucStatus;
struct AvailableMediaEntry;
}

namespace talk_base {
class Thread;
class NetworkManager;
}

namespace cricket {
class PortAllocator;
class MediaEngine;
class MediaSessionClient;
class Receiver;
class Call;
struct CallOptions;
class SessionManagerTask;
enum SignalingProtocol;
}

struct RosterItem {
  buzz::Jid jid;
  buzz::Status::Show show;
  std::string status;
};

class NullRenderer;

class CallClient: public sigslot::has_slots<> {
 public:
  explicit CallClient(buzz::XmppClient* xmpp_client);
  ~CallClient();

  cricket::MediaSessionClient* media_client() const { return media_client_; }
  void SetMediaEngine(cricket::MediaEngine* media_engine) {
    media_engine_ = media_engine;
  }
  void SetAutoAccept(bool auto_accept) {
    auto_accept_ = auto_accept;
  }
  void SetPmucDomain(const std::string &pmuc_domain) {
      pmuc_domain_ = pmuc_domain;
  }
  void SetConsole(Console *console) {
    console_ = console;
  }

  void ParseLine(const std::string &str);

  void SendChat(const std::string& to, const std::string msg);
  void InviteFriend(const std::string& user);
  void JoinMuc(const std::string& room);
  void InviteToMuc(const std::string& user, const std::string& room);
  void LeaveMuc(const std::string& room);
  void SetPortAllocatorFlags(uint32 flags) { portallocator_flags_ = flags; }
  void SetAllowLocalIps(bool allow_local_ips) {
    allow_local_ips_ = allow_local_ips;
  }

  void SetInitialProtocol(cricket::SignalingProtocol initial_protocol) {
    initial_protocol_ = initial_protocol;
  }


  typedef std::map<buzz::Jid, buzz::Muc*> MucMap;

  const MucMap& mucs() const {
    return mucs_;
  }

 private:
  void AddStream(uint32 audio_src_id, uint32 video_src_id);
  void RemoveStream(uint32 audio_src_id, uint32 video_src_id);
  void OnStateChange(buzz::XmppEngine::State state);

  void InitPhone();
  void InitPresence();
  void RefreshStatus();
  void OnRequestSignaling();
  void OnSessionCreate(cricket::Session* session, bool initiate);
  void OnCallCreate(cricket::Call* call);
  void OnCallDestroy(cricket::Call* call);
  void OnSessionState(cricket::Call* call,
                      cricket::BaseSession* session,
                      cricket::BaseSession::State state);
  void OnStatusUpdate(const buzz::Status& status);
  void OnMucInviteReceived(const buzz::Jid& inviter, const buzz::Jid& room,
      const std::vector<buzz::AvailableMediaEntry>& avail);
  void OnMucJoined(const buzz::Jid& endpoint);
  void OnMucStatusUpdate(const buzz::Jid& jid, const buzz::MucStatus& status);
  void OnMucLeft(const buzz::Jid& endpoint, int error);
  void OnDevicesChange();
  void OnFoundVoicemailJid(const buzz::Jid& to, const buzz::Jid& voicemail);
  void OnVoicemailJidError(const buzz::Jid& to);

  static const std::string strerror(buzz::XmppEngine::Error err);

  void PrintRoster();
  void MakeCallTo(const std::string& name, const cricket::CallOptions& options);
  void PlaceCall(const buzz::Jid& jid, const cricket::CallOptions& options);
  void CallVoicemail(const std::string& name);
  void Accept(const cricket::CallOptions& options);
  void Reject();
  void Quit();

  void GetDevices();
  void PrintDevices(const std::vector<std::string>& names);

  void SetVolume(const std::string& level);

  typedef std::map<std::string, RosterItem> RosterMap;

  Console *console_;
  buzz::XmppClient* xmpp_client_;
  talk_base::Thread* worker_thread_;
  talk_base::NetworkManager* network_manager_;
  cricket::PortAllocator* port_allocator_;
  cricket::SessionManager* session_manager_;
  cricket::SessionManagerTask* session_manager_task_;
  cricket::MediaEngine* media_engine_;
  cricket::MediaSessionClient* media_client_;
  MucMap mucs_;

  cricket::Call* call_;
  cricket::BaseSession *session_;
  bool incoming_call_;
  bool auto_accept_;
  std::string pmuc_domain_;
  NullRenderer* local_renderer_;
  NullRenderer* remote_renderer_;

  buzz::Status my_status_;
  buzz::PresencePushTask* presence_push_;
  buzz::PresenceOutTask* presence_out_;
  buzz::MucInviteRecvTask* muc_invite_recv_;
  buzz::MucInviteSendTask* muc_invite_send_;
  buzz::FriendInviteSendTask* friend_invite_send_;
  RosterMap* roster_;
  uint32 portallocator_flags_;

  bool allow_local_ips_;
  cricket::SignalingProtocol initial_protocol_;
  std::string last_sent_to_;
#ifdef USE_TALK_SOUND
  cricket::SoundSystemFactory* sound_system_factory_;
#endif
};

#endif  // TALK_EXAMPLES_CALL_CALLCLIENT_H_
