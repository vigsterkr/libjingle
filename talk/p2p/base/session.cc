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

#include "talk/p2p/base/session.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/helpers.h"
#include "talk/xmpp/constants.h"
#include "talk/xmpp/jid.h"
#include "talk/p2p/base/sessionclient.h"
#include "talk/p2p/base/transport.h"
#include "talk/p2p/base/transportchannelproxy.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/p2p/base/constants.h"

namespace {

const uint32 MSG_TIMEOUT = 1;
const uint32 MSG_ERROR = 2;
const uint32 MSG_STATE = 3;

// This will be initialized at run time to hold the list of default transports.
std::string* gDefaultTransports = NULL;
size_t gNumDefaultTransports = 0;

}  // namespace

namespace cricket {

Session::Session(SessionManager *session_manager, const std::string& name,
                 const SessionID& id, const std::string& session_type,
                 SessionClient* client) {
  ASSERT(session_manager->signaling_thread()->IsCurrent());
  ASSERT(client != NULL);
  session_manager_ = session_manager;
  name_ = name;
  id_ = id;
  session_type_ = session_type;
  client_ = client;
  error_ = ERROR_NONE;
  state_ = STATE_INIT;
  initiator_ = false;
  description_ = NULL;
  remote_description_ = NULL;
  transport_ = NULL;
  compatibility_mode_ = false;
}

Session::~Session() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  ASSERT(state_ != STATE_DEINIT);
  state_ = STATE_DEINIT;
  SignalState(this, state_);

  delete description_;
  delete remote_description_;

  for (ChannelMap::iterator iter = channels_.begin();
       iter != channels_.end();
       ++iter) {
    iter->second->SignalDestroyed(iter->second);
    delete iter->second;
  }

  for (TransportList::iterator iter = potential_transports_.begin();
       iter != potential_transports_.end();
       ++iter) {
    delete *iter;
  }

  delete transport_;
}

bool Session::Initiate(const std::string &to,
                       std::vector<buzz::XmlElement*>* extra_xml,
                       const SessionDescription *description) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Only from STATE_INIT
  if (state_ != STATE_INIT)
    return false;

  // Setup for signaling.
  remote_name_ = to;
  initiator_ = true;
  description_ = description;

  // Make sure we have transports to negotiate.
  CreateTransports();

  // Send the initiate message, including the application and transport offers.
  XmlElements elems;
  elems.push_back(client_->TranslateSessionDescription(description));
  for (TransportList::iterator iter = potential_transports_.begin();
       iter != potential_transports_.end();
       ++iter) {
    buzz::XmlElement* elem = (*iter)->CreateTransportOffer();
    elems.push_back(elem);
  }

  if (extra_xml != NULL) {       
    std::vector<buzz::XmlElement*>::iterator iter = extra_xml->begin();
    for (std::vector<buzz::XmlElement*>::iterator iter = extra_xml->begin();
        iter != extra_xml->end();
        ++iter) {
      elems.push_back(new buzz::XmlElement(**iter));
    }
  }
      
  SendSessionMessage("initiate", elems);

  SetState(Session::STATE_SENTINITIATE);

  // We speculatively start attempting connection of the P2P transports.
  ConnectDefaultTransportChannels(true);
  return true;
}

void Session::ConnectDefaultTransportChannels(bool create) {
  Transport* transport = GetTransport(kNsP2pTransport);
  if (transport) {
    for (ChannelMap::iterator iter = channels_.begin();
         iter != channels_.end();
         ++iter) {
      ASSERT(create != transport->HasChannel(iter->first));
      if (create) {
        transport->CreateChannel(iter->first, session_type());
      }
    }
    transport->ConnectChannels();
  }
}

void Session::CreateDefaultTransportChannel(const std::string& name) {
  // This method is only relevant when we have created the default transport
  // but not received a transport-accept.
  ASSERT(transport_ == NULL);
  ASSERT(state_ == STATE_SENTINITIATE);

  Transport* p2p_transport = GetTransport(kNsP2pTransport);
  if (p2p_transport) {
    ASSERT(!p2p_transport->HasChannel(name));
    p2p_transport->CreateChannel(name, session_type());
  }
}

bool Session::Accept(const SessionDescription *description) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Only if just received initiate
  if (state_ != STATE_RECEIVEDINITIATE)
    return false;

  // Setup for signaling.
  initiator_ = false;
  description_ = description;

  // If we haven't selected a transport, wait for ChooseTransport to complete
  if (transport_ == NULL)
    return true;

  // Send the accept message.
  XmlElements elems;
  elems.push_back(client_->TranslateSessionDescription(description_));
  SendSessionMessage("accept", elems);
  SetState(Session::STATE_SENTACCEPT);

  return true;
}

bool Session::Reject() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Reject is sent in response to an initiate or modify, to reject the
  // request
  if (state_ != STATE_RECEIVEDINITIATE && state_ != STATE_RECEIVEDMODIFY)
    return false;

  // Setup for signaling.
  initiator_ = false;

  // Send the reject message.
  SendSessionMessage("reject", XmlElements());
  SetState(STATE_SENTREJECT);

  return true;
}

bool Session::Redirect(const std::string & target) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Redirect is sent in response to an initiate or modify, to redirect the
  // request
  if (state_ != STATE_RECEIVEDINITIATE)
    return false;

  // Setup for signaling.
  initiator_ = false;

  // Send a redirect message to the given target.  We include an element that
  // names the redirector (us), which may be useful to the other side.

  buzz::XmlElement* target_elem = new buzz::XmlElement(QN_REDIRECT_TARGET);
  target_elem->AddAttr(buzz::QN_NAME, target);

  buzz::XmlElement* cookie = new buzz::XmlElement(QN_REDIRECT_COOKIE);
  buzz::XmlElement* regarding = new buzz::XmlElement(QN_REDIRECT_REGARDING);
  regarding->AddAttr(buzz::QN_NAME, name_);
  cookie->AddElement(regarding);

  XmlElements elems;
  elems.push_back(target_elem);
  elems.push_back(cookie);
  SendSessionMessage("redirect", elems);

  // A redirect puts us in the same state as reject.  It just sends a different
  // kind of reject message, if you like.
  SetState(STATE_SENTREDIRECT);

  return true;
}

bool Session::Terminate() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Either side can terminate, at any time.
  switch (state_) {
    case STATE_SENTTERMINATE:
    case STATE_RECEIVEDTERMINATE:
      return false;

    case STATE_SENTREDIRECT:
      // We must not send terminate if we redirect.
      break;

    case STATE_SENTREJECT:
    case STATE_RECEIVEDREJECT:
      // We don't need to send terminate if we sent or received a reject...
      // it's implicit.
      break;

    default:
      SendSessionMessage("terminate", XmlElements());
      break;
  }

  SetState(STATE_SENTTERMINATE);
  return true;
}

void Session::SendInfoMessage(const XmlElements& elems) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SendSessionMessage("info", elems);
}

void Session::SetPotentialTransports(const std::string names[], size_t length) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  for (size_t i = 0; i < length; ++i) {
    Transport* transport = NULL;
    if (names[i] == kNsP2pTransport) {
      transport = new P2PTransport(session_manager_);
    } else {
      ASSERT(false);
    }

    if (transport) {
      ASSERT(transport->name() == names[i]);
      potential_transports_.push_back(transport);
      transport->SignalConnecting.connect(
          this, &Session::OnTransportConnecting);
      transport->SignalWritableState.connect(
          this, &Session::OnTransportWritable);
      transport->SignalRequestSignaling.connect(
          this, &Session::OnTransportRequestSignaling);
      transport->SignalTransportMessage.connect(
          this, &Session::OnTransportSendMessage);
      transport->SignalTransportError.connect(
          this, &Session::OnTransportSendError);
      transport->SignalChannelGone.connect(
          this, &Session::OnTransportChannelGone);
    }
  }
}

Transport* Session::GetTransport(const std::string& name) {
  if (transport_ != NULL) {
    if (name == transport_->name())
      return transport_;
  } else {
    for (TransportList::iterator iter = potential_transports_.begin();
        iter != potential_transports_.end();
        ++iter) {
      if (name == (*iter)->name())
        return *iter;
    }
  }
  return NULL;
}

TransportChannel* Session::CreateChannel(const std::string& name) {
  //ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(channels_.find(name) == channels_.end());
  TransportChannelProxy* channel = new TransportChannelProxy(name, session_type_);
  channels_[name] = channel;
  if (transport_) {
    ASSERT(!transport_->HasChannel(name));
    channel->SetImplementation(transport_->CreateChannel(name, session_type_));
  } else if (state_ == STATE_SENTINITIATE) {
    // In this case, we have already speculatively created the default
    // transport.  We should create this channel as well so that it may begin
    // early connection.
    CreateDefaultTransportChannel(name);
  }
  return channel;
}

TransportChannel* Session::GetChannel(const std::string& name) {
  ChannelMap::iterator iter = channels_.find(name);
  return (iter != channels_.end()) ? iter->second : NULL;
}

void Session::DestroyChannel(TransportChannel* channel) {
  ChannelMap::iterator iter = channels_.find(channel->name());
  ASSERT(iter != channels_.end());
  ASSERT(channel == iter->second);
  channels_.erase(iter);
  channel->SignalDestroyed(channel);
  delete channel;
}

TransportChannelImpl* Session::GetImplementation(TransportChannel* channel) {
  ChannelMap::iterator iter = channels_.find(channel->name());
  return (iter != channels_.end()) ? iter->second->impl() : NULL;
}

void Session::CreateTransports() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT((state_ == STATE_INIT)
      || (state_ == STATE_RECEIVEDINITIATE));
  if (potential_transports_.empty()) {
    if (gDefaultTransports == NULL) {
      gNumDefaultTransports = 1;
      gDefaultTransports = new std::string[1];
      gDefaultTransports[0] = kNsP2pTransport;
    }
    SetPotentialTransports(gDefaultTransports, gNumDefaultTransports);
  }
}

bool Session::ChooseTransport(const buzz::XmlElement* stanza) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(state_ == STATE_RECEIVEDINITIATE);
  ASSERT(transport_ == NULL);

  // Make sure we have decided on our own transports.
  CreateTransports();

  // Retrieve the session message.
  const buzz::XmlElement* session = stanza->FirstNamed(QN_SESSION);
  ASSERT(session != NULL);

  // Try the offered transports until we find one that we support.
  bool found_offer = false;
  const buzz::XmlElement* elem = session->FirstElement();
  while (elem) {
    if (elem->Name().LocalPart() == "transport") {
      found_offer = true;
      Transport* transport = GetTransport(elem->Name().Namespace());
      if (transport && transport->OnTransportOffer(elem)) {
        SetTransport(transport);
        break;
      }
    }
    elem = elem->NextElement();
  }

  // If the offer did not include any transports, then we are talking to an
  // old client.  In that case, we turn on compatibility mode, and we assume
  // an offer containing just P2P, which is all that old clients support.
  if (!found_offer) {
    compatibility_mode_ = true;

    Transport* transport = GetTransport(kNsP2pTransport);
    ASSERT(transport != NULL);

    scoped_ptr<buzz::XmlElement> transport_offer(
      new buzz::XmlElement(kQnP2pTransport, true));
    bool valid = transport->OnTransportOffer(transport_offer.get());
    ASSERT(valid);
    if (valid)
      SetTransport(transport);
  }

  if (!transport_) {
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_NOT_ACCEPTABLE, "modify",
                       "no supported transport in offer", NULL);
    return false;
  }

  // Get the description of the transport we picked.
  buzz::XmlElement* answer = transport_->CreateTransportAnswer();
  ASSERT(answer->Name() == buzz::QName(transport_->name(), "transport"));

  // Send a transport-accept message telling the other side our decision,
  // unless this is an old client that is not expecting one.
  if (!compatibility_mode_) {
    XmlElements elems;
    elems.push_back(answer);
    SendSessionMessage("transport-accept", elems);
  }

  // If the user wants to accept, allow that now
  if (description_) {
    Accept(description_);
  }

  return true;
}

void Session::SetTransport(Transport* transport) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(transport_ == NULL);
  transport_ = transport;

  // Drop the transports that were not selected.
  bool found = false;
  for (TransportList::iterator iter = potential_transports_.begin();
       iter != potential_transports_.end();
       ++iter) {
    if (*iter == transport_) {
      found = true;
    } else {
      delete *iter;
    }
  }
  potential_transports_.clear();

  // We require the selected transport to be one of the potential transports
  ASSERT(found);

  // Create implementations for all of the channels if they don't exist.
  for (ChannelMap::iterator iter = channels_.begin();
       iter != channels_.end();
       ++iter) {
    TransportChannelProxy* channel = iter->second;
    TransportChannelImpl* impl = transport_->GetChannel(channel->name());
    if (impl == NULL)
      impl = transport_->CreateChannel(channel->name(), session_type());
    ASSERT(impl != NULL);
    channel->SetImplementation(impl);
  }

  // Have this transport start connecting if it is not already.
  // (We speculatively connect the most common transport right away.)
  transport_->ConnectChannels();
}

void Session::SetState(State state) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  if (state != state_) {
    state_ = state;
    SignalState(this, state_);
    session_manager_->signaling_thread()->Post(this, MSG_STATE);
  }
}

void Session::SetError(Error error) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  if (error != error_) {
    error_ = error;
    SignalError(this, error);
    if (error_ != ERROR_NONE)
      session_manager_->signaling_thread()->Post(this, MSG_ERROR);
  }
}

void Session::OnSignalingReady() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  // Forward this to every transport.  Those that did not request it should
  // ignore this call.
  if (transport_ != NULL) {
    transport_->OnSignalingReady();
  } else {
    for (TransportList::iterator iter = potential_transports_.begin();
        iter != potential_transports_.end();
        ++iter) {
      (*iter)->OnSignalingReady();
    }
  }
}

void Session::OnTransportConnecting(Transport* transport) {
  // This is an indication that we should begin watching the writability
  // state of the transport.
  OnTransportWritable(transport);
}

void Session::OnTransportWritable(Transport* transport) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT((NULL == transport_) || (transport == transport_));

  // If the transport is not writable, start a timer to make sure that it
  // becomes writable within a reasonable amount of time.  If it does not, we
  // terminate since we can't actually send data.  If the transport is writable,
  // cancel the timer.  Note that writability transitions may occur repeatedly
  // during the lifetime of the session.

  session_manager_->signaling_thread()->Clear(this, MSG_TIMEOUT);
  if (transport->HasChannels() && !transport->writable()) {
    session_manager_->signaling_thread()->PostDelayed(
        session_manager_->session_timeout() * 1000, this, MSG_TIMEOUT);
  }
}

void Session::OnTransportRequestSignaling(Transport* transport) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SignalRequestSignaling(this);
}

void Session::OnTransportSendMessage(Transport* transport,
                                     const XmlElements& elems) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  for (size_t i = 0; i < elems.size(); ++i)
    ASSERT(elems[i]->Name() == buzz::QName(transport->name(), "transport"));

  if (compatibility_mode_) {
    // In backward compatibility mode, we send a candidates message.
    XmlElements candidates;
    for (size_t i = 0; i < elems.size(); ++i) {
      for (const buzz::XmlElement* elem = elems[i]->FirstElement();
           elem != NULL;
           elem = elem->NextElement()) {
        ASSERT(elem->Name() == kQnP2pCandidate);

        // Convert this candidate to an old style candidate (namespace change)
        buzz::XmlElement* legacy_candidate = new buzz::XmlElement(*elem);        
        legacy_candidate->SetName(kQnLegacyCandidate);
        candidates.push_back(legacy_candidate);
      }
      delete elems[i];
    }

    SendSessionMessage("candidates", candidates);
  } else {
    // If we haven't finished negotiation, then we may later discover that we
    // need compatibility mode, in which case, we will need to re-send these.
    if ((transport_ == NULL) && (transport->name() == kNsP2pTransport)) {
      for (size_t i = 0; i < elems.size(); ++i)
        candidates_.push_back(new buzz::XmlElement(*elems[i]));
    }

    SendSessionMessage("transport-info", elems);
  }
}

void Session::OnTransportSendError(Transport* transport,
                                   const buzz::XmlElement* stanza,
                                   const buzz::QName& name,
                                   const std::string& type,
                                   const std::string& text,
                                   const buzz::XmlElement* extra_info) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SignalErrorMessage(this, stanza, name, type, text, extra_info);
}

void Session::OnTransportChannelGone(Transport* transport,
                                     const std::string& name) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SignalChannelGone(this, name);
}

void Session::SendSessionMessage(
    const std::string& type, const std::vector<buzz::XmlElement*>& elems) {
  scoped_ptr<buzz::XmlElement> iq(new buzz::XmlElement(buzz::QN_IQ));
  iq->SetAttr(buzz::QN_TO, remote_name_);
  iq->SetAttr(buzz::QN_TYPE, buzz::STR_SET);

  buzz::XmlElement *session = new buzz::XmlElement(QN_SESSION, true);
  session->AddAttr(buzz::QN_TYPE, type);
  session->AddAttr(buzz::QN_ID, id_.id_str());
  session->AddAttr(QN_INITIATOR, id_.initiator());

  for (size_t i = 0; i < elems.size(); ++i)
    session->AddElement(elems[i]);

  iq->AddElement(session);
  SignalOutgoingMessage(this, iq.get());
}

void Session::SendAcknowledgementMessage(const buzz::XmlElement* stanza) {
  scoped_ptr<buzz::XmlElement> ack(new buzz::XmlElement(buzz::QN_IQ));
  ack->SetAttr(buzz::QN_TO, remote_name_);
  ack->SetAttr(buzz::QN_ID, stanza->Attr(buzz::QN_ID));
  ack->SetAttr(buzz::QN_TYPE, "result");

  SignalOutgoingMessage(this, ack.get());
}

void Session::OnIncomingMessage(const buzz::XmlElement* stanza) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(stanza->Name() == buzz::QN_IQ);
  buzz::Jid remote(remote_name_);
  buzz::Jid from(stanza->Attr(buzz::QN_FROM));
  ASSERT(state_ == STATE_INIT || from == remote);

  const buzz::XmlElement* session = stanza->FirstNamed(QN_SESSION);
  ASSERT(session != NULL);

  if (stanza->Attr(buzz::QN_TYPE) != buzz::STR_SET) {
    ASSERT(false);
    return;
  }

  ASSERT(session->HasAttr(buzz::QN_TYPE));
  std::string type = session->Attr(buzz::QN_TYPE);

  bool valid = false;

  if (type == "initiate") {
    valid = OnInitiateMessage(stanza, session);
  } else if (type == "accept") {
    valid = OnAcceptMessage(stanza, session);
  } else if (type == "reject") {
    valid = OnRejectMessage(stanza, session);
  } else if (type == "redirect") {
    valid = OnRedirectMessage(stanza, session);
  } else if (type == "info") {
    valid = OnInfoMessage(stanza, session);
  } else if (type == "transport-accept") {
    valid = OnTransportAcceptMessage(stanza, session);
  } else if (type == "transport-info") {
    valid = OnTransportInfoMessage(stanza, session);
  } else if (type == "terminate") {
    valid = OnTerminateMessage(stanza, session);
  } else if (type == "candidates") {
    // This is provided for backward compatibility.
    // TODO: Remove this once old candidates are gone.
    valid = OnCandidatesMessage(stanza, session);
  } else {
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                       "unknown session message type", NULL);
  }

  // If the message was not valid, we should have sent back an error above.
  // If it was valid, then we send an acknowledgement here.
  if (valid)
    SendAcknowledgementMessage(stanza);
}

void Session::OnFailedSend(const buzz::XmlElement* orig_stanza,
                           const buzz::XmlElement* error_stanza) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());

  const buzz::XmlElement* orig_session = orig_stanza->FirstNamed(QN_SESSION);
  ASSERT(orig_session != NULL);

  std::string error_type = "cancel";

  const buzz::XmlElement* error = error_stanza->FirstNamed(buzz::QN_ERROR);
  ASSERT(error != NULL);
  if (error) {
    ASSERT(error->HasAttr(buzz::QN_TYPE));
    error_type = error->Attr(buzz::QN_TYPE);

    LOG(LERROR) << "Session error:\n" << error->Str() << "\n"
                << "in response to:\n" << orig_session->Str();
  }

  bool fatal_error = false;

  ASSERT(orig_session->HasAttr(buzz::QN_TYPE));
  if ((orig_session->Attr(buzz::QN_TYPE) == "transport-info")
      || (orig_session->Attr(buzz::QN_TYPE) == "candidates")) {
    // Transport messages frequently generate errors because they are sent right
    // when we detect a network failure.  For that reason, we ignore such
    // errors, because if we do not establish writability again, we will
    // terminate anyway.  The exceptions are transport-specific error tags,
    // which we pass on to the respective transport.
    for (const buzz::XmlElement* elem = error->FirstElement();
         NULL != elem; elem = elem->NextElement()) {
      if (Transport* transport = GetTransport(elem->Name().Namespace())) {
        if (!transport->OnTransportError(orig_session, elem)) {
          fatal_error = true;
          break;
        }
      }
    }
  } else if ((error_type != "continue") && (error_type != "wait")) {
    // We do not set an error if the other side said it is okay to continue
    // (possibly after waiting).  These errors can be ignored.
    fatal_error = true;
  }

  if (fatal_error) {
    SetError(ERROR_RESPONSE);
  }
}

bool Session::OnInitiateMessage(const buzz::XmlElement* stanza,
                                const buzz::XmlElement* session) {
  if (!CheckState(stanza, STATE_INIT))
    return false;
  if (!FindRemoteSessionDescription(stanza, session))
    return false;

  initiator_ = false;
  remote_name_ = stanza->Attr(buzz::QN_FROM);
  SetState(STATE_RECEIVEDINITIATE);
  return true;
}

bool Session::OnAcceptMessage(const buzz::XmlElement* stanza,
                              const buzz::XmlElement* session) {
  if (!CheckState(stanza, STATE_SENTINITIATE))
    return false;
  if (!FindRemoteSessionDescription(stanza, session))
    return false;

  SetState(STATE_RECEIVEDACCEPT);
  return true;
}

bool Session::OnRejectMessage(const buzz::XmlElement* stanza,
                              const buzz::XmlElement* session) {
  if (!CheckState(stanza, STATE_SENTINITIATE))
    return false;

  SetState(STATE_RECEIVEDREJECT);
  return true;
}

bool Session::OnRedirectMessage(const buzz::XmlElement* stanza,
                                const buzz::XmlElement* session) {
  if (!CheckState(stanza, STATE_SENTINITIATE))
    return false;

  const buzz::XmlElement *redirect_target;
  if (!FindRequiredElement(stanza, session, QN_REDIRECT_TARGET,
                           &redirect_target))
    return false;

  if (!FindRequiredAttribute(stanza, redirect_target, buzz::QN_NAME,
                             &remote_name_))
    return false;

  const buzz::XmlElement* redirect_cookie =
      session->FirstNamed(QN_REDIRECT_COOKIE);

  XmlElements elems;
  elems.push_back(client_->TranslateSessionDescription(description_));
  if (redirect_cookie)
    elems.push_back(new buzz::XmlElement(*redirect_cookie)); 
  SendSessionMessage("initiate", elems);

  // Clear the connection timeout (if any).  We will start the connection
  // timer from scratch when SignalConnecting fires.
  session_manager_->signaling_thread()->Clear(this, MSG_TIMEOUT);

  // Reset all of the sockets back into the initial state.
  for (TransportList::iterator iter = potential_transports_.begin();
       iter != potential_transports_.end();
       ++iter) {
    (*iter)->ResetChannels();
  }

  ConnectDefaultTransportChannels(false);
  return true;
}

bool Session::OnInfoMessage(const buzz::XmlElement* stanza,
                            const buzz::XmlElement* session) {
  XmlElements elems;
  for (const buzz::XmlElement* elem = session->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    elems.push_back(new buzz::XmlElement(*elem));
  }

  SignalInfoMessage(this, elems);
  return true;
}

bool Session::OnTransportAcceptMessage(const buzz::XmlElement* stanza,
                                       const buzz::XmlElement* session) {
  if (!CheckState(stanza, STATE_SENTINITIATE))
    return false;

  Transport* transport = NULL;
  const buzz::XmlElement* transport_elem = NULL;

  for(const buzz::XmlElement* elem = session->FirstElement();
      elem != NULL;
      elem = elem->NextElement()) {
    if (elem->Name().LocalPart() == "transport") {
      Transport* transport = GetTransport(elem->Name().Namespace());
      if (transport) {
        if (transport_elem) {  // trying to accept two transport?
          SignalErrorMessage(this, stanza, buzz::QN_STANZA_BAD_REQUEST, 
                             "modify", "transport-accept has two answers",
                             NULL);
          return false;
        }

        transport_elem = elem;
        if (!transport->OnTransportAnswer(transport_elem)) {
          SignalErrorMessage(this, stanza, buzz::QN_STANZA_BAD_REQUEST,
                             "modify", "transport-accept is not acceptable",
                             NULL);
          return false;
        }
        SetTransport(transport);
      }
    }
  }

  if (!transport_elem) {
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_NOT_ALLOWED, "modify",
                       "no supported transport in answer", NULL);
    return false;
  }

  // If we discovered that we need compatibility mode and we have sent some
  // candidates already (using transport-info), then we need to re-send them
  // using the candidates message.
  if (compatibility_mode_ && (candidates_.size() > 0)) {
    ASSERT(transport_ != NULL);
    ASSERT(transport_->name() == kNsP2pTransport);
    OnTransportSendMessage(transport_, candidates_);
  } else {
    for (size_t i = 0; i < candidates_.size(); ++i)
      delete candidates_[i];
  }
  candidates_.clear();

  return true;
}

bool Session::OnTransportInfoMessage(const buzz::XmlElement* stanza,
                                     const buzz::XmlElement* session) {
  for(const buzz::XmlElement* elem = session->FirstElement();
      elem != NULL;
      elem = elem->NextElement()) {
    if (elem->Name().LocalPart() == "transport") {
      Transport* transport = GetTransport(elem->Name().Namespace());
      if (transport) {
        if (!transport->OnTransportMessage(elem, stanza))
          return false;
      }
    }
  }
  return true;
}

bool Session::OnTerminateMessage(const buzz::XmlElement* stanza,
                                 const buzz::XmlElement* session) {
  for (const buzz::XmlElement *elem = session->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    // elem->Name().LocalPart() is the reason for termination
    SignalReceivedTerminateReason(this, elem->Name().LocalPart());
    // elem->FirstElement() might contain a debug string for termination
    const buzz::XmlElement *debugElement = elem->FirstElement();
    if (debugElement != NULL) {
      LOG(LS_VERBOSE) << "Received error on call: "
        << debugElement->Name().LocalPart();
    }
  }
  SetState(STATE_RECEIVEDTERMINATE);
  return true;
}

bool Session::OnCandidatesMessage(const buzz::XmlElement* stanza,
                                  const buzz::XmlElement* session) {
  // If we don't have a transport, then this is the first candidates message.
  // We first create a fake transport-accept message in order to finish the
  // negotiation and create a transport.
  if (!transport_) {
    compatibility_mode_ = true;

    scoped_ptr<buzz::XmlElement> transport_accept(
        new buzz::XmlElement(QN_SESSION));
    transport_accept->SetAttr(buzz::QN_TYPE, "transport-accept");

    buzz::XmlElement* transport_offer =
        new buzz::XmlElement(kQnP2pTransport, true);
    transport_accept->AddElement(transport_offer);

    // It is okay to pass the original stanza here.  That is only used if we
    // send an error message.  Normal processing looks only at transport_accept.
    bool valid = OnTransportAcceptMessage(stanza, transport_accept.get());
    ASSERT(valid);
  }

  ASSERT(transport_ != NULL);
  ASSERT(transport_->name() == kNsP2pTransport);

  // Wrap the candidates in a transport element as they would appear in a
  // transport-info message and send this to the transport.
  scoped_ptr<buzz::XmlElement> transport_info(
      new buzz::XmlElement(kQnP2pTransport, true));
  for (const buzz::XmlElement* elem = session->FirstNamed(kQnLegacyCandidate);
       elem != NULL;
       elem = elem->NextNamed(kQnLegacyCandidate)) {
    buzz::XmlElement* new_candidate = new buzz::XmlElement(*elem);
    new_candidate->SetName(kQnP2pCandidate);
    transport_info->AddElement(new_candidate);
  }
  return transport_->OnTransportMessage(transport_info.get(), stanza);
}

bool Session::CheckState(const buzz::XmlElement* stanza, State state) {
  ASSERT(state_ == state);
  if (state_ != state) {
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_NOT_ALLOWED, "modify",
                       "message not allowed in current state", NULL);
    return false;
  }
  return true;
}

bool Session::FindRequiredElement(const buzz::XmlElement* stanza,
                                  const buzz::XmlElement* parent,
                                  const buzz::QName& name,
                                  const buzz::XmlElement** elem) {
  *elem = parent->FirstNamed(name);
  if (*elem == NULL) {
    std::string text;
    text += "element '" + parent->Name().Merged() +
            "' missing required child '" + name.Merged() + "'";
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                       text, NULL);
    return false;
  }
  return true;
}

bool Session::FindRemoteSessionDescription(const buzz::XmlElement* stanza,
                                           const buzz::XmlElement* session) {
  buzz::QName qn_session(session_type_, "description");
  const buzz::XmlElement* desc;
  if (!FindRequiredElement(stanza, session, qn_session, &desc))
    return false;
  remote_description_ = client_->CreateSessionDescription(desc);
  return true;
}

bool Session::FindRequiredAttribute(const buzz::XmlElement* stanza,
                                    const buzz::XmlElement* elem,
                                    const buzz::QName& name,
                                    std::string* value) {
  if (!elem->HasAttr(name)) {
    std::string text;
    text += "element '" + elem->Name().Merged() +
            "' missing required attribute '" + name.Merged() + "'";
    SignalErrorMessage(this, stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                       text, NULL);
    return false;
  } else {
    *value = elem->Attr(name);
    return true;
  }
}

void Session::OnMessage(talk_base::Message *pmsg) {
  switch(pmsg->message_id) {
  case MSG_TIMEOUT:
    // Session timeout has occured.
    SetError(ERROR_TIME);
    break;

  case MSG_ERROR:
    // Any of the defined errors is most likely fatal.
    Terminate();
    break;

  case MSG_STATE:
    switch (state_) {
    case STATE_SENTACCEPT:
    case STATE_RECEIVEDACCEPT:
      SetState(STATE_INPROGRESS);
      ASSERT(transport_ != NULL);
      break;

    case STATE_SENTREJECT:
    case STATE_SENTREDIRECT:
    case STATE_RECEIVEDREJECT:
      Terminate();
      break;

    case STATE_SENTTERMINATE:
    case STATE_RECEIVEDTERMINATE:
      session_manager_->DestroySession(this);
      break;

    default:
      // Explicitly ignoring some states here.
      break;
    }
    break;
  }
}

} // namespace cricket
