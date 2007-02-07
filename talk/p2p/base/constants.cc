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

#include "talk/p2p/base/constants.h"

namespace cricket {

const std::string NS_EMPTY("");
const std::string NS_GOOGLESESSION("http://www.google.com/session");
#ifdef FEATURE_ENABLE_VOICEMAIL
const std::string NS_GOOGLEVOICEMAIL("http://www.google.com/session/voicemail");
#endif

const buzz::QName QN_SESSION(true, NS_GOOGLESESSION, "session");

const buzz::QName QN_REDIRECT_TARGET(true, NS_GOOGLESESSION, "target");
const buzz::QName QN_REDIRECT_COOKIE(true, NS_GOOGLESESSION, "cookie");
const buzz::QName QN_REDIRECT_REGARDING(true, NS_GOOGLESESSION, "regarding");

#ifdef FEATURE_ENABLE_VOICEMAIL
const buzz::QName QN_VOICEMAIL_REGARDING(true, NS_GOOGLEVOICEMAIL, "regarding");
#endif

const buzz::QName QN_INITIATOR(true, NS_EMPTY, "initiator");

const buzz::QName QN_ADDRESS(true, cricket::NS_EMPTY, "address");
const buzz::QName QN_PORT(true, cricket::NS_EMPTY, "port");
const buzz::QName QN_NETWORK(true, cricket::NS_EMPTY, "network");
const buzz::QName QN_GENERATION(true, cricket::NS_EMPTY, "generation");
const buzz::QName QN_USERNAME(true, cricket::NS_EMPTY, "username");
const buzz::QName QN_PASSWORD(true, cricket::NS_EMPTY, "password");
const buzz::QName QN_PREFERENCE(true, cricket::NS_EMPTY, "preference");
const buzz::QName QN_PROTOCOL(true, cricket::NS_EMPTY, "protocol");

// Legacy transport messages
const buzz::QName kQnLegacyCandidate(true, cricket::NS_GOOGLESESSION, 
                                     "candidate");
}  // namespace cricket
