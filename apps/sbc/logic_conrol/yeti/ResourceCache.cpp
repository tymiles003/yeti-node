#include "ResourceCache.h"
#include "log.h"
#include "AmUtils.h"
#include <sstream>

#define REDIS_STRING_ZERO "(null)"

struct GetReplyException {
	string what;
	int status;
	GetReplyException(string w, int s): what(w), status(s) {}
};

struct ReplyTypeException {
	string what;
	int type;
	ReplyTypeException(string w, int t): what(w), type(t) {}
};

struct ReplyDataException {
	string what;
	ReplyDataException(string w): what(w) {}
};


ResourceCache::ResourceCache():
	tostop(false),
	data_ready(true)
{
}

int ResourceCache::configure(const AmConfigReader &cfg){
	return
		write_pool.configure(cfg,"write") ||
		read_pool.configure(cfg,"read");
}

void ResourceCache::run(){
	ResourceList put;
	redisReply *reply;
	list <int> desired_response;
//	DBG("%s()",FUNC_NAME);
	redisContext *write_ctx;

	read_pool.start();
	write_pool.start();

	while(!tostop){
		DBG("%s() while start",FUNC_NAME);
		data_ready.wait_for();

		put_queue_mutex.lock();
			put.swap(put_resources_queue);
		put_queue_mutex.unlock();

		if(put.empty()){
			data_ready.set(false);
			continue;
		}

		write_ctx = write_pool.getConnection();
		while(write_ctx==NULL){
			DBG("get connection can't get connection from write redis pool. retry every 5s");
			sleep(5);
			if(tostop)
				return;
			write_ctx = write_pool.getConnection();
		}

		desired_response.clear();
		//pipeline all needed
		redisAppendCommand(write_ctx,"MULTI");
		desired_response.push_back(REDIS_REPLY_STATUS);

		ResourceList::iterator rit = put.begin();
		for(;rit!=put.end();++rit){
			Resource &r = (*rit);

			string key = get_key(r);
			string takes = int2str(r.takes);

			redisAppendCommand(write_ctx,"DECRBY %b %b",
				key.c_str(),key.size(),
				takes.c_str(),takes.size());
			desired_response.push_back(REDIS_REPLY_STATUS);
		}

		redisAppendCommand(write_ctx,"EXEC");
		desired_response.push_back(REDIS_REPLY_ARRAY);

		//process replies
		try {
			while(!desired_response.empty()){
				int desired = desired_response.front();
				desired_response.pop_front();

				int state = redisGetReply(write_ctx,(void **)&reply);
				if(state!=REDIS_OK)
					throw GetReplyException("redisGetReply() != REDIS_OK",state);
				if(reply==NULL)
					throw GetReplyException("reply == NULL",state);
				if(reply->type != desired)
					throw ReplyTypeException("type not desired",reply->type);

				if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
					redisReply *r;
					if(reply->elements != put.size()){
						throw ReplyDataException("mismatch responses array size");
					}
					unsigned int i = 0;
					while(i<reply->elements){
						r = reply->element[i];
						if(r->type!=REDIS_REPLY_INTEGER){
							throw ReplyDataException("integer expected");
						}
						DBG("resource %d:%d %lld/%d",put[i].type,put[i].id,r->integer,put[i].limit);
						i++;
					}
				}
				freeReplyObject(reply);
			}
			write_pool.putConnection(write_ctx,RedisConnPool::CONN_STATE_OK);
		} catch(GetReplyException &e){
			ERROR("GetReplyException %s status: %d",e.what.c_str(),e.status);
			write_pool.putConnection(write_ctx,RedisConnPool::CONN_STATE_ERR);
			put.clear();
			continue;
		} catch(ReplyTypeException &e){
			ERROR("ReplyTypeException %s type: %d",e.what.c_str(),e.type);
			freeReplyObject(reply);
			write_pool.putConnection(write_ctx,RedisConnPool::CONN_STATE_ERR);
			put.clear();
			continue;
		} catch(ReplyDataException &e){
			ERROR("ReplyDataException %s",e.what.c_str());
			freeReplyObject(reply);
			write_pool.putConnection(write_ctx,RedisConnPool::CONN_STATE_ERR);
			put.clear();
			continue;
		}
		put.clear();
		data_ready.set(false);
		DBG("%s() while end",FUNC_NAME);
	}
}

void ResourceCache::on_stop(){
	tostop = true;
	data_ready.set(true);

	write_pool.stop();
	read_pool.stop();
}


string ResourceCache::get_key(Resource &r){
	ostringstream ss;
	ss << r.type << ":" << r.id;
	return ss.str();
}

ResourceResponse ResourceCache::get(ResourceList &rl,
									ResourceList::iterator &resource)
{
	ResourceResponse ret = RES_ERR;
	bool resources_grabbed = false;
	string zero_string = REDIS_STRING_ZERO;

	DBG("%s()",FUNC_NAME);

	resource = rl.begin();

	try {
		bool resources_available = false;
		list <int> desired_response;
		redisReply *reply = NULL;
		redisContext *redis_ctx = NULL;
		RedisConnPool *redis_pool = &read_pool;

		redis_ctx = redis_pool->getConnection();
		if(redis_ctx==NULL){
			throw ResourceCacheException("can't get connection from read redis pool",0);
		}

		//prepare request

		redisAppendCommand(redis_ctx,"MULTI");
		desired_response.push_back(REDIS_REPLY_STATUS);

		ResourceList::iterator rit = rl.begin();
		for(;rit!=rl.end();++rit){
			string key = get_key(*rit);
			redisAppendCommand(redis_ctx,"GET %b",
				key.c_str(),key.size());
			desired_response.push_back(REDIS_REPLY_STATUS);
		}

		redisAppendCommand(redis_ctx,"EXEC");
		desired_response.push_back(REDIS_REPLY_ARRAY);

		//perform request

		try {
			while(!desired_response.empty()){
				int desired = desired_response.front();
				desired_response.pop_front();

				int state = redisGetReply(redis_ctx,(void **)&reply);
				if(state!=REDIS_OK)
					throw GetReplyException("redisGetReply() != REDIS_OK",state);
				if(reply==NULL)
					throw GetReplyException("reply == NULL",state);
				if(reply->type != desired){
					if(reply->type==REDIS_REPLY_ERROR){
						DBG("redis reply_error: %s",reply->str);
					}
					DBG("desired_reply: %d, reply: %d",desired,reply->type);
					throw ReplyTypeException("type not desired",reply->type);
				}
				if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
					redisReply *r;
					if(reply->elements != rl.size()){
						throw ReplyDataException("mismatch responses array size");
					}
					unsigned int i = 0;
					char *str_val;
					while(i<reply->elements){
						long int  now;
						r = reply->element[i];
						switch(r->type) {
							case REDIS_REPLY_INTEGER:	//integer response
								//DBG("we have integer reply. simply assign it");
								now = r->integer;
								break;
							case REDIS_REPLY_NIL:		//non existent key
								//DBG("we have nil reply. consider it as 0");
								now = 0;
								break;
							case REDIS_REPLY_STRING:	//string response
								//DBG("we have string reply '%s'. trying convert it",r->str);
								str_val = r->str;
								if(!str2long(str_val,now)){
									DBG("conversion falied for: '%s'",r->str);
									throw ReplyDataException("invalid response from redis");
								}
								break;
							case REDIS_REPLY_ERROR:
								DBG("reply error: '%s'",r->str);
								throw ReplyDataException("undesired reply");
								break;
							default:
								throw ReplyTypeException("reply type not desired",r->type);
								break;
						}
						int limit = rl[i].limit;
						DBG("resource %d:%d %ld/%d",
							rl[i].type,rl[i].id,now,limit);
						if(limit!=0){ //0 means unlimited resource
							//check limit
							if(now > limit){
								DBG("resource %d:%d overload ",
									rl[i].type,rl[i].id);
								resource = resource+i;
								break;
							}
						}
						i++;
					}
					if(i==reply->elements) //all resources checked and available
						resources_available = true;
				}
				freeReplyObject(reply);
			}
			redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_OK);

			if(!resources_available){
				DBG("resources unavailable");
				ret = RES_BUSY;
			} else {
				DBG("grab resources");
				//get write connection
				redis_pool = &write_pool;
				redis_ctx = redis_pool->getConnection();
				if(redis_ctx==NULL){
					throw ResourceCacheException("can't get connection from write redis pool",0);
				}
				//prepare query
				desired_response.clear();

				redisAppendCommand(redis_ctx,"MULTI");
				desired_response.push_back(REDIS_REPLY_STATUS);

				ResourceList::iterator rit = rl.begin();
				for(;rit!=rl.end();++rit){
					Resource &r = (*rit);
					string key = get_key(r);
					string takes = int2str(r.takes);
					redisAppendCommand(redis_ctx,"INCRBY %b %b",
						key.c_str(),key.size(),
						takes.c_str(),takes.size());
					desired_response.push_back(REDIS_REPLY_STATUS);
				}

				redisAppendCommand(redis_ctx,"EXEC");
				desired_response.push_back(REDIS_REPLY_ARRAY);

				//perform query
				while(!desired_response.empty()){
					int desired = desired_response.front();
					desired_response.pop_front();

					int state = redisGetReply(redis_ctx,(void **)&reply);
					if(state!=REDIS_OK)
						throw GetReplyException("redisGetReply() != REDIS_OK",state);
					if(reply==NULL)
						throw GetReplyException("reply == NULL",state);
					if(reply->type != desired){
						if(reply->type==REDIS_REPLY_ERROR){
							DBG("redis reply_error: %s",reply->str);
						}
						DBG("desired_reply: %d, reply: %d",desired,reply->type);
						throw ReplyTypeException("type not desired",reply->type);
					}

					if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
						redisReply *r;
						if(reply->elements != rl.size()){
							throw ReplyDataException("mismatch responses array size");
						}
						unsigned int i = 0;
						while(i<reply->elements){
							r = reply->element[i];
							if(r->type!=REDIS_REPLY_INTEGER){
								throw ReplyDataException("integer expected");
							}
							DBG("resource %d:%d %lld/%d",rl[i].type,rl[i].id,r->integer,rl[i].limit);
							i++;
						}
					}
					freeReplyObject(reply);
				}
				redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_OK);
				resources_grabbed = true;
				ret =  RES_SUCC;
			} //else if(!resources_available)
		} catch(GetReplyException &e){
			ERROR("GetReplyException %s status: %d",e.what.c_str(),e.status);
			redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_ERR);
		} catch(ReplyTypeException &e){
			ERROR("ReplyTypeException %s type: %d",e.what.c_str(),e.type);
			freeReplyObject(reply);
			redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_ERR);
		} catch(ReplyDataException &e){
			ERROR("ReplyDataException %s",e.what.c_str());
			freeReplyObject(reply);
			redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_ERR);
		}
	} catch(ResourceCacheException &e){
		ERROR("exception: %s %d",e.what.c_str(),e.code);
	}

	if(!resources_grabbed){
		DBG("resources not grabbed. clear resources list");
		//avoid freeing of non grabbed resources
		rl.clear();
	}
	DBG("%s() finished ",FUNC_NAME);
	return ret;
}

void ResourceCache::put(ResourceList &rl){
	DBG("%s()",FUNC_NAME);
	put_queue_mutex.lock();
		put_resources_queue.insert(
			put_resources_queue.begin(),
			rl.begin(),
			rl.end());
	put_queue_mutex.unlock();
	data_ready.set(true);
	DBG("%s() finished ",FUNC_NAME);
	return;
}

void ResourceCache::GetConfig(AmArg& ret){
	AmArg u;

	read_pool.GetConfig(u);
	ret.push("read_pool",u);

	u.clear();
	write_pool.GetConfig(u);
	ret.push("write_pool",u);
}
