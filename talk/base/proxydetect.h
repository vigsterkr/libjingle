#ifndef _PROXYDETECT_H_
#define _PROXYDETECT_H_

#include "talk/base/proxyinfo.h"

// Auto-detect the proxy server.  Returns true if a proxy is configured,
// although hostname may be empty if the proxy is not required for the given URL.

bool GetProxySettingsForUrl(const char* agent, const char* url,
                            talk_base::ProxyInfo& proxy,
                            bool long_operation = false);

#endif // _PROXYDETECT_H_
