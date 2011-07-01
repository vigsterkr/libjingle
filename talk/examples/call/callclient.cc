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

#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/network.h"
#include "talk/base/socketaddress.h"
#include "talk/base/stringencode.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/examples/call/console.h"
#include "talk/examples/call/presencepushtask.h"
#include "talk/examples/call/presenceouttask.h"
#include "talk/examples/call/mucinviterecvtask.h"
#include "talk/examples/call/mucinvitesendtask.h"
#include "talk/examples/call/friendinvitesendtask.h"
#include "talk/examples/call/muc.h"
#include "talk/examples/call/voicemailjidrequester.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/client/sessionmanagertask.h"
#include "talk/session/phone/devicemanager.h"
#include "talk/session/phone/mediaengine.h"
#include "talk/session/phone/mediamessages.h"
#include "talk/session/phone/mediasessionclient.h"
#include "talk/session/phone/videorendererfactory.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/mucroomlookuptask.h"

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
"  join [room_jid]     Joins a multi-user-chat with room JID.\n"
"  ljoin [room_name]   Joins a MUC by looking up JID from room name.\n"
"  invite user [room]  Invites a friend to a multi-user-chat.\n"
"  leave [room]        Leaves a multi-user-chat.\n"
"  nick [nick]         Sets the nick.\n"
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
      console_->PrintLine(RECEIVE_COMMANDS);
    }
  } else if (call_) {
    if (command == "hangup") {
      call_->Terminate();
    } else if (command == "mute") {
      call_->Mute(true);
    } else if (command == "unmute") {
      call_->Mute(false);
    } else if ((command == "dtmf") && (words.size() == 2)) {
      int ev = std::string("0123456789*#").find(words[1][0]);
      call_->PressDTMF(ev);
    } else {
      console_->PrintLine(CALL_COMMANDS);
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
        console_->PrintLine(
            "Invalid JID. JIDs should be in the form user@domain");
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
    } else if (command == "ljoin") {
      LookupAndJoinMuc(GetWord(words, 1, ""));
    } else if ((words.size() >= 2) && (command == "invite")) {
      InviteToMuc(words[1], GetWord(words, 2, ""));
    } else if (command == "leave") {
      LeaveMuc(GetWord(words, 1, ""));
    } else if (command == "nick") {
      SetNick(GetWord(words, 1, ""));
    } else if (command == "getdevs") {
      GetDevices();
    } else if ((words.size() == 2) && (command == "setvol")) {
      SetVolume(words[1]);
    } else if (command == "voicemail") {
      CallVoicemail((words.size() >= 2) ? words[1] : "");
    } else {
      console_->PrintLine(CONSOLE_COMMANDS);
    }
  }
}

CallClient::CallClient(buzz::XmppClient* xmpp_client)
    : xmpp_client_(xmpp_client),
      media_engine_(NULL),
      media_client_(NULL),
      call_(NULL),
      incoming_call_(false),
      auto_accept_(false),
      pmuc_domain_("groupchat.google.com"),
      local_renderer_(NULL),
      remote_renderer_(NULL),
      static_views_accumulated_count_(0),
      roster_(new RosterMap),
      portallocator_flags_(0),
      allow_local_ips_(false),
      initial_protocol_(cricket::PROTOCOL_HYBRID),
      secure_policy_(cricket::SEC_DISABLED) {
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
    RemoveAllStaticRenderedViews();
    console_->PrintLine("call destroyed");
    call_ = NULL;
    session_ = NULL;
  }
}

void CallClient::OnStateChange(buzz::XmppEngine::State state) {
  switch (state) {
  case buzz::XmppEngine::STATE_START:
    console_->PrintLine("connecting...");
    break;

  case buzz::XmppEngine::STATE_OPENING:
    console_->PrintLine("logging in...");
    break;

  case buzz::XmppEngine::STATE_OPEN:
    console_->PrintLine("logged in...");
    InitMedia();
    InitPresence();
    break;

  case buzz::XmppEngine::STATE_CLOSED:
    buzz::XmppEngine::Error error = xmpp_client_->GetError(NULL);
    console_->PrintLine("logged out... %s", strerror(error).c_str());
    Quit();
  }
}

void CallClient::InitMedia() {
  std::string client_unique = xmpp_client_->jid().Str();
  talk_base::InitRandom(client_unique.c_str(), client_unique.size());

  worker_thread_ = new talk_base::Thread();
  // The worker thread must be started here since initialization of
  // the ChannelManager will generate messages that need to be
  // dispatched by it.
  worker_thread_->Start();

  // TODO: It looks like we are leaking many objects. E.g.
  // |network_manager_| is never deleted.

  network_manager_ = new talk_base::NetworkManager();

  // TODO: Decide if the relay address should be specified here.
  talk_base::SocketAddress stun_addr("stun.l.google.com", 19302);
  port_allocator_ =  new cricket::BasicPortAllocator(
      network_manager_, stun_addr, talk_base::SocketAddress(),
      talk_base::SocketAddress(), talk_base::SocketAddress());

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

  if (!media_engine_) {
    media_engine_ = cricket::MediaEngine::Create();
  }

  media_client_ = new cricket::MediaSessionClient(
      xmpp_client_->jid(),
      session_manager_,
      media_engine_,
      new cricket::DeviceManager());
  media_client_->SignalCallCreate.connect(this, &CallClient::OnCallCreate);
  media_client_->SignalCallDestroy.connect(this, &CallClient::OnCallDestroy);
  media_client_->SignalDevicesChange.connect(this,
                                             &CallClient::OnDevicesChange);
  media_client_->set_secure(secure_policy_);
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
  call->SignalMediaSourcesUpdate.connect(
      this, &CallClient::OnMediaSourcesUpdate);
}

void CallClient::OnSessionState(cricket::Call* call,
                                cricket::BaseSession* session,
                                cricket::BaseSession::State state) {
  if (state == cricket::Session::STATE_RECEIVEDINITIATE) {
    buzz::Jid jid(session->remote_name());
    console_->PrintLine("Incoming call from '%s'", jid.Str().c_str());
    call_ = call;
    session_ = session;
    incoming_call_ = true;
    if (call->video()) {
      local_renderer_ =
          cricket::VideoRendererFactory::CreateGuiVideoRenderer(160, 100);
      remote_renderer_ =
          cricket::VideoRendererFactory::CreateGuiVideoRenderer(160, 100);
    }
    cricket::CallOptions options;
    if (auto_accept_) {
      Accept(options);
    }
  } else if (state == cricket::Session::STATE_SENTINITIATE) {
    if (call->video()) {
      local_renderer_ =
          cricket::VideoRendererFactory::CreateGuiVideoRenderer(160, 100);
      remote_renderer_ =
          cricket::VideoRendererFactory::CreateGuiVideoRenderer(160, 100);
    }
    console_->PrintLine("calling...");
  } else if (state == cricket::Session::STATE_RECEIVEDACCEPT) {
    console_->PrintLine("call answered");
  } else if (state == cricket::Session::STATE_RECEIVEDREJECT) {
    console_->PrintLine("call not answered");
  } else if (state == cricket::Session::STATE_INPROGRESS) {
    console_->PrintLine("call in progress");
    call->SignalSpeakerMonitor.connect(this, &CallClient::OnSpeakerChanged);
    call->StartSpeakerMonitor(session);
  } else if (state == cricket::Session::STATE_RECEIVEDTERMINATE) {
    console_->PrintLine("other side hung up");
  }
}

void CallClient::OnSpeakerChanged(cricket::Call* call,
                                  cricket::BaseSession* session,
                                  const cricket::NamedSource& speaker) {
  if (speaker.ssrc == 0) {
    console_->PrintLine("Session %s has no current speaker.",
                        session->id().c_str());
  } else if (speaker.nick.empty()) {
    console_->PrintLine("Session %s speaker change to unknown (%u).",
                        session->id().c_str(), speaker.ssrc);
  } else {
    console_->PrintLine("Session %s speaker changed to %s (%u).",
                        session->id().c_str(), speaker.nick.c_str(),
                        speaker.ssrc);
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
  my_status_.set_voice_capability(
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

  if (status.available() && status.voice_capability()) {
     console_->PrintLine("Adding to roster: %s", key.c_str());
    (*roster_)[key] = item;
    // TODO: Make some of these constants.
  } else {
    console_->PrintLine("Removing from roster: %s", key.c_str());
    RosterMap::iterator iter = roster_->find(key);
    if (iter != roster_->end())
      roster_->erase(iter);
  }
}

void CallClient::PrintRoster() {
  console_->PrintLine("Roster contains %d callable", roster_->size());
  RosterMap::iterator iter = roster_->begin();
  while (iter != roster_->end()) {
    console_->PrintLine("%s - %s",
                        iter->second.jid.BareJid().Str().c_str(),
                        DescribeStatus(iter->second.show, iter->second.status));
    iter++;
  }
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
    console_->PrintLine("Invalid JID. JIDs should be in the form user@domain.");
    return;
  }
  // Note: for some reason the Buzz backend does not forward our presence
  // subscription requests to the end user when that user is another call
  // client as opposed to a Smurf user. Thus, in that scenario, you must
  // run the friend command as the other user too to create the linkage
  // (and you won't be notified to do so).
  friend_invite_send_->Send(jid);
  console_->PrintLine("Requesting to befriend %s.", name.c_str());
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
    console_->PrintLine("Found %s '%s'",
                        options.is_muc ? "room" : "online friend",
                        found_jid.Str().c_str());
    PlaceCall(found_jid, options);
  } else {
    console_->PrintLine("Could not find online friend '%s'", name.c_str());
  }
}

void CallClient::PlaceCall(const buzz::Jid& jid,
                           const cricket::CallOptions& options) {
  if (!call_) {
    call_ = media_client_->CreateCall();
    session_ = call_->InitiateSession(jid, options);
  }
  media_client_->SetFocus(call_);
  if (call_->video()) {
    if (!options.is_muc) {
      call_->SetLocalRenderer(local_renderer_);
      call_->SetVideoRenderer(session_, 0, remote_renderer_);
    }
  }
}

void CallClient::CallVoicemail(const std::string& name) {
  buzz::Jid jid(name);
  if (!jid.IsValid() || jid.node() == "") {
    console_->PrintLine("Invalid JID. JIDs should be in the form user@domain.");
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
  console_->PrintLine("Calling %s's voicemail.", to.Str().c_str());
  PlaceCall(voicemail, cricket::CallOptions());
}

void CallClient::OnVoicemailJidError(const buzz::Jid& to) {
  console_->PrintLine("Unable to voicemail %s.", to.Str().c_str());
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

void CallClient::SetNick(const std::string& muc_nick) {
  my_status_.set_nick(muc_nick);

  // TODO: We might want to re-send presence, but right
  // now, it appears to be ignored by the MUC.
  //
  // presence_out_->Send(my_status_); for (MucMap::const_iterator itr
  // = mucs_.begin(); itr != mucs_.end(); ++itr) {
  // presence_out_->SendDirected(itr->second->local_jid(),
  // my_status_); }

  console_->PrintLine("Nick set to '%s'.", muc_nick.c_str());
}

void CallClient::LookupAndJoinMuc(const std::string& room_name) {
  // The room_name can't be empty for lookup task.
  if (room_name.empty()) {
    console_->PrintLine("Please provide a room name or room jid.");
    return;
  }

  std::string room = room_name;
  std::string domain =  xmpp_client_->jid().domain();
  if (room_name.find("@") != std::string::npos) {
    // Assume the room_name is a fully qualified room name.
    // We'll find the room name string and domain name string from it.
    room = room_name.substr(0, room_name.find("@"));
    domain = room_name.substr(room_name.find("@") + 1);
  }

  buzz::MucRoomLookupTask* lookup_query_task =
      new buzz::MucRoomLookupTask(xmpp_client_, room, domain);
  lookup_query_task->SignalResult.connect(this,
      &CallClient::OnRoomLookupResponse);
  lookup_query_task->SignalError.connect(this,
      &CallClient::OnRoomLookupError);
  lookup_query_task->Start();
}

void CallClient::JoinMuc(const std::string& room_jid_str) {
  if (room_jid_str.empty()) {
    buzz::Jid room_jid = GenerateRandomMucJid();
    console_->PrintLine("Generated a random room jid: %s",
                        room_jid.Str().c_str());
    JoinMuc(room_jid);
  } else {
    JoinMuc(buzz::Jid(room_jid_str));
  }
}

void CallClient::JoinMuc(const buzz::Jid& room_jid) {
  if (!room_jid.IsValid()) {
    console_->PrintLine("Unable to make valid muc endpoint for %s",
                        room_jid.Str().c_str());
    return;
  }

  std::string room_nick = room_jid.resource();
  if (room_nick.empty()) {
    room_nick = (xmpp_client_->jid().node()
                 + "_" + xmpp_client_->jid().resource());
  }

  MucMap::iterator elem = mucs_.find(room_jid);
  if (elem != mucs_.end()) {
    console_->PrintLine("This MUC already exists.");
    return;
  }

  buzz::Muc* muc = new buzz::Muc(room_jid.BareJid(), room_nick);
  mucs_[muc->jid()] = muc;
  presence_out_->SendDirected(muc->local_jid(), my_status_);
}

void CallClient::OnRoomLookupResponse(const buzz::MucRoomInfo& room_info) {
  JoinMuc(room_info.room_jid);
}

void CallClient::OnRoomLookupError(const buzz::XmlElement* stanza) {
  console_->PrintLine("Failed to look up the room_jid. %s",
                    stanza->Str().c_str());
}

void CallClient::OnMucInviteReceived(const buzz::Jid& inviter,
    const buzz::Jid& room,
    const std::vector<buzz::AvailableMediaEntry>& avail) {

  console_->PrintLine("Invited to join %s by %s.", room.Str().c_str(),
      inviter.Str().c_str());
  console_->PrintLine("Available media:");
  if (avail.size() > 0) {
    for (std::vector<buzz::AvailableMediaEntry>::const_iterator i =
            avail.begin();
        i != avail.end();
        ++i) {
      console_->PrintLine("  %s, %s",
                          buzz::AvailableMediaEntry::TypeAsString(i->type),
                          buzz::AvailableMediaEntry::StatusAsString(i->status));
    }
  } else {
    console_->PrintLine("  None");
  }
  // We automatically join the room.
  JoinMuc(room);
}

void CallClient::OnMucJoined(const buzz::Jid& endpoint) {
  MucMap::iterator elem = mucs_.find(endpoint);
  ASSERT(elem != mucs_.end() &&
         elem->second->state() == buzz::Muc::MUC_JOINING);

  buzz::Muc* muc = elem->second;
  muc->set_state(buzz::Muc::MUC_JOINED);
  console_->PrintLine("Joined \"%s\"", muc->jid().Str().c_str());
}

void CallClient::OnMucStatusUpdate(const buzz::Jid& jid,
    const buzz::MucStatus& status) {

  // Look up this muc.
  MucMap::iterator elem = mucs_.find(jid);
  ASSERT(elem != mucs_.end());

  buzz::Muc* muc = elem->second;

  if (status.jid().IsBare() || status.jid() == muc->local_jid()) {
    // We are only interested in status about other users.
    return;
  }

  if (!status.available()) {
    // Remove them from the room.
    muc->members().erase(status.jid().resource());
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
    console_->PrintLine("Invalid MUC JID.");
    return;
  }

  MucMap::iterator elem = mucs_.find(room_jid);
  if (elem == mucs_.end()) {
    console_->PrintLine("No such MUC.");
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
    console_->PrintLine("Failed to join \"%s\", code=%d",
                        muc->jid().Str().c_str(), error);
  } else if (muc->state() == buzz::Muc::MUC_JOINED) {
    console_->PrintLine("Kicked from \"%s\"",
                        muc->jid().Str().c_str());
  }

  delete muc;
  mucs_.erase(elem);
}

void CallClient::InviteToMuc(const std::string& given_user,
                             const std::string& room) {
  std::string user = given_user;

  // First find the room.
  const buzz::Muc* found_muc;
  if (room.length() == 0) {
    if (mucs_.size() == 0) {
      console_->PrintLine("Not in a room yet; can't invite.");
      return;
    }
    // Invite to the first muc
    found_muc = mucs_.begin()->second;
  } else {
    MucMap::iterator elem = mucs_.find(buzz::Jid(room));
    if (elem == mucs_.end()) {
      console_->PrintLine("Not in room %s.", room.c_str());
      return;
    }
    found_muc = elem->second;
  }

  buzz::Jid invite_to = found_muc->jid();

  // Now find the user. We invite all of their resources.
  bool found_user = false;
  buzz::Jid user_jid(user);
  for (RosterMap::iterator iter = roster_->begin();
       iter != roster_->end(); ++iter) {
    if (iter->second.jid.BareEquals(user_jid)) {
      buzz::Jid invitee = iter->second.jid;
      muc_invite_send_->Send(invite_to, invitee);
      found_user = true;
    }
  }
  if (!found_user) {
    buzz::Jid invitee = user_jid;
    muc_invite_send_->Send(invite_to, invitee);
  }
}

void CallClient::GetDevices() {
  std::vector<std::string> names;
  media_client_->GetAudioInputDevices(&names);
  console_->PrintLine("Audio input devices:");
  PrintDevices(names);
  media_client_->GetAudioOutputDevices(&names);
  console_->PrintLine("Audio output devices:");
  PrintDevices(names);
  media_client_->GetVideoCaptureDevices(&names);
  console_->PrintLine("Video capture devices:");
  PrintDevices(names);
}

void CallClient::PrintDevices(const std::vector<std::string>& names) {
  for (size_t i = 0; i < names.size(); ++i) {
    console_->PrintLine("%d: %s", static_cast<int>(i), names[i].c_str());
  }
}

void CallClient::OnDevicesChange() {
  console_->PrintLine("Devices changed.");
  RefreshStatus();
}

void CallClient::SetVolume(const std::string& level) {
  media_client_->SetOutputVolume(strtol(level.c_str(), NULL, 10));
}

void CallClient::OnMediaSourcesUpdate(cricket::Call* call,
                                      cricket::Session* session,
                                      const cricket::MediaSources& sources) {
  for (cricket::NamedSources::const_iterator it = sources.video().begin();
       it != sources.video().end(); ++it) {
    if (it->removed) {
      RemoveStaticRenderedView(it->ssrc);
    } else {
      // TODO: Make dimensions and positions more configurable.
      int offset = (50 * static_views_accumulated_count_) % 300;
      AddStaticRenderedView(session, it->ssrc, 640, 400, 30,
                            offset, offset);
    }
  }

  SendViewRequest(session);
}

// TODO: Would these methods to add and remove views make
// more sense in call.cc?  Would other clients use them?
void CallClient::AddStaticRenderedView(
    cricket::Session* session,
    uint32 ssrc, int width, int height, int framerate,
    int x_offset, int y_offset) {
  StaticRenderedView rendered_view(
      cricket::StaticVideoView(ssrc, width, height, framerate),
      cricket::VideoRendererFactory::CreateGuiVideoRenderer(
          x_offset, y_offset));
  rendered_view.renderer->SetSize(width, height, 0);
  call_->SetVideoRenderer(session, ssrc, rendered_view.renderer);
  static_rendered_views_.push_back(rendered_view);
  ++static_views_accumulated_count_;
  console_->PrintLine("Added renderer for ssrc %d", ssrc);
}

bool CallClient::RemoveStaticRenderedView(uint32 ssrc) {
  for (StaticRenderedViews::iterator it = static_rendered_views_.begin();
       it != static_rendered_views_.end(); ++it) {
    if (it->view.ssrc == ssrc) {
      delete it->renderer;
      static_rendered_views_.erase(it);
      console_->PrintLine("Removed renderer for ssrc %d", ssrc);
      return true;
    }
  }
  return false;
}

void CallClient::RemoveAllStaticRenderedViews() {
  for (StaticRenderedViews::iterator it = static_rendered_views_.begin();
       it != static_rendered_views_.end(); ++it) {
    delete it->renderer;
  }
  static_rendered_views_.clear();
}

void CallClient::SendViewRequest(cricket::Session* session) {
  cricket::ViewRequest request;
  for (StaticRenderedViews::iterator it = static_rendered_views_.begin();
       it != static_rendered_views_.end(); ++it) {
    request.static_video_views.push_back(it->view);
  }
  call_->SendViewRequest(session, request);
}

buzz::Jid CallClient::GenerateRandomMucJid() {
  // Generate a GUID of the form XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX,
  // for an eventual JID of private-chat-<GUID>@groupchat.google.com.
  char guid[37], guid_room[256];
  for (size_t i = 0; i < ARRAY_SIZE(guid) - 1;) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      guid[i++] = '-';
    } else {
      sprintf(guid + i, "%04x", rand());
      i += 4;
    }
  }

  talk_base::sprintfn(guid_room,
                      ARRAY_SIZE(guid_room),
                      "private-chat-%s@%s",
                      guid,
                      pmuc_domain_.c_str());
  return buzz::Jid(guid_room);
}
