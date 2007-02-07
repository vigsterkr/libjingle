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

#ifndef CRICKET_EXAMPLES_CALL_CALLCLIENT_H__
#define CRICKET_EXAMPLES_CALL_CALLCLIENT_H__

#include <map>
#include <string>
#include "talk/base/autodetectproxy.h"
#include "talk/p2p/base/session.h"
#include "talk/p2p/client/httpportallocator.h"
#include "talk/xmpp/xmppclient.h"
#include "talk/examples/login/status.h"
#include "talk/examples/call/console.h"

namespace buzz {
class PresencePushTask;
class Status;
}

namespace talk_base {
class Thread;
class NetworkManager;
}

namespace cricket {
class PortAllocator;
class PhoneSessionClient;
class Receiver;
class Call;
class SessionManagerTask;
}

struct RosterItem {
  buzz::Jid jid;
  buzz::Status::Show show;
  std::string status;
};

class CallClient: public sigslot::has_slots<> {
public:
  CallClient(buzz::XmppClient* xmpp_client);
  ~CallClient();

  cricket::PhoneSessionClient* phone_client() const { return phone_client_; }

  void PrintRoster();
  void MakeCallTo(const std::string& name);
  void SetConsole(Console *console) {console_ = console;}
  void ParseLine(const std::string &str);

private:
  typedef std::map<std::string,RosterItem> RosterMap;

  Console *console_;
  buzz::XmppClient* xmpp_client_;
  talk_base::Thread* worker_thread_;
  talk_base::NetworkManager network_manager_;
  talk_base::AutoDetectProxy *proxy_detect_;
  cricket::HttpPortAllocator* port_allocator_;
  cricket::SessionManager* session_manager_;
  cricket::SessionManagerTask* session_manager_task_;
  cricket::PhoneSessionClient* phone_client_;
  
  cricket::Call* call_; 
  cricket::Session *session_;
  bool incoming_call_;

  buzz::PresencePushTask* presence_push_;
  RosterMap* roster_;

  void OnStateChange(buzz::XmppEngine::State state);
  void OnJingleInfo(const std::string &relay_token, const std::vector<std::string> &relay_hosts, 
		    const std::vector<talk_base::SocketAddress> &stun_hosts);
  void OnProxyDetect(talk_base::SignalThread *thread);
  void InitPhone();
  void OnRequestSignaling();
  void OnCallCreate(cricket::Call* call);
  void OnCallDestroy(cricket::Call* call);
  const std::string strerror(buzz::XmppEngine::Error err);
  void OnSessionState(cricket::Call* call,
                      cricket::Session* session,
                      cricket::Session::State state);

  void InitPresence();
  void OnStatusUpdate(const buzz::Status& status);
};

#endif // CRICKET_EXAMPLES_CALL_CALLCLIENT_H__
