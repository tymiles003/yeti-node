#ifndef CODECSGROUP_H
#define CODECSGROUP_H

#include "AmArg.h"
#include <AmSdp.h>
#include <HeaderFilter.h>
#include "DbConfig.h"
#include "CodesTranslator.h"

#include <string>
#include <vector>
#include <map>
using namespace std;


struct CodecsGroupException : public InternalException {
	CodecsGroupException(unsigned int code,unsigned int codecs_group);
};

class CodecsGroupEntry {
	vector<SdpPayload> codecs_payloads;
  public:
	CodecsGroupEntry();
	~CodecsGroupEntry(){}
	bool add_codec(string codec);
	vector<SdpPayload> &get_payloads() { return codecs_payloads; }
	void getConfig(AmArg &ret) const;
};

class CodecsGroups {
	static CodecsGroups* _instance;

	DbConfig dbc;
	string db_schema;
	map<unsigned int,CodecsGroupEntry> m;

  public:
	CodecsGroups(){}
	~CodecsGroups(){}
	static CodecsGroups* instance(){
		if(!_instance)
			_instance = new CodecsGroups();
		return _instance;
	}

	int configure(AmConfigReader &cfg);
	void configure_db(AmConfigReader &cfg);
	int load_codecs_groups();
	bool reload();

	void get(int group_id,CodecsGroupEntry &e) {
		map<unsigned int,CodecsGroupEntry>::iterator i = m.find(group_id);
		if(i==m.end())
			throw CodecsGroupException(FC_CG_GROUP_NOT_FOUND,group_id);
		e = i->second;
	}

	bool insert(unsigned int group_id, string codec) {
		return m[group_id].add_codec(codec);
		//m.insert(pair<unsigned int,CodecsGroupEntry>(group_id,CodecsGroupEntry(group_codecs)));
	}

	void clear(){ m.clear(); }
	unsigned int size() { return m.size(); }

	void GetConfig(AmArg& ret);
};

#endif // CODECSGROUP_H
