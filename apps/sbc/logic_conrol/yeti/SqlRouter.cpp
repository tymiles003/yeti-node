#include "AmPlugIn.h"
#include "AmUtils.h"
#include "log.h"
#include "AmArg.h"
#include "AmConfig.h"
#include "SBCCallControlAPI.h"
#include <string.h>
#include <syslog.h>
#include <exception>
#include "SBCCallProfile.h"
#include "sip/parse_nameaddr.h"
#include "sip/parse_uri.h"
#include "PgConnectionPool.h"
#include "SqlRouter.h"
#include "DbTypes.h"

SqlRouter::SqlRouter():
  master_pool(NULL),
  slave_pool(NULL),
  cdr_writer(NULL),
  cache(NULL),
  mi(5)
{
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


int SqlRouter::run(){
  master_pool->start();
  WARN("Master SQLThread started\n");
  if (1==failover_to_slave){
    slave_pool->start();
    WARN("Slave SQLThread started\n");
  }
  return 0;
};

int SqlRouter::db_configure(AmConfigReader& cfg){
  string sql_query,prefix("master");
  pqxx::result r;
  DbConfig dbc;
  int n,ret = 1;
  
  dbc.cfg2dbcfg(cfg,prefix);
  try {  
    pqxx::connection c(dbc.conn_str());
      pqxx::work t(c);
	r = t.exec("SELECT * from switch.load_interface();");	
      t.commit();
    c.disconnect();
    dyn_fields.clear();
    
    for(pqxx::result::size_type i = 0; i < r.size();++i){
#ifndef NDEBUG
	DBG("%ld: %s,%s,%s",i,r[i]["varname"].c_str(),r[i]["vartype"].c_str(),r[i]["forcdr"].c_str());
#endif
      if(true==r[i]["forcdr"].as<bool>())
		dyn_fields.push_back(pair<string,string>(r[i]["varname"].c_str(),r[i]["vartype"].c_str()));
    }
    
    DBG("dyn_fields.size() = %ld",dyn_fields.size());
    prepared_queries.clear();
	sql_query = "SELECT * FROM switch.getprofile_f($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15);";
	prepared_queries["getprofile"] = pair<string,int>(sql_query,15);
    
    cdr_prepared_queries.clear();
    sql_query = "SELECT switch.writecdr($1";
    n = WRITECDR_STATIC_FIELDS_COUNT+dyn_fields.size();
    for(int i = 2;i<=n;i++){
	  sql_query.append(",$"+int2str(i));
    }
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
    INFO("Cdr writer pool configured. Try to start worker threads");
    cdr_writer->start();
  } else {
    ERROR("Cdr writer pool configuration error.");
  }
  
  used_header_fields_separator = cfg.getParameter("used_header_fields_separator");
  if(!used_header_fields_separator.length())
		used_header_fields_separator = ';';

  if(cfg.hasParameter("used_header_fields")) {
		used_header_fields = explode(cfg.getParameter("used_header_fields"), ",");  
	}
	
	for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
		DBG("header field: '%s'",it->data());
	}
	
  cache_enabled = cfg.getParameterInt("profiles_cache_enabled",0);
  if(cache_enabled){
    cache_check_interval = cfg.getParameterInt("profiles_cache_check_interval",30);
    int cache_buckets = cfg.getParameterInt("profiles_cache_buckets",65000);
    cache = new ProfilesCache(used_header_fields,cache_buckets,cache_check_interval);
  }
  return 0;
};

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

SqlCallProfile* SqlRouter::getprofile (const AmSipRequest &req)
{
  DBG("Lookup profile for request: \n %s",req.print().c_str());
  SqlCallProfile* ret = NULL;
  PgConnection *conn = NULL;
  PgConnectionPool *pool = master_pool;
  bool getprofile_fail = true;
  string req_hdrs,hdr;
  struct timeval start_time;

  hits++;
  gettimeofday(&start_time,NULL);

  if(cache_enabled&&(ret = cache->get_profile(&req))!=NULL){
    cache_hits++;
    update_counters(start_time);
    return ret;
  }
  
  while (getprofile_fail&&pool) {
    try {
		conn = pool->getActiveConnection();
		if(conn!=NULL){
			ret=_getprofile(req,conn);
			pool->returnConnection(conn);
			getprofile_fail = false;
		} else {
			DBG("Cant get active connection on %s",pool->pool_name.c_str());
		}
	} catch(GetProfileException &e){
		DBG("GetProfile exception on %s SQLThread: fatal = %d what = '%s'",
			pool->pool_name.c_str(),
			e.fatal,
			e.what.c_str());
		if(e.fatal){
			pool->returnConnection(conn,PgConnectionPool::CONN_COMM_ERR);
		} else {
			pool->returnConnection(conn);
			getprofile_fail = false;
			ret->SQLexception=true;
			ret->refuse_with = e.what;
			ret->cached = false;
		}
	} catch(pqxx::broken_connection &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_COMM_ERR);
		DBG("SQL exception on %s SQLThread: pqxx::broken_connection.",pool->pool_name.c_str());
	} catch(pqxx::conversion_error &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_DB_EXCEPTION);
		DBG("SQL exception on %s SQLThread: conversion error: %s.",pool->pool_name.c_str(),e.what());
    } catch(pqxx::pqxx_exception &e){
		pool->returnConnection(conn,PgConnectionPool::CONN_DB_EXCEPTION);
		DBG("SQL exception on %s SQLThread: %s.",pool->pool_name.c_str(),e.base().what());
    }
    
    if(getprofile_fail&&pool == master_pool&&1==failover_to_slave) {
      ERROR("SQL failover enabled. Trying slave connection");
      pool = slave_pool;
    } else {
      pool = NULL;
    }
  }

  if(getprofile_fail){
    ERROR("SQL cant get profile. Drop request");
    ret=new SqlCallProfile;
    ret->refuse_with="500 SQL error";
    ret->SQLexception=true;
    ret->cached = false;
  } else {
    update_counters(start_time);
    db_hits++;
    if(cache_enabled&&timerisset(&ret->expire_time))
      cache->insert_profile(&req,ret);
  }
  return ret;
}

SqlCallProfile* SqlRouter::_getprofile(const AmSipRequest &req, pqxx::connection* conn)
{
  pqxx::result r;
  pqxx::nontransaction tnx(*conn);
  SqlCallProfile* ret=new SqlCallProfile;
  string name="SQL";
  bool prepared_query = false;
  string req_hdrs,hdr;

	for(vector<string>::const_iterator it = used_header_fields.begin(); it != used_header_fields.end(); ++it){
		hdr = getHeader(req.hdrs,*it);
		if(hdr.length()){
			req_hdrs.append(*it);
			req_hdrs.append(":");
			req_hdrs.append(hdr);
			req_hdrs.append(used_header_fields_separator);
		}
	}
	DBG("req_hdrs: '%s' ",req_hdrs.data());

//	DBG("req dump : \n%s",req.print().c_str());

	const char *sptr;
	sip_nameaddr na;
	sip_uri from_uri,to_uri,contact_uri;
	//believe that already successfull being parsed before with underlying layers

	sptr = req.from.c_str();
	if(	parse_nameaddr(&na,&sptr,req.from.length()) < 0 ||
		parse_uri(&from_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException("500 Invalid can't parse 'from'",true);
	}
	sptr = req.to.c_str();
	if(	parse_nameaddr(&na,&sptr,req.to.length()) < 0 ||
		parse_uri(&to_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException("500 Invalid can't parse 'to'",true);
	}
	sptr = req.contact.c_str();
	if(	parse_nameaddr(&na,&sptr,req.contact.length()) < 0 ||
		parse_uri(&contact_uri,na.addr.s,na.addr.len) < 0){
		throw GetProfileException("500 Invalid can't parse 'contact'",true);
	}

  if(tnx.prepared("getprofile").exists()){
    prepared_query = true;
    pqxx::prepare::invocation invoc = tnx.prepared("getprofile");
      /* invoc static fields */
	invoc(req.remote_ip);
	invoc(req.remote_port);
	invoc(req.local_ip);
	invoc(req.local_port);
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
	invoc(req_hdrs);
      r = invoc.exec();
  } else {
	throw GetProfileException("no such prepared query",true);
  }
  
  if (r.size()==0){
	throw GetProfileException("500 Empty response from DB",false);
  }
  ret->SQLexception=false;
  if(prepared_query){
    /* fill dynamic fields list */
    DynFieldsT_iterator it = dyn_fields.begin();
    for(;it!=dyn_fields.end();++it){
      ret->dyn_fields.push_back(r[0][it->first].c_str());
    }
  } else {
    WARN("SqlRouter: Dynamic fields not used without prepared queries for perfomance reasons");
  }
  
  ret->append_headers=r[0]["append_headers"].c_str();
    
  ret->auth_enabled=r[0]["auth_enabled"].as<bool>();
  ret->auth_credentials.pwd=r[0]["auth_credentials_pwd"].c_str();
  ret->auth_credentials.user=r[0]["auth_credentials_user"].c_str();
  
  ret->auth_aleg_enabled=r[0]["auth_aleg_enabled"].as<bool>();
  ret->auth_aleg_credentials.pwd=r[0]["auth_aleg_credentials_pwd"].c_str();
  ret->auth_aleg_credentials.user=r[0]["auth_aleg_credentials_user"].c_str();

  ret->callid=r[0]["callid"].c_str();
  
  ret->codec_prefs.bleg_payload_order_str=r[0]["codec_prefs_bleg_payload_order"].c_str();
  ret->codec_prefs.bleg_prefer_existing_payloads_str=r[0]["codec_prefs_bleg_prefer_existing_payloads"].c_str();
  
  ret->codec_prefs.aleg_payload_order_str=r[0]["codec_prefs_aleg_payload_order"].c_str();
  ret->codec_prefs.aleg_prefer_existing_payloads_str=r[0]["codec_prefs_aleg_prefer_existing_payloads"].c_str();
  
//!FIX:  ret->contact=r[0]["contact"].c_str();
  ret->force_outbound_proxy=r[0]["force_outbound_proxy"].as<bool>();
  
  ret->rtprelay_enabled=r[0]["rtprelay_enabled"].as<bool>();
  ret->rtprelay_interface=r[0]["rtprelay_interface"].c_str();
  
  ret->aleg_rtprelay_interface=r[0]["aleg_rtprelay_interface"].c_str();

  ret->rtprelay_transparent_seqno=r[0]["rtprelay_transparent_seqno"].as<bool>();
  ret->rtprelay_transparent_ssrc=r[0]["rtprelay_transparent_ssrc"].as<bool>();
  
  if (true==r[0]["force_symmetric_rtp"].as<bool>()) {
      ret->force_symmetric_rtp="yes";
  }
   else{
      ret->force_symmetric_rtp="no";
  } 
  ret->msgflags_symmetric_rtp=r[0]["msgflags_symmetric_rtp"].as<bool>();

  //ret.force_symmetric_rtp_value
  ret->from=r[0]["from"].c_str();

  FilterEntry header_filter;
  header_filter.filter_type=String2FilterType(r[0]["headerfilter"].c_str());
  if (Undefined == header_filter.filter_type) {
    header_filter.filter_type=Transparent;
  }
  vector<string> elems = explode(r[0]["headerfilter_list"].c_str(), ",");
  for (vector<string>::iterator it=elems.begin(); it != elems.end(); it++) {
    transform(it->begin(), it->end(), it->begin(), ::tolower);
    header_filter.filter_list.insert(*it);
  }
  ret->headerfilter.push_back(header_filter);
  
  FilterEntry msg_filter;
  msg_filter.filter_type=String2FilterType(r[0]["messagefilter"].c_str());
  if ( Undefined==msg_filter.filter_type){
    msg_filter.filter_type=Transparent;
  }
  elems = explode(r[0]["messagefilter_list"].c_str(), ",");
  for (vector<string>::iterator it=elems.begin(); it != elems.end(); it++)
  msg_filter.filter_list.insert(*it);
  ret->messagefilter.push_back(msg_filter);
  
  //ret->next_hop=r[0]["next_hop"];
    if(r[0]["next_hop"].is_null()){
      DBG("NULL");
     // ret->next_hop.clear();
    } else {
      ret->next_hop=r[0]["next_hop"].c_str();
    }

  ret->next_hop_1st_req=r[0]["next_hop_1st_req"].as<bool>();
  ret->outbound_interface=r[0]["outbound_interface"].c_str();
  //ret.outbound_interface_value
  ret->outbound_proxy=r[0]["outbound_proxy"].c_str();
  ret->refuse_with=r[0]["refuse_with"].c_str();
  

  ret->ruri=r[0]["ruri"].c_str();
  //ret->ruri_host=r[0]["ruri_host"].c_str();

  FilterEntry sdpfilter;
  sdpfilter.filter_type=String2FilterType(r[0]["sdpfilter"].c_str());
  if ( Undefined!=sdpfilter.filter_type){
        vector<string> c_elems = explode(r[0]["sdpfilter_list"].c_str(), ",");
        for (vector<string>::iterator it=c_elems.begin(); it != c_elems.end(); it++) {
           string c = *it;
            std::transform(c.begin(), c.end(), c.begin(), ::tolower);
            sdpfilter.filter_list.insert(c);
        }
        ret->sdpfilter.push_back(sdpfilter);
  }

  ret->anonymize_sdp = r[0]["anonymize_sdp"].as<bool>();

  FilterEntry sdpalinesfilter;
  sdpalinesfilter.filter_type=String2FilterType(r[0]["sdpalinesfilter"].c_str());
  if (Transparent!=sdpalinesfilter.filter_type&&Undefined!=sdpalinesfilter.filter_type){
    vector<string> c_elems = explode(r[0]["sdpalinesfilter_list"].c_str(), ",");
    for (vector<string>::iterator it=c_elems.begin(); it != c_elems.end(); it++) {
      string c = *it;
      std::transform(c.begin(), c.end(), c.begin(), ::tolower);
      sdpalinesfilter.filter_list.insert(c);
    }
    ret->sdpalinesfilter.push_back(sdpalinesfilter);
  }

  ret->sst_enabled=r[0]["sst_enabled"].as<bool>()?"true":"false";
  ret->sst_aleg_enabled=r[0]["sst_aleg_enabled"].c_str();
  
  #define CP_SST_CFGVAR(cfgprefix, cfgkey, dstcfg)			\
    if (cfg.hasParameter(cfgprefix cfgkey)) {				\
      dstcfg.setParameter(cfgkey, cfg.getParameter(cfgprefix cfgkey));	\
    } else if (cfg.hasParameter(cfgkey)) {				\
      dstcfg.setParameter(cfgkey, cfg.getParameter(cfgkey));		\
    } else if (SBCFactory::cfg.hasParameter(cfgkey)) {			\
      dstcfg.setParameter(cfgkey, SBCFactory::cfg.getParameter(cfgkey)); \
    }
  
  if (ret->sst_enabled=="yes") {
    ret->sst_b_cfg.setParameter("session_expires",r[0]["sst_session_expires"].c_str());
    ret->sst_b_cfg.setParameter("minimum_timer",r[0]["sst_minimum_timer"].c_str());
    ret->sst_b_cfg.setParameter("maximum_timer",r[0]["sst_maximum_timer"].c_str());
    ret->sst_b_cfg.setParameter("session_refresh_method",r[0]["sst_session_refresh_method"].c_str());
    ret->sst_b_cfg.setParameter("accept_501_reply",r[0]["sst_accept_501_reply"].c_str());
  }
  if (ret->sst_aleg_enabled=="yes"){
    ret->sst_a_cfg.setParameter("session_expires",r[0]["sst_aleg_session_expires"].c_str());
    ret->sst_a_cfg.setParameter("minimum_timer",r[0]["sst_aleg_minimum_timer"].c_str());
    ret->sst_a_cfg.setParameter("maximum_timer",r[0]["sst_aleg_maximum_timer"].c_str());
    ret->sst_a_cfg.setParameter("session_refresh_method",r[0]["sst_aleg_session_refresh_method"].c_str());
    ret->sst_a_cfg.setParameter("accept_501_reply",r[0]["sst_aleg_accept_501_reply"].c_str());
    
  }
  
  #undef CP_SST_CFGVAR
  ret->to=r[0]["to"].c_str();

  vector<string> reply_translations_v = explode(r[0]["reply_translations"].c_str(), "|");
  for (vector<string>::iterator it = reply_translations_v.begin(); it != reply_translations_v.end(); it++) {
    // expected: "603=>488 Not acceptable here"
    vector<string> trans_components = explode(*it, "=>");
    if (trans_components.size() != 2) {
      ERROR("%s: entry '%s' in reply_translations could not be understood.\n",name.c_str(), it->c_str());
      ERROR("expected 'from_code=>to_code reason'\n");
    //  return false;
    }
    unsigned int from_code, to_code;
    if (str2i(trans_components[0], from_code)) {
      ERROR("%s: code '%s' in reply_translations not understood.\n", name.c_str(), trans_components[0].c_str());
    //  return false;
    }
    unsigned int s_pos = 0;
    string to_reply = trans_components[1];
    while (s_pos < to_reply.length() && to_reply[s_pos] != ' ')
      s_pos++;
    if (str2i(to_reply.substr(0, s_pos), to_code)) {
      ERROR("%s: code '%s' in reply_translations not understood.\n", name.c_str(), to_reply.substr(0, s_pos).c_str());
 //     return false;
    }
    if (s_pos < to_reply.length())
      s_pos++;
    // DBG("got translation %u => %u %s\n",
    // 	from_code, to_code, to_reply.substr(s_pos).c_str());
    ret->reply_translations[from_code] = make_pair(to_code, to_reply.substr(s_pos));
  }
  
  
  ret->transcoder.audio_codecs_str=r[0]["transcoder_codecs"].c_str();
  ret->transcoder.callee_codec_capabilities_str=r[0]["callee_codecapps"].c_str();
  ret->transcoder.transcoder_mode_str=r[0]["transcoder_mode"].c_str();
  ret->transcoder.dtmf_mode_str=r[0]["dtmf_mode"].c_str();
  ret->transcoder.lowfi_codecs_str=r[0]["lowfi_codecs"].c_str();
  ret->transcoder.audio_codecs_norelay_str=r[0]["prefer_transcoding_for_codecs"].c_str();
  ret->transcoder.audio_codecs_norelay_aleg_str=r[0]["prefer_transcoding_for_codecs_aleg"].c_str();

  ret->log_sip = r[0]["log_sip"].as<bool>();
  ret->log_rtp = r[0]["log_rtp"].as<bool>();
  ret->msg_logger_path = r[0]["msg_logger_path"].c_str();

  DBG("sql profile dump: \r\n %s \r\n",ret->print().c_str());
  DBG("sql profile codec_prefs dump: \r\n %s \r\n",ret->codec_prefs.print().c_str());
  DBG("sql profile transcoder dump: \r\n %s \r\n",ret->transcoder.print().c_str());

  ret->time_limit=r[0]["time_limit"].as<int>();
  ret->resources = r[0]["resources"].c_str();

  if(cache_enabled){
	int cache_time = r[0]["cache_time"].as<int>(0);
    if(cache_time > 0){
      DBG("SqlRouter: entry lifetime is %d seconds",cache_time);
      gettimeofday(&ret->expire_time,NULL);
      ret->expire_time.tv_sec+=cache_time;
    } else {
      timerclear(&ret->expire_time);
    }
  }

  ret->cached = false;
  
  return ret;
}


void SqlRouter::align_cdr(Cdr &cdr){
    DynFieldsT_iterator it = dyn_fields.begin();
    for(;it!=dyn_fields.end();++it){
        cdr.dyn_fields.push_back("0");
    }
}

void SqlRouter::write_cdr(Cdr* cdr)
{
  DBG("%s(%p)",FUNC_NAME,cdr);
  if(!cdr->writed){
    DBG("%s(%p) write now",FUNC_NAME,cdr);
    cdr->update(Write);
    cdr->inc();
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

void SqlRouter::getConfig(AmArg &arg){
	AmArg a;
	if(cdr_writer){
		cdr_writer->getConfig(a);
	}
	arg.push("cdrwriter",a);
}

void SqlRouter::getStats(AmArg &arg){
  AmArg underlying_stats;
      /* SqlRouter stats */
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
    underlying_stats["entries"] = (int)cache->get_count();
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
