#include "CdrList.h"
#include "log.h"

CdrList::CdrList(unsigned long buckets):MurmurHash<string,string,Cdr>(buckets){
  DBG("CdrList()");
}

CdrList::~CdrList(){
  DBG("~CdrList()");
}

uint64_t CdrList::hash_lookup_key(const string *key){
  return hashfn(key->c_str(),key->size());
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

Cdr *CdrList::get_by_local_tag(string local_tag){
  return at_data(&local_tag,false);
}

int CdrList::getCall(const string local_tag,AmArg &call){
  Cdr *cdr;
  int ret = 0;
  lock();
  if((cdr = get_by_local_tag(local_tag))){
    cdr2arg(cdr,call);
    ret = 1;
  }
  unlock();
  return ret;
}

void CdrList::getCalls(AmArg &calls,int limit){
  AmArg call;
  entry *e;
  Cdr *cdr;
  int i = limit;
  lock();
    e = first;
    while(e&&i--){
      cdr = e->data;
      cdr->lock();
	cdr2arg(cdr,call);
      cdr->unlock();
      calls.push(call);
      e = e->list_next;
    }
  unlock();
}

void CdrList::cdr2arg(const Cdr *cdr,AmArg& arg){
  #define add_field_to_ret(val)\
    arg[#val] = cdr->val;
  #define add_timeval_field_to_ret(val)\
    tt = cdr->val.tv_sec;\
    t = localtime(&tt);\
    strftime(s,sizeof s,"%Y.%m.%d %H:%M:%S",t);\
    arg[#val] = string(s);
    
  struct tm *t;
  time_t tt;
  char s[64];
  
  add_timeval_field_to_ret(cdr_born_time);
  add_timeval_field_to_ret(start_time);
  add_timeval_field_to_ret(connect_time);

  add_field_to_ret(legB_remote_port);
  add_field_to_ret(legB_local_port);
  add_field_to_ret(legA_remote_port);
  add_field_to_ret(legA_local_port);
  add_field_to_ret(legB_remote_ip);
  add_field_to_ret(legB_local_ip);
  add_field_to_ret(legA_remote_ip);
  add_field_to_ret(legA_local_ip);

  add_field_to_ret(orig_call_id);
  add_field_to_ret(term_call_id);
  add_field_to_ret(local_tag);
  
  add_field_to_ret(time_limit);
}
