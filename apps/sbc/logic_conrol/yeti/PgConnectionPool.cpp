#include "PgConnectionPool.h"
#include "log.h"
#include <string>
#include "AmUtils.h"

#define PGPOOL_VERSION v14

PgConnection::PgConnection(const PGSTD::string &opts): pqxx::connection(opts), exceptions(0)
{
  timerclear(&access_time);
  DBG("PgConnection::PgConnection() this = [%p]\n",this);
}

PgConnection::~PgConnection(){
  DBG("PgConnection::~PgConnection() this = [%p]\n",this);
}

PgConnectionPool::PgConnectionPool() : total_connections(0), failed_connections(0),
    have_active_connection(false), try_connect(true), stopped(false)
{
  gotostop=false;
  clearStats();
  mi = 5;
}

PgConnectionPool::~PgConnectionPool() {
  DBG("PgCP thread stopping\n");
}


int PgConnectionPoolCfg::cfg2PgCfg(AmConfigReader& cfg)
{
  dbconfig.cfg2dbcfg(cfg,name);
  string var;
  var=name+"_pool_size";
  if(cfg.hasParameter(var)){
    size=cfg.getParameterInt(var);
  } else {
    size=10;
    WARN("Variable %s not found in config. Using default value: %d",var.c_str(),size);
  }
  var=name+"_max_exceptions";
  if(cfg.hasParameter(var)){
    max_exceptions=cfg.getParameterInt(var);
  } else {
    max_exceptions=0;
    WARN("Variable %s not found in config. Using default value: %d",var.c_str(),max_exceptions);
  }
  var=name+"_check_interval";
  if(cfg.hasParameter(var)){
    check_interval=cfg.getParameterInt(var);
  } else {
    check_interval=1800;
    WARN("Variable %s not found in config. Using default value: %d",var.c_str(),check_interval);
  }
  return 0;
}


void PgConnectionPool::dump_config()
{
  string conn_str= "host="+conn_host+" port="+int2str(conn_port)+" user="+conn_user+" dbname="+conn_dbname+" password="+conn_password;
  INFO("PgCP: Pool %s CONFIG:",pool_name.c_str());
  INFO("PgCP:     remote db socket: %s:%s",conn_host.c_str(),int2str(conn_port).c_str());
  INFO("PgCP:     dbname: %s",conn_dbname.c_str());
  INFO("PgCP:     user/password: %s/%s",conn_user.c_str(),conn_password.c_str());
  INFO("PgCP:     max_exceptions: %s",int2str(max_exceptions).c_str());
  INFO("PgCP:     check_interval: %s",int2str(check_interval).c_str());
  INFO("PgCP: Pool RUNTIME:");
  connections_mut.lock();
  INFO("PgCP:     Connections total/failed: %s/%s",int2str(total_connections).c_str(),int2str(failed_connections).c_str());
  connections_mut.unlock();
  INFO("PgCP:     exceptions_count: %s",int2str(exceptions_count).c_str());
}
void PgConnectionPool::set_config(PgConnectionPoolCfg& cfg){
  check_interval=cfg.check_interval;
  conn_host=cfg.dbconfig.host;
  conn_port=cfg.dbconfig.port;
  conn_user=cfg.dbconfig.user;
  conn_dbname=cfg.dbconfig.name;
  conn_password=cfg.dbconfig.pass;
  max_exceptions=cfg.max_exceptions;
  exceptions_count=0;
  pool_name=cfg.name;
  prepared_queries = cfg.prepared_queries;
  DBG("%s: PgConnectionPool configured",pool_name.c_str());
}

void PgConnectionPool::add_connections(unsigned int count) {
  connections_mut.lock();
  failed_connections += count;
  total_connections += count;
  connections_mut.unlock();
  try_connect.set(true);
}

void PgConnectionPool::returnConnection(PgConnection* c,conn_stat stat) {
  std::map<PgConnection*,struct timeval *>::iterator tsti;
  bool return_connection = false,check = false;
  struct timeval now,ttdiff;
  double tt_curr;
  
  gettimeofday(&now,NULL);
  timerclear(&ttdiff);

  switch(stat){
    case CONN_SUCC: {
      return_connection = true;
    } break;
    case CONN_CHECK_SUCC: {
      return_connection = true;
      check = true;
    } break;
    case CONN_DB_EXCEPTION: {
      c->exceptions++;
      if(c->exceptions > max_exceptions)
	return_connection = true;
    } break;
    case CONN_COMM_ERR:
    default: {
      return_connection = false;
    };
  }
    
  if(return_connection){
    connections_mut.lock();
      connections.push_back(c);
      size_t active_size = connections.size();
      if(timerisset(&c->access_time)){
	//returnConnection() called after getActiveConnection()
	if(check)
	  stats.check_transactions_count++;
	else {
	  stats.transactions_count++;
	  timersub(&now,&c->access_time,&ttdiff);
	}
      } else {
	//returnConnection() called without previous getActiveConnection()
	timerclear(&c->access_time);
      }
    connections_mut.unlock();
    
    DBG("%s: Now %zd active connections\n",pool_name.c_str(),active_size);
  } else {
    //c->disconnect();
    delete c;
    connections_mut.lock();
      failed_connections++;
      unsigned int inactive_size = failed_connections;
      retry_index = 0;
    connections_mut.lock();
    try_connect.set(true);
    
    DBG("%s: Now %u inactive connections\n",pool_name.c_str(), inactive_size);
  } 
  
  have_active_connection.set(true);
  
  if(timerisset(&ttdiff)){
      tt_curr = ttdiff.tv_sec+ttdiff.tv_usec/(double)1e6;
      if(tt_curr > stats.tt_max)
	stats.tt_max = tt_curr;
      if(stats.tt_min){
	if(tt_curr < stats.tt_min)
	  stats.tt_min = tt_curr;
      } else {
	stats.tt_min = tt_curr;
      }
  }
  
}

PgConnection* PgConnectionPool::getActiveConnection() {  
  PgConnection* res = NULL;
  time_t now;
  double diff,tps;
  int intervals;

  while (NULL == res) {

    connections_mut.lock();
    if (connections.size()) {
      res = connections.front();
      connections.pop_front();
      have_active_connection.set(!connections.empty());
    }
    connections_mut.unlock();

    if (NULL == res) {
      // check if all connections broken -> return null
      connections_mut.lock();
      bool all_inactive = total_connections == failed_connections;
      connections_mut.unlock();

      if (all_inactive) {
        DBG("%s: all connections inactive - returning NO connection\n",pool_name.c_str());
        return NULL;
      }

      // wait until a connection is back
      DBG("%s: waiting for an active connection to return\n",pool_name.c_str());
      if (!have_active_connection.wait_for_to(max_wait)) {
        WARN("%s: timeout waiting for an active connection (waited %ums)\n",pool_name.c_str(), max_wait);
        break;
      }
    } else {
	/*	memorise connection get time	*/
      gettimeofday(&res->access_time,NULL);
	/*	compute tps	*/
      now = res->access_time.tv_sec;
      diff = difftime(now,mi_start);
      intervals = diff/mi;
      if(intervals > 0){
	//now is first point in current measurement interval
	mi_start = now;
	tps = tpi/(double)mi;
	stats.tps_avg = tps;
	if(tps > stats.tps_max)
	  stats.tps_max = tps;	
	tpi = 1;
      } else {
	//now is another point in current measurement interval
	tpi++;
      }
      DBG("%s: got active connection [%p]\n",pool_name.c_str(), res);
    }
  }

  return res;
}


void PgConnectionPool::run() {
  PgConnection* co=NULL;
  list<PgConnection*>::iterator it,start,stop;
  DBG("PgCP %s thread starting\n",pool_name.c_str());
  try_connect.set(true);
  while (true) {
    if(gotostop) {
      stopped.set(true);
      return;
    }
    DBG("PgCP: %s: Check cycle started.",pool_name.c_str());
    try_connect.wait_for_to(check_interval);
    //try_connect.wait_for();
    if (try_connect.get()){
      while (true) {
	if(gotostop) {
	  stopped.set(true);
	  return;
	}
	connections_mut.lock();
	unsigned int m_failed_connections = failed_connections;
	connections_mut.unlock();

	if (!m_failed_connections)
	  break;
	// add connections until error occurs
	DBG("PgCP: %s: start connection",pool_name.c_str());
	string conn_str= "host="+conn_host+" port="+int2str(conn_port)+" user="+conn_user+" dbname="+conn_dbname+" password="+conn_password;
	//DBG("PgCP v13: Connection string %s",conn_str.c_str());
	try {
	  PgConnection* conn = new PgConnection(conn_str);
	  if (conn->is_open()){
	    prepare_queries(conn);
	    DBG("PgCP: %s: SQL connected. Backend pid: %d.",pool_name.c_str(),conn->backendpid());
	    returnConnection(conn);
	    connections_mut.lock();
	    failed_connections--;
	    connections_mut.unlock();
	  } else {
          // WTF?
	    DBG("%s: waiting for retry 500 ms\n",pool_name.c_str());
	    usleep(500);
	  }
	} catch(const pqxx::broken_connection &exc) {
	  DBG("PgCP: %s: connection exception: %s",pool_name.c_str(),exc.what());
	  exceptions_count++;
	  if ((max_exceptions>0)&&(exceptions_count>max_exceptions)) {
	    DBG("PgCP: %s: max exception count reached. Pool stopped.",pool_name.c_str());
	    try_connect.set(false);
	    break;
	  }
	  usleep(1000000);
	}
	if (0==failed_connections){
	  WARN("PCP: %s: All sql connected.",pool_name.c_str());
	  try_connect.set(false);
	}
	DBG("PgCP: %s: Wait 10ms for next attempt.",pool_name.c_str());
	usleep(10000);
      }
    } else {
      // connection checking
      for(int i=0;i<10;i++){
 	bool fail=false;
	co=getActiveConnection();
	if(NULL==co){
	  ERROR("PCP: %s: checker: FAIL: no active connections.",pool_name.c_str());
	  break;
	}
	  try {
	    pqxx::work tnx(*co);
	    tnx.commit();
	  } catch (const pqxx::pqxx_exception &e) {
	    WARN("PgCP: %s: Connection [%p] check failed. Destroy connection. Reason: %s ",pool_name.c_str(),co,e.base().what());
	    delete co;
	    fail=true;
	    connections_mut.lock();
	    failed_connections++;
	    connections_mut.unlock();
	    try_connect.set(true);
//	    break;
	  }
	  if (!fail){
	    DBG("PgCP: %s: checker: OK",pool_name.c_str());
	    returnConnection(co,CONN_CHECK_SUCC);
	  }
      }
    }
  }
}

void PgConnectionPool::on_stop() {
  PgConnection* res;
  int len,pid;
  DBG("PgCP %s thread stopping\n",pool_name.c_str());
  try_connect.set(false);
  gotostop=true;
  try_connect.set(true);
  
  stopped.wait_for();
  
  len=connections.size();
  for (int i=0;i<len;i++){
      connections_mut.lock();
      res = connections.front();
      connections.pop_front();
      connections_mut.unlock();
      pid=res->backendpid();
      DBG("PgCP: %s: Disconnect SQL. Backend pid: %d.",pool_name.c_str(),pid);
      res->disconnect();
  }
  DBG("PgCP %s All disconnected",pool_name.c_str());
}

void PgConnectionPool::clearStats(){
  list<PgConnection*>::iterator it;	
  
  time(&mi_start);
  tpi = 0;
  stats.transactions_count = 0;
  stats.check_transactions_count = 0;
  stats.tt_min = 0;
  stats.tt_max = 0;
  stats.tps_max = 0;
  stats.tps_avg = 0;
  for(it = connections.begin();it!=connections.end();it++){
	(*it)->exceptions = 0;
  }
}

void PgConnectionPool::getStats(AmArg &arg){
  list<PgConnection*>::iterator it;
  AmArg conn,conns;
  
  connections_mut.lock();
    arg["total_connections"] = (int)total_connections;
    arg["failed_connections"] = (int)failed_connections;
    arg["transactions"] = stats.transactions_count;
    arg["check_transactions"] = stats.check_transactions_count;
    arg["tt_min"] = stats.tt_min;
    arg["tt_max"] = stats.tt_max;
    arg["tps_max"] = stats.tps_max;
    arg["tps_avg"] = stats.tps_avg;
    
    for(it = connections.begin();it!=connections.end();it++){
      conn["exceptions"] = (int)(*it)->exceptions;
      conns.push(conn);
      conn.clear();
    }
    arg.push("connections",conns);
  connections_mut.unlock();
}

void PgConnectionPool::prepare_queries(PgConnection *c){
  PreparedQueriesT::iterator it = prepared_queries.begin();
  for(;it!=prepared_queries.end();++it){
    pqxx::prepare::declaration d = c->prepare(it->first,it->second.first);
    for(int i = 0;i<it->second.second;i++){
      d("varchar",pqxx::prepare::treat_direct);
    }
    c->prepare_now(it->first);
  }
}
