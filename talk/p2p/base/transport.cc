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

#include "talk/base/common.h"
#include "talk/p2p/base/transport.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/base/transportchannelimpl.h"
#include "talk/p2p/base/constants.h"
#include "talk/xmllite/xmlelement.h"
#include "talk/xmpp/constants.h"

namespace {

struct ChannelParams {
  std::string name;
  std::string session_type;
  cricket::TransportChannelImpl* channel;
  buzz::XmlElement* elem;

  ChannelParams() : channel(NULL), elem(NULL) {}
};
typedef talk_base::TypedMessageData<ChannelParams*> ChannelMessage;

const int MSG_CREATECHANNEL = 1;
const int MSG_DESTROYCHANNEL = 2;
const int MSG_DESTROYALLCHANNELS = 3;
const int MSG_CONNECTCHANNELS = 4;
const int MSG_RESETCHANNELS = 5;
const int MSG_ONSIGNALINGREADY = 6;
const int MSG_FORWARDCHANNELMESSAGE = 7;
const int MSG_READSTATE = 8;
const int MSG_WRITESTATE = 9;
const int MSG_REQUESTSIGNALING = 10;
const int MSG_ONCHANNELMESSAGE = 11;
const int MSG_CONNECTING = 12;

}  // namespace

namespace cricket {

Transport::Transport(SessionManager* session_manager, const std::string& name)
  : session_manager_(session_manager), name_(name), destroyed_(false),
    readable_(false), writable_(false), connect_requested_(false),
    allow_local_ips_(false) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
}

Transport::~Transport() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(destroyed_);
}

TransportChannelImpl* Transport::CreateChannel(const std::string& name, const std::string &session_type) {
  ChannelParams params;
  params.name = name;
  params.session_type = session_type;
  ChannelMessage msg(&params);
  session_manager_->worker_thread()->Send(this, MSG_CREATECHANNEL, &msg);
  return msg.data()->channel;
}

TransportChannelImpl* Transport::CreateChannel_w(const std::string& name, const std::string &session_type) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());

  TransportChannelImpl* impl = CreateTransportChannel(name, session_type);
  impl->SignalReadableState.connect(this, &Transport::OnChannelReadableState);
  impl->SignalWritableState.connect(this, &Transport::OnChannelWritableState);
  impl->SignalRequestSignaling.connect(
      this, &Transport::OnChannelRequestSignaling);
  impl->SignalChannelMessage.connect(this, &Transport::OnChannelMessage);

  talk_base::CritScope cs(&crit_);
  ASSERT(channels_.find(name) == channels_.end());
  channels_[name] = impl;
  destroyed_ = false;
  if (connect_requested_) {
    impl->Connect();
    if (channels_.size() == 1) {
      // If this is the first channel, then indicate that we have started
      // connecting.
      session_manager_->signaling_thread()->Post(this, MSG_CONNECTING, NULL);
    }
  }
  return impl;
}

TransportChannelImpl* Transport::GetChannel(const std::string& name) {
  talk_base::CritScope cs(&crit_);
  ChannelMap::iterator iter = channels_.find(name);
  return (iter != channels_.end()) ? iter->second : NULL;
}

bool Transport::HasChannels() {
  talk_base::CritScope cs(&crit_);
  return !channels_.empty();
}

void Transport::DestroyChannel(const std::string& name) {
  ChannelParams params;
  params.name = name;
  ChannelMessage msg(&params);
  session_manager_->worker_thread()->Send(this, MSG_DESTROYCHANNEL, &msg);
}

void Transport::DestroyChannel_w(const std::string& name) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  TransportChannelImpl* impl = NULL;
  {
    talk_base::CritScope cs(&crit_);
    ChannelMap::iterator iter = channels_.find(name);
    ASSERT(iter != channels_.end());
    impl = iter->second;
    channels_.erase(iter);
  }

  if (connect_requested_ && channels_.empty()) {
    // We're not longer attempting to connect.
    session_manager_->signaling_thread()->Post(this, MSG_CONNECTING, NULL);
  }

  if (impl)
    DestroyTransportChannel(impl);
}

void Transport::ConnectChannels() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  session_manager_->worker_thread()->Post(this, MSG_CONNECTCHANNELS, NULL);
}

void Transport::ConnectChannels_w() {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  if (connect_requested_)
    return;
  connect_requested_ = true;
  session_manager_->signaling_thread()->Post(this, MSG_ONCHANNELMESSAGE, NULL);
  CallChannels_w(&TransportChannelImpl::Connect);
  if (!channels_.empty()) {
    session_manager_->signaling_thread()->Post(this, MSG_CONNECTING, NULL);
  }
}

void Transport::OnConnecting_s() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SignalConnecting(this);
}
 
void Transport::DestroyAllChannels() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  session_manager_->worker_thread()->Send(this, MSG_DESTROYALLCHANNELS, NULL);
  destroyed_ = true;
}

void Transport::DestroyAllChannels_w() {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  std::vector<TransportChannelImpl*> impls;
  {
    talk_base::CritScope cs(&crit_);
    for (ChannelMap::iterator iter = channels_.begin();
         iter != channels_.end();
         ++iter) {
      impls.push_back(iter->second);
    }
    channels_.clear();
  }

  for (size_t i = 0; i < impls.size(); ++i)
    DestroyTransportChannel(impls[i]);
}

void Transport::ResetChannels() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  session_manager_->worker_thread()->Post(this, MSG_RESETCHANNELS, NULL);
}

void Transport::ResetChannels_w() {
  ASSERT(session_manager_->worker_thread()->IsCurrent());

  // We are no longer attempting to connect
  connect_requested_ = false;

  // Clear out the old messages, they aren't relevant
  talk_base::CritScope cs(&crit_);
  for (size_t i=0; i<messages_.size(); ++i) {
    delete messages_[i];
  }
  messages_.clear();

  // Reset all of the channels
  CallChannels_w(&TransportChannelImpl::Reset);
}

void Transport::OnSignalingReady() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  session_manager_->worker_thread()->Post(this, MSG_ONSIGNALINGREADY, NULL);

  // Notify the subclass.
  OnTransportSignalingReady();
}

void Transport::CallChannels_w(TransportChannelFunc func) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  talk_base::CritScope cs(&crit_);
  for (ChannelMap::iterator iter = channels_.begin();
       iter != channels_.end();
       ++iter) {
    ((iter->second)->*func)();
  }
}

void Transport::ForwardChannelMessage(const std::string& name,
                                      buzz::XmlElement* elem) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(HasChannel(name));
  ChannelParams* params = new ChannelParams();
  params->name = name;
  params->elem = elem;
  ChannelMessage* msg = new ChannelMessage(params);
  session_manager_->worker_thread()->Post(this, MSG_FORWARDCHANNELMESSAGE, msg);
}

void Transport::ForwardChannelMessage_w(const std::string& name,
                                        buzz::XmlElement* elem) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  ChannelMap::iterator iter = channels_.find(name);
  // It's ok for a channel to go away while this message is in transit.
  if (iter != channels_.end()) {
    iter->second->OnChannelMessage(elem);
  }
  delete elem;
}

void Transport::OnChannelReadableState(TransportChannel* channel) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  session_manager_->signaling_thread()->Post(this, MSG_READSTATE, NULL);
}

void Transport::OnChannelReadableState_s() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  bool readable = GetTransportState_s(true);
  if (readable_ != readable) {
    readable_ = readable;
    SignalReadableState(this);
  }
}

void Transport::OnChannelWritableState(TransportChannel* channel) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  session_manager_->signaling_thread()->Post(this, MSG_WRITESTATE, NULL);
}

void Transport::OnChannelWritableState_s() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  bool writable = GetTransportState_s(false);
  if (writable_ != writable) {
    writable_ = writable;
    SignalWritableState(this);
  }
}

bool Transport::GetTransportState_s(bool read) {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  bool result = false;
  talk_base::CritScope cs(&crit_);
  for (ChannelMap::iterator iter = channels_.begin();
       iter != channels_.end();
       ++iter) {
    bool b = (read ? iter->second->readable() : iter->second->writable());
    result = result || b;
  }
  return result;
}

void Transport::OnChannelRequestSignaling() {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  session_manager_->signaling_thread()->Post(this, MSG_REQUESTSIGNALING, NULL);
}

void Transport::OnChannelRequestSignaling_s() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  SignalRequestSignaling(this);
}

void Transport::OnChannelMessage(TransportChannelImpl* impl,
                                 buzz::XmlElement* elem) {
  ASSERT(session_manager_->worker_thread()->IsCurrent());
  talk_base::CritScope cs(&crit_);
  messages_.push_back(elem);

  // We hold any messages until the client lets us connect.
  if (connect_requested_) {
    session_manager_->signaling_thread()->Post(
        this, MSG_ONCHANNELMESSAGE, NULL);
  }
}

void Transport::OnChannelMessage_s() {
  ASSERT(session_manager_->signaling_thread()->IsCurrent());
  ASSERT(connect_requested_);

  std::vector<buzz::XmlElement*> msgs;
  {
    talk_base::CritScope cs(&crit_);
    msgs.swap(messages_);
  }

  if (!msgs.empty())
    OnTransportChannelMessages(msgs);
}

void Transport::OnTransportChannelMessages(
    const std::vector<buzz::XmlElement*>& msgs) {
  std::vector<buzz::XmlElement*> elems;
  for (size_t i = 0; i < msgs.size(); ++i) {
    buzz::XmlElement* elem =
        new buzz::XmlElement(buzz::QName(name(), "transport"));
    elem->AddElement(msgs[i]);
    elems.push_back(elem);
  }
  SignalTransportMessage(this, elems);
}

void Transport::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
  case MSG_CREATECHANNEL:
    {
      ChannelParams* params = static_cast<ChannelMessage*>(msg->pdata)->data();
      params->channel = CreateChannel_w(params->name, params->session_type);
    }
    break;
  case MSG_DESTROYCHANNEL:
    {
      ChannelParams* params = static_cast<ChannelMessage*>(msg->pdata)->data();
      DestroyChannel_w(params->name);
    }
    break;
  case MSG_CONNECTCHANNELS:
    ConnectChannels_w();
    break;
  case MSG_RESETCHANNELS:
    ResetChannels_w();
    break;
  case MSG_DESTROYALLCHANNELS:
    DestroyAllChannels_w();
    break;
  case MSG_ONSIGNALINGREADY:
    CallChannels_w(&TransportChannelImpl::OnSignalingReady);
    break;
  case MSG_FORWARDCHANNELMESSAGE:
    {
      ChannelParams* params = static_cast<ChannelMessage*>(msg->pdata)->data();
      ForwardChannelMessage_w(params->name, params->elem);
      delete params;
    }
    break;
  case MSG_CONNECTING:
    OnConnecting_s();
    break;
  case MSG_READSTATE:
    OnChannelReadableState_s();
    break;
  case MSG_WRITESTATE:
    OnChannelWritableState_s();
    break;
  case MSG_REQUESTSIGNALING:
    OnChannelRequestSignaling_s();
    break;
  case MSG_ONCHANNELMESSAGE:
    OnChannelMessage_s();
    break;
  }
}

bool Transport::BadRequest(const buzz::XmlElement* stanza,
                           const std::string& text,
                           const buzz::XmlElement* extra_info) {
  SignalTransportError(this, stanza, buzz::QN_STANZA_BAD_REQUEST, "modify",
                       text, extra_info);
  return false;
}

bool Transport::ParseAddress(const buzz::XmlElement* stanza,
                             const buzz::XmlElement* elem,
                             talk_base::SocketAddress* address) {
  ASSERT(elem->HasAttr(QN_ADDRESS));
  ASSERT(elem->HasAttr(QN_PORT));

  // Record the parts of the address.
  address->SetIP(elem->Attr(QN_ADDRESS));
  std::istringstream ist(elem->Attr(QN_PORT));
  int port;
  ist >> port;
  address->SetPort(port);
 
  // No address zero. 
  if (address->IsAny())
    return BadRequest(stanza, "candidate has address of zero", NULL);
  
  // Always disallow addresses that refer to the local host.
  if (address->IsLocalIP() && !allow_local_ips_)
    return BadRequest(stanza, "candidate has local IP address", NULL);
  
  // Disallow all ports below 1024, except for 80 and 443 on public addresses.
  if (port < 1024) { 
    if ((port != 80) && (port != 443))
      return BadRequest(stanza,
                        "candidate has port below 1024, but not 80 or 443",
                        NULL);
    if (address->IsPrivateIP()) {
      return BadRequest(stanza, "candidate has port of 80 or 443 with private "
                                "IP address", NULL);
    }
  }

  return true;
}

}  // namespace cricket
