#include "alarms.h"
#include "log.h"

///#include <cstring>

#include "AmUtils.h"

static const char *alarms_descr[] = {
	"management database connections error",	//MGMT_DB_RECONN
	"cdr database connections error",			//CDR_DB_CONN
	"redis read connection error",				//REDIS_READ_CONN
	"redis write connection error",				//REDIS_WRITE_CONN
};

static const char *alarms_descr_unknown = "unknown alarm";

static bool check_alarm_id(int alarm_id) {
	if(alarm_id >= _alarms::MAX_ALARMS || alarm_id < 0){
		ERROR("invalid alarm_id [%d]",alarm_id);
		return false;
	}
	return true;
}

const char *id2str(int alarm_id){
	if(!check_alarm_id(alarm_id)) return alarms_descr_unknown;
	return alarms_descr[alarm_id];
}

alarm_entry::alarm_entry() { }

alarm_entry::~alarm_entry() { }

void alarm_entry::set(int value, bool silent) {
	gettimeofday(&change_time,NULL);
	raised.set(value);
	if(!silent) {
		if(value!=0){
			ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised.get());
		} else {
			ERROR("ALARM %d [%s] cleared. current: %d",id,name.c_str(),raised.get());
		}
	}
}

void alarm_entry::raise() {
	gettimeofday(&change_time,NULL);
	raised.inc();
	ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised.get());
}

void alarm_entry::clear() {
	gettimeofday(&change_time,NULL);
	if(raised.dec_and_test()){
		INFO("ALARM %d [%s] cleared",id,name.c_str());
	}
	//INFO("ALARM %d [%s] changed. current: %d",id,name.c_str(),raised.get());
}

bool alarm_entry::is_raised() const {
	return (raised.get()!=0);
}

const std::string& alarm_entry::get_name() const {
	return name;
}

const timeval &alarm_entry::get_change_time() const {
	return change_time;
}

void alarm_entry::set_info(int alarm_id, std::string alarm_name) {
	id = alarm_id;
	name  = alarm_name;
}

_alarms::_alarms() {
	for(int id = 0;id < MAX_ALARMS;id++)
		get(id).set_info(id,id2str(id));
}

_alarms::~_alarms() { }


alarm_entry& _alarms::get(int alarm_id) {
	if(!check_alarm_id(alarm_id))
		throw InvalidAlarmIdException();
	return entries[alarm_id];
}

const char *_alarms::id2str(int alarm_id){
	if(!check_alarm_id(alarm_id)) return alarms_descr_unknown;
	return alarms_descr[alarm_id];
}

