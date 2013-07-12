#include "ProfilesCache.h"

ProfilesCache::ProfilesCache(vector<string>	 used_header_fields, unsigned long buckets,double timeout):
  MurmurHash<ProfilesCacheKey,AmSipRequest,SqlCallProfile>(buckets),
  timeout(timeout),
  used_header_fields(used_header_fields) {
  DBG("ProfilesCache()");
  startTimer();
}

ProfilesCache::~ProfilesCache(){
  DBG("~ProfilesCache()");
  stopTimer();
}

uint64_t ProfilesCache::hash_lookup_key(const AmSipRequest *key){
	uint64_t ret;
	string hdr;
  ret = 
    hashfn(key->local_ip.c_str(),key->local_ip.size()) ^
    hashfn(&key->local_port,sizeof(unsigned short)) ^
    hashfn(&key->remote_port,sizeof(unsigned short)) ^
    hashfn(key->from_uri.c_str(),key->from_uri.size()) ^
    hashfn(key->to.c_str(),key->to.size()) ^
    hashfn(key->contact.c_str(),key->contact.size()) ^
    hashfn(key->user.c_str(),key->user.size());

	for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
		hdr = getHeader(key->hdrs,*it);
		if(hdr.length())
			ret ^= hashfn(hdr.c_str(),hdr.size());
	}
	
	return ret; 
}

bool ProfilesCache::cmp_lookup_key(const AmSipRequest *k1,const ProfilesCacheKey *k2){
	bool ret;
	string hdr;
	map<string,string>::const_iterator mit;

  ret = 
    (k1->local_ip == k2->local_ip) &&
    (k1->local_port == k2->local_port) &&
    (k1->remote_port == k2->remote_port) &&
    (k1->from_uri == k2->from_uri) &&
    (k1->contact == k2->contact) &&
    (k1->user == k2->user) &&
    (k1->to == k2->to);
    
  if(!ret)
		return false;
		
	for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
		hdr = getHeader(k1->hdrs,*it);
		if(hdr.length()){
			mit = k2->header_fields.find(*it);
			if(mit != k2->header_fields.end()){
				if(mit->second != hdr)
					return false;
			}else{
				return false;
			}
		}
	}

	return true;
}

void ProfilesCache::init_key(ProfilesCacheKey **dest,const AmSipRequest *src){
	string hdr;
  ProfilesCacheKey *key = new ProfilesCacheKey;
  *dest = key;
  key->local_ip.assign(src->local_ip);
  key->local_port = src->local_port;
  key->remote_port = src->remote_port;
  key->from_uri.assign(src->from_uri);
  key->contact.assign(src->contact);
  key->user.assign(src->user);
  key->to.assign(src->to);
  
	for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
		hdr = getHeader(src->hdrs,*it);
		if(hdr.length()){
			key->header_fields.insert(pair<string,string>(*it,hdr));
		}
	}
}

void ProfilesCache::free_key(ProfilesCacheKey *key){
  delete key;
}

SqlCallProfile *ProfilesCache::get_profile(const AmSipRequest *req){
  SqlCallProfile *profile = NULL;
  struct timeval now;
  entry *e;
  lock();
    e = at(req,false);
    if(e){
	DBG("ProflesCache: Found profile in cache");
	gettimeofday(&now,NULL);
	if(is_obsolete(e,&now)){
	  DBG("ProflesCache: Profile is obsolete. Remove it from cache");
	  delete e->data;
	  erase(e,false);
	} else {
      profile = new SqlCallProfile(*e->data);
	}
    } else {
      DBG("ProflesCache: No appropriate profile in cache");
    }
  unlock();
  return profile;
}

void ProfilesCache::insert_profile(const AmSipRequest *req,SqlCallProfile *profile){
  SqlCallProfile *p = new SqlCallProfile(*profile);
  lock();
  DBG("ProflesCache: add profile to cache");
  if(insert(req,p,false,true)){ //(false, true) eq (external locked,check unique)
    profile->cached = true;
    DBG("ProflesCache: profile added");
  } else {
    delete p;
    DBG("ProfilesCache: profile already in cache.");
  }
  unlock();
}

void ProfilesCache::check_obsolete(){
  entry *e,*next;
  struct timeval now;
  list<SqlCallProfile *> free_profiles;
  SqlCallProfile *profile;
  free_profiles.clear();
  lock();
    gettimeofday(&now,NULL);
    e = first;
    while(e){
      next = e->next;
      if(is_obsolete(e,&now)){
        //if(e->data->ref_cnt.get()==0){
        //    DBG("%s: cleanup %p",FUNC_NAME,e->data);
            free_profiles.push_back(e->data);
            erase(e,false);
        //} else {
        //    DBG("%s: has still used obsolete entry %p",FUNC_NAME,e->data);
        //}
      }
      e = next;
    }
  unlock();
  while(!free_profiles.empty()){
    profile = free_profiles.front();
    delete profile;
    free_profiles.pop_front();
  }
}

bool ProfilesCache::is_obsolete(entry *e,struct timeval *now){
  return timercmp(now,&e->data->expire_time,>);
}
  
void ProfilesCache::startTimer(){
  DBG("ProfilesCache: start timer");
  AmAppTimer::instance()->setTimer(this,timeout);
}

void ProfilesCache::stopTimer(){
  DBG("ProfilesCache: stop timer");
  AmAppTimer::instance()->removeTimer(this);
}

void ProfilesCache::on_clean(){
  DBG("ProfilesCache: cleanup timer hit");
  check_obsolete();
}
