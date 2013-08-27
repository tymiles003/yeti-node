#ifndef CODESTRANSLATOR_H
#define CODESTRANSLATOR_H

#include "AmConfigReader.h"
#include <map>

using namespace std;

class CodesTranslator {
	static CodesTranslator* _instance;

	/*! actions preferences */
	struct pref {
		bool is_stop_hunting;

		pref(bool stop_hunting):is_stop_hunting(stop_hunting){}
	};
	map<int,pref> code2pref;

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

  public:
	CodesTranslator();
	~CodesTranslator();
	static CodesTranslator* instance();

	int configure(AmConfigReader &cfg);

	void rewrite_response(unsigned int code,const string &reason,
						  unsigned int &out_code,string &out_reason);
	bool stop_hunting(unsigned int code);
};

#endif // CODESTRANSLATOR_H
