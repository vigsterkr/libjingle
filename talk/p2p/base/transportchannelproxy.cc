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

#include "talk/p2p/base/transportchannelproxy.h"
#include "talk/base/common.h"
#include "talk/p2p/base/transport.h"
#include "talk/p2p/base/transportchannelimpl.h"

namespace cricket {

TransportChannelProxy::TransportChannelProxy(const std::string& content_name,
                                             const std::string& name,
                                             int component)
    : TransportChannel(content_name, component),
      name_(name),
      impl_(NULL) {
}

TransportChannelProxy::~TransportChannelProxy() {
  if (impl_)
    impl_->GetTransport()->DestroyChannel(impl_->component());
}

void TransportChannelProxy::SetImplementation(TransportChannelImpl* impl) {
  // Destroy any existing impl_
  if (impl_) {
    impl_->GetTransport()->DestroyChannel(impl_->component());
  }

  impl_ = impl;
  impl_->SignalReadableState.connect(
      this, &TransportChannelProxy::OnReadableState);
  impl_->SignalWritableState.connect(
      this, &TransportChannelProxy::OnWritableState);
  impl_->SignalReadPacket.connect(this, &TransportChannelProxy::OnReadPacket);
  impl_->SignalRouteChange.connect(this, &TransportChannelProxy::OnRouteChange);
  for (OptionList::iterator it = pending_options_.begin();
       it != pending_options_.end();
       ++it) {
    impl_->SetOption(it->first, it->second);
  }
  if (!pending_srtp_ciphers_.empty()) {
    impl_->SetSrtpCiphers(pending_srtp_ciphers_);
  }
  pending_options_.clear();
}

int TransportChannelProxy::SendPacket(const char* data, size_t len, int flags) {
  // Fail if we don't have an impl yet.
  if (!impl_) {
    return -1;
  }
  return impl_->SendPacket(data, len, flags);
}

int TransportChannelProxy::SetOption(talk_base::Socket::Option opt, int value) {
  if (!impl_) {
    pending_options_.push_back(OptionPair(opt, value));
    return 0;
  }
  return impl_->SetOption(opt, value);
}

int TransportChannelProxy::GetError() {
  if (!impl_) {
    return 0;
  }
  return impl_->GetError();
}

bool TransportChannelProxy::GetStats(ConnectionInfos* infos) {
  if (!impl_) {
    return false;
  }
  return impl_->GetStats(infos);
}

bool TransportChannelProxy::IsDtlsActive() const {
  if (!impl_) {
    return false;
  }
  return impl_->IsDtlsActive();
}

bool TransportChannelProxy::SetSrtpCiphers(const std::vector<std::string>&
                                           ciphers) {
  pending_srtp_ciphers_ = ciphers;  // Cache so we can send later, but always
                            // set so it stays consistent.
  if (impl_) {
    return impl_->SetSrtpCiphers(ciphers);
  }
  return true;
}

bool TransportChannelProxy::GetSrtpCipher(std::string* cipher) {
  if (!impl_) {
    return false;
  }
  return impl_->GetSrtpCipher(cipher);
}

bool TransportChannelProxy::ExportKeyingMaterial(const std::string& label,
                                                 const uint8* context,
                                                 size_t context_len,
                                                 bool use_context,
                                                 uint8* result,
                                                 size_t result_len) {
  if (!impl_) {
    return false;
  }
  return impl_->ExportKeyingMaterial(label, context, context_len, use_context,
                                     result, result_len);
}

void TransportChannelProxy::OnReadableState(TransportChannel* channel) {
  ASSERT(channel == impl_);
  set_readable(impl_->readable());
  // Note: SignalReadableState fired by set_readable.
}

void TransportChannelProxy::OnWritableState(TransportChannel* channel) {
  ASSERT(channel == impl_);
  set_writable(impl_->writable());
  // Note: SignalWritableState fired by set_readable.
}

void TransportChannelProxy::OnReadPacket(TransportChannel* channel,
                                         const char* data, size_t size,
                                         int flags) {
  ASSERT(channel == impl_);
  SignalReadPacket(this, data, size, flags);
}

void TransportChannelProxy::OnRouteChange(TransportChannel* channel,
                                          const Candidate& candidate) {
  ASSERT(channel == impl_);
  SignalRouteChange(this, candidate);
}

}  // namespace cricket
