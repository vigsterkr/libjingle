/*
 * libjingle
 * Copyright 2011, Google Inc.
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

#include "talk/app/webrtc/mediastreamtrackproxy.h"

#include "talk/media/base/videocapturer.h"
#include "talk/app/webrtc/videosourceinterface.h"

namespace {

enum {
  MSG_REGISTER_OBSERVER = 1,
  MSG_UNREGISTER_OBSERVER,
  MSG_ID,
  MSG_ENABLED,
  MSG_SET_ENABLED,
  MSG_STATE,
  MSG_GET_AUDIOSOURCE,
  MSG_GET_VIDEOSOURCE,
  MSG_ADD_VIDEORENDERER,
  MSG_REMOVE_VIDEORENDERER,
  MSG_GET_VIDEOFRAMEINPUT,
};

typedef talk_base::TypedMessageData<std::string*> LabelMessageData;
typedef talk_base::TypedMessageData<webrtc::ObserverInterface*>
    ObserverMessageData;
typedef talk_base::TypedMessageData
    <webrtc::MediaStreamTrackInterface::TrackState> TrackStateMessageData;
typedef talk_base::TypedMessageData<bool> EnableMessageData;
typedef talk_base::TypedMessageData<webrtc::VideoRendererInterface*>
    VideoRendererInterfaceMessageData;
typedef talk_base::TypedMessageData<cricket::VideoRenderer*>
    VideoFrameInputMessageData;

class AudioSourceMessageData : public talk_base::MessageData {
 public:
  talk_base::scoped_refptr<webrtc::AudioSourceInterface> audio_source_;
};

class VideoSourceMessageData : public talk_base::MessageData {
 public:
  talk_base::scoped_refptr<webrtc::VideoSourceInterface> video_source_;
};

}  // namespace anonymous

namespace webrtc {

template <class T>
MediaStreamTrackProxy<T>::MediaStreamTrackProxy(
    T* track, talk_base::Thread* signaling_thread)
    : signaling_thread_(signaling_thread),
      track_(track) {
}

template <class T>
std::string MediaStreamTrackProxy<T>::kind() const {
  return track_->kind();
}

template <class T>
std::string MediaStreamTrackProxy<T>::id() const {
  if (!signaling_thread_->IsCurrent()) {
    std::string label;
    LabelMessageData msg(&label);
    Send(MSG_ID, &msg);
    return label;
  }
  return track_->id();
}

template <class T>
MediaStreamTrackInterface::TrackState MediaStreamTrackProxy<T>::state() const {
  if (!signaling_thread_->IsCurrent()) {
    TrackStateMessageData msg(MediaStreamTrackInterface::kInitializing);
    Send(MSG_STATE, &msg);
    return msg.data();
  }
  return track_->state();
}

template <class T>
bool MediaStreamTrackProxy<T>::enabled() const {
  if (!signaling_thread_->IsCurrent()) {
    EnableMessageData msg(false);
    Send(MSG_ENABLED, &msg);
    return msg.data();
  }
  return track_->enabled();
}

template <class T>
bool MediaStreamTrackProxy<T>::set_enabled(bool enable) {
  if (!signaling_thread_->IsCurrent()) {
    EnableMessageData msg(enable);
    Send(MSG_SET_ENABLED, &msg);
    return msg.data();
  }
  return track_->set_enabled(enable);
}

template <class T>
bool MediaStreamTrackProxy<T>::set_state(
    MediaStreamTrackInterface::TrackState new_state) {
  if (!signaling_thread_->IsCurrent()) {
    // State should only be allowed to be changed from the signaling thread.
    ASSERT(!"Not Allowed!");
    return false;
  }
  return track_->set_state(new_state);
}

template <class T>
void MediaStreamTrackProxy<T>::RegisterObserver(ObserverInterface* observer) {
  if (!signaling_thread_->IsCurrent()) {
    ObserverMessageData msg(observer);
    Send(MSG_REGISTER_OBSERVER, &msg);
    return;
  }
  track_->RegisterObserver(observer);
}

template <class T>
void MediaStreamTrackProxy<T>::UnregisterObserver(ObserverInterface* observer) {
  if (!signaling_thread_->IsCurrent()) {
    ObserverMessageData msg(observer);
    Send(MSG_UNREGISTER_OBSERVER, &msg);
    return;
  }
  track_->UnregisterObserver(observer);
}

template <class T>
void MediaStreamTrackProxy<T>::Send(uint32 id,
                                    talk_base::MessageData* data) const {
  signaling_thread_->Send(const_cast<MediaStreamTrackProxy<T>*>(this), id,
                          data);
}

template <class T>
bool MediaStreamTrackProxy<T>::HandleMessage(talk_base::Message* msg) {
  talk_base::MessageData* data = msg->pdata;
  switch (msg->message_id) {
    case MSG_REGISTER_OBSERVER: {
      ObserverMessageData* observer = static_cast<ObserverMessageData*>(data);
      track_->RegisterObserver(observer->data());
      return true;
      break;
    }
    case MSG_UNREGISTER_OBSERVER: {
      ObserverMessageData* observer = static_cast<ObserverMessageData*>(data);
      track_->UnregisterObserver(observer->data());
      return true;
      break;
    }
    case MSG_ID: {
      LabelMessageData* label = static_cast<LabelMessageData*>(data);
      *(label->data()) = track_->id();
      return true;
    }
    case MSG_SET_ENABLED: {
      EnableMessageData* enabled = static_cast<EnableMessageData*>(data);
      enabled->data() = track_->set_enabled(enabled->data());
      return true;
      break;
    }
    case MSG_ENABLED: {
      EnableMessageData* enabled = static_cast<EnableMessageData*>(data);
      enabled->data() = track_->enabled();
      return true;
      break;
    }
    case MSG_STATE: {
      TrackStateMessageData* state = static_cast<TrackStateMessageData*>(data);
      state->data() = track_->state();
      return true;
      break;
    }
    default:
      return false;
  }
}


AudioTrackProxy::AudioTrackProxy(AudioTrackInterface* track,
                                 talk_base::Thread* signaling_thread)
    : MediaStreamTrackProxy<AudioTrackInterface>(track,
                                                 signaling_thread) {
}

AudioSourceInterface* AudioTrackProxy::GetSource() const {
  if (!signaling_thread_->IsCurrent()) {
    AudioSourceMessageData msg;
    Send(MSG_GET_AUDIOSOURCE, &msg);
    return msg.audio_source_;
  }
  return track_->GetSource();
}

void AudioTrackProxy::OnMessage(talk_base::Message* msg) {
  if (!HandleMessage(msg)) {
    if (msg->message_id == MSG_GET_AUDIOSOURCE) {
      AudioSourceMessageData* audio_source =
          static_cast<AudioSourceMessageData*>(msg->pdata);
      audio_source->audio_source_ = track_->GetSource();
      return;
    }
    ASSERT(!"Not Implemented!");
  }
}

talk_base::scoped_refptr<AudioTrackProxy> AudioTrackProxy::Create(
    AudioTrackInterface* track,
    talk_base::Thread* signaling_thread) {
  talk_base::RefCountedObject<AudioTrackProxy>* proxy =
      new talk_base::RefCountedObject<AudioTrackProxy>(track,
                                                       signaling_thread);
  return proxy;
}

VideoTrackProxy::VideoTrackProxy(VideoTrackInterface* video_track,
                                 talk_base::Thread* signaling_thread)
    : MediaStreamTrackProxy<VideoTrackInterface>(video_track,
                                                 signaling_thread) {
}

void VideoTrackProxy::AddRenderer(VideoRendererInterface* renderer) {
  if (!signaling_thread_->IsCurrent()) {
    VideoRendererInterfaceMessageData msg(renderer);
    Send(MSG_ADD_VIDEORENDERER, &msg);
    return;
  }
  track_->AddRenderer(renderer);
}

void VideoTrackProxy::RemoveRenderer(VideoRendererInterface* renderer) {
  if (!signaling_thread_->IsCurrent()) {
    VideoRendererInterfaceMessageData msg(renderer);
    Send(MSG_REMOVE_VIDEORENDERER, &msg);
    return;
  }
  track_->RemoveRenderer(renderer);
}

cricket::VideoRenderer* VideoTrackProxy::FrameInput() {
  if (!signaling_thread_->IsCurrent()) {
    VideoFrameInputMessageData msg(NULL);
    Send(MSG_GET_VIDEOFRAMEINPUT, &msg);
    return msg.data();
  }
  return track_->FrameInput();
}

VideoSourceInterface* VideoTrackProxy::GetSource() const {
  if (!signaling_thread_->IsCurrent()) {
    VideoSourceMessageData msg;
    Send(MSG_GET_VIDEOSOURCE, &msg);
    return msg.video_source_;
  }
  return track_->GetSource();
}

void VideoTrackProxy::OnMessage(talk_base::Message* msg) {
  if (!MediaStreamTrackProxy<VideoTrackInterface>::HandleMessage(msg)) {
    switch (msg->message_id) {
       case  MSG_GET_VIDEOSOURCE: {
        VideoSourceMessageData* video_source =
            static_cast<VideoSourceMessageData*>(msg->pdata);
        video_source->video_source_ = track_->GetSource();
        break;
      }
      case MSG_ADD_VIDEORENDERER: {
        VideoRendererInterfaceMessageData* renderer =
            static_cast<VideoRendererInterfaceMessageData*>(msg->pdata);
        track_->AddRenderer(renderer->data());
        break;
      }
      case MSG_REMOVE_VIDEORENDERER: {
        VideoRendererInterfaceMessageData* message =
            static_cast<VideoRendererInterfaceMessageData*>(msg->pdata);
        track_->RemoveRenderer(message->data());
        break;
      }
      case MSG_GET_VIDEOFRAMEINPUT: {
        VideoFrameInputMessageData* message =
            static_cast<VideoFrameInputMessageData*>(msg->pdata);
        message->data() = track_->FrameInput();
        break;
      }
    default:
      ASSERT(!"Not Implemented!");
      break;
    }
  }
}

talk_base::scoped_refptr<VideoTrackProxy> VideoTrackProxy::Create(
    VideoTrackInterface* track,
    talk_base::Thread* signaling_thread) {
  talk_base::RefCountedObject<VideoTrackProxy>* proxy =
      new talk_base::RefCountedObject<VideoTrackProxy>(track,
                                                       signaling_thread);
  return proxy;
}

}  // namespace webrtc
