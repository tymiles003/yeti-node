#include "Registration.h"
#include "AmSipRegistration.h"
#include "ampi/SIPRegistrarClientAPI.h"
#include <pqxx/pqxx>
#include "yeti.h"

#define CHECK_INTERVAL_DEFAULT 5000

Registration* Registration::_instance=0;

Registration* Registration::instance(){
	if(!_instance)
		_instance = new Registration();
	return _instance;
}

void Registration::reg2arg(const RegInfo &reg,AmArg &arg){
	arg["id"] = reg.id;
	arg["domain"] = reg.domain;
	arg["user"] = reg.user;
	arg["display_name"] = reg.display_name;
	arg["auth_user"] = reg.auth_user;
	arg["passwd"] = reg.passwd;
	arg["proxy "] = reg.proxy;
	arg["contact"] = reg.contact;
	if(!reg.handle.empty()){
		if(reg.state==AmSIPRegistration::RegisterActive)
			arg["expires"] = reg.expires;
		arg["state"] = getSIPRegistationStateString(reg.state);
	} else {
		arg["state"] = "NotProcesesed";
	}
}

Registration::Registration() { }

Registration::~Registration(){ }

void Registration::configure_db(AmConfigReader &cfg){
	string prefix("master");
	dbc.cfg2dbcfg(cfg,prefix);
}

int Registration::load_registrations(){
	int ret = 1;

	try {
		pqxx::result r;
		pqxx::connection c(dbc.conn_str());
		pqxx::work t(c);
		vector<RegInfo> new_registrations;

		Yeti::global_config &gc = Yeti::instance()->config;
		c.prepare("load_reg","SELECT * from "+db_schema+".load_registrations_out($1,$2)")("integer")("integer");
		r = t.prepared("load_reg")(gc.pop_id)(gc.node_id).exec();
		for(pqxx::result::size_type i = 0; i < r.size();++i){
			RegInfo ri;
			const pqxx::result::tuple &row = r[i];
			ri.id = row["o_id"].as<int>();
			ri.domain = row["o_domain"].c_str();
			ri.user = row["o_user"].c_str();
			ri.display_name = row["o_display_name"].c_str();
			ri.auth_user = row["o_auth_user"].c_str();
			ri.passwd = row["o_auth_password"].c_str();
			ri.proxy = row["o_proxy"].c_str();
			ri.contact = row["o_contact"].c_str();
			new_registrations.push_back(ri);
		}

		t.commit();
		c.disconnect();

		cfg_mutex.lock();
			registrations.swap(new_registrations);
		cfg_mutex.unlock();

		ret = 0;
	} catch(const pqxx::pqxx_exception &e){
		ERROR("pqxx_exception: %s ",e.base().what());
	}
	return ret;
}

int Registration::configure(AmConfigReader &cfg){

	db_schema = Yeti::instance()->config.db_schema;
	configure_db(cfg);

	check_interval = cfg.getParameterInt("reg_check_interval",CHECK_INTERVAL_DEFAULT);

	if(load_registrations()){
		ERROR("can't load registrations");
		return -1;
	}

	return 0;
}

int Registration::reload(AmConfigReader &cfg){
	//remove old registrations
	clean_registrations();
	//add new
	return configure(cfg);
}

void Registration::create_registration(RegInfo& ri){
	AmDynInvokeFactory* di_f = AmPlugIn::instance()->getFactory4Di("registrar_client");
	if (di_f == NULL) {
		ERROR("unable to get a registrar_client\n");
	} else {
		AmDynInvoke* registrar_client_i = di_f->getInstance();
		if (registrar_client_i!=NULL) {
			DBG("calling createRegistration\n");
			AmArg di_args, reg_handle;
			di_args.push(ri.domain.c_str());
			di_args.push(ri.user.c_str());
			di_args.push(ri.display_name.c_str());	// display name
			di_args.push(ri.auth_user.c_str());		// auth_user
			di_args.push(ri.passwd.c_str());		// pwd
			di_args.push("");						//!TODO: implement AmSipRegistration events processing
			di_args.push(ri.proxy.c_str());
			di_args.push(ri.contact.c_str());
			registrar_client_i->invoke("createRegistration", di_args, reg_handle);
			if (reg_handle.size())
				ri.handle = reg_handle.get(0).asCStr();
		}
	}
}

bool Registration::check_registration(RegInfo& ri){
	if (!ri.handle.length())
		return false;
	AmDynInvokeFactory* di_f = AmPlugIn::instance()->getFactory4Di("registrar_client");
	if (di_f == NULL) {
		ERROR("unable to get a registrar_client\n");
		return true; //avoid create_registration calling
	} else {
		AmDynInvoke* registrar_client_i = di_f->getInstance();
		if (registrar_client_i!=NULL) {
			AmArg di_args, res;
			di_args.push(ri.handle.c_str());
			registrar_client_i->invoke("getRegistrationState", di_args, res);
			if (res.size()) {
				if (!res.get(0).asInt())
					return false; // does not exist
				int state = res.get(1).asInt();
				int expires = res.get(2).asInt();
				/*DBG("Got state %s with expires %us for registration %d.\n",
					getSIPRegistationStateString(state), expires,ri.id);*/
				ri.state = state;
				ri.expires = expires;
				if (state == AmSIPRegistration::RegisterExpired)
					return false;
				// else pending or active
				return true;
			}
		}
	}
	return false;
}

void Registration::remove_registration(RegInfo& ri){
	if (!ri.handle.length())
		return;
	AmDynInvokeFactory* di_f = AmPlugIn::instance()->getFactory4Di("registrar_client");
	if (di_f == NULL) {
		ERROR("unable to get a registrar_client\n");
	} else {
		AmDynInvoke* registrar_client_i = di_f->getInstance();
		if (registrar_client_i!=NULL) {
			AmArg di_args, res;
			di_args.push(ri.handle.c_str());
			registrar_client_i->invoke("removeRegistration", di_args, res);
		}
	}
}

void Registration::list_registrations(AmArg &ret){
	cfg_mutex.lock();
	for (vector<RegInfo>::iterator it = registrations.begin(); it != registrations.end(); it++) {
		AmArg r;
		reg2arg(*it,r);
		ret.push(r);
	}
	cfg_mutex.unlock();
}

long Registration::get_registrations_count(){
	long ret;
	cfg_mutex.lock();
		ret = registrations.size();
	cfg_mutex.unlock();
	return ret;
}

bool Registration::get_registration_info(int reg_id,AmArg &reg){
	cfg_mutex.lock();
	for (vector<RegInfo>::iterator it = registrations.begin();
			it != registrations.end(); it++) {
		if(it->id==reg_id){
			reg2arg(*it,reg);
			cfg_mutex.unlock();
			return true;
		}
	}
	cfg_mutex.unlock();
	return false;
}

void Registration::clean_registrations(){
	cfg_mutex.lock();
	for (vector<RegInfo>::iterator it = registrations.begin(); it != registrations.end(); it++) {
		remove_registration(*it);
	}
	registrations.clear();
	cfg_mutex.unlock();
}

void Registration::on_stop(){
	stopped.set(true);
}

void Registration::run(){
	setThreadName("yeti-reg");
	sleep(2);
	while (true) {
		cfg_mutex.lock();
		for (vector<RegInfo>::iterator it = registrations.begin(); it != registrations.end(); it++) {
			if (!check_registration(*it)) {
				DBG("Registration %d does not exist or timeout. Creating registration.\n",it->id);
				create_registration(*it);
			}
		}
		cfg_mutex.unlock();
		stopped.wait_for_to(check_interval);
		if(stopped.get())
			break;
	}
	clean_registrations();
}
