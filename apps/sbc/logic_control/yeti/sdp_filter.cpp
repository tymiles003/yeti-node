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

void fix_dynamic_payloads(AmSdp &sdp,PayloadIdMapping &mapping){
	DBG("fix_dynamic_payloads()");

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
							  const std::vector<SdpPayload> static_payloads,
							  bool add_codecs,
							  bool single_codec)
{
	DBG("filter_arrange_SDP() add_codecs = %s, single_codec = %s",
		add_codecs?"yes":"no",single_codec?"yes":"no");

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
			const SdpPayload &sp = *f_it;
			string p = sp.encoding_name,c;
			std::transform(p.begin(), p.end(), p.begin(), ::toupper);
			bool matched = false;
			std::vector<SdpPayload>::iterator p_it = media.payloads.begin();
			for (;p_it != media.payloads.end(); p_it++)
			{ //iterate over Sdp entries of certain SdpMedia
				c = p_it->encoding_name;
				std::transform(c.begin(), c.end(), c.begin(), ::toupper);
				if(c==p){
					//DBG("filter_arrange_SDP() matched %s. add to new payload",c.c_str());
					//new_pl.push_back(*p_it);
					matched = true;
					break; //each codec occurs in sdp only once, huh ?
				}
			}

			if(matched){	//is codec founded in original SDP ?
				//add matched codec from sdp
				if(!single_codec ||
					(single_codec &&
						(new_pl.empty()||c==DTMF_ENCODING_NAME)
					 )
				) new_pl.push_back(*p_it);
			} else if(add_codecs) {
				//add codec from static list anyway
				if(!single_codec ||
					(single_codec &&
						(new_pl.empty()||f_it->encoding_name==DTMF_ENCODING_NAME)
					 )
				) new_pl.push_back(*f_it);
			}
			/*if(add_codecs && !matched) {
				//we should add new codecs if it not matched in sdp
				//DBG("filter_arrange_SDP() add nonexistent payload '%s'",p.c_str());
				new_pl.push_back(*f_it);
			}*/
		}
		dump_SdpPayload(new_pl);

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
					bool single_codec,
					int static_codecs_id)
{
	DBG("negotiateRequestSdp() method = %s\n",method.c_str());
	AmMimeBody &body = req.body;
	AmMimeBody* sdp_body = body.hasContentType(SIP_APPLICATION_SDP);
	if (!sdp_body) return 0;

	if(method != SIP_METH_INVITE){
		DBG("filterInviteSdp() called for non invite method");
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

	res = filter_arrange_SDP(sdp,static_codecs_filter,
							 false,single_codec);
	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	//save negotiated result for the future usage
	negotiated_media = sdp.media;
	DBG("negotiated media size: %ld, sdp_size: %ld",
		negotiated_media.size(),sdp.media.size());

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
/*	int group_id = !a_leg ?
			call_profile.static_codecs_aleg_id : call_profile.static_codecs_bleg_id;*/

	/*vector<SdpMedia> &negotiated_media = !a_leg ?
			ctx->bleg_negotiated_media : ctx->aleg_negotiated_media;*/
	try {
		CodecsGroups::instance()->get(static_codecs_id,codecs_group);
	} catch(...){
		//!TODO: replace with correct InternalException throw
		ERROR("filterRequestSdp() can't find codecs group %d",
			  static_codecs_id);
		return -488;
	}

	std::vector<SdpPayload> &static_codecs = codecs_group.get_payloads();

	filter_arrange_SDP(sdp,static_codecs,
					   true,false/*TODO: implement 'bleg_single_codec processing*/);
	fix_dynamic_payloads(sdp,call->getTranscoderMapping());

	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	//update body
	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}


int filterReplySdp(SBCCallLeg *call,
				   AmMimeBody &body, const string &method,
				   const vector<SdpMedia> &negotiated_media)
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
		vector<SdpMedia>::const_iterator aleg_media_it = negotiated_media.begin();
		for (vector<SdpMedia>::iterator m_it = sdp.media.begin();
			m_it != sdp.media.end(); ++m_it, ++aleg_media_it)
		{
			const SdpMedia &aleg_m = *aleg_media_it;
			SdpMedia& m = *m_it;

			if(m.type!=aleg_m.type){
				ERROR("filterReplySdp() streams types not matched idx = %d",stream_idx);
				dump_SdpPayload(aleg_m.payloads,"aleg_m payload "+int2str(stream_idx));
				return -488;
			}

			//remove all unknown attributes
			m.attributes.clear();

			if(m.type!=MT_AUDIO)
				continue;

			m.payloads = aleg_m.payloads;
			//m.attributes.clear();

			stream_idx++;
		}
	} else {
		WARN("no negotiated media for leg%s. leave it as is",a_leg ? "A" : "B");
	}
	fix_dynamic_payloads(sdp,call->getTranscoderMapping());
	filterSDPalines(sdp, call_profile.sdpalinesfilter);

	//update body
	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}

