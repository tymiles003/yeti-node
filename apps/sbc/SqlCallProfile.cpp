#include "SqlCallProfile.h"
#include "AmUtils.h"
#include "SBC.h"
#include <algorithm>

SqlCallProfile::SqlCallProfile()
{
    DBG("SqlCallProfile(%p)",this);
    profile_file = "SQL";
}

SqlCallProfile::~SqlCallProfile()
{
    DBG("~SqlCallProfile(%p)",this);
}

bool SqlCallProfile::readFromTuple(const pqxx::result::tuple &t){

	profile_file = "SQL";

	ruri = t["ruri"].c_str();
	ruri_host = t["ruri_host"].c_str();
	from = t["from"].c_str();
	to = t["to"].c_str();
	//contact = t["Contact"].c_str();

	callid = t["call_id"].c_str();

	transparent_dlg_id = t["transparent_dlg_id"].as<bool>(false);
	dlg_nat_handling = t["dlg_nat_handling"].as<bool>(false);

	force_outbound_proxy = t["force_outbound_proxy"].as<bool>(false);
	outbound_proxy = t["outbound_proxy"].c_str();

	aleg_force_outbound_proxy = t["aleg_force_outbound_proxy"].as<bool>(false);
	aleg_outbound_proxy = t["aleg_outbound_proxy"].c_str();

	next_hop = t["next_hop"].c_str();
	next_hop_1st_req = t["next_hop_1st_req"].as<bool>(false);

	aleg_next_hop = t["aleg_next_hop"].c_str();

	if (!readFilter(t, "header_filter", "header_list", headerfilter, false))
		return false;

	if (!readFilter(t, "message_filter", "message_list", messagefilter, false))
		return false;

	if (!readFilter(t, "sdp_filter", "sdpfilter_list", sdpfilter, true))
		return false;

	// SDP alines filter
	if (!readFilter(t, "sdp_alines_filter", "sdp_alinesfilter_list",
			sdpalinesfilter, false))
		return false;

	sst_enabled = t["enable_session_timer"].as<bool>(false)?"yes":"no";
	if(column_exist(t,"enable_aleg_session_timer")) {
		sst_aleg_enabled = t["enable_aleg_session_timer"].as<bool>(false)?"yes":"no";
	} else {
		sst_aleg_enabled = sst_enabled;
	}

#define CP_SST_CFGVAR(cfgprefix, cfgkey, dstcfg)			\
	if (column_exist(t,cfgprefix cfgkey)) {				\
	dstcfg.setParameter(cfgkey, t[cfgprefix cfgkey].c_str());	\
	} else if (column_exist(t,cfgkey)) {				\
		dstcfg.setParameter(cfgkey, t[cfgkey].c_str());		\
	} else if (SBCFactory::instance()->cfg.hasParameter(cfgkey)) {	\
	  dstcfg.setParameter(cfgkey, SBCFactory::instance()->		\
			  cfg.getParameter(cfgkey));			\
	}

	if (sst_enabled.size() && sst_enabled != "no") {
		if (NULL == SBCFactory::instance()->session_timer_fact) {
			ERROR("session_timer module not loaded thus SST not supported, but required");
			return false;
		}

		sst_b_cfg.setParameter("enable_session_timer", "yes");
		// create sst_cfg with values from aleg_*
		CP_SST_CFGVAR("", "session_expires", sst_b_cfg);
		CP_SST_CFGVAR("", "minimum_timer", sst_b_cfg);
		CP_SST_CFGVAR("", "maximum_timer", sst_b_cfg);
		CP_SST_CFGVAR("", "session_refresh_method", sst_b_cfg);
		CP_SST_CFGVAR("", "accept_501_reply", sst_b_cfg);
	}

	if (sst_aleg_enabled.size() && sst_aleg_enabled != "no") {
		sst_a_cfg.setParameter("enable_session_timer", "yes");
		// create sst_a_cfg superimposing values from aleg_*
		CP_SST_CFGVAR("aleg_", "session_expires", sst_a_cfg);
		CP_SST_CFGVAR("aleg_", "minimum_timer", sst_a_cfg);
		CP_SST_CFGVAR("aleg_", "maximum_timer", sst_a_cfg);
		CP_SST_CFGVAR("aleg_", "session_refresh_method", sst_a_cfg);
		CP_SST_CFGVAR("aleg_", "accept_501_reply", sst_a_cfg);
	}
#undef CP_SST_CFGVAR

	auth_enabled = t["enable_auth"].as<bool>(false);
	auth_credentials.user = t["auth_user"].c_str();
	auth_credentials.pwd = t["auth_pwd"].c_str();

	auth_aleg_enabled = t["enable_aleg_auth"].as<bool>(false);
	auth_aleg_credentials.user = t["auth_aleg_user"].c_str();
	auth_aleg_credentials.pwd = t["auth_aleg_pwd"].c_str();


	vector<string> reply_translations_v =
			explode(t["reply_translations"].c_str(), "|");
	for (vector<string>::iterator it =
			reply_translations_v.begin(); it != reply_translations_v.end(); it++) {
		// expected: "603=>488 Not acceptable here"
		vector<string> trans_components = explode(*it, "=>");
		if (trans_components.size() != 2) {
			ERROR("entry '%s' in reply_translations could not be understood.\n", it->c_str());
			ERROR("expected 'from_code=>to_code reason'\n");
			return false;
		}

		unsigned int from_code, to_code;
		if (str2i(trans_components[0], from_code)) {
			ERROR("code '%s' in reply_translations not understood.\n", trans_components[0].c_str());
			return false;
		}
		unsigned int s_pos = 0;
		string to_reply = trans_components[1];
		while (s_pos < to_reply.length() && to_reply[s_pos] != ' ')
		s_pos++;
		if (str2i(to_reply.substr(0, s_pos), to_code)) {
			ERROR("code '%s' in reply_translations not understood.\n", to_reply.substr(0, s_pos).c_str());
			return false;
		}
		if (s_pos < to_reply.length())
			s_pos++;
		// DBG("got translation %u => %u %s\n",
		// 	from_code, to_code, to_reply.substr(s_pos).c_str());
		reply_translations[from_code] = make_pair(to_code, to_reply.substr(s_pos));
	}

	append_headers = t["append_headers"].c_str();
	append_headers_req = t["append_headers_req"].c_str();
	aleg_append_headers_req = t["aleg_append_headers_req"].c_str();

	refuse_with = t["refuse_with"].c_str();

	rtprelay_enabled = t["enable_rtprelay"].as<bool>(false);
	force_symmetric_rtp = t["rtprelay_force_symmetric_rtp"].as<bool>(false)?"yes":"no";
	aleg_force_symmetric_rtp = t["aleg_rtprelay_force_symmetric_rtp"].as<bool>(false)?"yes":"no";
	msgflags_symmetric_rtp = t["rtprelay_msgflags_symmetric_rtp"].as<bool>(false);

	rtprelay_interface = t["rtprelay_interface"].c_str();
	aleg_rtprelay_interface = t["aleg_rtprelay_interface"].c_str();

	rtprelay_transparent_seqno = t["rtprelay_transparent_seqno"].as<bool>(false);
	rtprelay_transparent_ssrc = t["rtprelay_transparent_ssrc"].as<bool>(true);

	outbound_interface = t["outbound_interface"].c_str();
	aleg_outbound_interface = t["aleg_outbound_interface"].c_str();

	contact.displayname = t["contact_displayname"].c_str();
	contact.user = t["contact_user"].c_str();
	contact.host = t["contact_host"].c_str();
	contact.port = t["contact_port"].c_str();

	contact.hiding = t["enable_contact_hiding"].as<bool>(false);
	contact.hiding_prefix = t["contact_hiding_prefix"].c_str();
	contact.hiding_vars = t["contact_hiding_vars"].c_str();

	if (!readCodecPrefs(t)) return false;
	if (!readTranscoder(t)) return false;

	msg_logger_path = t["msg_logger_path"].c_str();
	log_rtp = t["log_rtp"].as<bool>(false);
	log_sip = t["log_sip"].as<bool>(true);

	reg_caching = t["enable_reg_caching"].as<bool>(false);
	min_reg_expires = t["min_reg_expires"].as<int>(0);
	max_ua_expires = t["max_ua_expires"].as<int>(0);

	time_limit=t["time_limit"].as<int>(0);
	resources = t["resources"].c_str();
	cached = false;

	INFO("Yeti: loaded SQL profile\n");

	if (!refuse_with.empty()) {
		INFO("SBC:      refusing calls with '%s'\n", refuse_with.c_str());
	} else {
		INFO("SBC:      RURI      = '%s'\n", ruri.c_str());
		INFO("SBC:      RURI-host = '%s'\n", ruri_host.c_str());
		INFO("SBC:      From = '%s'\n", from.c_str());
		INFO("SBC:      To   = '%s'\n", to.c_str());
		// if (!contact.empty()) {
		//   INFO("SBC:      Contact   = '%s'\n", contact.c_str());
		// }
		if (!callid.empty()) {
			INFO("SBC:      Call-ID   = '%s'\n", callid.c_str());
		}

		INFO("SBC:      force outbound proxy: %s\n", force_outbound_proxy?"yes":"no");
		INFO("SBC:      outbound proxy = '%s'\n", outbound_proxy.c_str());

		if (!outbound_interface.empty()) {
			INFO("SBC:      outbound interface = '%s'\n", outbound_interface.c_str());
		}

		if (!aleg_outbound_interface.empty()) {
			INFO("SBC:      A leg outbound interface = '%s'\n", aleg_outbound_interface.c_str());
		}

		INFO("SBC:      A leg force outbound proxy: %s\n", aleg_force_outbound_proxy?"yes":"no");
		INFO("SBC:      A leg outbound proxy = '%s'\n", aleg_outbound_proxy.c_str());

		if (!next_hop.empty()) {
			INFO("SBC:      next hop = %s (%s)\n", next_hop.c_str(),
			next_hop_1st_req ? "1st req" : "all reqs");
		}

		if (!aleg_next_hop.empty()) {
			INFO("SBC:      A leg next hop = %s\n", aleg_next_hop.c_str());
		}

		string filter_type; size_t filter_elems;
		filter_type = headerfilter.size() ? FilterType2String(headerfilter.back().filter_type) : "disabled";
		filter_elems = headerfilter.size() ? headerfilter.back().filter_list.size() : 0;
		INFO("SBC:      header filter  is %s, %zd items in list\n", filter_type.c_str(), filter_elems);

		filter_type = messagefilter.size() ? FilterType2String(messagefilter.back().filter_type) : "disabled";
		filter_elems = messagefilter.size() ? messagefilter.back().filter_list.size() : 0;
		INFO("SBC:      message filter is %s, %zd items in list\n", filter_type.c_str(), filter_elems);

		filter_type = sdpfilter.size() ? FilterType2String(sdpfilter.back().filter_type) : "disabled";
		filter_elems = sdpfilter.size() ? sdpfilter.back().filter_list.size() : 0;
		INFO("SBC:      SDP filter is %sabled, %s, %zd items in list, %sanonymizing SDP\n",
		sdpfilter.size()?"en":"dis", filter_type.c_str(), filter_elems, anonymize_sdp?"":"not ");

		filter_type = sdpalinesfilter.size() ? FilterType2String(sdpalinesfilter.back().filter_type) : "disabled";
		filter_elems = sdpalinesfilter.size() ? sdpalinesfilter.back().filter_list.size() : 0;
		INFO("SBC:      SDP alines-filter is %sabled, %s, %zd items in list\n", sdpalinesfilter.size()?"en":"dis", filter_type.c_str(), filter_elems);

		INFO("SBC:      RTP relay %sabled\n", rtprelay_enabled?"en":"dis");
		if (rtprelay_enabled) {
			if (!force_symmetric_rtp.empty()) {
				INFO("SBC:      RTP force symmetric RTP: %s\n",
				force_symmetric_rtp.c_str());
			}
			if (msgflags_symmetric_rtp) {
				INFO("SBC:      P-MsgFlags symmetric RTP detection enabled\n");
			}
			if (!aleg_rtprelay_interface.empty()) {
				INFO("SBC:      RTP Relay interface A leg '%s'\n", aleg_rtprelay_interface.c_str());
			}
			if (!rtprelay_interface.empty()) {
				INFO("SBC:      RTP Relay interface B leg '%s'\n", rtprelay_interface.c_str());
			}

			INFO("SBC:      RTP Relay %s seqno\n",
			rtprelay_transparent_seqno?"transparent":"opaque");
			INFO("SBC:      RTP Relay %s SSRC\n",
			rtprelay_transparent_ssrc?"transparent":"opaque");
		}

		INFO("SBC:      SST on A leg enabled: '%s'\n", sst_aleg_enabled.empty() ? "no" : sst_aleg_enabled.c_str());
		if (sst_aleg_enabled.size() && sst_aleg_enabled != "no") {
			INFO("SBC:              session_expires=%s\n",
			sst_a_cfg.getParameter("session_expires").c_str());
			INFO("SBC:              minimum_timer=%s\n",
			sst_a_cfg.getParameter("minimum_timer").c_str());
			INFO("SBC:              maximum_timer=%s\n",
			sst_a_cfg.getParameter("maximum_timer").c_str());
			INFO("SBC:              session_refresh_method=%s\n",
			sst_a_cfg.getParameter("session_refresh_method").c_str());
			INFO("SBC:              accept_501_reply=%s\n",
			sst_a_cfg.getParameter("accept_501_reply").c_str());
		}
		INFO("SBC:      SST on B leg enabled: '%s'\n", sst_enabled.empty() ? "no" : sst_enabled.c_str());
		if (sst_enabled.size() && sst_enabled != "no") {
			INFO("SBC:              session_expires=%s\n",
			sst_b_cfg.getParameter("session_expires").c_str());
			INFO("SBC:              minimum_timer=%s\n",
			sst_b_cfg.getParameter("minimum_timer").c_str());
			INFO("SBC:              maximum_timer=%s\n",
			sst_b_cfg.getParameter("maximum_timer").c_str());
			INFO("SBC:              session_refresh_method=%s\n",
			sst_b_cfg.getParameter("session_refresh_method").c_str());
			INFO("SBC:              accept_501_reply=%s\n",
			sst_b_cfg.getParameter("accept_501_reply").c_str());
		}

		INFO("SBC:      SIP auth %sabled\n", auth_enabled?"en":"dis");
		INFO("SBC:      SIP auth for A leg %sabled\n", auth_aleg_enabled?"en":"dis");

		if (reply_translations.size()) {
			string reply_trans_codes;
			for(map<unsigned int, std::pair<unsigned int, string> >::iterator it=
					reply_translations.begin(); it != reply_translations.end(); it++)
				reply_trans_codes += int2str(it->first)+", ";
			reply_trans_codes.erase(reply_trans_codes.length()-2);
			INFO("SBC:      reply translation for  %s\n", reply_trans_codes.c_str());
		}

	}

	if (append_headers.size()) {
		INFO("SBC:      append headers '%s'\n", append_headers.c_str());
	}

	INFO("SBC:      reg-caching: '%s'\n", reg_caching ? "yes" : "no");
	INFO("SBC:      min_reg_expires: %i\n", min_reg_expires);
	INFO("SBC:      max_ua_expires: %i\n", max_ua_expires);

	INFO("SBC:      time_limit: %i\n", time_limit);
	INFO("SBC:      resources: %s\n", resources.c_str());

	codec_prefs.infoPrint();
	transcoder.infoPrint();

	return true;
}

bool SqlCallProfile::readFilter(const pqxx::result::tuple &t, const char* cfg_key_filter, const char* cfg_key_list,
		vector<FilterEntry>& filter_list, bool keep_transparent_entry){
	string filter = t[cfg_key_filter].c_str();
	if (filter.empty())
		return true;

	FilterEntry hf;
	hf.filter_type = String2FilterType(filter.c_str());
	if (Undefined == hf.filter_type) {
		ERROR("invalid %s mode '%s'\n", cfg_key_filter, filter.c_str());
		return false;
	}

	// no transparent filter
	if (!keep_transparent_entry && hf.filter_type==Transparent)
	return true;

	vector<string> elems = explode(t[cfg_key_list].c_str(), ",");
	for (vector<string>::iterator it=elems.begin(); it != elems.end(); it++) {
		string c = *it;
		std::transform(c.begin(), c.end(), c.begin(), ::tolower);
		hf.filter_list.insert(c);
	}

	filter_list.push_back(hf);
	return true;
}

bool SqlCallProfile::readCodecPrefs(const pqxx::result::tuple &t){
	codec_prefs.bleg_payload_order_str = t["codec_preference"].c_str();
	codec_prefs.bleg_prefer_existing_payloads_str = t["prefer_existing_codecs"].c_str();

	codec_prefs.aleg_payload_order_str = t["codec_preference_aleg"].c_str();
	codec_prefs.aleg_prefer_existing_payloads_str = t["prefer_existing_codecs_aleg"].c_str();

	return true;
}

bool SqlCallProfile::readTranscoder(const pqxx::result::tuple &t){
	// store string values for later evaluation
	transcoder.audio_codecs_str = t["transcoder_codecs"].c_str();
	transcoder.callee_codec_capabilities_str = t["callee_codeccaps"].c_str();
	transcoder.transcoder_mode_str = t["enable_transcoder"].c_str();
	transcoder.dtmf_mode_str = t["dtmf_transcoding"].c_str();
	transcoder.lowfi_codecs_str = t["lowfi_codecs"].c_str();
	transcoder.audio_codecs_norelay_str = t["prefer_transcoding_for_codecs"].c_str();
	transcoder.audio_codecs_norelay_aleg_str = t["prefer_transcoding_for_codecs_aleg"].c_str();

	return true;
}

bool SqlCallProfile::column_exist(const pqxx::result::tuple &t,string column_name){
	try {
		t.column_number(column_name);
		return true;
	} catch(...){
		DBG("%s column: %s",FUNC_NAME,column_name.c_str());
	}
	return false;
}
