#ifndef _URLENCODE_H_
#define _URLENCODE_H_ 

#include <string>

int UrlDecode(const char *source, char *dest);
int UrlEncode(const char *source, char *dest, unsigned max);
std::string UrlDecodeString(const std::string & encoded);
std::string UrlEncodeString(const std::string & decoded);

#endif

