#ifndef _ProfilesCache_
#define _ProfilesCache_

#include "AmSipMsg.h"
#include "AmAppTimer.h"
#include "AmArg.h"
#include "SqlCallProfile.h"
#include "MurmurHash.h"

using namespace std;

struct ProfilesCacheKey {
	string local_ip,
		from_uri,
		to,
		contact,
		user;
	unsigned short local_port,
		remote_port;
	map<string,string> header_fields;
};

struct ProfilesCacheEntry {
	struct timeval expire_time;
	list<SqlCallProfile *> profiles;

	ProfilesCacheEntry(){}
	~ProfilesCacheEntry(){
		list<SqlCallProfile *>::iterator it = profiles.begin();
		for(;it != profiles.end();++it){
			delete (*it);
		}
	}
	ProfilesCacheEntry *copy(){	//clone with all internal structures
		ProfilesCacheEntry *e = new ProfilesCacheEntry();

		e->expire_time = expire_time;

		list<SqlCallProfile *>::iterator it = profiles.begin();
		for(;it != profiles.end();++it){
			SqlCallProfile *profile = new SqlCallProfile();
			*profile =  *(*it);
			e->profiles.push_back(profile);
		}
		return e;
	}
};

class ProfilesCache:
public MurmurHash<ProfilesCacheKey,AmSipRequest,ProfilesCacheEntry>,
public DirectAppTimer
{
public:
	ProfilesCache(vector<string> used_header_fields, unsigned long buckets = 65000,double timeout = 5);
	~ProfilesCache();

	bool get_profiles(const AmSipRequest *req,list<SqlCallProfile *> &profiles);
	void insert_profile(const AmSipRequest *req,ProfilesCacheEntry *entry);

	void fire(){
		on_clean();
		AmAppTimer::instance()->setTimer_unsafe(this,timeout);
	}
	void startTimer();
	void stopTimer();

	void getStats(AmArg &arg);
	void clear();

protected:
	uint64_t hash_lookup_key(const AmSipRequest *key);
	bool cmp_lookup_key(const AmSipRequest *k1,const ProfilesCacheKey *k2);
	void init_key(ProfilesCacheKey **dest,const AmSipRequest *src);
	void free_key(ProfilesCacheKey *key);

private:
	double timeout;
	vector<string> used_header_fields;
	bool is_obsolete(entry *e,struct timeval *now);
	void check_obsolete();
	void on_clean();

};

#endif
