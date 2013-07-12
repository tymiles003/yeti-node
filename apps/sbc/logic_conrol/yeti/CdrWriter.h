#ifndef _CdrWriter_h_
#define _CdrWriter_h_

#include "AmThread.h"

#include <string>
#include <list>
#include <vector>
#include <pqxx/pqxx>
#include "Cdr.h"
#include "SBCCallProfile.h"
#include "DbConfig.h"
#include "Cdr.h"
#include "DbTypes.h"

using std::string;
using std::list;
using std::vector;

class CdrThreadCfg{
public:
  bool failover_to_slave;
  bool failover_to_file;
  string failover_file_dir;
  DbConfig masterdb,slavedb;
  PreparedQueriesT prepared_queries;
  DynFieldsT dyn_fields;
  int cfg2CdrThCfg(AmConfigReader& cfg,string& prefix);
};

class CdrWriterCfg :public CdrThreadCfg{
public:
  unsigned int poolsize;
  string name;
  int cfg2CdrWrCfg(AmConfigReader& cfg);
};



class CdrThread : public AmThread{
private:
  list<Cdr*> queue;
  AmMutex queue_mut;
  AmCondition<bool> queue_run;
  AmCondition<bool> stopped;
  pqxx::connection *masterconn,*slaveconn;
  CdrThreadCfg config;
  int _connectdb(pqxx::connection **conn,string conn_str);
  int connectdb();
  void prepare_queries(pqxx::connection *c);
  int writecdr(pqxx::connection* conn,Cdr* cdr);
  int writecdrtofile(Cdr* cdr);
  bool gotostop;
  struct {
    int db_exceptions;
    int writed_cdrs;
    int tried_cdrs;
  } stats;
 public:
   CdrThread();
   ~CdrThread();
  void clearStats();
  void getStats(AmArg &arg);
  void postcdr(Cdr* cdr);
  int configure(CdrThreadCfg& cfg);
  void run();
  void on_stop();
};

class CdrWriter{
private:
  vector<CdrThread*> cdrthreadpool;
  AmMutex cdrthreadpool_mut;
  CdrWriterCfg config;
public:
  void clearStats();
  void getStats(AmArg &arg);
  void getConfig(AmArg &arg);
  void postcdr(Cdr* cdr);
  int configure(CdrWriterCfg& cfg);
  void start();
  void stop();
  CdrWriter();
  ~CdrWriter();
};

#endif
