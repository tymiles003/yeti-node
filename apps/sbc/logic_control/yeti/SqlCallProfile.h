#ifndef SQLCALLPROFILE_H
#define SQLCALLPROFILE_H

#include "SBCCallProfile.h"
#include <pqxx/result>
#include <string>

#include "resources/Resource.h"
#include "db/DbTypes.h"

#define LOG_SIP_MASK	0x1
#define LOG_RTP_MASK	0x2
#define LOG_FULL_MASK	(LOG_SIP_MASK|LOG_RTP_MASK)

#define REFRESH_METHOD_INVITE					1
#define REFRESH_METHOD_UPDATE					2
#define REFRESH_METHOD_UPDATE_FALLBACK_INVITE	3

#define FILTER_TYPE_TRANSPARENT					0
#define FILTER_TYPE_BLACKLIST					1
#define FILTER_TYPE_WHITELIST					2

using std::string;

struct SqlCallProfile
	: public SBCCallProfile
{
	int time_limit;
	int disconnect_code_id;
	int session_refresh_method_id;
	int aleg_session_refresh_method_id;
	int aleg_override_id,bleg_override_id;
	int dump_level_id;

	AmArg dyn_fields;

	string resources;
	ResourceList rl;

	SqlCallProfile();
	~SqlCallProfile();

	bool readFromTuple(const pqxx::result::tuple &t,const DynFieldsT &df);
	bool readFilter(const pqxx::result::tuple &t, const char* cfg_key_filter,
			vector<FilterEntry>& filter_list, bool keep_transparent_entry,
			int failover_type_id = FILTER_TYPE_TRANSPARENT);
	bool readCodecPrefs(const pqxx::result::tuple &t);
	bool readTranscoder(const pqxx::result::tuple &t);
	bool readDynFields(const pqxx::result::tuple &t,const DynFieldsT &df);
	bool column_exist(const pqxx::result::tuple &t,string column_name);
	bool eval_resources();
	bool eval();

	void infoPrint(const DynFieldsT &df);
	SqlCallProfile *copy();
};

#endif // SQLCALLPROFILE_H
