#include "Cdr.h"
#include "AmUtils.h"
#include "log.h"
#include "RTPParameters.h"

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

void Cdr::init(){
    //initital values
    bzero(&start_time,sizeof(struct timeval));
    bzero(&connect_time,sizeof(struct timeval));
    bzero(&end_time,sizeof(struct timeval));

    gettimeofday(&cdr_born_time, NULL);

    writed=false;
	inserted2list = false;

	disconnect_reason = "Unhandled sequence. report";
	disconnect_code = 0;
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
	log_rtp = false;
	log_sip = false;

    time_limit = 0;

	attempt_num = 1;
}

void Cdr::update_sql(const SqlCallProfile &profile){
	DBG("Cdr::%s(SqlCallProfile)",FUNC_NAME);
    outbound_proxy = profile.outbound_proxy;
    dyn_fields = profile.dyn_fields;
    time_limit = profile.time_limit;
}

void Cdr::update_sbc(const SBCCallProfile &profile){
	DBG("Cdr::%s(SBCCallProfile)",FUNC_NAME);
	msg_logger_path = profile.get_logger_path();
	log_rtp = profile.log_rtp;
	log_sip = profile.log_sip;
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
	DBG("Cdr::%s(AmSipRequest)",FUNC_NAME);
	if(writed) return;
	lock();
	if(call->isALeg()){
		stream->getPayloadsHistory(legA_incoming_payloads,legA_outgoing_payloads);
	} else {
		stream->getPayloadsHistory(legB_incoming_payloads,legB_outgoing_payloads);
	}
	unlock();
}

void Cdr::update(const AmSipReply &reply){
	DBG("Cdr::%s(AmSipReply)",FUNC_NAME);
    if(writed) return;
    lock();
	if(reply.remote_port!=0){	//check for bogus reply (like timeout)
		legB_remote_ip = reply.remote_ip;
		legB_remote_port = reply.remote_port;
		legB_local_ip = reply.local_ip;
		legB_local_port = reply.local_port;
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
        term_call_id = leg.getCallID();
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
    case Connect:
        gettimeofday(&connect_time, NULL);
        break;
    case End:
        gettimeofday(&end_time, NULL);
        break;
    case Write:
        writed = true;
        break;
    }
}

void Cdr::update(DisconnectInitiator initiator,string reason, int code){
	DBG("Cdr::%s(initiator = %d,reason = '%s',code = %d)",FUNC_NAME,
		initiator,reason.c_str(),code);
    if(writed) return;
    lock();
    gettimeofday(&end_time, NULL);
    disconnect_initiator = initiator;
    disconnect_reason = reason;
    disconnect_code = code;
	disconnect_rewrited_reason = disconnect_reason;
	disconnect_rewrited_code = disconnect_code;
    unlock();
}

void Cdr::update_rewrited(string reason, int code){
	DBG("Cdr::%s(reason = '%s',code = %d)",FUNC_NAME,
		reason.c_str(),code);
	if(writed) return;
	lock();
	disconnect_rewrited_reason = reason;
	disconnect_rewrited_code = code;
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
	start_time = cdr.start_time;

	legA_remote_ip = cdr.legA_remote_ip;
	legA_remote_port = cdr.legA_remote_port;
	legA_local_ip = cdr.legA_local_ip;
	legA_local_port = cdr.legA_local_port;

	orig_call_id = cdr.orig_call_id;
	local_tag = cdr.local_tag;

	msg_logger_path = cdr.msg_logger_path;
	log_rtp = cdr.log_rtp;
	log_sip = cdr.log_sip;
}

Cdr::Cdr(const SqlCallProfile &profile)
{
    init();
	update_sql(profile);
}

