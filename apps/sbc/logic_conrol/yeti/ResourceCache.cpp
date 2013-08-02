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
	data_ready(true),
	write_exception(false),
	read_exception(false){
}

int ResourceCache::cfg2RedisCfg(const AmConfigReader &cfg, RedisCfg &rcfg,string prefix){
	rcfg.server = cfg.getParameter(prefix+"_redis_host");
	if(rcfg.server.empty()){
		DBG("no host for %s redis",prefix.c_str());
		return -1;
	}
	rcfg.port = cfg.getParameterInt(prefix+"_redis_port");
	if(!rcfg.port){
		DBG("no port for %s redis",prefix.c_str());
		return -1;
	}

	return 0;
}

int ResourceCache::configure(const AmConfigReader &cfg){
	return
		cfg2RedisCfg(cfg,write_cfg,"write") ||
		cfg2RedisCfg(cfg,read_cfg,"read");
}

void ResourceCache::run(){
	ResourceList put;
	redisReply *reply;
	list <int> desired_response;
	DBG("%s()",FUNC_NAME);

	if(!reconnect(write_ctx,write_cfg)){
		ERROR("%s() can't reconnect to write server",FUNC_NAME);
		write_exception = true;
	}
	if(!reconnect(read_ctx,read_cfg)){
		ERROR("%s() can't connect to read redis",FUNC_NAME);
		read_exception = true;
	}

	while(!tostop){
		if(!write_reconnect()){
			ERROR("%s() can't reconnect. sleep 5 seconds",FUNC_NAME);
			sleep(5);
			continue;
		} else {
			write_exception = false;
		}
		data_ready.wait_for();

		put_queue_mutex.lock();
			put.swap(put_resources_queue);
		put_queue_mutex.unlock();

		if(put.empty()){
			data_ready.set(false);
			continue;
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
		} catch(GetReplyException &e){
			ERROR("GetReplyException %s status: %d",e.what.c_str(),e.status);
			write_exception = true;
			put.clear();
			continue;
		} catch(ReplyTypeException &e){
			ERROR("ReplyTypeException %s type: %d",e.what.c_str(),e.type);
			freeReplyObject(reply);
			write_exception = true;
			put.clear();
			continue;
		} catch(ReplyDataException &e){
			ERROR("ReplyDataException %s",e.what.c_str());
			freeReplyObject(reply);
			write_exception = true;
			put.clear();
			continue;
		}
		put.clear();
		data_ready.set(false);
	}
}

void ResourceCache::on_stop(){
	tostop = true;
	data_ready.set(true);
	if(write_ctx!=NULL){
		redisFree(write_ctx);
	}
	if(read_ctx!=NULL){
		redisFree(read_ctx);
	}
}

bool ResourceCache::read_reconnect(){
	if(!read_exception)
		return true;
	return reconnect(read_ctx,read_cfg);
}

bool ResourceCache::write_reconnect(){
	if(!write_exception)
		return true;
	return reconnect(write_ctx,write_cfg);
}

bool ResourceCache::reconnect(redisContext *&ctx,const RedisCfg &cfg){
	if(ctx!=NULL){
		redisFree(ctx);
	}
	ctx = redisConnect(cfg.server.c_str(),cfg.port);
	if (ctx != NULL && ctx->err) {
		ERROR("%s() can't connect: %d <%s>",FUNC_NAME,ctx->err,ctx->errstr);
		return false;
	}
	return true;
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

	DBG("%s()",FUNC_NAME);

	resource = rl.begin();

	read_mutex.lock();
	try {
		if(!read_reconnect()){
			throw ResourceCacheException("connection to read redis lost",0);
		}

		redisAppendCommand(write_ctx,"MULTI");
		desired_response.push_back(REDIS_REPLY_STATUS);

		ResourceList::iterator rit = rl.begin();
		for(;rit!=rl.end();++rit){
			Resource &r = (*rit);

			string key = get_key(r);
			string takes = int2str(r.takes);

			redisAppendCommand(write_ctx,"INCRBY %b %b",
				key.c_str(),key.size(),
				takes.c_str(),takes.size());
			desired_response.push_back(REDIS_REPLY_STATUS);
		}

		redisAppendCommand(write_ctx,"EXEC");
		desired_response.push_back(REDIS_REPLY_ARRAY);

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
		} catch(GetReplyException &e){
			ERROR("GetReplyException %s status: %d",e.what.c_str(),e.status);
			read_exception = true;
		} catch(ReplyTypeException &e){
			ERROR("ReplyTypeException %s type: %d",e.what.c_str(),e.type);
			freeReplyObject(reply);
			read_exception = true;
		} catch(ReplyDataException &e){
			ERROR("ReplyDataException %s",e.what.c_str());
			freeReplyObject(reply);
			read_exception = true;
		}
	} catch(ResourceCacheException &e){
		ERROR("exception: %s %d",e.what.c_str(),e.code);
		read_exception = true;
	}
	read_mutex.unlock();
	return ret;
}

void ResourceCache::put(ResourceList &rl){
	DBG("%s()",FUNC_NAME);
	put_queue_mutex.lock();
		put_resources_queue.insert(
			put_resources_queue.begin(),
			rl.begin(),
			rl.end());
		//put_resources_queue.splice(put_resources_queue.begin(),rl); /*move instead of copy*/
	put_queue_mutex.unlock();
	//rl.clear();
	data_ready.set(true);
	return;
}

