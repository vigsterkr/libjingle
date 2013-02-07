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
 *
 */
#ifndef TALK_APP_WEBRTC_TEST_FAKECONSTRAINTS_H_
#define TALK_APP_WEBRTC_TEST_FAKECONSTRAINTS_H_

#include <string>
#include <vector>

#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/base/stringencode.h"

namespace webrtc {

class FakeConstraints : public webrtc::MediaConstraintsInterface {
 public:
  FakeConstraints() { }
  virtual ~FakeConstraints() { }

  virtual const Constraints& GetMandatory() const {
    return mandatory_;
  }

  virtual const Constraints& GetOptional() const {
    return optional_;
  }

  void AddMandatory(const std::string& key, const std::string& value) {
    mandatory_.push_back(Constraint(key, value));
  }

  void AddOptional(const std::string& key, const std::string& value) {
    optional_.push_back(Constraint(key, value));
  }

  void SetMandatoryMinAspectRatio(double ratio) {
    AddMandatory(MediaConstraintsInterface::kMinAspectRatio,
                 talk_base::ToString<double>(ratio));
  }

  void SetMandatoryMinWidth(int width) {
    AddMandatory(MediaConstraintsInterface::kMinWidth,
                 talk_base::ToString<int>(width));
  }

  void SetMandatoryMinHeight(int height) {
    AddMandatory(MediaConstraintsInterface::kMinHeight,
                 talk_base::ToString<int>(height));
  }

  void SetOptionalMaxWidth(int width) {
    AddOptional(MediaConstraintsInterface::kMaxWidth,
                talk_base::ToString<int>(width));
  }

  void SetMandatoryReceiveAudio(bool enable) {
    if (enable) {
      AddMandatory(MediaConstraintsInterface::kOfferToReceiveAudio,
                   MediaConstraintsInterface::kValueTrue);
    } else {
      AddMandatory(MediaConstraintsInterface::kOfferToReceiveAudio,
                   MediaConstraintsInterface::kValueFalse);
    }
  }

  void SetMandatoryReceiveVideo(bool enable) {
    if (enable) {
      AddMandatory(MediaConstraintsInterface::kOfferToReceiveVideo,
                   MediaConstraintsInterface::kValueTrue);
    } else {
      AddMandatory(MediaConstraintsInterface::kOfferToReceiveVideo,
                   MediaConstraintsInterface::kValueFalse);
    }
  }

  void SetMandatoryUseRtpMux(bool enable) {
    if (enable) {
      AddMandatory(MediaConstraintsInterface::kUseRtpMux,
                   MediaConstraintsInterface::kValueTrue);
    } else {
      AddMandatory(MediaConstraintsInterface::kUseRtpMux,
                   MediaConstraintsInterface::kValueFalse);
    }
  }

  void SetMandatoryIceRestart(bool enable) {
    if (enable) {
      AddMandatory(MediaConstraintsInterface::kIceRestart,
                   MediaConstraintsInterface::kValueTrue);
    } else {
      AddMandatory(MediaConstraintsInterface::kIceRestart,
                   MediaConstraintsInterface::kValueFalse);
    }
  }

  void SetAllowRtpDataChannels() {
    AddMandatory(MediaConstraintsInterface::kEnableRtpDataChannels,
                 MediaConstraintsInterface::kValueTrue);
  }

  bool FindConstraint(const std::string& key, std::string* value,
                      bool* mandatory) {
    if (FindConstraint(mandatory_, key, value)) {
      if (mandatory)
        *mandatory = true;
      return true;
    }

    if (FindConstraint(optional_, key, value)) {
      if (mandatory)
        *mandatory = false;
      return true;
    }
    return false;
  }

 private:
  bool FindConstraint(
      const MediaConstraintsInterface::Constraints& constraints,
      const std::string& key, std::string* value) {
    MediaConstraintsInterface::Constraints::const_iterator iter =
        constraints.begin();
    for (; iter != constraints.end(); ++iter) {
      if (iter->key == key) {
        if (value)
          *value = iter->value;
        return true;
      }
    }
    return false;
  }

  std::vector<Constraint> mandatory_;
  std::vector<Constraint> optional_;
};

}  // namespace webrtc

#endif  // TALK_APP_WEBRTC_TEST_FAKECONSTRAINTS_H_
