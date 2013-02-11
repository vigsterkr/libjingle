/*
 * libjingle
 * Copyright 2004 Google Inc.
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

#include "talk/session/media/mediasession.h"

#include <functional>
#include <set>

#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/scoped_ptr.h"
#include "talk/media/base/cryptoparams.h"
#include "talk/p2p/base/constants.h"
#include "talk/session/media/channelmanager.h"
#include "talk/session/media/srtpfilter.h"
#include "talk/xmpp/constants.h"

namespace {
const char kInline[] = "inline:";
}

namespace cricket {

using talk_base::scoped_ptr;

// RTP Profile names
// http://www.iana.org/assignments/rtp-parameters/rtp-parameters.xml
// RTC4585
const char kMediaProtocolAvpf[] = "RTP/AVPF";
// RFC5124
const char kMediaProtocolSavpf[] = "RTP/SAVPF";

static bool IsMediaContentOfType(const ContentInfo* content,
                                 MediaType media_type) {
  if (!IsMediaContent(content)) {
    return false;
  }

  const MediaContentDescription* mdesc =
      static_cast<const MediaContentDescription*>(content->description);
  return mdesc && mdesc->type() == media_type;
}

static bool CreateCryptoParams(int tag, const std::string& cipher,
                               CryptoParams *out) {
  std::string key;
  key.reserve(SRTP_MASTER_KEY_BASE64_LEN);

  if (!talk_base::CreateRandomString(SRTP_MASTER_KEY_BASE64_LEN, &key)) {
    return false;
  }
  out->tag = tag;
  out->cipher_suite = cipher;
  out->key_params = kInline;
  out->key_params += key;
  return true;
}

#ifdef HAVE_SRTP
static bool AddCryptoParams(const std::string& cipher_suite,
                            CryptoParamsVec *out) {
  int size = out->size();

  out->resize(size + 1);
  return CreateCryptoParams(size, cipher_suite, &out->at(size));
}

void AddMediaCryptos(const CryptoParamsVec& cryptos,
                     MediaContentDescription* media) {
  for (CryptoParamsVec::const_iterator crypto = cryptos.begin();
       crypto != cryptos.end(); ++crypto) {
    media->AddCrypto(*crypto);
  }
}

bool CreateMediaCryptos(const std::vector<std::string>& crypto_suites,
                        MediaContentDescription* media) {
  CryptoParamsVec cryptos;
  for (std::vector<std::string>::const_iterator it = crypto_suites.begin();
       it != crypto_suites.end(); ++it) {
    if (!AddCryptoParams(*it, &cryptos)) {
      return false;
    }
  }
  AddMediaCryptos(cryptos, media);
  return true;
}
#endif

const CryptoParamsVec* GetCryptos(const MediaContentDescription* media) {
  if (!media) {
    return NULL;
  }
  return &media->cryptos();
}

bool FindMatchingCrypto(const CryptoParamsVec& cryptos,
                        const CryptoParams& crypto,
                        CryptoParams* out) {
  for (CryptoParamsVec::const_iterator it = cryptos.begin();
       it != cryptos.end(); ++it) {
    if (crypto.Matches(*it)) {
      *out = *it;
      return true;
    }
  }
  return false;
}

// For audio, HMAC 32 is prefered because of the low overhead.
void GetSupportedAudioCryptoSuites(
    std::vector<std::string>* crypto_suites) {
#ifdef HAVE_SRTP
  crypto_suites->push_back(CS_AES_CM_128_HMAC_SHA1_32);
  crypto_suites->push_back(CS_AES_CM_128_HMAC_SHA1_80);
#endif
}

void GetSupportedVideoCryptoSuites(
    std::vector<std::string>* crypto_suites) {
  GetSupportedDefaultCryptoSuites(crypto_suites);
}

void GetSupportedDataCryptoSuites(
    std::vector<std::string>* crypto_suites) {
  GetSupportedDefaultCryptoSuites(crypto_suites);
}

void GetSupportedDefaultCryptoSuites(
    std::vector<std::string>* crypto_suites) {
#ifdef HAVE_SRTP
  crypto_suites->push_back(CS_AES_CM_128_HMAC_SHA1_80);
#endif
}

// For video support only 80-bit SHA1 HMAC. For audio 32-bit HMAC is
// tolerated unless bundle is enabled because it is low overhead. Pick the
// crypto in the list that is supported.
static bool SelectCrypto(const MediaContentDescription* offer,
                         bool bundle,
                         CryptoParams *crypto) {
  bool audio = offer->type() == MEDIA_TYPE_AUDIO;
  const CryptoParamsVec& cryptos = offer->cryptos();

  for (CryptoParamsVec::const_iterator i = cryptos.begin();
       i != cryptos.end(); ++i) {
    if (CS_AES_CM_128_HMAC_SHA1_80 == i->cipher_suite ||
        (CS_AES_CM_128_HMAC_SHA1_32 == i->cipher_suite && audio && !bundle)) {
      return CreateCryptoParams(i->tag, i->cipher_suite, crypto);
    }
  }
  return false;
}

static const StreamParams* FindFirstStreamParamsByCname(
    const StreamParamsVec& params_vec,
    const std::string& cname) {
  for (StreamParamsVec::const_iterator it = params_vec.begin();
       it != params_vec.end(); ++it) {
    if (cname == it->cname)
      return &*it;
  }
  return NULL;
}

// Generates a new CNAME or the CNAME of an already existing StreamParams
// if a StreamParams exist for another Stream in streams with sync_label
// sync_label.
static bool GenerateCname(const StreamParamsVec& params_vec,
                          const MediaSessionOptions::Streams& streams,
                          const std::string& synch_label,
                          std::string* cname) {
  ASSERT(cname != NULL);
  if (!cname)
    return false;

  // Check if a CNAME exist for any of the other synched streams.
  for (MediaSessionOptions::Streams::const_iterator stream_it = streams.begin();
       stream_it != streams.end() ; ++stream_it) {
    if (synch_label != stream_it->sync_label)
      continue;

    StreamParams param;
    // nick is empty for StreamParams generated using
    // MediaSessionDescriptionFactory.
    if (GetStreamByNickAndName(params_vec, "", stream_it->name,
                               &param)) {
      *cname = param.cname;
      return true;
    }
  }
  // No other stream seems to exist that we should sync with.
  // Generate a random string for the RTCP CNAME, as stated in RFC 6222.
  // This string is only used for synchronization, and therefore is opaque.
  do {
    if (!talk_base::CreateRandomString(16, cname)) {
      ASSERT(false);
      return false;
    }
  } while (FindFirstStreamParamsByCname(params_vec, *cname));

  return true;
}

// Generate random SSRC values that are not already present in |params_vec|.
// Either 2 or 1 ssrcs will be generated based on |include_rtx_stream| being
// true or false. The generated values are added to |ssrcs|.
static void GenerateSsrcs(const StreamParamsVec& params_vec,
                          bool include_rtx_stream,
                          std::vector<uint32>& ssrcs) {
  unsigned int num_ssrcs = include_rtx_stream ? 2 : 1;
  for (unsigned int i = 0; i < num_ssrcs; i++) {
    uint32 candidate;
    do {
      candidate = talk_base::CreateRandomNonZeroId();
    } while (GetStreamBySsrc(params_vec, candidate, NULL) ||
             std::count(ssrcs.begin(), ssrcs.end(), candidate) > 0);
    ssrcs.push_back(candidate);
  }
}

// Finds all StreamParams of all media types and attach them to stream_params.
static void GetCurrentStreamParams(const SessionDescription* sdesc,
                                   StreamParamsVec* stream_params) {
  if (!sdesc)
    return;

  const ContentInfos& contents = sdesc->contents();
  for (ContentInfos::const_iterator content = contents.begin();
       content != contents.end(); ++content) {
    if (!IsMediaContent(&*content)) {
      continue;
    }
    const MediaContentDescription* media =
        static_cast<const MediaContentDescription*>(
            content->description);
    const StreamParamsVec& streams = media->streams();
    for (StreamParamsVec::const_iterator it = streams.begin();
         it != streams.end(); ++it) {
      stream_params->push_back(*it);
    }
  }
}

// Helper class used for finding duplicate RTP payload types among audio, video
// and data codecs. When bundle is used the payload types may not collide.
class UsedPayloadTypes {
 public:
  UsedPayloadTypes() {
    memset(&payload_types_, 0, sizeof(payload_types_));
  }

  // Loops through all codecs in |codecs| and changes its payload type if it is
  // already in use by another codec. Call this methods with all codecs in a
  // session description to make sure no duplicate payload types exists.
  template <typename C>
  void FindAndSetPayloadTypesUsed(std::vector<C>* codecs) {
    for (typename std::vector<C>::iterator it = codecs->begin();
         it != codecs->end(); ++it) {
      FindAndSetPayloadTypeUsed<C>(&*it);
    }
  }

  // Finds and sets an unused payload type if the |codec| payload type is
  // already in use.
  template <typename C>
  void FindAndSetPayloadTypeUsed(C* codec) {
    int origina_pl_type = codec->id;
    int new_pl_type = codec->id;

    if (IsPayloadTypeUsed(origina_pl_type)) {
      new_pl_type = FindUnusedPayloadType();
      LOG(LS_WARNING) << "Duplicate pl-type found. Reassigning "
          << codec->name << " from " << origina_pl_type << " to "
          << new_pl_type;
      codec->id = new_pl_type;
    }
    SetPayloadTypeUsed(new_pl_type, origina_pl_type);
  }

  template <typename C>
  void UpdateRtxCodecs(std::vector<C>* codecs) {
    for (typename std::vector<C>::iterator it = codecs->begin();
         it != codecs->end(); ++it) {
      if (IsRtxCodec(*it)) {
        C& rtx_codec = *it;
        int referenced_pl_type =
            talk_base::FromString<int>(
                rtx_codec.params[kCodecParamAssociatedPayloadType]);

        int updated_referenced_pl_type =
            FindNewPayloadType(referenced_pl_type);
        if (updated_referenced_pl_type != referenced_pl_type) {
          LOG(LS_WARNING) << "Payload type referenced by RTX has been "
              "reassigned from pt " << referenced_pl_type << " to "
              << updated_referenced_pl_type
              << " Updating RTX reference accordingly.";

          rtx_codec.params[kCodecParamAssociatedPayloadType] =
              talk_base::ToString(updated_referenced_pl_type);
        };
      }
    }
  }

 private:
  static const int kDynamicPayloadTypeMin = 96;
  static const int kDynamicPayloadTypeMax = 127;

  // Return true if |payload_type| is a dynamic pay load type.
  bool IsDynamic(int payload_type) {
    return (payload_type >= kDynamicPayloadTypeMin &&
        payload_type <= kDynamicPayloadTypeMax);
  }

  // Returns the first unused dynamic payload-type in reverse order.
  // This hopefully reduce the risk of more collisions. We want to change the
  // default pay load types as little as possible.
  int FindUnusedPayloadType() {
    int payload_type = kDynamicPayloadTypeMax;
    for (; payload_type >= kDynamicPayloadTypeMin; --payload_type) {
      if (payload_types_[payload_type-kDynamicPayloadTypeMin] == 0)
        break;
    }
    ASSERT(payload_type >= kDynamicPayloadTypeMin);  // We have too many Codecs.
    return payload_type;
  }

  bool IsPayloadTypeUsed(int payload_type) {
    if (IsDynamic(payload_type)) {
      return payload_types_[payload_type - kDynamicPayloadTypeMin] != 0;
    }
    // Otherwise this is not a dynamic pl-type and we can't change it.
    return false;
  }

  void SetPayloadTypeUsed(int new_type, int original_type) {
    if (IsDynamic(new_type)) {
      payload_types_[new_type - kDynamicPayloadTypeMin] = original_type;
    }
  }

  int FindNewPayloadType(int original_type) {
    int payload_type = kDynamicPayloadTypeMax;
    for (; payload_type >= kDynamicPayloadTypeMin; --payload_type) {
      if (payload_types_[payload_type-kDynamicPayloadTypeMin] == original_type)
        break;
    }
    ASSERT(payload_type >= kDynamicPayloadTypeMin);
    return payload_type;
  }

  typedef int PayloadTypes[kDynamicPayloadTypeMax-kDynamicPayloadTypeMin+1];
  PayloadTypes payload_types_;
};

// Adds a StreamParams for each Stream in Streams with media type
// media_type to content_description.
// |current_params| - All currently known StreamParams of any media type.
template <class C>
static bool AddStreamParams(
    MediaType media_type,
    const MediaSessionOptions::Streams& streams,
    StreamParamsVec* current_streams,
    MediaContentDescriptionImpl<C>* content_description,
    const bool add_legacy_stream) {
  const bool include_rtx_stream =
    ContainsRtxCodec(content_description->codecs());

  if (streams.empty() && add_legacy_stream) {
    // TODO(perkj): Remove this legacy stream when all apps use StreamParams.
    std::vector<uint32> ssrcs;
    GenerateSsrcs(*current_streams, include_rtx_stream, ssrcs);
    if (include_rtx_stream) {
      content_description->AddLegacyStream(ssrcs[0], ssrcs[1]);
      content_description->set_multistream(true);
    } else {
      content_description->AddLegacyStream(ssrcs[0]);
    }
    return true;
  }

  MediaSessionOptions::Streams::const_iterator stream_it;
  for (stream_it = streams.begin();
       stream_it != streams.end(); ++stream_it) {
    if (stream_it->type != media_type)
      continue;  // Wrong media type.

    StreamParams param;
    // nick is empty for StreamParams generated using
    // MediaSessionDescriptionFactory.
    if (!GetStreamByNickAndName(*current_streams, "", stream_it->name,
                                &param)) {
      // This is a new stream.
      // Get a CNAME. Either new or same as one of the other synched streams.
      std::string cname;
      if (!GenerateCname(*current_streams, streams, stream_it->sync_label,
                         &cname)) {
        return false;
      }

      std::vector<uint32> ssrcs;
      GenerateSsrcs(*current_streams, include_rtx_stream, ssrcs);
      StreamParams stream_param;
      stream_param.name = stream_it->name;
      stream_param.ssrcs.push_back(ssrcs[0]);
      if (include_rtx_stream) {
        stream_param.AddFidSsrc(ssrcs[0], ssrcs[1]);
        content_description->set_multistream(true);
      }
      stream_param.cname = cname;
      stream_param.sync_label = stream_it->sync_label;
      content_description->AddStream(stream_param);

      // Store the new StreamParams in current_streams.
      // This is necessary so that we can use the CNAME for other media types.
      current_streams->push_back(stream_param);
    } else {
      content_description->AddStream(param);
    }
  }
  return true;
}

// Updates the transport infos of the |sdesc| according to the given
// |bundle_group|. The transport infos of the content names within the
// |bundle_group| should be updated to use the ufrag and pwd of the first
// content within the |bundle_group|.
static bool UpdateTransportInfoForBundle(const ContentGroup& bundle_group,
                                         SessionDescription* sdesc) {
  // The bundle should not be empty.
  if (!sdesc || !bundle_group.FirstContentName()) {
    return false;
  }

  // We should definitely have a transport for the first content.
  std::string selected_content_name = *bundle_group.FirstContentName();
  const TransportInfo* selected_transport_info =
      sdesc->GetTransportInfoByName(selected_content_name);
  if (!selected_transport_info) {
    return false;
  }

  // Set the other contents to use the same ICE credentials.
  const std::string selected_ufrag =
      selected_transport_info->description.ice_ufrag;
  const std::string selected_pwd =
      selected_transport_info->description.ice_pwd;
  for (TransportInfos::iterator it =
           sdesc->transport_infos().begin();
       it != sdesc->transport_infos().end(); ++it) {
    if (bundle_group.HasContentName(it->content_name) &&
        it->content_name != selected_content_name) {
      it->description.ice_ufrag = selected_ufrag;
      it->description.ice_pwd = selected_pwd;
    }
  }
  return true;
}

// Gets the CryptoParamsVec of the given |content_name| from |sdesc|, and
// sets it to |cryptos|.
static bool GetCryptosByName(const SessionDescription* sdesc,
                             const std::string& content_name,
                             CryptoParamsVec* cryptos) {
  if (!sdesc || !cryptos) {
    return false;
  }

  const ContentInfo* content = sdesc->GetContentByName(content_name);
  if (!IsMediaContent(content) || !content->description) {
    return false;
  }

  const MediaContentDescription* media_desc =
      static_cast<const MediaContentDescription*>(content->description);
  *cryptos = media_desc->cryptos();
  return true;
}

// Predicate function used by the remove_if.
// Returns true if the |crypto|'s cipher_suite is not found in |filter|.
static bool CryptoNotFound(const CryptoParams crypto,
                           const CryptoParamsVec* filter) {
  if (filter == NULL) {
    return true;
  }
  for (CryptoParamsVec::const_iterator it = filter->begin();
       it != filter->end(); ++it) {
    if (it->cipher_suite == crypto.cipher_suite) {
      return false;
    }
  }
  return true;
}

// Prunes the |target_cryptos| by removing the crypto params (cipher_suite)
// which are not available in |filter|.
static void PruneCryptos(const CryptoParamsVec& filter,
                         CryptoParamsVec* target_cryptos) {
  if (!target_cryptos) {
    return;
  }
  target_cryptos->erase(std::remove_if(target_cryptos->begin(),
                                       target_cryptos->end(),
                                       bind2nd(ptr_fun(CryptoNotFound),
                                               &filter)),
                        target_cryptos->end());
}

// Updates the crypto parameters of the |sdesc| according to the given
// |bundle_group|. The crypto parameters of all the contents within the
// |bundle_group| should be updated to use the common subset of the
// available cryptos.
static bool UpdateCryptoParamsForBundle(const ContentGroup& bundle_group,
                                        SessionDescription* sdesc) {
  // The bundle should not be empty.
  if (!sdesc || !bundle_group.FirstContentName()) {
    return false;
  }

  // Get the common cryptos.
  const ContentNames& content_names = bundle_group.content_names();
  CryptoParamsVec common_cryptos;
  for (ContentNames::const_iterator it = content_names.begin();
       it != content_names.end(); ++it) {
    if (it == content_names.begin()) {
      // Initial the common_cryptos with the first content in the bundle group.
      if (!GetCryptosByName(sdesc, *it, &common_cryptos)) {
        return false;
      }
      if (common_cryptos.empty()) {
        // If there's no crypto params, we should just return.
        return true;
      }
    } else {
      CryptoParamsVec cryptos;
      if (!GetCryptosByName(sdesc, *it, &cryptos)) {
        return false;
      }
      PruneCryptos(cryptos, &common_cryptos);
    }
  }

  if (common_cryptos.empty()) {
    return false;
  }

  // Update to use the common cryptos.
  for (ContentNames::const_iterator it = content_names.begin();
       it != content_names.end(); ++it) {
    ContentInfo* content = sdesc->GetContentByName(*it);
    if (IsMediaContent(content)) {
      MediaContentDescription* media_desc =
          static_cast<MediaContentDescription*>(content->description);
      if (!media_desc) {
        return false;
      }
      media_desc->set_cryptos(common_cryptos);
    }
  }
  return true;
}

template <class C>
static bool ContainsRtxCodec(const std::vector<C>& codecs) {
  typename std::vector<C>::const_iterator it;
  for (it = codecs.begin(); it != codecs.end(); ++it) {
    if (IsRtxCodec(*it)) {
      return true;
    }
  }
  return false;
}

template <class C>
static bool IsRtxCodec(const C& codec) {
  return stricmp(codec.name.c_str(), kRtxCodecName) == 0;
}

// Create a media content to be offered in a session-initiate,
// according to the given options.rtcp_mux, options.is_muc,
// options.streams, codecs, secure_transport, crypto, and streams.  If we don't
// currently have crypto (in current_cryptos) and it is enabled (in
// secure_policy), crypto is created (according to crypto_suites).  If
// add_legacy_stream is true, and current_streams is empty, a legacy
// stream is created.  The created content is added to the offer.
template <class C>
static bool CreateMediaContentOffer(
    const MediaSessionOptions& options,
    const std::vector<C>& codecs,
    const SecureMediaPolicy& secure_policy,
    const CryptoParamsVec* current_cryptos,
    const std::vector<std::string>& crypto_suites,
    bool add_legacy_stream,
    StreamParamsVec* current_streams,
    MediaContentDescriptionImpl<C>* offer) {
  offer->AddCodecs(codecs);
  offer->SortCodecs();

  offer->set_crypto_required(secure_policy == SEC_REQUIRED);
  offer->set_rtcp_mux(options.rtcp_mux_enabled);
  offer->set_multistream(options.is_muc);

  if (!AddStreamParams(
          offer->type(), options.streams, current_streams,
          offer, add_legacy_stream)) {
    return false;
  }

#ifdef HAVE_SRTP
  if (secure_policy != SEC_DISABLED) {
    if (current_cryptos) {
      AddMediaCryptos(*current_cryptos, offer);
    }
    if (offer->cryptos().empty()) {
      if (!CreateMediaCryptos(crypto_suites, offer)) {
        return false;
      }
    }
  }
#endif

  if (offer->crypto_required() && offer->cryptos().empty()) {
    return false;
  }
  return true;
}

template <class C>
static void NegotiateCodecs(const std::vector<C>& local_codecs,
                     const std::vector<C>& offered_codecs,
                     std::vector<C>* negotiated_codecs) {
  typename std::vector<C>::const_iterator ours;
  for (ours = local_codecs.begin();
       ours != local_codecs.end(); ++ours) {
    typename std::vector<C>::const_iterator theirs;
    for (theirs = offered_codecs.begin();
         theirs != offered_codecs.end(); ++theirs) {
      if (ours->Matches(*theirs)) {
        C negotiated(*ours);
        if (IsRtxCodec(negotiated)) {
          // Since we use the payload type from the |offered_codecs|, we also
          // need to use the referenced payload type.
          negotiated.params = theirs->params;
        }
        negotiated.id = theirs->id;
        negotiated_codecs->push_back(negotiated);
      }
    }
  }
}

template <class C>
static bool FindMatchingCodec(const std::vector<C>& codecs,
                              const C& codec_to_match,
                              C* found_codec) {
  for (typename std::vector<C>::const_iterator it = codecs.begin();
       it  != codecs.end(); ++it) {
    if (it->Matches(codec_to_match)) {
      if (found_codec != NULL) {
        *found_codec= *it;
      }
      return true;
    }
  }
  return false;
}

// Adds all codecs from |reference_codecs| to |offered_codecs| that dont'
// already exist in |offered_codecs| and ensure the payload types don't
// collide.
template <class C>
static void FindCodecsToOffer(
    const std::vector<C>& reference_codecs,
    std::vector<C>* offered_codecs,
    UsedPayloadTypes* used_pltypes) {
  std::vector<C> new_rtx_codecs_;
  for (typename std::vector<C>::const_iterator it = reference_codecs.begin();
       it != reference_codecs.end(); ++it) {
    if (!FindMatchingCodec<C>(*offered_codecs, *it, NULL)) {
      C codec = *it;
      used_pltypes->FindAndSetPayloadTypeUsed(&codec);
      offered_codecs->push_back(codec);
    }
  }
  used_pltypes->UpdateRtxCodecs(offered_codecs);
}

// Create a media content to be answered in a session-accept,
// according to the given options.rtcp_mux, options.streams, codecs,
// crypto, and streams.  If we don't currently have crypto (in
// current_cryptos) and it is enabled (in secure_policy), crypto is
// created (according to crypto_suites).  If add_legacy_stream is
// true, and current_streams is empty, a legacy stream is created.
// The codecs, rtcp_mux, and crypto are all negotiated with the offer
// from the incoming session-initiate.  If the negotiation fails, this
// method returns false.  The created content is added to the offer.
template <class C>
static bool CreateMediaContentAnswer(
    const MediaContentDescriptionImpl<C>* offer,
    const MediaSessionOptions& options,
    const std::vector<C>& local_codecs,
    const SecureMediaPolicy& sdes_policy,
    const CryptoParamsVec* current_cryptos,
    StreamParamsVec* current_streams,
    bool add_legacy_stream,
    bool bundle_enabled,
    MediaContentDescriptionImpl<C>* answer) {
  std::vector<C> negotiated_codecs;
  NegotiateCodecs(local_codecs, offer->codecs(), &negotiated_codecs);
  answer->AddCodecs(negotiated_codecs);
  answer->SortCodecs();
  answer->set_protocol(offer->protocol());

  answer->set_rtcp_mux(options.rtcp_mux_enabled && offer->rtcp_mux());

  if (sdes_policy != SEC_DISABLED) {
    CryptoParams crypto;
    if (SelectCrypto(offer, bundle_enabled, &crypto)) {
      if (current_cryptos) {
        FindMatchingCrypto(*current_cryptos, crypto, &crypto);
      }
      answer->AddCrypto(crypto);
    }
  }

  if (answer->cryptos().empty() &&
      (offer->crypto_required() || sdes_policy == SEC_REQUIRED)) {
    return false;
  }

  if (!AddStreamParams(
          answer->type(), options.streams, current_streams,
          answer, add_legacy_stream)) {
    return false;  // Something went seriously wrong.
  }

  return true;
}

static bool IsMediaProtocolSupported(MediaType type,
                                     const std::string& protocol) {
  // Since not all applications serialize and deserialize the media protocol,
  // we will have to accept |protocol| to be empty.
  return protocol == kMediaProtocolAvpf || protocol == kMediaProtocolSavpf ||
      protocol.empty();
}

static void SetMediaProtocol(bool secure_transport,
                             MediaContentDescription* desc) {
  if (!desc->cryptos().empty() || secure_transport)
    desc->set_protocol(kMediaProtocolSavpf);
  else
    desc->set_protocol(kMediaProtocolAvpf);
}

void MediaSessionOptions::AddStream(MediaType type,
                                    const std::string& name,
                                    const std::string& sync_label) {
  streams.push_back(Stream(type, name, sync_label));

  if (type == MEDIA_TYPE_VIDEO)
    has_video = true;
  else if (type == MEDIA_TYPE_AUDIO)
    has_audio = true;
  else if (type == MEDIA_TYPE_DATA)
    has_data = true;
}

void MediaSessionOptions::RemoveStream(MediaType type,
                                       const std::string& name) {
  Streams::iterator stream_it = streams.begin();
  for (; stream_it != streams.end(); ++stream_it) {
    if (stream_it->type == type && stream_it->name == name) {
      streams.erase(stream_it);
      return;
    }
  }
  ASSERT(false);
}

MediaSessionDescriptionFactory::MediaSessionDescriptionFactory(
    const TransportDescriptionFactory* transport_desc_factory)
    : secure_(SEC_DISABLED),
      add_legacy_(true),
      transport_desc_factory_(transport_desc_factory) {
}

MediaSessionDescriptionFactory::MediaSessionDescriptionFactory(
    ChannelManager* channel_manager,
    const TransportDescriptionFactory* transport_desc_factory)
    : secure_(SEC_DISABLED),
      add_legacy_(true),
      transport_desc_factory_(transport_desc_factory) {
  channel_manager->GetSupportedAudioCodecs(&audio_codecs_);
  channel_manager->GetSupportedVideoCodecs(&video_codecs_);
  channel_manager->GetSupportedDataCodecs(&data_codecs_);
}

SessionDescription* MediaSessionDescriptionFactory::CreateOffer(
    const MediaSessionOptions& options,
    const SessionDescription* current_description) const {
  bool secure_transport = (transport_desc_factory_->secure() != SEC_DISABLED);

  scoped_ptr<SessionDescription> offer(new SessionDescription());

  StreamParamsVec current_streams;
  GetCurrentStreamParams(current_description, &current_streams);

  AudioCodecs audio_codecs;
  VideoCodecs video_codecs;
  DataCodecs data_codecs;
  GetCodecsToOffer(current_description, &audio_codecs, &video_codecs,
                   &data_codecs);

  // Handle m=audio.
  if (options.has_audio) {
    scoped_ptr<AudioContentDescription> audio(new AudioContentDescription());
    std::vector<std::string> crypto_suites;
    GetSupportedAudioCryptoSuites(&crypto_suites);
    if (!CreateMediaContentOffer(
            options,
            audio_codecs,
            secure(),
            GetCryptos(GetFirstAudioContentDescription(current_description)),
            crypto_suites,
            add_legacy_,
            &current_streams,
            audio.get())) {
      return NULL;
    }

    audio->set_lang(lang_);
    SetMediaProtocol(secure_transport, audio.get());
    offer->AddContent(CN_AUDIO, NS_JINGLE_RTP, audio.release());
    if (!AddTransportOffer(CN_AUDIO, options.transport_options,
                           current_description, offer.get())) {
      return NULL;
    }
  }

  // Handle m=video.
  if (options.has_video) {
    scoped_ptr<VideoContentDescription> video(new VideoContentDescription());
    std::vector<std::string> crypto_suites;
    GetSupportedVideoCryptoSuites(&crypto_suites);
    if (!CreateMediaContentOffer(
            options,
            video_codecs,
            secure(),
            GetCryptos(GetFirstVideoContentDescription(current_description)),
            crypto_suites,
            add_legacy_,
            &current_streams,
            video.get())) {
      return NULL;
    }

    video->set_bandwidth(options.video_bandwidth);
    SetMediaProtocol(secure_transport, video.get());
    offer->AddContent(CN_VIDEO, NS_JINGLE_RTP, video.release());
    if (!AddTransportOffer(CN_VIDEO, options.transport_options,
                           current_description, offer.get())) {
      return NULL;
    }
  }

  // Handle m=data.
  if (options.has_data) {
    scoped_ptr<DataContentDescription> data(new DataContentDescription());
    std::vector<std::string> crypto_suites;
    GetSupportedDataCryptoSuites(&crypto_suites);
    if (!CreateMediaContentOffer(
            options,
            data_codecs,
            secure(),
            GetCryptos(GetFirstDataContentDescription(current_description)),
            crypto_suites,
            add_legacy_,
            &current_streams,
            data.get())) {
      return NULL;
    }

    data->set_bandwidth(options.data_bandwidth);
    SetMediaProtocol(secure_transport, data.get());
    offer->AddContent(CN_DATA, NS_JINGLE_RTP, data.release());
    if (!AddTransportOffer(CN_DATA, options.transport_options,
                           current_description, offer.get())) {
      return NULL;
    }
  }

  // Bundle the contents together, if we've been asked to do so, and update any
  // parameters that need to be tweaked for BUNDLE.
  if (options.bundle_enabled) {
    ContentGroup offer_bundle(GROUP_TYPE_BUNDLE);
    for (ContentInfos::const_iterator content = offer->contents().begin();
       content != offer->contents().end(); ++content) {
      offer_bundle.AddContentName(content->name);
    }
    offer->AddGroup(offer_bundle);
    if (!UpdateTransportInfoForBundle(offer_bundle, offer.get())) {
      LOG(LS_ERROR) << "CreateOffer failed to UpdateTransportInfoForBundle.";
      return NULL;
    }
    if (!UpdateCryptoParamsForBundle(offer_bundle, offer.get())) {
      LOG(LS_ERROR) << "CreateOffer failed to UpdateCryptoParamsForBundle.";
      return NULL;
    }
  }

  return offer.release();
}

SessionDescription* MediaSessionDescriptionFactory::CreateAnswer(
    const SessionDescription* offer, const MediaSessionOptions& options,
    const SessionDescription* current_description) const {
  // The answer contains the intersection of the codecs in the offer with the
  // codecs we support, ordered by our local preference. As indicated by
  // XEP-0167, we retain the same payload ids from the offer in the answer.
  scoped_ptr<SessionDescription> answer(new SessionDescription());

  StreamParamsVec current_streams;
  GetCurrentStreamParams(current_description, &current_streams);

  bool bundle_enabled =
      offer->HasGroup(GROUP_TYPE_BUNDLE) && options.bundle_enabled;

  // Handle m=audio.
  const ContentInfo* audio_content = GetFirstAudioContent(offer);
  if (audio_content) {
    scoped_ptr<TransportDescription> audio_transport(
        CreateTransportAnswer(audio_content->name, offer,
                              options.transport_options,
                              current_description));
    if (!audio_transport) {
      return NULL;
    }

    scoped_ptr<AudioContentDescription> audio_answer(
        new AudioContentDescription());
    // Do not require or create SDES cryptos if DTLS is used.
    cricket::SecurePolicy sdes_policy =
        audio_transport->secure() ? cricket::SEC_DISABLED : secure();
    if (!CreateMediaContentAnswer(
            static_cast<const AudioContentDescription*>(
                audio_content->description),
            options,
            audio_codecs_,
            sdes_policy,
            GetCryptos(GetFirstAudioContentDescription(current_description)),
            &current_streams,
            add_legacy_,
            bundle_enabled,
            audio_answer.get())) {
      return NULL;  // Fails the session setup.
    }

    bool rejected = !options.has_audio ||
          !IsMediaProtocolSupported(MEDIA_TYPE_AUDIO,
                                    audio_answer->protocol());
    if (!rejected) {
      AddTransportAnswer(audio_content->name, *(audio_transport.get()),
                         answer.get());
    } else {
      // RFC 3264
      // The answer MUST contain the same number of m-lines as the offer.
      LOG(LS_INFO) << "Audio is not supported in the answer.";
    }

    answer->AddContent(audio_content->name, audio_content->type, rejected,
                       audio_answer.release());
  } else {
    LOG(LS_INFO) << "Audio is not available in the offer.";
  }

  // Handle m=video.
  const ContentInfo* video_content = GetFirstVideoContent(offer);
  if (video_content) {
    scoped_ptr<TransportDescription> video_transport(
        CreateTransportAnswer(video_content->name, offer,
                              options.transport_options,
                              current_description));
    if (!video_transport) {
      return NULL;
    }

    scoped_ptr<VideoContentDescription> video_answer(
        new VideoContentDescription());
    // Do not require or create SDES cryptos if DTLS is used.
    cricket::SecurePolicy sdes_policy =
        video_transport->secure() ? cricket::SEC_DISABLED : secure();
    if (!CreateMediaContentAnswer(
            static_cast<const VideoContentDescription*>(
                video_content->description),
            options,
            video_codecs_,
            sdes_policy,
            GetCryptos(GetFirstVideoContentDescription(current_description)),
            &current_streams,
            add_legacy_,
            bundle_enabled,
            video_answer.get())) {
      return NULL;
    }
    bool rejected = !options.has_video ||
        !IsMediaProtocolSupported(MEDIA_TYPE_VIDEO, video_answer->protocol());
    if (!rejected) {
      if (!AddTransportAnswer(video_content->name, *(video_transport.get()),
                              answer.get())) {
        return NULL;
      }
      video_answer->set_bandwidth(options.video_bandwidth);
    } else {
      // RFC 3264
      // The answer MUST contain the same number of m-lines as the offer.
      LOG(LS_INFO) << "Video is not supported in the answer.";
    }
    answer->AddContent(video_content->name, video_content->type, rejected,
                       video_answer.release());
  } else {
    LOG(LS_INFO) << "Video is not available in the offer.";
  }

  // Handle m=data.
  const ContentInfo* data_content = GetFirstDataContent(offer);
  if (data_content) {
    scoped_ptr<TransportDescription> data_transport(
        CreateTransportAnswer(data_content->name, offer,
                              options.transport_options,
                              current_description));
    if (!data_transport) {
      return NULL;
    }
    scoped_ptr<DataContentDescription> data_answer(
        new DataContentDescription());
    // Do not require or create SDES cryptos if DTLS is used.
    cricket::SecurePolicy sdes_policy =
        data_transport->secure() ? cricket::SEC_DISABLED : secure();
    if (!CreateMediaContentAnswer(
            static_cast<const DataContentDescription*>(
                data_content->description),
            options,
            data_codecs_,
            sdes_policy,
            GetCryptos(GetFirstDataContentDescription(current_description)),
            &current_streams,
            add_legacy_,
            bundle_enabled,
            data_answer.get())) {
      return NULL;  // Fails the session setup.
    }
    bool rejected = !options.has_data ||
        !IsMediaProtocolSupported(MEDIA_TYPE_DATA, data_answer->protocol());
    if (!rejected) {
      data_answer->set_bandwidth(options.data_bandwidth);
      if (!AddTransportAnswer(data_content->name, *(data_transport.get()),
                              answer.get())) {
        return NULL;
      }
    } else {
      // RFC 3264
      // The answer MUST contain the same number of m-lines as the offer.
      LOG(LS_INFO) << "Data is not supported in the answer.";
    }
    answer->AddContent(data_content->name, data_content->type, rejected,
                       data_answer.release());
  } else {
    LOG(LS_INFO) << "Data is not available in the offer.";
  }

  // If the offer supports BUNDLE, and we want to use it too, create a BUNDLE
  // group in the answer with the appropriate content names.
  if (offer->HasGroup(GROUP_TYPE_BUNDLE) && options.bundle_enabled) {
    const ContentGroup* offer_bundle = offer->GetGroupByName(GROUP_TYPE_BUNDLE);
    ContentGroup answer_bundle(GROUP_TYPE_BUNDLE);
    for (ContentInfos::const_iterator content = answer->contents().begin();
       content != answer->contents().end(); ++content) {
      if (!content->rejected && offer_bundle->HasContentName(content->name)) {
        answer_bundle.AddContentName(content->name);
      }
    }
    if (answer_bundle.FirstContentName()) {
      answer->AddGroup(answer_bundle);

      // Share the same ICE credentials and crypto params across all contents,
      // as BUNDLE requires.
      if (!UpdateTransportInfoForBundle(answer_bundle, answer.get())) {
        LOG(LS_ERROR) << "CreateAnswer failed to UpdateTransportInfoForBundle.";
        return NULL;
      }

      if (!UpdateCryptoParamsForBundle(answer_bundle, answer.get())) {
        LOG(LS_ERROR) << "CreateAnswer failed to UpdateCryptoParamsForBundle.";
        return NULL;
      }
    }
  }

  return answer.release();
}

// Gets the TransportInfo of the given |content_name| from the
// |current_description|. If doesn't exist, returns a new one.
static const TransportDescription* GetTransportDescription(
    const std::string& content_name,
    const SessionDescription* current_description) {
  const TransportDescription* desc = NULL;
  if (current_description) {
    const TransportInfo* info =
        current_description->GetTransportInfoByName(content_name);
    if (info) {
      desc = &info->description;
    }
  }
  return desc;
}

void MediaSessionDescriptionFactory::GetCodecsToOffer(
    const SessionDescription* current_description,
    AudioCodecs* audio_codecs,
    VideoCodecs* video_codecs,
    DataCodecs* data_codecs) const {
  UsedPayloadTypes used_pltypes;
  audio_codecs->clear();
  video_codecs->clear();
  data_codecs->clear();


  // First - get all codecs from the current description if the media type
  // is used.
  // Add them to |used_pltypes| so the payloadtype is not reused if a new media
  // type is added.
  if (current_description) {
    const AudioContentDescription* audio =
        GetFirstAudioContentDescription(current_description);
    if (audio) {
      *audio_codecs = audio->codecs();
      used_pltypes.FindAndSetPayloadTypesUsed<AudioCodec>(audio_codecs);
    }
    const VideoContentDescription* video =
        GetFirstVideoContentDescription(current_description);
    if (video) {
      *video_codecs = video->codecs();
      used_pltypes.FindAndSetPayloadTypesUsed<VideoCodec>(video_codecs);
    }
    const DataContentDescription* data =
        GetFirstDataContentDescription(current_description);
    if (data) {
      *data_codecs = data->codecs();
      used_pltypes.FindAndSetPayloadTypesUsed<DataCodec>(data_codecs);
    }
  }

  // Add our codecs that are not in |current_description|.
  FindCodecsToOffer<AudioCodec>(audio_codecs_, audio_codecs, &used_pltypes);
  FindCodecsToOffer<VideoCodec>(video_codecs_, video_codecs, &used_pltypes);
  FindCodecsToOffer<DataCodec>(data_codecs_, data_codecs, &used_pltypes);
}

bool MediaSessionDescriptionFactory::AddTransportOffer(
  const std::string& content_name,
  const TransportOptions& transport_options,
  const SessionDescription* current_desc,
  SessionDescription* offer_desc) const {
  if (!transport_desc_factory_)
     return false;
  const TransportDescription* current_tdesc =
      GetTransportDescription(content_name, current_desc);
  talk_base::scoped_ptr<TransportDescription> new_tdesc(
      transport_desc_factory_->CreateOffer(transport_options, current_tdesc));
  bool ret = (new_tdesc.get() != NULL &&
      offer_desc->AddTransportInfo(TransportInfo(content_name, *new_tdesc)));
  if (!ret) {
    LOG(LS_ERROR)
        << "Failed to AddTransportOffer, content name=" << content_name;
  }
  return ret;
}

TransportDescription* MediaSessionDescriptionFactory::CreateTransportAnswer(
    const std::string& content_name,
    const SessionDescription* offer_desc,
    const TransportOptions& transport_options,
    const SessionDescription* current_desc) const {
  if (!transport_desc_factory_)
    return NULL;
  const TransportDescription* offer_tdesc =
      GetTransportDescription(content_name, offer_desc);
  const TransportDescription* current_tdesc =
      GetTransportDescription(content_name, current_desc);
  return
      transport_desc_factory_->CreateAnswer(offer_tdesc, transport_options,
                                            current_tdesc);
}

bool MediaSessionDescriptionFactory::AddTransportAnswer(
    const std::string& content_name,
    const TransportDescription& transport_desc,
    SessionDescription* answer_desc) const {
  if (!answer_desc->AddTransportInfo(TransportInfo(content_name,
                                                   transport_desc))) {
    LOG(LS_ERROR)
        << "Failed to AddTransportAnswer, content name=" << content_name;
    return false;
  }
  return true;
}

bool IsMediaContent(const ContentInfo* content) {
  return (content && content->type == NS_JINGLE_RTP);
}

bool IsAudioContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MEDIA_TYPE_AUDIO);
}

bool IsVideoContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MEDIA_TYPE_VIDEO);
}

bool IsDataContent(const ContentInfo* content) {
  return IsMediaContentOfType(content, MEDIA_TYPE_DATA);
}

static const ContentInfo* GetFirstMediaContent(const ContentInfos& contents,
                                               MediaType media_type) {
  for (ContentInfos::const_iterator content = contents.begin();
       content != contents.end(); content++) {
    if (IsMediaContentOfType(&*content, media_type)) {
      return &*content;
    }
  }
  return NULL;
}

const ContentInfo* GetFirstAudioContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MEDIA_TYPE_AUDIO);
}

const ContentInfo* GetFirstVideoContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MEDIA_TYPE_VIDEO);
}

const ContentInfo* GetFirstDataContent(const ContentInfos& contents) {
  return GetFirstMediaContent(contents, MEDIA_TYPE_DATA);
}

static const ContentInfo* GetFirstMediaContent(const SessionDescription* sdesc,
                                               MediaType media_type) {
  if (sdesc == NULL)
    return NULL;

  return GetFirstMediaContent(sdesc->contents(), media_type);
}

const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MEDIA_TYPE_AUDIO);
}

const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MEDIA_TYPE_VIDEO);
}

const ContentInfo* GetFirstDataContent(const SessionDescription* sdesc) {
  return GetFirstMediaContent(sdesc, MEDIA_TYPE_DATA);
}

const MediaContentDescription* GetFirstMediaContentDescription(
    const SessionDescription* sdesc, MediaType media_type) {
  const ContentInfo* content = GetFirstMediaContent(sdesc, media_type);
  const ContentDescription* description = content ? content->description : NULL;
  return static_cast<const MediaContentDescription*>(description);
}

const AudioContentDescription* GetFirstAudioContentDescription(
    const SessionDescription* sdesc) {
  return static_cast<const AudioContentDescription*>(
      GetFirstMediaContentDescription(sdesc, MEDIA_TYPE_AUDIO));
}

const VideoContentDescription* GetFirstVideoContentDescription(
    const SessionDescription* sdesc) {
  return static_cast<const VideoContentDescription*>(
      GetFirstMediaContentDescription(sdesc, MEDIA_TYPE_VIDEO));
}

const DataContentDescription* GetFirstDataContentDescription(
    const SessionDescription* sdesc) {
  return static_cast<const DataContentDescription*>(
      GetFirstMediaContentDescription(sdesc, MEDIA_TYPE_DATA));
}

bool GetMediaChannelNameFromComponent(
    int component, MediaType media_type, std::string* channel_name) {
  if (media_type == MEDIA_TYPE_AUDIO) {
    if (component == ICE_CANDIDATE_COMPONENT_RTP) {
      *channel_name = GICE_CHANNEL_NAME_RTP;
      return true;
    } else if (component == ICE_CANDIDATE_COMPONENT_RTCP) {
      *channel_name = GICE_CHANNEL_NAME_RTCP;
      return true;
    }
  } else if (media_type == MEDIA_TYPE_VIDEO) {
    if (component == ICE_CANDIDATE_COMPONENT_RTP) {
      *channel_name = GICE_CHANNEL_NAME_VIDEO_RTP;
      return true;
    } else if (component == ICE_CANDIDATE_COMPONENT_RTCP) {
      *channel_name = GICE_CHANNEL_NAME_VIDEO_RTCP;
      return true;
    }
  } else if (media_type == MEDIA_TYPE_DATA) {
    if (component == ICE_CANDIDATE_COMPONENT_RTP) {
      *channel_name = GICE_CHANNEL_NAME_DATA_RTP;
      return true;
    } else if (component == ICE_CANDIDATE_COMPONENT_RTCP) {
      *channel_name = GICE_CHANNEL_NAME_DATA_RTCP;
      return true;
    }
  }

  return false;
}

bool GetMediaComponentFromChannelName(
    const std::string& channel_name, int* component) {
  if (channel_name == GICE_CHANNEL_NAME_RTP ||
      channel_name == GICE_CHANNEL_NAME_VIDEO_RTP ||
      channel_name == GICE_CHANNEL_NAME_DATA_RTP) {
    *component = ICE_CANDIDATE_COMPONENT_RTP;
    return true;
  } else if (channel_name == GICE_CHANNEL_NAME_RTCP ||
             channel_name == GICE_CHANNEL_NAME_VIDEO_RTCP ||
             channel_name == GICE_CHANNEL_NAME_DATA_RTP) {
    *component = ICE_CANDIDATE_COMPONENT_RTCP;
    return true;
  }

  return false;
}

bool GetMediaTypeFromChannelName(
    const std::string& channel_name, MediaType* media_type) {
  if (channel_name == GICE_CHANNEL_NAME_RTP ||
      channel_name == GICE_CHANNEL_NAME_RTCP) {
    *media_type = MEDIA_TYPE_AUDIO;
    return true;
  } else if (channel_name == GICE_CHANNEL_NAME_VIDEO_RTP ||
             channel_name == GICE_CHANNEL_NAME_VIDEO_RTCP) {
    *media_type = MEDIA_TYPE_VIDEO;
    return true;
  } else if (channel_name == GICE_CHANNEL_NAME_DATA_RTP ||
             channel_name == GICE_CHANNEL_NAME_DATA_RTCP) {
    *media_type = MEDIA_TYPE_DATA;
    return true;
  }

  return false;
}

}  // namespace cricket
