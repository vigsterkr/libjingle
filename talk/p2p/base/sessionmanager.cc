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

#include "talk/p2p/base/sessionmanager.h"
#include "talk/base/common.h"
#include "talk/base/helpers.h"
#include "talk/p2p/base/constants.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/jid.h"

namespace cricket {

SessionManager::SessionManager(PortAllocator *allocator, 
                               talk_base::Thread *worker) {
  allocator_ = allocator;
  signaling_thread_ = talk_base::Thread::Current();
  if (worker == NULL) {
    worker_thread_ = talk_base::Thread::Current();
  } else {
    worker_thread_ = worker;
  }
  timeout_ = 50;
}

SessionManager::~SessionManager() {
  // Note: Session::Terminate occurs asynchronously, so it's too late to
  // delete them now.  They better be all gone.
  ASSERT(session_map_.empty());
  //TerminateAll();
}

void SessionManager::AddClient(const std::string& session_type,
                               SessionClient* client) {
  ASSERT(client_map_.find(session_type) == client_map_.end());
  client_map_[session_type] = client;
}

void SessionManager::RemoveClient(const std::string& session_type) {
  ClientMap::iterator iter = client_map_.find(session_type);
  ASSERT(iter != client_map_.end());
  client_map_.erase(iter);
}

SessionClient* SessionManager::GetClient(const std::string& session_type) {
  ClientMap::iterator iter = client_map_.find(session_type);
  return (iter != client_map_.end()) ? iter->second : NULL;
}

Session *SessionManager::CreateSession(const std::string& name,
                                       const std::string& session_type) {
  return CreateSession(name, SessionID(name, CreateRandomId()), session_type,
                       false);
}

Session *SessionManager::CreateSession(
    const std::string &name, const SessionID& id,
    const std::string& session_type, bool received_initiate) {
  SessionClient* client = GetClient(session_type);
  ASSERT(client != NULL);

  Session *session = new Session(this, name, id, session_type, client);
  session_map_[session->id()] = session;
  session->SignalRequestSignaling.connect(
      this, &SessionManager::OnRequestSignaling);
  session->SignalOutgoingMessage.connect(
      this, &SessionManager::OnOutgoingMessage);
  session->SignalErrorMessage.connect(this, &SessionManager::OnErrorMessage);
  SignalSessionCreate(session, received_initiate);
  session->client()->OnSessionCreate(session, received_initiate);
  return session;
}

void SessionManager::DestroySession(Session *session) {
  if (session != NULL) {
    SessionMap::iterator it = session_map_.find(session->id());
    if (it != session_map_.end()) {
      SignalSessionDestroy(session);
      session->client()->OnSessionDestroy(session);
      session_map_.erase(it);
      delete session;
    }
  }
}

Session *SessionManager::GetSession(const SessionID& id) {
  SessionMap::iterator it = session_map_.find(id);
  if (it != session_map_.end())
    return it->second;
  return NULL;
}

void SessionManager::TerminateAll() {
  while (session_map_.begin() != session_map_.end()) {
    Session *session = session_map_.begin()->second;
    session->Terminate();
  }
}

bool SessionManager::IsSessionMessage(const buzz::XmlElement* stanza) {
  if (stanza->Name() != buzz::QN_IQ)
    return false;
  if (!stanza->HasAttr(buzz::QN_TYPE))
    return false;
  if (stanza->Attr(buzz::QN_TYPE) != buzz::STR_SET)
    return false;

  const buzz::XmlElement* session = stanza->FirstNamed(QN_SESSION);
  if (!session)
    return false;
  if (!session->HasAttr(buzz::QN_TYPE))
    return false;
  if (!session->HasAttr(buzz::QN_ID) || !session->HasAttr(QN_INITIATOR))
    return false;

  return true;
}

Session* SessionManager::FindSessionForStanza(const buzz::XmlElement* stanza, 
                                              bool incoming) {
  const buzz::XmlElement* session_xml = stanza->FirstNamed(QN_SESSION);
  ASSERT(session_xml != NULL);

  SessionID id;
  id.set_id_str(session_xml->Attr(buzz::QN_ID));
  id.set_initiator(session_xml->Attr(QN_INITIATOR));

  // Pass this message to the session in question.
  SessionMap::iterator iter = session_map_.find(id);
  if (iter == session_map_.end())
    return NULL;

  Session* session = iter->second;

  // match on "from"? or "to"?
  buzz::QName attr = buzz::QN_TO;
  if (incoming) {
    attr = buzz::QN_FROM;
  }
  buzz::Jid remote(session->remote_name());
  buzz::Jid match(stanza->Attr(attr));
  if (remote == match) {
    return session;
  }
  return NULL;
}

void SessionManager::OnIncomingMessage(const buzz::XmlElement* stanza) {
  ASSERT(stanza->Attr(buzz::QN_TYPE) == buzz::STR_SET);

  Session* session = FindSessionForStanza(stanza, true);
  if (session) {
    session->OnIncomingMessage(stanza);
    return;
  }

  const buzz::XmlElement* session_xml = stanza->FirstNamed(QN_SESSION);
  ASSERT(session_xml != NULL);
  if (session_xml->Attr(buzz::QN_TYPE) == "initiate") {
    std::string session_type = FindClient(session_xml);
    if (session_type.size() == 0) {
      SendErrorMessage(stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                       "unknown session description type", NULL);
    } else {
      SessionID id;
      id.set_id_str(session_xml->Attr(buzz::QN_ID));
      id.set_initiator(session_xml->Attr(QN_INITIATOR));

      session = CreateSession(stanza->Attr(buzz::QN_TO), 
                              id,
                              session_type,  true);
      session->OnIncomingMessage(stanza);

      // If we haven't rejected, and we haven't selected a transport yet,
      // let's do it now.
      if ((session->state() != Session::STATE_SENTREJECT) &&
          (session->transport() == NULL)) {
        session->ChooseTransport(stanza);
      }
    }
    return;
  }

  SendErrorMessage(stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                  "unknown session", NULL);
}

void SessionManager::OnIncomingResponse(const buzz::XmlElement* orig_stanza,
    const buzz::XmlElement* response_stanza) {
  // We don't do anything with the response now.  If we need to we can forward
  // it to the session.
  return;
}

void SessionManager::OnFailedSend(const buzz::XmlElement* orig_stanza, 
                                  const buzz::XmlElement* error_stanza) {
  Session* session = FindSessionForStanza(orig_stanza, false);
  if (session) {
    scoped_ptr<buzz::XmlElement> synthetic_error;
    if (!error_stanza) {
      // A failed send is semantically equivalent to an error response, so we 
      // can just turn the former into the latter.
      synthetic_error.reset(
        CreateErrorMessage(orig_stanza, buzz::QN_STANZA_ITEM_NOT_FOUND, 
                           "cancel", "Recipient did not respond", NULL));
      error_stanza = synthetic_error.get();
    }

    session->OnFailedSend(orig_stanza, error_stanza);
  }
}

std::string SessionManager::FindClient(const buzz::XmlElement* session) {
  for (const buzz::XmlElement* elem = session->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    if (elem->Name().LocalPart() == "description") {
      ClientMap::iterator iter = client_map_.find(elem->Name().Namespace());
      if (iter != client_map_.end())
        return iter->first;
    }
  }
  return "";
}

void SessionManager::SendErrorMessage(const buzz::XmlElement* stanza,
                                      const buzz::QName& name,
                                      const std::string& type,
                                      const std::string& text,
                                      const buzz::XmlElement* extra_info) {
  scoped_ptr<buzz::XmlElement> msg(
      CreateErrorMessage(stanza, name, type, text, extra_info));
  SignalOutgoingMessage(msg.get());
}

buzz::XmlElement* SessionManager::CreateErrorMessage(
    const buzz::XmlElement* stanza,
    const buzz::QName& name,
    const std::string& type,
    const std::string& text,
    const buzz::XmlElement* extra_info) {
  buzz::XmlElement* iq = new buzz::XmlElement(buzz::QN_IQ);
  iq->SetAttr(buzz::QN_TO, stanza->Attr(buzz::QN_FROM));
  iq->SetAttr(buzz::QN_ID, stanza->Attr(buzz::QN_ID));
  iq->SetAttr(buzz::QN_TYPE, "error");

  for (const buzz::XmlElement* elem = stanza->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    iq->AddElement(new buzz::XmlElement(*elem));
  }

  buzz::XmlElement* error = new buzz::XmlElement(buzz::QN_ERROR);
  error->SetAttr(buzz::QN_TYPE, type);
  iq->AddElement(error);

  // If the error name is not in the standard namespace, we have to first add
  // some error from that namespace.
  if (name.Namespace() != buzz::NS_STANZA) {
     error->AddElement(
         new buzz::XmlElement(buzz::QN_STANZA_UNDEFINED_CONDITION));
  }
  error->AddElement(new buzz::XmlElement(name));

  if (extra_info)
    error->AddElement(new buzz::XmlElement(*extra_info));

  if (text.size() > 0) {
    // It's okay to always use English here.  This text is for debugging
    // purposes only.
    buzz::XmlElement* text_elem = new buzz::XmlElement(buzz::QN_STANZA_TEXT);
    text_elem->SetAttr(buzz::QN_XML_LANG, "en");
    text_elem->SetBodyText(text);
    error->AddElement(text_elem);
  }

  // TODO: Should we include error codes as well for SIP compatibility?

  return iq;
}

void SessionManager::OnOutgoingMessage(Session* session,
                                       const buzz::XmlElement* stanza) {
  SignalOutgoingMessage(stanza);
}

void SessionManager::OnErrorMessage(Session* session,
                                    const buzz::XmlElement* stanza,
                                    const buzz::QName& name,
                                    const std::string& type,
                                    const std::string& text,
                                    const buzz::XmlElement* extra_info) {
  SendErrorMessage(stanza, name, type, text, extra_info);
}

void SessionManager::OnSignalingReady() {
  for (SessionMap::iterator it = session_map_.begin();
      it != session_map_.end();
      ++it) {
    it->second->OnSignalingReady();
  }
}

void SessionManager::OnRequestSignaling(Session* session) {
  SignalRequestSignaling();
}

} // namespace cricket
