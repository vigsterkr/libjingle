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

#ifndef TALK_BASE_WIN32_H__
#define TALK_BASE_WIN32_H__

#include <winsock2.h>
#include <windows.h>
#include <malloc.h>

#include <string>

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////

inline std::wstring ToUtf16(const std::string& str) {
  int len16 = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(),
                                    NULL, 0);
  wchar_t *ws = static_cast<wchar_t*>(_alloca(len16 * sizeof(wchar_t)));
  ::MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), ws, len16);
  std::wstring result(ws, len16);
  return result;
}

inline std::string ToUtf8(const std::wstring& wstr) {
  int len8 = ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.length(),
                                   NULL, 0, NULL, NULL);
  char* ns = static_cast<char*>(_alloca(len8));
  ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr.length(),
                        ns, len8, NULL, NULL);
  std::string result(ns, len8);
  return result;
}

// Convert FILETIME to time_t
void FileTimeToUnixTime(const FILETIME& ft, time_t* ut);

// Convert time_t to FILETIME
void UnixTimeToFileTime(const time_t& ut, FILETIME * ft);

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif  // TALK_BASE_WIN32_H__
