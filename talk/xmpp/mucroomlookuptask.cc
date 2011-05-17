/*
 * libjingle
 * Copyright 2011, Google Inc.
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

#include "talk/xmpp/mucroomlookuptask.h"

#include "talk/base/logging.h"
#include "talk/base/scoped_ptr.h"
#include "talk/xmpp/constants.h"


namespace buzz {

static const int kLookupTimeout = 15;

MucRoomLookupTask::MucRoomLookupTask(Task* parent,
                                     const std::string& room_name,
                                     const std::string& organizer_domain)
    : XmppTask(parent, XmppEngine::HL_SINGLE),
      room_name_(room_name),
      organizer_domain_(organizer_domain) {
  set_timeout_seconds(kLookupTimeout);
}

MucRoomLookupTask::MucRoomLookupTask(Task* parent,
                                     const Jid& room_jid)
    : XmppTask(parent, XmppEngine::HL_SINGLE), room_jid_(room_jid) {
  set_timeout_seconds(kLookupTimeout);
}

int MucRoomLookupTask::ProcessStart() {
  talk_base::scoped_ptr<XmlElement> lookup(MakeIq(STR_SET,
      Jid(STR_MUC_LOOKUP_DOMAIN), task_id()));
  if (room_jid_ != JID_EMPTY) {
    // TODO: need to test the jid query calling code below.
    XmlElement* query_elem = MakeJidQuery(room_jid_.Str());
    lookup->AddElement(query_elem);
  } else {
    // We do room query if room jid is unknown.
    XmlElement* query_elem = MakeRoomQuery(room_name_, organizer_domain_);
    lookup->AddElement(query_elem);
  }

  if (SendStanza(lookup.get()) != XMPP_RETURN_OK) {
    SignalRoomLookupError(NULL);
    return STATE_ERROR;
  }
  return STATE_RESPONSE;
}

int MucRoomLookupTask::ProcessResponse() {
  const XmlElement* stanza = NextStanza();

  if (stanza == NULL)
    return STATE_BLOCKED;

  if (stanza->Attr(QN_TYPE) == STR_ERROR) {
    SignalRoomLookupError(stanza->FirstNamed(QN_ERROR));
    return STATE_DONE;
  }

  const XmlElement* query_elem = stanza->FirstNamed(QN_SEARCH_QUERY);
  if (query_elem != NULL) {
    const XmlElement* item_elem =
        query_elem->FirstNamed(QN_SEARCH_ITEM);
    if (item_elem != NULL && item_elem->HasAttr(QN_JID)) {
      MucRoomInfo room_info;
      if (GetRoomInfoFromResponse(item_elem, &room_info)) {
        SignalRoomLookupResponse(room_info);
        return STATE_DONE;
      }
    }
  }

  SignalRoomLookupError(NULL);
  return STATE_DONE;
}

int MucRoomLookupTask::OnTimeout() {
  SignalRoomLookupError(NULL);
  return XmppTask::OnTimeout();
}

bool MucRoomLookupTask::HandleStanza(const XmlElement* stanza) {
  if (MatchResponseIq(stanza, Jid(STR_MUC_LOOKUP_DOMAIN), task_id())) {
    QueueStanza(stanza);
    return true;
  }
  return false;
}

XmlElement* MucRoomLookupTask::MakeRoomQuery(const std::string& room_name,
    const std::string& org_domain) {
  XmlElement* room_elem = new XmlElement(QN_SEARCH_ROOM_NAME, false);
  room_elem->SetBodyText(room_name);

  XmlElement* domain_elem = new XmlElement(QN_SEARCH_ORGANIZERS_DOMAIN, false);
  domain_elem->SetBodyText(org_domain);

  XmlElement* query = new XmlElement(QN_SEARCH_QUERY, true);
  query->AddElement(room_elem);
  query->AddElement(domain_elem);

  return query;
}

XmlElement* MucRoomLookupTask::MakeJidQuery(const std::string& room_jid) {
  XmlElement* jid_elem = new XmlElement(QN_SEARCH_ROOM_JID);
  jid_elem->SetBodyText(room_jid);

  XmlElement* query = new XmlElement(QN_SEARCH_QUERY);
  query->AddElement(jid_elem);

  return query;
}

bool MucRoomLookupTask::GetRoomInfoFromResponse(
    const XmlElement* stanza, MucRoomInfo* info) {

  info->room_jid = Jid(stanza->Attr(buzz::QN_JID));
  if (!info->room_jid.IsValid()) return false;

  const XmlElement* room_name_elem = stanza->FirstNamed(QN_SEARCH_ROOM_NAME);
  const XmlElement* org_domain_elem =
      stanza->FirstNamed(QN_SEARCH_ORGANIZERS_DOMAIN);

  if (room_name_elem != NULL)
    info->room_name = room_name_elem->BodyText();
  if (org_domain_elem != NULL)
    info->organizer_domain = org_domain_elem->BodyText();

  return true;
}
}  // namespace buzz
