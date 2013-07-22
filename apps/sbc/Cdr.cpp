#include "Cdr.h"
#include "AmUtils.h"

void Cdr::init(){
    //initital values
    bzero(&start_time,sizeof(struct timeval));
    bzero(&connect_time,sizeof(struct timeval));
    bzero(&end_time,sizeof(struct timeval));
    bzero(&cdr_die_time,sizeof(struct timeval));

    gettimeofday(&cdr_born_time, NULL);

    writed=false;
    term_ip="";
    term_port=0;
    term_local_ip="";
    term_local_port=0;

    time_limit = 0; //!TODO: find where we can get it real value
}

void Cdr::update(const SqlCallProfile &profile){
    if(writed) return;
    SQLexception = profile.SQLexception;
    outbound_proxy = profile.outbound_proxy;
    dyn_fields = profile.dyn_fields;
}

void Cdr::update(const AmSipRequest &req){
    if(writed) return;
    local_port = req.local_port;
    orig_call_id=req.callid;
}

void Cdr::update(SBCCallLeg &leg){
    if(writed) return;
    if(leg.isALeg()){
        //A leg related variables
        local_tag = leg.getLocalTag();
    } else {
        //B leg related variables
        //term_call_id = "foobar";
        term_call_id = leg.getLocalTag();
    }
}

void Cdr::update(UpdateAction act){
    if(writed) return;
    switch(act){
    case Start: {
        gettimeofday(&start_time, NULL);
        end_time = start_time;
    } break;
    case Connect:
        gettimeofday(&connect_time, NULL);
    case End:
        gettimeofday(&end_time, NULL);
        break;
    case DisconnectByDB:
        disconnect_initiator=0;
        break;
    case DisconnectByTS:
        disconnect_initiator=1;
        break;
    case DisconnectByDST:
        disconnect_initiator=2;
        break;
    case DisconnectByORG:
        disconnect_initiator=3;
        break;
    case Write:
        writed = true;
        break;
    }
}

void Cdr::refuse(const SqlCallProfile &profile){
    if(writed) return;
    unsigned int refuse_with_code;
    string refuse_with = profile.refuse_with;
    size_t spos = refuse_with.find(' ');

    if (spos == string::npos || spos == refuse_with.size() ||
        str2i(refuse_with.substr(0, spos), refuse_with_code))
    {
        DBG("can't parse refuse_with in profile");
        disconnect_reason = refuse_with;
        disconnect_code = 0;
        return;
    }
    disconnect_reason = refuse_with.substr(spos+1);
    disconnect_code = refuse_with_code;
}

Cdr::Cdr(){
    init();
}

Cdr::Cdr(const SqlCallProfile &profile,const AmSipRequest &req)
{
    init();
    update(profile);
    update(req);
}
