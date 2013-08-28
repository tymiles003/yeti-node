#ifndef CODESTRANSLATOR_H
#define CODESTRANSLATOR_H

#include "AmConfigReader.h"
#include "AmThread.h"
#include <map>
#include "DbConfig.h"

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
	int load_translations_config();
  public:
	CodesTranslator();
	~CodesTranslator();
	static CodesTranslator* instance();

	int configure(AmConfigReader &cfg);
	bool reload();

	void rewrite_response(unsigned int code,const string &reason,
						  unsigned int &out_code,string &out_reason);
	bool stop_hunting(unsigned int code);
	void translate_db_code(unsigned int icode,
								 unsigned int &internal_code,
								 string &internal_reason,
								 unsigned int &response_code,
								 string &response_reason);
};

#endif // CODESTRANSLATOR_H
