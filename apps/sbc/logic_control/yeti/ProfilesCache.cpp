#include "ProfilesCache.h"

ProfilesCache::ProfilesCache(const vector<UsedHeaderField> &used_header_fields,
							 unsigned long buckets, double timeout):
	MurmurHash<ProfilesCacheKey,AmSipRequest,ProfilesCacheEntry>(buckets),
	timeout(timeout),
	used_header_fields(used_header_fields)
{
	startTimer();
}

ProfilesCache::~ProfilesCache(){
	stopTimer();
}

uint64_t ProfilesCache::hash_lookup_key(const AmSipRequest *key){
	uint64_t ret;
	string used_hdrs_values;

	getUsedHeadersValues(key,used_hdrs_values);
	ret =
		hashfn(key->local_ip.c_str(),key->local_ip.size()) ^
		hashfn(&key->local_port,sizeof(unsigned short)) ^
		hashfn(&key->remote_port,sizeof(unsigned short)) ^
		hashfn(key->from_uri.c_str(),key->from_uri.size()) ^
		hashfn(key->to.c_str(),key->to.size()) ^
		hashfn(key->contact.c_str(),key->contact.size()) ^
		hashfn(key->user.c_str(),key->user.size()) ^
		hashfn(used_hdrs_values.c_str(),used_hdrs_values.size());

	return ret; 
}

bool ProfilesCache::cmp_lookup_key(const AmSipRequest *k1,const ProfilesCacheKey *k2){
	string used_hdrs_values;
	getUsedHeadersValues(k1,used_hdrs_values);
	return
		(k1->local_ip == k2->local_ip) &&
		(k1->local_port == k2->local_port) &&
		(k1->remote_port == k2->remote_port) &&
		(k1->from_uri == k2->from_uri) &&
		(k1->contact == k2->contact) &&
		(k1->user == k2->user) &&
		(k1->to == k2->to) &&
		(used_hdrs_values == k2->used_headers_values);
}

void ProfilesCache::init_key(ProfilesCacheKey **dest,const AmSipRequest *src){
	string used_hdrs_values;
	ProfilesCacheKey *key = new ProfilesCacheKey;
	*dest = key;

	getUsedHeadersValues(src,used_hdrs_values);

	key->local_ip.assign(src->local_ip);
	key->local_port = src->local_port;
	key->remote_port = src->remote_port;
	key->from_uri.assign(src->from_uri);
	key->contact.assign(src->contact);
	key->user.assign(src->user);
	key->to.assign(src->to);
	key->used_headers_values.assign(used_hdrs_values);
}

void ProfilesCache::free_key(ProfilesCacheKey *key){
	delete key;
}

bool ProfilesCache::get_profiles(const AmSipRequest *req,list<SqlCallProfile *> &profiles){
	struct timeval now;
	bool ret = false;
	entry *e;

	lock();

	e = at(req,false);
	if(!e){
		DBG("ProflesCache: No appropriate profile in cache");
		unlock();
		return ret;
	}

	DBG("ProflesCache: Found profile in cache");
	gettimeofday(&now,NULL);
	if(is_obsolete(e,&now)){
		DBG("ProflesCache: Profile is obsolete. Remove it from cache");
		delete e->data;
		erase(e,false);
	} else {
		ProfilesCacheEntry *entry = e->data;
		list<SqlCallProfile *>::iterator pit = entry->profiles.begin();
		for(;pit!=entry->profiles.end();++pit){
			profiles.push_back((*pit)->copy());
		}
		ret = true;
	}

	unlock();

	return ret;
}

void ProfilesCache::insert_profile(const AmSipRequest *req,ProfilesCacheEntry *entry){
	ProfilesCacheEntry *e = entry->copy();
	lock();
	DBG("ProflesCache: add profile to cache");
	if(insert(req,e,false,true)){ //(false, true) eq (external locked,check unique)
		DBG("ProflesCache: profiles added");
	} else {
		ERROR("ProfilesCache: profiles already in cache. delete cloned entry");
		delete e;
	}
	unlock();
}

void ProfilesCache::check_obsolete(){
	entry *e,*next;
	struct timeval now;
	ProfilesCacheEntry *cache_entry;
	list<ProfilesCacheEntry *> free_entries;

	lock();
	gettimeofday(&now,NULL);
	e = first;
	while(e){
		next = e->next;
		if(is_obsolete(e,&now)){
			free_entries.push_back(e->data);
			erase(e,false);
		}
		e = next;
	}
	unlock();
	while(!free_entries.empty()){
		cache_entry = free_entries.front();
		delete cache_entry;
		free_entries.pop_front();
	}
}

bool ProfilesCache::is_obsolete(entry *e,struct timeval *now){
	return timercmp(now,&e->data->expire_time,>);
}

void ProfilesCache::startTimer(){
	AmAppTimer::instance()->setTimer(this,timeout);
}

void ProfilesCache::stopTimer(){
	AmAppTimer::instance()->removeTimer(this);
}

void ProfilesCache::on_clean(){
	check_obsolete();
}

void ProfilesCache::getStats(AmArg &arg){
	arg["entries"] = (int)get_count();
}

void ProfilesCache::clear(){
	entry *e,*next;
	list<ProfilesCacheEntry *> free_entries;
	ProfilesCacheEntry *cache_entry;
	lock();
	e = first;
	while(e){
		next = e->next;
		free_entries.push_back(e->data);
		erase(e,false);
		e = next;
	}
	unlock();
	while(!free_entries.empty()){
		cache_entry = free_entries.front();
		delete cache_entry;
		free_entries.pop_front();
	}
}

void ProfilesCache::getUsedHeadersValues(const AmSipRequest *key,string &values){
	string hdr;
	values.clear();
	for(vector<UsedHeaderField>::const_iterator it = used_header_fields.begin();
			it != used_header_fields.end(); ++it){
		if(it->is_hashkey()){
			hdr = getHeader(key->hdrs,it->getName());
			if(!hdr.empty())
				values += hdr;
		}
	}
}
