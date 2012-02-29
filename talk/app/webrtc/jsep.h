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

// Interfaces matching the JSEP proposal.
// http://www.ietf.org/id/draft-uberti-rtcweb-jsep-02.txt

#ifndef TALK_APP_WEBRTC_JSEP_H_
#define TALK_APP_WEBRTC_JSEP_H_

#include <string>
#include <vector>

namespace cricket {
class SessionDescription;
class Candidate;
}  // namespace cricket

namespace webrtc {

// Class used for describing what media a PeerConnection can receive.
class MediaHints {
 public:
  MediaHints() : has_audio_(true), has_video_(true) {}
  MediaHints(bool receive_audio, bool receive_video)
      : has_audio_(receive_audio),
        has_video_(receive_video) {
  }
  // The peer wants to  receive audio.
  bool has_audio() const { return has_audio_; }
  // The peer wants to receive video.
  bool has_video() const { return has_video_; }

 private:
  bool has_audio_;
  bool has_video_;
};

// Class representation of an ICE candidate.
class IceCandidateInterface {
 public:
  virtual ~IceCandidateInterface() {}
  // The m= line this candidate is associated with.
  virtual std::string label() const = 0;
  virtual const cricket::Candidate& candidate() const = 0;
  // Creates a SDP-ized form of this candidate.
  virtual bool ToString(std::string* out) const = 0;
};

// Creates a IceCandidateInterface based on SDP string.
// Returns NULL if the sdp string can't be parsed.
IceCandidateInterface* CreateIceCandidate(const std::string& label,
                                          const std::string& sdp);

// Class representation of a Session description.
class SessionDescriptionInterface {
 public:
  virtual ~SessionDescriptionInterface() {}
  virtual const cricket::SessionDescription* description() const = 0;
  // Release ownership of cricket::SessionDescription.
  virtual cricket::SessionDescription* ReleaseDescription() = 0;
  // Adds the specified candidate to the description.
  // Ownership is not transferred.
  virtual void AddCandidate(const IceCandidateInterface* candidate) = 0;
  // Serializes the description to SDP.
  virtual bool ToString(std::string* out) const = 0;
};

// Creates a SessionDescriptionInterface based on SDP string.
// Returns NULL if the sdp string can't be parsed.
SessionDescriptionInterface* CreateSessionDescription(const std::string& sdp);

// Jsep Ice candidate callback interface. An application should implement these
// methods to be notified of new local candidates.
class IceCandidateObserver {
 public:
  // New Ice candidate have been found.
  virtual void OnIceCandidate(const IceCandidateInterface* candidate) = 0;
  // All Ice candidates have been found.
  virtual void OnIceComplete() = 0;

 protected:
  ~IceCandidateObserver() {}
};

// Interface for implementing Jsep. PeerConnection implements these functions.
class JsepInterface {
 public:
  enum Action {
    kOffer,
    kAnswer,
  };

  virtual SessionDescriptionInterface* CreateOffer(const MediaHints& hints) = 0;
  // Create an answer to an offer. Returns NULL if an answer can't be created.
  virtual SessionDescriptionInterface* CreateAnswer(
      const MediaHints& hints,
      const SessionDescriptionInterface* offer) = 0;
  // Sets the local session description.
  // JsepInterface take ownership of |desc|.
  virtual bool SetLocalDescription(Action action,
                                   SessionDescriptionInterface* desc) = 0;
  // Sets the remote session description.
  // JsepInterface take ownership of |desc|.
  virtual bool SetRemoteDescription(Action action,
                                    SessionDescriptionInterface* desc) = 0;
  // Processes received ICE information.
  virtual bool ProcessIceMessage(
      const IceCandidateInterface* ice_candidate) = 0;

  virtual const SessionDescriptionInterface* local_description() const = 0;
  virtual const SessionDescriptionInterface* remote_description() const = 0;

 protected:
  ~JsepInterface() {}
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_JSEP_H_
