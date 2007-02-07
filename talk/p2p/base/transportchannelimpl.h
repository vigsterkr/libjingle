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

#ifndef _CRICKET_P2P_BASE_TRANSPORTCHANNELIMPL_H_
#define _CRICKET_P2P_BASE_TRANSPORTCHANNELIMPL_H_

#include <string>
#include "talk/p2p/base/transportchannel.h"

namespace buzz { class XmlElement; }

namespace cricket {

class Transport;

// Base class for real implementations of TransportChannel.  This includes some
// methods called only by Transport, which do not need to be exposed to the
// client.
class TransportChannelImpl: public TransportChannel {
 public:
  TransportChannelImpl(const std::string& name, const std::string& session_type)
    : TransportChannel(name, session_type) {}

  // Returns the transport that created this channel.
  virtual Transport* GetTransport() = 0;

  // Begins the process of attempting to make a connection to the other client.
  virtual void Connect() = 0;

  // Resets this channel back to the initial state (i.e., not connecting).
  virtual void Reset() = 0;

  // Allows an individual channel to request signaling and be notified when it
  // is ready.  This is useful if the individual named channels have need to
  // send their own transport-info stanzas.
  sigslot::signal0<> SignalRequestSignaling;
  virtual void OnSignalingReady() = 0;

  // Handles sending and receiving of stanzas related to this particular
  // channel.  Any channel may send whatever messages it wants.  The Transport
  // receives all incoming messages and may forward them to the relevant
  // channel.  The transport will delete signaled messages.
  //
  // Note: Since these messages are delivered asynchronously to the channel,
  // they cannot return an error if the message is invalid.  It is assumed that
  // the Transport will have checked validity before forwarding.
  virtual void OnChannelMessage(const buzz::XmlElement* msg) = 0;
  sigslot::signal2<TransportChannelImpl*,
                   buzz::XmlElement*> SignalChannelMessage;

 private:
  DISALLOW_EVIL_CONSTRUCTORS(TransportChannelImpl);
};

}  // namespace cricket

#endif  // _CRICKET_P2P_BASE_TRANSPORTCHANNELIMPL_H_
