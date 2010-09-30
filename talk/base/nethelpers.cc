/*
 * libjingle
 * Copyright 2008, Google Inc.
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

#include "talk/base/nethelpers.h"

#include "talk/base/byteorder.h"
#include "talk/base/signalthread.h"

namespace talk_base {

// AsyncResolver

AsyncResolver::AsyncResolver() : result_(NULL), error_(0) {
}

AsyncResolver::~AsyncResolver() {
  FreeHostEnt(result_);
}

void AsyncResolver::DoWork() {
  result_ = SafeGetHostByName(addr_.hostname().c_str(), &error_);
}

void AsyncResolver::OnWorkDone() {
  if (result_) {
    addr_.SetIP(NetworkToHost32(
        *reinterpret_cast<uint32*>(result_->h_addr_list[0])));
  }
}

// The functions below are used to do gethostbyname, but with an allocated
// instead of a static buffer.
hostent* SafeGetHostByName(const char* hostname, int* herrno) {
  hostent* result = NULL;
#if defined(WIN32) || (defined(POSIX) && !defined(OSX))
  // On Windows we have to allocate a buffer, and manually copy the hostent,
  // along with its embedded pointers.
  hostent* ent = gethostbyname(hostname);
  if (!ent) {
#ifdef WIN32
    *herrno = WSAGetLastError();
#else  // POSIX
    *herrno = h_errno;
#endif
    return NULL;
  }

  // Get the total number of bytes we need to copy, and allocate our buffer.
  int num_aliases = 0, num_addrs = 0;
  int total_len = sizeof(hostent);
  total_len += strlen(ent->h_name) + 1;
  while (ent->h_aliases[num_aliases]) {
    total_len += sizeof(char*) + strlen(ent->h_aliases[num_aliases]) + 1;
    ++num_aliases;
  }
  total_len += sizeof(char*);
  while (ent->h_addr_list[num_addrs]) {
    total_len += sizeof(char*) + ent->h_length;
    ++num_addrs;
  }
  total_len += sizeof(char*);

  result = static_cast<hostent*>(malloc(total_len));
  char* p = reinterpret_cast<char*>(result) + sizeof(hostent);

  // Copy the hostent into it, along with its embedded pointers.
  result->h_name = p;
  memcpy(p, ent->h_name, strlen(ent->h_name) + 1);
  p += strlen(ent->h_name) + 1;

  result->h_aliases = reinterpret_cast<char**>(p);
  p += (num_aliases + 1) * sizeof(char*);
  for (int i = 0; i < num_aliases; ++i) {
    result->h_aliases[i] = p;
    memcpy(p, ent->h_aliases[i], strlen(ent->h_aliases[i]) + 1);
    p += strlen(ent->h_aliases[i]) + 1;
  }
  result->h_aliases[num_aliases] = NULL;

  result->h_addrtype = ent->h_addrtype;
  result->h_length = ent->h_length;

  result->h_addr_list = reinterpret_cast<char**>(p);
  p += (num_addrs + 1) * sizeof(char*);
  for (int i = 0; i < num_addrs; ++i) {
    result->h_addr_list[i] = p;
    memcpy(p, ent->h_addr_list[i], ent->h_length);
    p += ent->h_length;
  }
  result->h_addr_list[num_addrs] = NULL;

  *herrno = 0;
#elif defined(OSX)
  // Mac OS returns an object with everything allocated.
  result = getipnodebyname(hostname, AF_INET, AI_DEFAULT, herrno);
#else
#error "I don't know how to do gethostbyname safely on your system."
#endif
  return result;
}

// This function should mirror the above function, and free any resources
// allocated by the above.
void FreeHostEnt(hostent* host) {
#if defined(WIN32) || (defined(POSIX) && !defined(OSX))
  free(host);
#elif defined(OSX)
  freehostent(host);
#else
#error "I don't know how to free a hostent on your system."
#endif
}

}  // namespace talk_base
