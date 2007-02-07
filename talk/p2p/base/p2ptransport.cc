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

#include "talk/p2p/base/p2ptransport.h"
#include "talk/base/common.h"
#include "talk/p2p/base/candidate.h"
#include "talk/p2p/base/constants.h"
#include "talk/base/helpers.h"
#include "talk/p2p/base/p2ptransportchannel.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/xmllite/qname.h"
#include "talk/xmllite/xmlelement.h"
#include "talk/xmpp/constants.h"

namespace {

// We only allow usernames to be this many characters or fewer.
const size_t kMaxUsernameSize = 16;

}  // namespace

namespace cricket {

const std::string kNsP2pTransport("http://www.google.com/transport/p2p");
const buzz::QName kQnP2pTransport(true, kNsP2pTransport, "transport");
const buzz::QName kQnP2pCandidate(true, kNsP2pTransport, "candidate");
const buzz::QName kQnP2pUnknownChannelName(true, kNsP2pTransport,
                                           "unknown-channel-name");

P2PTransport::P2PTransport(SessionManager* session_manager)
  : Transport(session_manager, kNsP2pTransport) {
}

P2PTransport::~P2PTransport() {
  DestroyAllChannels();
}

buzz::XmlElement* P2PTransport::CreateTransportOffer() {
  return new buzz::XmlElement(kQnP2pTransport, true);
}

buzz::XmlElement* P2PTransport::CreateTransportAnswer() {
  return new buzz::XmlElement(kQnP2pTransport, true);
}

bool P2PTransport::OnTransportOffer(const buzz::XmlElement* elem) {
  ASSERT(elem->Name() == kQnP2pTransport);
  // We don't support any options, so we ignore them.
  return true;
}

bool P2PTransport::OnTransportAnswer(const buzz::XmlElement* elem) {
  ASSERT(elem->Name() == kQnP2pTransport);
  // We don't support any options.  We fail if any are given.  The other side
  // should know from our request that we expected an empty response.
  return elem->FirstChild() == NULL;
}

bool P2PTransport::OnTransportMessage(const buzz::XmlElement* msg,
                                      const buzz::XmlElement* stanza) {
  ASSERT(msg->Name() == kQnP2pTransport);
  for (const buzz::XmlElement* elem = msg->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    if (elem->Name() == kQnP2pCandidate) {
      // Make sure this candidate is valid.
      Candidate candidate;
      if (!ParseCandidate(stanza, elem, &candidate))
        return false;

      ForwardChannelMessage(elem->Attr(buzz::QN_NAME),
                            new buzz::XmlElement(*elem));
    }
  }
  return true;
}

bool P2PTransport::OnTransportError(const buzz::XmlElement* session_msg,
                                    const buzz::XmlElement* error) {
  ASSERT(error->Name().Namespace() == kNsP2pTransport);
  if ((error->Name() == kQnP2pUnknownChannelName)
      && error->HasAttr(buzz::QN_NAME)) {
    std::string channel_name = error->Attr(buzz::QN_NAME);
    if (HasChannel(channel_name)) {
      SignalChannelGone(this, channel_name);
    }
  }
  return true;
}

void P2PTransport::OnTransportChannelMessages(
    const std::vector<buzz::XmlElement*>& candidates) {
  buzz::XmlElement* transport =
      new buzz::XmlElement(kQnP2pTransport, true);
  for (size_t i = 0; i < candidates.size(); ++i)
    transport->AddElement(candidates[i]);

  std::vector<buzz::XmlElement*> elems;
  elems.push_back(transport);
  SignalTransportMessage(this, elems);
}

bool P2PTransport::ParseCandidate(const buzz::XmlElement* stanza,
                                  const buzz::XmlElement* elem,
                                  Candidate* candidate) {
  // Check for all of the required attributes.
  if (!elem->HasAttr(buzz::QN_NAME) || 
      !elem->HasAttr(QN_ADDRESS) ||
      !elem->HasAttr(QN_PORT) ||
      !elem->HasAttr(QN_USERNAME) ||
      !elem->HasAttr(QN_PREFERENCE) ||
      !elem->HasAttr(QN_PROTOCOL) || 
      !elem->HasAttr(QN_GENERATION)) {
    return BadRequest(stanza, "candidate missing required attribute", NULL);
  }
  
  // Make sure the channel named actually exists.
  if (!HasChannel(elem->Attr(buzz::QN_NAME))) {
    scoped_ptr<buzz::XmlElement>
      extra_info(new buzz::XmlElement(kQnP2pUnknownChannelName));
    extra_info->AddAttr(buzz::QN_NAME, elem->Attr(buzz::QN_NAME));
    return BadRequest(stanza, "channel named in candidate does not exist",
                      extra_info.get());
  }

  // Parse the address given.
  talk_base::SocketAddress address;
  if (!ParseAddress(stanza, elem, &address))
    return false;

  candidate->set_name(elem->Attr(buzz::QN_NAME));
  candidate->set_address(address);
  candidate->set_username(elem->Attr(QN_USERNAME));
  candidate->set_preference_str(elem->Attr(QN_PREFERENCE));
  candidate->set_protocol(elem->Attr(QN_PROTOCOL));
  candidate->set_generation_str(elem->Attr(QN_GENERATION));

  // Check that the username is not too long and does not use any bad chars.
  if (candidate->username().size() > kMaxUsernameSize)
    return BadRequest(stanza, "candidate username is too long", NULL);
  if (!IsBase64Encoded(candidate->username()))
    return BadRequest(stanza,
                      "candidate username has non-base64 encoded characters",
                      NULL);

  // Look for the non-required attributes.
  if (elem->HasAttr(QN_PASSWORD))
    candidate->set_password(elem->Attr(QN_PASSWORD));
  if (elem->HasAttr(buzz::QN_TYPE))
    candidate->set_type(elem->Attr(buzz::QN_TYPE));
  if (elem->HasAttr(QN_NETWORK))
    candidate->set_network_name(elem->Attr(QN_NETWORK));

  return true;
}

buzz::XmlElement* P2PTransport::TranslateCandidate(const Candidate& c) {
  buzz::XmlElement* candidate = new buzz::XmlElement(kQnP2pCandidate);
  candidate->SetAttr(buzz::QN_NAME, c.name());
  candidate->SetAttr(QN_ADDRESS, c.address().IPAsString());
  candidate->SetAttr(QN_PORT, c.address().PortAsString());
  candidate->SetAttr(QN_PREFERENCE, c.preference_str());
  candidate->SetAttr(QN_USERNAME, c.username());
  candidate->SetAttr(QN_PROTOCOL, c.protocol());
  candidate->SetAttr(QN_GENERATION, c.generation_str());
  if (c.password().size() > 0)
    candidate->SetAttr(QN_PASSWORD, c.password());
  if (c.type().size() > 0)
    candidate->SetAttr(buzz::QN_TYPE, c.type());
  if (c.network_name().size() > 0)
    candidate->SetAttr(QN_NETWORK, c.network_name());
  return candidate;
}

TransportChannelImpl* P2PTransport::CreateTransportChannel(
  const std::string& name, const std::string &session_type) {
  return new P2PTransportChannel(
    name, session_type, this, session_manager()->port_allocator());
}

void P2PTransport::DestroyTransportChannel(TransportChannelImpl* channel) {
  delete channel;
}

}  // namespace cricket
