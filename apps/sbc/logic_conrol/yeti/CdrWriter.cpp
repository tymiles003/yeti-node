#include "CdrWriter.h"
#include "log.h"
#include "AmThread.h"
#include <pqxx/pqxx>

CdrWriter::CdrWriter()
{

}

CdrWriter::~CdrWriter()
{

}

int CdrWriter::configure(CdrWriterCfg& cfg)
{
  config=cfg;
  return 0;
}


void CdrWriter::start()
{
  cdrthreadpool_mut.lock();
  DBG("CdrWriter::start: Starting %d async DB threads",config.poolsize);
  for(unsigned int i=0;i<config.poolsize;i++){
    CdrThread* th = new CdrThread;
    th->configure(config);
    th->start();
    cdrthreadpool.push_back(th);
  }
  cdrthreadpool_mut.unlock();
}

void CdrWriter::stop()
{
  DBG("CdrWriter::stop: Begin shutdown cycle");
  cdrthreadpool_mut.lock();
  int len=cdrthreadpool.size();
  for(int i=0;i<len;i++){
    DBG("CdrWriter::stop: Try shutdown thread %d",i);
    CdrThread* th= cdrthreadpool.back();
    cdrthreadpool.pop_back();
    th->stop();
    delete th;
  }
  len=cdrthreadpool.size();
  DBG("CdrWriter::stop: LEN:: %d", len);
  cdrthreadpool_mut.unlock();
}

void CdrWriter::postcdr(Cdr* cdr )
{
  cdrthreadpool_mut.lock();	
  	cdrthreadpool[cdr->cdr_born_time.tv_usec%cdrthreadpool.size()]->postcdr(cdr); 
  cdrthreadpool_mut.unlock();
}

void CdrWriter::getConfig(AmArg &arg){
	AmArg param;
	AmArg params;
	AmArg query;
	
	DynFieldsT_iterator dit = config.dyn_fields.begin();
	int param_num = 1;
	
	//static params
	for(;param_num<WRITECDR_STATIC_FIELDS_COUNT;param_num++){
		param.push(param_num);
		param.push(dit->second);
		params.push(param);
		param.clear();
	}
	//dynamic params
	for(;dit!=config.dyn_fields.end();++dit){
		param.push(param_num++);
		param.push(dit->first);
		param.push(dit->second);
		params.push(param);
		param.clear();
	}
		
	query["name"] = config.prepared_queries.begin()->first;
	query["query"] = config.prepared_queries.begin()->second.first;
	query["params"] = params;

	arg.push("queries",query);

}

void CdrWriter::getStats(AmArg &arg){
  AmArg underlying_stats,threads;
  
  arg["name"] = config.name;
  arg["poolsize"]= (int)config.poolsize;
  cdrthreadpool_mut.lock();
    for(vector<CdrThread*>::iterator it = cdrthreadpool.begin();it != cdrthreadpool.end();it++){
      (*it)->getStats(underlying_stats);
      threads.push(underlying_stats);
      underlying_stats.clear();
    }
  cdrthreadpool_mut.unlock();
  arg.push("threads",threads);
}

void CdrWriter::clearStats(){
  cdrthreadpool_mut.lock();
      for(vector<CdrThread*>::iterator it = cdrthreadpool.begin();it != cdrthreadpool.end();it++)
	(*it)->clearStats();
  cdrthreadpool_mut.unlock();
}

void CdrThread::postcdr(Cdr* cdr)
{
    //Cdr *newcdr = new Cdr(*cdr);
    
    queue_mut.lock();
    //queue.push_back(newcdr);
    queue.push_back(cdr);
        queue_run.set(true);
    queue_mut.unlock();
}


CdrThread::CdrThread() : queue_run(false),stopped(false),masterconn(NULL),slaveconn(NULL),gotostop(false)
{
  clearStats();
}

CdrThread::~CdrThread()
{

}

void CdrThread::getStats(AmArg &arg){
  queue_mut.lock();
    arg["queue_len"] = (int)queue.size();
  queue_mut.unlock();
  
  arg["stopped"] = stopped.get();
  arg["queue_run"] = queue_run.get();
  arg["db_exceptions"] = stats.db_exceptions;
  arg["writed_cdrs"] = stats.writed_cdrs;
  arg["tried_cdrs"] = stats.tried_cdrs;
}

void CdrThread::clearStats(){
  stats.db_exceptions = 0;
  stats.writed_cdrs = 0;
  stats.tried_cdrs = 0;
}

int CdrThread::configure(CdrThreadCfg& cfg )
{
  config=cfg;
  queue_run.set(false);
  return 0;
}

void CdrThread::on_stop()
{
  INFO("Stopping CdrWriter thread");
  gotostop=true;
  queue_run.set(true); // we must switch thread to run state for exit.
  stopped.wait_for();
  
  int pid=masterconn->backendpid();
  DBG("CdrWriter: Disconnect master SQL. Backend pid: %d.",pid);
  masterconn->disconnect();
  if(slaveconn){
    pid=slaveconn->backendpid();
    DBG("CdrWriter: Disconnect slave SQL. Backend pid: %d.",pid);
    slaveconn->disconnect();
  }
}

void CdrThread::run()
{
  INFO("Starting CdrWriter thread");
  DBG("TEST: %p",&queue_run);
  connectdb();
  while(true){
    queue_run.wait_for();
    if (gotostop){
      stopped.set(true);
      return;
    }
    DBG("CdrWriter cycle startup");
    Cdr* cdr;
    queue_mut.lock();
    cdr=queue.front();
    queue.pop_front();
    //queue.pop_back();
    if(0==queue.size()){
      // stop writer if no data;
      queue_run.set(false);
      DBG("CdrWriter cycle stop.Empty queue");
    }
    queue_mut.unlock();
    if(!cdr->SQLexception){
      if(0!=writecdr(masterconn,cdr)){
	ERROR("Cant write CDR to master database. Try slave");
	if (config.failover_to_slave&&slaveconn)
	  if(0!=writecdr(slaveconn,cdr)){
	    ERROR("Cant write CDR to slave database");
	    if(config.failover_to_file)
	      if(0!=writecdrtofile(cdr)){
		ERROR("WTF? Cant write CDR to file too.");
	      }
	  }
      }
    }
    if(cdr->dec_and_test()) //check for references
        delete cdr;
    DBG("CDR deleted from queue");
  }
}

void CdrThread::prepare_queries(pqxx::connection *c){
  PreparedQueriesT_iterator it = config.prepared_queries.begin();
  DynFieldsT_iterator dit;
  for(;it!=config.prepared_queries.end();++it){
    pqxx::prepare::declaration d = c->prepare(it->first,it->second.first);
    //static fields
    for(int i = 0;i<WRITECDR_STATIC_FIELDS_COUNT;i++){
      d("varchar",pqxx::prepare::treat_direct);
    }
    //dynamic fields	
    dit = config.dyn_fields.begin();
    for(;dit!=config.dyn_fields.end();++dit){
      d(dit->second,pqxx::prepare::treat_direct);
    }
    c->prepare_now(it->first);
  }
}

int CdrThread::_connectdb(pqxx::connection **conn,string conn_str){
  pqxx::connection *c = NULL;
  int ret = 0;
  try{
    c = new pqxx::connection(conn_str);
    if (c->is_open()){
	  prepare_queries(c);
	  DBG("CdrWriter: SQL connected. Backend pid: %d.",c->backendpid());
    }
    *conn = c;
    ret = 1;
  } catch(const pqxx::broken_connection &e){
    DBG("CdrWriter: SQL connection exception: %s",e.what());
  } catch(const pqxx::undefined_function &e){
    ERROR("CdrWriter: SQL connection: undefined_function query: %s, what: %s",e.query().c_str(),e.what());
    c->disconnect();
//     delete c;
    throw std::runtime_error("CdrThread exception");
  }
  return ret;
}

int CdrThread::connectdb()
{
  int ret = _connectdb(&masterconn,config.masterdb.conn_str());
  if(config.failover_to_slave){
	  ret|=_connectdb(&slaveconn,config.slavedb.conn_str());
  }
  return ret;
}

int CdrThread::writecdr(pqxx::connection* conn, Cdr* cdr)
{
  int ret = 1;
  pqxx::result r;
  pqxx::nontransaction tnx(*conn);
  stats.tried_cdrs++;
  try{
    if(tnx.prepared("writecdr").exists()){
	/*DBG("cdr->start_data.time_limit '%d'",cdr->start_data.time_limit);
	DBG("cdr->term_ip '%s'",cdr->term_ip.c_str());
	DBG("cdr->term_port '%d'",cdr->term_port);
	DBG("cdr->term_local_ip '%s'",cdr->term_local_ip.c_str());
	DBG("cdr->start_data.local_port '%d'",cdr->start_data.local_port);
	DBG("cdr->start_time '%d'",cdr->start_time.tv_sec);
	DBG("cdr->connect_time '%d'",cdr->connect_time.tv_sec);
	DBG("cdr->end_time '%d'",cdr->end_time.tv_sec);
	DBG("cdr->disconnect_code '%d'",cdr->disconnect_code);
	DBG("cdr->disconnect_reason '%s'",cdr->disconnect_reason.c_str());
	DBG("cdr->disconnect_initiator '%d'",cdr->disconnect_initiator);
	DBG("cdr->orig_call_id '%s'",cdr->orig_call_id.c_str());
	DBG("cdr->term_call_id '%s'",cdr->term_call_id.c_str());
	DBG("cdr->local_tag '%s'",cdr->local_tag.c_str());*/
	    
      pqxx::prepare::invocation invoc = tnx.prepared("writecdr");
      /* invocate static fields */
    invoc(cdr->time_limit);
    invoc(cdr->legA_local_ip);
    invoc(cdr->legA_local_port);
    invoc(cdr->legA_remote_ip);
    invoc(cdr->legA_remote_port);
    invoc(cdr->legB_local_ip);
    invoc(cdr->legB_local_port);
    invoc(cdr->legB_remote_ip);
    invoc(cdr->legB_remote_port);
/*
	invoc(cdr->term_ip);
	invoc(cdr->term_port);
	invoc(cdr->term_local_ip);
    invoc(cdr->local_port);
*/
	invoc(cdr->start_time.tv_sec);
	invoc(cdr->connect_time.tv_sec);
	invoc(cdr->end_time.tv_sec);
	invoc(cdr->disconnect_code);
	invoc(cdr->disconnect_reason);
	invoc(cdr->disconnect_initiator);
	invoc(cdr->orig_call_id);
	invoc(cdr->term_call_id);
	invoc(cdr->local_tag);
      /* invocate dynamic fields  */
// 	DBG("dyn_fields.size() = %ld",cdr->start_data.dyn_fields.size());
    list<string>::iterator it = cdr->dyn_fields.begin();
    for(;it!=cdr->dyn_fields.end();++it){
// 		DBG("dyn: '%s'",(*it).c_str());
		invoc((*it));
	}
      r = invoc.exec();
    } else {
      WARN("CdrThread: Dynamic fields not used without prepared queries for perfomance reasons");
      r=tnx.exec("SELECT switch.writecdr("
    +tnx.quote(cdr->time_limit)+","
    +tnx.quote(cdr->legA_local_ip)+","
    +tnx.quote(cdr->legA_local_port)+","
    +tnx.quote(cdr->legA_remote_ip)+","
    +tnx.quote(cdr->legA_remote_port)+","
    +tnx.quote(cdr->legB_local_ip)+","
    +tnx.quote(cdr->legB_local_port)+","
    +tnx.quote(cdr->legB_remote_ip)+","
    +tnx.quote(cdr->legB_remote_port)+","
	+tnx.quote(cdr->start_time.tv_sec)+","
	+tnx.quote(cdr->connect_time.tv_sec)+","
	+tnx.quote(cdr->end_time.tv_sec)+","
	+tnx.quote(cdr->disconnect_code)+","
	+tnx.quote(cdr->disconnect_reason)+","
	+tnx.quote(cdr->disconnect_initiator)+","
	+tnx.quote(cdr->orig_call_id)+","
	+tnx.quote(cdr->term_call_id)+","
	+tnx.quote(cdr->local_tag)+")"
      );
    }
    if (r.size()!=0&&0==r[0][0].as<int>()){
      ret = 0;
      stats.writed_cdrs++;
    }
  } catch(const pqxx::pqxx_exception &e){
    DBG("SQL exception on CdrWriter thread: %s",e.base().what());
    stats.db_exceptions++;
  }
  return ret;
}

int CdrThread::writecdrtofile(Cdr* cdr)
{
  
  //TODO write to file
  return 0;
}


int CdrThreadCfg::cfg2CdrThCfg(AmConfigReader& cfg, string& prefix)
{
  string suffix;
  suffix="master"+prefix;
  masterdb.cfg2dbcfg(cfg,suffix);
  suffix="slave"+prefix;
  slavedb.cfg2dbcfg(cfg,suffix);
  return 0;
}

int CdrWriterCfg::cfg2CdrWrCfg(AmConfigReader& cfg)
{
  string var=name+"_pool_size";
  if (cfg.hasParameter(var)){
    poolsize=cfg.getParameterInt(var);
  } else{
    poolsize=10;
    WARN("Variable %s not found in config. Using: %d",var.c_str(),poolsize);
  }
  return cfg2CdrThCfg(cfg,name);
}
