#ifndef RESOURCECACHE_H
#define RESOURCECACHE_H

#include "AmConfigReader.h"
#include "AmThread.h"
#include "Resource.h"
#include "AmArg.h"
#include "hiredis/hiredis.h"
#include "RedisConnPool.h"
#include <list>

struct ResourceCacheException {
	int code;
	string what;
	ResourceCacheException(string w, int c): what(w), code(c) {}
};

enum ResourceResponse {
	RES_SUCC,			//we successful got all resources
	RES_BUSY,			//one of resources is busy
	RES_ERR				//error occured on interaction with cache
};

class ResourceCache
	: public AmThread
{
	RedisConnPool write_pool,read_pool;
	ResourceList put_resources_queue;
	ResourceList get_resources_queue;
	AmMutex queues_mutex;
	AmCondition <bool>data_ready;
	bool tostop;

	string get_key(Resource &r);

	bool init_resources();
	void pending_get(Resource &r);
	void pending_get_finish();

public:
	ResourceCache();redisContext *w_ctx;
	int configure(const AmConfigReader &cfg);
	void run();
	void on_stop();

	ResourceResponse get(ResourceList &rl,
						 ResourceList::iterator &resource);
	void put(ResourceList &rl);

	void getResourceState(int type, int id, AmArg &ret);

	void GetConfig(AmArg& ret);
};

#endif // RESOURCECACHE_H
