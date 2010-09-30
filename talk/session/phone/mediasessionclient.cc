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

#include "talk/session/phone/mediasessionclient.h"

#include "talk/base/logging.h"
#include "talk/base/stringutils.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/parsing.h"
#include "talk/xmpp/constants.h"
#include "talk/xmllite/qname.h"

using namespace talk_base;

namespace cricket {

MediaSessionClient::MediaSessionClient(
    const buzz::Jid& jid, SessionManager *manager)
    : jid_(jid), session_manager_(manager), focus_call_(NULL),
      channel_manager_(new ChannelManager(session_manager_->worker_thread())) {
  Construct();
}

MediaSessionClient::MediaSessionClient(
    const buzz::Jid& jid, SessionManager *manager,
    MediaEngine* media_engine, DeviceManager* device_manager)
    : jid_(jid), session_manager_(manager), focus_call_(NULL),
      channel_manager_(new ChannelManager(
          media_engine, device_manager, session_manager_->worker_thread())) {
  Construct();
}


void MediaSessionClient::Construct() {
  // Register ourselves as the handler of phone and video sessions.
  session_manager_->AddClient(NS_JINGLE_RTP, this);
  // Forward device notifications.
  SignalDevicesChange.repeat(channel_manager_->SignalDevicesChange);
  // Bring up the channel manager.
  // In previous versions of ChannelManager, this was done automatically
  // in the constructor.
  channel_manager_->Init();
}

MediaSessionClient::~MediaSessionClient() {
  // Destroy all calls
  std::map<uint32, Call *>::iterator it;
  while (calls_.begin() != calls_.end()) {
    std::map<uint32, Call *>::iterator it = calls_.begin();
    DestroyCall((*it).second);
  }

  // Delete channel manager. This will wait for the channels to exit
  delete channel_manager_;

  // Remove ourselves from the client map.
  session_manager_->RemoveClient(NS_JINGLE_RTP);
}

SessionDescription* MediaSessionClient::CreateOffer(bool video, bool set_ssrc) {
  SessionDescription* offer = new SessionDescription();
  AudioContentDescription* audio = new AudioContentDescription();


  AudioCodecs audio_codecs;
  channel_manager_->GetSupportedAudioCodecs(&audio_codecs);
  for (AudioCodecs::const_iterator codec = audio_codecs.begin();
       codec != audio_codecs.end(); ++codec) {
    audio->AddCodec(*codec);
  }
  if (set_ssrc) {
    audio->set_ssrc(0);
  }
  audio->SortCodecs();
  offer->AddContent(CN_AUDIO, NS_JINGLE_RTP, audio);

  // add video codecs, if this is a video call
  if (video) {
    VideoContentDescription* video = new VideoContentDescription();
    VideoCodecs video_codecs;
    channel_manager_->GetSupportedVideoCodecs(&video_codecs);
    for (VideoCodecs::const_iterator codec = video_codecs.begin();
         codec != video_codecs.end(); ++codec) {
      video->AddCodec(*codec);
    }
    if (set_ssrc) {
      video->set_ssrc(0);
    }
    video->SortCodecs();
    offer->AddContent(CN_VIDEO, NS_JINGLE_RTP, video);
  }

  return offer;
}

const ContentInfo* GetFirstMediaContent(const SessionDescription* sdesc,
                                        MediaType media_type) {
  if (sdesc == NULL)
    return NULL;

  const ContentInfos& contents = sdesc->contents();
  for (ContentInfos::const_iterator content = contents.begin();
       content != contents.end(); content++) {
    if (content->type == NS_JINGLE_RTP) {
      const MediaContentDescription* media =
          static_cast<const MediaContentDescription*>(content->description);
      if (media->type() == media_type) {
        return &*content;
      }
    }
  }
  return NULL;
}

const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MEDIA_TYPE_AUDIO);
}

const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MEDIA_TYPE_VIDEO);
}

SessionDescription* MediaSessionClient::CreateAnswer(
    const SessionDescription* offer) {
  // The answer contains the intersection of the codecs in the offer with the
  // codecs we support, ordered by our local preference. As indicated by
  // XEP-0167, we retain the same payload ids from the offer in the answer.
  SessionDescription* accept = new SessionDescription();

  const ContentInfo* audio_content = GetFirstAudioContent(offer);
  if (audio_content) {
    const AudioContentDescription* audio_offer =
        static_cast<const AudioContentDescription*>(audio_content->description);
    AudioContentDescription* audio_accept = new AudioContentDescription();
    AudioCodecs audio_codecs;
    channel_manager_->GetSupportedAudioCodecs(&audio_codecs);
    for (AudioCodecs::const_iterator ours = audio_codecs.begin();
        ours != audio_codecs.end(); ++ours) {
      for (AudioCodecs::const_iterator theirs = audio_offer->codecs().begin();
          theirs != audio_offer->codecs().end(); ++theirs) {
        if (ours->Matches(*theirs)) {
          AudioCodec negotiated(*ours);
          negotiated.id = theirs->id;
          audio_accept->AddCodec(negotiated);
        }
      }
    }

    audio_accept->SortCodecs();
    accept->AddContent(audio_content->name, audio_content->type, audio_accept);
  }

  const ContentInfo* video_content = GetFirstVideoContent(offer);
  if (video_content) {
    const VideoContentDescription* video_offer =
        static_cast<const VideoContentDescription*>(video_content->description);
    VideoContentDescription* video_accept = new VideoContentDescription();
    VideoCodecs video_codecs;
    channel_manager_->GetSupportedVideoCodecs(&video_codecs);
    for (VideoCodecs::const_iterator ours = video_codecs.begin();
        ours != video_codecs.end(); ++ours) {
      for (VideoCodecs::const_iterator theirs = video_offer->codecs().begin();
          theirs != video_offer->codecs().end(); ++theirs) {
        if (ours->Matches(*theirs)) {
          VideoCodec negotiated(*ours);
          negotiated.id = theirs->id;
          video_accept->AddCodec(negotiated);
        }
      }
    }

    video_accept->SortCodecs();
    accept->AddContent(video_content->name, video_content->type, video_accept);
  }

  return accept;
}

Call *MediaSessionClient::CreateCall(bool video, bool mux) {
  Call *call = new Call(this, video, mux);
  calls_[call->id()] = call;
  SignalCallCreate(call);
  return call;
}

void MediaSessionClient::OnSessionCreate(Session *session,
                                         bool received_initiate) {
  if (received_initiate) {
    session->SignalState.connect(this, &MediaSessionClient::OnSessionState);
  }
}

void MediaSessionClient::OnSessionState(BaseSession* base_session,
                                        BaseSession::State state) {
  // MediaSessionClient can only be used with a Session*, so it's
  // safe to cast here.
  Session* session = static_cast<Session*>(base_session);

  if (state == Session::STATE_RECEIVEDINITIATE) {
    // The creation of the call must happen after the session has
    // processed the initiate message because we need the
    // remote_description to know what content names to use in the
    // call.

    // If our accept would have no codecs, then we must reject this call.
    const SessionDescription* offer = session->remote_description();
    const SessionDescription* accept = CreateAnswer(offer);
    const ContentInfo* audio_content = GetFirstAudioContent(accept);
    const ContentInfo* video_content = GetFirstVideoContent(accept);
    const AudioContentDescription* audio_accept = (!audio_content) ? NULL :
        static_cast<const AudioContentDescription*>(audio_content->description);

    // For some reason, we need to create the call even when we
    // reject.
    Call *call = CreateCall(video_content != NULL);
    session_map_[session->id()] = call;
    call->IncomingSession(session, offer);

    if (!audio_accept || audio_accept->codecs().size() == 0) {
      session->Reject(STR_TERMINATE_INCOMPATIBLE_PARAMETERS);
    }
    delete accept;
  }
}

void MediaSessionClient::DestroyCall(Call *call) {
  // Change focus away, signal destruction

  if (call == focus_call_)
    SetFocus(NULL);
  SignalCallDestroy(call);

  // Remove it from calls_ map and delete

  std::map<uint32, Call *>::iterator it = calls_.find(call->id());
  if (it != calls_.end())
    calls_.erase(it);

  delete call;
}

void MediaSessionClient::OnSessionDestroy(Session *session) {
  // Find the call this session is in, remove it

  std::map<std::string, Call *>::iterator it = session_map_.find(session->id());
  ASSERT(it != session_map_.end());
  if (it != session_map_.end()) {
    Call *call = (*it).second;
    session_map_.erase(it);
    call->RemoveSession(session);
  }
}

Call *MediaSessionClient::GetFocus() {
  return focus_call_;
}

void MediaSessionClient::SetFocus(Call *call) {
  Call *old_focus_call = focus_call_;
  if (focus_call_ != call) {
    if (focus_call_ != NULL)
      focus_call_->EnableChannels(false);
    focus_call_ = call;
    if (focus_call_ != NULL)
      focus_call_->EnableChannels(true);
    SignalFocus(focus_call_, old_focus_call);
  }
}

void MediaSessionClient::JoinCalls(Call *call_to_join, Call *call) {
  // Move all sessions from call to call_to_join, delete call.
  // If call_to_join has focus, added sessions should have enabled channels.

  if (focus_call_ == call)
    SetFocus(NULL);
  call_to_join->Join(call, focus_call_ == call_to_join);
  DestroyCall(call);
}

Session *MediaSessionClient::CreateSession(Call *call) {
  const std::string& type = NS_JINGLE_RTP;
  Session *session = session_manager_->CreateSession(jid().Str(), type);
  session_map_[session->id()] = call;
  return session;
}

bool ParseGingleAudioCodec(const buzz::XmlElement* element, AudioCodec* out) {
  int id = GetXmlAttr(element, QN_ID, -1);
  if (id < 0)
    return false;

  std::string name = GetXmlAttr(element, QN_NAME, buzz::STR_EMPTY);
  int clockrate = GetXmlAttr(element, QN_CLOCKRATE, 0);
  int bitrate = GetXmlAttr(element, QN_BITRATE, 0);
  int channels = GetXmlAttr(element, QN_CHANNELS, 1);
  *out = AudioCodec(id, name, clockrate, bitrate, channels, 0);
  return true;
}

bool ParseGingleVideoCodec(const buzz::XmlElement* element, VideoCodec* out) {
  int id = GetXmlAttr(element, QN_ID, -1);
  if (id < 0)
    return false;

  std::string name = GetXmlAttr(element, QN_NAME, buzz::STR_EMPTY);
  int width = GetXmlAttr(element, QN_WIDTH, 0);
  int height = GetXmlAttr(element, QN_HEIGHT, 0);
  int framerate = GetXmlAttr(element, QN_FRAMERATE, 0);

  *out = VideoCodec(id, name, width, height, framerate, 0);
  return true;
}

void ParseGingleSsrc(const buzz::XmlElement* parent_elem,
                     const buzz::QName& name,
                     MediaContentDescription* content) {
  const buzz::XmlElement* ssrc_elem = parent_elem->FirstNamed(name);
  if (ssrc_elem) {
    content->set_ssrc(strtoul(ssrc_elem->BodyText().c_str(), NULL, 10));
  }
}

bool ParseGingleAudioContent(const buzz::XmlElement* content_elem,
                             const ContentDescription** content,
                             ParseError* error) {
  AudioContentDescription* audio = new AudioContentDescription();

  if (content_elem->FirstElement()) {
    for (const buzz::XmlElement* codec_elem =
             content_elem->FirstNamed(QN_GINGLE_AUDIO_PAYLOADTYPE);
         codec_elem != NULL;
         codec_elem = codec_elem->NextNamed(QN_GINGLE_AUDIO_PAYLOADTYPE)) {
      AudioCodec codec;
      if (ParseGingleAudioCodec(codec_elem, &codec)) {
        audio->AddCodec(codec);
      }
    }
  } else {
    // For backward compatibility, we can assume the other client is
    // an old version of Talk if it has no audio payload types at all.
    audio->AddCodec(AudioCodec(103, "ISAC", 16000, -1, 1, 1));
    audio->AddCodec(AudioCodec(0, "PCMU", 8000, 64000, 1, 0));
  }

  ParseGingleSsrc(content_elem, QN_GINGLE_VIDEO_SRCID, audio);

  *content = audio;
  return true;
}


bool ParseGingleVideoContent(const buzz::XmlElement* content_elem,
                             const ContentDescription** content,
                             ParseError* error) {
  VideoContentDescription* video = new VideoContentDescription();

  for (const buzz::XmlElement* codec_elem =
           content_elem->FirstNamed(QN_GINGLE_VIDEO_PAYLOADTYPE);
       codec_elem != NULL;
       codec_elem = codec_elem->NextNamed(QN_GINGLE_VIDEO_PAYLOADTYPE)) {
    VideoCodec codec;
    if (ParseGingleVideoCodec(codec_elem, &codec)) {
      video->AddCodec(codec);
    }
  }

  ParseGingleSsrc(content_elem, QN_GINGLE_VIDEO_SRCID, video);

  *content = video;
  return true;
}

void ParsePayloadTypeParameters(const buzz::XmlElement* element,
                                std::map<std::string, std::string>* paramap) {
  for (const buzz::XmlElement* param = element->FirstNamed(QN_PARAMETER);
       param != NULL; param = param->NextNamed(QN_PARAMETER)) {
    std::string name  = GetXmlAttr(param, QN_PAYLOADTYPE_PARAMETER_NAME, "");
    std::string value = GetXmlAttr(param, QN_PAYLOADTYPE_PARAMETER_VALUE, "");
    if (!name.empty() && !value.empty()) {
      paramap->insert(make_pair(name, value));
    }
  }
}

int FindWithDefault(const std::map<std::string, std::string>& map,
                    const std::string& key, const int def) {
  std::map<std::string, std::string>::const_iterator iter = map.find(key);
  return (iter == map.end()) ? def : atoi(iter->second.c_str());
}

bool ParseJingleAudioCodec(const buzz::XmlElement* elem, AudioCodec* codec) {
  int id = GetXmlAttr(elem, QN_ID, -1);
  if (id < 0)
    return false;

  std::string name = GetXmlAttr(elem, QN_NAME, "");
  int clockrate = GetXmlAttr(elem, QN_CLOCKRATE, 0);
  int channels = GetXmlAttr(elem, QN_CHANNELS, 1);

  std::map<std::string, std::string> paramap;
  ParsePayloadTypeParameters(elem, &paramap);
  int bitrate = FindWithDefault(paramap, PAYLOADTYPE_PARAMETER_BITRATE, 0);

  *codec = AudioCodec(id, name, clockrate, bitrate, channels, 0);
  return true;
}

bool ParseJingleVideoCodec(const buzz::XmlElement* elem, VideoCodec* codec) {
  int id = GetXmlAttr(elem, QN_ID, -1);
  if (id < 0)
    return false;

  std::string name = GetXmlAttr(elem, QN_NAME, "");

  std::map<std::string, std::string> paramap;
  ParsePayloadTypeParameters(elem, &paramap);
  int width = FindWithDefault(paramap, PAYLOADTYPE_PARAMETER_WIDTH, 0);
  int height = FindWithDefault(paramap, PAYLOADTYPE_PARAMETER_HEIGHT, 0);
  int framerate = FindWithDefault(paramap, PAYLOADTYPE_PARAMETER_FRAMERATE, 0);

  *codec = VideoCodec(id, name, width, height, framerate, 0);
  return true;
}

bool ParseJingleAudioContent(const buzz::XmlElement* content_elem,
                             const ContentDescription** content,
                             ParseError* error) {
  AudioContentDescription* audio = new AudioContentDescription();

  for (const buzz::XmlElement* payload_elem =
           content_elem->FirstNamed(QN_JINGLE_RTP_PAYLOADTYPE);
      payload_elem != NULL;
      payload_elem = payload_elem->NextNamed(QN_JINGLE_RTP_PAYLOADTYPE)) {
    AudioCodec codec;
    if (ParseJingleAudioCodec(payload_elem, &codec)) {
      audio->AddCodec(codec);
    }
  }

  // TODO: Figure out how to integrate SSRC into Jingle.
  *content = audio;
  return true;
}

bool ParseJingleVideoContent(const buzz::XmlElement* content_elem,
                             const ContentDescription** content,
                             ParseError* error) {
  VideoContentDescription* video = new VideoContentDescription();

  for (const buzz::XmlElement* payload_elem =
           content_elem->FirstNamed(QN_JINGLE_RTP_PAYLOADTYPE);
      payload_elem != NULL;
      payload_elem = payload_elem->NextNamed(QN_JINGLE_RTP_PAYLOADTYPE)) {
    VideoCodec codec;
    if (ParseJingleVideoCodec(payload_elem, &codec)) {
      video->AddCodec(codec);
    }
  }

  // TODO: Figure out how to integrate SSRC into Jingle.
  *content = video;
  return true;
}

bool MediaSessionClient::ParseContent(SignalingProtocol protocol,
                                     const buzz::XmlElement* content_elem,
                                     const ContentDescription** content,
                                     ParseError* error) {
  if (protocol == PROTOCOL_GINGLE) {
    const std::string& content_type = content_elem->Name().Namespace();
    if (NS_GINGLE_AUDIO == content_type) {
      return ParseGingleAudioContent(content_elem, content, error);
    } else if (NS_GINGLE_VIDEO == content_type) {
      return ParseGingleVideoContent(content_elem, content, error);
    } else {
      return BadParse("Unknown content type: " + content_type, error);
    }
  } else {
    std::string media;
    if (!RequireXmlAttr(content_elem, QN_JINGLE_CONTENT_MEDIA, &media, error))
      return false;

    if (media == JINGLE_CONTENT_MEDIA_AUDIO) {
      return ParseJingleAudioContent(content_elem, content, error);
    } else if (media == JINGLE_CONTENT_MEDIA_VIDEO) {
      return ParseJingleVideoContent(content_elem, content, error);
    } else {
      return BadParse("Unknown media: " + media, error);
    }
  }
}

buzz::XmlElement* CreateGingleAudioCodecElem(const AudioCodec& codec) {
  buzz::XmlElement* payload_type =
      new buzz::XmlElement(QN_GINGLE_AUDIO_PAYLOADTYPE, true);
  AddXmlAttr(payload_type, QN_ID, codec.id);
  payload_type->AddAttr(QN_NAME, codec.name);
  if (codec.clockrate > 0)
    AddXmlAttr(payload_type, QN_CLOCKRATE, codec.clockrate);
  if (codec.bitrate > 0)
    AddXmlAttr(payload_type, QN_BITRATE, codec.bitrate);
  if (codec.channels > 1)
    AddXmlAttr(payload_type, QN_CHANNELS, codec.channels);
  return payload_type;
}

buzz::XmlElement* CreateGingleVideoCodecElem(const VideoCodec& codec) {
  buzz::XmlElement* payload_type =
      new buzz::XmlElement(QN_GINGLE_VIDEO_PAYLOADTYPE, true);
  AddXmlAttr(payload_type, QN_ID, codec.id);
  payload_type->AddAttr(QN_NAME, codec.name);
  AddXmlAttr(payload_type, QN_WIDTH, codec.width);
  AddXmlAttr(payload_type, QN_HEIGHT, codec.height);
  AddXmlAttr(payload_type, QN_FRAMERATE, codec.framerate);
  return payload_type;
}

buzz::XmlElement* CreateGingleSsrcElem(const buzz::QName& name, uint32 ssrc) {
  buzz::XmlElement* elem = new buzz::XmlElement(name, true);
  if (ssrc) {
    SetXmlBody(elem, ssrc);
  }
  return elem;
}

buzz::XmlElement* CreateGingleAudioContentElem(
    const AudioContentDescription* audio) {
  buzz::XmlElement* elem =
      new buzz::XmlElement(QN_GINGLE_AUDIO_CONTENT, true);

  for (AudioCodecs::const_iterator codec = audio->codecs().begin();
       codec != audio->codecs().end(); ++codec) {
    elem->AddElement(CreateGingleAudioCodecElem(*codec));
  }
  if (audio->ssrc_set()) {
    elem->AddElement(CreateGingleSsrcElem(
        QN_GINGLE_AUDIO_SRCID, audio->ssrc()));
  }


  return elem;
}

buzz::XmlElement* CreateGingleVideoContentElem(
    const VideoContentDescription* video) {
  buzz::XmlElement* elem =
      new buzz::XmlElement(QN_GINGLE_VIDEO_CONTENT, true);

  for (VideoCodecs::const_iterator codec = video->codecs().begin();
       codec != video->codecs().end(); ++codec) {
    elem->AddElement(CreateGingleVideoCodecElem(*codec));
  }
  if (video->ssrc_set()) {
    elem->AddElement(CreateGingleSsrcElem(
        QN_GINGLE_VIDEO_SRCID, video->ssrc()));
  }

  return elem;
}

buzz::XmlElement* CreatePayloadTypeParameterElem(
    const std::string& name, int value) {
  buzz::XmlElement* elem = new buzz::XmlElement(QN_PARAMETER);

  elem->AddAttr(QN_PAYLOADTYPE_PARAMETER_NAME, name);
  AddXmlAttr(elem, QN_PAYLOADTYPE_PARAMETER_VALUE, value);

  return elem;
}

buzz::XmlElement* CreateJingleAudioCodecElem(const AudioCodec& codec) {
  buzz::XmlElement* elem = new buzz::XmlElement(QN_JINGLE_RTP_PAYLOADTYPE);

  AddXmlAttr(elem, QN_ID, codec.id);
  elem->AddAttr(QN_NAME, codec.name);
  if (codec.clockrate > 0) {
    AddXmlAttr(elem, QN_CLOCKRATE, codec.clockrate);
  }
  if (codec.bitrate > 0) {
    elem->AddElement(CreatePayloadTypeParameterElem(
        PAYLOADTYPE_PARAMETER_BITRATE, codec.bitrate));
  }
  if (codec.channels > 1) {
    AddXmlAttr(elem, QN_CHANNELS, codec.channels);
  }

  return elem;
}

buzz::XmlElement* CreateJingleVideoCodecElem(const VideoCodec& codec) {
  buzz::XmlElement* elem = new buzz::XmlElement(QN_JINGLE_RTP_PAYLOADTYPE);

  AddXmlAttr(elem, QN_ID, codec.id);
  elem->AddAttr(QN_NAME, codec.name);
  elem->AddElement(CreatePayloadTypeParameterElem(
      PAYLOADTYPE_PARAMETER_WIDTH, codec.width));
  elem->AddElement(CreatePayloadTypeParameterElem(
      PAYLOADTYPE_PARAMETER_HEIGHT, codec.height));
  elem->AddElement(CreatePayloadTypeParameterElem(
      PAYLOADTYPE_PARAMETER_FRAMERATE, codec.framerate));

  return elem;
}

buzz::XmlElement* CreateJingleAudioContentElem(
    const AudioContentDescription* audio) {
  buzz::XmlElement* elem =
      new buzz::XmlElement(QN_JINGLE_RTP_CONTENT, true);

  elem->SetAttr(QN_JINGLE_CONTENT_MEDIA, JINGLE_CONTENT_MEDIA_AUDIO);

  for (AudioCodecs::const_iterator codec = audio->codecs().begin();
       codec != audio->codecs().end(); ++codec) {
    elem->AddElement(CreateJingleAudioCodecElem(*codec));
  }

  // TODO: Figure out how to integrate SSRC into Jingle.
  return elem;
}

buzz::XmlElement* CreateJingleVideoContentElem(
    const VideoContentDescription* video) {
  buzz::XmlElement* elem =
      new buzz::XmlElement(QN_JINGLE_RTP_CONTENT, true);

  elem->SetAttr(QN_JINGLE_CONTENT_MEDIA, JINGLE_CONTENT_MEDIA_VIDEO);

  for (VideoCodecs::const_iterator codec = video->codecs().begin();
       codec != video->codecs().end(); ++codec) {
    elem->AddElement(CreateJingleVideoCodecElem(*codec));
  }

  // TODO: Figure out how to integrate SSRC into Jingle.
  return elem;
}

bool MediaSessionClient::WriteContent(SignalingProtocol protocol,
                                      const ContentDescription* content,
                                      buzz::XmlElement** elem,
                                      WriteError* error) {
  const MediaContentDescription* media =
      static_cast<const MediaContentDescription*>(content);

  if (media->type() == MEDIA_TYPE_AUDIO) {
    const AudioContentDescription* audio =
        static_cast<const AudioContentDescription*>(media);
    if (protocol == PROTOCOL_GINGLE) {
      *elem = CreateGingleAudioContentElem(audio);
    } else {
      *elem = CreateJingleAudioContentElem(audio);
    }
  } else if (media->type() == MEDIA_TYPE_VIDEO) {
    const VideoContentDescription* video =
        static_cast<const VideoContentDescription*>(media);
    if (protocol == PROTOCOL_GINGLE) {
      *elem = CreateGingleVideoContentElem(video);
    } else {
      *elem = CreateJingleVideoContentElem(video);
    }
  } else {
    return BadWrite("Unknown content type: " + media->type(), error);
  }

  return true;
}

}  // namespace cricket
