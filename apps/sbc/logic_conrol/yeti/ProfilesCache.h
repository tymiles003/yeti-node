#ifndef _ProfilesCache_
#define _ProfilesCache_

#include "AmSipMsg.h"
#include "AmAppTimer.h"
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

class ProfilesCache:
public MurmurHash<ProfilesCacheKey,AmSipRequest,SqlCallProfile>,
public DirectAppTimer
{
public:
  ProfilesCache(vector<string> used_header_fields, unsigned long buckets = 65000,double timeout = 5);
  ~ProfilesCache();

  SqlCallProfile *get_profile(const AmSipRequest *req);
  void insert_profile(const AmSipRequest *req,SqlCallProfile *profile);
  
  void fire(){
    on_clean();
    AmAppTimer::instance()->setTimer_unsafe(this,timeout);
  }
  void startTimer();
  void stopTimer();
  
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
