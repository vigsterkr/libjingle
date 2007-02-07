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

#ifndef TALK_BASE_HTTPCOMMON_H__
#define TALK_BASE_HTTPCOMMON_H__

#include <map>
#include <string>
#include <vector>
#include "talk/base/basictypes.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/stringutils.h"
#include "talk/base/stream.h"

namespace talk_base {

class CryptString;
class SocketAddress;

//////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////

enum HttpCode { 
  HC_OK = 200,
  HC_NON_AUTHORITATIVE = 203,
  HC_NO_CONTENT = 204,
  HC_PARTIAL_CONTENT = 206,

  HC_MULTIPLE_CHOICES = 300,
  HC_MOVED_PERMANENTLY = 301,
  HC_FOUND = 302,
  HC_SEE_OTHER = 303,
  HC_NOT_MODIFIED = 304,
  HC_MOVED_TEMPORARILY = 307,

  HC_BAD_REQUEST = 400,
  HC_UNAUTHORIZED = 401,
  HC_FORBIDDEN = 403,
  HC_NOT_FOUND = 404,
  HC_PROXY_AUTHENTICATION_REQUIRED = 407,
  HC_GONE = 410,

  HC_INTERNAL_SERVER_ERROR = 500 
};

enum HttpVersion {
  HVER_1_0, HVER_1_1,
  HVER_LAST = HVER_1_1
};

enum HttpVerb {
  HV_GET, HV_POST, HV_PUT, HV_DELETE, HV_CONNECT, HV_HEAD,
  HV_LAST = HV_HEAD
};

enum HttpError {
  HE_NONE,
  HE_PROTOCOL, HE_DISCONNECTED, HE_OVERFLOW,
  HE_SOCKET, HE_SHUTDOWN, HE_OPERATION_CANCELLED,
  HE_AUTH,                // Proxy Authentication Required
  HE_CERTIFICATE_EXPIRED, // During SSL negotiation
  HE_STREAM,              // Problem reading or writing to the document
  HE_CACHE,               // Problem reading from cache
  HE_DEFAULT
};

enum HttpHeader {
  HH_AGE,
  HH_CACHE_CONTROL,
  HH_CONNECTION,
  HH_CONTENT_LENGTH,
  HH_CONTENT_RANGE,
  HH_CONTENT_TYPE,
  HH_COOKIE,
  HH_DATE,
  HH_ETAG,
  HH_EXPIRES,
  HH_HOST,
  HH_IF_MODIFIED_SINCE,
  HH_IF_NONE_MATCH,
  HH_KEEP_ALIVE,
  HH_LAST_MODIFIED,
  HH_LOCATION,
  HH_PROXY_AUTHENTICATE,
  HH_PROXY_AUTHORIZATION,
  HH_PROXY_CONNECTION,
  HH_RANGE,
  HH_SET_COOKIE,
  HH_TE,
  HH_TRAILERS,
  HH_TRANSFER_ENCODING,
  HH_UPGRADE,
  HH_USER_AGENT,
  HH_WWW_AUTHENTICATE,
  HH_LAST = HH_WWW_AUTHENTICATE
};

const uint16 HTTP_DEFAULT_PORT = 80;
const uint16 HTTP_SECURE_PORT = 443;

//////////////////////////////////////////////////////////////////////
// Utility Functions
//////////////////////////////////////////////////////////////////////

inline HttpError mkerr(HttpError err, HttpError def_err = HE_DEFAULT) {
  return (err != HE_NONE) ? err : def_err;
}

const char* ToString(HttpVersion version);
bool FromString(HttpVersion& version, const std::string& str);

const char* ToString(HttpVerb verb);
bool FromString(HttpVerb& verb, const std::string& str);

const char* ToString(HttpHeader header);
bool FromString(HttpHeader& header, const std::string& str);

inline bool HttpCodeIsInformational(uint32 code) { return ((code / 100) == 1); }
inline bool HttpCodeIsSuccessful(uint32 code)    { return ((code / 100) == 2); }
inline bool HttpCodeIsRedirection(uint32 code)   { return ((code / 100) == 3); }
inline bool HttpCodeIsClientError(uint32 code)   { return ((code / 100) == 4); }
inline bool HttpCodeIsServerError(uint32 code)   { return ((code / 100) == 5); }

bool HttpCodeHasBody(uint32 code);
bool HttpCodeIsCacheable(uint32 code);
bool HttpHeaderIsEndToEnd(HttpHeader header);
bool HttpHeaderIsCollapsible(HttpHeader header);

struct HttpData;
bool HttpShouldKeepAlive(const HttpData& data);

typedef std::pair<std::string, std::string> HttpAttribute;
typedef std::vector<HttpAttribute> HttpAttributeList;
void HttpParseAttributes(const char * data, size_t len, 
                         HttpAttributeList& attributes);
bool HttpHasAttribute(const HttpAttributeList& attributes,
                      const std::string& name,
                      std::string* value);
bool HttpHasNthAttribute(HttpAttributeList& attributes,
                         size_t index, 
                         std::string* name,
                         std::string* value);

// Convert RFC1123 date (DoW, DD Mon YYYY HH:MM:SS TZ) to unix timestamp
bool HttpDateToSeconds(const std::string& date, unsigned long* seconds);

inline const uint16 UrlDefaultPort(bool secure) {
  return secure ? HTTP_SECURE_PORT : HTTP_DEFAULT_PORT;
}

// functional for insensitive std::string compare
struct iless {
  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return (::_stricmp(lhs.c_str(), rhs.c_str()) < 0);
  }
};

//////////////////////////////////////////////////////////////////////
// Url
//////////////////////////////////////////////////////////////////////

template<class CTYPE>
class Url {
public:
  typedef typename Traits<CTYPE>::string string;

  // TODO: Implement Encode/Decode
  static int Encode(const CTYPE* source, CTYPE* destination, size_t len);
  static int Encode(const string& source, string& destination);
  static int Decode(const CTYPE* source, CTYPE* destination, size_t len);
  static int Decode(const string& source, string& destination);

  Url(const string& url);
  Url(const string& path, const string& server, uint16 port = HTTP_DEFAULT_PORT)
  : m_server(server), m_path(path), m_port(port),
    m_secure(HTTP_SECURE_PORT == port)
  {
    ASSERT(m_path.empty() || (m_path[0] == static_cast<CTYPE>('/')));
  }
  
  bool valid() const { return !m_server.empty(); }
  const string& server() const { return m_server; }
  // Note: path() was renamed to path_, because it now uses the stricter sense
  // of not including a query string.  I'm trying to think of a clearer name.
  const string& path_() const { return m_path; }
  const string& query() const { return m_query; }
  string full_path();
  string url();
  uint16 port() const { return m_port; }
  bool secure() const { return m_secure; }

  void set_server(const string& val) { m_server = val; }
  void set_path(const string& val) {
    ASSERT(val.empty() || (val[0] == static_cast<CTYPE>('/')));
    m_path = val;
  }
  void set_query(const string& val) {
    ASSERT(val.empty() || (val[0] == static_cast<CTYPE>('?')));
    m_query = val;
  }
  void set_port(uint16 val) { m_port = val; }
  void set_secure(bool val) { m_secure = val; }

private:
  string m_server, m_path, m_query;
  uint16 m_port;
  bool m_secure;
};

//////////////////////////////////////////////////////////////////////
// HttpData
//////////////////////////////////////////////////////////////////////

struct HttpData {
  typedef std::multimap<std::string, std::string, iless> HeaderMap;
  typedef HeaderMap::const_iterator const_iterator;
  
  HttpVersion version;
  scoped_ptr<StreamInterface> document;

  HttpData() : version(HVER_1_1) { }

  enum HeaderCombine { HC_YES, HC_NO, HC_AUTO, HC_REPLACE, HC_NEW };
  void changeHeader(const std::string& name, const std::string& value,
                    HeaderCombine combine);
  inline void addHeader(const std::string& name, const std::string& value,
                        bool append = true) {
    changeHeader(name, value, append ? HC_AUTO : HC_NO);
  }
  inline void setHeader(const std::string& name, const std::string& value,
                        bool overwrite = true) {
    changeHeader(name, value, overwrite ? HC_REPLACE : HC_NEW);
  }
  void clearHeader(const std::string& name);

  // keep in mind, this may not do what you want in the face of multiple headers
  bool hasHeader(const std::string& name, std::string* value) const;

  inline const_iterator begin() const {
    return m_headers.begin();
  }
  inline const_iterator end() const {
    return m_headers.end();
  }
  inline const_iterator begin(const std::string& name) const {
    return m_headers.lower_bound(name);
  }
  inline const_iterator end(const std::string& name) const {
    return m_headers.upper_bound(name);
  }
  
  // Convenience methods using HttpHeader
  inline void changeHeader(HttpHeader header, const std::string& value,
                           HeaderCombine combine) {
    changeHeader(ToString(header), value, combine);
  }
  inline void addHeader(HttpHeader header, const std::string& value,
                        bool append = true) {
    addHeader(ToString(header), value, append);
  }
  inline void setHeader(HttpHeader header, const std::string& value,
                        bool overwrite = true) {
    setHeader(ToString(header), value, overwrite);
  }
  inline void clearHeader(HttpHeader header) {
    clearHeader(ToString(header));
  }
  inline bool hasHeader(HttpHeader header, std::string* value) const {
    return hasHeader(ToString(header), value);
  }
  inline const_iterator begin(HttpHeader header) const {
    return m_headers.lower_bound(ToString(header));
  }
  inline const_iterator end(HttpHeader header) const {
    return m_headers.upper_bound(ToString(header));
  }

  void setContent(const std::string& content_type, StreamInterface* document);

  virtual size_t formatLeader(char* buffer, size_t size) = 0;
  virtual HttpError parseLeader(const char* line, size_t len) = 0;
  
protected:  
  virtual ~HttpData() { }
  void clear(bool release_document);

private:
  HeaderMap m_headers;
};

struct HttpRequestData : public HttpData {
  HttpVerb verb;
  std::string path;

  HttpRequestData() : verb(HV_GET) { }

  void clear(bool release_document);

  virtual size_t formatLeader(char* buffer, size_t size);
  virtual HttpError parseLeader(const char* line, size_t len);
};

struct HttpResponseData : public HttpData {
  uint32 scode;
  std::string message;

  HttpResponseData() : scode(HC_INTERNAL_SERVER_ERROR) { }
  void clear(bool release_document);

  // Convenience methods
  void set_success(uint32 scode = HC_OK);
  void set_success(const std::string& content_type, StreamInterface* document,
                   uint32 scode = HC_OK);
  void set_redirect(const std::string& location,
                    uint32 scode = HC_MOVED_TEMPORARILY);
  void set_error(uint32 scode);

  virtual size_t formatLeader(char* buffer, size_t size);
  virtual HttpError parseLeader(const char* line, size_t len);
};

//////////////////////////////////////////////////////////////////////
// Http Authentication
//////////////////////////////////////////////////////////////////////

struct HttpAuthContext {
  std::string auth_method;
  HttpAuthContext(const std::string& auth) : auth_method(auth) { }
  virtual ~HttpAuthContext() { }
};

enum HttpAuthResult { HAR_RESPONSE, HAR_IGNORE, HAR_CREDENTIALS, HAR_ERROR };

// 'context' is used by this function to record information between calls.
// Start by passing a null pointer, then pass the same pointer each additional
// call.  When the authentication attempt is finished, delete the context.
HttpAuthResult HttpAuthenticate(
  const char * challenge, size_t len,
  const SocketAddress& server,
  const std::string& method, const std::string& uri,
  const std::string& username, const CryptString& password,
  HttpAuthContext *& context, std::string& response, std::string& auth_method);

//////////////////////////////////////////////////////////////////////

} // namespace talk_base

#endif // TALK_BASE_HTTPCOMMON_H__
