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

#ifndef TALK_BASE_HTTPCOMMON_INL_H__
#define TALK_BASE_HTTPCOMMON_INL_H__

#include "talk/base/common.h"
#include "talk/base/httpcommon.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// Url
///////////////////////////////////////////////////////////////////////////////

template<class CTYPE>
Url<CTYPE>::Url(const string& url) {
  const CTYPE* raw_url = url.c_str();
  if (ascnicmp(raw_url, "http://", 7) == 0) {
    raw_url += 7;
    m_secure = false;
  } else if (ascnicmp(raw_url, "https://", 8) == 0) {
    raw_url += 8;
    m_secure = true;
  } else {
    return;
  }
  m_port = UrlDefaultPort(m_secure);
  const CTYPE* colon = ::strchr(raw_url, static_cast<CTYPE>(':'));
  const CTYPE* slash = ::strchr(raw_url, static_cast<CTYPE>('/'));
  if (!colon && !slash) {
    m_server = url;
    // TODO: rethink this slash
    m_path.append(1, static_cast<CTYPE>('/'));
  } else {
    const CTYPE* ptr;
    if (colon == 0) {
      ptr = slash;
    } else if (slash == 0) {
      ptr = colon;
    } else {
      ptr = _min(colon, slash);
    }
    m_server.assign(raw_url, ptr - raw_url);
    if (ptr == colon) {
      CTYPE* tmp = 0;
      m_port = static_cast<uint16>(::strtoul(ptr + 1, &tmp, 10));
      ptr = tmp;
    }
    const CTYPE* query = ::strchr(ptr, static_cast<CTYPE>('?'));
    if (!query) {
      m_path.assign(ptr);
    } else {
      m_path.assign(ptr, query - ptr);
      m_query.assign(query);
    }
  }
  ASSERT(m_path.empty() || (m_path[0] == static_cast<CTYPE>('/')));
  ASSERT(m_query.empty() || (m_query[0] == static_cast<CTYPE>('?')));
}

template<class CTYPE>
typename Traits<CTYPE>::string Url<CTYPE>::full_path() {
  string full_path(m_path);
  full_path.append(m_query);
  return full_path;
}

template<class CTYPE>
typename Traits<CTYPE>::string Url<CTYPE>::url() {
  CTYPE protocol[9];
  asccpyn(protocol, ARRAY_SIZE(protocol), m_secure ? "https://" : "http://");
  string url(protocol);
  url.append(m_server);
  if (m_port != UrlDefaultPort(m_secure)) {
    CTYPE format[5], port[32];
    asccpyn(format, ARRAY_SIZE(format), ":%hu");
    sprintfn(port, ARRAY_SIZE(port), format, m_port);
    url.append(port);
  }
  url.append(m_path);
  url.append(m_query);
  return url;
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif  // TALK_BASE_HTTPCOMMON_INL_H__
