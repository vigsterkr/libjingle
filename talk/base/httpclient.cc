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

#include <time.h>

#include "talk/base/httpcommon-inl.h"

#include "talk/base/asyncsocket.h"
#include "talk/base/common.h"
#include "talk/base/diskcache.h"
#include "talk/base/httpclient.h"
#include "talk/base/logging.h"
#include "talk/base/pathutils.h"
#include "talk/base/socketstream.h"
#include "talk/base/stringencode.h"
#include "talk/base/stringutils.h"
#include "talk/base/basicdefs.h"

namespace talk_base {

//////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////

namespace {

const size_t kCacheHeader = 0;
const size_t kCacheBody = 1;

std::string HttpAddress(const SocketAddress& address) {
  return (address.port() == HTTP_DEFAULT_PORT)
          ? address.hostname() : address.ToString();
}

// Convert decimal string to integer
bool HttpStringToInt(const std::string& str, unsigned long* val) {
  ASSERT(NULL != val);
  char* eos = NULL;
  *val = strtoul(str.c_str(), &eos, 10);
  return (*eos == '\0');
}

bool HttpShouldCache(const HttpRequestData& request, 
                     const HttpResponseData& response) {
  bool verb_allows_cache = (request.verb == HV_GET)
                           || (request.verb == HV_HEAD);
  bool is_range_response = response.hasHeader(HH_CONTENT_RANGE, NULL);
  bool has_expires = response.hasHeader(HH_EXPIRES, NULL);
  bool request_allows_cache =
    has_expires || (std::string::npos != request.path.find('?'));
  bool response_allows_cache =
    has_expires || HttpCodeIsCacheable(response.scode);

  bool may_cache = verb_allows_cache
                   && request_allows_cache
                   && response_allows_cache
                   && !is_range_response;

  std::string value;
  if (response.hasHeader(HH_CACHE_CONTROL, &value)) {
    HttpAttributeList directives;
    HttpParseAttributes(value.data(), value.size(), directives);
    // Response Directives Summary:
    // public - always cacheable
    // private - do not cache in a shared cache
    // no-cache - may cache, but must revalidate whether fresh or stale
    // no-store - sensitive information, do not cache or store in any way
    // max-age - supplants Expires for staleness
    // s-maxage - use as max-age for shared caches, ignore otherwise
    // must-revalidate - may cache, but must revalidate after stale
    // proxy-revalidate - shared cache must revalidate
    if (HttpHasAttribute(directives, "no-store", NULL)) {
      may_cache = false;
    } else if (HttpHasAttribute(directives, "public", NULL)) {
      may_cache = true;
    }
  }
  return may_cache;
}

enum HttpCacheState {
  HCS_FRESH,  // In cache, may use
  HCS_STALE,  // In cache, must revalidate
  HCS_NONE    // Not in cache
};

HttpCacheState HttpGetCacheState(const HttpRequestData& request, 
                                 const HttpResponseData& response) {
  // Temporaries
  std::string s_temp;
  unsigned long i_temp;

  // Current time
  unsigned long now = time(0);

  HttpAttributeList cache_control;
  if (response.hasHeader(HH_CACHE_CONTROL, &s_temp)) {
    HttpParseAttributes(s_temp.data(), s_temp.size(), cache_control);
  }

  // Compute age of cache document
  unsigned long date;
  if (!response.hasHeader(HH_DATE, &s_temp)
      || !HttpDateToSeconds(s_temp, &date))
    return HCS_NONE;

  // TODO: Timestamp when cache request sent and response received?
  unsigned long request_time = date;
  unsigned long response_time = date;

  unsigned long apparent_age = 0;
  if (response_time > date) {
    apparent_age = response_time - date;
  }

  unsigned long corrected_received_age = apparent_age;
  if (response.hasHeader(HH_AGE, &s_temp)
      && HttpStringToInt(s_temp, &i_temp)) {
    corrected_received_age = stdmax(apparent_age, i_temp);
  }

  unsigned long response_delay = response_time - request_time;
  unsigned long corrected_initial_age = corrected_received_age + response_delay;
  unsigned long resident_time = now - response_time;
  unsigned long current_age = corrected_initial_age + resident_time;

  // Compute lifetime of document
  unsigned long lifetime;
  if (HttpHasAttribute(cache_control, "max-age", &s_temp)) {
    lifetime = atoi(s_temp.c_str());
  } else if (response.hasHeader(HH_EXPIRES, &s_temp)
             && HttpDateToSeconds(s_temp, &i_temp)) {
    lifetime = i_temp - date;
  } else if (response.hasHeader(HH_LAST_MODIFIED, &s_temp)
             && HttpDateToSeconds(s_temp, &i_temp)) {
    // TODO: Issue warning 113 if age > 24 hours
    lifetime = (now - i_temp) / 10;
  } else {
    return HCS_STALE;
  }

  return (lifetime > current_age) ? HCS_FRESH : HCS_STALE;
}

enum HttpValidatorStrength {
  HVS_NONE,
  HVS_WEAK,
  HVS_STRONG
};

HttpValidatorStrength
HttpRequestValidatorLevel(const HttpRequestData& request) {
  if (HV_GET != request.verb)
    return HVS_STRONG;
  return request.hasHeader(HH_RANGE, NULL) ? HVS_STRONG : HVS_WEAK;
}

HttpValidatorStrength
HttpResponseValidatorLevel(const HttpResponseData& response) {
  std::string value;
  if (response.hasHeader(HH_ETAG, &value)) {
    bool is_weak = (strnicmp(value.c_str(), "W/", 2) == 0);
    return is_weak ? HVS_WEAK : HVS_STRONG;
  }
  if (response.hasHeader(HH_LAST_MODIFIED, &value)) {
    unsigned long last_modified, date;
    if (HttpDateToSeconds(value, &last_modified) 
        && response.hasHeader(HH_DATE, &value)
        && HttpDateToSeconds(value, &date)
        && (last_modified + 60 < date)) {
      return HVS_STRONG;
    }
    return HVS_WEAK;
  }
  return HVS_NONE;
}

std::string GetCacheID(const SocketAddress& server,
                       const HttpRequestData& request) {
  std::string url;
  url.append(ToString(request.verb));
  url.append("_");
  if ((_strnicmp(request.path.c_str(), "http://", 7) == 0)
      || (_strnicmp(request.path.c_str(), "https://", 8) == 0)) {
    url.append(request.path);
  } else {
    url.append("http://");
    url.append(HttpAddress(server));
    url.append(request.path);
  }
  return url;
}

}  // anonymous namespace

//////////////////////////////////////////////////////////////////////
// HttpClient
//////////////////////////////////////////////////////////////////////

HttpClient::HttpClient(const std::string& agent, StreamPool* pool)
: agent_(agent), pool_(pool), fail_redirect_(false), absolute_uri_(false),
  cache_(NULL), cache_state_(CS_READY)
{
  base_.notify(this);
}

HttpClient::~HttpClient() {
  base_.notify(NULL);
  base_.abort(HE_SHUTDOWN);
  release();
}

void HttpClient::reset() {
  server_.Clear();
  request_.clear(true);
  response_.clear(true);
  context_.reset();
  base_.abort(HE_OPERATION_CANCELLED);
}

void HttpClient::set_server(const SocketAddress& address) {
  server_ = address;
  // Setting 'Host' here allows it to be overridden before starting the request,
  // if necessary.
  request_.setHeader(HH_HOST, HttpAddress(server_), true);
}

void HttpClient::start() {
  if (base_.mode() != HM_NONE) {
    // call reset() to abort an in-progress request
    ASSERT(false);
    return;
  }

  ASSERT(!IsCacheActive());

  if (request_.hasHeader(HH_TRANSFER_ENCODING, NULL)) {
    // Exact size must be known on the client.  Instead of using chunked
    // encoding, wrap data with auto-caching file or memory stream.
    ASSERT(false);
    return;
  }

  // If no content has been specified, using length of 0.
  request_.setHeader(HH_CONTENT_LENGTH, "0", false);

  request_.setHeader(HH_USER_AGENT, agent_, false);
  request_.setHeader(HH_CONNECTION, "Keep-Alive", false);
  if (_strnicmp(request_.path.c_str(), "http", 4) == 0) {
    request_.setHeader(HH_PROXY_CONNECTION, "Keep-Alive", false);
  }

  bool absolute_uri = absolute_uri_;
  if (PROXY_HTTPS == proxy_.type) {
    request().version = HVER_1_0;
    // Proxies require canonical form
    absolute_uri = true;
  }

  // Convert to canonical form (if not already)
  if (absolute_uri && (_strnicmp(request().path.c_str(), "http://", 7) != 0)) {
    std::string canonical_path("http://");
    canonical_path.append(HttpAddress(server_));
    canonical_path.append(request().path);
    request().path = canonical_path;
  }

  if ((NULL != cache_) && CheckCache()) {
    return;
  }

  int stream_err;
  StreamInterface* stream = pool_->RequestConnectedStream(server_, &stream_err);
  if (stream == NULL) {
    if (stream_err)
      LOG(LS_ERROR) << "RequestConnectedStream returned: " << stream_err;
    onHttpComplete(HM_CONNECT, (stream_err == 0) ? HE_NONE : HE_SOCKET);
  } else {
    base_.attach(stream);
    if (stream->GetState() == SS_OPEN) {
      base_.send(&request_);
    }
  }
}

void HttpClient::prepare_get(const std::string& url) {
  reset();
  Url<char> purl(url);
  set_server(SocketAddress(purl.server(), purl.port(), false));
  request().verb = HV_GET;
  request().path = purl.full_path();
}

void HttpClient::prepare_post(const std::string& url,
                              const std::string& content_type,
                              StreamInterface* request_doc) {
  reset();
  Url<char> purl(url);
  set_server(SocketAddress(purl.server(), purl.port(), false));
  request().verb = HV_POST;
  request().path = purl.full_path();
  request().setContent(content_type, request_doc);
}

void HttpClient::release() {
  if (StreamInterface* stream = base_.detach()) {
    pool_->ReturnConnectedStream(stream);
  }
}

bool HttpClient::BeginCacheFile() {
  ASSERT(NULL != cache_);
  ASSERT(CS_READY == cache_state_);

  std::string id = GetCacheID(server_, request_);
  CacheLock lock(cache_, id, true);
  if (!lock.IsLocked()) {
    LOG_F(LS_WARNING) << "Couldn't lock cache";
    return false;
  }

  if (HE_NONE != WriteCacheHeaders(id)) {
    return false;
  }

  scoped_ptr<StreamInterface> stream(cache_->WriteResource(id, kCacheBody));
  if (!stream.get()) {
    LOG_F(LS_ERROR) << "Couldn't open body cache";
    return false;
  }
  lock.Commit();

  // Let's secretly replace the response document with Folgers Crystals,
  // er, StreamTap, so that we can mirror the data to our cache.
  StreamInterface* output = response_.document.release();
  if (!output) {
    output = new NullStream;
  }
  StreamTap* tap = new StreamTap(output, stream.release());
  response_.document.reset(tap);
  return true;
}

HttpError HttpClient::WriteCacheHeaders(const std::string& id) {
  scoped_ptr<StreamInterface> stream(cache_->WriteResource(id, kCacheHeader));
  if (!stream.get()) {
    LOG_F(LS_ERROR) << "Couldn't open header cache";
    return HE_CACHE;
  }

  // Write all unknown and end-to-end headers to a cache file
  for (HttpData::const_iterator it = response_.begin();
       it != response_.end(); ++it) {
    HttpHeader header;
    if (FromString(header, it->first) && !HttpHeaderIsEndToEnd(header))
      continue;
    std::string formatted_header(it->first);
    formatted_header.append(": ");
    formatted_header.append(it->second);
    formatted_header.append("\r\n");
    StreamResult result = stream->WriteAll(formatted_header.data(),
                                           formatted_header.length(),
                                           NULL, NULL);
    if (SR_SUCCESS != result) {
      LOG_F(LS_ERROR) << "Couldn't write header cache";
      return HE_CACHE;
    }
  }

  return HE_NONE;
}

void HttpClient::CompleteCacheFile() {
  // Restore previous response document
  StreamTap* tap = static_cast<StreamTap*>(response_.document.release());
  response_.document.reset(tap->Detach());

  int error;
  StreamResult result = tap->GetTapResult(&error);

  // Delete the tap and cache stream (which completes cache unlock)
  delete tap;

  if (SR_SUCCESS != result) {
    LOG(LS_ERROR) << "Cache file error: " << error;
    cache_->DeleteResource(GetCacheID(server_, request_));
  }
}

bool HttpClient::CheckCache() {
  ASSERT(NULL != cache_);
  ASSERT(CS_READY == cache_state_);

  std::string id = GetCacheID(server_, request_);
  if (!cache_->HasResource(id)) {
    // No cache file available
    return false;
  }

  HttpError error = ReadCacheHeaders(id, true);

  if (HE_NONE == error) {
    switch (HttpGetCacheState(request_, response_)) {
    case HCS_FRESH:
      // Cache content is good, read from cache
      break;
    case HCS_STALE:
      // Cache content may be acceptable.  Issue a validation request.
      if (PrepareValidate()) {
        return false;
      }
      // Couldn't validate, fall through.
    case HCS_NONE:
      // Cache content is not useable.  Issue a regular request.
      response_.clear(false);
      return false;
    }
  }

  if (HE_NONE == error) {
    error = ReadCacheBody(id);
    cache_state_ = CS_READY;
  }

  if (HE_CACHE == error) {
    LOG_F(LS_WARNING) << "Cache failure, continuing with normal request";
    response_.clear(false);
    return false;
  }

  SignalHttpClientComplete(this, error);
  return true;
}

HttpError HttpClient::ReadCacheHeaders(const std::string& id, bool override) {
  scoped_ptr<StreamInterface> stream(cache_->ReadResource(id, kCacheHeader));
  if (!stream.get()) {
    return HE_CACHE;
  }

  HttpData::HeaderCombine combine =
    override ? HttpData::HC_REPLACE : HttpData::HC_AUTO;

  while (true) {
    std::string formatted_header;
    StreamResult result = stream->ReadLine(&formatted_header);
    if (SR_EOS == result)
      break;

    if (SR_SUCCESS != result) {
      LOG_F(LS_ERROR) << "ReadLine error in cache headers";
      return HE_CACHE;
    }
    size_t end_of_name = formatted_header.find(':');
    if (std::string::npos == end_of_name) {
      LOG_F(LS_WARNING) << "Malformed cache header";
      continue;
    }
    size_t start_of_value = end_of_name + 1;
    size_t end_of_value = formatted_header.length();
    while ((start_of_value < end_of_value)
           && isspace(formatted_header[start_of_value]))
      ++start_of_value;
    while ((start_of_value < end_of_value)
           && isspace(formatted_header[end_of_value-1]))
     --end_of_value;
    size_t value_length = end_of_value - start_of_value;

    std::string name(formatted_header.substr(0, end_of_name));
    std::string value(formatted_header.substr(start_of_value, value_length));
    response_.changeHeader(name, value, combine);
  }

  response_.scode = HC_OK;
  return HE_NONE;
}

HttpError HttpClient::ReadCacheBody(const std::string& id) {
  cache_state_ = CS_READING;

  HttpError error = HE_NONE;

  size_t data_size;
  scoped_ptr<StreamInterface> stream(cache_->ReadResource(id, kCacheBody));
  if (!stream.get() || !stream->GetSize(&data_size)) {
    LOG_F(LS_ERROR) << "Unavailable cache body";
    error = HE_CACHE;
  } else {
    error = OnHeaderAvailable(false, false, data_size);
  }

  if ((HE_NONE == error)
      && (HV_HEAD != request_.verb)
      && (NULL != response_.document.get())) {
    char buffer[1024 * 64];
    StreamResult result = Flow(stream.get(), buffer, ARRAY_SIZE(buffer),
                               response_.document.get());
    if (SR_SUCCESS != result) {
      error = HE_STREAM;
    }
  }

  return error;
}

bool HttpClient::PrepareValidate() {
  ASSERT(CS_READY == cache_state_);
  // At this point, request_ contains the pending request, and response_
  // contains the cached response headers.  Reformat the request to validate
  // the cached content.
  HttpValidatorStrength vs_required = HttpRequestValidatorLevel(request_);
  HttpValidatorStrength vs_available = HttpResponseValidatorLevel(response_);
  if (vs_available < vs_required) {
    return false;
  }
  std::string value;
  if (response_.hasHeader(HH_ETAG, &value)) {
    request_.addHeader(HH_IF_NONE_MATCH, value);
  }
  if (response_.hasHeader(HH_LAST_MODIFIED, &value)) {
    request_.addHeader(HH_IF_MODIFIED_SINCE, value);
  }
  response_.clear(false);
  cache_state_ = CS_VALIDATING;
  return true;
}

HttpError HttpClient::CompleteValidate() {
  ASSERT(CS_VALIDATING == cache_state_);

  std::string id = GetCacheID(server_, request_);

  // Merge cached headers with new headers
  HttpError error = ReadCacheHeaders(id, false);
  if (HE_NONE != error) {
    // Rewrite merged headers to cache
    CacheLock lock(cache_, id);
    error = WriteCacheHeaders(id);
  }
  if (HE_NONE != error) {
    error = ReadCacheBody(id);
  }
  return error;
}

HttpError HttpClient::OnHeaderAvailable(bool ignore_data, bool chunked,
                                        size_t data_size) {
  if (!ignore_data && !chunked && response_.document.get()) {
    // Attempt to pre-allocate space for the downloaded data.
    if (!response_.document->ReserveSize(data_size)) {
      return HE_OVERFLOW;
    }
  }
  SignalHeaderAvailable(this, chunked, data_size);
  return HE_NONE;
}

//
// HttpBase Implementation
//

HttpError HttpClient::onHttpHeaderComplete(bool chunked, size_t& data_size) {
  if (CS_VALIDATING == cache_state_) {
    if (HC_NOT_MODIFIED == response_.scode) {
      return CompleteValidate();
    }
    // Should we remove conditional headers from request?
    cache_state_ = CS_READY;
    cache_->DeleteResource(GetCacheID(server_, request_));
    // Continue processing response as normal
  }

  ASSERT(!IsCacheActive());
  if ((request_.verb == HV_HEAD) || !HttpCodeHasBody(response_.scode)) {
    // HEAD requests and certain response codes contain no body
    data_size = 0;
  }
  if ((HttpCodeIsRedirection(response_.scode) && !fail_redirect_)
      || ((HC_PROXY_AUTHENTICATION_REQUIRED == response_.scode)
          && (PROXY_HTTPS == proxy_.type))) {
    // We're going to issue another request, so ignore the incoming data.
    base_.set_ignore_data(true);
  }

  HttpError error = OnHeaderAvailable(base_.ignore_data(), chunked, data_size);
  if (HE_NONE != error) {
    return error;
  }

  if ((NULL != cache_)
      && !base_.ignore_data()
      && HttpShouldCache(request_, response_)) {
    if (BeginCacheFile()) {
      cache_state_ = CS_WRITING;
    }
  }
  return HE_NONE;
}

void HttpClient::onHttpComplete(HttpMode mode, HttpError err) {
  if (err != HE_NONE) {
    // fall through
  } else if (mode == HM_CONNECT) {
    base_.send(&request_);
    return;
  } else if ((mode == HM_SEND) || HttpCodeIsInformational(response_.scode)) {
    // If you're interested in informational headers, catch
    // SignalHeaderAvailable.
    base_.recv(&response_);
    return;
  } else {
    if (!HttpShouldKeepAlive(response_)) {
      LOG(INFO) << "HttpClient: closing socket";
      base_.stream()->Close();
    }
    if (HttpCodeIsRedirection(response_.scode) && !fail_redirect_) {
      std::string value;
      if (!response_.hasHeader(HH_LOCATION, &value)) {
        err = HE_PROTOCOL;
      } else {
        Url<char> purl(value);
        set_server(SocketAddress(purl.server(), purl.port(), false));
        request_.path = purl.full_path();
        if (response_.scode == HC_SEE_OTHER) {
          request_.verb = HV_GET;
          request_.clearHeader(HH_CONTENT_TYPE);
          request_.clearHeader(HH_CONTENT_LENGTH);
          request_.document.reset();
        } else if (request_.document.get() && !request_.document->Rewind()) {
          // Unable to replay the request document.
          err = HE_STREAM;
        }
      }
      if (err == HE_NONE) {
        context_.reset();
        response_.clear(false);
        release();
        start();
        return;
      }
    } else if ((HC_PROXY_AUTHENTICATION_REQUIRED == response_.scode)
               && (PROXY_HTTPS == proxy_.type)) {
      std::string response, auth_method;
      HttpData::const_iterator begin = response_.begin(HH_PROXY_AUTHENTICATE);
      HttpData::const_iterator end = response_.end(HH_PROXY_AUTHENTICATE);
      for (HttpData::const_iterator it = begin; it != end; ++it) {
        HttpAuthResult res = HttpAuthenticate(
          it->second.data(), it->second.size(),
          proxy_.address,
          ToString(request_.verb), request_.path,
          proxy_.username, proxy_.password,
          *context_.use(), response, auth_method);
        if (res == HAR_RESPONSE) {
          request_.setHeader(HH_PROXY_AUTHORIZATION, response);
          if (request_.document.get() && !request_.document->Rewind()) {
            err = HE_STREAM;
          } else {
            // Explicitly do not reset the HttpAuthContext
            response_.clear(false);
            // TODO: Reuse socket when authenticating?
            release();
            start();
            return;
          }
        } else if (res == HAR_IGNORE) {
          LOG(INFO) << "Ignoring Proxy-Authenticate: " << auth_method;
          continue;
        } else {
          break;
        }
      }
    }
  }
  if (CS_WRITING == cache_state_) {
    CompleteCacheFile();
    cache_state_ = CS_READY;
  } else if (CS_READING == cache_state_) {
    cache_state_ = CS_READY;
  }
  release();
  SignalHttpClientComplete(this, err);
}

void HttpClient::onHttpClosed(HttpError err) {
  SignalHttpClientClosed(this, err);
}

//////////////////////////////////////////////////////////////////////

} // namespace talk_base
