#ifndef RESOURCECONTROL_H
#define RESOURCECONTROL_H

#include "AmConfigReader.h"
#include "ResourceCache.h"
#include <map>

using namespace std;

struct ResourceConfig {
	int id;
	string name;
	int reject_code;
	string reject_reason;
	bool reject_on_overload;
	bool next_route_on_overload;

	ResourceConfig(int i,string n, int c, string r,bool reject,bool next_route):
		id(i),
		name(n),
		reject_code(c),
		reject_reason(r),
		reject_on_overload(reject),
		next_route_on_overload(next_route){}
	string print() const;
};

enum ResourceCtlResponse {
	RES_CTL_OK,
//	RES_CTL_NEXT,
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
