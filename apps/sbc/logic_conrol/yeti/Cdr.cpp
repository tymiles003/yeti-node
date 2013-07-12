#include "Cdr.h"

Cdr::Cdr(const Cdr &cdr){

}

Cdr::Cdr(const SqlCallProfile &profile) {

#define copy_field(name) name = profile.name;

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

    //copy needed from profile
    copy_field(disconnect_reason)
    copy_field(disconnect_code)
    copy_field(disconnect_initiator)
    copy_field(start_time)
    copy_field(connect_time)
    copy_field(end_time)
    copy_field(cdr_die_time)
    copy_field(term_ip)
    copy_field(term_local_ip)
    copy_field(term_port)
    copy_field(term_local_port)
    copy_field(orig_call_id)
    copy_field(term_call_id)
    copy_field(local_tag)

    copy_field(time_limit)
    copy_field(local_port)
    copy_field(SQLexception)
    copy_field(dyn_fields);

    copy_field(outbound_proxy)
#undef copy_field

}

void update_cdr(Cdr &cdr,UpdateAction act){
    switch(act){
    case Start: {
        gettimeofday(&cdr.start_time, NULL);
        cdr.end_time = cdr.start_time;
    } break;
    case Connect:
        gettimeofday(&cdr.connect_time, NULL);
    case End:
        gettimeofday(&cdr.end_time, NULL);
        break;
    case DisconnectByDB:
        cdr.disconnect_initiator=0;
        break;
    case DisconnectByTS:
        cdr.disconnect_initiator=1;
        break;
    case DisconnectByDST:
        cdr.disconnect_initiator=2;
        break;
    case DisconnectByORG:
        cdr.disconnect_initiator=3;
        break;
    case Write:
        cdr.writed = true;
        break;
    }
}
