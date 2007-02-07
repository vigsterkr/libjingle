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

#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/winping.h"
#include "talk/base/win32socketserver.h"
#include "talk/base/win32window.h"
#include <ws2tcpip.h>

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// Win32Socket
///////////////////////////////////////////////////////////////////////////////

static const int kfRead  = 0x0001;
static const int kfWrite = 0x0002;

// Standard MTUs
static const uint16 PACKET_MAXIMUMS[] = {
  65535,    // Theoretical maximum, Hyperchannel
  32000,    // Nothing
  17914,    // 16Mb IBM Token Ring
  8166,     // IEEE 802.4
  //4464,   // IEEE 802.5 (4Mb max)
  4352,     // FDDI
  //2048,   // Wideband Network
  2002,     // IEEE 802.5 (4Mb recommended)
  //1536,   // Expermental Ethernet Networks
  //1500,   // Ethernet, Point-to-Point (default)
  1492,     // IEEE 802.3
  1006,     // SLIP, ARPANET
  //576,    // X.25 Networks
  //544,    // DEC IP Portal
  //512,    // NETBIOS
  508,      // IEEE 802/Source-Rt Bridge, ARCNET
  296,      // Point-to-Point (low delay)
  68,       // Official minimum
  0,        // End of list marker
};

static const uint32 IP_HEADER_SIZE = 20;
static const uint32 ICMP_HEADER_SIZE = 8;

#ifdef DEBUG
LPCSTR WSAErrorToString(int error, LPCSTR *description_result) {
  LPCSTR string = "Unspecified";
  LPCSTR description = "Unspecified description";
  switch (error) {
    case ERROR_SUCCESS:
      string = "SUCCESS";
      description = "Operation succeeded";
      break;
    case WSAEWOULDBLOCK:
      string = "WSAEWOULDBLOCK";
      description = "Using a non-blocking socket, will notify later";
      break;
    case WSAEACCES:
      string = "WSAEACCES";
      description = "Access denied, or sharing violation";
      break;
    case WSAEADDRNOTAVAIL:
      string = "WSAEADDRNOTAVAIL";
      description = "Address is not valid in this context";
      break;
    case WSAENETDOWN:
      string = "WSAENETDOWN";
      description = "Network is down";
      break;
    case WSAENETUNREACH:
      string = "WSAENETUNREACH";
      description = "Network is up, but unreachable";
      break;
    case WSAENETRESET:
      string = "WSANETRESET";
      description = "Connection has been reset due to keep-alive activity";
      break;
    case WSAECONNABORTED:
      string = "WSAECONNABORTED";
      description = "Aborted by host";
      break;
    case WSAECONNRESET:
      string = "WSAECONNRESET";
      description = "Connection reset by host";
      break;
    case WSAETIMEDOUT:
      string = "WSAETIMEDOUT";
      description = "Timed out, host failed to respond";
      break;
    case WSAECONNREFUSED:
      string = "WSAECONNREFUSED";
      description = "Host actively refused connection";
      break;
    case WSAEHOSTDOWN:
      string = "WSAEHOSTDOWN";
      description = "Host is down";
      break;
    case WSAEHOSTUNREACH:
      string = "WSAEHOSTUNREACH";
      description = "Host is unreachable";
      break;
    case WSAHOST_NOT_FOUND:
      string = "WSAHOST_NOT_FOUND";
      description = "No such host is known";
      break;
  }
  if (description_result) {
    *description_result = description;
  }
  return string;
}

void ReportWSAError(LPCSTR context, int error, const sockaddr_in& addr) {
  talk_base::SocketAddress address;
  address.FromSockAddr(addr);
  LPCSTR description_string;
  LPCSTR error_string = WSAErrorToString(error, &description_string);
  LOG(LS_INFO) << context << " = " << error
    << " (" << error_string << ":" << description_string << ") ["
    << address.ToString() << "]";
}
#else
void ReportWSAError(LPCSTR context, int error, const sockaddr_in& addr) { }
#endif

/////////////////////////////////////////////////////////////////////////////
// Win32Socket::EventSink
/////////////////////////////////////////////////////////////////////////////

#define WM_SOCKETNOTIFY  (WM_USER + 50)
#define WM_DNSNOTIFY     (WM_USER + 51)

struct Win32Socket::DnsLookup {
  HANDLE handle;
  uint16 port;
  char buffer[MAXGETHOSTSTRUCT];
};

class Win32Socket::EventSink : public Win32Window {
public:
  EventSink(Win32Socket * parent) : parent_(parent) { }

  void Dispose();

  virtual bool OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                         LRESULT& result);
  virtual void OnFinalMessage(HWND hWnd);

private:
  bool OnSocketNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);
  bool OnDnsNotify(WPARAM wParam, LPARAM lParam, LRESULT& result);

  Win32Socket * parent_;
};

void
Win32Socket::EventSink::Dispose() {
  parent_ = NULL;
  if (::IsWindow(handle())) {
    ::DestroyWindow(handle());
  } else {
    delete this;
  }
}

bool Win32Socket::EventSink::OnMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                       LRESULT& result) {
  switch (uMsg) {
  case WM_SOCKETNOTIFY:
  case WM_TIMER:
    return OnSocketNotify(uMsg, wParam, lParam, result);
  case WM_DNSNOTIFY:
    return OnDnsNotify(wParam, lParam, result);
  }
  return false;
}

bool
Win32Socket::EventSink::OnSocketNotify(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                       LRESULT& result) {
  result = 0;

  // Make sure the socket isn't already closed
  if (!parent_ || (parent_->socket_ == INVALID_SOCKET))
    return true;

  int event = WSAGETSELECTEVENT(lParam);
  int wsa_error = WSAGETSELECTERROR(lParam);

  if (uMsg == WM_TIMER) {
    event = FD_CLOSE;
    wsa_error = WSAETIMEDOUT;
  } else if (event == FD_CLOSE) {
    char ch;
    if (::recv(parent_->socket_, &ch, 1, MSG_PEEK) > 0) {
      parent_->signal_close_ = true;
      return true;
    }
  }

  parent_->OnSocketNotify(event, wsa_error);
  return true;
}

bool
Win32Socket::EventSink::OnDnsNotify(WPARAM wParam, LPARAM lParam,
                                    LRESULT& result) {
  result = 0;

  if (!parent_)
    return true;

  if (!parent_->dns_ ||
     (parent_->dns_->handle != reinterpret_cast<HANDLE>(wParam))) {
    ASSERT(false);  
    return true;
  }

  uint32 ip = 0;
  int error = WSAGETASYNCERROR(lParam);

  if (error == 0) {
    hostent * pHost = reinterpret_cast<hostent *>(parent_->dns_->buffer);
    uint32 net_ip = *reinterpret_cast<uint32 *>(pHost->h_addr_list[0]);
    ip = talk_base::NetworkToHost32(net_ip);
  }

  parent_->OnDnsNotify(ip, error);
  return true;
}

void
Win32Socket::EventSink::OnFinalMessage(HWND hWnd) {
  delete this;
}

/////////////////////////////////////////////////////////////////////////////
// Win32Socket
/////////////////////////////////////////////////////////////////////////////

Win32Socket::Win32Socket()
  : socket_(INVALID_SOCKET), error_(0), state_(CS_CLOSED),
    signal_close_(false), sink_(NULL), dns_(NULL) {
  // TODO: replace addr_ with SocketAddress
  memset(&addr_, 0, sizeof(addr_));
}

Win32Socket::~Win32Socket() {
  Close();
}

int
Win32Socket::Attach(SOCKET s) {
  ASSERT(socket_ == INVALID_SOCKET);
  if (socket_ != INVALID_SOCKET)
    return SOCKET_ERROR;

  ASSERT(s != INVALID_SOCKET);
  if (s == INVALID_SOCKET)
    return SOCKET_ERROR;

  socket_ = s;
  state_ = CS_CONNECTED;

  if (!Create(FD_READ | FD_WRITE | FD_CLOSE))
    return SOCKET_ERROR;

  return 0;
}

void
Win32Socket::SetTimeout(int ms) {
  if (sink_) 
    ::SetTimer(sink_->handle(), 1, ms, 0);
}

talk_base::SocketAddress
Win32Socket::GetLocalAddress() const {
  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int result = ::getsockname(socket_, (sockaddr*)&addr, &addrlen);
  ASSERT(addrlen == sizeof(addr));
  talk_base::SocketAddress address;
  if (result >= 0) {
    address.FromSockAddr(addr);
  } else {
    ASSERT(result >= 0);
  }
  return address;
}

talk_base::SocketAddress 
Win32Socket::GetRemoteAddress() const {
  sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int result = ::getpeername(socket_, (sockaddr*)&addr, &addrlen);
  ASSERT(addrlen == sizeof(addr));
  talk_base::SocketAddress address;
  if (result >= 0) {
    address.FromSockAddr(addr);
  } else {
    ASSERT(errno == ENOTCONN);
  }
  return address;
}

int
Win32Socket::Bind(const talk_base::SocketAddress& addr) {
  ASSERT(socket_ == INVALID_SOCKET);
  if (socket_ != INVALID_SOCKET)
    return SOCKET_ERROR;

  if (!Create(FD_ACCEPT | FD_CLOSE))
    return SOCKET_ERROR;

  sockaddr_in saddr;
  addr.ToSockAddr(&saddr);
  int err = ::bind(socket_, (sockaddr*)&saddr, sizeof(saddr));
  UpdateLastError();
  return err;
}

int
Win32Socket::Connect(const talk_base::SocketAddress& addr) {
  ASSERT(socket_ == INVALID_SOCKET);
  if (socket_ != INVALID_SOCKET)
    return SOCKET_ERROR;

  if (!Create(FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE))
    return SOCKET_ERROR;

  if (!addr.IsUnresolved()) {
    sockaddr_in saddr;
    addr.ToSockAddr(&saddr);

    // now connect
    return DoConnect(saddr);
  }

  LOG_F(LS_INFO) << "async dns lookup (" << addr.IPAsString() << ")";
  DnsLookup * dns = new DnsLookup;
  dns->handle = WSAAsyncGetHostByName(sink_->handle(), WM_DNSNOTIFY,
        addr.IPAsString().c_str(), dns->buffer, sizeof(dns->buffer));

  if (!dns->handle) {
    LOG_F(LS_ERROR) << "WSAAsyncGetHostByName error: " << WSAGetLastError();
    delete dns;
    UpdateLastError();
    Close();
    return SOCKET_ERROR;
  }

  dns->port = addr.port();
  dns_ = dns;
  state_ = CS_CONNECTING;
  return 0;
}

int
Win32Socket::DoConnect(const sockaddr_in& addr) {
  connect_time_ = talk_base::GetMillisecondCount();
  int result = connect(socket_, (SOCKADDR*)&addr, sizeof(addr));
  if (result == SOCKET_ERROR) {
    int code = WSAGetLastError();
    if (code != WSAEWOULDBLOCK) {
      ReportWSAError("WSAAsync:connect", code, addr);
      error_ = code;
      Close();
      return SOCKET_ERROR;
    }
  }
  addr_ = addr;
  state_ = CS_CONNECTING;
  return 0;
}

void
Win32Socket::OnSocketNotify(int event, int error) {
  error_ = error;
  switch (event) {
    case FD_CONNECT:
      if (error != ERROR_SUCCESS) {
        ReportWSAError("WSAAsync:connect notify", error, addr_);
#ifdef DEBUG
        int32 duration = talk_base::TimeDiff(talk_base::GetMillisecondCount(), 
            connect_time_);
        LOG(LS_INFO) << "WSAAsync:connect error (" << duration
                     << " ms), faking close";
#endif
        Close();
        // If you get an error connecting, close doesn't really do anything
        // and it certainly doesn't send back any close notification, but
        // we really only maintain a few states, so it is easiest to get
        // back into a known state by pretending that a close happened, even
        // though the connect event never did occur.
        SignalCloseEvent(this, error);
      } else {
#ifdef DEBUG
        int32 duration = talk_base::TimeDiff(talk_base::GetMillisecondCount(), 
            connect_time_);
        LOG(LS_INFO) << "WSAAsync:connect (" << duration << " ms)";
#endif
        state_ = CS_CONNECTED;
        SignalConnectEvent(this);
      }
      break;

    case FD_ACCEPT:
    case FD_READ:
      if (error != ERROR_SUCCESS) {
        ReportWSAError("WSAAsync:read notify", error, addr_);
        Close();
      } else {
        SignalReadEvent(this);
      }
      break;

    case FD_WRITE:
      if (error != ERROR_SUCCESS) {
        ReportWSAError("WSAAsync:write notify", error, addr_);
        Close();
      } else {
        SignalWriteEvent(this);
      }
      break;

    case FD_CLOSE:
      ReportWSAError("WSAAsync:close notify", error, addr_);
      Close();
      SignalCloseEvent(this, error);
      break;
  }
}

void
Win32Socket::OnDnsNotify(int ip, int error) {
  LOG_F(LS_INFO) << "(" << talk_base::SocketAddress::IPToString(ip)
                 << ", " << error << ")";
  if (error == 0) {
    talk_base::SocketAddress address(ip, dns_->port);
    sockaddr_in addr;
    address.ToSockAddr(&addr);
    error = DoConnect(addr);
  } else {
    Close();
  }

  if (error) {
    error_ = error;
    SignalCloseEvent(this, error_);
  } else {
    delete dns_;
    dns_ = NULL;
  }
}

int
Win32Socket::GetError() const {
  return error_;
}

void
Win32Socket::SetError(int error) {
  error_ = error;
}

Socket::ConnState 
Win32Socket::GetState() const {
  return state_;
}

int 
Win32Socket::SetOption(Option opt, int value) {
  ASSERT(opt == OPT_DONTFRAGMENT);
  value = (value == 0) ? 0 : 1;
  return ::setsockopt(socket_, IPPROTO_IP, IP_DONTFRAGMENT, 
      reinterpret_cast<char*>(&value), sizeof(value));
}

int
Win32Socket::Send(const void *pv, size_t cb) {
  int sent = ::send(socket_, reinterpret_cast<const char *>(pv), (int)cb, 0);
  UpdateLastError();
  return sent;
}

int
Win32Socket::SendTo(const void *pv, size_t cb, 
    const talk_base::SocketAddress& addr) {
  sockaddr_in saddr;
  addr.ToSockAddr(&saddr);
  int sent = ::sendto(socket_, reinterpret_cast<const char *>(pv), (int)cb, 0, 
      (sockaddr*)&saddr, sizeof(saddr));
  UpdateLastError();
  return sent;
}

int 
Win32Socket::Recv(void *pv, size_t cb) {
  int received = ::recv(socket_, (char *)pv, (int)cb, 0);
  UpdateLastError();
  if (signal_close_ && (received > 0)) {
    char ch;
    if (::recv(socket_, &ch, 1, MSG_PEEK) <= 0) {
      signal_close_ = false;
      ::PostMessage(sink_->handle(), WM_SOCKETNOTIFY,
                    WSAMAKESELECTREPLY(FD_CLOSE, 0), 0);
    }
  }
  return received;
}

int
Win32Socket::RecvFrom(void *pv, size_t cb, talk_base::SocketAddress *paddr) {
  sockaddr_in saddr;
  socklen_t cbAddr = sizeof(saddr);
  int received = ::recvfrom(socket_, (char *)pv, (int)cb, 0, (sockaddr*)&saddr,
                            &cbAddr);
  UpdateLastError();
  if (received != SOCKET_ERROR)
    paddr->FromSockAddr(saddr);
  if (signal_close_ && (received > 0)) {
    char ch;
    if (::recv(socket_, &ch, 1, MSG_PEEK) <= 0) {
      signal_close_ = false;
      ::PostMessage(sink_->handle(), WM_SOCKETNOTIFY,
                    WSAMAKESELECTREPLY(FD_CLOSE, 0), 0);
    }
  }
  return received;
}

int 
Win32Socket::Listen(int backlog) {
  int err = ::listen(socket_, backlog);
  UpdateLastError();
  if (err == 0)
    state_ = CS_CONNECTING;
  return err;
}

talk_base::Socket* 
Win32Socket::Accept(talk_base::SocketAddress *paddr) {
  sockaddr_in saddr;
  socklen_t cbAddr = sizeof(saddr);
  SOCKET s = ::accept(socket_, (sockaddr*)&saddr, &cbAddr);
  UpdateLastError();
  if (s == INVALID_SOCKET)
    return NULL;
  if (paddr)
    paddr->FromSockAddr(saddr);
  Win32Socket* socket = new Win32Socket;
  if (0 == socket->Attach(s))
    return socket;
  delete socket;
  return NULL;
}

int 
Win32Socket::Close() {
  int err = 0;
  if (socket_ != INVALID_SOCKET) {
    err = ::closesocket(socket_);
    socket_ = INVALID_SOCKET;
    signal_close_ = false;
    UpdateLastError();
  }
  if (dns_) {
    WSACancelAsyncRequest(dns_->handle);
    delete dns_;
    dns_ = NULL;
  }
  if (sink_) {
    sink_->Dispose();
    sink_ = NULL;
  }
  memset(&addr_, 0, sizeof(addr_));        // no longer connected, zero ip/port
  state_ = CS_CLOSED;
  return err;
}

int 
Win32Socket::EstimateMTU(uint16* mtu) {
  talk_base::SocketAddress addr = GetRemoteAddress();
  if (addr.IsAny()) {
    error_ = ENOTCONN;
    return -1;
  }

  talk_base::WinPing ping;
  if (!ping.IsValid()) {
    error_ = EINVAL; // can't think of a better error ID
    return -1;
  }

  for (int level = 0; PACKET_MAXIMUMS[level + 1] > 0; ++level) {
    int32 size = PACKET_MAXIMUMS[level] - IP_HEADER_SIZE - ICMP_HEADER_SIZE;
    talk_base::WinPing::PingResult result =
      ping.Ping(addr.ip(), size, 0, 1, false);
    if (result == talk_base::WinPing::PING_FAIL) {
      error_ = EINVAL; // can't think of a better error ID
      return -1;
    }
    if (result != talk_base::WinPing::PING_TOO_LARGE) {
      *mtu = PACKET_MAXIMUMS[level];
      return 0;
    }
  }

  ASSERT(false);
  return 0;
}

bool
Win32Socket::Create(long events) {
  ASSERT(NULL == sink_);

  if (INVALID_SOCKET == socket_) {
    socket_ = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, 0);
    if (socket_ == INVALID_SOCKET) {
      UpdateLastError();
      return false;
    }
  }

  // Create window
  sink_ = new EventSink(this);
  sink_->Create(NULL, L"EventSink", 0, 0, 0, 0, 10, 10);

  // start the async select
  if (WSAAsyncSelect(socket_, sink_->handle(), WM_SOCKETNOTIFY, events)
      == SOCKET_ERROR) {
    UpdateLastError();
    Close();
    return false;
  }

  return true;
}

void
Win32Socket::UpdateLastError() {
  error_ = WSAGetLastError();
}

///////////////////////////////////////////////////////////////////////////////
// Win32SocketServer
///////////////////////////////////////////////////////////////////////////////

static UINT s_wm_wakeup_id;

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT wm, WPARAM wp, LPARAM lp);

// A socket server that provides cricket base services on top of a win32 gui thread

Win32SocketServer::Win32SocketServer(MessageQueue *message_queue) {
  if (s_wm_wakeup_id == 0)
    s_wm_wakeup_id = RegisterWindowMessage(L"WM_WAKEUP");
  message_queue_ = message_queue;
  hwnd_ = NULL;
  CreateDummyWindow();
}

Win32SocketServer::~Win32SocketServer() {
  if (hwnd_ != NULL) {
    KillTimer(hwnd_, 1);
    ::DestroyWindow(hwnd_);
  }
}

Socket* Win32SocketServer::CreateSocket(int type) {
  ASSERT(SOCK_STREAM == type);
  return new Win32Socket;
}

AsyncSocket* Win32SocketServer::CreateAsyncSocket(int type) {
  ASSERT(SOCK_STREAM == type);
  return new Win32Socket;
}

bool Win32SocketServer::Wait(int cms, bool process_io) {
  ASSERT(!process_io || (cms == 0));  // Should only be used for Thread::Send, or in Pump, below
  if (cms == -1) {
    MSG msg;
    GetMessage(&msg, NULL, s_wm_wakeup_id, s_wm_wakeup_id);
  } else if (cms != 0) {
    Sleep(cms);
  }
  return true;
}

void Win32SocketServer::WakeUp() {
  // Always post for every wakeup, so there are no
  // critical sections
  if (hwnd_ != NULL)
    PostMessage(hwnd_, s_wm_wakeup_id, 0, 0);
}

void Win32SocketServer::Pump() {
  // Process messages
  Message msg;
  while (message_queue_->Get(&msg, 0))
    message_queue_->Dispatch(&msg);

  // Anything remaining?
  int delay = message_queue_->GetDelay();
  if (delay == -1) {
    KillTimer(hwnd_, 1);
  } else {
    SetTimer(hwnd_, 1, delay, NULL);
  }
}

void Win32SocketServer::CreateDummyWindow()
{
  static bool s_registered;
  if (!s_registered) {
    ::WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbWndExtra = sizeof(this);
    wc.lpszClassName = L"Dummy";
    wc.lpfnWndProc = DummyWndProc;
    ::RegisterClassW(&wc);
    s_registered = true;
  }

  hwnd_ = ::CreateWindowW(L"Dummy", L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
  SetWindowLong(hwnd_, GWL_USERDATA, (LONG)(LONG_PTR)this);
}

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT wm, WPARAM wp, LPARAM lp)
{
  if (wm == s_wm_wakeup_id || (wm == WM_TIMER && wp == 1)) {
    Win32SocketServer *ss = (Win32SocketServer *)(LONG_PTR)GetWindowLong(hwnd, GWL_USERDATA);
    ss->Pump();
    return 0;
  }
  return ::DefWindowProc(hwnd, wm, wp, lp);
}

} // namespace talk_base
