// TODO: Abstract this better for cross-platformability

#ifdef _WINDOWS
#include "talk/base/win32.h"
#include <shlobj.h>
#endif

#include "talk/base/httpcommon.h"
#include "talk/base/httpcommon-inl.h"
#include "talk/base/proxydetect.h"
#include "talk/base/stringutils.h"
#include "talk/base/basicdefs.h"

#if _WINDOWS
#define _TRY_FIREFOX 1
#define _TRY_WINHTTP 1
#define _TRY_JSPROXY 0
#define _TRY_WM_FINDPROXY 0
#define _TRY_IE_LAN_SETTINGS 1
#endif // _WINDOWS

#if _TRY_WINHTTP
//#include <winhttp.h>
// Note: From winhttp.h

const char WINHTTP[] = "winhttp";
typedef LPVOID HINTERNET;

typedef struct {
    DWORD  dwAccessType;      // see WINHTTP_ACCESS_* types below
    LPWSTR lpszProxy;         // proxy server list
    LPWSTR lpszProxyBypass;   // proxy bypass list
} WINHTTP_PROXY_INFO, * LPWINHTTP_PROXY_INFO;

typedef struct {
    DWORD   dwFlags;
    DWORD   dwAutoDetectFlags;
    LPCWSTR lpszAutoConfigUrl;
    LPVOID  lpvReserved;
    DWORD   dwReserved;
    BOOL    fAutoLogonIfChallenged;
} WINHTTP_AUTOPROXY_OPTIONS;

typedef struct {
    BOOL    fAutoDetect;
    LPWSTR  lpszAutoConfigUrl;
    LPWSTR  lpszProxy;
    LPWSTR  lpszProxyBypass;
} WINHTTP_CURRENT_USER_IE_PROXY_CONFIG;

extern "C" {
typedef HINTERNET (WINAPI * pfnWinHttpOpen)
(
    IN LPCWSTR pwszUserAgent,
    IN DWORD   dwAccessType,
    IN LPCWSTR pwszProxyName   OPTIONAL,
    IN LPCWSTR pwszProxyBypass OPTIONAL,
    IN DWORD   dwFlags
);
typedef BOOL (STDAPICALLTYPE * pfnWinHttpCloseHandle)
(
    IN HINTERNET hInternet
);
typedef BOOL (STDAPICALLTYPE * pfnWinHttpGetProxyForUrl)
(
    IN  HINTERNET                   hSession,
    IN  LPCWSTR                     lpcwszUrl,
    IN  WINHTTP_AUTOPROXY_OPTIONS * pAutoProxyOptions,
    OUT WINHTTP_PROXY_INFO *        pProxyInfo  
);
typedef BOOL (STDAPICALLTYPE * pfnWinHttpGetIEProxyConfig)
(
    IN OUT WINHTTP_CURRENT_USER_IE_PROXY_CONFIG * pProxyConfig
);

} // extern "C"

#define WINHTTP_AUTOPROXY_AUTO_DETECT           0x00000001
#define WINHTTP_AUTOPROXY_CONFIG_URL            0x00000002
#define WINHTTP_AUTOPROXY_RUN_INPROCESS         0x00010000
#define WINHTTP_AUTOPROXY_RUN_OUTPROCESS_ONLY   0x00020000
#define WINHTTP_AUTO_DETECT_TYPE_DHCP           0x00000001
#define WINHTTP_AUTO_DETECT_TYPE_DNS_A          0x00000002
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY               0
#define WINHTTP_ACCESS_TYPE_NO_PROXY                    1
#define WINHTTP_ACCESS_TYPE_NAMED_PROXY                 3
#define WINHTTP_NO_PROXY_NAME     NULL
#define WINHTTP_NO_PROXY_BYPASS   NULL

#endif // _TRY_WINHTTP

#if _TRY_JSPROXY
extern "C" {
typedef BOOL (STDAPICALLTYPE * pfnInternetGetProxyInfo)
(
    LPCSTR lpszUrl,
    DWORD dwUrlLength,
    LPSTR lpszUrlHostName,
    DWORD dwUrlHostNameLength,
    LPSTR * lplpszProxyHostName,
    LPDWORD lpdwProxyHostNameLength
  );
} // extern "C"
#endif // _TRY_JSPROXY

#if _TRY_WM_FINDPROXY
#include <comutil.h>
#include <wmnetsourcecreator.h>
#include <wmsinternaladminnetsource.h>
#endif // _TRY_WM_FINDPROXY

#if _TRY_IE_LAN_SETTINGS
#include <wininet.h>
#include <string>
#endif // _TRY_IE_LAN_SETTINGS

using namespace talk_base;

//////////////////////////////////////////////////////////////////////
// Utility Functions
//////////////////////////////////////////////////////////////////////

#ifdef _WINDOWS
#ifdef _UNICODE

typedef std::wstring tstring;
std::string Utf8String(const tstring& str) { return ToUtf8(str); }

#else  // !_UNICODE

typedef std::string tstring;
std::string Utf8String(const tstring& str) { return str; }

#endif  // !_UNICODE
#endif  // _WINDOWS

//////////////////////////////////////////////////////////////////////
// GetProxySettingsForUrl
//////////////////////////////////////////////////////////////////////

bool WildMatch(const char * target, const char * pattern) {
  while (*pattern) {
    if (*pattern == '*') {
      if (!*++pattern) {
        return true;
      }
      while (*target) {
        if ((toupper(*pattern) == toupper(*target)) && WildMatch(target + 1, pattern + 1)) {
          return true;
        }
        ++target;
      }
      return false;
    } else {
      if (toupper(*pattern) != toupper(*target)) {
        return false;
      }
      ++target;
      ++pattern;
    }
  }
  return !*target;
}

bool ProxyItemMatch(const Url<char>& url, char * item, size_t len) {
  // hostname:443
  if (char * port = strchr(item, ':')) {
    *port++ = '\0';
    if (url.port() != atol(port)) {
      return false;
    }
  }

  // A.B.C.D or A.B.C.D/24
  int a, b, c, d, m;
  int match = sscanf(item, "%d.%d.%d.%d/%d", &a, &b, &c, &d, &m);
  if (match >= 4) {
    uint32 ip = ((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((c & 0xFF) << 8) | (d & 0xFF);
    if ((match < 5) || (m > 32))
      m = 32;
    else if (m < 0)
      m = 0;
    uint32 mask = (m == 0) ? 0 : (~0UL) << (32 - m);
    SocketAddress addr(url.server());
    return !addr.IsUnresolved() && ((addr.ip() & mask) == (ip & mask));
  }

  // .foo.com
  if (*item == '.') {
    size_t hostlen = url.server().length();
    return (hostlen > len)
      && (stricmp(url.server().c_str() + (hostlen - len), item) == 0);
  }

  // localhost or www.*.com
  if (!WildMatch(url.server().c_str(), item))
    return false;

  return true;
}

bool ProxyListMatch(const Url<char>& url, const std::string& slist, char sep) {
  const size_t BUFSIZE = 256;
  char buffer[BUFSIZE];
  const char* list = slist.c_str();
  while (*list) {
    // Remove leading space
    if (isspace(*list)) {
      ++list;
      continue;
    }
    // Break on separator
    size_t len;
    const char * start = list;
    if (const char * end = strchr(list, sep)) {
      len = (end - list);
      list += len + 1;
    } else {
      len = strlen(list);
      list += len;
    }
    // Remove trailing space
    while ((len > 0) && isspace(start[len-1]))
      --len;
    // Check for oversized entry
    if (len >= BUFSIZE)
      continue;
    memcpy(buffer, start, len);
    buffer[len] = 0;
    if (!ProxyItemMatch(url, buffer, len))
      continue;
    return true;
  }
  return false;
}

bool Better(ProxyType lhs, const ProxyType rhs) {
  // PROXY_NONE, PROXY_HTTPS, PROXY_SOCKS5, PROXY_UNKNOWN
  const int PROXY_VALUE[4] = { 0, 2, 3, 1 };
  return (PROXY_VALUE[lhs] > PROXY_VALUE[rhs]);
}

bool ParseProxy(const std::string& saddress, ProxyInfo& proxy) {
  const size_t kMaxAddressLength = 1024;
  // Allow semicolon, space, or tab as an address separator
  const char* const kAddressSeparator = " ;\t";

  ProxyType ptype;
  std::string host;
  uint16 port;

  const char* address = saddress.c_str();
  while (*address) {
    size_t len;
    const char * start = address;
    if (const char * sep = strchr(address, kAddressSeparator)) {
      len = (sep - address);
      address += len + 1;
      while (strchr(kAddressSeparator, *address)) {
        address += 1;
      }
    } else {
      len = strlen(address);
      address += len;
    }

    if (len > kMaxAddressLength - 1) {
      LOG(LS_WARNING) << "Proxy address too long [" << start << "]";
      continue;
    }

    char buffer[kMaxAddressLength];
    memcpy(buffer, start, len);
    buffer[len] = 0;

    char * colon = strchr(buffer, ':');
    if (!colon) {
      LOG(LS_WARNING) << "Proxy address without port [" << buffer << "]";
      continue;
    }

    *colon = 0;
    char * endptr;
    port = static_cast<uint16>(strtol(colon + 1, &endptr, 0));
    if (*endptr != 0) {
      LOG(LS_WARNING) << "Proxy address with invalid port [" << buffer << "]";
      continue;
    }

    if (char * equals = strchr(buffer, '=')) {
      *equals = 0;
      host = equals + 1;
      if (_stricmp(buffer, "socks") == 0) {
        ptype = PROXY_SOCKS5;
      } else if (_stricmp(buffer, "https") == 0) {
        ptype = PROXY_HTTPS;
      } else {
        LOG(LS_WARNING) << "Proxy address with unknown protocol ["
                        << buffer << "]";
        ptype = PROXY_UNKNOWN;
      }
    } else {
      host = buffer;
      ptype = PROXY_UNKNOWN;
    }

    if (Better(ptype, proxy.type)) {
      proxy.type = ptype;
      proxy.address.SetIP(host);
      proxy.address.SetPort((int)port);
    }
  }

  return (proxy.type != PROXY_NONE);
}

#if _WINDOWS
bool IsDefaultBrowserFirefox() {
  HKEY key;
  LONG result = RegOpenKeyEx(HKEY_CLASSES_ROOT, L"http\\shell\\open\\command",
                             0, KEY_READ, &key);
  if (ERROR_SUCCESS != result)
    return false;

  wchar_t* value = NULL;
  DWORD size, type;
  result = RegQueryValueEx(key, L"", 0, &type, NULL, &size);
  if (REG_SZ != type) {
    result = ERROR_ACCESS_DENIED;  // Any error is fine
  } else if (ERROR_SUCCESS == result) {
    value = new wchar_t[size+1];
    BYTE* buffer = reinterpret_cast<BYTE*>(value);
    result = RegQueryValueEx(key, L"", 0, &type, buffer, &size);
  }
  RegCloseKey(key);

  bool success = false;
  if (ERROR_SUCCESS == result) {
    value[size] = L'\0';
    for (size_t i=0; i<size; ++i) {
      value[i] = tolowercase(value[i]);
    }
    success = (NULL != strstr(value, L"firefox.exe"));
  }
  delete [] value;
  return success;
}
#endif

#if _TRY_FIREFOX

#define USE_FIREFOX_PROFILES_INI 1

bool GetDefaultFirefoxProfile(std::wstring* profile) {
  ASSERT(NULL != profile);

  wchar_t path[MAX_PATH];
  if (SHGetFolderPath(0, CSIDL_APPDATA, 0, SHGFP_TYPE_CURRENT, path) != S_OK)
    return false;

  std::wstring profile_root(path);
  profile_root.append(L"\\Mozilla\\Firefox\\");

#if USE_FIREFOX_PROFILES_INI
  std::wstring tmp(profile_root);
  tmp.append(L"profiles.ini");

  FILE * fp = _wfopen(tmp.c_str(), L"rb");
  if (!fp)
    return false;

  // [Profile0]
  // Name=default
  // IsRelative=1
  // Path=Profiles/2de53ejb.default
  // Default=1

  // Note: we are looking for the first entry with "Default=1", or the last entry in the file

  std::wstring candidate;
  bool relative = true;

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), fp)) {
    size_t len = strlen(buffer);
    while ((len > 0) && isspace(buffer[len-1]))
      buffer[--len] = 0;
    if (buffer[0] == '[') {
      relative = true;
      candidate.clear();
    } else if (strnicmp(buffer, "IsRelative=", 11) == 0) {
      relative = (buffer[11] != '0');
    } else if (strnicmp(buffer, "Path=", 5) == 0) {
      if (relative) {
        candidate = profile_root;
      } else {
        candidate.clear();
      }
      candidate.append(ToUtf16(buffer + 5));
      candidate.append(L"\\");
    } else if (strnicmp(buffer, "Default=", 8) == 0) {
      if ((buffer[8] != '0') && !candidate.empty()) {
        break;
      }
    }
  }
  fclose(fp);
  if (candidate.empty())
    return false;
  *profile = candidate;

#else // !USE_FIREFOX_PROFILES_INI
  std::wstring tmp(profile_root);
  tmp.append(L"Profiles\\*.default");
  WIN32_FIND_DATA fdata;
  HANDLE hFind = FindFirstFile(tmp.c_str(), &fdata);
  if (hFind == INVALID_HANDLE_VALUE)
    return false;

  profile->assign(profile_root);
  profile->append(L"Profiles\\");
  profile->append(fdata.cFileName);
  profile->append(L"\\");
  FindClose(hFind);
#endif // !USE_FIREFOX_PROFILES_INI

  return true;
}

struct StringMap {
public:
  void Add(const char * name, const char * value) { map_[name] = value; }
  const std::string& Get(const char * name, const char * def = "") const {
    std::map<std::string, std::string>::const_iterator it =
      map_.find(name);
    if (it != map_.end())
      return it->second;
    def_ = def;
    return def_;
  }
  bool IsSet(const char * name) const { 
    return (map_.find(name) != map_.end());
  }
private:
  std::map<std::string, std::string> map_;
  mutable std::string def_;
};

bool ReadFirefoxPrefs(const std::wstring& filename,
                      const char * prefix,
                      StringMap& settings) {
  FILE * fp = _wfopen(filename.c_str(), L"rb");
  if (!fp)
    return false;

  size_t prefix_len = strlen(prefix);
  bool overlong_line = false;

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), fp)) {
    size_t len = strlen(buffer);
    bool missing_newline = (len > 0) && (buffer[len-1] != '\n');

    if (missing_newline) {
      overlong_line = true;
      continue;
    } else if (overlong_line) {
      LOG_F(LS_INFO) << "Skipping long line";
      overlong_line = false;
      continue;
    }

    while ((len > 0) && isspace(buffer[len-1]))
      buffer[--len] = 0;

    // Skip blank lines
    if ((len == 0) || (buffer[0] == '#')
        || (strncmp(buffer, "/*", 2) == 0)
        || (strncmp(buffer, " *", 2) == 0))
      continue;

    int nstart = 0, nend = 0, vstart = 0, vend = 0;
    sscanf(buffer, "user_pref(\"%n%*[^\"]%n\", %n%*[^)]%n);",
      &nstart, &nend, &vstart, &vend);
    if (vend > 0) {
      char * name = buffer + nstart;
      name[nend - nstart] = 0;
      if ((vend - vstart >= 2) && (buffer[vstart] == '"')) {
        vstart += 1;
        vend -= 1;
      }
      char * value = buffer + vstart;
      value[vend - vstart] = 0;
      if ((strncmp(name, prefix, prefix_len) == 0) && *value) {
        settings.Add(name + prefix_len, value);
      }
    } else {
      LOG_F(LS_WARNING) << "Unparsed pref [" << buffer << "]";
    }
  }
  fclose(fp);
  return true;
}
#endif // _TRY_FIREFOX

#ifdef WIN32
BOOL MyWinHttpGetProxyForUrl(pfnWinHttpGetProxyForUrl pWHGPFU,
    HINTERNET hWinHttp, LPCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *options,
    WINHTTP_PROXY_INFO *info) {
  // WinHttpGetProxyForUrl() can call plugins which can crash.
  // In the case of McAfee scriptproxy.dll, it does crash in
  // older versions. Try to catch crashes here and treat as an
  // error.
  BOOL success = FALSE;

#if (_HAS_EXCEPTIONS == 0)
  __try {
    success = pWHGPFU(hWinHttp, url, options, info);
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    LOG_GLEM(LERROR,WINHTTP) << "WinHttpGetProxyForUrl faulted!!";
  }
#else
  success = pWHGPFU(hWinHttp, url, options, info);
#endif

  return success;
}
#endif

bool GetProxySettingsForUrl(const char* agent, const char* url,
                            ProxyInfo& proxy,
                            bool long_operation) {
  bool success = false;
  Url<char> purl(url);

#if 0
  assert( WildMatch(_T("A.B.C.D"), _T("a.b.c.d")));
  assert( WildMatch(_T("127.0.0.1"), _T("12*.0.*1")));
  assert(!WildMatch(_T("127.0.0.0"), _T("12*.0.*1")));
  assert(!WildMatch(_T("127.0.0.0"), _T("12*.0.*1")));
  assert( WildMatch(_T("127.1.0.21"), _T("12*.0.*1")));
  assert(!WildMatch(_T("127.1.1.21"), _T("12*.0.*1")));
  purl = PUrl(_T("http://a.b.c:500/"));
  wchar_t item[256];
  _tcscpy(item, _T("a.b.c"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("a.x.c"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("a.b.*"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("a.x.*"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T(".b.c"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T(".x.c"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("a.b.c:500"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("a.b.c:501"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
  purl = PUrl(_T("http://1.2.3.4/"));
  _tcscpy(item, _T("1.2.3.4"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("1.2.3.5"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("1.2.3.5/31"));
  assert( ProxyItemMatch(purl, item, _tcslen(item)));
  _tcscpy(item, _T("1.2.3.5/32"));
  assert(!ProxyItemMatch(purl, item, _tcslen(item)));
#endif

  bool autoconfig = false;
  bool use_firefox = false;
  std::string autoconfig_url;

#if _TRY_FIREFOX
  use_firefox = IsDefaultBrowserFirefox();

  if (use_firefox) {
    std::wstring tmp;
    if (GetDefaultFirefoxProfile(&tmp)) {
      bool complete = true;

      StringMap settings;
      tmp.append(L"prefs.js");
      if (ReadFirefoxPrefs(tmp, "network.proxy.", settings)) {
        success = true;
        if (settings.Get("type") == "1") {
          if (ProxyListMatch(purl, settings.Get("no_proxies_on", "localhost, 127.0.0.1").c_str(), ',')) {
            // Bypass proxy
          } else if (settings.Get("share_proxy_settings") == "true") {
            proxy.type = PROXY_UNKNOWN;
            proxy.address.SetIP(settings.Get("http"));
            proxy.address.SetPort(atoi(settings.Get("http_port").c_str()));
          } else if (settings.IsSet("socks")) {
            proxy.type = PROXY_SOCKS5;
            proxy.address.SetIP(settings.Get("socks"));
            proxy.address.SetPort(atoi(settings.Get("socks_port").c_str()));
          } else if (settings.IsSet("ssl")) {
            proxy.type = PROXY_HTTPS;
            proxy.address.SetIP(settings.Get("ssl"));
            proxy.address.SetPort(atoi(settings.Get("ssl_port").c_str()));
          } else if (settings.IsSet("http")) {
            proxy.type = PROXY_HTTPS;
            proxy.address.SetIP(settings.Get("http"));
            proxy.address.SetPort(atoi(settings.Get("http_port").c_str()));
          } 
        } else if (settings.Get("type") == "2") {
          complete = success = false;
          autoconfig_url = settings.Get("autoconfig_url").c_str();
        } else if (settings.Get("type") == "4") {
          complete = success = false;
          autoconfig = true;
        }
      }
      if (complete) { // Otherwise fall through to IE autoproxy code
        return success;
      }
    }
  }
#endif // _TRY_FIREFOX

#if _TRY_WINHTTP
  if (!success) {
    if (HMODULE hModWH = LoadLibrary(L"winhttp.dll")) {
      pfnWinHttpOpen pWHO = reinterpret_cast<pfnWinHttpOpen>(GetProcAddress(hModWH, "WinHttpOpen"));
      pfnWinHttpCloseHandle pWHCH = reinterpret_cast<pfnWinHttpCloseHandle>(GetProcAddress(hModWH, "WinHttpCloseHandle"));
      pfnWinHttpGetProxyForUrl pWHGPFU = reinterpret_cast<pfnWinHttpGetProxyForUrl>(GetProcAddress(hModWH, "WinHttpGetProxyForUrl"));
      pfnWinHttpGetIEProxyConfig pWHGIEPC = reinterpret_cast<pfnWinHttpGetIEProxyConfig>(GetProcAddress(hModWH, "WinHttpGetIEProxyConfigForCurrentUser"));
      if (pWHO && pWHCH && pWHGPFU && pWHGIEPC) {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG iecfg;
        memset(&iecfg, 0, sizeof(iecfg));
        if (!use_firefox && !pWHGIEPC(&iecfg)) {
          LOG_GLEM(LERROR,WINHTTP) << "WinHttpGetIEProxyConfigForCurrentUser";
        } else {
          success = true;
          if (!use_firefox) {
            if (iecfg.fAutoDetect) {
              autoconfig = true;
            }
            if (iecfg.lpszAutoConfigUrl) {
              autoconfig_url = ToUtf8(iecfg.lpszAutoConfigUrl);
            }
          }
          if (!long_operation) {
            // Unless we perform this operation in the background, don't allow
            // it to take a long time.
            autoconfig = false;
          }
          if (autoconfig || !autoconfig_url.empty()) {
            if (HINTERNET hWinHttp = pWHO(ToUtf16(agent).c_str(),
                                          WINHTTP_ACCESS_TYPE_NO_PROXY,
                                          WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS,
                                          0)) {
              WINHTTP_AUTOPROXY_OPTIONS options;
              memset(&options, 0, sizeof(options));
              if (autoconfig) {
                options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
                options.dwAutoDetectFlags |= WINHTTP_AUTO_DETECT_TYPE_DHCP
                                             | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
              }
              std::wstring autoconfig_url16((ToUtf16)(autoconfig_url));
              if (!autoconfig_url.empty()) {
                options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
                options.lpszAutoConfigUrl = autoconfig_url16.c_str();
              }
              options.fAutoLogonIfChallenged = TRUE;
              WINHTTP_PROXY_INFO info;
              memset(&info, 0, sizeof(info));

              BOOL success = MyWinHttpGetProxyForUrl(pWHGPFU,
                hWinHttp, ToUtf16(url).c_str(), &options, &info);

              if (!success) {
                LOG_GLEM(LERROR,WINHTTP) << "WinHttpGetProxyForUrl";
              } else {
                if (iecfg.lpszProxy)
                  GlobalFree(iecfg.lpszProxy);
                if (iecfg.lpszProxyBypass)
                  GlobalFree(iecfg.lpszProxyBypass);
                iecfg.lpszProxy = info.lpszProxy;
                iecfg.lpszProxyBypass = info.lpszProxyBypass;
              }
              pWHCH(hWinHttp);
            }
          }
          if (!ProxyListMatch(purl, ToUtf8(nonnull(iecfg.lpszProxyBypass)), ' ')) {
            ParseProxy(ToUtf8(nonnull(iecfg.lpszProxy)), proxy);
          }
          if (iecfg.lpszAutoConfigUrl)
            GlobalFree(iecfg.lpszAutoConfigUrl);
          if (iecfg.lpszProxy)
            GlobalFree(iecfg.lpszProxy);
          if (iecfg.lpszProxyBypass)
            GlobalFree(iecfg.lpszProxyBypass);
        }
      }
      FreeLibrary(hModWH);
    }
  }
#endif // _TRY_WINHTTP

#if _TRY_JSPROXY
  if (!success) {
    if (HMODULE hModJS = LoadLibrary(_T("jsproxy.dll"))) {
      pfnInternetGetProxyInfo pIGPI = reinterpret_cast<pfnInternetGetProxyInfo>(GetProcAddress(hModJS, "InternetGetProxyInfo"));
      if (pIGPI) {
        char proxy[256], host[256];
        memset(proxy, 0, sizeof(proxy));
        char * ptr = proxy;
        DWORD proxylen = sizeof(proxy);
        std::string surl = Utf8String(url);
        DWORD hostlen = _snprintf(host, sizeof(host), "http%s://%S", purl.secure() ? "s" : "", purl.server());
        if (pIGPI(surl.data(), surl.size(), host, hostlen, &ptr, &proxylen)) {
          LOG(INFO) << "Proxy: " << proxy;
        } else {
          LOG_GLE(INFO) << "InternetGetProxyInfo";
        }
      }
      FreeLibrary(hModJS);
    }
  }
#endif // _TRY_JSPROXY

#if _TRY_WM_FINDPROXY
  if (!success) {
    INSNetSourceCreator * nsc = 0;
    HRESULT hr = CoCreateInstance(CLSID_ClientNetManager, 0, CLSCTX_ALL, IID_INSNetSourceCreator, (LPVOID *) &nsc);
    if (SUCCEEDED(hr)) {
      if (SUCCEEDED(hr = nsc->Initialize())) {
        VARIANT dispatch;
        VariantInit(&dispatch);
        if (SUCCEEDED(hr = nsc->GetNetSourceAdminInterface(L"http", &dispatch))) {
          IWMSInternalAdminNetSource * ians = 0;
          if (SUCCEEDED(hr = dispatch.pdispVal->QueryInterface(IID_IWMSInternalAdminNetSource, (LPVOID *) &ians))) {
            _bstr_t host(purl.server());
            BSTR proxy = 0;
            BOOL bProxyEnabled = FALSE;
            DWORD port, context = 0;
            if (SUCCEEDED(hr = ians->FindProxyForURL(L"http", host, &bProxyEnabled, &proxy, &port, &context))) {
              success = true;
              if (bProxyEnabled) {
                _bstr_t sproxy = proxy;
                proxy.ptype = PT_HTTPS;
                proxy.host = sproxy;
                proxy.port = port;
              }
            }
            SysFreeString(proxy);
            if (FAILED(hr = ians->ShutdownProxyContext(context))) {
              LOG(LS_INFO) << "IWMSInternalAdminNetSource::ShutdownProxyContext failed: " << hr;
            }
            ians->Release();
          }
        }
        VariantClear(&dispatch);
        if (FAILED(hr = nsc->Shutdown())) {
          LOG(LS_INFO) << "INSNetSourceCreator::Shutdown failed: " << hr;
        }
      }
      nsc->Release();
    }
  }
#endif // _TRY_WM_FINDPROXY

#if _TRY_IE_LAN_SETTINGS
  if (!success) {
    wchar_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    INTERNET_PROXY_INFO * info = reinterpret_cast<INTERNET_PROXY_INFO *>(buffer);
    DWORD dwSize = sizeof(buffer);

    if (!InternetQueryOption(0, INTERNET_OPTION_PROXY, info, &dwSize)) {
      LOG(LS_INFO) << "InternetQueryOption failed: " << GetLastError();
    } else if (info->dwAccessType == INTERNET_OPEN_TYPE_DIRECT) {
      success = true;
    } else if (info->dwAccessType == INTERNET_OPEN_TYPE_PROXY) {
      success = true;
      if (!ProxyListMatch(purl, nonnull(reinterpret_cast<const char*>(info->lpszProxyBypass)), ' ')) {
        ParseProxy(nonnull(reinterpret_cast<const char*>(info->lpszProxy)), proxy);
      }
    } else {
      LOG(LS_INFO) << "unknown internet access type: " << info->dwAccessType;
    }
  }
#endif // _TRY_IE_LAN_SETTINGS

#if 0
  if (!success) {
    INTERNET_PER_CONN_OPTION_LIST list;
    INTERNET_PER_CONN_OPTION options[3];
    memset(&list, 0, sizeof(list));
    memset(&options, 0, sizeof(options));

    list.dwSize = sizeof(list);
    list.dwOptionCount = 3;
    list.pOptions = options;
    options[0].dwOption = INTERNET_PER_CONN_FLAGS;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    DWORD dwSize = sizeof(list);

    if (!InternetQueryOption(0, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &dwSize)) {
      LOG(LS_INFO) << "InternetQueryOption failed: " << GetLastError();
    } else if ((options[0].Value.dwValue & PROXY_TYPE_PROXY) != 0) {
      success = true;
      if (!ProxyListMatch(purl, nonnull(options[2].Value.pszValue), _T(';'))) {
        ParseProxy(nonnull(options[1].Value.pszValue), proxy);
      }
    } else if ((options[0].Value.dwValue & PROXY_TYPE_DIRECT) != 0) {
      success = true;
    } else {
      LOG(LS_INFO) << "unknown internet access type: "
        << options[0].Value.dwValue;
    }
    if (options[1].Value.pszValue) {
      GlobalFree(options[1].Value.pszValue);
    }
    if (options[2].Value.pszValue) {
      GlobalFree(options[2].Value.pszValue);
    }
  }
#endif // 0

  return success;
}
