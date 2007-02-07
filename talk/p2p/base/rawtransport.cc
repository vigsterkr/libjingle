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

#include "talk/p2p/base/rawtransport.h"
#include "talk/base/common.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/base/rawtransportchannel.h"
#include "talk/xmllite/qname.h"
#include "talk/xmllite/xmlelement.h"
#include "talk/xmpp/constants.h"

namespace cricket {

const std::string kNsRawTransport("http://www.google.com/transport/raw-udp");
const buzz::QName kQnRawTransport(true, kNsRawTransport, "transport");
const buzz::QName kQnRawChannel(true, kNsRawTransport, "channel");
const buzz::QName kQnRawBehindSymmetricNat(true, buzz::STR_EMPTY, 
  "behind-symmetric-nat");
const buzz::QName kQnRawCanReceiveFromSymmetricNat(true, buzz::STR_EMPTY,
  "can-receive-from-symmetric-nat");

RawTransport::RawTransport(SessionManager* session_manager)
  : Transport(session_manager, kNsRawTransport) {
}

RawTransport::~RawTransport() {
  DestroyAllChannels();
}

buzz::XmlElement* RawTransport::CreateTransportOffer() {
  buzz::XmlElement* xml = new buzz::XmlElement(kQnRawTransport, true);

  // Assume that we are behind a symmetric NAT.  Also note that we can't 
  // handle the adjustment necessary to talk to someone else who is behind
  // a symmetric NAT.
  xml->AddAttr(kQnRawBehindSymmetricNat, "true");
  xml->AddAttr(kQnRawCanReceiveFromSymmetricNat, "false");

  return xml;
}

buzz::XmlElement* RawTransport::CreateTransportAnswer() {
  return new buzz::XmlElement(kQnRawTransport, true);
}

bool RawTransport::OnTransportOffer(const buzz::XmlElement* elem) {
  ASSERT(elem->Name() == kQnRawTransport);

  // If the other side is behind a symmetric NAT then we can't talk to him.
  // We also bail if this attribute isn't specified.
  if (!elem->HasAttr(kQnRawBehindSymmetricNat) 
      || elem->Attr(kQnRawBehindSymmetricNat) != "false") {
    return false;
  }

  // If the other side doesn't explicitly state that he can receive from 
  // someone behind a symmetric NAT, we bail.
  if (!elem->HasAttr(kQnRawCanReceiveFromSymmetricNat)
      || elem->Attr(kQnRawCanReceiveFromSymmetricNat) != "true") {
    return false;
  }

  // We don't support any options, so we ignore them.
  return true;
}

bool RawTransport::OnTransportAnswer(const buzz::XmlElement* elem) {
  ASSERT(elem->Name() == kQnRawTransport);
  // We don't support any options.  We fail if any are given.  The other side
  // should know from our request that we expected an empty response.
  return elem->FirstChild() == NULL;
}

bool RawTransport::OnTransportMessage(const buzz::XmlElement* msg,
                                      const buzz::XmlElement* stanza) {
  ASSERT(msg->Name() == kQnRawTransport);
  for (const buzz::XmlElement* elem = msg->FirstElement();
       elem != NULL;
       elem = elem->NextElement()) {
    if (elem->Name() == kQnRawChannel) {
      talk_base::SocketAddress addr;
      if (!ParseAddress(stanza, elem, &addr))
        return false;

      ForwardChannelMessage(elem->Attr(buzz::QN_NAME),
                            new buzz::XmlElement(*elem));
    }
  }
  return true;
}

bool RawTransport::OnTransportError(const buzz::XmlElement* session_msg,
                                    const buzz::XmlElement* error) {
  return true;
}

bool RawTransport::ParseAddress(const buzz::XmlElement* stanza,
                                const buzz::XmlElement* elem,
                                talk_base::SocketAddress* addr) {
  // Make sure the required attributes exist
  if (!elem->HasAttr(buzz::QN_NAME) ||
      !elem->HasAttr(QN_ADDRESS) ||
      !elem->HasAttr(QN_PORT)) {
    return BadRequest(stanza, "channel missing required attribute", NULL);
  }

  // Make sure the channel named actually exists.
  if (!HasChannel(elem->Attr(buzz::QN_NAME)))
    return BadRequest(stanza, "channel named does not exist", NULL);

  // Parse the address.
  return Transport::ParseAddress(stanza, elem, addr);
}

TransportChannelImpl* RawTransport::CreateTransportChannel(
    const std::string& name, const std::string &session_type) {
  return new RawTransportChannel(
     name, session_type, this, session_manager()->port_allocator());
}

void RawTransport::DestroyTransportChannel(TransportChannelImpl* channel) {
  delete channel;
}

}  // namespace cricket
