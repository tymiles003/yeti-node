#ifndef RESOURCECONTROL_H
#define RESOURCECONTROL_H

#include "AmConfigReader.h"
#include "ResourceCache.h"
#include <map>
#include "log.h"

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

class ResourceControl
{
	ResourceCache cache;
	map<int,ResourceConfig> type2cfg;

	void replace(string &s,Resource &r,ResourceConfig &rc);
	void replace(string& s, const string& from, const string& to);

	int reject_on_error;
public:
	ResourceControl();
	int configure(AmConfigReader &cfg);
	void start();
	void stop();

	ResourceCtlResponse get(ResourceList &rl,
							  int &reject_code,
							  string &reject_reason);

	void put(ResourceList &rl);
};

#endif // RESOURCECONTROL_H
