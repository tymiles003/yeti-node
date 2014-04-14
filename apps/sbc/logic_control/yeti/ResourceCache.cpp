#include "ResourceCache.h"
#include "log.h"
#include "AmUtils.h"
#include <sstream>

#define REDIS_STRING_ZERO "(null)"

#define CHECK_STATE_NORMAL 0
#define CHECK_STATE_FAILOVER 1
#define CHECK_STATE_SKIP 2

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


static long int Reply2Int(redisReply *r){
	long int ret = 0;
	char *s;
	switch(r->type) {
		case REDIS_REPLY_INTEGER:	//integer response
			DBG("Reply2Int: we have integer reply. simply assign it");
			ret = r->integer;
			break;
		case REDIS_REPLY_NIL:		//non existent key
			DBG("Reply2Int: we have nil reply. consider it as 0");
			ret = 0;
			break;
		case REDIS_REPLY_STRING:	//string response
			DBG("Reply2Int: we have string reply '%s'. trying convert it",r->str);
			s = r->str;
			if(!str2long(s,ret)){
				DBG("Reply2Int: conversion falied for: '%s'",r->str);
				throw ReplyDataException("invalid response from redis");
			}
			break;
		case REDIS_REPLY_ERROR:
			DBG("reply error: '%s'",r->str);
			throw ReplyDataException("undesired reply");
			break;
		default:
			throw ReplyTypeException("reply type [%d] not desired",r->type);
			break;
	}
	return ret;
}

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
	ResourceList filtered_put;
	redisReply *reply;
	list <int> desired_response;
	redisContext *write_ctx;

	setThreadName("yeti-res-wr");

	read_pool.start();
	write_pool.start();

	while(!tostop){
		data_ready.wait_for();

		put_queue_mutex.lock();
			put.swap(put_resources_queue);
		put_queue_mutex.unlock();

		for(ResourceList::const_iterator rit = put.begin();rit!=put.end();++rit)
			if((*rit).taken)
				filtered_put.push_back(*rit);

		if(!filtered_put.size()){
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

		ResourceList::iterator rit = filtered_put.begin();
		for(;rit!=filtered_put.end();++rit){
			Resource &r = (*rit);
			string key = get_key(r);
			redisAppendCommand(write_ctx,"DECRBY %b %d",
				key.c_str(),key.size(),
				r.takes);
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
					throw GetReplyException("DECRBY redisGetReply() != REDIS_OK",state);
				if(reply==NULL)
					throw GetReplyException("DECRBY reply == NULL",state);
				if(reply->type != desired){
					if(reply->type==REDIS_REPLY_ERROR){
						DBG("redis reply_error: %s",reply->str);
					}
					DBG("DECRBY desired_reply: %d, reply: %d",desired,reply->type);
					throw ReplyTypeException("DECRBY type not desired",reply->type);
				}

				if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
					redisReply *r;
					if(reply->elements != filtered_put.size()){
						throw ReplyDataException("DECRBY mismatch responses array size");
					}
					unsigned int i = 0;
					while(i<reply->elements){
						r = reply->element[i];
						if(r->type!=REDIS_REPLY_INTEGER){
							DBG("DECRBY r->type!=REDIS_REPLY_INTEGER, r->type = %d",
								r->type);
							throw ReplyDataException("DECRBY integer expected");
						}
						Resource &res =  filtered_put[i];
						INFO("put_resource %d:%d %lld/%d",res.type,res.id,r->integer,res.limit);
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
		filtered_put.clear();
		data_ready.set(false);
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
	resource = rl.begin();

	try {

		//preliminary resources availability check

		bool resources_available = true;
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
					throw GetReplyException("GET redisGetReply() != REDIS_OK",state);
				if(reply==NULL)
					throw GetReplyException("GET reply == NULL",state);
				if(reply->type != desired){
					if(reply->type==REDIS_REPLY_ERROR){
						DBG("redis reply_error: %s",reply->str);
					}
					DBG("GET desired_reply: %d, reply: %d",desired,reply->type);
					throw ReplyTypeException("GET type not desired",reply->type);
				}
				if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
					size_t n = reply->elements;
					if(n != rl.size()){
						DBG("GET reply->elements = %ld, desired size = %ld",
							n,rl.size());
						throw ReplyDataException("GET mismatch responses array size");
					}

					//resources availability checking cycle
					int check_state = CHECK_STATE_NORMAL;
					for(unsigned int i = 0;i<n;i++){
						long int now = Reply2Int(reply->element[i]);
						Resource &res = rl[i];

						if(CHECK_STATE_SKIP==check_state){
							DBG("skip %d:%d intended for failover",res.type,res.id);
							if(!res.failover_to_next) //last failover resource
								check_state = CHECK_STATE_NORMAL;
							continue;
						}

						INFO("check_resource %d:%d %ld/%d",
							res.type,res.id,now,res.limit);

						//check limit
						if(now > res.limit){
							INFO("resource %d:%d overload ",
								res.type,res.id);
							if(res.failover_to_next){
								INFO("failover_to_next enabled. check the next resource");
								check_state = CHECK_STATE_FAILOVER;
								continue;
							}
							resource = resource+i;
							resources_available = false;
							break;
						} else {
							res.active = true;
							if(CHECK_STATE_FAILOVER==check_state){
								INFO("failovered to resource %d:%d",res.type,res.id);
								/*if(res.failover_to_next)	//skip if not last
									check_state = CHECK_STATE_SKIP;*/
							}
							check_state = res.failover_to_next ?
								CHECK_STATE_SKIP : CHECK_STATE_NORMAL;
						}
					}
				}
				freeReplyObject(reply);
			}
			redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_OK);

			//aquire resources if available

			if(!resources_available){
				WARN("resources unavailable");
				ret = RES_BUSY;
			} else {
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

				vector<int> active_idx;
				ResourceList::iterator rit = rl.begin();
				for(int i = 0;rit!=rl.end();++rit,i++){
					Resource &r = (*rit);

					if(!r.active || r.taken) continue;

					string key = get_key(r);
					redisAppendCommand(redis_ctx,"INCRBY %b %d",
						key.c_str(),key.size(),
						r.takes);
					desired_response.push_back(REDIS_REPLY_STATUS);
					active_idx.push_back(i);
				}

				redisAppendCommand(redis_ctx,"EXEC");
				desired_response.push_back(REDIS_REPLY_ARRAY);

				//perform query
				while(!desired_response.empty()){
					int desired = desired_response.front();
					desired_response.pop_front();

					int state = redisGetReply(redis_ctx,(void **)&reply);
					if(state!=REDIS_OK)
						throw GetReplyException("INCRBY redisGetReply() != REDIS_OK",state);
					if(reply==NULL)
						throw GetReplyException("INCRBY reply == NULL",state);
					if(reply->type != desired){
						if(reply->type==REDIS_REPLY_ERROR){
							DBG("INCRBY redis reply_error: %s",reply->str);
						}
						DBG("INCRBY desired_reply: %d, reply: %d",desired,reply->type);
						throw ReplyTypeException("INCRBY type not desired",reply->type);
					}

					if(reply->type==REDIS_REPLY_ARRAY){ /* process EXEC here */
						redisReply *r;
						size_t n = reply->elements;
						if(n != active_idx.size()){
							DBG("INCRBY reply->elements = %ld, desired size = %ld",
								n,active_idx.size());
							throw ReplyDataException("INCRBY mismatch responses array size");
						}
						for(unsigned int i = 0;i<n;i++){
							r = reply->element[i];
							if(r->type!=REDIS_REPLY_INTEGER){
								throw ReplyDataException("INCRBY integer expected");
							}
							Resource &res = rl[active_idx[i]];
							res.taken = true;
							INFO("grabbed_resource %d:%d %lld/%d",res.type,res.id,r->integer,res.limit);
						}
					}
					freeReplyObject(reply);
				}
				redis_pool->putConnection(redis_ctx,RedisConnPool::CONN_STATE_OK);
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

	DBG("%s() finished ",FUNC_NAME);
	return ret;
}

void ResourceCache::put(ResourceList &rl){
	//DBG("%s()",FUNC_NAME);
	put_queue_mutex.lock();
		put_resources_queue.insert(
			put_resources_queue.begin(),
			rl.begin(),
			rl.end());
	put_queue_mutex.unlock();
	data_ready.set(true);
	//DBG("%s() finished ",FUNC_NAME);
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
