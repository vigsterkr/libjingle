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

#ifndef _CRICKET_P2P_BASE_SESSIONCLIENT_H_
#define _CRICKET_P2P_BASE_SESSIONCLIENT_H_

namespace buzz {
class XmlElement;
}

namespace cricket {

class Session;
class SessionDescription;

// A SessionClient exists in 1-1 relation with each session.  The implementor
// of this interface is the one that understands *what* the two sides are
// trying to send to one another.  The lower-level layers only know how to send
// data; they do not know what is being sent.
class SessionClient {
 public:
  // Notifies the client of the creation / destruction of sessions of this type.
  //
  // IMPORTANT: The SessionClient, in its handling of OnSessionCreate, must
  // create whatever channels are indicate in the description.  This is because
  // the remote client may already be attempting to connect those channels. If
  // we do not create our channel right away, then connection may fail or be
  // delayed.
  virtual void OnSessionCreate(Session* session, bool received_initiate) = 0;
  virtual void OnSessionDestroy(Session* session) = 0;

  // Provides functions to convert between the XML description of the session
  // and the data structures useful to the client.  The resulting objects are
  // held by the Session for easy access.
  virtual const SessionDescription* CreateSessionDescription(
      const buzz::XmlElement* element) = 0;
  virtual buzz::XmlElement* TranslateSessionDescription(
      const SessionDescription* description) = 0;

protected:
  // The SessionClient interface explicitly does not include destructor
  virtual ~SessionClient() { }
};

}  // namespace cricket

#endif // _CRICKET_P2P_BASE_SESSIONCLIENT_H_
