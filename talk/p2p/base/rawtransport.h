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

#ifndef _CRICKET_P2P_BASE_RAWTRANSPORT_H_
#define _CRICKET_P2P_BASE_RAWTRANSPORT_H_

#include "talk/p2p/base/transport.h"

namespace cricket {

// Xml names used to name this transport and create our elements
extern const std::string kNsRawTransport;
extern const buzz::QName kQnRawTransport;
extern const buzz::QName kQnRawChannel;
extern const buzz::QName kQnRawNatType;
extern const buzz::QName kQnRawNatTypeAllowed;

// Implements a transport that only sends raw packets, no STUN.  As a result,
// it cannot do pings to determine connectivity, so it only uses a single port
// that it thinks will work.
class RawTransport: public Transport {
 public:
  RawTransport(SessionManager* session_manager);
  virtual ~RawTransport();

  // Handles the raw transport protocol descriptions, which are trivial.
  virtual buzz::XmlElement* CreateTransportOffer();
  virtual buzz::XmlElement* CreateTransportAnswer();
  virtual bool OnTransportOffer(const buzz::XmlElement* elem);
  virtual bool OnTransportAnswer(const buzz::XmlElement* elem);

  // Forwards messages containing channel addresses to the appropriate channel.
  virtual bool OnTransportMessage(const buzz::XmlElement* msg,
                                  const buzz::XmlElement* stanza);
  virtual bool OnTransportError(const buzz::XmlElement* session_msg,
                                const buzz::XmlElement* error);

 protected:
  // Creates and destroys raw channels.
  virtual TransportChannelImpl* CreateTransportChannel(
     const std::string& name, const std::string &session_type);
  virtual void DestroyTransportChannel(TransportChannelImpl* channel);

 private:
  // Parses the given element, which should describe the address to use for a
  // given channel.  This will return false and signal an error if the address
  // or channel name is bad.
  bool ParseAddress(const buzz::XmlElement* stanza,
                    const buzz::XmlElement* elem,
                    talk_base::SocketAddress* addr);

  friend class RawTransportChannel;  // For ParseAddress.

  DISALLOW_EVIL_CONSTRUCTORS(RawTransport);
};

}  // namespace cricket


#endif  // _CRICKET_P2P_BASE_RAWTRANSPORT_H_
