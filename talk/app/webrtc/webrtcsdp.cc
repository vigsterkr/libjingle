/*
 * libjingle
 * Copyright 2011, Google Inc.
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

#include "talk/app/webrtc/webrtcsdp.h"

#include <stdio.h>
#include <string>
#include <vector>

#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"
#include "talk/p2p/base/candidate.h"
#include "talk/p2p/base/constants.h"
#include "talk/p2p/base/relayport.h"
#include "talk/p2p/base/stunport.h"
#include "talk/p2p/base/udpport.h"
#include "talk/session/phone/codec.h"
#include "talk/session/phone/cryptoparams.h"
#include "talk/session/phone/mediasession.h"
#include "talk/session/phone/mediasessionclient.h"

using cricket::AudioContentDescription;
using cricket::Candidate;
using cricket::Candidates;
using cricket::ContentDescription;
using cricket::ContentInfo;
using cricket::CryptoParams;
using cricket::ICE_CANDIDATE_COMPONENT_RTP;
using cricket::ICE_CANDIDATE_COMPONENT_RTCP;
using cricket::MediaContentDescription;
using cricket::MediaType;
using cricket::NS_GINGLE_P2P;
using cricket::StreamParams;
using cricket::TransportInfo;
using cricket::VideoContentDescription;
using talk_base::SocketAddress;

namespace webrtc {

// Line type
// RFC 4566
// An SDP session description consists of a number of lines of text of
// the form:
// <type>=<value>
// where <type> MUST be exactly one case-significant character.
static const int kLinePrefixLength = 2;  // Lenght of <type>=
static const char kLineTypeVersion = 'v';
static const char kLineTypeOrigin = 'o';
static const char kLineTypeSessionName = 's';
static const char kLineTypeSessionInfo = 'i';
static const char kLineTypeSessionUri = 'u';
static const char kLineTypeSessionEmail = 'e';
static const char kLineTypeSessionPhone = 'p';
static const char kLineTypeSessionBandwidth = 'b';
static const char kLineTypeTiming = 't';
static const char kLineTypeRepeatTimes = 'r';
static const char kLineTypeTimeZone = 'z';
static const char kLineTypeEncryptionKey = 'k';
static const char kLineTypeMedia = 'm';
static const char kLineTypeConnection = 'c';
static const char kLineTypeAttributes = 'a';

// Attributes
static const char kAttributeGroup[] = "group";
static const char kAttributeMid[] = "mid";
static const char kAttributeRtcpMux[] = "rtcp-mux";
static const char kAttributeSsrc[] = "ssrc";
static const char kSsrcAttributeCname[] = "cname";
static const char kSsrcAttributeMslabel[] = "mslabel";
static const char kSSrcAttributeLabel[] = "label";
static const char kAttributeCrypto[] = "crypto";
static const char kAttributeCandidate[] = "candidate";
static const char kAttributeCandidateTyp[] = "typ";
static const char kAttributeCandidateRaddr[] = "raddr";
static const char kAttributeCandidateRport[] = "rport";
static const char kAttributeCandidateGeneration[] = "generation";
static const char kAttributeRtpmap[] = "rtpmap";
static const char kAttributeRtcp[] = "rtcp";
static const char kAttributeIceUfrag[] = "ice-ufrag";
static const char kAttributeIcePwd[] = "ice-pwd";
static const char kAttributeSendOnly[] ="sendonly";
static const char kAttributeRecvOnly[] ="recvonly";
static const char kAttributeSendRecv[] ="sendrecv";
static const char kAttributeInactive[] ="inactive";

// Candidate
static const char kCandidateHost[] = "host";
static const char kCandidateSrflx[] = "srflx";
// TODO: How to map the prflx with circket candidate type
// static const char kCandidatePrflx[] = "prflx";
static const char kCandidateRelay[] = "relay";

static const char kSdpDelimiterEqual = '=';
static const char kSdpDelimiterSpace = ' ';
static const char kSdpDelimiterColon = ':';
static const char kLineBreak[] = "\r\n";

// TODO: Generate the Session and Time description
// instead of hardcoding.
static const char kSessionVersion[] = "v=0";
// RFC 4566
static const char kSessionOriginUsername[] = "-";
static const char kSessionOriginSessionId[] = "0";
static const char kSessionOriginSessionVersion[] = "0";
static const char kSessionOriginNettype[] = "IN";
static const char kSessionOriginAddrtype[] = "IP4";
static const char kSessionOriginAddress[] = "127.0.0.1";
static const char kSessionName[] = "s=";
static const char kTimeDescription[] = "t=0 0";
static const char kAttrGroup[] = "a=group:BUNDLE";
static const char kConnectionNettype[] = "IN";
static const char kConnectionAddrtype[] = "IP4";
static const char kMediaTypeVideo[] = "video";
static const char kMediaTypeAudio[] = "audio";
static const char kMediaPortPlaceholder = 1;
static const char kMediaProtocolAvpf[] = "RTP/AVPF";
static const char kMediaProtocolSavpf[] = "RTP/SAVPF";
static const char kDefaultAddress[] = "0.0.0.0";
static const char kDefaultPort[] = "1";

// Default Video resolution.
// TODO: Implement negotiation of video resolution.
static const int kDefaultVideoWidth = 640;
static const int kDefaultVideoHeight = 480;
static const int kDefaultVideoFrameRate = 30;
static const int kDefaultVideoPreference = 0;
static const int kDefaultVideoClockrate = 90000;

// Serializes the passed in SessionDescription to a SDP string.
// desc - The SessionDescription object to be serialized.
static std::string SdpSerializeSessionDescription(
    const JsepSessionDescription& jdesc);

static void BuildMediaDescription(const ContentInfo* content_info,
                                  const TransportInfo* transport_info,
                                  const MediaType media_type,
                                  std::string* message);
static void BuildRtpMap(const MediaContentDescription* media_desc,
                        const MediaType media_type,
                        std::string* message);
static void BuildCandidate(const std::vector<Candidate>& candidates,
                           std::string* message);

static bool ParseSessionDescription(const std::string& message, size_t* pos,
                                    std::string* session_id,
                                    std::string* session_version,
                                    std::string* session_ice_ufrag,
                                    std::string* session_ice_pwd,
                                    cricket::SessionDescription* desc);
static bool ParseGroupAttribute(const std::string& line,
                                cricket::SessionDescription* desc);
static bool ParseMediaDescription(const std::string& message,
                                  const std::string& session_ice_ufrag,
                                  const std::string& session_ice_pwd,
                                  size_t* pos,
                                  cricket::SessionDescription* desc,
                                  std::vector<JsepIceCandidate*>* candidates);
static bool ParseContent(const std::string& message,
                         const MediaType media_type,
                         int mline_index,
                         size_t* pos,
                         ContentDescription* content,
                         std::string* content_name,
                         std::string* media_ice_ufrag,
                         std::string* media_ice_pwd,
                         std::vector<JsepIceCandidate*>* candidates);
static bool ParseSsrcAttribute(const std::string& line,
                               MediaContentDescription* media_desc);
static bool ParseCryptoAttribute(const std::string& line,
                                 MediaContentDescription* media_desc);
static bool ParseRtpmapAttribute(const std::string& line,
                                 const MediaType media_type,
                                 MediaContentDescription* media_desc);
static bool ParseCandidate(const std::string& message, Candidate* candidate);

// Helper functions
#define LOG_PREFIX_PARSING_ERROR(line_type) LOG(LS_ERROR) \
    << "Failed to parse the \"" << line_type << "\" line";

#define LOG_LINE_PARSING_ERROR(line) LOG(LS_ERROR) \
    << "Failed to parse line:" << line;

static bool AddLine(const std::string& line, std::string* message) {
  if (!message)
    return false;

  message->append(line);
  message->append(kLineBreak);
  return true;
}

static bool GetLine(const std::string& message,
                    size_t* pos,
                    std::string* line) {
  size_t line_begin = *pos;
  size_t line_end = message.find('\n', line_begin);
  if (line_end == std::string::npos) {
    return false;
  }
  // Update the new start position
  *pos = line_end + 1;
  if (line_end > 0 && (message.at(line_end - 1) == '\r')) {
    --line_end;
  }
  *line = message.substr(line_begin, (line_end - line_begin));
  const char* cline = line->c_str();
  // RFC 4566
  // An SDP session description consists of a number of lines of text of
  // the form:
  // <type>=<value>
  // where <type> MUST be exactly one case-significant character and
  // <value> is structured text whose format depends on <type>.
  // Whitespace MUST NOT be used on either side of the "=" sign.
  if (cline[0] == kSdpDelimiterSpace ||
      cline[1] != kSdpDelimiterEqual ||
      cline[2] == kSdpDelimiterSpace) {
    LOG_LINE_PARSING_ERROR(*line);
    return false;
  }
  return true;
}

static bool IsLineType(const std::string& message,
                       const char type,
                       size_t line_start) {
  if (message.size() < line_start + kLinePrefixLength) {
    return false;
  }
  const char* cmessage = message.c_str();
  return (cmessage[line_start] == type &&
          cmessage[line_start + 1] == kSdpDelimiterEqual);
}

static bool IsLineType(const std::string& line,
                       const char type) {
  return IsLineType(line, type, 0);
}

static bool GetLineWithType(const std::string& message, size_t* pos,
                            std::string* line, const char type) {
  if (!IsLineType(message, type, *pos)) {
    return false;
  }

  if (!GetLine(message, pos, line))
    return false;

  return true;
}

static bool HasAttribute(const std::string& line,
                         const std::string& attribute) {
  return (line.compare(kLinePrefixLength, attribute.size(), attribute) == 0);
}

// Init the |os| to "|type|=|value|".
static void InitLine(const char type,
                     const std::string& value,
                     std::ostringstream* os) {
  os->str("");
  *os << type << kSdpDelimiterEqual << value;
}

// Init the |os| to "a=|attribute|".
static void InitAttrLine(const std::string& attribute, std::ostringstream* os) {
  InitLine(kLineTypeAttributes, attribute, os);
}

static bool AddSsrcLine(uint32 ssrc_id, const std::string& attribute,
                        const std::string& value, std::string* message) {
  // RFC 5576
  // a=ssrc:<ssrc-id> <attribute>:<value>
  std::ostringstream os;
  InitAttrLine(kAttributeSsrc, &os);
  os << kSdpDelimiterColon << ssrc_id << kSdpDelimiterSpace
     << attribute << kSdpDelimiterColon << value;
  return AddLine(os.str(), message);
}

// Split the message into two parts by the first delimiter.
static bool SplitByDelimiter(const std::string& message,
                             const char delimiter,
                             std::string* field1,
                             std::string* field2) {
  // Find the first delimiter
  size_t pos = message.find(delimiter);
  if (pos == std::string::npos) {
    return false;
  }
  *field1 = message.substr(0, pos);
  // The rest is the value.
  *field2 = message.substr(pos + 1);
  return true;
}

// Get value only from <attribute>:<value>.
static bool GetValue(const std::string& message, const std::string& attribute,
                     std::string* value) {
  std::string leftpart;
  if (!SplitByDelimiter(message, kSdpDelimiterColon, &leftpart, value)) {
    return false;
  }
  // The left part should be end with the expected attribute.
  if (leftpart.length() < attribute.length() ||
      leftpart.compare(leftpart.length() - attribute.length(),
                       attribute.length(), attribute) != 0) {
    return false;
  }
  return true;
}

// RFC 5245
// It is RECOMMENDED that default candidates be chosen based on the
// likelihood of those candidates to work with the peer that is being
// contacted.  It is RECOMMENDED that relayed > reflexive > host.
static const int kPreferenceUnknown = 0;
static const int kPreferenceHost = 1;
static const int kPreferenceReflexive = 2;
static const int kPreferenceRelayed = 3;

static int GetCandidatePreferenceFromType(const std::string& type) {
  int preference = kPreferenceUnknown;
  if (type == cricket::LOCAL_PORT_TYPE) {
    preference = kPreferenceHost;
  } else if (type == cricket::STUN_PORT_TYPE) {
    preference = kPreferenceReflexive;
  } else if (type == cricket::RELAY_PORT_TYPE) {
    preference = kPreferenceRelayed;
  } else {
    ASSERT(false);
  }
  return preference;
}

// Get ip and port of the default destination from the |candidates| with
// the given value of |component_id|.
// RFC 5245
// The value of |component_id| currently supported are 1 (RTP) and 2 (RTCP).
// TODO: Decide the default destination in webrtcsession and
// pass it down via SessionDescription.
static bool GetDefaultDestination(const std::vector<Candidate>& candidates,
    int component_id, std::string* port, std::string* ip) {
  *port = kDefaultPort;
  *ip = kDefaultAddress;
  int current_preference = kPreferenceUnknown;
  for (std::vector<Candidate>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    if (it->component() != component_id) {
      continue;
    }
    const int preference = GetCandidatePreferenceFromType(it->type());
    // See if this candidate is more preferable then the current one.
    if (preference <= current_preference) {
      continue;
    }
    current_preference = preference;
    *port = it->address().PortAsString();
    *ip = it->address().ipaddr().ToString();
  }
  return true;
}

// Update the media default destination.
static void UpdateMediaDefaultDestination(
    const std::vector<Candidate>& candidates, std::string* mline) {
  std::ostringstream os;
  std::string rtp_port, rtp_ip;
  if (GetDefaultDestination(candidates, ICE_CANDIDATE_COMPONENT_RTP,
                            &rtp_port, &rtp_ip)) {
    // Found default RTP candidate.
    // RFC 5245
    // The default candidates are added to the SDP as the default
    // destination for media.  For streams based on RTP, this is done by
    // placing the IP address and port of the RTP candidate into the c and m
    // lines, respectively.

    // Update the port in the m line.
    // RFC 4566
    // m=<media> <port> <proto> <fmt> ...
    const size_t first_space = mline->find(kSdpDelimiterSpace);
    const size_t second_space =
        mline->find(kSdpDelimiterSpace, first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos)
      return;
    mline->replace(first_space + 1,
                   second_space - first_space -1,
                   rtp_port);
    // Add the c line.
    // RFC 4566
    // c=<nettype> <addrtype> <connection-address>
    InitLine(kLineTypeConnection, kConnectionNettype, &os);
    os << " " << kConnectionAddrtype << " " << rtp_ip;
    AddLine(os.str(), mline);
  }

  std::string rtcp_port, rtcp_ip;
  if (GetDefaultDestination(candidates, ICE_CANDIDATE_COMPONENT_RTCP,
                            &rtcp_port, &rtcp_ip)) {
    // Found default RTCP candidate.
    // RFC 5245
    // If the agent is utilizing RTCP, it MUST encode the RTCP candidate
    // using the a=rtcp attribute as defined in RFC 3605.

    // RFC 3605
    // rtcp-attribute =  "a=rtcp:" port  [nettype space addrtype space
    // connection-address] CRLF
    InitAttrLine(kAttributeRtcp, &os);
    os << kSdpDelimiterColon
       << rtcp_port << " "
       << kConnectionNettype << " "
       << kConnectionAddrtype << " "
       << rtcp_ip;
    AddLine(os.str(), mline);
  }
}

// Get candidates according to the mline index from SessionDescriptionInterface.
static void GetCandidatesByMindex(const SessionDescriptionInterface& desci,
                                  int mline_index,
                                  std::vector<Candidate>* candidates) {
  if (!candidates) {
    return;
  }
  const IceCandidateCollection* cc = desci.candidates(mline_index);
  for (size_t i = 0; i < cc->count(); ++i) {
    const IceCandidateInterface* candidate = cc->at(i);
    candidates->push_back(candidate->candidate());
  }
}

std::string SdpSerialize(const JsepSessionDescription& jdesc) {
  std::string sdp = SdpSerializeSessionDescription(jdesc);

  std::string sdp_with_candidates;
  size_t pos = 0;
  std::string line;
  int mline_index = -1;
  while (GetLine(sdp, &pos, &line)) {
    if (IsLineType(line, kLineTypeMedia)) {
      ++mline_index;
      std::vector<Candidate> candidates;
      GetCandidatesByMindex(jdesc, mline_index, &candidates);
      // Media line may append other lines inside the
      // UpdateMediaDefaultDestination call, so add the kLineBreak here first.
      line.append(kLineBreak);
      UpdateMediaDefaultDestination(candidates, &line);
      sdp_with_candidates.append(line);
      // Build the a=candidate lines.
      BuildCandidate(candidates, &sdp_with_candidates);
    } else {
      // Copy old line to new sdp without change.
      AddLine(line, &sdp_with_candidates);
    }
  }
  sdp = sdp_with_candidates;

  return sdp;
}

std::string SdpSerializeSessionDescription(
    const JsepSessionDescription& jdesc) {
  const cricket::SessionDescription* desc = jdesc.description();
  if (!desc) {
    return "";
  }

  std::string message;

  // Session Description.
  AddLine(kSessionVersion, &message);
  // Session Origin
  // RFC 4566
  // o=<username> <sess-id> <sess-version> <nettype> <addrtype>
  // <unicast-address>
  std::ostringstream os;
  InitLine(kLineTypeOrigin, kSessionOriginUsername, &os);
  const std::string session_id = jdesc.session_id().empty() ?
      kSessionOriginSessionId : jdesc.session_id();
  const std::string session_version = jdesc.session_version().empty() ?
      kSessionOriginSessionVersion : jdesc.session_version();
  os << " " << session_id << " " << session_version << " "
     << kSessionOriginNettype << " " << kSessionOriginAddrtype << " "
     << kSessionOriginAddress;
  AddLine(os.str(), &message);
  AddLine(kSessionName, &message);

  // Time Description.
  AddLine(kTimeDescription, &message);

  // Group
  if (desc->HasGroup(cricket::GROUP_TYPE_BUNDLE)) {
    std::string group_line = kAttrGroup;
    const cricket::ContentGroup* group =
        desc->GetGroupByName(cricket::GROUP_TYPE_BUNDLE);
    ASSERT(group != NULL);
    const std::set<std::string>& content_types = group->content_types();
    for (std::set<std::string>::const_iterator it = content_types.begin();
         it != content_types.end(); ++it) {
      group_line.append(" ");
      group_line.append(*it);
    }
    AddLine(group_line, &message);
  }

  // Media Description
  const ContentInfo* audio_content = GetFirstAudioContent(desc);
  if (audio_content) {
    BuildMediaDescription(audio_content,
                          desc->GetTransportInfoByName(audio_content->name),
                          cricket::MEDIA_TYPE_AUDIO, &message);
  }

  const ContentInfo* video_content = GetFirstVideoContent(desc);
  if (video_content) {
    BuildMediaDescription(video_content,
                          desc->GetTransportInfoByName(video_content->name),
                          cricket::MEDIA_TYPE_VIDEO, &message);
  }

  return message;
}

// Serializes the passed in IceCandidateInterface to a SDP string.
// candidate - The candidate to be serialized.
std::string SdpSerializeCandidate(
    const IceCandidateInterface& candidate) {
  std::string message;
  std::vector<cricket::Candidate> candidates;
  candidates.push_back(candidate.candidate());
  BuildCandidate(candidates, &message);
  return message;
}

bool SdpDeserialize(const std::string& message,
                    JsepSessionDescription* jdesc) {
  std::string session_id;
  std::string session_version;
  std::string session_ice_ufrag;
  std::string session_ice_pwd;
  cricket::SessionDescription* desc = new cricket::SessionDescription();
  std::vector<JsepIceCandidate*> candidates;
  size_t current_pos = 0;

  // Session Description
  if (!ParseSessionDescription(message, &current_pos,
                               &session_id, &session_version,
                               &session_ice_ufrag, &session_ice_pwd, desc)) {
    delete desc;
    return false;
  }

  // Media Description
  if (!ParseMediaDescription(message, session_ice_ufrag, session_ice_pwd,
                             &current_pos, desc, &candidates)) {
    delete desc;
    for (std::vector<JsepIceCandidate*>::const_iterator
         it = candidates.begin(); it != candidates.end(); ++it) {
      delete *it;
    }
    return false;
  }

  jdesc->Initialize(desc, session_id, session_version);

  for (std::vector<JsepIceCandidate*>::const_iterator
       it = candidates.begin(); it != candidates.end(); ++it) {
    jdesc->AddCandidate(*it);
    delete *it;
  }
  return true;
}

bool SdpDeserializeCandidate(const std::string& message,
    JsepIceCandidate* jcandidate) {
  ASSERT(jcandidate != NULL);
  Candidate candidate;
  if (!ParseCandidate(message, &candidate)) {
    return false;
  }
  jcandidate->SetCandidate(candidate);
  return true;
}

bool ParseCandidate(const std::string& message, Candidate* candidate) {
  ASSERT(candidate != NULL);

  if (!IsLineType(message, kLineTypeAttributes) ||
      !HasAttribute(message, kAttributeCandidate)) {
    // Must start with a=candidate line
    return false;
  }
  std::vector<std::string> fields;
  talk_base::split(message.substr(kLinePrefixLength),
                   kSdpDelimiterSpace, &fields);
  // RFC 5245
  // a=candidate:<foundation> <component-id> <transport> <priority>
  // <connection-address> <port> typ <candidate-types>
  // [raddr <connection-address>] [rport <port>]
  // *(SP extension-att-name SP extension-att-value)
  // 8 mandatory fields
  const size_t mandatory_fields_num = 8;
  if (fields.size() < mandatory_fields_num ||
      (fields[6] != kAttributeCandidateTyp)) {
    LOG_LINE_PARSING_ERROR(message);
    return false;
  }
  std::string foundation_str;
  if (!GetValue(fields[0], kAttributeCandidate, &foundation_str)) {
    return false;
  }
  const uint32 foundation = talk_base::FromString<uint32>(foundation_str);
  const int component_id = talk_base::FromString<int>(fields[1]);
  const std::string transport = fields[2];
  const uint32 priority = talk_base::FromString<uint32>(fields[3]);
  const std::string connection_address = fields[4];
  const int port = talk_base::FromString<int>(fields[5]);
  SocketAddress address(connection_address, port);

  std::string candidate_type;
  const std::string type = fields[7];
  if (type == kCandidateHost) {
    candidate_type = cricket::LOCAL_PORT_TYPE;
  } else if (type == kCandidateSrflx) {
    candidate_type = cricket::STUN_PORT_TYPE;
  } else if (type == kCandidateRelay) {
    candidate_type = cricket::RELAY_PORT_TYPE;
  } else {
    LOG(LS_ERROR) << "Unsupported candidate type from message: " << message;
    return false;
  }

  size_t current_position = mandatory_fields_num;
  SocketAddress related_address;
  // The 2 optional fields for related address
  // [raddr <connection-address>] [rport <port>]
  if (fields.size() >= (current_position + 2) &&
      fields[current_position] == kAttributeCandidateRaddr) {
    related_address.SetIP(fields[++current_position]);
    ++current_position;
  }
  if (fields.size() >= (current_position + 2) &&
      fields[current_position] == kAttributeCandidateRport) {
    related_address.SetPort(
        talk_base::FromString<int>(fields[++current_position]));
    ++current_position;
  }

  // Extension
  uint32 generation = 0;
  for (size_t i = current_position; i + 1 < fields.size(); ++i) {
    // RFC 5245
    // *(SP extension-att-name SP extension-att-value)
    if (fields[i] == kAttributeCandidateGeneration) {
      generation = talk_base::FromString<uint32>(fields[++i]);
    } else {
      // Skip the unknown extension.
      ++i;
    }
  }

  // Empty string as the candidate id and network name.
  const std::string id;
  const std::string network_name;
  // Empty string as the candidate username and password.
  // Will be updated later with the ice-ufrag and ice-pwd.
  const std::string username;
  const std::string password;
  *candidate = Candidate(id, component_id, transport, address, priority,
      username, password, candidate_type, network_name, generation,
      foundation);
  candidate->set_related_address(related_address);
  return true;
}

void BuildMediaDescription(const ContentInfo* content_info,
                           const TransportInfo* transport_info,
                           const MediaType media_type,
                           std::string* message) {
  ASSERT(message != NULL);
  if (content_info == NULL || message == NULL) {
    return;
  }
  // TODO: Rethink if we should use sprintfn instead of stringstream.
  // According to the style guide, streams should only be used for logging.
  // http://google-styleguide.googlecode.com/svn/
  // trunk/cppguide.xml?showone=Streams#Streams
  std::ostringstream os;
  const MediaContentDescription* media_desc =
      static_cast<const MediaContentDescription*> (
          content_info->description);
  ASSERT(media_desc != NULL);

  // RFC 4566
  // m=<media> <port> <proto> <fmt>
  // fmt is a list of payload type numbers that MAY be used in the session.
  const char* type = NULL;
  if (media_type == cricket::MEDIA_TYPE_AUDIO)
    type = kMediaTypeAudio;
  else if (media_type == cricket::MEDIA_TYPE_VIDEO)
    type = kMediaTypeVideo;
  else
    ASSERT(false);

  std::string fmt;
  if (media_type == cricket::MEDIA_TYPE_VIDEO) {
    const VideoContentDescription* video_desc =
        static_cast<const VideoContentDescription*>(media_desc);
    for (std::vector<cricket::VideoCodec>::const_iterator it =
             video_desc->codecs().begin();
         it != video_desc->codecs().end(); ++it) {
      fmt.append(" ");
      fmt.append(talk_base::ToString<int>(it->id));
    }
  } else if (media_type == cricket::MEDIA_TYPE_AUDIO) {
    const AudioContentDescription* audio_desc =
        static_cast<const AudioContentDescription*>(media_desc);
    for (std::vector<cricket::AudioCodec>::const_iterator it =
             audio_desc->codecs().begin();
         it != audio_desc->codecs().end(); ++it) {
      fmt.append(" ");
      fmt.append(talk_base::ToString<int>(it->id));
    }
  }
  // The port number in the m line will be updated later when associate with
  // the candidates.
  const int port = kMediaPortPlaceholder;
  const char* proto = kMediaProtocolAvpf;
  // RFC 4568
  // SRTP security descriptions MUST only be used with the SRTP transport.
  if (media_desc->cryptos().size() > 0) {
    proto = kMediaProtocolSavpf;
  }
  InitLine(kLineTypeMedia, type, &os);
  os << " " << port << " " << proto << fmt;
  AddLine(os.str(), message);

  // Use the transport_info to build the media level ice-ufrag and ice-pwd.
  if (transport_info) {
    // RFC 5245
    // ice-pwd-att           = "ice-pwd" ":" password
    // ice-ufrag-att         = "ice-ufrag" ":" ufrag
    // ice-ufrag
    InitAttrLine(kAttributeIceUfrag, &os);
    os << kSdpDelimiterColon << transport_info->ice_ufrag;
    AddLine(os.str(), message);
    // ice-pwd
    InitAttrLine(kAttributeIcePwd, &os);
    os << kSdpDelimiterColon << transport_info->ice_pwd;
    AddLine(os.str(), message);
  }

  // RFC 3264
  // a=sendrecv || a=sendonly || a=sendrecv || a=inactive
  switch (media_desc->direction()) {
    case cricket::MD_INACTIVE:
      InitAttrLine(kAttributeInactive, &os);
      break;
    case cricket::MD_SENDONLY:
      InitAttrLine(kAttributeSendOnly, &os);
      break;
    case cricket::MD_RECVONLY:
      InitAttrLine(kAttributeRecvOnly, &os);
      break;
    case cricket::MD_SENDRECV:
    default:
      InitAttrLine(kAttributeSendRecv, &os);
      break;
  }
  AddLine(os.str(), message);

  // RFC 3388
  // mid-attribute      = "a=mid:" identification-tag
  // identification-tag = token
  // Use the content name as the mid identification-tag.
  InitAttrLine(kAttributeMid, &os);
  os << kSdpDelimiterColon << content_info->name;
  AddLine(os.str(), message);

  // RFC 5761
  // a=rtcp-mux
  if (media_desc->rtcp_mux()) {
    InitAttrLine(kAttributeRtcpMux, &os);
    AddLine(os.str(), message);
  }

  // RFC 4568
  // a=crypto:<tag> <crypto-suite> <key-params> [<session-params>]
  for (std::vector<CryptoParams>::const_iterator it =
           media_desc->cryptos().begin();
       it != media_desc->cryptos().end(); ++it) {
    InitAttrLine(kAttributeCrypto, &os);
    os << kSdpDelimiterColon << it->tag << " " << it->cipher_suite << " "
       << it->key_params << " " << it->session_params;
    AddLine(os.str(), message);
  }

  // RFC 4566
  // a=rtpmap:<payload type> <encoding name>/<clock rate>
  // [/<encodingparameters>]
  BuildRtpMap(media_desc, media_type, message);

  // RFC 5576 and draft-alvestrand-rtcweb-msid
  // a=ssrc:<ssrc-id> <attribute>:<value>
  // a=ssrc:<ssrc-id> cname:<value>
  // draft-alvestrand-rtcweb-mid-01
  // a=ssrc:<ssrc-id> mslabel:<value>

  // The label isn't yet defined.
  // a=ssrc:<ssrc-id> label:<value>
  for (cricket::StreamParamsVec::const_iterator it =
           media_desc->streams().begin();
       it != media_desc->streams().end(); ++it) {
    // Require that the track belongs to a media stream,
    // ie the sync_label is set. This extra check is necessary since the
    // MediaContentDescription always contains a streamparam with an ssrc even
    // if no track or media stream have been created.
    if (it->sync_label.empty()) continue;

    AddSsrcLine(it->first_ssrc(), kSsrcAttributeCname, it->cname, message);
    AddSsrcLine(it->first_ssrc(), kSsrcAttributeMslabel,
                it->sync_label, message);
    AddSsrcLine(it->first_ssrc(), kSSrcAttributeLabel, it->name, message);
  }
}

void BuildRtpMap(const MediaContentDescription* media_desc,
                 const MediaType media_type,
                 std::string* message) {
  ASSERT(message != NULL);
  ASSERT(media_desc != NULL);
  std::ostringstream os;
  if (media_type == cricket::MEDIA_TYPE_VIDEO) {
    const VideoContentDescription* video_desc =
        static_cast<const VideoContentDescription*>(media_desc);
    for (std::vector<cricket::VideoCodec>::const_iterator it =
             video_desc->codecs().begin();
         it != video_desc->codecs().end(); ++it) {
      // RFC 4566
      // a=rtpmap:<payload type> <encoding name>/<clock rate>
      // [/<encodingparameters>]
      InitAttrLine(kAttributeRtpmap, &os);
      os << kSdpDelimiterColon << it->id << " " << it->name
         << "/" << kDefaultVideoClockrate;
      AddLine(os.str(), message);
    }
  } else if (media_type == cricket::MEDIA_TYPE_AUDIO) {
    const AudioContentDescription* audio_desc =
        static_cast<const AudioContentDescription*>(media_desc);
    for (std::vector<cricket::AudioCodec>::const_iterator it =
             audio_desc->codecs().begin();
         it != audio_desc->codecs().end(); ++it) {
      // RFC 4566
      // a=rtpmap:<payload type> <encoding name>/<clock rate>
      // [/<encodingparameters>]
      InitAttrLine(kAttributeRtpmap, &os);
      os << kSdpDelimiterColon << it->id << " "
         << it->name << "/" << it->clockrate;
      if (it->channels != 1) {
        os << "/" << it->channels;
      }
      AddLine(os.str(), message);
    }
  }
}

static void BuildCandidate(const std::vector<Candidate>& candidates,
                           std::string* message) {
  std::ostringstream os;

  for (std::vector<Candidate>::const_iterator it = candidates.begin();
       it != candidates.end(); ++it) {
    // RFC 5245
    // a=candidate:<foundation> <component-id> <transport> <priority>
    // <connection-address> <port> typ <candidate-types>
    // [raddr <connection-address>] [rport <port>]
    // *(SP extension-att-name SP extension-att-value)
    std::string type;
    // Map the cricket candidate type to "host" / "srflx" / "prflx" / "relay"
    if (it->type() == cricket::LOCAL_PORT_TYPE) {
      type = kCandidateHost;
    } else if (it->type() == cricket::STUN_PORT_TYPE) {
      type = kCandidateSrflx;
    } else if (it->type() == cricket::RELAY_PORT_TYPE) {
      type = kCandidateRelay;
    } else {
      ASSERT(false);
    }

    InitAttrLine(kAttributeCandidate, &os);
    os << kSdpDelimiterColon
       << it->foundation() << " " << it->component() << " "
       << it->protocol() << " " << it->priority() << " "
       << it->address().ipaddr().ToString() << " "
       << it->address().PortAsString() << " "
       << kAttributeCandidateTyp << " " << type << " ";

    // Related address
    if (!it->related_address().IsNil()) {
      os << kAttributeCandidateRaddr << " "
         << it->related_address().ipaddr().ToString() << " "
         << kAttributeCandidateRport << " "
         << it->related_address().PortAsString() << " ";
    }

    // Extensions
    os << kAttributeCandidateGeneration << " " << it->generation();

    AddLine(os.str(), message);
  }
}

bool ParseSessionDescription(const std::string& message, size_t* pos,
                             std::string* session_id,
                             std::string* session_version,
                             std::string* session_ice_ufrag,
                             std::string* session_ice_pwd,
                             cricket::SessionDescription* desc) {
  std::string line;

  // RFC 4566
  // v=  (protocol version)
  if (!GetLineWithType(message, pos, &line, kLineTypeVersion)) {
    LOG_PREFIX_PARSING_ERROR(kLineTypeVersion);
    return false;
  }
  // RFC 4566
  // o=<username> <sess-id> <sess-version> <nettype> <addrtype>
  // <unicast-address>
  if (GetLineWithType(message, pos, &line, kLineTypeOrigin)) {
    std::vector<std::string> fields;
    talk_base::split(line.substr(kLinePrefixLength),
                     kSdpDelimiterSpace, &fields);
    if (fields.size() != 6) {
      return false;
    }
    *session_id = fields[1];
    *session_version = fields[2];
  } else {
    LOG_PREFIX_PARSING_ERROR(kLineTypeOrigin);
    return false;
  }
  // RFC 4566
  // s=  (session name)
  if (!GetLineWithType(message, pos, &line, kLineTypeSessionName)) {
    LOG_PREFIX_PARSING_ERROR(kLineTypeSessionName);
    return false;
  }

  // Optional lines
  // Those are the optional lines, so shouldn't return false if not present.
  // RFC 4566
  // i=* (session information)
  GetLineWithType(message, pos, &line, kLineTypeSessionInfo);

  // RFC 4566
  // u=* (URI of description)
  GetLineWithType(message, pos, &line, kLineTypeSessionUri);

  // RFC 4566
  // e=* (email address)
  GetLineWithType(message, pos, &line, kLineTypeSessionEmail);

  // RFC 4566
  // p=* (phone number)
  GetLineWithType(message, pos, &line, kLineTypeSessionPhone);

  // RFC 4566
  // c=* (connection information -- not required if included in
  //      all media)
  GetLineWithType(message, pos, &line, kLineTypeConnection);

  // RFC 4566
  // b=* (zero or more bandwidth information lines)
  while (GetLineWithType(message, pos, &line, kLineTypeSessionBandwidth)) {
    // By pass zero or more b lines.
  }

  // RFC 4566
  // One or more time descriptions ("t=" and "r=" lines; see below)
  // t=  (time the session is active)
  // r=* (zero or more repeat times)
  // Ensure there's at least one time description
  if (!GetLineWithType(message, pos, &line, kLineTypeTiming)) {
    LOG_PREFIX_PARSING_ERROR(kLineTypeTiming);
    return false;
  }

  while (GetLineWithType(message, pos, &line, kLineTypeRepeatTimes)) {
    // By pass zero or more r lines.
  }

  // Go through the rest of the time descriptions
  while (GetLineWithType(message, pos, &line, kLineTypeTiming)) {
    while (GetLineWithType(message, pos, &line, kLineTypeRepeatTimes)) {
      // By pass zero or more r lines.
    }
  }

  // RFC 4566
  // z=* (time zone adjustments)
  GetLineWithType(message, pos, &line, kLineTypeTimeZone);

  // RFC 4566
  // k=* (encryption key)
  GetLineWithType(message, pos, &line, kLineTypeEncryptionKey);

  // RFC 4566
  // a=* (zero or more session attribute lines)
  while (GetLineWithType(message, pos, &line, kLineTypeAttributes)) {
    if (HasAttribute(line, kAttributeGroup)) {
      if (!ParseGroupAttribute(line, desc)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeIceUfrag)) {
      if (!GetValue(line, kAttributeIceUfrag, session_ice_ufrag)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeIcePwd)) {
      if (!GetValue(line, kAttributeIcePwd, session_ice_pwd)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    }
  }

  return true;
}

bool ParseGroupAttribute(const std::string& line,
                         cricket::SessionDescription* desc) {
  ASSERT(desc != NULL);

  // RFC 5888 and draft-holmberg-mmusic-sdp-bundle-negotiation-00
  // a=group:BUNDLE video voice
  std::vector<std::string> fields;
  talk_base::split(line.substr(kLinePrefixLength),
                   kSdpDelimiterSpace, &fields);
  if (fields.size() < 2) {
    return false;
  }
  std::string semantics;
  if (!GetValue(fields[0], kAttributeGroup, &semantics)) {
    return false;
  }
  cricket::ContentGroup group(semantics);
  for (size_t i = 1; i < fields.size(); ++i) {
    group.AddContentName(fields[i]);
  }
  desc->AddGroup(group);
  return true;
}

bool ParseMediaDescription(const std::string& message,
                           const std::string& session_ice_ufrag,
                           const std::string& session_ice_pwd,
                           size_t* pos,
                           cricket::SessionDescription* desc,
                           std::vector<JsepIceCandidate*>* candidates) {
  ASSERT(desc != NULL);

  std::string media_ice_ufrag;
  std::string media_ice_pwd;
  std::string line;
  int mline_index = -1;

  // Zero or more media descriptions
  // RFC 4566
  // m=<media> <port> <proto> <fmt>
  while (GetLineWithType(message, pos, &line, kLineTypeMedia)) {
    ++mline_index;
    MediaType media_type = cricket::MEDIA_TYPE_VIDEO;
    ContentDescription* content = NULL;
    std::string content_name;
    if (HasAttribute(line, kMediaTypeVideo)) {
      media_type = cricket::MEDIA_TYPE_VIDEO;
      content = new VideoContentDescription();
      // Default content name.
      content_name = cricket::CN_VIDEO;
    } else if (HasAttribute(line, kMediaTypeAudio)) {
      media_type = cricket::MEDIA_TYPE_AUDIO;
      content = new AudioContentDescription();
      // Default content name.
      content_name = cricket::CN_AUDIO;
    } else {
      LOG(LS_WARNING) << "Unsupported media type: " << line;
      continue;
    }

    // RFC 5245
    // Whether present at the session or media-level, there MUST be an
    // ice-pwd and ice-ufrag attribute for each media stream.
    // If we didn't get the username and password from the media level,
    // then use the one from session level.
    media_ice_ufrag = session_ice_ufrag;
    media_ice_pwd = session_ice_pwd;

    if (!ParseContent(message, media_type, mline_index, pos, content,
                      &content_name, &media_ice_ufrag, &media_ice_pwd,
                      candidates))
      return false;

    desc->AddContent(content_name, cricket::NS_JINGLE_RTP, content);
    // Create TransportInfo with the media level "ice-pwd" and "ice-ufrag".
    TransportInfo transport_info(content_name, NS_GINGLE_P2P,
        media_ice_ufrag, media_ice_pwd, Candidates());
    if (!desc->AddTransportInfo(transport_info)) {
      LOG(LS_ERROR) << "Failed to AddTransportInfo with content name: "
                    << content_name;
      return false;
    }
  }
  return true;
}

bool ParseContent(const std::string& message,
                  const MediaType media_type,
                  int mline_index,
                  size_t* pos,
                  ContentDescription* content,
                  std::string* content_name,
                  std::string* media_ice_ufrag,
                  std::string* media_ice_pwd,
                  std::vector<JsepIceCandidate*>* candidates) {
  ASSERT(content != NULL);
  ASSERT(content_name != NULL);
  ASSERT(candidates != NULL);

  // The media level "ice-ufrag" and "ice-pwd".
  // The candidates before update the media level "ice-pwd" and "ice-ufrag".
  Candidates candidates_orig;
  std::string line;
  // Loop until the next m line
  while (!IsLineType(message, kLineTypeMedia, *pos)) {
    if (!GetLine(message, pos, &line)) {
      if (*pos >= message.size()) {
        break;  // Done parsing
      } else {
        return false;
      }
    }

    if (!content) {
      // Unsupported media type, just skip it.
      continue;
    }

    if (!IsLineType(line, kLineTypeAttributes)) {
      // TODO: Handle other lines if needed.
      LOG(LS_INFO) << "Ignored line: " << line;
      continue;
    }

    MediaContentDescription* media_desc =
        static_cast<MediaContentDescription*> (content);

    if (HasAttribute(line, kAttributeMid)) {
      // RFC 3388
      // mid-attribute      = "a=mid:" identification-tag
      // identification-tag = token
      // Use the mid identification-tag as the content name.
      GetValue(line, kAttributeMid, content_name);
      continue;
    } else if (HasAttribute(line, kAttributeRtcpMux)) {
      media_desc->set_rtcp_mux(true);
    } else if (HasAttribute(line, kAttributeSsrc)) {
      if (!ParseSsrcAttribute(line, media_desc)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeCrypto)) {
      if (!ParseCryptoAttribute(line, media_desc)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeCandidate)) {
      Candidate candidate;
      if (!ParseCandidate(line, &candidate)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
      candidates_orig.push_back(candidate);
    } else if (HasAttribute(line, kAttributeRtpmap)) {
      if (!ParseRtpmapAttribute(line, media_type, media_desc)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeIceUfrag)) {
      if (!GetValue(line, kAttributeIceUfrag, media_ice_ufrag)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeIcePwd)) {
      if (!GetValue(line, kAttributeIcePwd, media_ice_pwd)) {
        LOG_LINE_PARSING_ERROR(line);
        return false;
      }
    } else if (HasAttribute(line, kAttributeSendOnly)) {
      media_desc->set_direction(cricket::MD_SENDONLY);
    } else if (HasAttribute(line, kAttributeRecvOnly)) {
      media_desc->set_direction(cricket::MD_RECVONLY);
    } else if (HasAttribute(line, kAttributeInactive)) {
      media_desc->set_direction(cricket::MD_INACTIVE);
    } else if (HasAttribute(line, kAttributeSendRecv)) {
      media_desc->set_direction(cricket::MD_SENDRECV);
    } else {
      // Only parse lines that we are interested of.
      LOG(LS_INFO) << "Ignored line: " << line;
      continue;
    }
  }
  // RFC 5245
  // Update the candidates with the media level "ice-pwd" and "ice-ufrag".
  for (Candidates::iterator it = candidates_orig.begin();
       it != candidates_orig.end(); ++it) {
    ASSERT((*it).username().empty());
    (*it).set_username(*media_ice_ufrag);
    ASSERT((*it).password().empty());
    (*it).set_password(*media_ice_pwd);
    candidates->push_back(
        new JsepIceCandidate(talk_base::ToString<int>(mline_index), *it));
  }
  return true;
}

bool ParseSsrcAttribute(const std::string& line,
                        MediaContentDescription* media_desc) {
  ASSERT(media_desc != NULL);
  // RFC 5576
  // a=ssrc:<ssrc-id> <attribute>
  // a=ssrc:<ssrc-id> <attribute>:<value>
  std::string field1, field2;
  if (!SplitByDelimiter(line.substr(kLinePrefixLength),
                        kSdpDelimiterSpace,
                        &field1,
                        &field2)) {
    return false;
  }

  // ssrc:<ssrc-id>
  std::string ssrc_id_s;
  if (!GetValue(field1, kAttributeSsrc, &ssrc_id_s)) {
    return false;
  }
  uint32 ssrc_id = talk_base::FromString<uint32>(ssrc_id_s);

  // RFC 5576
  // cname:<value>
  // draft-alvestrand-rtcweb-mid-01
  // mslabel:<value>

  // The label isn't yet defined.
  // label:<value>
  std::string attribute;
  std::string value;
  if (!SplitByDelimiter(field2, kSdpDelimiterColon,
                        &attribute, &value)) {
    return false;
  }

  cricket::StreamParamsVec& streams = media_desc->mutable_streams();
  StreamParams* new_stream_pointer = NULL;
  bool found = false;
  for (cricket::StreamParamsVec::iterator it = streams.begin();
       it != streams.end(); ++it) {
    if (it->has_ssrc(ssrc_id)) {
      new_stream_pointer = &(*it);
      found = true;
      break;
    }
  }
  if (!found) {
    ASSERT(new_stream_pointer == NULL);
    new_stream_pointer = new StreamParams();
    new_stream_pointer->ssrcs.push_back(ssrc_id);
  }
  if (attribute.compare(kSsrcAttributeCname) == 0) {
    new_stream_pointer->cname = value;
  } else if (attribute.compare(kSsrcAttributeMslabel) == 0) {
    new_stream_pointer->sync_label = value;
  } else if (attribute.compare(kSSrcAttributeLabel) == 0) {
    new_stream_pointer->name = value;
  }
  if (!found) {
    // This is a new stream.
    media_desc->AddStream(*new_stream_pointer);
    delete new_stream_pointer;
  }
  return true;
}

bool ParseCryptoAttribute(const std::string& line,
                          MediaContentDescription* media_desc) {
  std::vector<std::string> fields;
  talk_base::split(line.substr(kLinePrefixLength),
                   kSdpDelimiterSpace, &fields);
  // RFC 4568
  // a=crypto:<tag> <crypto-suite> <key-params> [<session-params>]
  if (fields.size() < 3) {  // 3 mandatory fields
    return false;
  }
  std::string tag_value;
  if (!GetValue(fields[0], kAttributeCrypto, &tag_value)) {
    return false;
  }
  int tag = talk_base::FromString<int>(tag_value);
  const std::string crypto_suite = fields[1];
  const std::string key_params = fields[2];
  media_desc->AddCrypto(CryptoParams(tag, crypto_suite, key_params, ""));
  return true;
}

bool ParseRtpmapAttribute(const std::string& line,
                          const MediaType media_type,
                          MediaContentDescription* media_desc) {
  std::vector<std::string> fields;
  talk_base::split(line.substr(kLinePrefixLength),
                   kSdpDelimiterSpace, &fields);
  // RFC 4566
  // a=rtpmap:<payload type> <encoding name>/<clock rate>[/<encodingparameters>]
  // 2 mandatory fields
  if (fields.size() < 2) {
    return false;
  }
  std::string payload_type_value;
  GetValue(fields[0], kAttributeRtpmap, &payload_type_value);
  const int payload_type = talk_base::FromString<int>(payload_type_value);
  const std::string encoder = fields[1];
  std::vector<std::string> codec_params;
  talk_base::split(encoder, '/', &codec_params);
  // <encoding name>/<clock rate>[/<encodingparameters>]
  // 2 mandatory fields
  if (codec_params.size() < 2 || codec_params.size() > 3) {
    return false;
  }
  const std::string encoding_name = codec_params[0];
  const int clock_rate = talk_base::FromString<int>(codec_params[1]);
  if (media_type == cricket::MEDIA_TYPE_VIDEO) {
    VideoContentDescription* video_desc =
        static_cast<VideoContentDescription*>(media_desc);
    // TODO: We will send resolution in SDP. For now, use VGA.
    video_desc->AddCodec(cricket::VideoCodec(payload_type, encoding_name,
                                             kDefaultVideoWidth,
                                             kDefaultVideoHeight,
                                             kDefaultVideoFrameRate,
                                             kDefaultVideoPreference));
  } else if (media_type == cricket::MEDIA_TYPE_AUDIO) {
    // RFC 4566
    // For audio streams, <encoding parameters> indicates the number
    // of audio channels.  This parameter is OPTIONAL and may be
    // omitted if the number of channels is one, provided that no
    // additional parameters are needed.
    int channels = 1;
    if (codec_params.size() == 3) {
      channels = talk_base::FromString<int>(codec_params[2]);
    }
    AudioContentDescription* audio_desc =
        static_cast<AudioContentDescription*>(media_desc);
    audio_desc->AddCodec(cricket::AudioCodec(payload_type, encoding_name,
                                             clock_rate, 0, channels, 0));
  }
  return true;
}

}  // namespace webrtc
