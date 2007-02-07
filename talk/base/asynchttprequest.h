#ifndef _ASYNCHTTPREQUEST_H_
#define _ASYNCHTTPREQUEST_H_

#include "talk/base/httpclient.h"
#include "talk/base/logging.h"
#include "talk/base/proxyinfo.h"
#include "talk/base/socketserver.h"
#include "talk/base/thread.h"
#include "talk/base/signalthread.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// AsyncHttpRequest
// Performs an HTTP request on a background thread.  Notifies on the foreground
// thread once the request is done (successfully or unsuccessfully).
///////////////////////////////////////////////////////////////////////////////

class FirewallManager;
class MemoryStream;

class AsyncHttpRequest:
  public SignalThread,
  public sigslot::has_slots<> {
public:
  AsyncHttpRequest(const std::string &user_agent);

  void set_proxy(const talk_base::ProxyInfo& proxy) {
    proxy_ = proxy;
  }
  void set_firewall(talk_base::FirewallManager * firewall) {
    firewall_ = firewall;
  }

  // The DNS name of the host to connect to.
  const std::string& host() { return host_; }
  void set_host(const std::string& host) { host_ = host; }

  // The port to connect to on the target host.
  int port() { return port_; }
  void set_port(int port) { port_ = port; }
       
   // Whether the request should use SSL.
  bool secure() { return secure_; }
  void set_secure(bool secure) { secure_ = secure; }

  // Returns the redirect when redirection occurs
  const std::string& response_redirect() { return response_redirect_; }

  // Time to wait on the download, in ms.  Default is 5000 (5s)
  int timeout() { return timeout_; }
  void set_timeout(int timeout) { timeout_ = timeout; }

  // Fail redirects to allow analysis of redirect urls, etc.
  bool fail_redirect() const { return fail_redirect_; }
  void set_fail_redirect(bool fail_redirect) { fail_redirect_ = fail_redirect; }

  HttpRequestData& request() { return client_.request(); }
  HttpResponseData& response() { return client_.response(); }
   
private:
  // SignalThread Interface
  virtual void DoWork();
  
  talk_base::ProxyInfo proxy_;
  talk_base::FirewallManager * firewall_;
  std::string host_;
  int port_;
  bool secure_;
  int timeout_;
  bool fail_redirect_;
  HttpClient client_;
  std::string response_redirect_;
};

///////////////////////////////////////////////////////////////////////////////
// HttpMonitor
///////////////////////////////////////////////////////////////////////////////

class HttpMonitor : public sigslot::has_slots<> {
public:
  HttpMonitor(SocketServer *ss);

  void reset() { complete_ = false; }

  bool done() const { return complete_; }
  int error() const { return err_; }

  void Connect(talk_base::HttpClient* http);  
  void OnHttpClientComplete(talk_base::HttpClient * http, int err);

private:
  bool complete_;
  int err_;
  SocketServer *ss_;
};

///////////////////////////////////////////////////////////////////////////////
// SslSocketFactory
///////////////////////////////////////////////////////////////////////////////

class SslSocketFactory : public talk_base::SocketFactory {
 public:
  SslSocketFactory(talk_base::SocketFactory * factory, const std::string &user_agent)
    : factory_(factory), logging_level_(talk_base::LS_VERBOSE), 
      binary_mode_(false), agent_(user_agent) { }

  void UseSSL(const char * hostname) { hostname_ = hostname; }
  void DisableSSL() { hostname_.clear(); }

  void SetProxy(const talk_base::ProxyInfo& proxy) { proxy_ = proxy; }
  const talk_base::ProxyInfo& proxy() const { return proxy_; }
  bool ignore_bad_cert() {return ignore_bad_cert_;}
  void SetIgnoreBadCert(bool ignore) { ignore_bad_cert_ = ignore; }

  void SetLogging(talk_base::LoggingSeverity level, const std::string& label, 
      bool binary_mode = false) {
    logging_level_ = level;
    logging_label_ = label;
    binary_mode_ = binary_mode;
  }

  virtual talk_base::Socket * CreateSocket(int type);
  virtual talk_base::AsyncSocket * CreateAsyncSocket(int type);

private:
  talk_base::SocketFactory * factory_;
  talk_base::ProxyInfo proxy_;
  std::string hostname_, logging_label_;
  talk_base::LoggingSeverity logging_level_;
  bool binary_mode_;
  std::string agent_;
  bool ignore_bad_cert_;
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base_

#endif  // _ASYNCHTTPREQUEST_H_
