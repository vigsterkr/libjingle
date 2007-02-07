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

#ifdef POSIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <cstring>
#include <sstream>

#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/socketaddress.h"

#ifdef WIN32
#undef SetPort
int inet_aton(const char * cp, struct in_addr * inp) {
  inp->s_addr = inet_addr(cp);
  return (inp->s_addr == INADDR_NONE) ? 0 : 1;
}
#endif // WIN32

#ifdef _DEBUG
#define DISABLE_DNS 0
#else // !_DEBUG
#define DISABLE_DNS 0
#endif // !_DEBUG

namespace talk_base {

SocketAddress::SocketAddress() {
  Clear();
}

SocketAddress::SocketAddress(const std::string& hostname, int port, bool use_dns) {
  SetIP(hostname, use_dns);
  SetPort(port);
}

SocketAddress::SocketAddress(uint32 ip, int port) {
  SetIP(ip);
  SetPort(port);
}

SocketAddress::SocketAddress(const SocketAddress& addr) {
  this->operator=(addr);
}

void SocketAddress::Clear() {
  hostname_.clear();
  ip_ = 0;
  port_ = 0;
}

SocketAddress& SocketAddress::operator=(const SocketAddress& addr) {
  hostname_ = addr.hostname_;
  ip_ = addr.ip_;
  port_ = addr.port_;
  return *this;
}

void SocketAddress::SetIP(uint32 ip) {
  hostname_.clear();
  ip_ = ip;
}

bool SocketAddress::SetIP(const std::string& hostname, bool use_dns) {
  hostname_ = hostname;
  ip_ = 0;
  return Resolve(true, use_dns);
}

void SocketAddress::SetResolvedIP(uint32 ip) {
  ip_ = ip;
}

void SocketAddress::SetPort(int port) {
  ASSERT((0 <= port) && (port < 65536));
  port_ = port;
}

uint32 SocketAddress::ip() const {
  return ip_;
}

uint16 SocketAddress::port() const {
  return port_;
}

std::string SocketAddress::IPAsString() const {
  if (!hostname_.empty())
    return hostname_;
  return IPToString(ip_);
}

std::string SocketAddress::PortAsString() const {
  std::ostringstream ost;
  ost << port_;
  return ost.str();
}

std::string SocketAddress::ToString() const {
  std::ostringstream ost;
  ost << IPAsString();
  ost << ":";
  ost << port();
  return ost.str();
}

bool SocketAddress::IsAny() const {
  return (ip_ == 0);
}

bool SocketAddress::IsLocalIP() const {
  return (ip_ >> 24) == 127;
}

bool SocketAddress::IsPrivateIP() const {
  return ((ip_ >> 24) == 127) ||
         ((ip_ >> 24) == 10) ||
         ((ip_ >> 20) == ((172 << 4) | 1)) ||
         ((ip_ >> 16) == ((192 << 8) | 168));
}

bool SocketAddress::IsUnresolved() const {
  return IsAny() && !hostname_.empty();
}

bool SocketAddress::Resolve(bool force, bool use_dns) {
  if (hostname_.empty()) {
    // nothing to resolve
  } else if (!force && !IsAny()) {
    // already resolved
  } else if (uint32 ip = StringToIP(hostname_, use_dns)) {
    ip_ = ip;
  } else {
    return false;
  }
  return true;
}

bool SocketAddress::operator ==(const SocketAddress& addr) const {
  return EqualIPs(addr) && EqualPorts(addr);
}

bool SocketAddress::operator <(const SocketAddress& addr) const {
  if (ip_ < addr.ip_)
    return true;
  else if (addr.ip_ < ip_)
    return false;

  // We only check hostnames if both IPs are zero.  This matches EqualIPs()
  if (addr.ip_ == 0) {
    if (hostname_ < addr.hostname_)
      return true;
    else if (addr.hostname_ < hostname_)
      return false;
  }

  return port_ < addr.port_;
}

bool SocketAddress::EqualIPs(const SocketAddress& addr) const {
  return (ip_ == addr.ip_) && ((ip_ != 0) || (hostname_ == addr.hostname_));
}

bool SocketAddress::EqualPorts(const SocketAddress& addr) const {
  return (port_ == addr.port_);
}

size_t SocketAddress::Hash() const {
  size_t h = 0;
  h ^= ip_;
  h ^= port_ | (port_ << 16);
  return h;
}

size_t SocketAddress::Size_() const {
  return sizeof(ip_) + sizeof(port_);
}

void SocketAddress::Write_(char* buf, int len) const {
  // TODO: Depending on how this is used, we may want/need to write hostname
  ASSERT((size_t)len >= Size_());
  reinterpret_cast<uint32*>(buf)[0] = ip_;
  buf += sizeof(ip_);
  reinterpret_cast<uint16*>(buf)[0] = port_;
}

void SocketAddress::Read_(const char* buf, int len) {
  ASSERT((size_t)len >= Size_());
  ip_ = reinterpret_cast<const uint32*>(buf)[0];
  buf += sizeof(ip_);
  port_ = reinterpret_cast<const uint16*>(buf)[0];
}

void SocketAddress::ToSockAddr(sockaddr_in* saddr) const {
  memset(saddr, 0, sizeof(*saddr));
  saddr->sin_family = AF_INET;
  saddr->sin_port = HostToNetwork16(port_);
  if (0 == ip_) {
    saddr->sin_addr.s_addr = INADDR_ANY;
  } else {
    saddr->sin_addr.s_addr = HostToNetwork32(ip_);
  }
}

void SocketAddress::FromSockAddr(const sockaddr_in& saddr) {
  SetIP(NetworkToHost32(saddr.sin_addr.s_addr));
  SetPort(NetworkToHost16(saddr.sin_port));
}

std::string SocketAddress::IPToString(uint32 ip) {
  std::ostringstream ost;
  ost << ((ip >> 24) & 0xff);
  ost << '.';
  ost << ((ip >> 16) & 0xff);
  ost << '.';
  ost << ((ip >> 8) & 0xff);
  ost << '.';
  ost << ((ip >> 0) & 0xff);
  return ost.str();
}

uint32 SocketAddress::StringToIP(const std::string& hostname, bool use_dns) {
  uint32 ip = 0;
  in_addr addr;
  if (inet_aton(hostname.c_str(), &addr) != 0) {
    ip = NetworkToHost32(addr.s_addr);
  } else if (use_dns) {
    // Note: this is here so we can spot spurious DNS resolutions for a while
    LOG(INFO) << "=== DNS RESOLUTION (" << hostname << ") ===";
#if DISABLE_DNS
    LOG(WARNING) << "*** DNS DISABLED ***";
#if WIN32
    WSASetLastError(WSAHOST_NOT_FOUND);
#endif // WIN32
#endif // DISABLE_DNS
    if (hostent * pHost = gethostbyname(hostname.c_str())) {
      ip = NetworkToHost32(*reinterpret_cast<uint32 *>(pHost->h_addr_list[0]));
    } else {
#if WIN32
      LOG(LS_ERROR) << "gethostbyname error: " << WSAGetLastError();
#else
      LOG(LS_ERROR) << "gethostbyname error: " << strerror(h_errno);
#endif
    }
    LOG(INFO) << hostname << " resolved to " << IPToString(ip);
  }
  return ip;
}

std::string SocketAddress::GetHostname() {
  char hostname[256];
  if (gethostname(hostname, ARRAY_SIZE(hostname)) == 0)
    return hostname;
  return "";
}

bool SocketAddress::GetLocalIPs(std::vector<uint32>& ips) {
  ips.clear();

  const std::string hostname = GetHostname();
  if (hostname.empty())
    return false;

  if (hostent * pHost = gethostbyname(hostname.c_str())) {
    for (size_t i=0; pHost->h_addr_list[i]; ++i) {
      uint32 ip =
        NetworkToHost32(*reinterpret_cast<uint32 *>(pHost->h_addr_list[i]));
      ips.push_back(ip);
    }
    return !ips.empty();
  }
#if WIN32
  LOG(LS_ERROR) << "gethostbyname error: " << WSAGetLastError();
#else
  LOG(LS_ERROR) << "gethostbyname error: " << strerror(h_errno);
#endif
  return false;
}

} // namespace talk_base
