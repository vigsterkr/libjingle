/*
 * libjingle
 * Copyright 2012, The Libjingle Authors.
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

#ifndef TALK_P2P_BASE_TRANSPORTDESCRIPTION_H_
#define TALK_P2P_BASE_TRANSPORTDESCRIPTION_H_

#include "talk/base/sslfingerprint.h"
#include "talk/p2p/base/candidate.h"
#include "talk/p2p/base/constants.h"

namespace cricket {

typedef std::vector<Candidate> Candidates;

struct TransportDescription {
  TransportDescription() {}

  TransportDescription(const std::string& transport_type,
                       const std::string& ice_options,
                       const std::string& ice_ufrag,
                       const std::string& ice_pwd,
                       const talk_base::SSLFingerprint* identity_fingerprint,
                       const Candidates& candidates)
      : transport_type(transport_type),
        ice_options(ice_options),
        ice_ufrag(ice_ufrag),
        ice_pwd(ice_pwd),
        identity_fingerprint(CopyFingerprint(identity_fingerprint)),
        candidates(candidates) {}
  TransportDescription(const std::string& transport_type,
                       const Candidates& candidates)
      : transport_type(transport_type),
        candidates(candidates) {}
  TransportDescription(const TransportDescription& from)
      : transport_type(from.transport_type),
        ice_options(from.ice_options),
        ice_ufrag(from.ice_ufrag),
        ice_pwd(from.ice_pwd),
        identity_fingerprint(CopyFingerprint(from.identity_fingerprint.get())),
        candidates(from.candidates) {}

  void operator=(const TransportDescription& from) {
    // Self-assignment
    if (this == &from)
      return;

    transport_type = from.transport_type;
    ice_options = from.ice_options;
    ice_ufrag = from.ice_ufrag;
    ice_pwd = from.ice_pwd;

    identity_fingerprint.reset(CopyFingerprint(
        from.identity_fingerprint.get()));
    candidates = from.candidates;
  }

  static talk_base::SSLFingerprint* CopyFingerprint(
      const talk_base::SSLFingerprint* from) {
    if (!from)
      return NULL;

    return new talk_base::SSLFingerprint(*from);
  }

  std::string transport_type;  // xmlns of <transport>
  std::string ice_options;
  std::string ice_ufrag;
  std::string ice_pwd;

  talk_base::scoped_ptr<talk_base::SSLFingerprint> identity_fingerprint;
  Candidates candidates;
};

}  // namespace cricket

#endif  // TALK_P2P_BASE_TRANSPORTDESCRIPTION_H_
