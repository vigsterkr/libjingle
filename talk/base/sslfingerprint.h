/*
 * libjingle
 * Copyright 2012, Google Inc.
 * Copyright 2012, RTFM Inc.
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

#ifndef TALK_BASE_SSLFINGERPRINT_H_
#define TALK_BASE_SSLFINGERPRINT_H_

#include <string>

#include "talk/base/buffer.h"
#include "talk/base/helpers.h"
#include "talk/base/sslidentity.h"

namespace talk_base {

struct SSLFingerprint {
  SSLFingerprint(const std::string& algorithm, const uint8* digest_in,
                 size_t digest_len) : algorithm(algorithm) {
    digest.SetData(digest_in, digest_len);
  }
  SSLFingerprint(const std::string& algorithm,
                 const talk_base::SSLIdentity* identity)
      : algorithm(algorithm) {
    ASSERT(identity != NULL);

    unsigned char digest_val[32];
    bool retval;
    size_t digest_len;

    retval = identity->certificate().ComputeDigest(
        algorithm, digest_val, sizeof(digest_val), &digest_len);
    ASSERT(retval);
    if (retval) {
      digest.SetData(digest_val, digest_len);
    }
  }
  SSLFingerprint(const SSLFingerprint& from)
      : algorithm(from.algorithm), digest(from.digest) {}
  bool operator==(const SSLFingerprint& other) const {
    return algorithm == other.algorithm &&
           digest == other.digest;
  }

  std::string algorithm;
  talk_base::Buffer digest;
};

}  // namespace talk_base

#endif  // TALK_BASE_SSLFINGERPRINT_H_
