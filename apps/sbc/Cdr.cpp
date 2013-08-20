#include "Cdr.h"
#include "AmUtils.h"

void Cdr::init(){
    //initital values
    bzero(&start_time,sizeof(struct timeval));
    bzero(&connect_time,sizeof(struct timeval));
    bzero(&end_time,sizeof(struct timeval));

    gettimeofday(&cdr_born_time, NULL);

    writed=false;

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
}

void Cdr::update_sql(const SqlCallProfile &profile){
    SQLexception = profile.SQLexception;
    outbound_proxy = profile.outbound_proxy;
    dyn_fields = profile.dyn_fields;
    time_limit = profile.time_limit;
	try {
		rl = resource_parse(profile.resources);
		ResourceList::const_iterator i = rl.begin();
		for(;i!=rl.end();++i){
			DBG("%p: resource: <%s>",this,resource_print(*i).c_str());
		}
	} catch(ResourceParseException &e){
		DBG("resources parse error:  %s <ctx = '%s'>",e.what.c_str(),e.ctx.c_str());
	}
}

void Cdr::update_sbc(const SBCCallProfile &profile){
	msg_logger_path = profile.msg_logger_path;
	log_rtp = profile.log_rtp;
	log_sip = profile.log_sip;
}

void Cdr::update(const AmSipRequest &req){
	if(writed) return;
	legA_remote_ip = req.remote_ip;
    legA_remote_port = req.remote_port;
    legA_local_ip = req.local_ip;
    legA_local_port = req.local_port;
    orig_call_id=req.callid;
}

void Cdr::update(const AmSipReply &reply){
    if(writed) return;
    lock();
    legB_remote_ip = reply.remote_ip;
    legB_remote_port = reply.remote_port;
    legB_local_ip = reply.local_ip;
    legB_local_port = reply.local_port;
    unlock();
}

void Cdr::update(SBCCallLeg &leg){
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
    if(writed) return;
    lock();
    gettimeofday(&end_time, NULL);
    disconnect_initiator = initiator;
    disconnect_reason = reason;
    disconnect_code = code;
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
        DBG("can't parse refuse_with in profile");
        disconnect_reason = refuse_with;
        disconnect_code = 0;
    } else {
        disconnect_reason = refuse_with.substr(spos+1);
        disconnect_code = refuse_with_code;
    }
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
	msg_logger_path = ctx.replaceParameters(msg_logger_path,"msg_logger_path",req);
}

Cdr::Cdr(){
    init();
}

Cdr::Cdr(const SqlCallProfile &profile)
{
    init();
	update_sql(profile);
}
