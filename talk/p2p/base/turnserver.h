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

#ifndef TALK_P2P_BASE_TURNSERVER_H_
#define TALK_P2P_BASE_TURNSERVER_H_

#include <list>
#include <map>
#include <set>
#include <string>

#include "talk/base/messagequeue.h"
#include "talk/base/sigslot.h"
#include "talk/base/socketaddress.h"

namespace talk_base {
class AsyncPacketSocket;
class ByteBuffer;
class PacketSocketFactory;
class Thread;
}

namespace cricket {

class StunMessage;
class TurnMessage;

// The default server port for TURN, as specified in RFC5766.
const int TURN_SERVER_PORT = 3478;

// An interface through which the MD5 credential hash can be retrieved.
class TurnAuthInterface {
 public:
  // Gets HA1 for the specified user and realm.
  // HA1 = MD5(A1) = MD5(username:realm:password).
  // Return true if the given username and realm are valid, or false if not.
  virtual bool GetKey(const std::string& username, const std::string& realm,
                      std::string* key) = 0;
};

// The core TURN server class. Give it a socket to listen on via
// AddInternalServerSocket, and a factory to create external sockets via
// SetExternalSocketFactory, and it's ready to go.
// Not yet wired up: TCP support.
class TurnServer : public sigslot::has_slots<> {
 public:
  explicit TurnServer(talk_base::Thread* thread);
  ~TurnServer();

  // Gets/sets the realm value to use for the server.
  const std::string& realm() const { return realm_; }
  void set_realm(const std::string& realm) { realm_ = realm; }

  // Gets/sets the value for the SOFTWARE attribute for TURN messages.
  const std::string& software() const { return software_; }
  void set_software(const std::string& software) { software_ = software; }

  // Sets the authentication callback; does not take ownership.
  void set_auth_hook(TurnAuthInterface* auth_hook) { auth_hook_ = auth_hook; }

  // Starts listening for packets from internal clients.
  void AddInternalServerSocket(talk_base::AsyncPacketSocket* socket);
  // Specifies the factory to use for creating external sockets.
  void SetExternalSocketFactory(talk_base::PacketSocketFactory* factory,
                                const talk_base::SocketAddress& address);

 private:
  // The protocol used by the client to connect to the server.
  enum ProtocolType {
    TURNPROTO_UNKNOWN,
    TURNPROTO_UDP,
    TURNPROTO_TCP,
    TURNPROTO_SSLTCP
  };
  // Encapsulates the client's connection to the server.
  class Connection {
   public:
    Connection() : proto_(TURNPROTO_UNKNOWN) {}
    Connection(const talk_base::SocketAddress& src,
               const talk_base::SocketAddress& dst, ProtocolType proto);
    const talk_base::SocketAddress& src() const { return src_; }
    const talk_base::SocketAddress& dst() const { return dst_; }
    bool operator==(const Connection& t) const;
    bool operator<(const Connection& t) const;
    std::string ToString() const;

   private:
    talk_base::SocketAddress src_;
    talk_base::SocketAddress dst_;
    ProtocolType proto_;
  };
  class Allocation;
  class Permission;
  class Channel;
  typedef std::map<Connection, Allocation*> AllocationMap;

  void OnInternalPacket(talk_base::AsyncPacketSocket* socket, const char* data,
                        size_t size, const talk_base::SocketAddress& address);
  void HandleStunMessage(const Connection& conn, const char* data, size_t size);
  void HandleBindingRequest(const Connection& conn, const StunMessage* msg);
  void HandleAllocateRequest(const Connection& conn, const TurnMessage* msg,
                             const std::string& key);

  bool GetKey(const StunMessage* msg, std::string* key);
  bool CheckAuthorization(const Connection& conn, const StunMessage* msg,
                          const char* data, size_t size,
                          const std::string& key);
  std::string GenerateNonce() const;
  bool ValidateNonce(const std::string& nonce) const;

  Allocation* FindAllocation(const Connection& conn);
  Allocation* CreateAllocation(const Connection& conn, int proto,
                               const std::string& key);

  void SendErrorResponse(const Connection& conn, const StunMessage* req,
                         int code, const std::string& reason);
  void SendErrorResponseWithRealmAndNonce(const Connection& conn,
                                          const StunMessage* req, int code,
                                          const std::string& reason);
  void SendStun(const Connection& conn, StunMessage* msg);
  void Send(const Connection& conn, const talk_base::ByteBuffer& buf);

  void OnAllocationDestroyed(Allocation* allocation);

  talk_base::Thread* thread_;
  std::string nonce_key_;
  std::string realm_;
  std::string software_;
  TurnAuthInterface* auth_hook_;
  talk_base::scoped_ptr<talk_base::AsyncPacketSocket> server_socket_;
  talk_base::scoped_ptr<talk_base::PacketSocketFactory>
      external_socket_factory_;
  talk_base::SocketAddress external_addr_;
  AllocationMap allocations_;
};

}  // namespace cricket

#endif  // TALK_P2P_BASE_TURNSERVER_H_
