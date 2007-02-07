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

#ifndef _SESSION_H_
#define _SESSION_H_

#include "talk/base/socketaddress.h"
#include "talk/p2p/base/sessiondescription.h"
#include "talk/p2p/base/sessionmanager.h"
#include "talk/p2p/base/sessionclient.h"
#include "talk/p2p/base/sessionid.h"
#include "talk/p2p/base/port.h"
#include "talk/xmllite/xmlelement.h"
#include <string>

namespace cricket {

class SessionManager;
class Transport;
class TransportChannel;
class TransportChannelProxy;
class TransportChannelImpl;

// A specific Session created by the SessionManager.  A Session manages
// signaling for session setup and tear down.  This setup includes negotiation
// of both the application-level and network-level protocols:  the former
// defines what will be sent and the latter defines how it will be sent.  Each
// network-level protocol is represented by a Transport object.  Each Transport
// participates in the network-level negotiation.  The individual streams of
// packets are represented by TransportChannels.
class Session : public talk_base::MessageHandler, public sigslot::has_slots<> {
 public:
  enum State {
    STATE_INIT = 0,
    STATE_SENTINITIATE,      // sent initiate, waiting for Accept or Reject
    STATE_RECEIVEDINITIATE,  // received an initiate. Call Accept or Reject
    STATE_SENTACCEPT,        // sent accept. begin connecting transport
    STATE_RECEIVEDACCEPT,    // received accept. begin connecting transport
    STATE_SENTMODIFY,        // sent modify, waiting for Accept or Reject
    STATE_RECEIVEDMODIFY,    // received modify, call Accept or Reject
    STATE_SENTREJECT,        // sent reject after receiving initiate
    STATE_RECEIVEDREJECT,    // received reject after sending initiate
    STATE_SENTREDIRECT,      // sent direct after receiving initiate
    STATE_SENTTERMINATE,     // sent terminate (any time / either side)
    STATE_RECEIVEDTERMINATE, // received terminate (any time / either side)
    STATE_INPROGRESS,        // session accepted and in progress
    STATE_DEINIT,            // session is being destroyed
  };

  enum Error {
    ERROR_NONE = 0, // no error
    ERROR_TIME,     // no response to signaling
    ERROR_RESPONSE, // error during signaling
    ERROR_NETWORK,  // network error, could not allocate network resources
  };

  // Returns the manager that created and owns this session.
  SessionManager* session_manager() const { return session_manager_; }

  // Returns the XML namespace identifying the type of this session.
  const std::string& session_type() const { return session_type_; }

  // Returns the client that is handling the application data of this session.
  SessionClient* client() const { return client_; }

  // Returns the JID this client.
  const std::string &name() const { return name_; }

  // Returns the JID of the other peer in this session.
  const std::string &remote_name() const { return remote_name_; }

  // Indicates whether we initiated this session.
  bool initiator() const { return initiator_; }

  // Holds the ID of this session, which should be unique across the world.
  const SessionID& id() const { return id_; }

  // Returns the applicication-level description given by our client.  This
  // will be null until Initiate or Accept.
  const SessionDescription *description() const { return description_; }

  // Returns the applicication-level description given by the other client.
  // If we are the initiator, this will be null until we receive an accept.
  const SessionDescription *remote_description() const {
    return remote_description_;
  }

  // Returns the current state of the session.  See the enum above for details.
  // Each time the state changes, we will fire this signal.
  State state() const { return state_; }
  sigslot::signal2<Session *, State> SignalState;

  // Fired whenever we receive a terminate message along with a reason
  sigslot::signal2<Session *, const std::string &> SignalReceivedTerminateReason;

  // Returns the last error in the session.  See the enum above for details.
  // Each time the an error occurs, we will fire this signal.
  Error error() const { return error_; }
  sigslot::signal2<Session *, Error> SignalError;

  // Returns the transport that has been negotiated or NULL if negotiation is
  // still in progress.
  Transport* transport() const { return transport_; }

  // When a session was created by us, we are the initiator, and we send the
  // initiate message when this method is invoked.  The extra_xml parameter is
  // a list of elements that will get inserted inside <Session> ... </Session>
  bool Initiate(const std::string &to, std::vector<buzz::XmlElement*>* extra_xml, 
                const SessionDescription *description);

  // When we receive a session initiation from another client, we create a
  // session in the RECEIVEDINITIATE state.  We respond by accepting,
  // rejecting, or redirecting the session somewhere else.
  bool Accept(const SessionDescription *description);
  bool Reject();
  bool Redirect(const std::string& target);

  // At any time, we may terminate an outstanding session.
  bool Terminate();

  // The two clients in the session may also send one another arbitrary XML
  // messages, which are called "info" messages.  Both of these functions take
  // ownership of the XmlElements and delete them when done.
  typedef std::vector<buzz::XmlElement*> XmlElements;
  void SendInfoMessage(const XmlElements& elems);
  sigslot::signal2<Session*, const XmlElements&> SignalInfoMessage;

  // Controls the set of transports that will be allowed for this session.  If
  // we are initiating, then this list will be used to construct the transports
  // that we will offer to the other side.  In that case, the order of the
  // transport names indicates our preference (first has highest preference).
  // If we are receiving, then this list indicates the set of transports that
  // we will allow.  We will choose the first transport in the offered list
  // (1) whose name appears in the given list and (2) that can accept the offer
  // provided (which may include parameters particular to the transport).
  //
  // If this function is not called (or if it is called with a NULL array),
  // then we will use a default set of transports.
  void SetPotentialTransports(const std::string names[], size_t length);

  // Once transports have been created (by SetTransports), this function will
  // return the transport with the given name or NULL if none was created.
  // Once a particular transport has been chosen, only that transport will be
  // returned.
  Transport* GetTransport(const std::string& name);

  // Creates a new channel with the given name.  This method may be called
  // immediately after creating the session.  However, the actual
  // implementation may not be fixed until transport negotiation completes.
  TransportChannel* CreateChannel(const std::string& name);

  // Returns the channel with the given name.
  TransportChannel* GetChannel(const std::string& name);

  // Destroys the given channel.
  void DestroyChannel(TransportChannel* channel);

  // Note: This function is a hack and should not be used.
  TransportChannelImpl* GetImplementation(TransportChannel* channel);

  // Invoked when we notice that there is no matching channel on our peer.
  sigslot::signal2<Session*, const std::string&> SignalChannelGone;

 private:
  typedef std::list<Transport*> TransportList;
  typedef std::map<std::string, TransportChannelProxy*> ChannelMap;

  SessionManager *session_manager_;
  std::string name_;
  std::string remote_name_;
  bool initiator_;
  SessionID id_;
  std::string session_type_;
  SessionClient* client_;
  const SessionDescription *description_;
  const SessionDescription *remote_description_;
  State state_;
  Error error_;
  std::string redirect_target_;
  // Note that the following two members are mutually exclusive
  TransportList potential_transports_; // order implies preference
  Transport* transport_;  // negotiated transport
  ChannelMap channels_;
  bool compatibility_mode_;  // indicates talking to an old client
  XmlElements candidates_;  // holds candidates sent in case of compat-mode

  // Creates or destroys a session.  (These are called only SessionManager.)
  Session(SessionManager *session_manager,
          const std::string& name,
          const SessionID& id,
          const std::string& session_type,
          SessionClient* client);
  ~Session();

  // Updates the state, signaling if necessary.
  void SetState(State state);

  // Updates the error state, signaling if necessary.
  void SetError(Error error);

  // To improve connection time, this creates the channels on the most common
  // transport type and initiates connection.
  void ConnectDefaultTransportChannels(bool create);

  // If a new channel is created after we have created the default transport,
  // then we should create this channel as well and let it connect.
  void CreateDefaultTransportChannel(const std::string& name);

  // Creates a default set of transports if the client did not specify some.
  void CreateTransports();

  // Attempts to choose a transport that is in both our list and the other
  // clients.  This will examine the children of the given XML element to find
  // the descriptions of the other client's transports.  We will pick the first
  // transport in the other client's list that we also support.
  // (This is called only by SessionManager.)
  bool ChooseTransport(const buzz::XmlElement* msg);

  // Called when a single transport has been negotiated.
  void SetTransport(Transport* transport);

  // Called when the first channel of a transport begins connecting.  We use
  // this to start a timer, to make sure that the connection completes in a
  // reasonable amount of time.
  void OnTransportConnecting(Transport* transport);

  // Called when a transport changes its writable state.  We track this to make
  // sure that the transport becomes writable within a reasonable amount of
  // time.  If this does not occur, we signal an error.
  void OnTransportWritable(Transport* transport);

  // Called when a transport requests signaling.
  void OnTransportRequestSignaling(Transport* transport);

  // Called when a transport signals that it has a message to send.   Note that
  // these messages are just the transport part of the stanza; they need to be
  // wrapped in the appropriate session tags.
  void OnTransportSendMessage(Transport* transport, const XmlElements& elems);

  // Called when a transport signals that it found an error in an incoming
  // message.
  void OnTransportSendError(Transport* transport,
                            const buzz::XmlElement* stanza,
                            const buzz::QName& name,
                            const std::string& type,
                            const std::string& text,
                            const buzz::XmlElement* extra_info);

  // Called when we notice that one of our local channels has no peer, so it
  // should be destroyed.
  void OnTransportChannelGone(Transport* transport, const std::string& name);

  // When the session needs to send signaling messages, it beings by requesting
  // signaling.  The client should handle this by calling OnSignalingReady once
  // it is ready to send the messages.
  // (These are called only by SessionManager.)
  sigslot::signal1<Session*> SignalRequestSignaling;
  void OnSignalingReady();

  // Sends a message of the given type to the other client.  The body will
  // contain the given list of elements (which are consumed by the function).
  void SendSessionMessage(const std::string& type,
                          const XmlElements& elems);

  // Sends a message back to the other client indicating that we have received
  // and accepted their message.
  void SendAcknowledgementMessage(const buzz::XmlElement* stanza);

  // Once signaling is ready, the session will use this signal to request the
  // sending of each message.  When messages are received by the other client,
  // they should be handed to OnIncomingMessage.
  // (These are called only by SessionManager.)
  sigslot::signal2<Session *, const buzz::XmlElement*> SignalOutgoingMessage;
  void OnIncomingMessage(const buzz::XmlElement* stanza);

  void OnFailedSend(const buzz::XmlElement* orig_stanza,
                    const buzz::XmlElement* error_stanza);

  // Invoked when an error is found in an incoming message.  This is translated
  // into the appropriate XMPP response by SessionManager.
  sigslot::signal6<Session*,
                   const buzz::XmlElement*,
                   const buzz::QName&,
                   const std::string&,
                   const std::string&,
                   const buzz::XmlElement*> SignalErrorMessage;

  // Handlers for the various types of messages.  These functions may take
  // pointers to the whole stanza or to just the session element.
  bool OnInitiateMessage(const buzz::XmlElement* stanza,
                         const buzz::XmlElement* session);
  bool OnAcceptMessage(const buzz::XmlElement* stanza,
                       const buzz::XmlElement* session);
  bool OnRejectMessage(const buzz::XmlElement* stanza,
                       const buzz::XmlElement* session);
  bool OnRedirectMessage(const buzz::XmlElement* stanza,
                         const buzz::XmlElement* session);
  bool OnInfoMessage(const buzz::XmlElement* stanza,
                     const buzz::XmlElement* session);
  bool OnTransportAcceptMessage(const buzz::XmlElement* stanza,
                                const buzz::XmlElement* session);
  bool OnTransportInfoMessage(const buzz::XmlElement* stanza,
                              const buzz::XmlElement* session);
  bool OnTerminateMessage(const buzz::XmlElement* stanza,
                          const buzz::XmlElement* session);
  bool OnCandidatesMessage(const buzz::XmlElement* stanza,
                           const buzz::XmlElement* session);

  // Helper functions for parsing various message types.  CheckState verifies
  // that we are in the appropriate state to receive this message.  The latter
  // three verify that an element has the required child or attribute.
  bool CheckState(const buzz::XmlElement* stanza, State state);
  bool FindRequiredElement(const buzz::XmlElement* stanza,
                           const buzz::XmlElement* parent,
                           const buzz::QName& name,
                           const buzz::XmlElement** elem);
  bool FindRemoteSessionDescription(const buzz::XmlElement* stanza,
                                    const buzz::XmlElement* session);
  bool FindRequiredAttribute(const buzz::XmlElement* stanza,
                             const buzz::XmlElement* elem,
                             const buzz::QName& name,
                             std::string* value);

  // Handles messages posted to us.
  void OnMessage(talk_base::Message *pmsg);

  friend class SessionManager;  // For access to constructor, destructor,
                                // and signaling related methods.
};

} // namespace cricket

#endif // _SESSION_H_
