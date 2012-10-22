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

// Interfaces matching the draft-ietf-rtcweb-jsep-00.
// TODO(ronghuawu): Remove all the jsep-00 APIs (marked as deprecated) once
// chromium (WebKit and glue code) is ready. And update the comment above to
// jsep-01.

#ifndef TALK_APP_WEBRTC_JSEP_H_
#define TALK_APP_WEBRTC_JSEP_H_

#include <string>
#include <vector>

#include "talk/base/basictypes.h"
#include "talk/base/refcount.h"

namespace cricket {
class SessionDescription;
class Candidate;
}  // namespace cricket

namespace webrtc {

class MediaConstraintsInterface;

class SessionDescriptionOptions {
 public:
  SessionDescriptionOptions() : has_audio_(true), has_video_(true) {}
  SessionDescriptionOptions(bool receive_audio, bool receive_video)
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

// Class used for describing what media a PeerConnection can receive.
class MediaHints {  // Deprecated (jsep00)
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
// An instance of this interface is supposed to be owned by one class at
// a time and is therefore not expected to be thread safe.
class IceCandidateInterface {
 public:
  virtual ~IceCandidateInterface() {}
  /// If present, this contains the identierfier of the "media stream
  // identification" as defined in [RFC 3388] for m-line this candidate is
  // assocated with.
  virtual std::string sdp_mid() const = 0;
  // This indeicates the index (starting at zero) of m-line in the SDP this
  // candidate is assocated with.
  virtual int sdp_mline_index() const = 0;
  virtual const cricket::Candidate& candidate() const = 0;
  // Creates a SDP-ized form of this candidate.
  virtual bool ToString(std::string* out) const = 0;
};

// Creates a IceCandidateInterface based on SDP string.
// Returns NULL if the sdp string can't be parsed.
IceCandidateInterface* CreateIceCandidate(const std::string& sdp_mid,
                                          int sdp_mline_index,
                                          const std::string& sdp);

// This class represents a collection of candidates for a specific m-line.
// This class is used in SessionDescriptionInterface to represent all known
// candidates for a certain m-line.
class IceCandidateCollection {
 public:
  virtual ~IceCandidateCollection() {}
  virtual size_t count() const = 0;
  // Returns true if an equivalent |candidate| exist in the collection.
  virtual bool HasCandidate(const IceCandidateInterface* candidate) const = 0;
  virtual const IceCandidateInterface* at(size_t index) const = 0;
};

// Class representation of a Session description.
// An instance of this interface is supposed to be owned by one class at
// a time and is therefore not expected to be thread safe.
class SessionDescriptionInterface {
 public:
  // Supported types:
  static const char kOffer[];
  static const char kPrAnswer[];
  static const char kAnswer[];

  virtual ~SessionDescriptionInterface() {}
  virtual cricket::SessionDescription* description() = 0;
  virtual const cricket::SessionDescription* description() const = 0;
  // Get the session id and session version, which are defined based on
  // RFC 4566 for the SDP o= line.
  virtual std::string session_id() const = 0;
  virtual std::string session_version() const = 0;
  virtual std::string type() const = 0;
  // Adds the specified candidate to the description.
  // Ownership is not transferred.
  // Returns false if the session description does not have a media section that
  // corresponds to the |candidate| label.
  virtual bool AddCandidate(const IceCandidateInterface* candidate) = 0;
  // Returns the number of m- lines in the session description.
  virtual size_t number_of_mediasections() const = 0;
  // Returns a collection of all candidates that belong to a certain m-line
  virtual const IceCandidateCollection* candidates(
      size_t mediasection_index) const = 0;
  // Serializes the description to SDP.
  virtual bool ToString(std::string* out) const = 0;
};

// Deprecated (jsep00)
SessionDescriptionInterface* CreateSessionDescription(const std::string& sdp);
// Creates a SessionDescriptionInterface based on SDP string and the type.
// Returns NULL if the sdp string can't be parsed or the type is unsupported.
SessionDescriptionInterface* CreateSessionDescription(const std::string& type,
                                                      const std::string& sdp);

// Jsep Ice candidate callback interface. An application should implement these
// methods to be notified of new local candidates.
class IceCandidateObserver {
 public:
  // TODO(ronghuawu): Implement OnIceChange.
  // Called any time the iceState changes.
  virtual void OnIceChange() {}
  // New Ice candidate have been found.
  virtual void OnIceCandidate(const IceCandidateInterface* candidate) = 0;
  // All Ice candidates have been found.
  // Deprecated (jsep00)
  virtual void OnIceComplete() {}

 protected:
  ~IceCandidateObserver() {}
};

// Jsep CreateOffer and CreateAnswer callback interface.
class CreateSessionDescriptionObserver : public talk_base::RefCountInterface {
 public:
  // The implementation of the CreateSessionDescriptionObserver takes
  // the ownership of the |desc|.
  virtual void OnSuccess(SessionDescriptionInterface* desc) = 0;
  virtual void OnFailure(const std::string& error) = 0;

 protected:
  ~CreateSessionDescriptionObserver() {}
};

// Jsep SetLocalDescription and SetRemoteDescription callback interface.
class SetSessionDescriptionObserver : public talk_base::RefCountInterface {
 public:
  virtual void OnSuccess() = 0;
  virtual void OnFailure(const std::string& error) = 0;

 protected:
  ~SetSessionDescriptionObserver() {}
};

// Interface for implementing Jsep. PeerConnection implements these functions.
class JsepInterface {
 public:
  // Indicates the type of SessionDescription in a call to SetLocalDescription
  // and SetRemoteDescription.
  // Deprecated (jsep00)
  enum Action {
    kOffer,
    kPrAnswer,
    kAnswer,
  };

  // Indicates what types of local candidates should be used.
  // Deprecated (jsep00)
  enum IceOptions {
    kUseAll,
    kNoRelay,
    kOnlyRelay
  };

  struct IceServer {
    std::string uri;
    std::string password;
  };

  typedef std::vector<IceServer> IceServers;

  // Deprecated (jsep00)
  virtual SessionDescriptionInterface* CreateOffer(const MediaHints& hints) = 0;

  // Deprecated (jsep00)
  // Create an answer to an offer. Returns NULL if an answer can't be created.
  virtual SessionDescriptionInterface* CreateAnswer(
      const MediaHints& hints,
      const SessionDescriptionInterface* offer) = 0;

  // Deprecated (jsep00)
  // Starts or updates the ICE Agent process of
  // gathering local candidates and pinging remote candidates.
  // SetLocalDescription must be called before calling this method.
  virtual bool StartIce(IceOptions options) = 0;

  // Deprecated (jsep00)
  // Sets the local session description.
  // JsepInterface take ownership of |desc|.
  virtual bool SetLocalDescription(Action action,
                                   SessionDescriptionInterface* desc) = 0;

  // Deprecated (jsep00)
  // Sets the remote session description.
  // JsepInterface take ownership of |desc|.
  virtual bool SetRemoteDescription(Action action,
                                    SessionDescriptionInterface* desc) = 0;

  // Deprecated (jsep00)
  // Processes received ICE information.
  virtual bool ProcessIceMessage(
      const IceCandidateInterface* ice_candidate) = 0;

  virtual const SessionDescriptionInterface* local_description() const = 0;
  virtual const SessionDescriptionInterface* remote_description() const = 0;

  // JSEP01
  // Create a new offer.
  // The CreateSessionDescriptionObserver callback will be called when done.
  virtual void CreateOffer(CreateSessionDescriptionObserver* observer,
                           const MediaConstraintsInterface* constraints) = 0;
  // Create an answer to an offer.
  // The CreateSessionDescriptionObserver callback will be called when done.
  virtual void CreateAnswer(CreateSessionDescriptionObserver* observer,
                            const MediaConstraintsInterface* constraints) = 0;
  // Sets the local session description.
  // JsepInterface takes the ownership of |desc| even if it fails.
  // The |observer| callback will be called when done.
  virtual void SetLocalDescription(SetSessionDescriptionObserver* observer,
                                   SessionDescriptionInterface* desc) = 0;
  // Sets the remote session description.
  // JsepInterface takes the ownership of |desc| even if it fails.
  // The |observer| callback will be called when done.
  virtual void SetRemoteDescription(SetSessionDescriptionObserver* observer,
                                    SessionDescriptionInterface* desc) = 0;
  // Restarts or updates the ICE Agent process of gathering local candidates
  // and pinging remote candidates.
  virtual bool UpdateIce(const IceServers& configuration,
                         const MediaConstraintsInterface* constraints) = 0;
  // Provides a remote candidate to the ICE Agent.
  // A copy of the |candidate| will be created and added to the remote
  // description. So the caller of this method still has the ownership of the
  // |candidate|.
  // TODO(ronghuawu): Consider to change this so that the AddIceCandidate will
  // take the ownership of the |candidate|.
  virtual bool AddIceCandidate(const IceCandidateInterface* candidate) = 0;

 protected:
  virtual ~JsepInterface() {}
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_JSEP_H_
