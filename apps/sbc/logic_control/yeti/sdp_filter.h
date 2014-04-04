#ifndef SDP_FILTER_H
#define SDP_FILTER_H

#include <string>
using std::string;
#include <vector>
using std::vector;

#include <AmSdp.h>
#include "SBCCallLeg.h"

#define DTMF_ENCODING_NAME "telephone-event"

void dump_SdpPayload(const vector<SdpPayload> &p,string prefix="");
void fix_dynamic_payloads(AmSdp &sdp,PayloadIdMapping &mapping);

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
					   const std::vector<SdpPayload> static_payloads,
					   bool add_codecs,
					   bool single_codec);


/**
 * @brief decide whether or not we can accept codecs in incoming INVITE
 * according to static_codecs_aleg preference
 * @param[in] call_profile current legA callprofile
 * @param[in] body
 * @param[out] negotiated_media
 * @param[in] method
 * @return 0 if succ. negative value with error code on errors
 */
int filterInviteSdp(SBCCallProfile &call_profile,
					AmSipRequest &req, vector<SdpMedia> &negotiated_media,
					const string &method);

/**
 * @brief transform request SDP in both directions between legs
 * @param[in] call
 * @param[in,out] body
 * @param[in] method
 * @return 0 if succ. negative value with error code on errors
 */
int filterRequestSdp(SBCCallLeg *call,
                     AmMimeBody &body, const string &method);

/**
 * @brief transform reply SDP in both directions between legs
 * @param[in] call
 * @param[in,out] body
 * @param[in] method
 * @return 0 if succ. negative value with error code on errors
 */
int filterReplySdp(SBCCallLeg *call,
                   AmMimeBody &body, const string &method);

#endif // SDP_FILTER_H