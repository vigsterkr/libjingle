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

#ifndef TALK_APP_WEBRTC_DTMFSENDER_H_
#define TALK_APP_WEBRTC_DTMFSENDER_H_

#include <string>

#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/base/messagehandler.h"
#include "talk/base/refcount.h"

// DtmfSender is the native implementation of the RTCDTMFSender defined by
// the WebRTC W3C Editor's Draft.
// http://dev.w3.org/2011/webrtc/editor/webrtc.html

namespace talk_base {
class Thread;
}

namespace webrtc {

// This interface is called by DtmfSender to talk to the actual audio channel
// to send DTMF.
class DtmfProviderInterface {
 public:
  // Returns true if the audio track with given id (|track_id|) is capable
  // of sending DTMF. Otherwise returns false.
  virtual bool CanInsertDtmf(const std::string& track_id) = 0;
  // Sends DTMF |code| via the audio track with given id (|track_id|).
  // The |duration| indicates the length of the DTMF tone in ms.
  // Returns true on success and false on failure.
  virtual bool InsertDtmf(const std::string& track_id,
                          int code, int duration) = 0;

 protected:
  virtual ~DtmfProviderInterface() {}
};

// DtmfSender callback interface. Application should implement this interface
// to get notifications from the DtmfSender.
class DtmfSenderObserverInterface : public talk_base::RefCountInterface {
 public:
  // Triggered when DTMF |tone| is sent.
  // If |tone| is empty that means the DtmfSender has sent out all the given
  // tones.
  virtual void OnToneChange(const std::string& tone) = 0;

 protected:
  virtual ~DtmfSenderObserverInterface() {}
};

// Native implementation of the RTCDTMFSender defined by the WebRTC W3C Editor's
// Draft.
class DtmfSender : public talk_base::MessageHandler {
 public:
  DtmfSender(AudioTrackInterface* track,
             DtmfSenderObserverInterface* observer,
             talk_base::Thread* signaling_thread,
             DtmfProviderInterface* provider);
  virtual ~DtmfSender();

  // Returns true if this DtmfSender is capable of sending DTMF.
  // Otherwise returns false.
  bool CanInsertDtmf();

  // Queues a task that sends the DTMF |tones|. The |tones| parameter is treated
  // as a series of characters. The characters 0 through 9, A through D, #, and
  // * generate the associated DTMF tones. The characters a to d are equivalent
  // to A to D. The character ',' indicates a delay of 2 seconds before
  // processing the next character in the tones parameter.
  // Unrecognized characters are ignored.
  // The |duration| parameter indicates the duration in ms to use for each
  // character passed in the |tones| parameter.
  // The duration cannot be more than 6000 or less than 70.
  // The |inter_tone_gap| parameter indicates the gap between tones in ms.
  // The |inter_tone_gap| must be at least 50 ms but should be as short as
  // possible.
  // If InsertDtmf is called on the same object while an existing task for this
  // object to generate DTMF is still running, the previous task is canceled.
  // Returns true on success and false on failure.
  bool InsertDtmf(const std::string& tones, int duration, int inter_tone_gap);

  // Returns the track given as argument to the constructor.
  const AudioTrackInterface* track() const;

  // Returns the tones remaining to be played out.
  const std::string tones() const;

  // Returns the current tone duration value in ms.
  // This value will be the value last set via the InsertDtmf() method, or the
  // default value of 100 ms if InsertDtmf() was never called.
  int duration() const;

  // Returns the current value of the between-tone gap in ms.
  // This value will be the value last set via the InsertDtmf() method, or the
  // default value of 50 ms if InsertDtmf() was never called.
  int inter_tone_gap() const;

 private:
  DtmfSender();

  // Implements MessageHandler.
  virtual void OnMessage(talk_base::Message* msg);

  // The DTMF sending task.
  void DoInsertDtmf();

  talk_base::scoped_refptr<AudioTrackInterface> track_;
  talk_base::scoped_refptr<DtmfSenderObserverInterface> observer_;
  talk_base::Thread* signaling_thread_;
  DtmfProviderInterface* provider_;
  std::string tones_;
  int duration_;
  int inter_tone_gap_;

  DISALLOW_COPY_AND_ASSIGN(DtmfSender);
};

// Get DTMF code from the DTMF event character.
bool GetDtmfCode(char tone, int* code);

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_DTMFSENDER_H_
