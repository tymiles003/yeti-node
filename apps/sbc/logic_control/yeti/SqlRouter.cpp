#include "AmPlugIn.h"
#include "AmUtils.h"
#include "log.h"
#include "AmArg.h"
#include "AmConfig.h"
#include "SBCCallControlAPI.h"
#include <string.h>
#include <syslog.h>
#include <exception>
#include <algorithm>
#include "SBCCallProfile.h"
#include "sip/parse_nameaddr.h"
#include "sip/parse_uri.h"
#include "PgConnectionPool.h"
#include "SqlRouter.h"
#include "DbTypes.h"
#include "yeti.h"

SqlRouter::SqlRouter():
  master_pool(NULL),
  slave_pool(NULL),
  cdr_writer(NULL),
  cache(NULL),
  mi(5)
{
  set(1);
  clearStats();
}

SqlRouter::~SqlRouter()
{
  
  if (master_pool)
    delete master_pool;
  
  if (slave_pool)
    delete slave_pool;
  
  if (cdr_writer)
    delete cdr_writer;
  
  if (cache_enabled&&cache)
    delete cache;
}

void SqlRouter::stop()
{

  if(master_pool)
    master_pool->stop();
  if(slave_pool)
    slave_pool->stop();
  if(cdr_writer)
    cdr_writer->stop();
}

void SqlRouter::release(std::set<SqlRouter *> &routers){
	DBG("SqlRouter::release()");
	if(dec_and_test()){
		DBG("SqlRouter::release() stop and delete instance");
		stop();
		routers.erase(this);
		delete this;
	}
}

int SqlRouter::run(){
  master_pool->start();
  WARN("Master SQLThread started\n");
  if (1==failover_to_slave){
    slave_pool->start();
    WARN("Slave SQLThread started\n");
  }
  start_time = time(NULL);
  return 0;
};

int SqlRouter::db_configure(AmConfigReader& cfg){
	int ret = 1;
try {
	int n;
		//load config from db
	string sql_query,prefix("master");
	dbc.cfg2dbcfg(cfg,prefix);
	pqxx::connection c(dbc.conn_str());
	{
		pqxx::nontransaction t(c);
		pqxx::result r = t.exec("SELECT * from "+getprofile_schema+".load_interface_out()");
		for(pqxx::result::size_type i = 0; i < r.size();++i){
			const pqxx::result::tuple &t = r[i];
			DBG("load_interface_out:     %ld: %s : %s, %s",i,
				t["varname"].c_str(),t["vartype"].c_str(),t["forcdr"].c_str());
			if(true==t["forcdr"].as<bool>())
				dyn_fields.push_back(pair<string,string>(t["varname"].c_str(),t["vartype"].c_str()));
		}
	}
	{
		pqxx::nontransaction t(c);
		pqxx::result r = t.exec("SELECT * from "+getprofile_schema+".load_interface_in()");
		for(pqxx::result::size_type i = 0; i < r.size();++i){
			const pqxx::result::tuple &t = r[i];
			DBG("load_interface_in:     %ld: %s : %s",i,
				t["varname"].c_str(),t["vartype"].c_str());
			used_header_fields.push_back(t["varname"].c_str());
		}
	}
	c.disconnect();

		//apply them
	sql_query = "SELECT * FROM "+getprofile_schema+"."+getprofile_function+"($1";
		n = GETPROFILE_STATIC_FIELDS_COUNT+used_header_fields.size();
		for(int i = 2;i<=n;i++) sql_query.append(",$"+int2str(i));
		sql_query.append(");");
	prepared_queries["getprofile"] = pair<string,int>(sql_query,n);

	sql_query = "SELECT "+writecdr_schema+"."+writecdr_function+"($1";
		n = WRITECDR_STATIC_FIELDS_COUNT+dyn_fields.size();
		for(int i = 2;i<=n;i++) sql_query.append(",$"+int2str(i));
		sql_query.append(");");
	cdr_prepared_queries["writecdr"] = pair<string,int>(sql_query,n);

	ret = 0;

} catch(const pqxx::pqxx_exception &e){
	ERROR("SqlRouter::db_configure: pqxx_exception: %s ",e.base().what());
}
	return ret;
}

int SqlRouter::configure(AmConfigReader &cfg){

	CdrWriterCfg cdrconfig;
	PgConnectionPoolCfg masterpoolcfg,slavepoolcfg;

#define GET_VARIABLE(var)\
	if(!cfg.hasParameter(#var)){\
		ERROR("missed parameter '"#var"'");\
		return 1;\
	}\
	var = cfg.getParameter(#var);

	getprofile_schema = Yeti::instance()->config.db_schema;
	GET_VARIABLE(getprofile_function);

	GET_VARIABLE(writecdr_schema);
	GET_VARIABLE(writecdr_function);

#undef GET_VARIABLE

  if(0==db_configure(cfg)){
    INFO("SqlRouter::db_configure: config successfuly readed");
  } else {
    INFO("SqlRouter::db_configure: config read error");
    return 1;
  }
    
  cdrconfig.name="cdr";
  if (0==cdrconfig.cfg2CdrWrCfg(cfg)){
    cdrconfig.prepared_queries = cdr_prepared_queries;
    cdrconfig.dyn_fields  = dyn_fields;
    INFO("Cdr writer pool config loaded");
  } else {
    INFO("Cdr writer pool config loading error");
    return 1;
  }
  
  masterpoolcfg.name="master";
  if(0==masterpoolcfg.cfg2PgCfg(cfg)){
    masterpoolcfg.prepared_queries = prepared_queries;
    INFO("Master pool config loaded");
  } else {
    ERROR("Master pool config loading error");
    return 1;
  }
  failover_to_slave=cfg.hasParameter("failover_to_slave") ? cfg.getParameterInt("failover_to_slave") : 0;
  
  if (1==failover_to_slave){
    slavepoolcfg.name="slave";
    if (0==slavepoolcfg.cfg2PgCfg(cfg)){
      slavepoolcfg.prepared_queries = prepared_queries;
      INFO("Slave pool config loaded");
    } else{
      WARN("Failover to slave enabled but slave config is wrong. Disabling failover");
      failover_to_slave=0;
    }
  }

  master_pool= new PgConnectionPool;
  master_pool->set_config(masterpoolcfg);
  master_pool->add_connections(masterpoolcfg.size);
  master_pool->dump_config();
  WARN("Master SQLThread configured\n");
  if (1==failover_to_slave){
    slave_pool= new PgConnectionPool;
    slave_pool->set_config(slavepoolcfg);
    slave_pool->add_connections(slavepoolcfg.size);
    slave_pool->dump_config();
    WARN("Slave SQLThread configured\n");
  } else {
    WARN("Slave SQLThread disabled\n");
  }
  cdr_writer = new CdrWriter;
  if (0==cdr_writer->configure(cdrconfig)){
	//INFO("Cdr writer pool configured. Try to start worker threads");
    cdr_writer->start();
  } else {
    ERROR("Cdr writer pool configuration error.");
	return 1;
  }

  cache_enabled = cfg.getParameterInt("profiles_cache_enabled",0);
  if(cache_enabled){
    cache_check_interval = cfg.getParameterInt("profiles_cache_check_interval",30);
	cache_buckets = cfg.getParameterInt("profiles_cache_buckets",65000);
    cache = new ProfilesCache(used_header_fields,cache_buckets,cache_check_interval);
  }
  return 0;
}

void SqlRouter::update_counters(struct timeval &start_time){
    struct timeval now_time,diff_time;
    time_t now;
    int intervals;
    double diff,gps;

    gettimeofday(&now_time,NULL);
    //per second
    now = now_time.tv_sec;
    diff = difftime(now,mi_start);
    intervals = diff/mi;
    if(intervals > 0){
        mi_start = now;
        gps = gpi/(double)mi;
        gps_avg = gps;
        if(gps > gps_max)
            gps_max = gps;
        gpi = 1;
    } else {
       gpi++;
    }
    // took
    timersub(&now_time,&start_time,&diff_time);
    diff = diff_time.tv_sec+diff_time.tv_usec/(double)1e6;
    if(diff > gt_max)
        gt_max = diff;
    if(!gt_min || (diff < gt_min))
        gt_min = diff;
}

void SqlRouter::getprofiles(const AmSipRequest &req,CallCtx &ctx)
{
	PgConnection *conn = NULL;
	PgConnectionPool *pool = master_pool;
	ProfilesCacheEntry *entry = NULL;
	bool getprofile_fail = true;
	int refuse_code = 0xffff;
	string req_hdrs,hdr;
	struct timeval start_time;

	DBG("Lookup profile for request: \n %s",req.print().c_str());

	hits++;
	gettimeofday(&start_time,NULL);

	if(cache_enabled&&cache->get_profiles(&req,ctx.profiles)){
		DBG("%s() got from cache. %ld profiles in set",FUNC_NAME,ctx.profiles.size());
		cache_hits++;
		update_counters(start_time);
		return;
	}

	while (getprofile_fail&&pool) {
	try {
		conn = pool->getActiveConnection();
		if(conn!=NULL){
			entry = _getprofiles(req,conn);
			pool->returnConnection(conn);
			getprofile_fail = false;
		} else {
			DBG("Cant get active connection on %s",pool->pool_name.c_str());
			refuse_code = FC_GET_ACTIVE_CONNECTION;
		}
	} catch(GetProfileException &e){
		DBG("GetProfile exception on %s SQLThread: fatal = %d code  = '%d'",
			pool->pool_name.c_str(),
			e.fatal,e.code);
		if(e.fatal){
			pool->returnConnection(conn,PgConnectionPool::CONN_COMM_ERR);
		} else {
			pool->returnConnection(conn);
			refuse_code = e.code;
		}
	} catch(pqxx::broken_connection &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_COMM_ERR);
		refuse_code = FC_DB_BROKEN_EXCEPTION;
		DBG("SQL exception on %s SQLThread: pqxx::broken_connection.",pool->pool_name.c_str());
	} catch(pqxx::conversion_error &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_DB_EXCEPTION);
		refuse_code =  FC_DB_CONVERSION_EXCEPTION;
		DBG("SQL exception on %s SQLThread: conversion error: %s.",pool->pool_name.c_str(),e.what());
	} catch(pqxx::pqxx_exception &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_DB_EXCEPTION);
		refuse_code = FC_DB_BASE_EXCEPTION;
		DBG("SQL exception on %s SQLThread: %s.",pool->pool_name.c_str(),e.base().what());
	}

	if(getprofile_fail&&pool == master_pool&&1==failover_to_slave) {
		ERROR("SQL failover enabled. Trying slave connection");
		pool = slave_pool;
	} else {
		pool = NULL;
	}

	} //while

	if(getprofile_fail){
		ERROR("SQL cant get profiles. Drop request");
		SqlCallProfile *profile = new SqlCallProfile();
		profile->disconnect_code_id = refuse_code;
		ctx.SQLexception = true;
		ctx.profiles.push_back(profile);
	} else {
		update_counters(start_time);
		db_hits++;
		ctx.profiles = entry->profiles;
		if(cache_enabled&&timerisset(&entry->expire_time))
			cache->insert_profile(&req,entry);
	}
	return;
}

ProfilesCacheEntry* SqlRouter::_getprofiles(const AmSipRequest &req, pqxx::connection* conn)
{
	pqxx::result r;
	pqxx::nontransaction tnx(*conn);
	ProfilesCacheEntry *entry = NULL;
	Yeti::global_config &gc = Yeti::instance()->config;

	const char *sptr;
	sip_nameaddr na;
	sip_uri from_uri,to_uri,contact_uri;

	sptr = req.to.c_str();
	if(	parse_nameaddr(&na,&sptr,req.to.length()) < 0 ||
		parse_uri(&to_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException(FC_PARSE_TO_FAILED,false);
	}
	sptr = req.contact.c_str();
	if(	parse_nameaddr(&na,&sptr,req.contact.length()) < 0 ||
		parse_uri(&contact_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException(FC_PARSE_CONTACT_FAILED,false);
	}
	sptr = req.from.c_str();
	if(	parse_nameaddr(&na,&sptr,req.from.length()) < 0 ||
		parse_uri(&from_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException(FC_PARSE_FROM_FAILED,false);
	}

	string from_name = c2stlstr(na.name);
	//simply remove all double quotes from string
	from_name.erase(std::remove(from_name.begin(), from_name.end(), '"'),from_name.end());

	if(tnx.prepared("getprofile").exists()){
		pqxx::prepare::invocation invoc = tnx.prepared("getprofile");
		invoc(gc.node_id);
		invoc(gc.pop_id);
		invoc(req.remote_ip);
		invoc(req.remote_port);
		invoc(req.local_ip);
		invoc(req.local_port);
		invoc(from_name);
		invoc(c2stlstr(from_uri.user));
		invoc(c2stlstr(from_uri.host));
		invoc(from_uri.port);
		invoc(c2stlstr(to_uri.user));
		invoc(c2stlstr(to_uri.host));
		invoc(to_uri.port);
		invoc(c2stlstr(contact_uri.user));
		invoc(c2stlstr(contact_uri.host));
		invoc(contact_uri.port);
		invoc(req.user);
		//invoc headers from sip request
		for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
			string hdr = getHeader(req.hdrs,*it);
			if(hdr.length())
				invoc(hdr);
			else
				invoc();
		}
		r = invoc.exec();
	} else {
		throw GetProfileException(FC_NOT_PREPARED,true);
	}

	//DBG("%s() database returned %ld profiles",FUNC_NAME,r.size());

	if (r.size()==0){
		throw GetProfileException(FC_DB_EMPTY_RESPONSE,false);
	}

	entry = new ProfilesCacheEntry();

	if(cache_enabled){
		//get first callprofile cache_time as cache_time for entire profiles set
		int cache_time = r[0]["cache_time"].as<int>(0);
		//DBG("%s() cache_time = %d",FUNC_NAME,cache_time);
		if(cache_time > 0){
			//DBG("SqlRouter: entry lifetime is %d seconds",cache_time);
			gettimeofday(&entry->expire_time,NULL);
			entry->expire_time.tv_sec+=cache_time;
		} else {
			timerclear(&entry->expire_time);
		}
	}

	pqxx::result::const_iterator rit = r.begin();
	for(;rit != r.end();++rit){
		const pqxx::result::tuple &t = (*rit);
		SqlCallProfile* profile = new SqlCallProfile();
		//read profile
		if(!profile->readFromTuple(t)){
			delete profile;
			delete entry;
			throw GetProfileException(FC_READ_FROM_TUPLE_FAILED,false);
		}
		//fill dyn fields
		DynFieldsT_iterator it = dyn_fields.begin();
		for(;it!=dyn_fields.end();++it){
			profile->dyn_fields.push_back(t[it->first].c_str());
		}
		profile->infoPrint(dyn_fields);
		//evaluate it
		if(!profile->eval()){
			delete profile;
			delete entry;
			throw GetProfileException(FC_EVALUATION_FAILED,false);
		}
		//push to ret
		entry->profiles.push_back(profile);
	}

	return entry;
}


void SqlRouter::align_cdr(Cdr &cdr){
    DynFieldsT_iterator it = dyn_fields.begin();
    for(;it!=dyn_fields.end();++it){
        cdr.dyn_fields.push_back("0");
    }
}

void SqlRouter::write_cdr(Cdr* cdr, bool last)
{
  DBG("%s(%p) last = %d",FUNC_NAME,cdr,last);
  if(!cdr->writed){
	//DBG("%s(%p) write now",FUNC_NAME,cdr);
    cdr->update(Write);
	cdr->is_last = last;
    cdr_writer->postcdr(cdr);
  } else {
	  DBG("%s(%p) trying to write already writed cdr",FUNC_NAME,cdr);
  }
}

void SqlRouter::dump_config()
{
  master_pool->dump_config();
  slave_pool->dump_config();
}

void SqlRouter::closeCdrFiles(){
	if(cdr_writer)
		cdr_writer->closeFiles();
}

void SqlRouter::clearStats(){
  if(cdr_writer)
    cdr_writer->clearStats();
  if(master_pool)
    master_pool->clearStats();
  if(slave_pool)
    slave_pool->clearStats();

  time(&mi_start);
  hits = 0;
  db_hits = 0;
  cache_hits = 0;
  gpi = 0;
  gt_min = 0;
  gt_max = 0;
  gps_max = 0;
  gps_avg = 0;

}

void SqlRouter::clearCache(){
	if(cache_enabled && cache){
		cache->clear();
	}
}

void SqlRouter::getConfig(AmArg &arg){
	AmArg u;
	arg["config_db"] = dbc.conn_str();
	arg["failover_to_slave"] = failover_to_slave;

	if(master_pool){
		master_pool->getConfig(u);
		arg.push("master_pool",u);
		u.clear();
	}

	if(failover_to_slave&&slave_pool){
		slave_pool->getConfig(u);
		arg.push("slave_pool",u);
		u.clear();
	}

	arg["getprofile_call"] = getprofile_schema+"."+getprofile_function;
	arg["writecdr_call"] = writecdr_schema+"."+writecdr_function;

	vector<string>::const_iterator fit = used_header_fields.begin();
	for(;fit!=used_header_fields.end();++fit){
		u.push(*fit);
	}
	arg.push("sipreq_header_fields",u);
	u.clear();

	DynFieldsT_iterator dit = dyn_fields.begin();
	for(;dit!=dyn_fields.end();++dit){
		u.push(dit->first + " : "+dit->second);
	}
	arg.push("dyn_fields",u);
	u.clear();

	if(cdr_writer){
		cdr_writer->getConfig(u);
		arg.push("cdrwriter",u);
		u.clear();
	}

	arg["cache_enabled"] = cache_enabled;
	if(cache_enabled){
		arg["cache_check_interval"] = cache_check_interval;
		arg["cache_buckets"] = cache_buckets;
	}

}

void SqlRouter::getStats(AmArg &arg){
  AmArg underlying_stats;
      /* SqlRouter stats */
  arg["uptime"] = difftime(time(NULL),start_time);
  arg["refs"] = (long int)get();
  arg["gt_min"] = gt_min;
  arg["gt_max"] = gt_max;
  arg["gps_max"] = gps_max;
  arg["gps_avg"] = gps_avg;

  arg["hits"] = hits;
  arg["db_hits"] = db_hits;
  if(cache_enabled){
    arg["cache_hits"] = cache_hits;
  }
      /* SqlRouter ProfilesCache stats */
  if(cache_enabled){
	//underlying_stats["entries"] = (int)cache->get_count();
	cache->getStats(underlying_stats);
	arg.push("profiles_cache",underlying_stats);
	underlying_stats.clear();
  }
      /* pools stats */
  if(master_pool){
	master_pool->getStats(underlying_stats);
	arg.push("master_pool",underlying_stats);
	underlying_stats.clear();
  }
  if(slave_pool){
	slave_pool->getStats(underlying_stats);
	arg.push("slave_pool",underlying_stats);
	underlying_stats.clear();
  }
      /* cdr writer stats */
  if(cdr_writer){
	cdr_writer->getStats(underlying_stats);
	arg.push("cdr_writer",underlying_stats);
	underlying_stats.clear();
  }
}
