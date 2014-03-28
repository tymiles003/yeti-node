#ifndef CDR_H
#define CDR_H
#include "time.h"

#include "SqlCallProfile.h"
#include "SBCCallLeg.h"
#include "Resource.h"

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

struct Cdr: public
	AmMutex
{
    bool writed;
    bool suppress;
	bool inserted2list;
	int attempt_num;
	bool is_last;

	string msg_logger_path;
	int dump_level_id;

    int disconnect_initiator;

    string disconnect_reason;
    int disconnect_code;
    bool bleg_reason_writed;

	string disconnect_internal_reason;
	int disconnect_internal_code;

	string disconnect_rewrited_reason;
	int disconnect_rewrited_code;

    struct timeval cdr_born_time;
    struct timeval start_time;
    struct timeval connect_time;
    struct timeval end_time;

    string legB_remote_ip, legB_local_ip;
    unsigned short legB_remote_port, legB_local_port;
    string legA_remote_ip, legA_local_ip;
    unsigned short legA_remote_port, legA_local_port;

    string orig_call_id;
    string term_call_id;
    string local_tag;
    int time_limit;

    list<string> dyn_fields;
    string outbound_proxy;

	vector<string> legA_incoming_payloads,legB_incoming_payloads;
	vector<string> legA_outgoing_payloads,legB_outgoing_payloads;
	unsigned long legA_bytes_recvd, legB_bytes_recvd;
	unsigned long legA_bytes_sent, legB_bytes_sent;

	Cdr();
	Cdr(const Cdr& cdr,const SqlCallProfile &profile);
    Cdr(const SqlCallProfile &profile);

    void init();
	void update_sql(const SqlCallProfile &profile);
	void update_sbc(const SBCCallProfile &profile);
	void update(const AmSipRequest &req);
    void update(const AmSipReply &reply);
    void update(SBCCallLeg &leg);
	void update(SBCCallLeg *call,AmRtpStream *stream);
    void update(UpdateAction act);
    void update_bleg_reason(string reason, int code);
    void update_aleg_reason(string reason, int code);
    void update_internal_reason(DisconnectInitiator initiator,string reason, int code);
    void setSuppress(bool s);
	void replace(ParamReplacerCtx &ctx,const AmSipRequest &req);
	void replace(string& s, const string& from, const string& to);
    void refuse(const SBCCallProfile &profile);
	void refuse(int code, string reason);
};

#endif // CDR_H
