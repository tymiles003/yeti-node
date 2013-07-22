#ifndef CDR_H
#define CDR_H

#include "SqlCallProfile.h"
#include "SBCCallLeg.h"
#include "time.h"

enum UpdateAction {
    Start,
    Connect,
    End,
    Write
};

enum DisconnectInitiator {
    DisconnectByDB = 0,
    DisconnectByTS = 1,
    DisconnectByDST = 2,
    DisconnectByORG = 3
};

struct Cdr:
    public AmMutex,
           atomic_int
{
//    AmMutex lock;
    bool writed;
           //!Cdrfields
    string disconnect_reason;
    int disconnect_code;
    int disconnect_initiator;
    struct timeval cdr_born_time;
    struct timeval start_time;
    struct timeval connect_time;
    struct timeval end_time;
    string term_ip,term_local_ip;
    int term_port,term_local_port;
    string orig_call_id;
    string term_call_id;
    string local_tag;
        //!CallStartData
    int time_limit;
    int local_port;
    bool SQLexception;
    list<string> dyn_fields;
        //!CallProfileData
    string outbound_proxy;

    Cdr();
    Cdr(const SqlCallProfile &profile,const AmSipRequest &req);

    void init();
    void update(const SqlCallProfile &profile);
    void update(const AmSipRequest &req);
    void update(const AmSipReply &reply);
    void update(SBCCallLeg &leg);
    void update(UpdateAction act);
    void update(DisconnectInitiator initiator,string reason, int code);
    void refuse(const SqlCallProfile &profile);
};

#endif // CDR_H
