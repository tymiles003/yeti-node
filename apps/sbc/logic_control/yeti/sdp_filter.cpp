#include "sdp_filter.h"
#include "log.h"

#include <algorithm>
#include "SDPFilter.h"
#include "CallCtx.h"
#include "CodecsGroup.h"


const char *conn_location2str(int location_id){
	static const char *both = "both";
	static const char *session_only = "session_only";
	static const char *media_only = "media_only";
	static const char *unknown = "unknown";
	switch(location_id){
		case BOTH: return both; break;
		case SESSION_ONLY: return session_only; break;
		case MEDIA_ONLY: return media_only; break;
		default: return unknown; break;
	}
}

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
	DBG("DUMP SdpMedia %s %p:",prefix.c_str(),&m);
	unsigned stream_idx = 0;
	for (vector<SdpMedia>::const_iterator j = m.begin(); j != m.end(); ++j) {
		const SdpMedia &media = *j;
		DBG("media[%p] conn = %s",&media,media.conn.debugPrint().c_str());
		if (media.type == MT_AUDIO) {
			DBG("sdpmedia '%s' audio stream %d, port %d:",prefix.c_str(),
				stream_idx,media.port);
			dump_SdpPayload(j->payloads,prefix);
			stream_idx++;
		} else {
			DBG("sdpmedia '%s' %s stream, port %d",prefix.c_str(),
				media.type2str(media.type).c_str(),media.port);
		}
	}
}

static const SdpPayload *findPayload(const std::vector<SdpPayload>& payloads, const SdpPayload &payload, int transport)
{
//#define DBG_FP(...) DBG(__VA_ARGS__)
#define DBG_FP(...) ;

	string pname = payload.encoding_name;
	transform(pname.begin(), pname.end(), pname.begin(), ::tolower);

	DBG_FP("findPayload: payloads[%p] transport = %d, payload = {%d,'%s'/%d/%d}",
		&payloads,transport,
		payload.payload_type,payload.encoding_name.c_str(),
		payload.clock_rate,payload.encoding_param);

	bool static_payload = (transport == TP_RTPAVP && payload.payload_type >= 0 && payload.payload_type < 20);
	for (vector<SdpPayload>::const_iterator p = payloads.begin(); p != payloads.end(); ++p) {
		DBG_FP("findPayload: next payload payload = {%d,'%s'/%d/%d}",
			p->payload_type,p->encoding_name.c_str(),
			p->clock_rate, p->encoding_param);
		// fix for clients using non-standard names for static payload type (SPA504g: G729a)
		if (static_payload) {
			if (payload.payload_type != p->payload_type) {
				string s = p->encoding_name;
				transform(s.begin(), s.end(), s.begin(), ::tolower);
				if (s != pname) {
					DBG_FP("findPayload: static payload. types not matched. names not matched");
					continue;
				}
			}
		} else {
			string s = p->encoding_name;
			transform(s.begin(), s.end(), s.begin(), ::tolower);
			if (s != pname){
				DBG_FP("findPayload: dynamic payload. names not matched");
				continue;
			}
		}
		if (p->clock_rate > 0 && (p->clock_rate != payload.clock_rate)) {
			DBG_FP("findPayload: clock rates not matched");
			continue;
		}
		if ((p->encoding_param >= 0) && (payload.encoding_param >= 0) &&
			(p->encoding_param != payload.encoding_param)) {
			DBG_FP("findPayload: encoding params not matched");
			continue;
		}
		DBG_FP("findPayload: payloads matched");
		return &(*p);
	}
	return NULL;
#undef DBG_FP
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

static bool all_media_conn_equal(const AmSdp &sdp, SdpConnection &conn){
	bool all_is_equal = true;
	for(std::vector<SdpMedia>::const_iterator m = sdp.media.begin();
			m!=sdp.media.end();++m){
		const SdpConnection &c = m->conn;
		if(!c.address.empty()){
			if(conn.address.empty()){
				conn = c;
				continue;
			} else {
				if(!(conn==c)){
					DBG("%s mismatched with %s",
						conn.debugPrint().c_str(),c.debugPrint().c_str());
					all_is_equal = false;
					break;
				}
			}
		}
	}
	return all_is_equal;
}

static bool assert_session_conn(AmSdp &sdp){
	if(!sdp.conn.address.empty())
		return true; //already have session conn

	bool have_session_level = false;

	if(sdp.media.size()>1){
		//we have several streams. check conn eq for them
		//it's cheking for global conn line possibility
		SdpConnection conn;
		bool all_is_equal = all_media_conn_equal(sdp,conn);
		if(all_is_equal && !conn.address.empty()){
			sdp.conn = conn;
			have_session_level = true;
			DBG("propagate media level conn %s to session level",
				sdp.conn.debugPrint().c_str());
		}
	} else {
		//just [0..1] stream. propagate it's address to the session level
		if(sdp.media.size()){
			const SdpConnection &conn = sdp.media.begin()->conn;
			if(!conn.address.empty()){
				sdp.conn = conn;
				have_session_level = true;
				DBG("propagate media level conn %s to session level",
					sdp.conn.debugPrint().c_str());
			}
		}
	}
	return have_session_level;
}

static bool assert_media_conn(AmSdp &sdp){
	if(sdp.conn.address.empty()){
		DBG("assert_media_conn no session level conn");
		return false; //no session level conn. give up
	}

	bool changed = false;
	int stream_idx = 0;
	for(std::vector<SdpMedia>::iterator m = sdp.media.begin();
			m!=sdp.media.end();++m,++stream_idx){
		if(m->conn.address.empty()){
			m->conn = sdp.conn;
			changed = true;
			DBG("propagate session level %s for media stream %d",
				sdp.conn.debugPrint().c_str(),stream_idx);
		}
	}
	return changed;
}

static void remove_media_conn(AmSdp &sdp){
	int stream_idx = 0;
	for(std::vector<SdpMedia>::iterator m = sdp.media.begin();
			m!=sdp.media.end();++m,++stream_idx){
		if(!m->conn.address.empty()){
			DBG("remove conn %s from media stream %d",
				m->conn.debugPrint().c_str(),stream_idx);
			m->conn = SdpConnection();
		}
	}
}

void normalize_conn_location(AmSdp &sdp, int location_id){
	DBG("normalise_conn_location(%p,%s)",&sdp,conn_location2str(location_id));
	switch(location_id){
	case BOTH: {
		assert_session_conn(sdp);
		assert_media_conn(sdp);
	} break;
	case SESSION_ONLY: {
		if(assert_session_conn(sdp)){
			//we got session level conn. clean conn from all streams
			remove_media_conn(sdp);
		}
	} break;
	case MEDIA_ONLY: {
		assert_session_conn(sdp);
		assert_media_conn(sdp);
		sdp.conn = SdpConnection();
	} break;
	default:
		ERROR("unknown conn_location_id = %d",location_id);
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

	dump_SdpMedia(sdp.media,"filter_arrange_SDP_in");

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
				SdpPayload new_p = *p;
				/*! TODO: should be changed to replace with params from codec group */
				if(add_codecs){
					SdpPayload new_p = *p;
					new_p.format.clear();
					//override sdp_format_parameters from static codecs
					new_p.sdp_format_parameters = f_it->sdp_format_parameters;
					//override payload_type
					if(new_p.payload_type >= DYNAMIC_PAYLOAD_TYPE_START &&
							f_it->payload_type != -1){
						new_p.payload_type = f_it->payload_type;
					}
					new_pl.push_back(new_p);
				} else {
					new_pl.push_back(*p);
				}
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


	dump_SdpMedia(sdp.media,"filter_arrange_SDP_out");

	if ((!media_line_left) && media_line_filtered_out) {
		DBG("all streams were marked as inactive\n");
		return -488;
	}
	return 0;
}

int filterNoAudioStreams(AmSdp &sdp, bool filter){
	if(!filter) return 0;

	for (std::vector<SdpMedia>::iterator m_it = sdp.media.begin(); m_it != sdp.media.end(); m_it++)
	{
		SdpMedia& media = *m_it;
		if(media.type!=MT_AUDIO){
			media.port = 0;
		}
	}
	return 0;
}

int cutNoAudioStreams(AmSdp &sdp, bool cut){
	if(!cut) return 0;

	vector<SdpMedia> new_media;

	for (vector<SdpMedia>::iterator m_it = sdp.media.begin(); m_it != sdp.media.end(); m_it++)
	{
		SdpMedia& m = *m_it;
		if(m.type==MT_AUDIO){
			new_media.push_back(m);
		}
	}
	if(!new_media.size()){
		return -488;
	}
	sdp.media = new_media;
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

	filterNoAudioStreams(sdp,call_profile.filter_noaudio_streams);

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

	dump_SdpMedia(sdp.media,"filterRequestSdp_in");

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

	filterSDPalines(sdp, a_leg ?
						call_profile.sdpalinesfilter :
						call_profile.bleg_sdpalinesfilter);

	res = cutNoAudioStreams(sdp,call_profile.filter_noaudio_streams);
	if(0 != res){
		ERROR("filterRequestSdp() no streams after no audio streams filtering");
		return res;
	}

	normalize_conn_location(sdp, a_leg ?
								call_profile.bleg_conn_location_id :
								call_profile.aleg_conn_location_id);

	dump_SdpMedia(sdp.media,"filterRequestSdp_out");

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

/*void copy_media_conn(SdpMedia &dst,const SdpMedia &src)
{
	dst.port = src.port;
	dst.conn = src.conn;
}

void copy_sdp_conn(SdpMedia &dst,const SdpMedia &src)
{
	//
}*/

int filterReplySdp(SBCCallLeg *call,
				   AmMimeBody &body, const string &method,
				   vector<SdpMedia> &negotiated_media,
				   bool single_codec,
				   bool noaudio_streams_filtered)
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

	dump_SdpMedia(negotiated_media,"filterReplySdp_negotiated_media");
	dump_SdpMedia(sdp.media,"filterReplySdp_in");

	if(negotiated_media.size()){

		if(!sdp.media.size()){
			ERROR("filterReplySdp() empty answer sdp");
			return -488;
		}

		if(negotiated_media.size()!=sdp.media.size()){
			if(noaudio_streams_filtered){
				//count audio streams
				unsigned int audio_streams = 0;
				for(vector<SdpMedia>::const_iterator it = negotiated_media.begin();
						it!=negotiated_media.end();++it)
				{
					if(it->type==MT_AUDIO)
						audio_streams++;

				}
				if(sdp.media.size()!=audio_streams){
					ERROR("filterReplySdp() audio streams count not equal reply: %lu, saved: %u)",
						  sdp.media.size(),audio_streams);
					return -488;
				}
			} else {
				ERROR("filterReplySdp() streams count not equal reply: %lu, saved: %lu)",
					sdp.media.size(),negotiated_media.size());
				return -488;
			}
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

			if(m.type!=MT_AUDIO)
				continue;

			//remove all unknown attributes
			//m.attributes.clear();

			dump_SdpPayload(m.payloads,"m.payloads");
			dump_SdpPayload(other_m.payloads,"other_m.payloads");

			std::vector<SdpPayload> new_pl;
			if(!call_profile.avoid_transcoding){
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
			dump_SdpPayload(new_pl,"new_pl");
			m.payloads = new_pl;

			stream_idx++;
		}

		//! HACK: add nonaudio media which presented in negotiated media to the end
		if(noaudio_streams_filtered){
			other_media_it = negotiated_media.begin();
			for(;other_media_it!=negotiated_media.end();++other_media_it){
				if(other_media_it->type!=MT_AUDIO){
					sdp.media.push_back(*other_media_it);
				}
			}
		}

	} else {
		WARN("no negotiated media for leg%s. leave it as is",a_leg ? "A" : "B");
	}
	fix_dynamic_payloads(sdp,call->getTranscoderMapping());
	filterSDPalines(sdp, a_leg ?
						call_profile.sdpalinesfilter :
						call_profile.bleg_sdpalinesfilter);

	normalize_conn_location(sdp, a_leg ?
								call_profile.bleg_conn_location_id :
								call_profile.aleg_conn_location_id);

	dump_SdpMedia(sdp.media,"filterReplySdp_out");

	negotiated_media = sdp.media;

	//update body
	string n_body;
	sdp.print(n_body);
	sdp_body->setPayload((const unsigned char*)n_body.c_str(), n_body.length());

	return res;
}

