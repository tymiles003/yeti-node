#ifndef RESOURCECACHE_H
#define RESOURCECACHE_H

#include "AmConfigReader.h"
#include "AmThread.h"
#include "Resource.h"
#include "hiredis/hiredis.h"
#include <list>

struct ResourceCacheException {
	int code;
	string what;
	ResourceCacheException(string w, int c): what(w), code(c) {}
};


struct RedisCfg {
	short port;
	string server;
};

enum ResourceResponse {
	RES_SUCC,			//we successful got all resources
	RES_BUSY,			//one of resources is busy
	RES_ERR				//error occured on interaction with cache
};

class ResourceCache
	: public AmThread
{
	ResourceList put_resources_queue;
	AmMutex put_queue_mutex;
	RedisCfg write_cfg;
	redisContext *write_ctx;
	AmCondition <bool>data_ready;
	bool tostop;
	bool write_exception;
	bool write_reconnect();

	redisContext *read_ctx;
	AmMutex read_mutex;
	RedisCfg read_cfg;
	bool read_exception;
	bool read_reconnect();

	string get_key(Resource &r);
	bool reconnect(redisContext *&ctx,const RedisCfg &cfg);
public:
	ResourceCache();redisContext *w_ctx;
	int configure(const AmConfigReader &cfg);
	int cfg2RedisCfg(const AmConfigReader &cfg, RedisCfg &rcfg,string prefix);
	void run();
	void on_stop();

	ResourceResponse get(ResourceList &rl,
						 ResourceList::iterator &resource);
	void put(ResourceList &rl);
};

#endif // RESOURCECACHE_H
