/* libjingle
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

#include "talk/app/webrtc/jsepsessiondescription.h"

#include "talk/app/webrtc/webrtcsdp.h"
#include "talk/p2p/base/sessiondescription.h"

namespace webrtc {

JsepSessionDescription::JsepSessionDescription()
    : const_description_(NULL) {
}

JsepSessionDescription::~JsepSessionDescription() {
}

void JsepSessionDescription::SetDescription(
    cricket::SessionDescription* description) {
  description_.reset(description);
  const_description_ = description_.get();
}

void JsepSessionDescription::SetConstDescription(
    const cricket::SessionDescription* description) {
  description_.reset(NULL);
  const_description_ = description_.get();
}

bool JsepSessionDescription::Initialize(const std::string& sdp) {
  if (description_.get() != NULL)
    return false;
  description_.reset(new cricket::SessionDescription());
  const_description_ = description_.get();
  return SdpDeserialize(sdp, description_.get(), &candidates_);
}

cricket::SessionDescription* JsepSessionDescription::ReleaseDescription() {
  return description_.release();
}

void JsepSessionDescription::AddCandidate(
    const IceCandidateInterface* candidate) {
  if (candidate)
    candidates_.push_back(candidate->candidate());
}

bool JsepSessionDescription::ToString(std::string* out) const {
  if (!const_description_ || !out)
    return false;
  *out = SdpSerialize(*const_description_, candidates_);
  return !out->empty();
}

}  // namespace webrtc

