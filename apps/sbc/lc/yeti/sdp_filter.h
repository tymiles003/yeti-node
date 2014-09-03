#ifndef SDP_FILTER_H
#define SDP_FILTER_H

#include <string>
using std::string;
#include <vector>
using std::vector;

#include <AmSdp.h>
#include "SBCCallLeg.h"

#define DTMF_ENCODING_NAME "TELEPHONE-EVENT"

int AmMimeBody2Sdp(const AmMimeBody &body,AmSdp &sdp);

void dump_SdpPayload(const vector<SdpPayload> &p,string prefix="");
void dump_SdpMedia(const vector<SdpMedia> &m,string prefix="");

void fix_dynamic_payloads(AmSdp &sdp,PayloadIdMapping &mapping);

enum conn_location {
	BOTH = 0,
	SESSION_ONLY,
	MEDIA_ONLY
};
const char *conn_location2str(int location_id);
void normalize_conn_location(AmSdp &sdp, int location_id);

/**
 * @brief filter and arrange codecs in SDP\n
 * uses static_codecs for appropriate leg\n
 * orders codecs according to their positions in static_codecs
 * @param[in,out] sdp SDP which will be processed
 * @param[in] static_payloads desired codecs configuration
 * @param[in] add_codecs add codecs which nonexistent in incoming SDP,
 * but present in static_codecs
 * @return 0 if succ. negative value with error code on errors
 */
int filter_arrange_SDP(AmSdp& sdp,
					   const std::vector<SdpPayload> &static_payloads,
					   bool add_codecs);


/**
 * @brief decide whether or not we can accept codecs in incoming INVITE
 * according to static_codecs_aleg preference
 * @param[in] call_profile current leg callprofile
 * @param[in] body
 * @param[out] negotiated_media
 * @param[in] method
 * @param[in] static_codecs_id
 * @param[in] local negotiate for request local processing (no further relay)
 * @param[in] single_codec reduce negotiation result to one codec
 * @return 0 if succ. negative value with error code on errors
 */
int negotiateRequestSdp(SBCCallProfile &call_profile,
					AmSipRequest &req, vector<SdpMedia> &negotiated_media,
					const string &method,
					int static_codecs_id,
					bool local = false,
					bool single_codec = false);

/**
 * @brief transform request SDP in both directions between legs
 * @param[in] call
 * @param[in] call_profile
 * @param[in,out] body
 * @param[in] method
 * @param[in] static_codecs_id
 * @return 0 if succ. negative value with error code on errors
 */
int filterRequestSdp(SBCCallLeg *call,
                     SBCCallProfile &call_profile,
                     AmMimeBody &body,
                     const string &method,
                     int static_codecs_id);

/**
 * @brief transform reply SDP in both directions between legs
 * @param[in] call
 * @param[in,out] body
 * @param[in] method
 * @param[in,out] negotiated_media
 * @param[in] single_codec
 * @param[in] noaudio_streams_filtered. is noaudo streams filtering is enabled
 * @return 0 if succ. negative value with error code on errors
 */
int filterReplySdp(SBCCallLeg *call,
                   AmMimeBody &body,
                   const string &method,
				   vector<SdpMedia> &negotiated_media,
				   bool single_codec,
				   bool noaudio_streams_filtered);

#endif // SDP_FILTER_H
