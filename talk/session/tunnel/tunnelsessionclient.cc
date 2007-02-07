/*
 * libjingle
 * Copyright 2004--2006, Google Inc.
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

#include "talk/base/basicdefs.h"
#include "talk/base/basictypes.h"
#include "talk/base/common.h"
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/xmllite/xmlelement.h"
#include "pseudotcpchannel.h"
#include "tunnelsessionclient.h"

namespace cricket {

const std::string NS_TUNNEL("http://www.google.com/talk/tunnel");
const buzz::QName QN_TUNNEL_DESCRIPTION(NS_TUNNEL, "description");
const buzz::QName QN_TUNNEL_TYPE(NS_TUNNEL, "type");

enum {
  MSG_CLOCK = 1,
  MSG_DESTROY,
  MSG_TERMINATE,
  MSG_EVENT,
  MSG_CREATE_TUNNEL,
};

struct EventData : public talk_base::MessageData {
  int event, error;
  EventData(int ev, int err = 0) : event(ev), error(err) { }
};

struct CreateTunnelData : public talk_base::MessageData {
  buzz::Jid jid;
  std::string description;
  talk_base::Thread* thread;
  talk_base::StreamInterface* stream;
};

extern const talk_base::ConstantLabel SESSION_STATES[];

const talk_base::ConstantLabel SESSION_STATES[] = {
  KLABEL(Session::STATE_INIT),
  KLABEL(Session::STATE_SENTINITIATE),
  KLABEL(Session::STATE_RECEIVEDINITIATE),
  KLABEL(Session::STATE_SENTACCEPT),
  KLABEL(Session::STATE_RECEIVEDACCEPT),
  KLABEL(Session::STATE_SENTMODIFY),
  KLABEL(Session::STATE_RECEIVEDMODIFY),
  KLABEL(Session::STATE_SENTREJECT),
  KLABEL(Session::STATE_RECEIVEDREJECT),
  KLABEL(Session::STATE_SENTREDIRECT),
  KLABEL(Session::STATE_SENTTERMINATE),
  KLABEL(Session::STATE_RECEIVEDTERMINATE),
  KLABEL(Session::STATE_INPROGRESS),
  KLABEL(Session::STATE_DEINIT),
  LASTLABEL
};

///////////////////////////////////////////////////////////////////////////////
// TunnelSessionDescription
///////////////////////////////////////////////////////////////////////////////

struct TunnelSessionDescription : public SessionDescription {
  std::string description;

  TunnelSessionDescription(const std::string& desc) : description(desc) { }
};

///////////////////////////////////////////////////////////////////////////////
// TunnelSessionClient
///////////////////////////////////////////////////////////////////////////////

TunnelSessionClient::TunnelSessionClient(const buzz::Jid& jid,
                                         SessionManager* manager)
  : jid_(jid), session_manager_(manager), shutdown_(false) {
  // Register ourselves as the handler of tunnel sessions.
  session_manager_->AddClient(NS_TUNNEL, this);
}

TunnelSessionClient::~TunnelSessionClient() {
  shutdown_ = true;
  for (std::vector<TunnelSession*>::iterator it = sessions_.begin();
       it != sessions_.end();
       ++it) {
     Session* session = (*it)->ReleaseSession(true);
     session_manager_->DestroySession(session);
  }
  session_manager_->RemoveClient(NS_TUNNEL);
}

const SessionDescription* TunnelSessionClient::CreateSessionDescription(
    const buzz::XmlElement* element) {
  if (const buzz::XmlElement* type_elem = element->FirstNamed(QN_TUNNEL_TYPE)) {
    return new TunnelSessionDescription(type_elem->BodyText());
  }
  ASSERT(false);
  return 0;
}

buzz::XmlElement* TunnelSessionClient::TranslateSessionDescription(
    const SessionDescription* description) {
  const TunnelSessionDescription* desc =
      static_cast<const TunnelSessionDescription*>(description);

  buzz::XmlElement* root = new buzz::XmlElement(QN_TUNNEL_DESCRIPTION, true);
  buzz::XmlElement* type_elem = new buzz::XmlElement(QN_TUNNEL_TYPE);
  type_elem->SetBodyText(desc->description);
  root->AddElement(type_elem);
  return root;
}

void TunnelSessionClient::OnSessionCreate(Session* session, bool received) {
  LOG(LS_INFO) << "TunnelSessionClient::OnSessionCreate: received=" << received;
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  if (received)
    sessions_.push_back(
      new TunnelSession(this, session, talk_base::Thread::Current()));
}

void TunnelSessionClient::OnSessionDestroy(Session* session) {
  LOG(LS_INFO) << "TunnelSessionClient::OnSessionDestroy";
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  if (shutdown_)
    return;
  for (std::vector<TunnelSession*>::iterator it = sessions_.begin();
       it != sessions_.end();
       ++it) {
    if ((*it)->HasSession(session)) {
      VERIFY((*it)->ReleaseSession(false) == session);
      sessions_.erase(it);
      return;
    }
  }
}

talk_base::StreamInterface* TunnelSessionClient::CreateTunnel(
    const buzz::Jid& to, const std::string& description) {
  // Valid from any thread
  CreateTunnelData data;
  data.jid = to;
  data.description = description;
  data.thread = talk_base::Thread::Current();
  session_manager_->signaling_thread()->Send(this, MSG_CREATE_TUNNEL, &data);
  return data.stream;
}

talk_base::StreamInterface* TunnelSessionClient::AcceptTunnel(
    Session* session) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  TunnelSession* tunnel = NULL;
  for (std::vector<TunnelSession*>::iterator it = sessions_.begin();
       it != sessions_.end();
       ++it) {
    if ((*it)->HasSession(session)) {
      tunnel = *it;
      break;
    }
  }
  ASSERT(tunnel != NULL);

  const TunnelSessionDescription* in_desc =
    static_cast<const TunnelSessionDescription*>(
      session->remote_description());
  TunnelSessionDescription* out_desc = new TunnelSessionDescription(
    in_desc->description);
  session->Accept(out_desc);
  return tunnel->GetStream();
}

void TunnelSessionClient::DeclineTunnel(Session* session) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  session->Reject();
}

void TunnelSessionClient::OnMessage(talk_base::Message* pmsg) {
  if (pmsg->message_id == MSG_CREATE_TUNNEL) {
    ASSERT(session_manager_->signaling_thread()->IsCurrent());
    CreateTunnelData* data = static_cast<CreateTunnelData*>(pmsg->pdata);
    Session* session = session_manager_->CreateSession(jid_.Str(), NS_TUNNEL);
    TunnelSession* tunnel = new TunnelSession(this, session, data->thread);
    sessions_.push_back(tunnel);
    TunnelSessionDescription* desc = new TunnelSessionDescription(data->description);
    session->Initiate(data->jid.Str(), NULL, desc);
    data->stream = tunnel->GetStream();
  }
}

///////////////////////////////////////////////////////////////////////////////
// TunnelSession
///////////////////////////////////////////////////////////////////////////////

//
// Signalling thread methods
//

TunnelSession::TunnelSession(TunnelSessionClient* client, Session* session,
                             talk_base::Thread* stream_thread)
    : client_(client), session_(session), channel_(NULL) {
  ASSERT(client_ != NULL);
  ASSERT(session_ != NULL);
  session_->SignalState.connect(this, &TunnelSession::OnSessionState);
  channel_ = new PseudoTcpChannel(stream_thread, session_);
  channel_->SignalChannelClosed.connect(this, &TunnelSession::OnChannelClosed);
}

TunnelSession::~TunnelSession() {
  ASSERT(client_ != NULL);
  ASSERT(session_ == NULL);
  ASSERT(channel_ == NULL);
}

talk_base::StreamInterface* TunnelSession::GetStream() {
  ASSERT(channel_ != NULL);
  return channel_->GetStream();
}

bool TunnelSession::HasSession(Session* session) {
  ASSERT(NULL != session_);
  return (session_ == session);
}

Session* TunnelSession::ReleaseSession(bool channel_exists) {
  ASSERT(NULL != session_);
  ASSERT(NULL != channel_);
  Session* session = session_;
  session_->SignalState.disconnect(this);
  session_ = NULL;
  if (channel_exists)
    channel_->SignalChannelClosed.disconnect(this);
  channel_ = NULL;
  delete this;
  return session;
}

void TunnelSession::OnSessionState(Session* session, Session::State state) {
  LOG(LS_INFO) << "TunnelSession::OnSessionState("
               << talk_base::nonnull(
                    talk_base::FindLabel(state, SESSION_STATES), "Unknown")
               << ")";
  ASSERT(session == session_);

  switch (state) {
  case Session::STATE_RECEIVEDINITIATE:
    OnInitiate();
    break;
  case Session::STATE_SENTACCEPT:
  case Session::STATE_RECEIVEDACCEPT:
    OnAccept();
    break;
  case Session::STATE_SENTTERMINATE:
  case Session::STATE_RECEIVEDTERMINATE:
    OnTerminate();
    break;
  case Session::STATE_DEINIT:
    // ReleaseSession should have been called before this.
    ASSERT(false);
    break;
  }
}

void TunnelSession::OnInitiate() {
  const TunnelSessionDescription* in_desc =
    static_cast<const TunnelSessionDescription*>(
      session_->remote_description());

  ASSERT(client_ != NULL);
  ASSERT(session_ != NULL);
  client_->SignalIncomingTunnel(client_,
                                buzz::Jid(session_->remote_name()),
                                in_desc->description,
                                session_);
}

void TunnelSession::OnAccept() {
  ASSERT(channel_ != NULL);
  VERIFY(channel_->Connect("tcp"));
}

void TunnelSession::OnTerminate() {
}

void TunnelSession::OnChannelClosed(PseudoTcpChannel* channel) {
  ASSERT(channel_ == channel);
  ASSERT(session_ != NULL);
  session_->Terminate();
}

///////////////////////////////////////////////////////////////////////////////

} // namespace cricket
