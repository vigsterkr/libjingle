//
// libjingle
// Copyright 2004--2007, Google Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//  3. The name of the author may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "talk/session/phone/mediaengine.h"

#if defined(HAVE_LINPHONE)
#include "talk/session/phone/linphonemediaengine.h"
#elif defined(HAVE_WEBRTC)
#include "talk/session/phone/webrtcvoiceengine.h"
#include "talk/session/phone/webrtcvideoengine.h"
#if defined(PLATFORM_CHROMIUM)
#include "content/renderer/renderer_webrtc_audio_device_impl.h"
#else  // Other browsers
#endif  // PLATFORM_CHROMIUM
#else
#endif  // HAVE_LINPHONE

namespace cricket {
#if defined(PLATFORM_CHROMIUM)
class ChromiumWebRtcVoiceEngine : public WebRtcVoiceEngine {
 public:
  // TODO: where should we get the AudioDevice initial configuration
  ChromiumWebRtcVoiceEngine() : WebRtcVoiceEngine(
      new RendererWebRtcAudioDeviceImpl(1440, 1440, 1, 1, 48000, 48000)) {}
};
#else  // Other browsers
#endif  // PLATFORM_CHROMIUM

MediaEngine* MediaEngine::Create() {
#if defined(HAVE_LINPHONE)
  return new LinphoneMediaEngine("", "");
#elif defined(HAVE_WEBRTC) && defined(PLATFORM_CHROMIUM)
  return new CompositeMediaEngine<ChromiumWebRtcVoiceEngine,
      WebRtcVideoEngine>();
#elif defined(HAVE_WEBRTC)
  return new CompositeMediaEngine<WebRtcVoiceEngine, WebRtcVideoEngine>();
#elif defined(ANDROID)
  return AndroidMediaEngineFactory::Create();
#else
  return new NullMediaEngine();
#endif  // HAVE_LINPHONE HAVE_WEBRTC PLATFORM_CHROMIUM ANDROID
}

};  // namespace cricket
