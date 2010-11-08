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

#include "talk/examples/call/callclient.h"

#include <string>

#include "talk/xmpp/constants.h"
#include "talk/base/helpers.h"
#include "talk/base/thread.h"
#include "talk/base/network.h"
#include "talk/base/socketaddress.h"
#include "talk/base/stringutils.h"
#include "talk/base/stringencode.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/client/sessionmanagertask.h"
#include "talk/session/phone/devicemanager.h"
#include "talk/session/phone/mediaengine.h"
#include "talk/session/phone/mediasessionclient.h"
#include "talk/examples/call/console.h"
#include "talk/examples/call/presencepushtask.h"
#include "talk/examples/call/presenceouttask.h"
#include "talk/examples/call/mucinviterecvtask.h"
#include "talk/examples/call/mucinvitesendtask.h"
#include "talk/examples/call/friendinvitesendtask.h"
#include "talk/examples/call/muc.h"
#include "talk/examples/call/voicemailjidrequester.h"
#ifdef USE_TALK_SOUND
#include "talk/sound/platformsoundsystemfactory.h"
#endif

#include "talk/base/logging.h"

class NullRenderer : public cricket::VideoRenderer {
 public:
  explicit NullRenderer(const char* s) : s_(s) {}
 private:
  bool SetSize(int width, int height, int reserved) {
    LOG(LS_INFO) << "Video size for " << s_ << ": " << width << "x" << height;
    return true;
  }
  bool RenderFrame(const cricket::VideoFrame *frame) {
    return true;
  }
  const char* s_;
};

namespace {

const char* DescribeStatus(buzz::Status::Show show, const std::string& desc) {
  switch (show) {
  case buzz::Status::SHOW_XA:      return desc.c_str();
  case buzz::Status::SHOW_ONLINE:  return "online";
  case buzz::Status::SHOW_AWAY:    return "away";
  case buzz::Status::SHOW_DND:     return "do not disturb";
  case buzz::Status::SHOW_CHAT:    return "ready to chat";
  default:                         return "offline";
  }
}

std::string GetWord(const std::vector<std::string>& words,
                    size_t index, const std::string& def) {
  if (words.size() > index) {
    return words[index];
  } else {
    return def;
  }
}

int GetInt(const std::vector<std::string>& words, size_t index, int def) {
  int val;
  if (words.size() > index && talk_base::FromString(words[index], &val)) {
    return val;
  } else {
    return def;
  }
}


}  // namespace

const char* CALL_COMMANDS =
"Available commands:\n"
"\n"
"  hangup  Ends the call.\n"
"  mute    Stops sending voice.\n"
"  unmute  Re-starts sending voice.\n"
"  dtmf    Sends a DTMF tone.\n"
"  quit    Quits the application.\n"
"";

const char* RECEIVE_COMMANDS =
"Available commands:\n"
"\n"
"  accept [bw] Accepts the incoming call and switches to it.\n"
"  reject  Rejects the incoming call and stays with the current call.\n"
"  quit    Quits the application.\n"
"";

const char* CONSOLE_COMMANDS =
"Available commands:\n"
"\n"
"  roster              Prints the online friends from your roster.\n"
"  friend user         Request to add a user to your roster.\n"
"  call [jid] [bw]     Initiates a call to the user[/room] with the\n"
"                      given JID and with optional bandwidth.\n"
"  vcall [jid] [bw]    Initiates a video call to the user[/room] with\n"
"                      the given JID and with optional bandwidth.\n"
"  voicemail [jid]     Leave a voicemail for the user with the given JID.\n"
"  join [room]         Joins a multi-user-chat.\n"
"  invite user [room]  Invites a friend to a multi-user-chat.\n"
"  leave [room]        Leaves a multi-user-chat.\n"
"  getdevs             Prints the available media devices.\n"
"  quit                Quits the application.\n"
"";

void CallClient::ParseLine(const std::string& line) {
  std::vector<std::string> words;
  int start = -1;
  int state = 0;
  for (int index = 0; index <= static_cast<int>(line.size()); ++index) {
    if (state == 0) {
      if (!isspace(line[index])) {
        start = index;
        state = 1;
      }
    } else {
      ASSERT(state == 1);
      ASSERT(start >= 0);
      if (isspace(line[index])) {
        std::string word(line, start, index - start);
        words.push_back(word);
        start = -1;
        state = 0;
      }
    }
  }

  // Global commands
  const std::string& command = GetWord(words, 0, "");
  if (command == "quit") {
    Quit();
  } else if (call_ && incoming_call_) {
    if (command == "accept") {
      cricket::CallOptions options;
      options.video_bandwidth = GetInt(words, 1, cricket::kAutoBandwidth);
      Accept(options);
    } else if (command == "reject") {
      Reject();
    } else {
      console_->Print(RECEIVE_COMMANDS);
    }
  } else if (call_) {
    if (command == "hangup") {
      // TODO: do more shutdown here, move to Terminate()
      call_->Terminate();
      call_ = NULL;
      session_ = NULL;
      console_->SetPrompt(NULL);
    } else if (command == "mute") {
      call_->Mute(true);
    } else if (command == "unmute") {
      call_->Mute(false);
    } else if ((command == "dtmf") && (words.size() == 2)) {
      int ev = std::string("0123456789*#").find(words[1][0]);
      call_->PressDTMF(ev);
    } else {
      console_->Print(CALL_COMMANDS);
    }
  } else {
    if (command == "roster") {
      PrintRoster();
    } else if (command == "send") {
      buzz::Jid jid(words[1]);
      if (jid.IsValid()) {
        last_sent_to_ = words[1];
        SendChat(words[1], words[2]);
      } else if (!last_sent_to_.empty()) {
        SendChat(last_sent_to_, words[1]);
      } else {
        console_->Printf(
            "Invalid JID. JIDs should be in the form user@domain\n");
      }
    } else if ((words.size() == 2) && (command == "friend")) {
      InviteFriend(words[1]);
    } else if (command == "call") {
      std::string to = GetWord(words, 1, "");
      MakeCallTo(to, cricket::CallOptions());
    } else if (command == "vcall") {
      std::string to = GetWord(words, 1, "");
      int bandwidth = GetInt(words, 2, cricket::kAutoBandwidth);
      cricket::CallOptions options;
      options.is_video = true;
      options.video_bandwidth = bandwidth;
      MakeCallTo(to, options);
    } else if (command == "join") {
      JoinMuc(GetWord(words, 1, ""));
    } else if ((words.size() >= 2) && (command == "invite")) {
      InviteToMuc(words[1], GetWord(words, 2, ""));
    } else if (command == "leave") {
      LeaveMuc(GetWord(words, 1, ""));
    } else if (command == "getdevs") {
      GetDevices();
    } else if ((words.size() == 2) && (command == "setvol")) {
      SetVolume(words[1]);
    } else if (command == "voicemail") {
      CallVoicemail((words.size() >= 2) ? words[1] : "");
    } else {
      console_->Print(CONSOLE_COMMANDS);
    }
  }
}

CallClient::CallClient(buzz::XmppClient* xmpp_client)
    : xmpp_client_(xmpp_client), media_engine_(NULL), media_client_(NULL),
      call_(NULL), incoming_call_(false),
      auto_accept_(false), pmuc_domain_("groupchat.google.com"),
      local_renderer_(NULL), remote_renderer_(NULL),
      roster_(new RosterMap), portallocator_flags_(0),
      allow_local_ips_(false), initial_protocol_(cricket::PROTOCOL_HYBRID)
#ifdef USE_TALK_SOUND
      , sound_system_factory_(NULL)
#endif
    {
  xmpp_client_->SignalStateChange.connect(this, &CallClient::OnStateChange);
}

CallClient::~CallClient() {
  delete media_client_;
  delete roster_;
}

const std::string CallClient::strerror(buzz::XmppEngine::Error err) {
  switch (err) {
    case  buzz::XmppEngine::ERROR_NONE:
      return "";
    case  buzz::XmppEngine::ERROR_XML:
      return "Malformed XML or encoding error";
    case  buzz::XmppEngine::ERROR_STREAM:
      return "XMPP stream error";
    case  buzz::XmppEngine::ERROR_VERSION:
      return "XMPP version error";
    case  buzz::XmppEngine::ERROR_UNAUTHORIZED:
      return "User is not authorized (Check your username and password)";
    case  buzz::XmppEngine::ERROR_TLS:
      return "TLS could not be negotiated";
    case  buzz::XmppEngine::ERROR_AUTH:
      return "Authentication could not be negotiated";
    case  buzz::XmppEngine::ERROR_BIND:
      return "Resource or session binding could not be negotiated";
    case  buzz::XmppEngine::ERROR_CONNECTION_CLOSED:
      return "Connection closed by output handler.";
    case  buzz::XmppEngine::ERROR_DOCUMENT_CLOSED:
      return "Closed by </stream:stream>";
    case  buzz::XmppEngine::ERROR_SOCKET:
      return "Socket error";
    default:
      return "Unknown error";
  }
}

void CallClient::OnCallDestroy(cricket::Call* call) {
  if (call == call_) {
    if (remote_renderer_) {
      delete remote_renderer_;
      remote_renderer_ = NULL;
    }
    if (local_renderer_) {
      delete local_renderer_;
      local_renderer_ = NULL;
    }
    console_->SetPrompt(NULL);
    console_->Print("call destroyed");
    call_ = NULL;
    session_ = NULL;
  }
}

void CallClient::OnStateChange(buzz::XmppEngine::State state) {
  switch (state) {
  case buzz::XmppEngine::STATE_START:
    console_->Print("connecting...");
    break;

  case buzz::XmppEngine::STATE_OPENING:
    console_->Print("logging in...");
    break;

  case buzz::XmppEngine::STATE_OPEN:
    console_->Print("logged in...");
    InitPhone();
    InitPresence();
    break;

  case buzz::XmppEngine::STATE_CLOSED:
    buzz::XmppEngine::Error error = xmpp_client_->GetError(NULL);
    console_->Print("logged out..." + strerror(error));
    Quit();
  }
}

void CallClient::InitPhone() {
  std::string client_unique = xmpp_client_->jid().Str();
  talk_base::InitRandom(client_unique.c_str(), client_unique.size());

  worker_thread_ = new talk_base::Thread();
  // The worker thread must be started here since initialization of
  // the ChannelManager will generate messages that need to be
  // dispatched by it.
  worker_thread_->Start();

  network_manager_ = new talk_base::NetworkManager();

  // TODO: Decide if the relay address should be specified here.
  talk_base::SocketAddress stun_addr("stun.l.google.com", 19302);
  port_allocator_ =
      new cricket::BasicPortAllocator(network_manager_, stun_addr,
          talk_base::SocketAddress(), talk_base::SocketAddress(),
          talk_base::SocketAddress());

  if (portallocator_flags_ != 0) {
    port_allocator_->set_flags(portallocator_flags_);
  }
  session_manager_ = new cricket::SessionManager(
      port_allocator_, worker_thread_);
  session_manager_->SignalRequestSignaling.connect(
      this, &CallClient::OnRequestSignaling);
  session_manager_->SignalSessionCreate.connect(
      this, &CallClient::OnSessionCreate);
  session_manager_->OnSignalingReady();

  session_manager_task_ =
      new cricket::SessionManagerTask(xmpp_client_, session_manager_);
  session_manager_task_->EnableOutgoingMessages();
  session_manager_task_->Start();

#ifdef USE_TALK_SOUND
  if (!sound_system_factory_) {
    sound_system_factory_ = new cricket::PlatformSoundSystemFactory();
  }
#endif

  if (!media_engine_) {
    media_engine_ = cricket::MediaEngine::Create(
#ifdef USE_TALK_SOUND
        sound_system_factory_
#endif
        );
  }

  media_client_ = new cricket::MediaSessionClient(
      xmpp_client_->jid(),
      session_manager_,
      media_engine_,
      new cricket::DeviceManager(
#ifdef USE_TALK_SOUND
          sound_system_factory_
#endif
          ));
  media_client_->SignalCallCreate.connect(this, &CallClient::OnCallCreate);
  media_client_->SignalDevicesChange.connect(this,
                                             &CallClient::OnDevicesChange);
}

void CallClient::OnRequestSignaling() {
  session_manager_->OnSignalingReady();
}

void CallClient::OnSessionCreate(cricket::Session* session, bool initiate) {
  session->set_allow_local_ips(allow_local_ips_);
  session->set_current_protocol(initial_protocol_);
}

void CallClient::OnCallCreate(cricket::Call* call) {
  call->SignalSessionState.connect(this, &CallClient::OnSessionState);
  if (call->video()) {
    local_renderer_ = new NullRenderer("local");
    remote_renderer_ = new NullRenderer("remote");
  }
}

void CallClient::OnSessionState(cricket::Call* call,
                                cricket::BaseSession* session,
                                cricket::BaseSession::State state) {
  if (state == cricket::Session::STATE_RECEIVEDINITIATE) {
    buzz::Jid jid(session->remote_name());
    console_->Printf("Incoming call from '%s'", jid.Str().c_str());
    call_ = call;
    session_ = session;
    incoming_call_ = true;
    cricket::CallOptions options;
    if (auto_accept_) {
      Accept(options);
    }
  } else if (state == cricket::Session::STATE_SENTINITIATE) {
    console_->Print("calling...");
  } else if (state == cricket::Session::STATE_RECEIVEDACCEPT) {
    console_->Print("call answered");
  } else if (state == cricket::Session::STATE_RECEIVEDREJECT) {
    console_->Print("call not answered");
  } else if (state == cricket::Session::STATE_INPROGRESS) {
    console_->Print("call in progress");
  } else if (state == cricket::Session::STATE_RECEIVEDTERMINATE) {
    console_->Print("other side hung up");
  }
}

void CallClient::InitPresence() {
  presence_push_ = new buzz::PresencePushTask(xmpp_client_, this);
  presence_push_->SignalStatusUpdate.connect(
    this, &CallClient::OnStatusUpdate);
  presence_push_->SignalMucJoined.connect(this, &CallClient::OnMucJoined);
  presence_push_->SignalMucLeft.connect(this, &CallClient::OnMucLeft);
  presence_push_->SignalMucStatusUpdate.connect(
    this, &CallClient::OnMucStatusUpdate);
  presence_push_->Start();

  presence_out_ = new buzz::PresenceOutTask(xmpp_client_);
  RefreshStatus();
  presence_out_->Start();

  muc_invite_recv_ = new buzz::MucInviteRecvTask(xmpp_client_);
  muc_invite_recv_->SignalInviteReceived.connect(this,
      &CallClient::OnMucInviteReceived);
  muc_invite_recv_->Start();

  muc_invite_send_ = new buzz::MucInviteSendTask(xmpp_client_);
  muc_invite_send_->Start();

  friend_invite_send_ = new buzz::FriendInviteSendTask(xmpp_client_);
  friend_invite_send_->Start();
}

void CallClient::RefreshStatus() {
  int media_caps = media_client_->GetCapabilities();
  my_status_.set_jid(xmpp_client_->jid());
  my_status_.set_available(true);
  my_status_.set_show(buzz::Status::SHOW_ONLINE);
  my_status_.set_priority(0);
  my_status_.set_know_capabilities(true);
  my_status_.set_pmuc_capability(true);
  my_status_.set_phone_capability(
      (media_caps & cricket::MediaEngine::AUDIO_RECV) != 0);
  my_status_.set_video_capability(
      (media_caps & cricket::MediaEngine::VIDEO_RECV) != 0);
  my_status_.set_camera_capability(
      (media_caps & cricket::MediaEngine::VIDEO_SEND) != 0);
  my_status_.set_is_google_client(true);
  my_status_.set_version("1.0.0.67");
  presence_out_->Send(my_status_);
}

void CallClient::OnStatusUpdate(const buzz::Status& status) {
  RosterItem item;
  item.jid = status.jid();
  item.show = status.show();
  item.status = status.status();

  std::string key = item.jid.Str();

  if (status.available() && status.phone_capability()) {
     console_->Printf("Adding to roster: %s", key.c_str());
    (*roster_)[key] = item;
  } else {
    console_->Printf("Removing from roster: %s", key.c_str());
    RosterMap::iterator iter = roster_->find(key);
    if (iter != roster_->end())
      roster_->erase(iter);
  }
}

void CallClient::PrintRoster() {
  console_->SetPrompting(false);
  console_->Printf("Roster contains %d callable", roster_->size());
  RosterMap::iterator iter = roster_->begin();
  while (iter != roster_->end()) {
    console_->Printf("%s - %s",
                     iter->second.jid.BareJid().Str().c_str(),
                     DescribeStatus(iter->second.show, iter->second.status));
    iter++;
  }
  console_->SetPrompting(true);
}

void CallClient::SendChat(const std::string& to, const std::string msg) {
  buzz::XmlElement* stanza = new buzz::XmlElement(buzz::QN_MESSAGE);
  stanza->AddAttr(buzz::QN_TO, to);
  stanza->AddAttr(buzz::QN_ID, talk_base::CreateRandomString(16));
  stanza->AddAttr(buzz::QN_TYPE, "chat");
  buzz::XmlElement* body = new buzz::XmlElement(buzz::QN_BODY);
  body->SetBodyText(msg);
  stanza->AddElement(body);

  xmpp_client_->SendStanza(stanza);
  delete stanza;
}

void CallClient::InviteFriend(const std::string& name) {
  buzz::Jid jid(name);
  if (!jid.IsValid() || jid.node() == "") {
    console_->Printf("Invalid JID. JIDs should be in the form user@domain\n");
    return;
  }
  // Note: for some reason the Buzz backend does not forward our presence
  // subscription requests to the end user when that user is another call
  // client as opposed to a Smurf user. Thus, in that scenario, you must
  // run the friend command as the other user too to create the linkage
  // (and you won't be notified to do so).
  friend_invite_send_->Send(jid);
  console_->Printf("Requesting to befriend %s.\n", name.c_str());
}

void CallClient::MakeCallTo(const std::string& name,
                            const cricket::CallOptions& given_options) {
  // Copy so we can change .is_muc.
  cricket::CallOptions options = given_options;

  bool found = false;
  options.is_muc = false;
  buzz::Jid callto_jid(name);
  buzz::Jid found_jid;
  if (name.length() == 0 && mucs_.size() > 0) {
    // if no name, and in a MUC, establish audio with the MUC
    found_jid = mucs_.begin()->first;
    found = true;
    options.is_muc = true;
  } else if (name[0] == '+') {
    // if the first character is a +, assume it's a phone number
    found_jid = callto_jid;
    found = true;
  } else if (callto_jid.resource() == "voicemail") {
    // if the resource is /voicemail, allow that
    found_jid = callto_jid;
    found = true;
  } else {
    // otherwise, it's a friend
    for (RosterMap::iterator iter = roster_->begin();
         iter != roster_->end(); ++iter) {
      if (iter->second.jid.BareEquals(callto_jid)) {
        found = true;
        found_jid = iter->second.jid;
        break;
      }
    }

    if (!found) {
      if (mucs_.count(callto_jid) == 1 &&
          mucs_[callto_jid]->state() == buzz::Muc::MUC_JOINED) {
        found = true;
        found_jid = callto_jid;
        options.is_muc = true;
      }
    }
  }

  if (found) {
    console_->Printf("Found %s '%s'", options.is_muc ? "room" : "online friend",
        found_jid.Str().c_str());
    PlaceCall(found_jid, options);
  } else {
    console_->Printf("Could not find online friend '%s'", name.c_str());
  }
}

void CallClient::PlaceCall(const buzz::Jid& jid,
                           const cricket::CallOptions& options) {
  media_client_->SignalCallDestroy.connect(
      this, &CallClient::OnCallDestroy);
  if (!call_) {
    call_ = media_client_->CreateCall();
    console_->SetPrompt(jid.Str().c_str());
    session_ = call_->InitiateSession(jid, options);
    if (options.is_muc) {
      // If people in this room are already in a call, must add all their
      // streams.
      buzz::Muc::MemberMap& members = mucs_[jid]->members();
      for (buzz::Muc::MemberMap::iterator elem = members.begin();
           elem != members.end();
           ++elem) {
        AddStream(elem->second.audio_src_id(), elem->second.video_src_id());
      }
    }
  }
  media_client_->SetFocus(call_);
  if (call_->video()) {
    call_->SetLocalRenderer(local_renderer_);
    // TODO: Call this once for every different remote SSRC
    // once we get to testing multiway video.
    call_->SetVideoRenderer(session_, 0, remote_renderer_);
  }
}

void CallClient::CallVoicemail(const std::string& name) {
  buzz::Jid jid(name);
  if (!jid.IsValid() || jid.node() == "") {
    console_->Printf("Invalid JID. JIDs should be in the form user@domain\n");
    return;
  }
  buzz::VoicemailJidRequester *request =
    new buzz::VoicemailJidRequester(xmpp_client_, jid, my_status_.jid());
  request->SignalGotVoicemailJid.connect(this,
                                         &CallClient::OnFoundVoicemailJid);
  request->SignalVoicemailJidError.connect(this,
                                           &CallClient::OnVoicemailJidError);
  request->Start();
}

void CallClient::OnFoundVoicemailJid(const buzz::Jid& to,
                                     const buzz::Jid& voicemail) {
  console_->Printf("Calling %s's voicemail.\n", to.Str().c_str());
  PlaceCall(voicemail, cricket::CallOptions());
}

void CallClient::OnVoicemailJidError(const buzz::Jid& to) {
  console_->Printf("Unable to voicemail %s.\n", to.Str().c_str());
}

void CallClient::AddStream(uint32 audio_src_id, uint32 video_src_id) {
  if (audio_src_id || video_src_id) {
    console_->Printf("Adding stream (%u, %u)\n", audio_src_id, video_src_id);
    call_->AddStream(session_, audio_src_id, video_src_id);
  }
}

void CallClient::RemoveStream(uint32 audio_src_id, uint32 video_src_id) {
  if (audio_src_id || video_src_id) {
    console_->Printf("Removing stream (%u, %u)\n", audio_src_id, video_src_id);
    call_->RemoveStream(session_, audio_src_id, video_src_id);
  }
}

void CallClient::Accept(const cricket::CallOptions& options) {
  ASSERT(call_ && incoming_call_);
  ASSERT(call_->sessions().size() == 1);
  call_->AcceptSession(call_->sessions()[0], options);
  media_client_->SetFocus(call_);
  if (call_->video()) {
    call_->SetLocalRenderer(local_renderer_);
    // The client never does an accept for multiway, so this must be 1:1,
    // so there's no SSRC.
    call_->SetVideoRenderer(session_, 0, remote_renderer_);
  }
  incoming_call_ = false;
}

void CallClient::Reject() {
  ASSERT(call_ && incoming_call_);
  call_->RejectSession(call_->sessions()[0]);
  incoming_call_ = false;
}

void CallClient::Quit() {
  talk_base::Thread::Current()->Quit();
}

void CallClient::JoinMuc(const std::string& room) {
  buzz::Jid room_jid;
  if (room.length() > 0) {
    room_jid = buzz::Jid(room);
  } else {
    // generate a GUID of the form XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX,
    // for an eventual JID of private-chat-<GUID>@groupchat.google.com
    char guid[37], guid_room[256];
    for (size_t i = 0; i < ARRAY_SIZE(guid) - 1;) {
      if (i == 8 || i == 13 || i == 18 || i == 23) {
        guid[i++] = '-';
      } else {
        sprintf(guid + i, "%04x", rand());
        i += 4;
      }
    }

    talk_base::sprintfn(guid_room, ARRAY_SIZE(guid_room),
                        "private-chat-%s@%s", guid, pmuc_domain_.c_str());
    room_jid = buzz::Jid(guid_room);
  }

  if (!room_jid.IsValid()) {
    console_->Printf("Unable to make valid muc endpoint for %s", room.c_str());
    return;
  }

  MucMap::iterator elem = mucs_.find(room_jid);
  if (elem != mucs_.end()) {
    console_->Printf("This MUC already exists.");
    return;
  }

  buzz::Muc* muc = new buzz::Muc(room_jid, xmpp_client_->jid().node());
  mucs_[room_jid] = muc;
  presence_out_->SendDirected(muc->local_jid(), my_status_);
}

void CallClient::OnMucInviteReceived(const buzz::Jid& inviter,
    const buzz::Jid& room,
    const std::vector<buzz::AvailableMediaEntry>& avail) {

  console_->Printf("Invited to join %s by %s.\n", room.Str().c_str(),
      inviter.Str().c_str());
  console_->Printf("Available media:\n");
  if (avail.size() > 0) {
    for (std::vector<buzz::AvailableMediaEntry>::const_iterator i =
            avail.begin();
        i != avail.end();
        ++i) {
      console_->Printf("  %s, %s\n",
          buzz::AvailableMediaEntry::TypeAsString(i->type),
          buzz::AvailableMediaEntry::StatusAsString(i->status));
    }
  } else {
    console_->Printf("  None\n");
  }
  // We automatically join the room.
  JoinMuc(room.Str());
}

void CallClient::OnMucJoined(const buzz::Jid& endpoint) {
  MucMap::iterator elem = mucs_.find(endpoint);
  ASSERT(elem != mucs_.end() &&
         elem->second->state() == buzz::Muc::MUC_JOINING);

  buzz::Muc* muc = elem->second;
  muc->set_state(buzz::Muc::MUC_JOINED);
  console_->Printf("Joined \"%s\"", muc->jid().Str().c_str());
}

void CallClient::OnMucStatusUpdate(const buzz::Jid& jid,
    const buzz::MucStatus& status) {

  // Look up this muc.
  MucMap::iterator elem = mucs_.find(jid);
  ASSERT(elem != mucs_.end() &&
         elem->second->state() == buzz::Muc::MUC_JOINED);

  buzz::Muc* muc = elem->second;

  if (status.jid().IsBare() || status.jid() == muc->local_jid()) {
    // We are only interested in status about other users.
    return;
  }

  if (!status.available()) {
    // User is leaving the room.
    buzz::Muc::MemberMap::iterator elem =
      muc->members().find(status.jid().resource());

    ASSERT(elem != muc->members().end());

    // If user had src-ids, they have the left the room without explicitly
    // hanging-up; must tear down the stream if in a call to this room.
    if (call_ && session_->remote_name() == muc->jid().Str()) {
      RemoveStream(elem->second.audio_src_id(), elem->second.video_src_id());
    }

    // Remove them from the room.
    muc->members().erase(elem);
  } else {
    // Either user has joined or something changed about them.
    // Note: The [] operator here will create a new entry if it does not
    // exist, which is what we want.
    buzz::MucStatus& member_status(
        muc->members()[status.jid().resource()]);
    if (call_ && session_->remote_name() == muc->jid().Str()) {
      // We are in a call to this muc. Must potentially update our streams.
      // The following code will correctly update our streams regardless of
      // whether the SSRCs have been removed, added, or changed and regardless
      // of whether that has been done to both or just one. This relies on the
      // fact that AddStream/RemoveStream do nothing for SSRC arguments that are
      // zero.
      uint32 remove_audio_src_id = 0;
      uint32 remove_video_src_id = 0;
      uint32 add_audio_src_id = 0;
      uint32 add_video_src_id = 0;
      if (member_status.audio_src_id() != status.audio_src_id()) {
        remove_audio_src_id = member_status.audio_src_id();
        add_audio_src_id = status.audio_src_id();
      }
      if (member_status.video_src_id() != status.video_src_id()) {
        remove_video_src_id = member_status.video_src_id();
        add_video_src_id = status.video_src_id();
      }
      // Remove the old SSRCs, if any.
      RemoveStream(remove_audio_src_id, remove_video_src_id);
      // Add the new SSRCs, if any.
      AddStream(add_audio_src_id, add_video_src_id);
    }
    // Update the status. This will use the compiler-generated copy
    // constructor, which is perfectly adequate for this class.
    member_status = status;
  }
}

void CallClient::LeaveMuc(const std::string& room) {
  buzz::Jid room_jid;
  if (room.length() > 0) {
    room_jid = buzz::Jid(room);
  } else if (mucs_.size() > 0) {
    // leave the first MUC if no JID specified
    room_jid = mucs_.begin()->first;
  }

  if (!room_jid.IsValid()) {
    console_->Printf("Invalid MUC JID.");
    return;
  }

  MucMap::iterator elem = mucs_.find(room_jid);
  if (elem == mucs_.end()) {
    console_->Printf("No such MUC.");
    return;
  }

  buzz::Muc* muc = elem->second;
  muc->set_state(buzz::Muc::MUC_LEAVING);

  buzz::Status status;
  status.set_jid(my_status_.jid());
  status.set_available(false);
  status.set_priority(0);
  presence_out_->SendDirected(muc->local_jid(), status);
}

void CallClient::OnMucLeft(const buzz::Jid& endpoint, int error) {
  // We could be kicked from a room from any state.  We would hope this
  // happens While in the MUC_LEAVING state
  MucMap::iterator elem = mucs_.find(endpoint);
  if (elem == mucs_.end())
    return;

  buzz::Muc* muc = elem->second;
  if (muc->state() == buzz::Muc::MUC_JOINING) {
    console_->Printf("Failed to join \"%s\", code=%d",
                     muc->jid().Str().c_str(), error);
  } else if (muc->state() == buzz::Muc::MUC_JOINED) {
    console_->Printf("Kicked from \"%s\"",
                     muc->jid().Str().c_str());
  }

  delete muc;
  mucs_.erase(elem);
}

void CallClient::InviteToMuc(const std::string& user, const std::string& room) {
  // First find the room.
  const buzz::Muc* found_muc;
  if (room.length() == 0) {
    if (mucs_.size() == 0) {
      console_->Printf("Not in a room yet; can't invite.\n");
      return;
    }
    // Invite to the first muc
    found_muc = mucs_.begin()->second;
  } else {
    MucMap::iterator elem = mucs_.find(buzz::Jid(room));
    if (elem == mucs_.end()) {
      console_->Printf("Not in room %s.\n", room.c_str());
      return;
    }
    found_muc = elem->second;
  }
  // Now find the user. We invite all of their resources.
  bool found_user = false;
  buzz::Jid user_jid(user);
  for (RosterMap::iterator iter = roster_->begin();
       iter != roster_->end(); ++iter) {
    if (iter->second.jid.BareEquals(user_jid)) {
      muc_invite_send_->Send(iter->second.jid, *found_muc);
      found_user = true;
    }
  }
  if (!found_user) {
    console_->Printf("No such friend as %s.\n", user.c_str());
    return;
  }
}

void CallClient::GetDevices() {
  std::vector<std::string> names;
  media_client_->GetAudioInputDevices(&names);
  printf("Audio input devices:\n");
  PrintDevices(names);
  media_client_->GetAudioOutputDevices(&names);
  printf("Audio output devices:\n");
  PrintDevices(names);
  media_client_->GetVideoCaptureDevices(&names);
  printf("Video capture devices:\n");
  PrintDevices(names);
}

void CallClient::PrintDevices(const std::vector<std::string>& names) {
  for (size_t i = 0; i < names.size(); ++i) {
    printf("%d: %s\n", static_cast<int>(i), names[i].c_str());
  }
}

void CallClient::OnDevicesChange() {
  printf("Devices changed.\n");
  RefreshStatus();
}

void CallClient::SetVolume(const std::string& level) {
  media_client_->SetOutputVolume(strtol(level.c_str(), NULL, 10));
}
