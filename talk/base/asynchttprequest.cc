#include "talk/base/common.h"
#include "talk/base/firewallsocketserver.h"
#include "talk/base/httpclient.h"
#include "talk/base/logging.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/socketadapters.h"
#include "talk/base/socketpool.h"
#include "talk/base/ssladapter.h"
#include "talk/base/asynchttprequest.h"

using namespace talk_base;

///////////////////////////////////////////////////////////////////////////////
// HttpMonitor
///////////////////////////////////////////////////////////////////////////////

HttpMonitor::HttpMonitor(SocketServer *ss) {
  ASSERT(talk_base::Thread::Current() != NULL);
  ss_ = ss;
  reset();
}

void HttpMonitor::Connect(talk_base::HttpClient *http) {
  http->SignalHttpClientComplete.connect(this,
    &HttpMonitor::OnHttpClientComplete);
}

void HttpMonitor::OnHttpClientComplete(talk_base::HttpClient * http, int err) {
  complete_ = true;
  err_ = err;
  ss_->WakeUp();
}

///////////////////////////////////////////////////////////////////////////////
// SslSocketFactory
///////////////////////////////////////////////////////////////////////////////

talk_base::Socket * SslSocketFactory::CreateSocket(int type) {
  return factory_->CreateSocket(type);
}

talk_base::AsyncSocket * SslSocketFactory::CreateAsyncSocket(int type) {
  talk_base::AsyncSocket * socket = factory_->CreateAsyncSocket(type);
  if (!socket)
    return 0;

  // Binary logging happens at the lowest level 
  if (!logging_label_.empty() && binary_mode_) {
    socket = new talk_base::LoggingSocketAdapter(socket, logging_level_, 
                                                 logging_label_.c_str(),
                                                 binary_mode_);
  }

  if (proxy_.type) {
    talk_base::AsyncSocket * proxy_socket = 0;
    if (proxy_.type == talk_base::PROXY_SOCKS5) {
      proxy_socket = new talk_base::AsyncSocksProxySocket(socket, proxy_.address,
        proxy_.username, proxy_.password);
    } else {
      // Note: we are trying unknown proxies as HTTPS currently
      proxy_socket = new talk_base::AsyncHttpsProxySocket(socket,
        agent_, proxy_.address,
        proxy_.username, proxy_.password);
    }
    if (!proxy_socket) {
      delete socket;
      return 0;
    }
    socket = proxy_socket;  // for our purposes the proxy is now the socket
  }

  if (!hostname_.empty()) {
    talk_base::SSLAdapter * ssl_adapter = talk_base::SSLAdapter::Create(socket);
    ssl_adapter->set_ignore_bad_cert(ignore_bad_cert_);
    ssl_adapter->StartSSL(hostname_.c_str(), true);
    socket = ssl_adapter;
  }

  // Regular logging occurs at the highest level
  if (!logging_label_.empty() && !binary_mode_) {
    socket = new talk_base::LoggingSocketAdapter(socket, logging_level_, 
                                                 logging_label_.c_str(),
                                                 binary_mode_);
  }
  return socket;
}

///////////////////////////////////////////////////////////////////////////////
// AsyncHttpRequest
///////////////////////////////////////////////////////////////////////////////

const int kDefaultHTTPTimeout = 30 * 1000; // 30 sec

AsyncHttpRequest::AsyncHttpRequest(const std::string &user_agent)
: firewall_(0), port_(80), secure_(false),
  timeout_(kDefaultHTTPTimeout), fail_redirect_(false),
  client_(user_agent.c_str(), NULL)
{
}

void AsyncHttpRequest::DoWork() {
  // TODO: Rewrite this to use the thread's native socket server, and a more
  // natural flow?

  talk_base::PhysicalSocketServer physical;
  talk_base::SocketServer * ss = &physical;
  if (firewall_) {
    ss = new talk_base::FirewallSocketServer(ss, firewall_);
  }

  SslSocketFactory factory(ss, client_.agent());
  factory.SetProxy(proxy_);
  if (secure_)
    factory.UseSSL(host_.c_str());

  //factory.SetLogging("AsyncHttpRequest");

  talk_base::ReuseSocketPool pool(&factory);
  client_.set_pool(&pool);
  
  bool transparent_proxy = (port_ == 80)
    && ((proxy_.type == talk_base::PROXY_HTTPS)
        || (proxy_.type == talk_base::PROXY_UNKNOWN));

  if (transparent_proxy) {
    client_.set_proxy(proxy_);
  }
  client_.set_fail_redirect(fail_redirect_);

  talk_base::SocketAddress server(host_, port_);
  client_.set_server(server);

  HttpMonitor monitor(ss);
  monitor.Connect(&client_);
  client_.start();
  ss->Wait(timeout_, true);
  if (!monitor.done()) {
    LOG(LS_INFO) << "AsyncHttpRequest request timed out";
    client_.reset();
    return;
  }
  
  if (monitor.error()) {
    LOG(LS_INFO) << "AsyncHttpRequest request error: " << monitor.error();
    if (monitor.error() == talk_base::HE_AUTH) {
      //proxy_auth_required_ = true;
    }
    return;
  }

  std::string value;
  if (client_.response().hasHeader(HH_LOCATION, &value)) {
    response_redirect_ = value.c_str();
  }
}
