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

#include "talk/app/webrtc/peerconnectionproxy.h"

namespace {

enum {
  MSG_ADDSTREAM = 1,
  MSG_REMOVESTREAM,
  MSG_CREATEDTMFSENDER,
  MSG_RETURNLOCALMEDIASTREAMS,
  MSG_RETURNREMOTEMEDIASTREAMS,
  MSG_SIGNALINGSTATE,
  MSG_ICESTATE,
  MSG_ICECONNECTION,
  MSG_ICEGATHERING,
  MSG_CREATEDATACHANNEL,
  MSG_TERMINATE,
  MSG_CREATEOFFER,
  MSG_CREATEANSWER,
  MSG_SETLOCALDESCRIPTION,
  MSG_SETREMOTEDESCRIPTION,
  MSG_UPDATEICE,
  MSG_ADDICECANDIDATE,
  MSG_GETLOCALDESCRIPTION,
  MSG_GETREMOTEDESCRIPTION,
  MSG_GETSTATS,
};

struct MediaStreamParams : public talk_base::MessageData {
  MediaStreamParams(webrtc::MediaStreamInterface* stream,
                    const webrtc::MediaConstraintsInterface* constraints)
      : stream(stream),
        constraints(constraints),
        result(false) {
  }
  webrtc::MediaStreamInterface* stream;
  const webrtc::MediaConstraintsInterface* constraints;
  bool result;
};

struct DtmfSenderParams : public talk_base::MessageData {
  explicit DtmfSenderParams(webrtc::AudioTrackInterface* track)
      : track(track),
        dtmf_sender(NULL) {
  }
  webrtc::AudioTrackInterface* track;
  talk_base::scoped_refptr<webrtc::DtmfSenderInterface> dtmf_sender;
};

struct IceConfigurationParams : public talk_base::MessageData {
  IceConfigurationParams()
      : configuration(NULL),
        constraints(NULL),
        result(false) {
  }
  const webrtc::PeerConnectionInterface::IceServers* configuration;
  const webrtc::MediaConstraintsInterface* constraints;
  bool result;
};

struct StatsParams : public talk_base::MessageData {
  StatsParams(webrtc::StatsObserver* observer,
              webrtc::MediaStreamTrackInterface* track)
    : observer(observer),
      track(track),
      result(false) {
    }
  webrtc::StatsObserver* observer;
  webrtc::MediaStreamTrackInterface* track;
  bool result;
};

struct StreamLabelParams : public talk_base::MessageData {
  explicit StreamLabelParams(const std::string& label)
      : label(label),
        result(false) {
  }
  std::string label;
  bool result;
};

struct JsepSessionDescriptionParams : public talk_base::MessageData {
  JsepSessionDescriptionParams()
      : result(false), desc(NULL), const_desc(NULL) {
  }
  bool result;
  webrtc::SessionDescriptionInterface* desc;
  const webrtc::SessionDescriptionInterface* const_desc;
};

struct CreateSessionDescriptionParams : public talk_base::MessageData {
  CreateSessionDescriptionParams() : observer(NULL), constraints(NULL) {}
  webrtc::CreateSessionDescriptionObserver* observer;
  const webrtc::MediaConstraintsInterface* constraints;
};

struct SetSessionDescriptionParams : public talk_base::MessageData {
  SetSessionDescriptionParams() : observer(NULL), desc(NULL) {}
  webrtc::SetSessionDescriptionObserver* observer;
  webrtc::SessionDescriptionInterface* desc;
};

struct JsepIceCandidateParams : public talk_base::MessageData {
  explicit JsepIceCandidateParams(
      const webrtc::IceCandidateInterface* candidate)
      : result(false),
        candidate(candidate) {}
  bool result;
  const webrtc::IceCandidateInterface* candidate;
};

struct StreamCollectionParams : public talk_base::MessageData {
  explicit StreamCollectionParams(webrtc::StreamCollectionInterface* streams)
      : streams(streams) {}
  talk_base::scoped_refptr<webrtc::StreamCollectionInterface> streams;
};

struct SignalingStateMessage : public talk_base::MessageData {
  SignalingStateMessage() : state(webrtc::PeerConnectionInterface::kStable) {}
  webrtc::PeerConnectionInterface::SignalingState state;
};

struct IceStateMessage : public talk_base::MessageData {
  IceStateMessage() : state(webrtc::PeerConnectionInterface::kIceNew) {}
  webrtc::PeerConnectionInterface::IceState state;
};

struct IceConnectionMessage : public talk_base::MessageData {
  IceConnectionMessage()
      : state(webrtc::PeerConnectionInterface::kIceConnectionNew) {}
  webrtc::PeerConnectionInterface::IceConnectionState state;
};

struct IceGatheringMessage : public talk_base::MessageData {
  IceGatheringMessage()
      : state(webrtc::PeerConnectionInterface::kIceGatheringNew) {}
  webrtc::PeerConnectionInterface::IceGatheringState state;
};

struct CreateDataChannelMessageData : public talk_base::MessageData {
  CreateDataChannelMessageData(std::string label,
                               const webrtc::DataChannelInit* init)
      : label(label),
        init(init) {
  }

  std::string label;
  const webrtc::DataChannelInit* init;
  talk_base::scoped_refptr<webrtc::DataChannelInterface> data_channel;
};

}  // namespace

namespace webrtc {

PeerConnectionInterface* PeerConnectionProxy::Create(
    talk_base::Thread* signaling_thread,
    PeerConnectionInterface* peerconnection) {
  ASSERT(signaling_thread != NULL);
  return new talk_base::RefCountedObject<PeerConnectionProxy>(
      signaling_thread, peerconnection);
}

PeerConnectionProxy::PeerConnectionProxy(
    talk_base::Thread* signaling_thread,
    PeerConnectionInterface* peerconnection)
    : signaling_thread_(signaling_thread),
      peerconnection_(peerconnection) {
}

PeerConnectionProxy::~PeerConnectionProxy() {
  signaling_thread_->Send(this, MSG_TERMINATE);
}

talk_base::scoped_refptr<StreamCollectionInterface>
PeerConnectionProxy::local_streams() {
  if (!signaling_thread_->IsCurrent()) {
    StreamCollectionParams msg(NULL);
    signaling_thread_->Send(this, MSG_RETURNLOCALMEDIASTREAMS, &msg);
    return msg.streams;
  }
  return peerconnection_->local_streams();
}

talk_base::scoped_refptr<StreamCollectionInterface>
PeerConnectionProxy::remote_streams() {
  if (!signaling_thread_->IsCurrent()) {
    StreamCollectionParams msg(NULL);
    signaling_thread_->Send(this, MSG_RETURNREMOTEMEDIASTREAMS, &msg);
    return msg.streams;
  }
  return peerconnection_->remote_streams();
}

bool PeerConnectionProxy::AddStream(
    MediaStreamInterface* local_stream,
    const MediaConstraintsInterface* constraints) {
  if (!signaling_thread_->IsCurrent()) {
    MediaStreamParams msg(local_stream, constraints);
    signaling_thread_->Send(this, MSG_ADDSTREAM, &msg);
    return msg.result;
  }
  return peerconnection_->AddStream(local_stream, constraints);
}

void PeerConnectionProxy::RemoveStream(MediaStreamInterface* remove_stream) {
  if (!signaling_thread_->IsCurrent()) {
    MediaStreamParams msg(remove_stream, NULL);
    signaling_thread_->Send(this, MSG_REMOVESTREAM, &msg);
    return;
  }
  peerconnection_->RemoveStream(remove_stream);
}

talk_base::scoped_refptr<DtmfSenderInterface>
    PeerConnectionProxy::CreateDtmfSender(AudioTrackInterface* track) {
  if (!signaling_thread_->IsCurrent()) {
    DtmfSenderParams msg(track);
    signaling_thread_->Send(this, MSG_CREATEDTMFSENDER, &msg);
    return msg.dtmf_sender;
  }
  return peerconnection_->CreateDtmfSender(track);
}

bool PeerConnectionProxy::GetStats(StatsObserver* observer,
                                   MediaStreamTrackInterface* track) {
  if (!signaling_thread_->IsCurrent()) {
    StatsParams msg(observer, track);
    signaling_thread_->Send(this, MSG_GETSTATS, &msg);
    return msg.result;
  }
  return peerconnection_->GetStats(observer, track);
}

PeerConnectionInterface::SignalingState PeerConnectionProxy::signaling_state() {
  if (!signaling_thread_->IsCurrent()) {
    SignalingStateMessage msg;
    signaling_thread_->Send(this, MSG_SIGNALINGSTATE, &msg);
    return msg.state;
  }
  return peerconnection_->signaling_state();
}


PeerConnectionInterface::IceState PeerConnectionProxy::ice_state() {
  if (!signaling_thread_->IsCurrent()) {
    IceStateMessage msg;
    signaling_thread_->Send(this, MSG_ICESTATE, &msg);
    return msg.state;
  }
  return peerconnection_->ice_state();
}

PeerConnectionInterface::IceConnectionState
PeerConnectionProxy::ice_connection_state() {
  if (!signaling_thread_->IsCurrent()) {
    IceConnectionMessage msg;
    signaling_thread_->Send(this, MSG_ICECONNECTION, &msg);
    return msg.state;
  }
  return peerconnection_->ice_connection_state();
}

PeerConnectionInterface::IceGatheringState
PeerConnectionProxy::ice_gathering_state() {
  if (!signaling_thread_->IsCurrent()) {
    IceGatheringMessage msg;
    signaling_thread_->Send(this, MSG_ICEGATHERING, &msg);
    return msg.state;
  }
  return peerconnection_->ice_gathering_state();
}

talk_base::scoped_refptr<DataChannelInterface>
PeerConnectionProxy::CreateDataChannel(const std::string& label,
                                       const DataChannelInit* config) {
  if (!signaling_thread_->IsCurrent()) {
    CreateDataChannelMessageData msg(label, config);
    signaling_thread_->Send(this, MSG_CREATEDATACHANNEL, &msg);
    return msg.data_channel;
  }
  return peerconnection_->CreateDataChannel(label, config);
}

void PeerConnectionProxy::CreateOffer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  if (!signaling_thread_->IsCurrent()) {
    CreateSessionDescriptionParams msg;
    msg.observer = observer;
    msg.constraints = constraints;
    signaling_thread_->Send(this, MSG_CREATEOFFER, &msg);
    return;
  }
  peerconnection_->CreateOffer(observer, constraints);
}

void PeerConnectionProxy::CreateAnswer(
    CreateSessionDescriptionObserver* observer,
    const MediaConstraintsInterface* constraints) {
  if (!signaling_thread_->IsCurrent()) {
    CreateSessionDescriptionParams msg;
    msg.observer = observer;
    msg.constraints = constraints;
    signaling_thread_->Send(this, MSG_CREATEANSWER, &msg);
    return;
  }
  peerconnection_->CreateAnswer(observer, constraints);
}

void PeerConnectionProxy::SetLocalDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  if (!signaling_thread_->IsCurrent()) {
    SetSessionDescriptionParams msg;
    msg.observer = observer;
    msg.desc = desc;
    signaling_thread_->Send(this, MSG_SETLOCALDESCRIPTION, &msg);
    return;
  }
  peerconnection_->SetLocalDescription(observer, desc);
}

void PeerConnectionProxy::SetRemoteDescription(
    SetSessionDescriptionObserver* observer,
    SessionDescriptionInterface* desc) {
  if (!signaling_thread_->IsCurrent()) {
    SetSessionDescriptionParams msg;
    msg.observer = observer;
    msg.desc = desc;
    signaling_thread_->Send(this, MSG_SETREMOTEDESCRIPTION, &msg);
    return;
  }
  peerconnection_->SetRemoteDescription(observer, desc);
}

bool PeerConnectionProxy::UpdateIce(
    const IceServers& configuration,
    const MediaConstraintsInterface* constraints) {
  if (!signaling_thread_->IsCurrent()) {
    IceConfigurationParams msg;
    msg.configuration = &configuration;
    msg.constraints = constraints;
    signaling_thread_->Send(this, MSG_UPDATEICE, &msg);
    return msg.result;
  }
  return peerconnection_->UpdateIce(configuration, constraints);
}

bool PeerConnectionProxy::AddIceCandidate(
    const IceCandidateInterface* ice_candidate) {
  if (!signaling_thread_->IsCurrent()) {
    JsepIceCandidateParams msg(ice_candidate);
    signaling_thread_->Send(this, MSG_ADDICECANDIDATE, &msg);
    return msg.result;
  }
  return peerconnection_->AddIceCandidate(ice_candidate);
}

const SessionDescriptionInterface*
PeerConnectionProxy::local_description() const {
  if (!signaling_thread_->IsCurrent()) {
    JsepSessionDescriptionParams msg;
    signaling_thread_->Send(const_cast<PeerConnectionProxy*>(this),
                            MSG_GETLOCALDESCRIPTION, &msg);
    return msg.const_desc;
  }
  return peerconnection_->local_description();
}

const SessionDescriptionInterface*
PeerConnectionProxy::remote_description() const {
  if (!signaling_thread_->IsCurrent()) {
    JsepSessionDescriptionParams msg;
    signaling_thread_->Send(const_cast<PeerConnectionProxy*>(this),
                            MSG_GETREMOTEDESCRIPTION, &msg);
    return msg.const_desc;
  }
  return peerconnection_->remote_description();
}

void PeerConnectionProxy::OnMessage(talk_base::Message* msg) {
  talk_base::MessageData* data = msg->pdata;
  switch (msg->message_id) {
    case MSG_ADDSTREAM: {
      MediaStreamParams* param(static_cast<MediaStreamParams*> (data));
      param->result = peerconnection_->AddStream(param->stream,
                                                 param->constraints);
      break;
    }
    case MSG_REMOVESTREAM: {
      MediaStreamParams* param(static_cast<MediaStreamParams*> (data));
      peerconnection_->RemoveStream(param->stream);
      break;
    }
    case MSG_CREATEDTMFSENDER: {
      DtmfSenderParams* param(static_cast<DtmfSenderParams*> (data));
      param->dtmf_sender =
          peerconnection_->CreateDtmfSender(param->track);
      break;
    }
    case MSG_GETSTATS: {
      StatsParams* param(static_cast<StatsParams*> (data));
      param->result = peerconnection_->GetStats(param->observer, param->track);
      break;
    }
    case MSG_RETURNLOCALMEDIASTREAMS: {
      StreamCollectionParams* param(static_cast<StreamCollectionParams*>(data));
      param->streams = peerconnection_->local_streams();
      break;
    }
    case MSG_RETURNREMOTEMEDIASTREAMS: {
      StreamCollectionParams* param(static_cast<StreamCollectionParams*>(data));
      param->streams = peerconnection_->remote_streams();
      break;
    }
    case MSG_SIGNALINGSTATE: {
      SignalingStateMessage* param(static_cast<SignalingStateMessage*> (data));
      param->state = peerconnection_->signaling_state();
      break;
    }
    case MSG_ICESTATE: {
      IceStateMessage* param(static_cast<IceStateMessage*> (data));
      param->state = peerconnection_->ice_state();
      break;
    }
    case MSG_ICECONNECTION: {
      IceConnectionMessage* param(static_cast<IceConnectionMessage*> (data));
      param->state = peerconnection_->ice_connection_state();
      break;
    }
    case MSG_ICEGATHERING: {
      IceGatheringMessage* param(static_cast<IceGatheringMessage*> (data));
      param->state = peerconnection_->ice_gathering_state();
      break;
    }
    case MSG_CREATEDATACHANNEL: {
      CreateDataChannelMessageData* param(
          static_cast<CreateDataChannelMessageData*>(data));
      param->data_channel = peerconnection_->CreateDataChannel(param->label,
                                                               param->init);
      break;
    }
    case MSG_CREATEOFFER: {
      CreateSessionDescriptionParams* param(
          static_cast<CreateSessionDescriptionParams*> (data));
      peerconnection_->CreateOffer(param->observer, param->constraints);
      break;
    }
    case MSG_CREATEANSWER: {
      CreateSessionDescriptionParams* param(
          static_cast<CreateSessionDescriptionParams*> (data));
      peerconnection_->CreateAnswer(param->observer, param->constraints);
      break;
    }
    case MSG_SETLOCALDESCRIPTION: {
      SetSessionDescriptionParams* param(
          static_cast<SetSessionDescriptionParams*> (data));
      peerconnection_->SetLocalDescription(param->observer,
                                           param->desc);
      break;
    }
    case MSG_SETREMOTEDESCRIPTION: {
      SetSessionDescriptionParams* param(
          static_cast<SetSessionDescriptionParams*> (data));
      peerconnection_->SetRemoteDescription(param->observer,
                                            param->desc);
      break;
    }
    case MSG_UPDATEICE: {
      IceConfigurationParams* param(
          static_cast<IceConfigurationParams*> (data));
      param->result  = peerconnection_->UpdateIce(*param->configuration,
                                                  param->constraints);
      break;
    }
    case MSG_ADDICECANDIDATE: {
      JsepIceCandidateParams* param(
          static_cast<JsepIceCandidateParams*> (data));
      param->result  = peerconnection_->AddIceCandidate(param->candidate);
      break;
    }
    case MSG_GETLOCALDESCRIPTION: {
      JsepSessionDescriptionParams* param(
          static_cast<JsepSessionDescriptionParams*> (data));
      param->const_desc  = peerconnection_->local_description();
      break;
    }
    case  MSG_GETREMOTEDESCRIPTION: {
      JsepSessionDescriptionParams* param(
          static_cast<JsepSessionDescriptionParams*> (data));
      param->const_desc  = peerconnection_->remote_description();
      break;
    }
    case MSG_TERMINATE: {
      // Called by the destructor to release the peerconnection reference on the
      // signaling thread.
      peerconnection_ = NULL;
      break;
    }
    default:
      ASSERT(!"NOT IMPLEMENTED");
      break;
  }
}

}  // namespace webrtc
