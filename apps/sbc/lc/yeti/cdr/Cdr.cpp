#include "Cdr.h"
#include "AmUtils.h"
#include "log.h"
#include "RTPParameters.h"
#include "TrustedHeaders.h"

static const char *updateAction2str(UpdateAction act){
	static const char *aStart = "Start";
	static const char *aConnect = "Connect";
	static const char *aEnd = "End";
	static const char *aWrite = "Write";
	static const char *aUnknown = "Unknown";

	switch(act){
		case Start:
			return aStart;
			break;
		case Connect:
			return aConnect;
			break;
		case End:
			return aEnd;
			break;
		case Write:
			return aWrite;
			break;
		default:
			return aUnknown;
	}
}

void Cdr::replace(string& s, const string& from, const string& to){
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != string::npos) {
		 s.replace(pos, from.length(), to);
		 pos += s.length();
	}
}

/*static AmRtpStream::ErrorsStats& operator +=(AmRtpStream::ErrorsStats lhs,const AmRtpStream::ErrorsStats &rhs){
	lhs.decode_errors+=rhs.decode_errors;
	lhs.out_of_buffer_errors+=rhs.out_of_buffer_errors;
	lhs.rtp_parse_errors+=rhs.rtp_parse_errors;
	return lhs;
}*/

void Cdr::init(){
    //initital values
	timerclear(&start_time);
	timerclear(&bleg_invite_time);
	timerclear(&connect_time);
	timerclear(&end_time);
	timerclear(&sip_10x_time);
	timerclear(&sip_18x_time);

    gettimeofday(&cdr_born_time, NULL);

	sip_early_media_present = false;
	trusted_hdrs_gw = false;
	TrustedHeaders::instance()->init_hdrs(trusted_hdrs);

    writed=false;
    suppress = false;
	inserted2list = false;

	disconnect_initiator_writed = false;
	aleg_reason_writed = false;
	bleg_reason_writed = false;
	disconnect_reason = "";
	disconnect_code = 0;
	disconnect_internal_reason = "Unhandled sequence";
	disconnect_internal_code = 0;
	disconnect_rewrited_reason = "";
	disconnect_rewrited_code = 0;

    legB_remote_port = 0;
    legB_local_port = 0;
    legA_remote_port = 0;
    legA_local_port = 0;

    legB_remote_ip = "";
    legB_local_ip = "";
    legA_remote_ip = "";
    legA_local_ip = "";

	msg_logger_path = "";
	dump_level_id = 0;

    time_limit = 0;

	attempt_num = 1;

	legA_bytes_recvd = legB_bytes_recvd = 0;
	legA_bytes_sent = legB_bytes_sent = 0;
}

void Cdr::update_sql(const SqlCallProfile &profile){
	DBG("Cdr::%s(SqlCallProfile)",FUNC_NAME);
	trusted_hdrs_gw = profile.trusted_hdrs_gw;
    outbound_proxy = profile.outbound_proxy;
    dyn_fields = profile.dyn_fields;
    time_limit = profile.time_limit;
    dump_level_id = profile.dump_level_id;
}

void Cdr::update_sbc(const SBCCallProfile &profile){
	DBG("Cdr::%s(SBCCallProfile)",FUNC_NAME);
	msg_logger_path = profile.get_logger_path();
	global_tag =  profile.global_tag;
}

void Cdr::update(const AmSipRequest &req){
	DBG("Cdr::%s(AmSipRequest)",FUNC_NAME);
	if(writed) return;
	legA_remote_ip = req.remote_ip;
    legA_remote_port = req.remote_port;
    legA_local_ip = req.local_ip;
    legA_local_port = req.local_port;
    orig_call_id=req.callid;
}

void Cdr::update(SBCCallLeg *call,AmRtpStream *stream){
	//AmRtpStream::ErrorsStats err_stats;
	DBG("Cdr::%s(SBCCallLeg [%p], AmRtpStream [%p])",FUNC_NAME,
		call,stream);
	if(writed) return;
	lock();
	//stream->getErrorsStats(err_stats);
	if(call->isALeg()){
		stream->getPayloadsHistory(legA_payloads);
		stream->getErrorsStats(legA_stream_errors);
		legA_bytes_recvd = stream->getRcvdBytes();
		legA_bytes_sent = stream->getSentBytes();
	} else {
		stream->getPayloadsHistory(legB_payloads);
		stream->getErrorsStats(legB_stream_errors);
		legB_bytes_recvd = stream->getRcvdBytes();
		legB_bytes_sent = stream->getSentBytes();
	}
	unlock();
}

void Cdr::update(const AmSipReply &reply){
	DBG("Cdr::%s(AmSipReply)",FUNC_NAME);
    if(writed) return;
    lock();
	if(reply.code == 200 && trusted_hdrs_gw){ //try to fill trusted headers from 200 OK reply
		TrustedHeaders::instance()->parse_reply_hdrs(reply,trusted_hdrs);
	}
	if(reply.remote_port!=0){	//check for bogus reply (like timeout)
		legB_remote_ip = reply.remote_ip;
		legB_remote_port = reply.remote_port;
		legB_local_ip = reply.local_ip;
		legB_local_port = reply.local_port;
		if(reply.code>=100){
			if(reply.code<110){ //10x codes
				if(!timerisset(&sip_10x_time)){
					gettimeofday(&sip_10x_time,NULL);
				}
			} else if(reply.code>=180 && reply.code<190){ //18x codes
				if(!timerisset(&sip_18x_time)){
					gettimeofday(&sip_18x_time,NULL);
				}
				if(NULL!=reply.body.hasContentType(SIP_APPLICATION_SDP)){ //18x with SDP
					sip_early_media_present = true;
				}
			}
		}
	}
    unlock();
}

void Cdr::update(SBCCallLeg &leg){
	DBG("Cdr::%s(SBCCallLeg)",FUNC_NAME);
    if(writed) return;
    lock();
    if(leg.isALeg()){
        //A leg related variables
		local_tag = leg.getLocalTag();
        orig_call_id = leg.getCallID();
    } else {
        //B leg related variables
        SBCCallProfile &profile = leg.getCallProfile();
        term_call_id = profile.callid.empty()? leg.getCallID() : profile.callid;
    }
    unlock();
}

void Cdr::update(UpdateAction act){
	DBG("Cdr::%s(act = %s)",FUNC_NAME,updateAction2str(act));
    if(writed) return;
    switch(act){
    case Start:
		gettimeofday(&start_time, NULL);
        end_time = start_time;
        break;
	case BLegInvite:
		if(!timerisset(&bleg_invite_time))
			gettimeofday(&bleg_invite_time, NULL);
		break;
    case Connect:
        gettimeofday(&connect_time, NULL);
        break;
    case End:
		if(end_time.tv_sec==start_time.tv_sec){
			gettimeofday(&end_time, NULL);
		}
        break;
    case Write:
        writed = true;
        break;
    }
}

void Cdr::set_start_time(const timeval &t){
	end_time = start_time = t;
}

void Cdr::update_bleg_reason(string reason, int code){
	DBG("Cdr::%s(reason = '%s',code = %d)",FUNC_NAME,
		reason.c_str(),code);    if(writed) return;
	if(writed) return;
    lock();
    if(!bleg_reason_writed){
        disconnect_reason = reason;
        disconnect_code = code;
        bleg_reason_writed = true;
    }
    unlock();
}

void Cdr::update_aleg_reason(string reason, int code){
	DBG("Cdr::%s(reason = '%s',code = %d)",FUNC_NAME,
		reason.c_str(),code);
	if(writed) return;
	lock();
	if(!aleg_reason_writed){
		disconnect_rewrited_reason = reason;
		disconnect_rewrited_code = code;
		aleg_reason_writed = true;
	}
	unlock();
}

void Cdr::update_internal_reason(DisconnectInitiator initiator,string reason, int code){
	DBG("Cdr::%s(initiator = %d,reason = '%s',code = %d)",FUNC_NAME,
		initiator,reason.c_str(),code);

	if(writed) return;
	lock();
	update(End);
	if(!disconnect_initiator_writed){
		disconnect_initiator = initiator;
		disconnect_internal_reason = reason;
		disconnect_internal_code = code;
		disconnect_initiator_writed = true;
	}
	if(!aleg_reason_writed){
		disconnect_rewrited_reason = reason;
		disconnect_rewrited_code = code;
	}
	unlock();
}

void Cdr::setSuppress(bool s){
	if(writed) return;
	lock();
	suppress = s;
	unlock();
}

void Cdr::refuse(const SBCCallProfile &profile){
    if(writed) return;
    lock();
    unsigned int refuse_with_code;
    string refuse_with = profile.refuse_with;
    size_t spos = refuse_with.find(' ');
    disconnect_initiator = DisconnectByDB;
    if (spos == string::npos || spos == refuse_with.size() ||
        str2i(refuse_with.substr(0, spos), refuse_with_code))
    {
		ERROR("can't parse refuse_with in profile");
        disconnect_reason = refuse_with;
        disconnect_code = 0;
    } else {
        disconnect_reason = refuse_with.substr(spos+1);
        disconnect_code = refuse_with_code;
    }
	disconnect_rewrited_reason = disconnect_reason;
	disconnect_rewrited_code = disconnect_code;
    unlock();
}

void Cdr::refuse(int code, string reason){
	if(writed) return;
	lock();
	disconnect_code = code;
	disconnect_reason = reason;
	unlock();
}

void Cdr::replace(ParamReplacerCtx &ctx,const AmSipRequest &req){
	//msg_logger_path = ctx.replaceParameters(msg_logger_path,"msg_logger_path",req);
}

Cdr::Cdr(){
    init();
}

Cdr::Cdr(const Cdr& cdr,const SqlCallProfile &profile){
	//DBG("Cdr::%s(cdr = %p,profile = %p) = %p",FUNC_NAME,
	//	&cdr,&profile,this);

	init();
	update_sql(profile);

	attempt_num = cdr.attempt_num+1;
	end_time = start_time = cdr.start_time;

	legA_remote_ip = cdr.legA_remote_ip;
	legA_remote_port = cdr.legA_remote_port;
	legA_local_ip = cdr.legA_local_ip;
	legA_local_port = cdr.legA_local_port;

	orig_call_id = cdr.orig_call_id;
	local_tag = cdr.local_tag;
	global_tag = cdr.global_tag;

	msg_logger_path = cdr.msg_logger_path;
	dump_level_id = cdr.dump_level_id;
}

Cdr::Cdr(const SqlCallProfile &profile)
{
    init();
	update_sql(profile);
}

static string join_str_vector2(const vector<string> &v1,
                               const vector<string> &v2,
                               const string &delim){
    std::stringstream ss;
    for(vector<string>::const_iterator i = v1.begin();i!=v1.end();++i){
        if(i != v1.begin())
            ss << delim;
        ss << *i;
    }
    //if(!(v1.empty()||v2.empty()))
        ss << "/";
    for(vector<string>::const_iterator i = v2.begin();i!=v2.end();++i){
        if(i != v2.begin())
            ss << delim;
        ss << *i;
    }
    return string(ss.str());
}

char *Cdr::serialize_rtp_stats(){
#define field_name fields[i++]
    int i = 0;
    cJSON *j;
    char *s;
    static const char *fields[] = {
        "lega_rx_payloads",
        "lega_tx_payloads",
        "legb_rx_payloads",
        "legb_tx_payloads",
        "lega_rx_bytes",
        "lega_tx_bytes",
        "legb_rx_bytes",
        "legb_tx_bytes",
        "lega_rx_decode_errs",
        "lega_rx_no_buf_errs",
        "lega_rx_parse_errs",
        "legb_rx_decode_errs",
        "legb_rx_no_buf_errs",
        "legb_rx_parse_errs",
    };

    j = cJSON_CreateObject();

    //tx/rx uploads
    cJSON_AddStringToObject(j,field_name,
                                join_str_vector2(
                                    legA_payloads.incoming,
                                    legA_payloads.incoming_relayed,","
                                ).c_str()
                            );

    cJSON_AddStringToObject(j,field_name,
                                join_str_vector2(
                                    legA_payloads.outgoing,
                                    legA_payloads.outgoing_relayed,","
                                ).c_str()
                            );
    cJSON_AddStringToObject(j,field_name,
                                join_str_vector2(
                                    legB_payloads.incoming,
                                    legB_payloads.incoming_relayed,","
                                ).c_str()
                            );
    cJSON_AddStringToObject(j,field_name,
                                join_str_vector2(
                                    legB_payloads.outgoing,
                                    legB_payloads.outgoing_relayed,","
                                ).c_str()
                            );

    //tx/rx bytes
    cJSON_AddNumberToObject(j,field_name,legA_bytes_recvd);
    cJSON_AddNumberToObject(j,field_name,legA_bytes_sent);
    cJSON_AddNumberToObject(j,field_name,legB_bytes_recvd);
    cJSON_AddNumberToObject(j,field_name,legB_bytes_sent);

    //tx/rx rtp errors
    cJSON_AddNumberToObject(j,field_name,legA_stream_errors.decode_errors);
    cJSON_AddNumberToObject(j,field_name,legA_stream_errors.out_of_buffer_errors);
    cJSON_AddNumberToObject(j,field_name,legA_stream_errors.rtp_parse_errors);
    cJSON_AddNumberToObject(j,field_name,legB_stream_errors.decode_errors);
    cJSON_AddNumberToObject(j,field_name,legB_stream_errors.out_of_buffer_errors);
    cJSON_AddNumberToObject(j,field_name,legB_stream_errors.rtp_parse_errors);

    s = cJSON_Print(j);
    cJSON_Delete(j);
    return s;
#undef field_name
}


static inline void invoc_AmArg(pqxx::prepare::invocation &invoc,const AmArg &arg){
	short type = arg.getType();
	AmArg a;
	switch(type){
	case AmArg::Int:      { invoc(arg.asInt()); } break;
	case AmArg::LongLong: { invoc(arg.asLongLong()); } break;
	case AmArg::Bool:     { invoc(arg.asBool()); } break;
	case AmArg::CStr:     { invoc(arg.asCStr()); } break;
	case AmArg::Undef:    { invoc(); } break;
	default: {
		ERROR("invoc_AmArg. unhandled AmArg type %s",a.t2str(type));
		invoc();
	}
	}
}

void Cdr::invoc(pqxx::prepare::invocation &invoc,AmArg &invoced_values)
{
#define invoc_field(field_value)\
	invoced_values.push(AmArg(field_value));\
	invoc(field_value);

#define invoc_timestamp(field_value)\
	if(timerisset(&field_value)){\
		double v = timeval2double(field_value);\
		invoced_values.push(AmArg(v));\
		invoc(v);\
	} else {\
		invoced_values.push(AmArg());\
		invoc();\
	}

	invoc_field(attempt_num);
	invoc_field(is_last);
	invoc_field(time_limit);
	invoc_field(legA_local_ip);
	invoc_field(legA_local_port);
	invoc_field(legA_remote_ip);
	invoc_field(legA_remote_port);
	invoc_field(legB_local_ip);
	invoc_field(legB_local_port);
	invoc_field(legB_remote_ip);
	invoc_field(legB_remote_port);
	invoc_timestamp(start_time);
	invoc_timestamp(bleg_invite_time);
	invoc_timestamp(connect_time);
	invoc_timestamp(end_time);
	invoc_timestamp(sip_10x_time);
	invoc_timestamp(sip_18x_time);
	invoc_field(sip_early_media_present);
	invoc_field(disconnect_code);
	invoc_field(disconnect_reason);
	invoc_field(disconnect_initiator);
	invoc_field(disconnect_internal_code);
	invoc_field(disconnect_internal_reason);
	if(is_last){
		invoc_field(disconnect_rewrited_code);
		invoc_field(disconnect_rewrited_reason);
	} else {
		invoc_field(0);
		invoc_field("");
	}
	invoc_field(orig_call_id);
	invoc_field(term_call_id);
	invoc_field(local_tag);
	invoc_field(msg_logger_path);
	invoc_field(dump_level_id);

	char *rtp_stats = serialize_rtp_stats();
	invoc_field(rtp_stats);
	free(rtp_stats);

	invoc_field(global_tag);

	/* invocate dynamic fields  */
	const size_t n = dyn_fields.size();
	for(unsigned int k = 0;k<n;++k)
		invoc_AmArg(invoc,dyn_fields.get(k));
	/* invocate trusted hdrs  */
	for(vector<AmArg>::const_iterator i = trusted_hdrs.begin();
			i != trusted_hdrs.end(); ++i){
		invoc_AmArg(invoc,*i);
	}

#undef invoc_field
}

template<class T>
static void join_csv(ofstream &s, const T &a){
	if(!a.size())
		return;

	int n = a.size()-1;

	s << ",";
	for(int k = 0;k<n;k++)
		s << "'" << AmArg::print(a[k]) << "',";
	s << "'" << AmArg::print(a[n]) << "'";
}

void Cdr::to_csv_stream(ofstream &s)
{
#define quote(v) "'"<<v<< "'" << ','

	s <<
	quote(attempt_num) <<
	quote(is_last) <<
	quote(time_limit) <<
	quote(legA_local_ip) << quote(legA_local_port) <<
	quote(legA_remote_ip) << quote(legA_remote_port) <<
	quote(legB_local_ip) << quote(legB_local_port) <<
	quote(legB_remote_ip) << quote(legB_remote_port) <<
	quote(timeval2double(start_time)) <<
	quote(timeval2double(bleg_invite_time)) <<
	quote(timeval2double(connect_time)) <<
	quote(timeval2double(end_time)) <<
	quote(timeval2double(sip_10x_time)) <<
	quote(timeval2double(sip_18x_time)) <<
	quote(sip_early_media_present) <<

	quote(disconnect_code) << quote(disconnect_reason) <<
	quote(disconnect_initiator) <<
	quote(disconnect_internal_code) << quote(disconnect_internal_reason);

	if(is_last){
		s <<
		quote(disconnect_rewrited_code) <<
		quote(disconnect_rewrited_reason);
	} else {
		s <<
		quote(0) <<
		quote("");
	}

	s << quote(orig_call_id) << quote(term_call_id) <<
	quote(local_tag) << quote(msg_logger_path) <<
	quote(dump_level_id);

	char *rtp_stats = serialize_rtp_stats();
	s << quote(rtp_stats);
	free(rtp_stats);

	s << quote(global_tag);

		//dynamic fields
	join_csv(s,dyn_fields);

		//trusted fields
	join_csv(s,trusted_hdrs);
#undef quote
}
