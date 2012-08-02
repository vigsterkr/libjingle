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

// This file contains the PeerConnection interface as defined in
// http://dev.w3.org/2011/webrtc/editor/webrtc.html#peer-to-peer-connections.
// Applications must use this interface to implement peerconnection.
// PeerConnectionFactory class provides factory methods to create
// peerconnection, mediastream and media tracks objects.
//
// The Following steps are needed to setup a typical call using Jsep.
// 1. Create a PeerConnectionFactoryInterface. Check constructors for more
// information about input parameters.
// 2. Create a PeerConnection object. Provide a configuration string which
// points either to stun or turn server to generate ICE candidates and provide
// an object that implements the PeerConnectionObserver interface.
// 3. Create local MediaStream and MediaTracks using the PeerConnectionFactory
// and add it to PeerConnection by calling AddStream.
// 4. Create an offer and serialize it and send it to the remote peer.
// 5. Once an ice candidate have been found PeerConnection will call the
// observer function OnIceCandidate. The candidates must also be serialized and
// sent to the remote peer.
// 6. Once an answer is received from the remote peer, call
// SetLocalSessionDescription with the offer and SetRemoteSessionDescription
// with the remote answer.
// 7. Once a remote candidate is received from the remote peer, provide it to
// the peerconnection by calling AddIceCandidate.


// The Receiver of a call can decide to accept or reject the call.
// This decision will be taken by the application not peerconnection.
// If application decides to accept the call
// 1. Create PeerConnectionFactoryInterface if it doesn't exist.
// 2. Create a new PeerConnection.
// 3. Provide the remote offer to the new PeerConnection object by calling
// SetRemoteSessionDescription.
// 4. Generate an answer to the remote offer by calling CreateAnswer and send it
// back to the remote peer.
// 5. Provide the local answer to the new PeerConnection by calling
// SetLocalSessionDescription with the answer.
// 6. Provide the remote ice candidates by calling AddIceCandidate.
// 7. Once a candidate have been found PeerConnection will call the observer
// function OnIceCandidate. Send these candidates to the remote peer.

#ifndef TALK_APP_WEBRTC_PEERCONNECTIONINTERFACE_H_
#define TALK_APP_WEBRTC_PEERCONNECTIONINTERFACE_H_

#include <string>
#include <vector>

#include "talk/app/webrtc/jsep.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/base/socketaddress.h"

namespace talk_base {
class Thread;
}

namespace cricket {
class PortAllocator;
}

namespace webrtc {
class VideoCaptureModule;

// MediaStream container interface.
class StreamCollectionInterface : public talk_base::RefCountInterface {
 public:
  virtual size_t count() = 0;
  virtual MediaStreamInterface* at(size_t index) = 0;
  virtual MediaStreamInterface* find(const std::string& label) = 0;
 protected:
  // Dtor protected as objects shouldn't be deleted via this interface.
  ~StreamCollectionInterface() {}
};

// PeerConnection callback interface. Application should implement these
// methods.
class PeerConnectionObserver : public IceCandidateObserver {
 public:
  enum StateType {
    kReadyState,
    kIceState,
  };

  virtual void OnError() = 0;

  // Triggered when ReadyState, SdpState or IceState have changed.
  virtual void OnStateChange(StateType state_changed) = 0;

  // Triggered when media is received on a new stream from remote peer.
  virtual void OnAddStream(MediaStreamInterface* stream) = 0;

  // Triggered when a remote peer close a stream.
  virtual void OnRemoveStream(MediaStreamInterface* stream) = 0;

  // Triggered when renegotation is needed, for example the ICE has restarted.
  virtual void OnRenegotiationNeeded() {}

 protected:
  // Dtor protected as objects shouldn't be deleted via this interface.
  ~PeerConnectionObserver() {}
};

class PeerConnectionInterface : public JsepInterface,
                                public talk_base::RefCountInterface {
 public:
  enum ReadyState {
    kNew,
    kOpening,
    kActive,
    kClosing,
    kClosed,
  };

  enum IceState {
    kIceNew,
    kIceGathering,
    kIceWaiting,
    kIceChecking,
    kIceConnected,
    kIceCompleted,
    kIceFailed,
    kIceClosed,
  };

  // Accessor methods to active local streams.
  virtual talk_base::scoped_refptr<StreamCollectionInterface>
      local_streams() = 0;

  // Accessor methods to remote streams.
  virtual talk_base::scoped_refptr<StreamCollectionInterface>
      remote_streams() = 0;

  // Add a new local stream.
  // Note that a SessionDescription negotiation is needed before the
  // remote peer can receive the stream.
  // Deprecated (jsep00)
  virtual void AddStream(LocalMediaStreamInterface* stream) = 0;

  // Add a new MediaStream to be sent on this PeerConnection.
  // Note that a SessionDescription negotiation is needed before the
  // remote peer can receive the stream.
  virtual bool AddStream(MediaStreamInterface* stream,
                         const MediaConstraintsInterface* constraints) = 0;

  // Remove a MediaStream from this PeerConnection.
  // Note that a SessionDescription negotiation is need before the
  // remote peer is notified.
  virtual void RemoveStream(MediaStreamInterface* stream) = 0;

  // Returns the current ReadyState.
  virtual ReadyState ready_state() = 0;

  // Returns the current IceState.
  virtual IceState ice_state() = 0;

 protected:
  // Dtor protected as objects shouldn't be deleted via this interface.
  ~PeerConnectionInterface() {}
};

// Helper function to create a new instance of cricket::VideoCapturer
// from VideoCaptureModule.
// TODO: This function should be removed once chrome implement video
// capture as the cricket::VideoCapturer.
cricket::VideoCapturer* CreateVideoCapturer(VideoCaptureModule* vcm);

// Factory class used for creating cricket::PortAllocator that is used
// for ICE negotiation.
class PortAllocatorFactoryInterface : public talk_base::RefCountInterface {
 public:
  struct StunConfiguration {
    StunConfiguration(const std::string& address, int port)
        : server(address, port) {}
    // STUN server address and port.
    talk_base::SocketAddress server;
  };

  struct TurnConfiguration {
    TurnConfiguration(const std::string& address,
                      int port,
                      const std::string& username,
                      const std::string& password)
        : server(address, port),
          username(username),
          password(password) {}
    talk_base::SocketAddress server;
    std::string username;
    std::string password;
  };

  virtual cricket::PortAllocator* CreatePortAllocator(
      const std::vector<StunConfiguration>& stun_servers,
      const std::vector<TurnConfiguration>& turn_configurations) = 0;

 protected:
  PortAllocatorFactoryInterface() {}
  ~PortAllocatorFactoryInterface() {}
};

// PeerConnectionFactoryInterface is the factory interface use for creating
// PeerConnection, MediaStream and media tracks.
// PeerConnectionFactoryInterface will create required libjingle threads,
// socket and network manager factory classes for networking.
// If application decides to provide its own implementation of these classes
// it should use alternate create method which accepts a threads and a
// PortAllocatorFactoryInterface as input.
class PeerConnectionFactoryInterface : public talk_base::RefCountInterface {
 public:
  // Deprecated (jsep00)
  virtual talk_base::scoped_refptr<PeerConnectionInterface>
      CreatePeerConnection(const std::string& config,
                           PeerConnectionObserver* observer) = 0;
  virtual talk_base::scoped_refptr<PeerConnectionInterface>
      CreatePeerConnection(const JsepInterface::IceServers& configuration,
                           const MediaConstraintsInterface* constraints,
                           PeerConnectionObserver* observer) = 0;
  virtual talk_base::scoped_refptr<LocalMediaStreamInterface>
      CreateLocalMediaStream(const std::string& label) = 0;

  virtual talk_base::scoped_refptr<LocalVideoTrackInterface>
      CreateLocalVideoTrack(const std::string& label,
                            cricket::VideoCapturer* video_device) = 0;

  virtual talk_base::scoped_refptr<LocalAudioTrackInterface>
      CreateLocalAudioTrack(const std::string& label,
                            AudioDeviceModule* audio_device) = 0;

 protected:
  // Dtor and ctor protected as objects shouldn't be created or deleted via
  // this interface.
  PeerConnectionFactoryInterface() {}
  ~PeerConnectionFactoryInterface() {} // NOLINT
};

// Create a new instance of PeerConnectionFactoryInterface.
talk_base::scoped_refptr<PeerConnectionFactoryInterface>
CreatePeerConnectionFactory();

// Create a new instance of PeerConnectionFactoryInterface.
// Ownership of |factory| and |default_adm| is transferred to the returned
// factory.
talk_base::scoped_refptr<PeerConnectionFactoryInterface>
CreatePeerConnectionFactory(talk_base::Thread* worker_thread,
                            talk_base::Thread* signaling_thread,
                            PortAllocatorFactoryInterface* factory,
                            AudioDeviceModule* default_adm);

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_PEERCONNECTIONINTERFACE_H_
