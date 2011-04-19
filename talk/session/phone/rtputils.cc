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

#include "talk/session/phone/rtputils.h"

namespace cricket {

bool GetRtpPayloadType(const void* data, size_t len, int* value) {
  if (!data || len < kMinRtpPacketLen || !value) return false;
  *value = *(static_cast<const uint8*>(data) + 1) & 0x7F;
  return true;
}

bool GetRtpSeqNum(const void* data, size_t len, int* value) {
  if (!data || len < kMinRtpPacketLen || !value) return false;
  *value = static_cast<int>(
      talk_base::GetBE16(static_cast<const uint8*>(data) + 2));
  return true;
}

bool GetRtpTimestamp(const void* data, size_t len, uint32* value) {
  if (!data || len < kMinRtpPacketLen || !value) return false;
  *value = talk_base::GetBE32(static_cast<const uint8*>(data) + 4);
  return true;
}

bool GetRtpSsrc(const void* data, size_t len, uint32* value) {
  if (!data || len < kMinRtpPacketLen || !value) return false;
  *value = talk_base::GetBE32(static_cast<const uint8*>(data) + 8);
  return true;
}

bool GetRtcpType(const void* data, size_t len, int* value) {
  if (!data || len < kMinRtcpPacketLen || !value) return false;
  *value = static_cast<int>(*(static_cast<const uint8*>(data) + 1));
  return true;
}

}  // namespace cricket

