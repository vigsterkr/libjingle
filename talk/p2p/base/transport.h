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

// A Transport manages a set of named channels of the same type.
//
// Subclasses choose the appropriate class to instantiate for each channel;
// however, this base class keeps track of the channels by name, watches their
// state changes (in order to update the manager's state), and forwards
// requests to begin connecting or to reset to each of the channels.
//
// On Threading:  Transport performs work on both the signaling and worker
// threads.  For subclasses, the rule is that all signaling related calls will
// be made on the signaling thread and all channel related calls (including
// signaling for a channel) will be made on the worker thread.  When
// information needs to be sent between the two threads, this class should do
// the work (e.g., ForwardChannelMessage).
//
// Note: Subclasses must call DestroyChannels() in their own constructors.
// It is not possible to do so here because the subclass constructor will
// already have run.

#ifndef _CRICKET_P2P_BASE_TRANSPORT_H_
#define _CRICKET_P2P_BASE_TRANSPORT_H_

#include <string>
#include <map>
#include <vector>
#include "talk/base/criticalsection.h"
#include "talk/base/messagequeue.h"
#include "talk/base/sigslot.h"

namespace buzz {
class QName;
class XmlElement;
}

namespace cricket {

class SessionManager;
class Session;
class TransportChannel;
class TransportChannelImpl;

class Transport : public talk_base::MessageHandler, public sigslot::has_slots<> {
 public:
  Transport(SessionManager* session_manager, const std::string& name);
  virtual ~Transport();

  // Returns a pointer to the singleton session manager.
  SessionManager* session_manager() const { return session_manager_; }

  // Returns the name of this transport.
  const std::string& name() const { return name_; }

  // Returns the readable and states of this manager.  These bits are the ORs
  // of the corresponding bits on the managed channels.  Each time one of these
  // states changes, a signal is raised.
  bool readable() const { return readable_; }
  bool writable() const { return writable_; }
  sigslot::signal1<Transport*> SignalReadableState;
  sigslot::signal1<Transport*> SignalWritableState;

  // Returns whether the client has requested the channels to connect.
  bool connect_requested() const { return connect_requested_; }

  // Create, destroy, and lookup the channels of this type by their names.
  TransportChannelImpl* CreateChannel(const std::string& name, const std::string &session_type);
  // Note: GetChannel may lead to race conditions, since the mutex is not held
  // after the pointer is returned.
  TransportChannelImpl* GetChannel(const std::string& name);
  // Note: HasChannel does not lead to race conditions, unlike GetChannel.
  bool HasChannel(const std::string& name) { return (NULL != GetChannel(name)); }
  bool HasChannels();
  void DestroyChannel(const std::string& name);

  // Tells all current and future channels to start connecting.  When the first
  // channel begins connecting, the following signal is raised.
  void ConnectChannels();
  sigslot::signal1<Transport*> SignalConnecting;

  // Resets all of the channels back to their initial state.  They are no
  // longer connecting.
  void ResetChannels();

  // Destroys every channel created so far.
  void DestroyAllChannels();

  // The session handshake includes negotiation of both the application and the
  // transport.  The initiating transport creates an "offer" describing what
  // options it supports and the responding transport creates an "answer"
  // describing which options it has accepted.  If OnTransport* returns false,
  // that indicates that no acceptable options given and this transport cannot
  // be negotiated.
  //
  // The transport negotiation operates as follows.  When the initiating client
  // creates the session, but before they send the initiate, we create the
  // supported transports.  The client may override these, but otherwise they
  // get a default set.  When the initiate is sent, we ask each transport to
  // produce an offer.  When the receiving client gets the initiate, they will
  // iterate through the transport offers in order of their own preference.
  // For each one, they create the transport (if they know what it is) and
  // call OnTransportOffer.  If this returns true, then we're good; otherwise,
  // we continue iterating.  If no transport works, then we reject the session.
  // Otherwise, we have a single transport, and we send back a transport-accept
  // message that contains the answer.  When this arrives at the initiating
  // client, we destroy all transports but the one in the answer and then pass
  // the answer to it.  If this transport cannot be found or it cannot accept
  // the answer, then we reject the session.  Otherwise, we're in good shape.
  virtual buzz::XmlElement* CreateTransportOffer() = 0;
  virtual buzz::XmlElement* CreateTransportAnswer() = 0;
  virtual bool OnTransportOffer(const buzz::XmlElement* elem) = 0;
  virtual bool OnTransportAnswer(const buzz::XmlElement* elem) = 0;

  // Before any stanza is sent, the manager will request signaling.  Once
  // signaling is available, the client should call OnSignalingReady.  Once
  // this occurs, the transport (or its channels) can send any waiting stanzas.
  // OnSignalingReady invokes OnTransportSignalingReady and then forwards this
  // signal to each channel.
  sigslot::signal1<Transport*> SignalRequestSignaling;
  void OnSignalingReady();

  // Handles sending and receiving of stanzas related to negotiating the
  // connections of the channels.  Different transports may have very different
  // signaling, so the XML is handled by the subclass.  The msg variable holds
  // the element whose name matches this transport, while stanza holds the
  // entire stanza.  The latter is needed when sending an error response.
  // SignalTransportMessage is given the elements that will become the children
  // of the transport-info message.  Each element must have a name that matches
  // the transport's name.
  virtual bool OnTransportMessage(const buzz::XmlElement* msg,
                                  const buzz::XmlElement* stanza) = 0;
  sigslot::signal2<Transport*, const std::vector<buzz::XmlElement*>&>
      SignalTransportMessage;

  // A transport message has generated an transport-specific error.  The
  // stanza that caused the error is available in session_msg.  If false is
  // returned, the error is considered unrecoverable, and the session is
  // terminated.
  virtual bool OnTransportError(const buzz::XmlElement* session_msg,
                                const buzz::XmlElement* error) = 0;
  sigslot::signal6<Transport*, const buzz::XmlElement*, const buzz::QName&,
                   const std::string&, const std::string&,
                   const buzz::XmlElement*>
      SignalTransportError;

  sigslot::signal2<Transport*, const std::string&> SignalChannelGone;

  // (For testing purposes only.)  This indicates whether we will allow local
  // IPs (e.g. 127.*) to be used as addresses for P2P.
  bool allow_local_ips() const { return allow_local_ips_; }
  void set_allow_local_ips(bool value) { allow_local_ips_ = value; }

 protected:
  // Helper function to bad-request error for a stanza passed to
  // OnTransportMessage.  Returns false.
  bool BadRequest(const buzz::XmlElement* stanza, const std::string& text,
                  const buzz::XmlElement* extra_info);

  // Helper function to parse an element describing an address.  This retrieves
  // the IP and port from the given element (using QN_ADDRESS and QN_PORT) and
  // verifies that they look like plausible values.
  bool ParseAddress(const buzz::XmlElement* stanza,
                    const buzz::XmlElement* elem,
                    talk_base::SocketAddress* address);

  // These are called by Create/DestroyChannel above in order to create or
  // destroy the appropriate type of channel.
  virtual TransportChannelImpl* CreateTransportChannel(
		  const std::string& name, const std::string &session_type) = 0;
  virtual void DestroyTransportChannel(TransportChannelImpl* channel) = 0;

  // Informs the subclass that we received the signaling ready message.
  virtual void OnTransportSignalingReady() {}

  // Forwards the given XML element to the channel on the worker thread.  This
  // occurs asynchronously, so we take ownership of the element.  Furthermore,
  // the channel will not be able to return an error if the XML is invalid, so
  // the transport should have checked its validity already.
  void ForwardChannelMessage(const std::string& name,
                             buzz::XmlElement* elem);

  // Handles a set of messages sent by the channels.  The default
  // implementation simply forwards each as its own transport message by
  // wrapping it in an element identifying this transport and then invoking
  // SignalTransportMessage.  Smarter transports may be able to place multiple
  // channel messages within one transport message.
  //
  // Note: The implementor of this method is responsible for deleting the XML
  // elements passed in, unless they are sent to SignalTransportMessage, where
  // the receiver will delete them.
  virtual void OnTransportChannelMessages(
      const std::vector<buzz::XmlElement*>& msgs);

 private:
  typedef std::map<std::string, TransportChannelImpl*> ChannelMap;
  typedef std::vector<buzz::XmlElement*> XmlElementList;

  SessionManager* session_manager_;
  std::string name_;
  bool destroyed_;
  bool readable_;
  bool writable_;
  bool connect_requested_;
  ChannelMap channels_;
  XmlElementList messages_;
  talk_base::CriticalSection crit_; // Protects changes to channels and messages
  bool allow_local_ips_;

  // Called when the state of a channel changes.
  void OnChannelReadableState(TransportChannel* channel);
  void OnChannelWritableState(TransportChannel* channel);

  // Called when a channel requests signaling.
  void OnChannelRequestSignaling();

  // Called when a channel wishes to send a transport message.
  void OnChannelMessage(TransportChannelImpl* impl, buzz::XmlElement* elem);

  // Dispatches messages to the appropriate handler (below).
  void OnMessage(talk_base::Message* msg);

  // These are versions of the above methods that are called only on a
  // particular thread (s = signaling, w = worker).  The above methods post or
  // send a message to invoke this version.
  TransportChannelImpl* CreateChannel_w(const std::string& name, const std::string& session_type);
  void DestroyChannel_w(const std::string& name);
  void ConnectChannels_w();
  void ResetChannels_w();
  void DestroyAllChannels_w();
  void ForwardChannelMessage_w(const std::string& name,
                               buzz::XmlElement* elem);
  void OnChannelReadableState_s();
  void OnChannelWritableState_s();
  void OnChannelRequestSignaling_s();
  void OnConnecting_s();

  // Helper function that invokes the given function on every channel.
  typedef void (TransportChannelImpl::* TransportChannelFunc)();
  void CallChannels_w(TransportChannelFunc func);

  // Computes the OR of the channel's read or write state (argument picks).
  bool GetTransportState_s(bool read);

  // Invoked when there are messages waiting to send in the messages_ list.
  // We wait to send any messages until the client asks us to connect.
  void OnChannelMessage_s();

  DISALLOW_EVIL_CONSTRUCTORS(Transport);
};

}  // namespace cricket

#endif  // _CRICKET_P2P_BASE_TRANSPORT_H_
