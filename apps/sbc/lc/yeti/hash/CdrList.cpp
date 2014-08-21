#include "CdrList.h"
#include "log.h"

CdrList::CdrList(unsigned long buckets):MurmurHash<string,string,Cdr>(buckets){
	//DBG("CdrList()");
}

CdrList::~CdrList(){
	//DBG("~CdrList()");
}

uint64_t CdrList::hash_lookup_key(const string *key){
	//!got segfault due to invalid key->size() value. do not trust it
	//return hashfn(key->c_str(),key->size());
	const char *s = key->c_str();
	return hashfn(s,strlen(s));
}

bool CdrList::cmp_lookup_key(const string *k1,const string *k2){
	return *k1 == *k2;
}

void CdrList::init_key(string **dest,const string *src){
	*dest = new string;
	(*dest)->assign(*src);
}

void CdrList::free_key(string *key){
	delete key;
}

int CdrList::insert(Cdr *cdr){
	int err = 1;
	if(cdr){
		DBG("%s() local_tag = %s",FUNC_NAME,cdr->local_tag.c_str());
		cdr->lock();
			if(!cdr->inserted2list && MurmurHash::insert(&cdr->local_tag,cdr,true,true)){
				err = 0;
				cdr->inserted2list = true;
			} else {
				ERROR("attempt to double insert into active calls list. integrity threat");
				log_stacktrace(L_ERR);
			}
		cdr->unlock();
	} else {
		ERROR("%s() cdr = NULL",FUNC_NAME);
		log_stacktrace(L_ERR);
	}
	return err;
}

void CdrList::erase_unsafe(const string &local_tag, bool locked){
	erase_lookup_key(&local_tag, locked);
}

int CdrList::erase(Cdr *cdr){
	int err = 1;
	if(cdr){
		//DBG("%s() local_tag = %s",FUNC_NAME,cdr->local_tag.c_str());
		cdr->lock();
			if(cdr->inserted2list){
				//erase_lookup_key(&cdr->local_tag);
				erase_unsafe(cdr->local_tag);
				err = 0;
			} else {
				//ERROR("attempt to erase not inserted cdr local_tag = %s",cdr->local_tag.c_str());
				//log_stacktrace(L_ERR);
			}
		cdr->unlock();
	} else {
		//ERROR("CdrList::%s() cdr = NULL",FUNC_NAME);
		//log_stacktrace(L_ERR);
	}
	return err;
}

Cdr *CdrList::get_by_local_tag(string local_tag){
	return at_data(&local_tag,false);
}

int CdrList::getCall(const string &local_tag,AmArg &call,const SqlRouter *router){
	Cdr *cdr;
	int ret = 0;
	lock();
	if((cdr = get_by_local_tag(local_tag))){
		cdr2arg(cdr,router,call);
		ret = 1;
	}
	unlock();
	return ret;
}

void CdrList::getCalls(AmArg &calls,int limit,const SqlRouter *router){
	AmArg call;
	entry *e;
	Cdr *cdr;
	int i = limit;
	lock();
		e = first;
		while(e&&i--){
			cdr = e->data;
			//cdr->lock();
				cdr2arg(cdr,router,call);
			//cdr->unlock();
			calls.push(call);
			e = e->list_next;
		}
	unlock();
}

void CdrList::getCallsFields(AmArg &calls,int limit,const SqlRouter *router,const AmArg &fields){
	vector<string> str_fields;
	AmArg call;
	entry *e;
	Cdr *cdr;

	//convert AmArg array to vector of strings
	assertArgArray(fields);
	for(unsigned int k = 0;k < fields.size(); k++)
		str_fields.push_back(fields.get(k).asCStr());

	int i = limit;
	lock();
		e = first;
		while(e&&i--){
			cdr = e->data;
			//cdr->lock();
				cdr2arg(cdr,router,call,str_fields);
			//cdr->unlock();
			calls.push(call);
			e = e->list_next;
		}
	unlock();
}

void CdrList::cdr2arg(const Cdr *cdr,const SqlRouter *router, AmArg& arg){
	#define add_field(val)\
		arg[#val] = cdr->val;
	#define add_timeval_field(val)\
		arg[#val] = (cdr->val.tv_sec+cdr->val.tv_usec/1e6);

	struct timeval now;

	gettimeofday(&now, NULL);
	arg["local_time"] = now.tv_sec+now.tv_usec/1e6;
	add_timeval_field(cdr_born_time);
	add_timeval_field(start_time);
	add_timeval_field(connect_time);
	add_timeval_field(end_time);

	add_field(legB_remote_port);
	add_field(legB_local_port);
	add_field(legA_remote_port);
	add_field(legA_local_port);
	add_field(legB_remote_ip);
	add_field(legB_local_ip);
	add_field(legA_remote_ip);
	add_field(legA_local_ip);

	add_field(orig_call_id);
	add_field(term_call_id);
	add_field(local_tag);
	add_field(global_tag);

	add_field(time_limit);
	add_field(dump_level_id);

	const DynFieldsT &router_dyn_fields = router->getDynFields();
	DynFieldsT::const_iterator it = router_dyn_fields.begin();
	const size_t n = cdr->dyn_fields.size();
	for(unsigned int k = 0;k<n;++k,++it){
		arg[it->first] = cdr->dyn_fields.get(k);
	}

	#undef add_field
	#undef add_timeval_field
}

void CdrList::cdr2arg(const Cdr *cdr,const SqlRouter *router, AmArg& arg, const vector<string> &wanted_fields){
	#define filter(val)\
		if(find(wanted_fields.begin(),wanted_fields.end(),val)!=wanted_fields.end())
	#define add_field(val)\
		filter(#val)\
			arg[#val] = cdr->val;
	#define add_timeval_field(val)\
		filter(#val)\
			arg[#val] = (cdr->val.tv_sec+cdr->val.tv_usec/1e6);

	struct timeval now;

	arg.assertStruct();

	if(wanted_fields.empty())
		return;

	filter("local_time") {
		gettimeofday(&now, NULL);
		arg["local_time"] = now.tv_sec+now.tv_usec/1e6;
	}
	add_timeval_field(cdr_born_time);
	add_timeval_field(start_time);
	add_timeval_field(connect_time);
	add_timeval_field(end_time);

	add_field(legB_remote_port);
	add_field(legB_local_port);
	add_field(legA_remote_port);
	add_field(legA_local_port);
	add_field(legB_remote_ip);
	add_field(legB_local_ip);
	add_field(legA_remote_ip);
	add_field(legA_local_ip);

	add_field(orig_call_id);
	add_field(term_call_id);
	add_field(local_tag);
	add_field(global_tag);

	add_field(time_limit);
	add_field(dump_level_id);

	const DynFieldsT &router_dyn_fields = router->getDynFields();
	DynFieldsT::const_iterator it = router_dyn_fields.begin();
	const size_t n = cdr->dyn_fields.size();
	for(unsigned int k = 0;k<n;++k,++it){
		filter(it->first)
			arg[it->first] = cdr->dyn_fields.get(k);
	}

	#undef add_field
	#undef add_timeval_field
	#undef filter
}