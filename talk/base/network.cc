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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "talk/base/network.h"
#include "talk/base/stream.h"

#ifdef POSIX
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#endif  // POSIX

#ifdef WIN32
#include "talk/base/win32.h"
#include <Iphlpapi.h>
#endif

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "talk/base/host.h"
#include "talk/base/logging.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/socket.h"  // includes something that makes windows happy
#include "talk/base/stringencode.h"
#include "talk/base/time.h"

namespace {

const double kAlpha = 0.5;  // weight for data infinitely far in the past
const double kHalfLife = 2000;  // half life of exponential decay (in ms)
const double kLog2 = 0.693147180559945309417;
const double kLambda = kLog2 / kHalfLife;

// assume so-so quality unless data says otherwise
const double kDefaultQuality = talk_base::QUALITY_FAIR;

typedef std::map<std::string, std::string> StrMap;

void BuildMap(const StrMap& map, std::string& str) {
  str.append("{");
  bool first = true;
  for (StrMap::const_iterator i = map.begin(); i != map.end(); ++i) {
    if (!first) str.append(",");
    str.append(i->first);
    str.append("=");
    str.append(i->second);
    first = false;
  }
  str.append("}");
}

void ParseCheck(std::istringstream& ist, char ch) {
  if (ist.get() != ch)
    LOG(LERROR) << "Expecting '" << ch << "'";
}

std::string ParseString(std::istringstream& ist) {
  std::string str;
  int count = 0;
  while (ist) {
    char ch = ist.peek();
    if ((count == 0) && ((ch == '=') || (ch == ',') || (ch == '}'))) {
      break;
    } else if (ch == '{') {
      count += 1;
    } else if (ch == '}') {
      count -= 1;
      if (count < 0)
        LOG(LERROR) << "mismatched '{' and '}'";
    }
    str.append(1, static_cast<char>(ist.get()));
  }
  return str;
}

void ParseMap(const std::string& str, StrMap& map) {
  if (str.size() == 0)
    return;
  std::istringstream ist(str);
  ParseCheck(ist, '{');
  for (;;) {
    std::string key = ParseString(ist);
    ParseCheck(ist, '=');
    std::string val = ParseString(ist);
    map[key] = val;
    if (ist.peek() == ',')
      ist.get();
    else
      break;
  }
  ParseCheck(ist, '}');
  if (ist.rdbuf()->in_avail() != 0)
    LOG(LERROR) << "Unexpected characters at end";
}

}  // namespace

namespace talk_base {

NetworkManager::~NetworkManager() {
  for (NetworkMap::iterator i = networks_.begin(); i != networks_.end(); ++i)
    delete i->second;
}

bool NetworkManager::GetNetworks(std::vector<Network*>* result) {
  std::vector<Network*> list;
  if (!EnumNetworks(false, &list)) {
    return false;
  }

  for (uint32 i = 0; i < list.size(); ++i) {
    NetworkMap::iterator iter = networks_.find(list[i]->name());

    Network* network;
    if (iter == networks_.end()) {
      network = list[i];
    } else {
      network = iter->second;
      network->set_ip(list[i]->ip());
      network->set_gateway_ip(list[i]->gateway_ip());
      delete list[i];
    }

    networks_[network->name()] = network;
    result->push_back(network);
  }
  return true;
}

void NetworkManager::DumpNetworks(bool include_ignored) {
  std::vector<Network*> list;
  EnumNetworks(include_ignored, &list);
  LOG(LS_INFO) << "NetworkManager detected " << list.size() << " networks:";
  for (size_t i = 0; i < list.size(); ++i) {
    const Network* network = list[i];
    if (!network->ignored() || include_ignored) {
      LOG(LS_INFO) << network->ToString() << ": " << network->description()
                   << ", Gateway="
                   << SocketAddress::IPToString(network->gateway_ip())
                   << ((network->ignored()) ? ", Ignored" : "");
    }
  }
}

std::string NetworkManager::GetState() const {
  StrMap map;
  for (NetworkMap::const_iterator i = networks_.begin();
       i != networks_.end(); ++i)
    map[i->first] = i->second->GetState();

  std::string str;
  BuildMap(map, str);
  return str;
}

void NetworkManager::SetState(const std::string& str) {
  StrMap map;
  ParseMap(str, map);

  for (StrMap::iterator i = map.begin(); i != map.end(); ++i) {
    std::string name = i->first;
    std::string state = i->second;

    Network* network = new Network(name, "", 0, 0);
    network->SetState(state);
    networks_[name] = network;
  }
}

#ifdef POSIX
// Gets the default gateway for the specified interface.
uint32 GetDefaultGateway(const std::string& name) {
#ifdef OSX
  // TODO: /proc/net/route doesn't exist, 
  // Use ioctl to get the routing table
  return 0xFFFFFFFF;
#endif

  uint32 gateway_ip = 0;

  FileStream fs;
  if (fs.Open("/proc/net/route", "r")) {
    std::string line;
    while (fs.ReadLine(&line) == SR_SUCCESS && gateway_ip == 0) {
      char iface[16];
      unsigned int ip, gw;
      if (sscanf(line.c_str(), "%7s %8X %8X", iface, &ip, &gw) == 3 &&
          name == iface && ip == 0) {
        gateway_ip = ntohl(gw);
      }
    }
  }

  return gateway_ip;
}


bool NetworkManager::CreateNetworks(bool include_ignored,
                                    std::vector<Network*>* networks) {
  int fd;
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    LOG_ERR(LERROR) << "socket";
    return false;
  }

  struct ifconf ifc;
  ifc.ifc_len = 64 * sizeof(struct ifreq);
  ifc.ifc_buf = new char[ifc.ifc_len];

  if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
    LOG_ERR(LERROR) << "ioctl";
    return false;
  }
  assert(ifc.ifc_len < static_cast<int>(64 * sizeof(struct ifreq)));

  struct ifreq* ptr = reinterpret_cast<struct ifreq*>(ifc.ifc_buf);
  struct ifreq* end =
      reinterpret_cast<struct ifreq*>(ifc.ifc_buf + ifc.ifc_len);

  while (ptr < end) {
    struct sockaddr_in* inaddr =
        reinterpret_cast<struct sockaddr_in*>(&ptr->ifr_ifru.ifru_addr);
    if (inaddr->sin_family == AF_INET) {
      uint32 ip = ntohl(inaddr->sin_addr.s_addr);
      scoped_ptr<Network> network(
          new Network(ptr->ifr_name, ptr->ifr_name, ip,
                      GetDefaultGateway(ptr->ifr_name)));
      network->set_ignored(IsIgnoredNetwork(*network));
      if (include_ignored || !network->ignored()) {
        networks->push_back(network.release());
      }
    }

#ifdef _SIZEOF_ADDR_IFREQ
    ptr = reinterpret_cast<struct ifreq*>(
        reinterpret_cast<char*>(ptr) + _SIZEOF_ADDR_IFREQ(*ptr));
#else
    ptr++;
#endif
  }

  delete [] ifc.ifc_buf;
  close(fd);
  return true;
}
#endif  // POSIX

#ifdef WIN32
bool NetworkManager::CreateNetworks(bool include_ignored,
                                    std::vector<Network*>* networks) {
  IP_ADAPTER_INFO info_temp;
  ULONG len = 0;

  if (GetAdaptersInfo(&info_temp, &len) != ERROR_BUFFER_OVERFLOW)
    // This just means there's zero networks, which is not an error.
    return true;

  scoped_array<char> buf(new char[len]);
  IP_ADAPTER_INFO *infos = reinterpret_cast<IP_ADAPTER_INFO *>(buf.get());
  DWORD ret = GetAdaptersInfo(infos, &len);
  if (ret != NO_ERROR) {
    LOG_ERR_EX(LS_ERROR, ret) << "GetAdaptersInfo failed";
    return false;
  }

  int count = 0;
  for (IP_ADAPTER_INFO *info = infos; info != NULL; info = info->Next) {
    // Ignore the loopback device.
    if (info->Type == MIB_IF_TYPE_LOOPBACK) {
      continue;
    }

    // In non-debug builds, don't transmit the network name because of
    // privacy concerns. Transmit a number instead.
    std::string name;
#ifdef _DEBUG
    name = info->Description;
#else  // !_DEBUG
    std::ostringstream ost;
    ost << count;
    name = ost.str();
    count++;
#endif  // !_DEBUG

    scoped_ptr<Network> network(new Network(name, info->Description,
        SocketAddress::StringToIP(info->IpAddressList.IpAddress.String),
        SocketAddress::StringToIP(info->GatewayList.IpAddress.String)));
    network->set_ignored(IsIgnoredNetwork(*network));
    if (include_ignored || !network->ignored()) {
      networks->push_back(network.release());
    }
  }

  return true;
}
#endif  // WIN32

bool NetworkManager::IsIgnoredNetwork(const Network& network) {
#ifdef POSIX
  // Ignore local networks (lo, lo0, etc)
  // Also filter out VMware interfaces, typically named vmnet1 and vmnet8
  if (strncmp(network.name().c_str(), "lo", 2) == 0 ||
      strncmp(network.name().c_str(), "vmnet", 5) == 0) {
    return true;
  }
#elif defined(WIN32)
  // Ignore any HOST side vmware adapters with a description like:
  // VMware Virtual Ethernet Adapter for VMnet1
  // but don't ignore any GUEST side adapters with a description like:
  // VMware Accelerated AMD PCNet Adapter #2
  if (strstr(network.description().c_str(), "VMnet") != NULL) {
    return true;
  }
#endif

  // Ignore any networks with a 0.x.y.z IP
  return (network.ip() < 0x01000000);
}

bool NetworkManager::EnumNetworks(bool include_ignored,
                                  std::vector<Network*>* result) {
  return CreateNetworks(include_ignored, result);
}


Network::Network(const std::string& name, const std::string& desc,
                 uint32 ip, uint32 gateway_ip)
    : name_(name), description_(desc), ip_(ip), gateway_ip_(gateway_ip),
      ignored_(false), uniform_numerator_(0), uniform_denominator_(0),
      exponential_numerator_(0), exponential_denominator_(0),
      quality_(kDefaultQuality) {
  last_data_time_ = Time();

  // TODO: seed the historical data with one data point based
  // on the link speed metric from XP (4.0 if < 50, 3.0 otherwise).
}

void Network::StartSession(NetworkSession* session) {
  assert(std::find(sessions_.begin(), sessions_.end(), session) ==
         sessions_.end());
  sessions_.push_back(session);
}

void Network::StopSession(NetworkSession* session) {
  SessionList::iterator iter =
      std::find(sessions_.begin(), sessions_.end(), session);
  if (iter != sessions_.end())
    sessions_.erase(iter);
}

void Network::EstimateQuality() {
  uint32 now = Time();

  // Add new data points for the current time.
  for (uint32 i = 0; i < sessions_.size(); ++i) {
    if (sessions_[i]->HasQuality())
      AddDataPoint(now, sessions_[i]->GetCurrentQuality());
  }

  // Construct the weighted average using both uniform and exponential weights.

  double exp_shift = exp(-kLambda * (now - last_data_time_));
  double numerator = uniform_numerator_ + exp_shift * exponential_numerator_;
  double denominator = uniform_denominator_ + exp_shift *
                       exponential_denominator_;

  if (denominator < DBL_EPSILON)
    quality_ = kDefaultQuality;
  else
    quality_ = numerator / denominator;
}

std::string Network::ToString() const {
  std::stringstream ss;
  // Print out the first space-terminated token of the network desc, plus
  // the IP address.
  ss << "Net[" << description_.substr(0, description_.find(' '))
     << ":" << SocketAddress::IPToString(ip_) << "]";
  return ss.str();
}

void Network::AddDataPoint(uint32 time, double quality) {
  uniform_numerator_ += kAlpha * quality;
  uniform_denominator_ += kAlpha;

  double exp_shift = exp(-kLambda * (time - last_data_time_));
  exponential_numerator_ = (1 - kAlpha) * quality + exp_shift *
                           exponential_numerator_;
  exponential_denominator_ = (1 - kAlpha) + exp_shift *
                             exponential_denominator_;

  last_data_time_ = time;
}

std::string Network::GetState() const {
  StrMap map;
  map["lt"] = talk_base::ToString<uint32>(last_data_time_);
  map["un"] = talk_base::ToString<double>(uniform_numerator_);
  map["ud"] = talk_base::ToString<double>(uniform_denominator_);
  map["en"] = talk_base::ToString<double>(exponential_numerator_);
  map["ed"] = talk_base::ToString<double>(exponential_denominator_);

  std::string str;
  BuildMap(map, str);
  return str;
}

void Network::SetState(const std::string& str) {
  StrMap map;
  ParseMap(str, map);

  last_data_time_ = FromString<uint32>(map["lt"]);
  uniform_numerator_ = FromString<double>(map["un"]);
  uniform_denominator_ = FromString<double>(map["ud"]);
  exponential_numerator_ = FromString<double>(map["en"]);
  exponential_denominator_ = FromString<double>(map["ed"]);
}

}  // namespace talk_base
