#include "RedisConnPool.h"
#include "log.h"
#include "exception"

RedisConnPool::RedisConnPool():
	tostop(false),
	failed_ready(true),
	active_ready(false),
	failed_count(0)
{

}

int RedisConnPool::configure(const AmConfigReader &cfg,string name){
	pool_name = name;
	pool_size= cfg.getParameterInt(name+"_redis_size");
	if(!pool_size){
		ERROR("no pool size for %s redis",name.c_str());
		return -1;
	}
	active_timeout = cfg.getParameterInt(name+"_redis_timeout");
	if(!active_timeout){
		ERROR("no timeout for %s redis",name.c_str());
		return -1;
	}
	return cfg2RedisCfg(cfg,_cfg,pool_name);
}

void RedisConnPool::run(){
	redisContext *ctx = NULL;
	list<redisContext *> c;

//	DBG("%s()",FUNC_NAME);

	conn_mtx.lock();
		unsigned int active_count = active_ctxs.size();
		while(active_count < pool_size){
			ctx = redisConnect(_cfg.server.c_str(),_cfg.port);
			if(ctx != NULL && ctx->err){
				ERROR("failed conn for %s redis pool <host = %s:%d>",
					  pool_name.c_str(),
					  _cfg.server.c_str(),
					  _cfg.port);
				throw std::exception();
			} else {
				active_ctxs.push_back(ctx);
				active_count++;
			}
		}
	conn_mtx.unlock();

	while(!tostop){
//		DBG("failed_ready.wait_for()");
		failed_ready.wait_for();
		if(tostop) break;

		conn_mtx.lock();
			c.swap(failed_ctxs);
		conn_mtx.unlock();

		if(c.empty()){
			failed_ready.set(false);
			continue;
		}

		while(!c.empty()){
			if(tostop)
				break;
			ctx = c.front();
			if(reconnect(ctx)){
				c.pop_front();
				conn_mtx.lock();
					active_ctxs.push_back(ctx);
					failed_count--;
				conn_mtx.unlock();
				active_ready.set(true);
			} else {
				DBG("can't reconnect sleep %us",5);
				sleep(5);
			}
		}
		conn_mtx.lock();
			failed_ready.set(failed_count>0);
		conn_mtx.unlock();
	}
}

void RedisConnPool::on_stop(){
	redisContext *ctx;

//	DBG("%s()",FUNC_NAME);

	tostop = true;
	failed_ready.set(true);

	conn_mtx.lock();
		while(!active_ctxs.empty()){
			ctx = active_ctxs.front();
			active_ctxs.pop_front();
			redisFree(ctx);
		}
	conn_mtx.unlock();

	conn_mtx.lock();
		while(!failed_ctxs.empty()){
			ctx = failed_ctxs.front();
			failed_ctxs.pop_front();
			redisFree(ctx);
		}
	conn_mtx.unlock();

//	DBG("%s() finished",FUNC_NAME);
}

redisContext *RedisConnPool::getConnection(){
	redisContext *ctx = NULL;

	DBG("%s()",FUNC_NAME);

	while(ctx==NULL){

		conn_mtx.lock();
		if(active_ctxs.size()){
			ctx = active_ctxs.front();
			active_ctxs.pop_front();
			active_ready.set(!active_ctxs.empty());
		}
		conn_mtx.unlock();

		if(ctx==NULL){
			conn_mtx.lock();
			bool all_failed = pool_size == failed_count;
			conn_mtx.unlock();
			if (all_failed){
				DBG("all connections failed");
				break;
			}

			if(!active_ready.wait_for_to(active_timeout)){
				DBG("timeout waiting for an active con1ection (waited %ums)",active_timeout);
				break;
			}
		} else {
			DBG("got active connection [%p]",ctx);
		}
	}
	DBG("%s() = %p",FUNC_NAME,ctx);
	return ctx;
}

void RedisConnPool::putConnection(redisContext *ctx,ConnReturnState state){
	DBG("%s(%p,%d)",FUNC_NAME,ctx,state);

	if(state==CONN_STATE_OK){
		conn_mtx.lock();
			active_ctxs.push_back(ctx);
		conn_mtx.unlock();
		return;
	}
	if(state==CONN_STATE_ERR){
		conn_mtx.lock();
			failed_ctxs.push_back(ctx);
			failed_count++;
		conn_mtx.unlock();
		failed_ready.set(true);
		return;
	}
}

int RedisConnPool::cfg2RedisCfg(const AmConfigReader &cfg, RedisCfg &rcfg,string prefix){
	DBG("%s()",FUNC_NAME);

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

bool RedisConnPool::reconnect(redisContext *&ctx){
	DBG("%s()",FUNC_NAME);

	if(ctx!=NULL){
		redisFree(ctx);
	}
	ctx = redisConnect(_cfg.server.c_str(),_cfg.port);
	if (ctx != NULL && ctx->err) {
		ERROR("%s() can't connect: %d <%s>",FUNC_NAME,ctx->err,ctx->errstr);
		return false;
	}
	return true;
}
