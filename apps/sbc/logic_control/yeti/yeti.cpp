#include "yeti.h"
#include "sdp_filter.h"

#include <string.h>
#include <ctime>

#include "log.h"
#include "AmPlugIn.h"
#include "AmArg.h"
#include "AmSession.h"
#include "AmUtils.h"
#include "AmAudioFile.h"
#include "AmMediaProcessor.h"
#include "SDPFilter.h"
#include "CallLeg.h"
#include "Version.h"
#include "RegisterDialog.h"
#include "Registration.h"
#include "cdr/TrustedHeaders.h"
#include "SBC.h"
struct CallLegCreator;

class YetiFactory : public AmDynInvokeFactory
{
public:
	YetiFactory(const string& name)
		: AmDynInvokeFactory(name) {}

	~YetiFactory(){
		//DBG("~YetiFactory()");
		delete Yeti::instance();
	}

	AmDynInvoke* getInstance(){
		return Yeti::instance();
	}

	int onLoad(){
		if (Yeti::instance()->onLoad())
			return -1;
		DBG("logic control loaded.\n");
		return 0;
	}

};

EXPORT_PLUGIN_CLASS_FACTORY(YetiFactory, MOD_NAME);

Yeti* Yeti::_instance=0;

Yeti* Yeti::instance() {
	if(!_instance)
		_instance = new Yeti();
	return _instance;
}

Yeti::Yeti():
	router(new SqlRouter())
{
	routers.insert(router);
	//DBG("Yeti()");
}


Yeti::~Yeti() {
	//DBG("~Yeti()");
	router->release(routers);
	Registration::instance()->stop();
	rctl.stop();
}

bool Yeti::read_config(){
	if(cfg.loadFile(AmConfig::ModConfigPath + string(MOD_NAME ".conf"))) {
		ERROR("No configuration for "MOD_NAME" present (%s)\n",
			(AmConfig::ModConfigPath + string(MOD_NAME ".conf")).c_str());
		return false;
	}

	config.node_id = cfg.getParameterInt("node_id",-1);
	if(-1 == config.node_id){
		ERROR("Missed parameter 'node_id'");
		return false;
	}

	config.pop_id = cfg.getParameterInt("pop_id",-1);
	if(-1 == config.pop_id){
		ERROR("Missed parameter 'pop_id'");
		return false;
	}

	if(!cfg.hasParameter("routing_schema")){
		ERROR("Missed parameter 'routing_schema'");
		return false;
	}
	config.routing_schema = cfg.getParameter("routing_schema");

	if(!cfg.hasParameter("msg_logger_dir")){
		ERROR("Missed parameter 'msg_logger_dir'");
		return false;
	}
	config.msg_logger_dir = cfg.getParameter("msg_logger_dir");

	//check permissions for logger dir
	ofstream st;
	string testfile = config.msg_logger_dir + "/test";
	st.open(testfile.c_str(),std::ofstream::out | std::ofstream::trunc);
	if(!st.is_open()){
		ERROR("can't write test file in '%s' directory",config.msg_logger_dir.c_str());
		return false;
	}
	remove(testfile.c_str());


	return true;
}

int Yeti::onLoad() {
	if(!read_config()){
		return -1;
	}

	calls_show_limit = cfg.getParameterInt("calls_show_limit",100);

	self_iface.cc_module = "yeti";
	self_iface.cc_name = "yeti";
	self_iface.cc_values.clear();

	profile = new SBCCallProfile();
	string profile_file_name = AmConfig::ModConfigPath + "oodprofile.yeti.conf";
	if(!profile->readFromConfiguration("transparent",profile_file_name)){
		ERROR("can't read profile for OoD requests '%s'",profile_file_name.c_str());
		return -1;
	}
	profile->cc_vars.clear();
	profile->cc_interfaces.clear();

	DBG("p = %p",profile);
	if(rctl.configure(cfg)){
		ERROR("ResourceControl configure failed");
		return -1;
	}
	rctl.start();

	if(TrustedHeaders::instance()->configure(cfg)){
		ERROR("TrustedHeaders configure failed");
		return -1;
	}

	if(CodecsGroups::instance()->configure(cfg)){
		ERROR("CodecsGroups configure failed");
		return -1;
	}

	if (CodesTranslator::instance()->configure(cfg)){
		ERROR("CodesTranslator configure failed");
		return -1;
	}

	if (router->configure(cfg)){
		ERROR("SqlRouter confgiure failed");
		return -1;
	}

	if(router->run()){
		ERROR("SqlRouter start failed");
		return -1;
	}

	if(Registration::instance()->configure(cfg)){
		ERROR("Registration agent configure failed");
		return -1;
	}
	Registration::instance()->start();

	start_time = time(NULL);

	init_xmlrpc_cmds();

	return 0;
}

