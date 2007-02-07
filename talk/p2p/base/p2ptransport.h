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

#ifndef _CRICKET_P2P_BASE_P2PTRANSPORT_H_
#define _CRICKET_P2P_BASE_P2PTRANSPORT_H_

#include "talk/p2p/base/transport.h"

namespace cricket {

class Candidate;

// Xml names used to name this transport and create our elements
extern const std::string kNsP2pTransport;
extern const buzz::QName kQnP2pTransport;
extern const buzz::QName kQnP2pCandidate;

class P2PTransport: public Transport {
 public:
  P2PTransport(SessionManager* session_manager);
  virtual ~P2PTransport();

  // Implements negotiation of the P2P protocol.
  virtual buzz::XmlElement* CreateTransportOffer();
  virtual buzz::XmlElement* CreateTransportAnswer();
  virtual bool OnTransportOffer(const buzz::XmlElement* elem);
  virtual bool OnTransportAnswer(const buzz::XmlElement* elem);

  // Forwards each candidate message to the appropriate channel.
  virtual bool OnTransportMessage(const buzz::XmlElement* msg,
                                  const buzz::XmlElement* stanza);
  virtual bool OnTransportError(const buzz::XmlElement* session_msg,
                                const buzz::XmlElement* error);

 protected:
  // Creates and destroys P2PTransportChannel.
  virtual TransportChannelImpl* CreateTransportChannel(const std::string& name, const std::string &session_type);
  virtual void DestroyTransportChannel(TransportChannelImpl* channel);

  // Sends a given set of channel messages, which each describe a candidate,
  // to the other client as a single transport message.
  void OnTransportChannelMessages(
      const std::vector<buzz::XmlElement*>& candidates);

 private:
  // Attempts to parse the given XML into a candidate.  Returns true if the
  // XML is valid.  If not, we will signal an error.
  bool ParseCandidate(const buzz::XmlElement* stanza,
                      const buzz::XmlElement* elem,
                      Candidate* candidate);

  // Generates a XML element describing the given candidate.
  buzz::XmlElement* TranslateCandidate(const Candidate& c);

  friend class P2PTransportChannel;

  DISALLOW_EVIL_CONSTRUCTORS(P2PTransport);
};

}  // namespace cricket

#endif  // _CRICKET_P2P_BASE_P2PTRANSPORT_H_
