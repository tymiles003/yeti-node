#ifndef CODESTRANSLATOR_H
#define CODESTRANSLATOR_H

#include "AmConfigReader.h"
#include "AmThread.h"
#include "AmArg.h"
#include <map>
#include "DbConfig.h"

//fail codes for TS
#define	FC_PARSE_FROM_FAILED		114
#define	FC_PARSE_TO_FAILED			115
#define	FC_PARSE_CONTACT_FAILED		116
#define FC_NOT_PREPARED				117
#define FC_DB_EMPTY_RESPONSE		118
#define FC_READ_FROM_TUPLE_FAILED	119
#define FC_EVALUATION_FAILED		120
#define FC_GET_ACTIVE_CONNECTION	121
#define FC_DB_BROKEN_EXCEPTION		122
#define FC_DB_CONVERSION_EXCEPTION	123
#define FC_DB_BASE_EXCEPTION		124
#define DC_RTP_TIMEOUT				125

using namespace std;

class CodesTranslator {
	static CodesTranslator* _instance;

	/*! actions preferences */
	struct pref {
		bool is_stop_hunting;

		pref(bool stop_hunting):is_stop_hunting(stop_hunting){}
	};
	map<int,pref> code2pref;
	AmMutex code2pref_mutex;

	/*! response translation preferences */
	struct trans {
		bool pass_reason_to_originator;
		int rewrite_code;
		string rewrite_reason;
		trans(bool p,int c,string r):
			pass_reason_to_originator(p),
			rewrite_code(c),
			rewrite_reason(r){}
	};
	map<int,trans> code2trans;
	AmMutex code2trans_mutex;

	/*! internal codes translator */
	struct icode {
		int internal_code,response_code;
		string internal_reason,response_reason;
		icode(int ic,string ir,int rc, string rr):
			internal_code(ic),
			internal_reason(ir),
			response_code(rc),
			response_reason(rr){}
	};
	map<unsigned int,icode> icode2resp;
	AmMutex icode2resp_mutex;
	DbConfig dbc;
	string db_schema;
	int load_translations_config();

	struct {
		unsigned int unknown_response_codes;
		unsigned int missed_response_configs;
		unsigned int unknown_internal_codes;
		void clear(){
			unknown_response_codes = 0;
			missed_response_configs = 0;
			unknown_internal_codes = 0;
		}
		void get(AmArg &arg){
			arg["unknown_code_resolves"] = (long)unknown_response_codes;
			arg["missed_response_configs"] = (long)missed_response_configs;
			arg["unknown_internal_codes"] = (long)unknown_internal_codes;
		}
	} stat;
  public:
	CodesTranslator();
	~CodesTranslator();
	static CodesTranslator* instance();

	int configure(AmConfigReader &cfg);
	void configure_db(AmConfigReader &cfg);
	bool reload();

	void rewrite_response(unsigned int code,const string &reason,
						  unsigned int &out_code,string &out_reason);
	bool stop_hunting(unsigned int code);
	void translate_db_code(unsigned int icode,
								 unsigned int &internal_code,
								 string &internal_reason,
								 unsigned int &response_code,
								 string &response_reason);

	void GetConfig(AmArg& ret);
	void clearStats();
	void getStats(AmArg &ret);
};

#endif // CODESTRANSLATOR_H
