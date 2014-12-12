#include "CdrList.h"
#include "log.h"

static const char *static_call_fields[] = {
	"local_time",
	"cdr_born_time",
	"start_time",
	"connect_time",
	"end_time",
	"legB_remote_port",
	"legB_local_port",
	"legA_remote_port",
	"legA_local_port",
	"legB_remote_ip",
	"legB_local_ip",
	"legA_remote_ip",
	"legA_local_ip",
	"orig_call_id",
	"term_call_id",
	"local_tag",
	"global_tag",
	"time_limit",
	"dump_level_id"
};
static unsigned int static_call_fields_count = 19;

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
	int i = limit;
	calls.assertArray();
	lock();
		e = first;
		while(e&&i--){
			calls.push(AmArg());
			cdr2arg(e->data,router,calls.back());
			e = e->list_next;
		}
	unlock();
}

void CdrList::getCallsFields(AmArg &calls,int limit,const SqlRouter *router,const vector<string> &fields){
	AmArg call;
	entry *e;

	//convert AmArg array to vector of strings
	calls.assertArray();
	int i = limit;
	lock();
		e = first;
		while(e&&i--){
			calls.push(AmArg());
			cdr2arg(e->data,router,calls.back(),fields);
			e = e->list_next;
		}
	unlock();
}

void CdrList:: cdr2arg(const Cdr *cdr,const SqlRouter *router, AmArg& arg){
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

void CdrList::getFields(AmArg &ret,SqlRouter *r){
	ret.assertArray();

	for(unsigned int k = 0; k < static_call_fields_count;k++)
		ret.push(static_call_fields[k]);

	const DynFieldsT &router_dyn_fields = r->getDynFields();
	for(DynFieldsT::const_iterator it = router_dyn_fields.begin();
			it!=router_dyn_fields.end();++it)
		ret.push(it->first);
}


bool CdrList::validate_fields(const vector<string> &wanted_fields, SqlRouter *router, AmArg &failed_fields){
	bool ret = true;
	const DynFieldsT &df = router->getDynFields();
	for(vector<string>::const_iterator i = wanted_fields.begin();
			i!=wanted_fields.end();++i){
		const string &f = *i;
		int k = static_call_fields_count-1;
		for(;k>=0;k--){
			if(f==static_call_fields[k])
				break;
		}
		if(k<0){
			//not present in static fields. search in dynamic
			DynFieldsT::const_iterator it = df.begin();
			for(;it!=df.end();++it)
				if(f==it->first)
					break;
			if(it==df.end()){ //not found in dynamic fields too
				ret = false;
				failed_fields.push(f);
			}
		}
	}
	return ret;
}

