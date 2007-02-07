#ifndef _AUTODETECTPROXY_H_
#define _AUTODETECTPROXY_H_

#include "talk/base/sigslot.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/proxyinfo.h"
#include "talk/base/signalthread.h"
#include "talk/base/cryptstring.h"

namespace buzz { 
class XmppClientSettings; 
}

namespace talk_base { 

///////////////////////////////////////////////////////////////////////////////
// AutoDetectProxy
///////////////////////////////////////////////////////////////////////////////

class AsyncSocket;

class AutoDetectProxy : public SignalThread, public sigslot::has_slots<> {
public:
  AutoDetectProxy(const std::string& user_agent);

  const talk_base::ProxyInfo& proxy() const { return proxy_; }

  void set_server_url(const std::string& url) {
    server_url_ = url;
  }
  void set_proxy(const SocketAddress& proxy) {
    proxy_.type = PROXY_UNKNOWN;
    proxy_.address = proxy;
  }
  void set_auth_info(bool use_auth, const std::string& username,
      const CryptString& password) {
    if (use_auth) {
      proxy_.username = username;
      proxy_.password = password;
    }
  }

protected:
  virtual ~AutoDetectProxy();

  // SignalThread Interface
  virtual void DoWork();
  virtual void OnMessage(Message *msg);

  void Next();
  void Complete(ProxyType type);

  void OnConnectEvent(AsyncSocket * socket);
  void OnReadEvent(AsyncSocket * socket);
  void OnCloseEvent(AsyncSocket * socket, int error);

private:
  std::string agent_, server_url_;
  ProxyInfo proxy_;
  AsyncSocket * socket_;
  int next_;
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif  // _AUTODETECTPROXY_H_
