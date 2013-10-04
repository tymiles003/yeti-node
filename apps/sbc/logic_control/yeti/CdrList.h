#ifndef _CdrList_H
#define _CdrList_H

#include <AmThread.h>
#include "Cdr.h"
#include "SqlRouter.h"
#include "MurmurHash.h"

class CdrList: public MurmurHash<string,string,Cdr> {
    public:
	CdrList(unsigned long buckets = 65000);
	~CdrList();
	
    Cdr *get_by_local_tag(string local_tag);
	void getCalls(AmArg &calls,int limit,const SqlRouter *router);
	int getCall(const string local_tag,AmArg &call,const SqlRouter *router);
	
    protected:
      	uint64_t hash_lookup_key(const string *key);
	bool cmp_lookup_key(const string *k1,const string *k2);
	void init_key(string **dest,const string *src);
	void free_key(string *key);
	
    private:
	void cdr2arg(const Cdr *cdr,const SqlRouter *router,AmArg& arg);
};

#endif

