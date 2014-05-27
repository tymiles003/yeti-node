#include "sdp_filter.h"
#include "log.h"

#include <algorithm>
#include "SDPFilter.h"
#include "CallCtx.h"
#include "CodecsGroup.h"

void dump_SdpPayload(const vector<SdpPayload> &p,string prefix){
	if(!prefix.empty())
		prefix.insert(0,"for ");
	DBG("dump SdpPayloads %s %p:",prefix.c_str(),&p);
	if(!p.size()){
		DBG("    empty payloads container");
		return;
	}
	for (std::vector<SdpPayload>::const_iterator p_it =
		 p.begin();p_it != p.end(); p_it++)
	{
		const SdpPayload &s = *p_it;
		DBG("    type: %d, payload_type: %d, encoding_name: '%s'', format: '%s'', sdp_format_parameters: '%s'",
			s.type,s.payload_type,s.encoding_name.c_str(),
			s.format.c_str(),s.sdp_format_parameters.c_str());
	}
}

void dump_SdpMedia(const vector<SdpMedia> &m,string prefix){
	DBG("dump SdpMedia %s %p:",prefix.c_str(),&m);
	unsigned stream_idx = 0;
	for (vector<SdpMedia>::const_iterator j = m.begin(); j != m.end(); ++j) {
		if (j->type == MT_AUDIO) {
			DBG("sdpmedia '%s' audio stream %d:",prefix.c_str(),stream_idx);
			dump_SdpPayload(j->payloads,prefix);
			stream_idx++;
		}
	}
}

static const SdpPayload *findPayload(const std::vector<SdpPayload>& payloads, const SdpPayload &payload, int transport)
{
	string pname = payload.encoding_name;
	transform(pname.begin(), pname.end(), pname.begin(), ::tolower);

	for (vector<SdpPayload>::const_iterator p = payloads.begin(); p != payloads.end(); ++p) {
		// fix for clients using non-standard names for static payload type (SPA504g: G729a)
		if (transport == TP_RTPAVP && payload.payload_type < 20) {
			if (payload.payload_type != p->payload_type) {
				string s = p->encoding_name;
				transform(s.begin(), s.end(), s.begin(), ::tolower);
				if (s != pname) continue;
			}
		} else {
			string s = p->encoding_name;
			transform(s.begin(), s.end(), s.begin(), ::tolower);
			if (s != pname) continue;
		}
		if (p->clock_rate != payload.clock_rate) continue;
		if ((p->encoding_param >= 0) && (payload.encoding_param >= 0) &&
			(p->encoding_param != payload.encoding_param)) continue;
		return &(*p);
	}
	return NULL;
}

static bool containsPayload(const std::vector<SdpPayload>& payloads, const SdpPayload &payload, int transport)
{
	return findPayload(payloads, payload, transport) != NULL;
}

void fix_dynamic_payloads(AmSdp &sdp,PayloadIdMapping &mapping){
	unsigned stream_idx = 0;
	for (vector<SdpMedia>::iterator m = sdp.media.begin(); m != sdp.media.end(); ++m) {
		if (m->type == MT_AUDIO) {
			int id = 96;
			unsigned idx = 0;
			PayloadMask used_payloads;
			for(std::vector<SdpPayload>::iterator i = m->payloads.begin();
				i!=m->payloads.end(); ++i, ++idx)
			{
				int &pid = i->payload_type;
				if (pid < 0) {
					pid = mapping.get(stream_idx, idx);
				}
				if ((pid < 0) || used_payloads.get(pid)) {
					 pid = id++;
				}
				used_payloads.set(pid);
				//!correct me. if we should change leg PayloadIdMapping here ?
				mapping.map(stream_idx,idx,pid);
			}
			stream_idx++;
		}
	}
}

int filter_arrange_SDP(AmSdp& sdp,
							  const std::vector<SdpPayload> &static_payloads,
							  bool add_codecs)
{
	//DBG("filter_arrange_SDP() add_codecs = %s", add_codecs?"yes":"no");

	bool media_line_filtered_out = false;
	bool media_line_left = false;
	int media_idx = 0;
	int stream_idx = 0;

	for (vector<SdpMedia>::iterator m_it =
		 sdp.media.begin();m_it != sdp.media.end(); m_it++)
	{ //iterate over SdpMedia
		vector<SdpPayload> new_pl;
		SdpMedia& media = *m_it;

		if(media.type!=MT_AUDIO){	//skip non audio media
			media_idx++;
			continue;
		}

		for(vector<SdpPayload>::const_iterator f_it = static_payloads.begin();
			f_it != static_payloads.end(); ++f_it)
		{ //iterate over arranged(!) filter entries
			const SdpPayload *p = findPayload(media.payloads,*f_it,media.transport);
			if(p!=NULL){
				new_pl.push_back(*p);
			} else if(add_codecs) {
				new_pl.push_back(*f_it);
			}
		}
		//dump_SdpPayload(new_pl);

		if(!new_pl.size() && media.payloads.size()) {
			new_pl.push_back(*media.payloads.begin());
			media.port = 0;
			media_line_filtered_out = true;
		} else {
			media_line_left = true;
		}

		media.payloads = new_pl;
		media_idx++;
		stream_idx++;
	}

	if ((!media_line_left) && media_line_filtered_out) {
		DBG("all streams were marked as inactive\n");
		return -488;
	}
	return 0;
}


int negotiateRequestSdp(SBCCallProfile &call_profile,
					AmSipRequest &req, vector<SdpMedia> &negotiated_media,
					const string &method,
					int static_codecs_id)
{
	DBG("negotiateRequestSdp() method = %s\n",method.c_str());
	AmMimeBody &body = req.body;
	AmMimeBody* sdp_body = body.hasContentType(SIP_APPLICATION_SDP);
	if (!sdp_body) return 0;

	if(method != SIP_METH_INVITE){
		DBG("negotiateRequestSdp() called for non invite method");
		return 0;
	}

	AmSdp sdp;
	int res = sdp.parse((const char *)sdp_body->getPayload());
	if (0 != res) {
		DBG("SDP parsing failed during body filtering!\n");
		return res;
	}

	CodecsGroupEntry codecs_group;
	CodecsGroups::instance()->get(static_codecs_id, codecs_group);

	vector<SdpPayload> static_codecs_filter = codecs_group.get_payloads();

	res = filter_arrange_SDP(sdp,static_codecs_filter, false);
	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	//save negotiated result for the future usage
	negotiated_media = sdp.media;

	dump_SdpPayload(static_codecs_filter,"static_codecs_filter");
	dump_SdpMedia(negotiated_media,"negotiateRequestSdp");

	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}

int filterRequestSdp(SBCCallLeg *call,
					 SBCCallProfile &call_profile,
					 AmMimeBody &body, const string &method,
					 int static_codecs_id)
{
	bool a_leg = call->isALeg();
	DBG("filterRequestSdp() method = %s, a_leg = %d\n",method.c_str(),a_leg);

	AmMimeBody* sdp_body = body.hasContentType(SIP_APPLICATION_SDP);
	if (!sdp_body) return 0;

	// filter body for given methods only
	if (!(method == SIP_METH_INVITE ||
		method == SIP_METH_UPDATE ||
		method == SIP_METH_PRACK ||
		method == SIP_METH_ACK)){
			//DBG("filterRequestSdp() ignore method");
			return 0;
	}

	AmSdp sdp;
	int res = sdp.parse((const char *)sdp_body->getPayload());
	if (0 != res) {
		ERROR("filterRequestSdp() SDP parsing failed during body filtering!\n");
		return res;
	}

	normalizeSDP(sdp, false, "");

	CodecsGroupEntry codecs_group;
	try {
		CodecsGroups::instance()->get(static_codecs_id,codecs_group);
	} catch(...){
		//!TODO: replace with correct InternalException throw
		ERROR("filterRequestSdp() can't find codecs group %d",
			  static_codecs_id);
		return -488;
	}

	std::vector<SdpPayload> &static_codecs = codecs_group.get_payloads();

	filter_arrange_SDP(sdp,static_codecs, true);
	fix_dynamic_payloads(sdp,call->getTranscoderMapping());

	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	//update body
	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}

//add payload into payloads list with checking
inline void add_codec(std::vector<SdpPayload> &pl,const SdpPayload &p,bool single_codec){
	if(!single_codec){
		pl.push_back(p);
	} else {
		if(pl.empty()){
			pl.push_back(p);
		} else {
			string c = p.encoding_name;
			std::transform(c.begin(), c.end(), c.begin(), ::toupper);
			if(c==DTMF_ENCODING_NAME){
				pl.push_back(p);
			}
		}
	}
}

int filterReplySdp(SBCCallLeg *call,
				   AmMimeBody &body, const string &method,
				   vector<SdpMedia> &negotiated_media,
				   bool single_codec)
{
	bool a_leg = call->isALeg();
	SBCCallProfile &call_profile = call->getCallProfile();

	DBG("filterReplySdp() method = %s, a_leg = %d\n",method.c_str(),a_leg);

	AmMimeBody* sdp_body = body.hasContentType(SIP_APPLICATION_SDP);
	if (!sdp_body) return 0;

	// filter body for given methods only
	if (!(method == SIP_METH_INVITE ||
		method == SIP_METH_UPDATE ||
		method == SIP_METH_PRACK ||
		method == SIP_METH_ACK))
	{
		DBG("filterReplySdp() ignore method");
		return 0;
	}

	AmSdp sdp;
	int res = sdp.parse((const char *)sdp_body->getPayload());
	if (0 != res) {
		DBG("filterReplySdp() SDP parsing failed during body filtering!\n");
		return res;
	}

	normalizeSDP(sdp, false, ""); // anonymization is done in the other leg to use correct IP address

	if(negotiated_media.size()){
		if(negotiated_media.size()!=sdp.media.size()){
			ERROR("filterReplySdp() streams count not equal reply: %ld, saved: %ld)",
				sdp.media.size(),negotiated_media.size());
			return -488;
		}

		int stream_idx = 0;
		vector<SdpMedia>::const_iterator other_media_it = negotiated_media.begin();
		for (vector<SdpMedia>::iterator m_it = sdp.media.begin();
			m_it != sdp.media.end(); ++m_it, ++other_media_it)
		{
			const SdpMedia &other_m = *other_media_it;
			SdpMedia& m = *m_it;

			if(m.type!=other_m.type){
				ERROR("filterReplySdp() streams types not matched idx = %d",stream_idx);
				dump_SdpPayload(other_m.payloads,"other_m payload "+int2str(stream_idx));
				return -488;
			}

			//remove all unknown attributes
			m.attributes.clear();

			if(m.type!=MT_AUDIO)
				continue;

			//dump_SdpPayload(m.payloads,"m.payloads");
			//dump_SdpPayload(other_m.payloads,"other_m.payloads");

			std::vector<SdpPayload> new_pl;
			if(!call_profile.avoid_transcoding){
				DBG("<<<< if(!call_profile.avoid_transcoding){");
				//clear all except of first codec and dtmf
				std::vector<SdpPayload>::const_iterator p_it = other_m.payloads.begin();
				for (;p_it != other_m.payloads.end(); p_it++){
					add_codec(new_pl,*p_it,single_codec);
				}
			} else {
				//arrange previously negotiated codecs according to received sdp

				/* fill with codecs from received sdp
				 * which exists in negotiated payload */
				std::vector<SdpPayload>::const_iterator f_it = m.payloads.begin();
				for(;f_it!=m.payloads.end();f_it++){
					const SdpPayload *p = findPayload(other_m.payloads,*f_it,m.transport);
					if(p!=NULL){
						add_codec(new_pl,*p,single_codec);
					}
				}
				/* add codecs from negotiated payload
				 * which doesn't exists in recevied sdp
				 * to the tail */
				std::vector<SdpPayload>::const_iterator p_it = other_m.payloads.begin();
				for (;p_it != other_m.payloads.end(); p_it++){
					if(!containsPayload(m.payloads,*p_it,m.transport)){
						add_codec(new_pl,*p_it,single_codec);
					}
				}
			}
			m.payloads = new_pl;

			stream_idx++;
		}
	} else {
		WARN("no negotiated media for leg%s. leave it as is",a_leg ? "A" : "B");
	}
	fix_dynamic_payloads(sdp,call->getTranscoderMapping());
	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	negotiated_media = sdp.media;
	//update body
	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}

