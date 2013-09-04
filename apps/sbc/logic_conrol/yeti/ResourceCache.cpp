#include "ResourceCache.h"
#include "log.h"
#include "AmUtils.h"
#include <sstream>

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
	list <int> desired_response;
	redisReply *reply;
	redisContext *read_ctx;

	DBG("%s()",FUNC_NAME);

	resource = rl.begin();

	try {
		read_ctx = read_pool.getConnection();
		if(read_ctx==NULL){
			throw ResourceCacheException("can't get connection from pool",0);
		}

		redisAppendCommand(read_ctx,"MULTI");
		desired_response.push_back(REDIS_REPLY_STATUS);

		ResourceList::iterator rit = rl.begin();
		for(;rit!=rl.end();++rit){
			Resource &r = (*rit);

			string key = get_key(r);
			string takes = int2str(r.takes);

			redisAppendCommand(read_ctx,"INCRBY %b %b",
				key.c_str(),key.size(),
				takes.c_str(),takes.size());
			desired_response.push_back(REDIS_REPLY_STATUS);
		}

		redisAppendCommand(read_ctx,"EXEC");
		desired_response.push_back(REDIS_REPLY_ARRAY);

		try {
			while(!desired_response.empty()){
				int desired = desired_response.front();
				desired_response.pop_front();

				int state = redisGetReply(read_ctx,(void **)&reply);
				if(state!=REDIS_OK)
					throw GetReplyException("redisGetReply() != REDIS_OK",state);
				if(reply==NULL)
					throw GetReplyException("reply == NULL",state);
				if(reply->type != desired)
					throw ReplyTypeException("type not desired",reply->type);
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
						int limit = rl[i].limit;
						long long int  now = r->integer;
						DBG("resource %d:%d %lld/%d",
							rl[i].type,rl[i].id,now,limit);
						if(limit!=0){ //0 means unlimited resource
							//check limit
							if(now > limit){
								DBG("resource %d:%d overload ",
									rl[i].type,
									rl[i].id);
								//iterator on busy resource
								resource = resource+i;
								ret =  RES_BUSY;
								break;
							}
						}
						i++;
					}
					if(i==reply->elements) //all resources checked and available
						ret =  RES_SUCC;
				}
				freeReplyObject(reply);
			}
			read_pool.putConnection(read_ctx,RedisConnPool::CONN_STATE_OK);
		} catch(GetReplyException &e){
			ERROR("GetReplyException %s status: %d",e.what.c_str(),e.status);
			read_pool.putConnection(read_ctx,RedisConnPool::CONN_STATE_ERR);
		} catch(ReplyTypeException &e){
			ERROR("ReplyTypeException %s type: %d",e.what.c_str(),e.type);
			freeReplyObject(reply);
			read_pool.putConnection(read_ctx,RedisConnPool::CONN_STATE_ERR);
		} catch(ReplyDataException &e){
			ERROR("ReplyDataException %s",e.what.c_str());
			freeReplyObject(reply);
			read_pool.putConnection(read_ctx,RedisConnPool::CONN_STATE_ERR);
		}
	} catch(ResourceCacheException &e){
		ERROR("exception: %s %d",e.what.c_str(),e.code);
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
