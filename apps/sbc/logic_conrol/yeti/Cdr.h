#ifndef CDR_H
#define CDR_H

#include "SqlCallProfile.h"
#include "time.h"

enum UpdateAction {
    Start,
    Connect,
    End,
    DisconnectByDB,
    DisconnectByTS,
    DisconnectByDST,
    DisconnectByORG,
    Write
};

struct Cdr {
    AmMutex lock;
    bool writed;
           //old Cdrfields
    string disconnect_reason;
    int disconnect_code;
    int disconnect_initiator;
    struct timeval cdr_born_time;
    struct timeval start_time;
    struct timeval connect_time;
    struct timeval end_time;
    struct timeval cdr_die_time;
    string term_ip,term_local_ip;
    int term_port,term_local_port;
    string orig_call_id;
    string term_call_id;
    string local_tag;
        //old CallStartData
    int time_limit;
    int local_port;
    bool SQLexception;
    list<string> dyn_fields;
        //old CallProfileData
    string outbound_proxy;
    Cdr(const Cdr &cdr);
    Cdr(const SqlCallProfile &profile);
};

void update_cdr(Cdr &cdr,UpdateAction act);

#endif // CDR_H
