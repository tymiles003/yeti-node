#ifndef SQLCALLPROFILE_H
#define SQLCALLPROFILE_H

#include "SBCCallProfile.h"

#include <string>

using std::string;

struct SqlCallProfile
        : public SBCCallProfile
{
    //int time_limit; --> not used. find solution

    //int local_port; --> TODO:cdr

    bool SQLexception;

    bool cached;
    //atomic_int ref_cnt;
    struct timeval expire_time;

    list<string> dyn_fields;

    //add accounting fields to profile. much faster then old arch
/*
   string disconnect_reason;
    int disconnect_code;
    int disconnect_initiator;
*/
/*
    struct timeval start_time;
    struct timeval connect_time;
    struct timeval end_time;

    struct timeval cdr_die_time;
*/

/*
    string term_ip,term_local_ip;
    int term_port,term_local_port;

    string orig_call_id;
    string term_call_id;
    string local_tag;
*/

    SqlCallProfile();
    ~SqlCallProfile();
};

#endif // SQLCALLPROFILE_H
