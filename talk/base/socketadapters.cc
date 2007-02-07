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

#if defined(_MSC_VER) && _MSC_VER < 1300
#pragma warning(disable:4786)
#endif

#include <time.h>
#include <errno.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define _WINSOCKAPI_
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#endif

#include "talk/base/basicdefs.h"
#include "talk/base/bytebuffer.h"
#include "talk/base/common.h"
#include "talk/base/httpcommon.h"
#include "talk/base/logging.h"
#include "talk/base/socketadapters.h"
#include "talk/base/stringencode.h"
#include "talk/base/stringutils.h"

#ifdef WIN32
#include "talk/base/sec_buffer.h"
#endif // WIN32

namespace talk_base {

BufferedReadAdapter::BufferedReadAdapter(AsyncSocket* socket, size_t buffer_size)
  : AsyncSocketAdapter(socket), buffer_size_(buffer_size), data_len_(0), buffering_(false) {
  buffer_ = new char[buffer_size_];
}

BufferedReadAdapter::~BufferedReadAdapter() {
  delete [] buffer_;
}

int BufferedReadAdapter::Send(const void *pv, size_t cb) {
  if (buffering_) {
    // TODO: Spoof error better; Signal Writeable
    socket_->SetError(EWOULDBLOCK);
    return -1;
  }
  return AsyncSocketAdapter::Send(pv, cb);
}

int BufferedReadAdapter::Recv(void *pv, size_t cb) {
  if (buffering_) {
    socket_->SetError(EWOULDBLOCK);
    return -1;
  }

  size_t read = 0;

  if (data_len_) {
    read = _min(cb, data_len_);
    memcpy(pv, buffer_, read);
    data_len_ -= read;
    if (data_len_ > 0) {
      memmove(buffer_, buffer_ + read, data_len_);
    }
    pv = static_cast<char *>(pv) + read;
    cb -= read;
  }

  // FIX: If cb == 0, we won't generate another read event

  int res = AsyncSocketAdapter::Recv(pv, cb);
  if (res < 0)
    return res;

  return res + static_cast<int>(read);
}

void BufferedReadAdapter::BufferInput(bool on) {
  buffering_ = on;
}

void BufferedReadAdapter::OnReadEvent(AsyncSocket * socket) {
  ASSERT(socket == socket_);

  if (!buffering_) {
    AsyncSocketAdapter::OnReadEvent(socket);
    return;
  }

  if (data_len_ >= buffer_size_) {
    LOG(INFO) << "Input buffer overflow";
    ASSERT(false);
    data_len_ = 0;
  }

  int len = socket_->Recv(buffer_ + data_len_, buffer_size_ - data_len_);
  if (len < 0) {
    // TODO: Do something better like forwarding the error to the user.
    LOG(INFO) << "Recv: " << errno << " " <<  std::strerror(errno);
    return;
  }

  data_len_ += len;

  ProcessInput(buffer_, data_len_);
}

///////////////////////////////////////////////////////////////////////////////

const uint8 SSL_SERVER_HELLO[] = {
  22,3,1,0,74,2,0,0,70,3,1,66,133,69,167,39,169,93,160,
  179,197,231,83,218,72,43,63,198,90,202,137,193,88,82,
  161,120,60,91,23,70,0,133,63,32,14,211,6,114,91,91,
  27,95,21,172,19,249,136,83,157,155,232,61,123,12,48,
  50,110,56,77,162,117,87,65,108,52,92,0,4,0
};

const char SSL_CLIENT_HELLO[] = {
  -128,70,1,3,1,0,45,0,0,0,16,1,0,-128,3,0,-128,7,0,-64,6,0,64,2,0,
  -128,4,0,-128,0,0,4,0,-2,-1,0,0,10,0,-2,-2,0,0,9,0,0,100,0,0,98,0,
  0,3,0,0,6,31,23,12,-90,47,0,120,-4,70,85,46,-79,-125,57,-15,-22
};

AsyncSSLSocket::AsyncSSLSocket(AsyncSocket* socket) : BufferedReadAdapter(socket, 1024) {
}

int AsyncSSLSocket::Connect(const SocketAddress& addr) {
  // Begin buffering before we connect, so that there isn't a race condition between
  // potential senders and receiving the OnConnectEvent signal
  BufferInput(true);
  return BufferedReadAdapter::Connect(addr);
}

void AsyncSSLSocket::OnConnectEvent(AsyncSocket * socket) {
  ASSERT(socket == socket_);

  // TODO: we could buffer output too...
  int res = DirectSend(SSL_CLIENT_HELLO, sizeof(SSL_CLIENT_HELLO));
  ASSERT(res == sizeof(SSL_CLIENT_HELLO));
}

void AsyncSSLSocket::ProcessInput(char * data, size_t& len) {
  if (len < sizeof(SSL_SERVER_HELLO))
    return;

  if (memcmp(SSL_SERVER_HELLO, data, sizeof(SSL_SERVER_HELLO)) != 0) {
    Close();
    SignalCloseEvent(this, 0); // TODO: error code?
    return;
  }

  len -= sizeof(SSL_SERVER_HELLO);
  if (len > 0) {
    memmove(data, data + sizeof(SSL_SERVER_HELLO), len);
  }

  bool remainder = (len > 0);
  BufferInput(false);
  SignalConnectEvent(this);

  // FIX: if SignalConnect causes the socket to be destroyed, we are in trouble
  if (remainder)
    SignalReadEvent(this);
}

///////////////////////////////////////////////////////////////////////////////

AsyncHttpsProxySocket::AsyncHttpsProxySocket(AsyncSocket* socket,
                                             const std::string& user_agent,
                                             const SocketAddress& proxy,
                                             const std::string& username,
                                             const CryptString& password)
  : BufferedReadAdapter(socket, 1024), proxy_(proxy), agent_(user_agent), 
    user_(username), pass_(password), state_(PS_ERROR), context_(0) {
}

AsyncHttpsProxySocket::~AsyncHttpsProxySocket() {
  delete context_;
}

int AsyncHttpsProxySocket::Connect(const SocketAddress& addr) {
  LOG(LS_VERBOSE) << "AsyncHttpsProxySocket::Connect("
                  << proxy_.ToString() << ")";
  dest_ = addr;
  if (dest_.port() != 80) {
    BufferInput(true);
  }
  return BufferedReadAdapter::Connect(proxy_);
}

SocketAddress AsyncHttpsProxySocket::GetRemoteAddress() const {
  return dest_;
}

int AsyncHttpsProxySocket::Close() {
  headers_.clear();
  state_ = PS_ERROR;
  delete context_;
  context_ = 0;
  return BufferedReadAdapter::Close();
}

void AsyncHttpsProxySocket::OnConnectEvent(AsyncSocket * socket) {
  LOG(LS_VERBOSE) << "AsyncHttpsProxySocket::OnConnectEvent";
  // TODO: Decide whether tunneling or not should be explicitly set,
  // or indicated by destination port (as below)
  if (dest_.port() == 80) {
    state_ = PS_TUNNEL;
    BufferedReadAdapter::OnConnectEvent(socket);
    return;
  }
  SendRequest();
}

void AsyncHttpsProxySocket::OnCloseEvent(AsyncSocket * socket, int err) {
  LOG(LS_VERBOSE) << "AsyncHttpsProxySocket::OnCloseEvent(" << err << ")";
  if ((state_ == PS_WAIT_CLOSE) && (err == 0)) {
    state_ = PS_ERROR;
    Connect(dest_);
  } else {
    BufferedReadAdapter::OnCloseEvent(socket, err);
  }
}

void AsyncHttpsProxySocket::ProcessInput(char * data, size_t& len) {
  size_t start = 0;
  for (size_t pos = start; (state_ < PS_TUNNEL) && (pos < len); ) {
    if (state_ == PS_SKIP_BODY) {
      size_t consume = _min(len - pos, content_length_);
      pos += consume;
      start = pos;
      content_length_ -= consume;
      if (content_length_ == 0) {
        EndResponse();
      }
      continue;
    }

    if (data[pos++] != '\n')
      continue;

    size_t len = pos - start - 1;
    if ((len > 0) && (data[start + len - 1] == '\r'))
      --len;

    data[start + len] = 0;
    ProcessLine(data + start, len);
    start = pos;
  }

  len -= start;
  if (len > 0) {
    memmove(data, data + start, len);
  }

  if (state_ != PS_TUNNEL)
    return;

  bool remainder = (len > 0);
  BufferInput(false);
  SignalConnectEvent(this);

  // FIX: if SignalConnect causes the socket to be destroyed, we are in trouble
  if (remainder)
    SignalReadEvent(this); // TODO: signal this??
}

void AsyncHttpsProxySocket::SendRequest() {
  std::stringstream ss;
  ss << "CONNECT " << dest_.ToString() << " HTTP/1.0\r\n";
  ss << "User-Agent: " << agent_ << "\r\n";
  ss << "Host: " << dest_.IPAsString() << "\r\n";
  ss << "Content-Length: 0\r\n";
  ss << "Proxy-Connection: Keep-Alive\r\n";
  ss << headers_;
  ss << "\r\n";
  std::string str = ss.str();
  DirectSend(str.c_str(), str.size());
  state_ = PS_LEADER;
  expect_close_ = true;
  content_length_ = 0;
  headers_.clear();

  LOG(LS_VERBOSE) << "AsyncHttpsProxySocket >> " << str;
}

void AsyncHttpsProxySocket::ProcessLine(char * data, size_t len) {
  LOG(LS_VERBOSE) << "AsyncHttpsProxySocket << " << data;

  if (len == 0) {
    if (state_ == PS_TUNNEL_HEADERS) {
      state_ = PS_TUNNEL;
    } else if (state_ == PS_ERROR_HEADERS) {
      Error(defer_error_);
      return;
    } else if (state_ == PS_SKIP_HEADERS) {
      if (content_length_) {
        state_ = PS_SKIP_BODY;
      } else {
        EndResponse();
        return;
      }
    } else {
      static bool report = false;
      if (!unknown_mechanisms_.empty() && !report) {
        report = true;
        std::string msg(
          "Unable to connect to the Google Talk service due to an incompatibility "
          "with your proxy.\r\nPlease help us resolve this issue by submitting the "
          "following information to us using our technical issue submission form "
          "at:\r\n\r\n"
          "http://www.google.com/support/talk/bin/request.py\r\n\r\n"
          "We apologize for the inconvenience.\r\n\r\n"
          "Information to submit to Google: "
          );
        //std::string msg("Please report the following information to foo@bar.com:\r\nUnknown methods: ");
        msg.append(unknown_mechanisms_);
#ifdef WIN32
        MessageBoxA(0, msg.c_str(), "Oops!", MB_OK);
#endif
#ifdef POSIX
        //TODO: Raise a signal or something so the UI can be separated.
        LOG(LS_ERROR) << "Oops!\n\n" << msg;
#endif
      }
      // Unexpected end of headers
      Error(0);
      return;
    }
  } else if (state_ == PS_LEADER) {
    uint32 code;
    if (sscanf(data, "HTTP/%*lu.%*lu %lu", &code) != 1) {
      Error(0);
      return;
    }
    switch (code) {
    case 200:
      // connection good!
      state_ = PS_TUNNEL_HEADERS;
      return;
#if defined(HTTP_STATUS_PROXY_AUTH_REQ) && (HTTP_STATUS_PROXY_AUTH_REQ != 407)
#error Wrong code for HTTP_STATUS_PROXY_AUTH_REQ
#endif
    case 407: // HTTP_STATUS_PROXY_AUTH_REQ
      state_ = PS_AUTHENTICATE;
      return;
    default:
      defer_error_ = 0;
      state_ = PS_ERROR_HEADERS;
      return;
    }
  } else if ((state_ == PS_AUTHENTICATE)
             && (_strnicmp(data, "Proxy-Authenticate:", 19) == 0)) {
    std::string response, auth_method;
    switch (HttpAuthenticate(data + 19, len - 19,
                             proxy_, "CONNECT", "/",
                             user_, pass_, context_, response, auth_method)) {
    case HAR_IGNORE:
      LOG(LS_VERBOSE) << "Ignoring Proxy-Authenticate: " << auth_method;
      if (!unknown_mechanisms_.empty())
        unknown_mechanisms_.append(", ");
      unknown_mechanisms_.append(auth_method);
      break;
    case HAR_RESPONSE:
      headers_ = "Proxy-Authorization: ";
      headers_.append(response);
      headers_.append("\r\n");
      state_ = PS_SKIP_HEADERS;
      unknown_mechanisms_.clear();
      break;
    case HAR_CREDENTIALS:
      defer_error_ = SOCKET_EACCES;
      state_ = PS_ERROR_HEADERS;
      unknown_mechanisms_.clear();
      break;
    case HAR_ERROR:
      defer_error_ = 0;
      state_ = PS_ERROR_HEADERS;
      unknown_mechanisms_.clear();
      break;
    }
  } else if (_strnicmp(data, "Content-Length:", 15) == 0) {
    content_length_ = strtoul(data + 15, 0, 0);
  } else if (_strnicmp(data, "Proxy-Connection: Keep-Alive", 28) == 0) {
    expect_close_ = false;
    /*
  } else if (_strnicmp(data, "Connection: close", 17) == 0) {
    expect_close_ = true;
    */
  }
}

void AsyncHttpsProxySocket::EndResponse() {
  if (!expect_close_) {
    SendRequest();
    return;
  }

  // No point in waiting for the server to close... let's close now
  // TODO: Refactor out PS_WAIT_CLOSE
  state_ = PS_WAIT_CLOSE;
  BufferedReadAdapter::Close();
  OnCloseEvent(this, 0);
}

void AsyncHttpsProxySocket::Error(int error) {
  BufferInput(false);
  Close();
  SetError(error);
  SignalCloseEvent(this, error);
}

///////////////////////////////////////////////////////////////////////////////

AsyncSocksProxySocket::AsyncSocksProxySocket(AsyncSocket* socket, const SocketAddress& proxy,
                                   const std::string& username, const CryptString& password)
  : BufferedReadAdapter(socket, 1024), proxy_(proxy), user_(username), pass_(password),
    state_(SS_ERROR) {
}

int AsyncSocksProxySocket::Connect(const SocketAddress& addr) {
  dest_ = addr;
  BufferInput(true);
  return BufferedReadAdapter::Connect(proxy_);
}

SocketAddress AsyncSocksProxySocket::GetRemoteAddress() const {
  return dest_;
}

void AsyncSocksProxySocket::OnConnectEvent(AsyncSocket * socket) {
  SendHello();
}

void AsyncSocksProxySocket::ProcessInput(char * data, size_t& len) {
  ASSERT(state_ < SS_TUNNEL);

  ByteBuffer response(data, len);

  if (state_ == SS_HELLO) {
    uint8 ver, method;
    if (!response.ReadUInt8(ver) ||
        !response.ReadUInt8(method))
      return;

    if (ver != 5) {
      Error(0);
      return;
    }

    if (method == 0) {
      SendConnect();
    } else if (method == 2) {
      SendAuth();
    } else {
      Error(0);
      return;
    }
  } else if (state_ == SS_AUTH) {
    uint8 ver, status;
    if (!response.ReadUInt8(ver) ||
        !response.ReadUInt8(status))
      return;

    if ((ver != 1) || (status != 0)) {
      Error(SOCKET_EACCES);
      return;
    }

    SendConnect();
  } else if (state_ == SS_CONNECT) {
    uint8 ver, rep, rsv, atyp;
    if (!response.ReadUInt8(ver) ||
        !response.ReadUInt8(rep) ||
        !response.ReadUInt8(rsv) ||
        !response.ReadUInt8(atyp))
      return;

    if ((ver != 5) || (rep != 0)) {
      Error(0);
      return;
    }

    uint16 port;
    if (atyp == 1) {
      uint32 addr;
      if (!response.ReadUInt32(addr) ||
          !response.ReadUInt16(port))
        return;
      LOG(LS_VERBOSE) << "Bound on " << addr << ":" << port;
    } else if (atyp == 3) {
      uint8 len;
      std::string addr;
      if (!response.ReadUInt8(len) ||
          !response.ReadString(addr, len) ||
          !response.ReadUInt16(port))
        return;
      LOG(LS_VERBOSE) << "Bound on " << addr << ":" << port;
    } else if (atyp == 4) {
      std::string addr;
      if (!response.ReadString(addr, 16) ||
          !response.ReadUInt16(port))
        return;
      LOG(LS_VERBOSE) << "Bound on <IPV6>:" << port;
    } else {
      Error(0);
      return;
    }

    state_ = SS_TUNNEL;
  }

  // Consume parsed data
  len = response.Length();
  memcpy(data, response.Data(), len);

  if (state_ != SS_TUNNEL)
    return;

  bool remainder = (len > 0);
  BufferInput(false);
  SignalConnectEvent(this);

  // FIX: if SignalConnect causes the socket to be destroyed, we are in trouble
  if (remainder)
    SignalReadEvent(this); // TODO: signal this??
}

void AsyncSocksProxySocket::SendHello() {
  ByteBuffer request;
  request.WriteUInt8(5);   // Socks Version
  if (user_.empty()) {
    request.WriteUInt8(1); // Authentication Mechanisms
    request.WriteUInt8(0); // No authentication
  } else {
    request.WriteUInt8(2); // Authentication Mechanisms
    request.WriteUInt8(0); // No authentication
    request.WriteUInt8(2); // Username/Password
  }
  DirectSend(request.Data(), request.Length());
  state_ = SS_HELLO;
}

void AsyncSocksProxySocket::SendAuth() {
  ByteBuffer request;
  request.WriteUInt8(1);      // Negotiation Version
  request.WriteUInt8(static_cast<uint8>(user_.size()));
  request.WriteString(user_); // Username
  request.WriteUInt8(static_cast<uint8>(pass_.GetLength()));
  size_t len = pass_.GetLength() + 1;
  char * sensitive = new char[len];
  pass_.CopyTo(sensitive, true);
  request.WriteString(sensitive); // Password
  memset(sensitive, 0, len);
  delete [] sensitive;
  DirectSend(request.Data(), request.Length());
  state_ = SS_AUTH;
}

void AsyncSocksProxySocket::SendConnect() {
  ByteBuffer request;
  request.WriteUInt8(5);             // Socks Version
  request.WriteUInt8(1);             // CONNECT
  request.WriteUInt8(0);             // Reserved
  if (dest_.IsUnresolved()) {
    std::string hostname = dest_.IPAsString();
    request.WriteUInt8(3);           // DOMAINNAME
    request.WriteUInt8(static_cast<uint8>(hostname.size()));
    request.WriteString(hostname);   // Destination Hostname
  } else {
    request.WriteUInt8(1);           // IPV4
    request.WriteUInt32(dest_.ip()); // Destination IP
  }
  request.WriteUInt16(dest_.port()); // Destination Port
  DirectSend(request.Data(), request.Length());
  state_ = SS_CONNECT;
}

void AsyncSocksProxySocket::Error(int error) {
  state_ = SS_ERROR;
  BufferInput(false);
  Close();
  SetError(SOCKET_EACCES);
  SignalCloseEvent(this, error);
}

///////////////////////////////////////////////////////////////////////////////

LoggingSocketAdapter::LoggingSocketAdapter(AsyncSocket* socket,
                                           LoggingSeverity level,
                                           const char * label, bool hex_mode)
: AsyncSocketAdapter(socket), level_(level), hex_mode_(hex_mode)
{
  label_.append("[");
  label_.append(label);
  label_.append("]");
}

int
LoggingSocketAdapter::Send(const void *pv, size_t cb) {
  int res = AsyncSocketAdapter::Send(pv, cb);
  if (res > 0)
    LogMultiline(level_, label_.c_str(), false,
                 static_cast<const char *>(pv), res, hex_mode_, &lms_);
  return res;
}

int
LoggingSocketAdapter::SendTo(const void *pv, size_t cb,
                             const SocketAddress& addr) {
  int res = AsyncSocketAdapter::SendTo(pv, cb, addr);
  if (res > 0)
    LogMultiline(level_, label_.c_str(), false,
                 static_cast<const char *>(pv), res, hex_mode_, &lms_);
  return res;
}

int
LoggingSocketAdapter::Recv(void *pv, size_t cb) {
  int res = AsyncSocketAdapter::Recv(pv, cb);
  if (res > 0)
    LogMultiline(level_, label_.c_str(), true,
                 static_cast<const char *>(pv), res, hex_mode_, &lms_);
  return res;
}

int
LoggingSocketAdapter::RecvFrom(void *pv, size_t cb, SocketAddress *paddr) {
  int res = AsyncSocketAdapter::RecvFrom(pv, cb, paddr);
  if (res > 0)
    LogMultiline(level_, label_.c_str(), true,
                 static_cast<const char *>(pv), res, hex_mode_, &lms_);
  return res;
}

void
LoggingSocketAdapter::OnConnectEvent(AsyncSocket * socket) {
  LOG_V(level_) << label_ << " Connected";
  AsyncSocketAdapter::OnConnectEvent(socket);
}

void
LoggingSocketAdapter::OnCloseEvent(AsyncSocket * socket, int err) {
  LOG_V(level_) << label_ << " Closed with error: " << err;
  AsyncSocketAdapter::OnCloseEvent(socket, err);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace talk_base
