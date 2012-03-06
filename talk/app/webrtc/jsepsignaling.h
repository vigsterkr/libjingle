/*
 * libjingle
 * Copyright 2012, Google Inc.
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

// This file contains classes used for handling JSEP signaling between
// two PeerConnections.

#ifndef TALK_APP_WEBRTC_JSEPSIGNALING_H_
#define TALK_APP_WEBRTC_JSEPSIGNALING_H_

#include <string>
#include <vector>

#include "talk/app/webrtc/jsep.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/scoped_ref_ptr.h"

namespace talk_base {
class Thread;
}  // namespace talk_base

namespace cricket {
struct StreamParams;
}  // namespace cricket

namespace webrtc {

class JsepIceCandidate;
class MediaStreamInterface;
class StreamCollection;
class StreamCollectionInterface;
class SessionDescriptionProvider;

// JsepRemoteMediaStreamObserver is triggered when
// JsepSignaling::SetRemoteDescription is called with a new
// SessionDescription with a new set of MediaStreams.
class JsepRemoteMediaStreamObserver {
 public:
  // Triggered when media is received on a new stream from remote peer.
  virtual void OnAddStream(MediaStreamInterface* stream) = 0;

  // Triggered when a remote peer close a stream.
  virtual void OnRemoveStream(MediaStreamInterface* stream) = 0;
 protected:
  ~JsepRemoteMediaStreamObserver() {}
};

// JsepSignaling is a class responsible for handling Jsep signaling
// between two PeerConnection objects. The class is responsible for creating SDP
// offers/answers based on the MediaStream the local peer want to send.
// Further more it is responsible for creating the remote MediaStreams that a
// remote peer signals in an SDP message that it want to send.
//
// JsepSignaling is Thread-compatible and all non-const methods are
// expected to be called on the signaling thread.
class JsepSignaling : public JsepInterface {
 public:
  JsepSignaling(talk_base::Thread* signaling_thread,
                SessionDescriptionProvider* provider,
                JsepRemoteMediaStreamObserver* stream_observer);
  virtual ~JsepSignaling();

  void SetLocalStreams(StreamCollectionInterface* local_streams);

  // Returns all current remote MediaStreams.
  StreamCollection* remote_streams() { return remote_streams_.get(); }

  // Implement JsepInterface.
  virtual SessionDescriptionInterface* CreateOffer(const MediaHints& hints);
  virtual SessionDescriptionInterface* CreateAnswer(
      const MediaHints& hints,
      const SessionDescriptionInterface* offer);
  virtual bool SetLocalDescription(Action action,
                                   SessionDescriptionInterface* desc);
  virtual bool SetRemoteDescription(Action action,
                                    SessionDescriptionInterface* desc);
  virtual bool ProcessIceMessage(const IceCandidateInterface* ice_candidate);
  virtual const SessionDescriptionInterface* local_description() const {
    return local_description_.get();
  }
  virtual const SessionDescriptionInterface* remote_description() const {
    return remote_description_.get();
  }

 private:
  // Creates and destroys remote media streams based on |remote_desc|.
  void UpdateRemoteStreams(const cricket::SessionDescription* remote_desc);
  // Create new MediaStreams and Tracks if they exist in |streams|
  // Both new and existing MediaStreams are added to |current_streams|.
  template <typename TrackInterface, typename TrackProxy>
  void UpdateRemoteStreamsList(
      const std::vector<cricket::StreamParams>& streams,
      StreamCollection* current_streams);

  talk_base::Thread* signaling_thread_;
  SessionDescriptionProvider* provider_;
  JsepRemoteMediaStreamObserver* stream_observer_;
  talk_base::scoped_refptr<StreamCollectionInterface> local_streams_;
  talk_base::scoped_ptr<SessionDescriptionInterface> local_description_;
  talk_base::scoped_refptr<StreamCollection> remote_streams_;
  talk_base::scoped_ptr<SessionDescriptionInterface> remote_description_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_JSEPSIGNALING_H_
