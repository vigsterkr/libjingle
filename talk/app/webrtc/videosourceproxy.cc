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

#include "talk/app/webrtc/videosourceproxy.h"

#include "talk/media/base/videocapturer.h"

namespace {

enum {
  MSG_REGISTER_OBSERVER = 1,
  MSG_UNREGISTER_OBSERVER,
  MSG_STATE,
  MSG_ADD_SINK,
  MSG_REMOVE_SINK,
  MSG_GET_VIDEO_CAPTURER,
  MSG_OPTIONS,
  MSG_TERMINATE
};

typedef talk_base::TypedMessageData<webrtc::ObserverInterface*>
    ObserverMessageData;
typedef talk_base::TypedMessageData<cricket::VideoRenderer*> SinkMessageData;
typedef talk_base::TypedMessageData<cricket::VideoCapturer*>
    CapturerMessageData;
typedef talk_base::TypedMessageData<const cricket::VideoOptions*>
    OptionsMessageData;
typedef talk_base::TypedMessageData
    <webrtc::MediaSourceInterface::SourceState> StateMessageData;

}  // namespace

namespace webrtc {

VideoSourceInterface* VideoSourceProxy::Create(
    talk_base::Thread* signaling_thread,
    VideoSourceInterface* source) {
  ASSERT(signaling_thread != NULL);
  ASSERT(source != NULL);
  return new talk_base::RefCountedObject<VideoSourceProxy>(signaling_thread,
                                                           source);
}

VideoSourceProxy::VideoSourceProxy(talk_base::Thread* signaling_thread,
                                   VideoSourceInterface* source)
    : signaling_thread_(signaling_thread),
      source_(source) {
}

VideoSourceProxy::~VideoSourceProxy() {
  // Since VideoSourceInterface is reference counted we don't know which
  // application thread that holds on to the last reference.
  // Here we ensure the real implementation is always released on the
  // signaling thread.
  signaling_thread_->Send(this, MSG_TERMINATE);
}

cricket::VideoCapturer* VideoSourceProxy::GetVideoCapturer() {
  if (!signaling_thread_->IsCurrent()) {
    CapturerMessageData msg(NULL);
    signaling_thread_->Send(const_cast<VideoSourceProxy*>(this),
                            MSG_GET_VIDEO_CAPTURER, &msg);
    return msg.data();
  }
  return source_->GetVideoCapturer();
}

void VideoSourceProxy::AddSink(cricket::VideoRenderer* output) {
  if (!signaling_thread_->IsCurrent()) {
    SinkMessageData msg(output);
    signaling_thread_->Send(const_cast<VideoSourceProxy*>(this),
                            MSG_ADD_SINK, &msg);
    return;
  }
  return source_->AddSink(output);
}

void VideoSourceProxy::RemoveSink(cricket::VideoRenderer* output) {
  if (!signaling_thread_->IsCurrent()) {
    SinkMessageData msg(output);
    signaling_thread_->Send(const_cast<VideoSourceProxy*>(this),
                            MSG_REMOVE_SINK, &msg);
    return;
  }
  return source_->RemoveSink(output);
}

const cricket::VideoOptions* VideoSourceProxy::options() const {
  if (!signaling_thread_->IsCurrent()) {
    OptionsMessageData msg(NULL);
    signaling_thread_->Send(const_cast<VideoSourceProxy*>(this),
                            MSG_OPTIONS, &msg);
    return msg.data();
  }
  return source_->options();
}

MediaSourceInterface::SourceState VideoSourceProxy::state() const {
  if (!signaling_thread_->IsCurrent()) {
    StateMessageData msg(MediaSourceInterface::kEnded);
    signaling_thread_->Send(const_cast<VideoSourceProxy*>(this),
                            MSG_STATE, &msg);
    return msg.data();
  }
  return source_->state();
}

void VideoSourceProxy::RegisterObserver(ObserverInterface* observer) {
  if (!signaling_thread_->IsCurrent()) {
    ObserverMessageData msg(observer);
    signaling_thread_->Send(this, MSG_REGISTER_OBSERVER, &msg);
    return;
  }
  return source_->RegisterObserver(observer);
}

void VideoSourceProxy::UnregisterObserver(ObserverInterface* observer) {
  if (!signaling_thread_->IsCurrent()) {
    ObserverMessageData msg(observer);
    signaling_thread_->Send(this, MSG_UNREGISTER_OBSERVER, &msg);
    return;
  }
  return source_->UnregisterObserver(observer);
}


void VideoSourceProxy::OnMessage(talk_base::Message* msg) {
  switch (msg->message_id) {
    case MSG_STATE: {
      StateMessageData* ready_state =
          static_cast<StateMessageData*>(msg->pdata);
      ready_state->data() =  source_->state();
      break;
    }
    case MSG_REGISTER_OBSERVER: {
      ObserverMessageData* observer =
          static_cast<ObserverMessageData*>(msg->pdata);
      source_->RegisterObserver(observer->data());
      break;
    }
    case MSG_UNREGISTER_OBSERVER: {
      ObserverMessageData* observer = static_cast<ObserverMessageData*>(
          msg->pdata);
      source_->UnregisterObserver(observer->data());
      break;
    }
    case MSG_ADD_SINK: {
      SinkMessageData* sink = static_cast<SinkMessageData*>(
          msg->pdata);
      source_->AddSink(sink->data());
      break;
    }
    case MSG_REMOVE_SINK: {
      SinkMessageData* sink = static_cast<SinkMessageData*>(
          msg->pdata);
      source_->RemoveSink(sink->data());
      break;
    }
    case MSG_GET_VIDEO_CAPTURER: {
      CapturerMessageData* capturer = static_cast<CapturerMessageData*>(
          msg->pdata);
      capturer->data() =  source_->GetVideoCapturer();
      break;
    }
    case MSG_OPTIONS: {
      OptionsMessageData* options = static_cast<OptionsMessageData*>(
          msg->pdata);
      options->data() = source_->options();
      break;
    }
    case MSG_TERMINATE: {
      source_ = NULL;
      break;
    }
  }
}

}  // namespace webrtc
