#include "alarms.h"
#include "log.h"

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

alarm_entry::alarm_entry()
	: raised(0)
{
	timerclear(&change_time);
}

alarm_entry::~alarm_entry() { }

void alarm_entry::set(int value, bool silent) {
	AmLock l(_lock);
	gettimeofday(&change_time,NULL);
	//raised.set(value);
	raised = value;
	if(!silent) {
		if(value!=0){
			//ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised.get());
			ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised);
		} else {
			//ERROR("ALARM %d [%s] cleared. current: %d",id,name.c_str(),raised.get());
			ERROR("ALARM %d [%s] cleared. current: %d",id,name.c_str(),raised);
		}
	}
}

void alarm_entry::raise() {
	AmLock l(_lock);
	gettimeofday(&change_time,NULL);
	//raised.inc();
	raised++;
	//ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised.get());
	ERROR("ALARM %d [%s] raised. current: %d",id,name.c_str(),raised);
}

void alarm_entry::clear() {
	AmLock l(_lock);
	gettimeofday(&change_time,NULL);
	//if(raised.dec_and_test()){
	raised--;
	if(raised < 0) raised = 0; //avoid negative values

	if(!raised){
		INFO("ALARM %d [%s] cleared",id,name.c_str());
	}
	//INFO("ALARM %d [%s] changed. current: %d",id,name.c_str(),raised.get());
}

bool alarm_entry::is_raised() {
	AmLock l(_lock);
	//return (raised.get()!=0);
	return (raised!=0);
}

int alarm_entry::value() {
	AmLock l(_lock);
	//return raised.get();
	return raised;
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

void alarm_entry::getInfo(AmArg &r){
	r["id"] = id;
	r["name"] = name;
	r["state"] = is_raised();
	r["value"] = value();
	r["changed_at"] = !timerisset(&change_time) ?
				"nil" : timeval2str(change_time);
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

