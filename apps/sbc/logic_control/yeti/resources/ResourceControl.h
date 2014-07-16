#ifndef RESOURCECONTROL_H
#define RESOURCECONTROL_H

#include "AmConfigReader.h"
#include "ResourceCache.h"
#include "AmArg.h"
#include <map>
#include "log.h"
#include "../db/DbConfig.h"

using namespace std;

#define ResourceAction_Reject 1
#define ResourceAction_NextRoute 2
#define ResourceAction_Accept 3

struct ResourceConfig {
	int id;
	string name;
	int reject_code;
	string reject_reason;
	enum ActionType {
		Reject = 0,
		NextRoute,
		Accept
	} action;
	string str_action;

	ResourceConfig(int i,string n, int c, string r,int a):
		id(i),
		name(n),
		reject_code(c),
		reject_reason(r)
	{
		set_action(a);
	}
	void set_action(int a);
	string print() const;
};

enum ResourceCtlResponse {
	RES_CTL_OK,
	RES_CTL_NEXT,
	RES_CTL_REJECT,
	RES_CTL_ERROR
};

enum ResourceState {
	RES_STATE_OK,
	RES_STATE_ERR,
	RES_STATE_UNKNOWN
};

class ResourceControl
{
	ResourceCache cache;
	map<int,ResourceConfig> type2cfg;
	AmMutex cfg_lock;
	DbConfig dbc;
	string db_schema;

	void replace(string &s,Resource &r,ResourceConfig &rc);
	void replace(string& s, const string& from, const string& to);
	int load_resources_config();
	int reject_on_error;

	struct {
		unsigned int hits;
		unsigned int overloaded;
		unsigned int rejected;
		unsigned int nextroute;
		unsigned int errors;
		void clear(){
			hits = 0;
			overloaded = 0;
			rejected = 0;
			nextroute = 0;
			errors = 0;
		}
		void get(AmArg &arg){
			arg["hits"] = (long)hits;
			arg["overloaded"] = (long)overloaded;
			arg["rejected"] = (long)rejected;
			arg["nextroute"] = (long)nextroute;
			arg["errors"] = (long)errors;
		}
	} stat;
public:
	ResourceControl();
	int configure(AmConfigReader &cfg);
	void configure_db(AmConfigReader &cfg);
	void start();
	void stop();
	bool reload();

	ResourceCtlResponse get(ResourceList &rl,
							  int &reject_code,
							  string &reject_reason);

	void put(ResourceList &rl);

	void GetConfig(AmArg& ret);
	void clearStats();
	void getStats(AmArg &ret);
	void getResourceState(int type, int id, AmArg &ret);
};

#endif // RESOURCECONTROL_H
