/*
 * libjingle
 * Copyright 2007, Google Inc.
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

#ifndef _CONVERT_H_
#define _CONVERT_H_

#include <string>
#include <windows.h>

#ifndef NO_ATL
#include <atlstr.h>
#endif // NO_ATL

#include "talk/base/basictypes.h"

class Utf8 {
public:
#ifndef NO_ATL
	explicit Utf8(const CString & str) {
    *this = str;
  }
#else
	explicit Utf8(const wchar_t *str) {
	*this = str;
	}
#endif

  explicit Utf8() {}
#ifndef NO_ATL
  inline Utf8& operator =(const CString & str) {
    // TODO: deal with errors
    int len8 = WideCharToMultiByte(CP_UTF8, 0, str.GetString(), str.GetLength(),
                                   NULL, 0, NULL, NULL);
    char * ns = static_cast<char*>(_alloca(len8));
    WideCharToMultiByte(CP_UTF8, 0, str.GetString(), str.GetLength(),
                        ns, len8, NULL, NULL);
    str_.assign(ns, len8);
    return *this;
  }
#else
inline Utf8& operator =(const wchar_t *str) {
    // TODO: deal with errors
    int len8 = WideCharToMultiByte(CP_UTF8, 0, str, wcslen(str),
                                   NULL, 0, NULL, NULL);
    char * ns = static_cast<char*>(_alloca(len8));
    WideCharToMultiByte(CP_UTF8, 0, str, wcslen(str),
                        ns, len8, NULL, NULL);
    str_.assign(ns, len8);
    return *this;
  }
#endif // NO_ATL

  inline operator const std::string & () const {
    return str_;
  }

  inline const char * AsSz() const {
    return str_.c_str();
  }

  // Deprecated
  inline const std::string & AsString() const {
    return str_;
  }

  // Deprecated
  inline int Len8() const {
    return (int)str_.length();
  }

private:
  DISALLOW_EVIL_CONSTRUCTORS(Utf8);
  std::string str_;
};

class Utf16 {
public:
  explicit Utf16(const std::string & str) {
    // TODO: deal with errors
    int len16 = MultiByteToWideChar(CP_UTF8, 0, str.data(), -1,
                                    NULL, 0);
#ifndef NO_ATL
    wchar_t * ws = cstr_.GetBuffer(len16);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), str.length(), ws, len16);
    cstr_.ReleaseBuffer(len16);
#else
	str_ = new wchar_t[len16];
	MultiByteToWideChar(CP_UTF8, 0, str.data(), -1, str_, len16);
#endif
  }

#ifndef NO_ATL
  inline operator const CString & () const {
    return cstr_;
  }
    // Deprecated
  inline const CString & AsCString() const {
    return cstr_;
  }
  // Deprecated
  inline int Len16() const {
    return cstr_.GetLength();
  }
  inline const wchar_t * AsWz() const {
    return cstr_.GetString();
  }
#else
  ~Utf16() {
    delete[] str_;
  }
  inline const wchar_t * AsWz() const {
    return str_;
  }
#endif

private:
  DISALLOW_EVIL_CONSTRUCTORS(Utf16);
#ifndef NO_ATL
  CString cstr_;
#else
  wchar_t *str_;
#endif
};

#endif  // _CONVERT_H_
