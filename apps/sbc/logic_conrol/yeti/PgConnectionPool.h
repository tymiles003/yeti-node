#ifndef _PgConnectionPool_h_
#define _PgConnectionPool_h_

#include "AmThread.h"
#include "AmArg.h"

#include <string>
#include <list>
#include <vector>
#include <pqxx/pqxx>
#include <boost/iterator/iterator_concepts.hpp>
#include "DbConfig.h"
#include <sys/time.h>
#include <unistd.h>
#include "DbTypes.h"

#define PGPOOL_VERSION v14

using std::string;
using std::list;
using std::vector;

class PgConnection: public pqxx::connection {
public:
  PgConnection(const PGSTD::string &opts);
  ~PgConnection();
  unsigned int exceptions;
  struct timeval access_time;
private:
  //
};

class PgConnectionPoolCfg{
public:
  DbConfig dbconfig;
  string name;
  unsigned int size;
  unsigned int max_exceptions;
  unsigned int check_interval;
  PreparedQueriesT prepared_queries;
  int cfg2PgCfg(AmConfigReader& cfg);
};

class PgConnectionPool
: public AmThread
{
  list<PgConnection*> connections;

  unsigned int total_connections;
  unsigned int failed_connections;
  AmMutex connections_mut;

  AmCondition<bool> have_active_connection;
  AmCondition<bool> try_connect;
  

  vector<unsigned int> retry_timers;
  unsigned int retry_index;

  string conn_host,conn_user,conn_dbname,conn_password;
  //string pool_name;
  unsigned int conn_port;
  unsigned int max_wait;
  unsigned int max_exceptions;
  unsigned int exceptions_count;
  unsigned int check_interval;
private:
  AmCondition<bool> stopped;
  bool gotostop;
  time_t mi_start;			//last measurement interval start time
  time_t mi;				//tps measurement interval
  unsigned int tpi;			//transactions per interval
  struct {
    int transactions_count;		//total succ transactions count
    int check_transactions_count;	//total succ check_transactions count
    double tt_min,tt_max;		//transactions time (duration)
    double tps_max,tps_avg;		//transactions per second
  } stats;
  
  PreparedQueriesT prepared_queries;
  void prepare_queries(PgConnection *c);
  
 public:
  PgConnectionPool();
  ~PgConnectionPool();
  enum conn_stat {
      CONN_SUCC,		/*! no errors */
      CONN_CHECK_SUCC,		/*! no errors during check */
      CONN_COMM_ERR,		/*! communication error */
      CONN_DB_EXCEPTION		/*! database exception */
  };
  PgConnection* getActiveConnection();
  void clearStats();
  void getStats(AmArg &arg);
  void returnConnection(PgConnection* c,conn_stat stat = CONN_SUCC);
  void set_config(PgConnectionPoolCfg& config);
  void dump_config();
  void add_connections(unsigned int count);
  void run();
  void on_stop();
  
  string pool_name;
};

#endif
