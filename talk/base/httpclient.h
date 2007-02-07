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

#ifndef TALK_BASE_HTTPCLIENT_H__
#define TALK_BASE_HTTPCLIENT_H__

#include "talk/base/common.h"
#include "talk/base/httpbase.h"
#include "talk/base/proxyinfo.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/sigslot.h"
#include "talk/base/socketaddress.h"
#include "talk/base/socketpool.h"

namespace talk_base {

//////////////////////////////////////////////////////////////////////
// HttpClient
//////////////////////////////////////////////////////////////////////

class DiskCache;
class HttpClient;
class IPNetPool;

class HttpClient : private IHttpNotify {
public:
  HttpClient(const std::string& agent, StreamPool* pool);
  virtual ~HttpClient();

  void set_pool(StreamPool* pool) { pool_ = pool; }

  const std::string& agent() const { return agent_; }
  
  void set_proxy(const ProxyInfo& proxy) { proxy_ = proxy; }
  const ProxyInfo& proxy() const { return proxy_; }

  void set_fail_redirect(bool fail_redirect) { fail_redirect_ = fail_redirect; }
  bool fail_redirect() const { return fail_redirect_; }

  void use_absolute_uri(bool absolute_uri) { absolute_uri_ = absolute_uri; }
  bool absolute_uri() const { return absolute_uri_; }

  void set_cache(DiskCache* cache) { ASSERT(!IsCacheActive()); cache_ = cache; }
  bool cache_enabled() const { return (NULL != cache_); }

  // reset clears the server, request, and response structures.  It will also
  // abort an active request.
  void reset();
  
  void set_server(const SocketAddress& address);
  const SocketAddress& server() const { return server_; }

  HttpRequestData& request() { return request_; }
  const HttpRequestData& request() const { return request_; }
  HttpResponseData& response() { return response_; }
  const HttpResponseData& response() const { return response_; }
  
  // convenience methods
  void prepare_get(const std::string& url);
  void prepare_post(const std::string& url, const std::string& content_type,
                    StreamInterface* request_doc);

  // After you finish setting up your request, call start.
  void start();
  
  // Signalled when the header has finished downloading, before the document
  // content is processed.  This notification is for informational purposes
  // only.  Do not modify the client in response to this.
  sigslot::signal3<const HttpClient*,bool,size_t> SignalHeaderAvailable;
  // Signalled when the current 'call' finishes.  On success, err is 0.
  sigslot::signal2<HttpClient*,int> SignalHttpClientComplete;
  // Signalled when the network connection goes down while a call is not
  // in progress.
  sigslot::signal2<HttpClient*,int> SignalHttpClientClosed;

protected:
  void release();

  bool BeginCacheFile();
  HttpError WriteCacheHeaders(const std::string& id);
  void CompleteCacheFile();

  bool CheckCache();
  HttpError ReadCacheHeaders(const std::string& id, bool override);
  HttpError ReadCacheBody(const std::string& id);

  bool PrepareValidate();
  HttpError CompleteValidate();

  HttpError OnHeaderAvailable(bool ignore_data, bool chunked, size_t data_size);

  // IHttpNotify Interface
  virtual HttpError onHttpHeaderComplete(bool chunked, size_t& data_size);
  virtual void onHttpComplete(HttpMode mode, HttpError err);
  virtual void onHttpClosed(HttpError err);
  
private:
  enum CacheState { CS_READY, CS_WRITING, CS_READING, CS_VALIDATING };
  bool IsCacheActive() const { return (cache_state_ > CS_READY); }

  std::string agent_;
  StreamPool* pool_;
  HttpBase base_;
  SocketAddress server_;
  ProxyInfo proxy_;
  HttpRequestData request_;
  HttpResponseData response_;
  bool fail_redirect_, absolute_uri_;
  scoped_ptr<HttpAuthContext> context_;
  DiskCache* cache_;
  CacheState cache_state_;
};

//////////////////////////////////////////////////////////////////////
// Default implementation of HttpClient
//////////////////////////////////////////////////////////////////////

class HttpClientDefault : public ReuseSocketPool, public HttpClient {
public:
  HttpClientDefault(SocketFactory* factory, const std::string& agent) 
  : ReuseSocketPool(factory), HttpClient(agent, NULL)
  { 
    set_pool(this);
  }
};

//////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif // TALK_BASE_HTTPCLIENT_H__
