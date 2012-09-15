/*
 * libjingle
 * Copyright 2012, Google, Inc.
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

#ifndef TALK_P2P_BASE_DTLSTRANSPORT_H_
#define TALK_P2P_BASE_DTLSTRANSPORT_H_

#include "talk/p2p/base/dtlstransportchannel.h"

namespace talk_base {
class SSLIdentity;
}

namespace cricket {

class PortAllocator;

// Base should be a descendant of cricket::Transport
template<class Base>
class DtlsTransport : public Base {
 public:
  DtlsTransport(talk_base::Thread* signaling_thread,
                talk_base::Thread* worker_thread,
                const std::string& content_name,
                PortAllocator* allocator,
                talk_base::SSLIdentity* identity)
      : Base(signaling_thread, worker_thread, content_name, allocator),
        identity_(identity) {
  }
  ~DtlsTransport() {
    Base::DestroyAllChannels();
  }

  virtual DtlsTransportChannelWrapper* CreateTransportChannel(int component) {
    DtlsTransportChannelWrapper* dtls_channel = new
        DtlsTransportChannelWrapper(this,
                                    Base::CreateTransportChannel(component));
    // Push down the identity, if one exists, to the transport channel.
    if (identity_) {
      bool ret = dtls_channel->SetLocalIdentity(identity_);
      if (!ret) {
        DestroyTransportChannel(dtls_channel);
        dtls_channel = NULL;
      }
    }
    return dtls_channel;
  }
  virtual void DestroyTransportChannel(TransportChannelImpl* channel) {
    // Kind of ugly, but this lets us do the exact inverse of the create.
    DtlsTransportChannelWrapper* dtls_channel =
        static_cast<DtlsTransportChannelWrapper*>(channel);
    TransportChannelImpl* base_channel = dtls_channel->channel();
    delete dtls_channel;
    Base::DestroyTransportChannel(base_channel);
  }

 private:
  talk_base::SSLIdentity *identity_;
};

}  // namespace cricket

#endif  // TALK_P2P_BASE_DTLSTRANSPORT_H_
