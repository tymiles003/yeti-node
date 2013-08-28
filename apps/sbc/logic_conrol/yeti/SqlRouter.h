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
#include "CallCtx.h"
struct CallCtx;

using std::string;
using std::list;
using std::vector;

struct GetProfileException {
	bool fatal;			//if true we should reload pg connection
	string what;
	GetProfileException(string w, bool h): what(w), fatal(h) {}
};

class SqlRouter: public atomic_int {
public:
  void getprofiles(const AmSipRequest&,CallCtx &ctx);
  int configure(AmConfigReader &cfg);
  int run();
  void stop();
  void release(std::set<SqlRouter *> &routers);
  void align_cdr(Cdr &cdr);
  void write_cdr(Cdr *cdr, bool last);
  void dump_config();
  void clearStats();
  void getStats(AmArg &arg);
  void getConfig(AmArg &arg);

  const DynFieldsT &getDynFields() const { return dyn_fields; }

  SqlRouter();
  ~SqlRouter();

private:
  //stats
  int cache_hits,db_hits,hits;
  double gt_min,gt_max;
  double gps_max,gps_avg;
  time_t mi_start;
  time_t mi;
  unsigned int gpi;

  int db_configure(AmConfigReader &cfg);
  ProfilesCacheEntry* _getprofiles(const AmSipRequest&,
							   pqxx::connection*);
  void update_counters(struct timeval &start_time);

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
