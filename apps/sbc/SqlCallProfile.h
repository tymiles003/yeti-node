#ifndef SQLCALLPROFILE_H
#define SQLCALLPROFILE_H

#include "SBCCallProfile.h"
#include <pqxx/result>
#include <string>

using std::string;

struct SqlCallProfile
	: public SBCCallProfile
{
	int time_limit;
	bool SQLexception;
	bool cached;
	struct timeval expire_time;
	list<string> dyn_fields;
	string resources;

	SqlCallProfile();
	~SqlCallProfile();

	bool readFromTuple(const pqxx::result::tuple &t);
	bool readFilter(const pqxx::result::tuple &t, const char* cfg_key_filter, const char* cfg_key_list,
			vector<FilterEntry>& filter_list, bool keep_transparent_entry);
	bool readCodecPrefs(const pqxx::result::tuple &t);
	bool readTranscoder(const pqxx::result::tuple &t);
	bool column_exist(const pqxx::result::tuple &t,string column_name);

};

#endif // SQLCALLPROFILE_H
