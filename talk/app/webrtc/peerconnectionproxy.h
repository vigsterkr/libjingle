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

#ifndef TALK_APP_WEBRTC_PEERCONNECTIONPROXY_H_
#define TALK_APP_WEBRTC_PEERCONNECTIONPROXY_H_

#include "talk/app/webrtc/mediastreamsignaling.h"
#include "talk/app/webrtc/peerconnectioninterface.h"

namespace talk_base {
class Thread;
}

namespace webrtc {

class PeerConnectionProxy : public PeerConnectionInterface,
                            public talk_base::MessageHandler {
 public:
  static PeerConnectionInterface* Create(
      talk_base::Thread* signaling_thread,
      PeerConnectionInterface* peerconnection);
  virtual talk_base::scoped_refptr<StreamCollectionInterface> local_streams();
  virtual talk_base::scoped_refptr<StreamCollectionInterface> remote_streams();
  virtual bool AddStream(MediaStreamInterface* local_stream,
                         const MediaConstraintsInterface* constraints);
  virtual void RemoveStream(MediaStreamInterface* local_stream);
  virtual talk_base::scoped_refptr<DtmfSenderInterface> CreateDtmfSender(
      AudioTrackInterface* track);
  virtual bool GetStats(StatsObserver* observer,
                        webrtc::MediaStreamTrackInterface* track);
  virtual talk_base::scoped_refptr<DataChannelInterface> CreateDataChannel(
      const std::string& label,
      const DataChannelInit* config);

  // TODO(perkj): Remove ready_state when callers removed. It is deprecated.
  virtual ReadyState ready_state() { return signaling_state(); }
  virtual SignalingState signaling_state();
  // TODO(bemasc): Remove ice_state when callers are removed. It is deprecated.
  virtual IceState ice_state();
  virtual IceConnectionState ice_connection_state();
  virtual IceGatheringState ice_gathering_state();

  virtual const SessionDescriptionInterface* local_description() const;
  virtual const SessionDescriptionInterface* remote_description() const;

  // JSEP01
  virtual void CreateOffer(CreateSessionDescriptionObserver* observer,
                           const MediaConstraintsInterface* constraints);
  virtual void CreateAnswer(CreateSessionDescriptionObserver* observer,
                            const MediaConstraintsInterface* constraints);
  virtual void SetLocalDescription(SetSessionDescriptionObserver* observer,
                                   SessionDescriptionInterface* desc);
  virtual void SetRemoteDescription(SetSessionDescriptionObserver* observer,
                                    SessionDescriptionInterface* desc);
  virtual bool UpdateIce(const IceServers& configuration,
                         const MediaConstraintsInterface* constraints);
  virtual bool AddIceCandidate(const IceCandidateInterface* candidate);

 protected:
  PeerConnectionProxy(talk_base::Thread* signaling_thread,
                      PeerConnectionInterface* peerconnection);
  virtual ~PeerConnectionProxy();

 private:
  // Implement talk_base::MessageHandler.
  void OnMessage(talk_base::Message* msg);

  mutable talk_base::Thread* signaling_thread_;
  talk_base::scoped_refptr<PeerConnectionInterface> peerconnection_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_PEERCONNECTIONPROXY_H_
