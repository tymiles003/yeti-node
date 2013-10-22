#ifndef REGISTRATION_H
#define REGISTRATION_H

#include "AmSession.h"
#include "AmConfigReader.h"
#include "DbConfig.h"

class Registration : public AmThread {
	static Registration* _instance;
	AmCondition<bool> stopped;
	DbConfig dbc;
	string db_schema;
	AmMutex cfg_mutex;
	int check_interval;

	struct RegInfo {
		int id;
		string domain;
		string user;
		string display_name;
		string auth_user;
		string passwd;
		string proxy;
		string contact;

		string handle;
		int expires;
		int state;
	};

	void reg2arg(const RegInfo &reg,AmArg &arg);
	vector<RegInfo> registrations;

	void create_registration(RegInfo& ri);
	bool check_registration(RegInfo& ri);
	void remove_registration(RegInfo& ri);
	void clean_registrations();

protected:
	void run();
	void on_stop();

public:
	Registration();
	~Registration();
	static Registration* instance();

	void configure_db(AmConfigReader &cfg);
	int load_registrations();
	int configure(AmConfigReader &cfg);
	int reload(AmConfigReader &cfg);
	void list_registrations(AmArg &ret);
};

#endif // REGISTRATION_H
