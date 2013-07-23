#ifndef _SQLRouter_
#define _SQLRouter_

#include "SBCCallProfile.h"
#include "PgConnectionPool.h"
#include "AmUtils.h"
#include "HeaderFilter.h"
#include <algorithm>
#include "CdrWriter.h"
#include "ProfilesCache.h"
#include "DbTypes.h"
#include "Cdr.h"

using std::string;
using std::list;
using std::vector;

class SqlRouter {
public:
  SqlCallProfile *getprofile(const AmSipRequest&);
  int configure(AmConfigReader &cfg);
  int run();
  void stop();
  void align_cdr(Cdr &cdr);
  void write_cdr(Cdr *cdr);
  void dump_config();
  void clearStats();
  void getStats(AmArg &arg);
  void getConfig(AmArg &arg);

  SqlRouter();
  ~SqlRouter();

private:
  int db_configure(AmConfigReader &cfg);
  SqlCallProfile* _getprofile(const AmSipRequest&, pqxx::connection*);
  
  PgConnectionPool *master_pool;
  PgConnectionPool *slave_pool;
  CdrWriter *cdr_writer;
  ProfilesCache *cache;
  
  string used_header_fields_separator;
  vector<string> used_header_fields;
  int failover_to_slave;
  int cache_enabled;
  double cache_check_interval;
  PreparedQueriesT prepared_queries;
  PreparedQueriesT cdr_prepared_queries;
  DynFieldsT dyn_fields;
};

#endif
